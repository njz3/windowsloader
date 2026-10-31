// windowsloader - runs Windows arcade games with emulated cabinet hardware
// Copyright (C) 2026 Benjamin Maurin - njz3
// License: GNU General Public License version 3 (GPL v3)
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE
// file for the full text. The author reserves the right to change the license of this code.
//
// Code taken from MAME, linuxloader, sm2-emu, Flycast or segatools stays under the license of
// its own project; such code is marked with its origin.

// Observer for the rest of the system: DLLs, dynamic imports, mutexes (a game often checks that
// its cabinet shell runs), debug output, message boxes. Also guards the PC the game runs on:
// an arcade game expects to own its machine, so it may start programs, shut Windows down or
// change system settings. Those calls are logged and refused.

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <strmif.h>
#include <stdio.h>

struct sockaddr; // read as bytes: winsock's headers are not needed

#include "hooks.h"
#include "ioports.h"
#include "log.h"
#include "outputs.h"
#include "settings.h"

static const char *where_of(const void *caller, char *buf, size_t size)
{
    return wl_where(caller, buf, size);
}

// ------------------------------------------------------------------------------
// DLLs and dynamic imports
// ------------------------------------------------------------------------------

static void on_load(const char *name, HMODULE mod, DWORD err, const void *caller)
{
    if (mod)
        wl_hooks_install_pending(); // hooks waiting for this DLL
    if (wl_is_system_caller(caller))
        return;
    char where[128];
    if (mod)
        wl_log("LoadLibrary \"%s\" -> %p [%s]", name, mod, where_of(caller, where, sizeof where));
    else
        wl_log("LoadLibrary \"%s\" -> failed (error %lu) [%s]", name, err, where_of(caller, where, sizeof where));
}

// inpout32 and its kin reach the PC's I/O ports through a kernel driver: the game gets
// windowsloader's emulated ports instead (ioports.cpp)
static HMODULE port_driver(const char *name, const void *caller)
{
    char where[128];
    HMODULE mod = wl_ioports_module();
    wl_log("LoadLibrary \"%s\" -> %p, emulated I/O ports (the real DLL is not loaded) [%s]", name, mod, where_of(caller, where, sizeof where));
    return mod;
}

// A DLL refused by the game's module (wl_system_refuse_dll)
static char g_refusedDll[64];

void wl_system_refuse_dll(const char *name)
{
    snprintf(g_refusedDll, sizeof g_refusedDll, "%s", name);
}

static int refused_dll(const char *path, const void *caller)
{
    if (!g_refusedDll[0] || !path)
        return 0;
    const char *base = strrchr(path, '\\');
    const char *slash = strrchr(path, '/');
    if (slash && (!base || slash > base))
        base = slash;
    base = base ? base + 1 : path;
    if (_stricmp(base, g_refusedDll))
        return 0;
    char where[128];
    wl_log("LoadLibrary \"%s\" refused by the game's module [%s]", path, where_of(caller, where, sizeof where));
    SetLastError(ERROR_MOD_NOT_FOUND);
    return 1;
}

// A DLL the game's module plays itself (wl_system_virtual_dll): a handle of ours for it, its
// functions given by GetProcAddress
#define VIRTUAL_DLL ((HMODULE)(ULONG_PTR)0x5EF00000u)
static char g_virtualDll[64];
static const WlVirtualDllFunction *g_virtualFns;
static int g_virtualCount;

void wl_system_virtual_dll(const char *name, const WlVirtualDllFunction *fns, int count)
{
    snprintf(g_virtualDll, sizeof g_virtualDll, "%s", name);
    g_virtualFns = fns;
    g_virtualCount = count;
}

static HMODULE virtual_dll(const char *path, const void *caller)
{
    if (!g_virtualDll[0] || !path)
        return NULL;
    const char *base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    if (_stricmp(base, g_virtualDll))
        return NULL;
    char where[128];
    wl_log("LoadLibrary \"%s\": played by windowsloader [%s]", path, where_of(caller, where, sizeof where));
    return VIRTUAL_DLL;
}

static FARPROC virtual_proc(LPCSTR name)
{
    for (int i = 0; i < g_virtualCount; i++)
        if (IS_INTRESOURCE(name) ? g_virtualFns[i].ordinal == (unsigned)(ULONG_PTR)name : !strcmp(g_virtualFns[i].name, name))
            return (FARPROC)g_virtualFns[i].fn;
    SetLastError(ERROR_PROC_NOT_FOUND);
    return NULL;
}

static HMODULE(WINAPI *oLoadLibraryExW)(LPCWSTR, HANDLE, DWORD);

// A DLL named with its folder, which the game wrote there with a write folder (WRITE_DIR): loaded
// from the write folder (files.cpp). The ANSI calls go through the W one, the write folder's path
// may be long (\\?\). NULL when it is not there
static const wchar_t *written_dll(const char *name, wchar_t *w, wchar_t *buf)
{
    if (!name || (!strchr(name, '\\') && !strchr(name, '/')) || !MultiByteToWideChar(CP_ACP, 0, name, -1, w, MAX_PATH * 2))
        return NULL;
    const wchar_t *mapped = wl_files_read_path(w, buf, MAX_PATH * 2);
    return mapped == w ? NULL : mapped;
}

static HMODULE(WINAPI *oLoadLibraryA)(LPCSTR);
static HMODULE WINAPI hLoadLibraryA(LPCSTR name)
{
    int nested = wl_busy();
    if (!nested && wl_ioports_is_driver_dll(name) && !wl_is_system_caller(WL_CALLER))
        return port_driver(name, WL_CALLER);
    HMODULE v = nested ? NULL : virtual_dll(name, WL_CALLER);
    if (v)
        return v;
    if (!nested && refused_dll(name, WL_CALLER))
        return NULL;
    wchar_t w[MAX_PATH * 2], wbuf[MAX_PATH * 2];
    const wchar_t *written = nested ? NULL : written_dll(name, w, wbuf);
    wl_busy_enter();
    HMODULE mod = written ? oLoadLibraryExW(written, NULL, 0) : oLoadLibraryA(name);
    wl_busy_leave();
    DWORD err = GetLastError();
    if (!nested && name)
        on_load(name, mod, err, WL_CALLER);
    SetLastError(err);
    return mod;
}

static HMODULE(WINAPI *oLoadLibraryW)(LPCWSTR);
static HMODULE WINAPI hLoadLibraryW(LPCWSTR name)
{
    int nested = wl_busy();
    char n8[MAX_PATH * 2];
    if (!nested && name && wl_ioports_is_driver_dll(wl_u8(name, n8, sizeof n8)) && !wl_is_system_caller(WL_CALLER))
        return port_driver(n8, WL_CALLER);
    HMODULE v = nested || !name ? NULL : virtual_dll(wl_u8(name, n8, sizeof n8), WL_CALLER);
    if (v)
        return v;
    if (!nested && name && refused_dll(wl_u8(name, n8, sizeof n8), WL_CALLER))
        return NULL;
    wchar_t wbuf[MAX_PATH * 2];
    const wchar_t *path = nested ? name : wl_files_read_path(name, wbuf, MAX_PATH * 2);
    wl_busy_enter();
    HMODULE mod = oLoadLibraryW(path);
    wl_busy_leave();
    DWORD err = GetLastError();
    if (!nested && name)
    {
        char n[MAX_PATH * 2];
        on_load(wl_u8(name, n, sizeof n), mod, err, WL_CALLER);
    }
    SetLastError(err);
    return mod;
}

static HMODULE(WINAPI *oLoadLibraryExA)(LPCSTR, HANDLE, DWORD);
static HMODULE WINAPI hLoadLibraryExA(LPCSTR name, HANDLE file, DWORD flags)
{
    int nested = wl_busy();
    if (!nested && wl_ioports_is_driver_dll(name) && !wl_is_system_caller(WL_CALLER))
        return port_driver(name, WL_CALLER);
    HMODULE v = nested ? NULL : virtual_dll(name, WL_CALLER);
    if (v)
        return v;
    if (!nested && refused_dll(name, WL_CALLER))
        return NULL;
    wchar_t w[MAX_PATH * 2], wbuf[MAX_PATH * 2];
    const wchar_t *written = nested ? NULL : written_dll(name, w, wbuf);
    wl_busy_enter();
    HMODULE mod = written ? oLoadLibraryExW(written, file, flags) : oLoadLibraryExA(name, file, flags);
    wl_busy_leave();
    DWORD err = GetLastError();
    if (!nested && name)
        on_load(name, mod, err, WL_CALLER);
    SetLastError(err);
    return mod;
}

static HMODULE WINAPI hLoadLibraryExW(LPCWSTR name, HANDLE file, DWORD flags)
{
    int nested = wl_busy();
    char n8[MAX_PATH * 2];
    if (!nested && name && wl_ioports_is_driver_dll(wl_u8(name, n8, sizeof n8)) && !wl_is_system_caller(WL_CALLER))
        return port_driver(n8, WL_CALLER);
    HMODULE v = nested || !name ? NULL : virtual_dll(wl_u8(name, n8, sizeof n8), WL_CALLER);
    if (v)
        return v;
    if (!nested && name && refused_dll(wl_u8(name, n8, sizeof n8), WL_CALLER))
        return NULL;
    wchar_t wbuf[MAX_PATH * 2];
    const wchar_t *path = nested ? name : wl_files_read_path(name, wbuf, MAX_PATH * 2);
    wl_busy_enter();
    HMODULE mod = oLoadLibraryExW(path, file, flags);
    wl_busy_leave();
    DWORD err = GetLastError();
    if (!nested && name)
    {
        char n[MAX_PATH * 2];
        on_load(wl_u8(name, n, sizeof n), mod, err, WL_CALLER);
    }
    SetLastError(err);
    return mod;
}

// True the first time a module/function pair is asked for. Old CRTs look EncodePointer up
// again on every call; each name is worth one line.
static int first_lookup(HMODULE mod, LPCSTR name)
{
    static ULONG seen[1024];
    static int count;
    static SRWLOCK lock = SRWLOCK_INIT;

    ULONG h = 2166136261u ^ (ULONG)(ULONG_PTR)mod;
    if (IS_INTRESOURCE(name))
        h = (h ^ (ULONG)(ULONG_PTR)name) * 16777619u;
    else
        for (const char *p = name; *p; p++)
            h = (h ^ (unsigned char)*p) * 16777619u;

    int first = 1;
    AcquireSRWLockExclusive(&lock);
    for (int i = 0; i < count && first; i++)
        first = seen[i] != h;
    if (first && count < (int)(sizeof seen / sizeof seen[0]))
        seen[count++] = h;
    ReleaseSRWLockExclusive(&lock);
    return first;
}

static FARPROC(WINAPI *oGetProcAddress)(HMODULE, LPCSTR);
static FARPROC WINAPI hGetProcAddress(HMODULE mod, LPCSTR name)
{
    int nested = wl_busy();
    if (mod == VIRTUAL_DLL && g_virtualCount)
        return virtual_proc(name);
    FARPROC port = nested ? NULL : wl_ioports_proc(mod, name);
    if (port)
    {
        wl_log("GetProcAddress %s -> emulated I/O ports", name);
        return port;
    }
    wl_busy_enter();
    FARPROC p = oGetProcAddress(mod, name);
    wl_busy_leave();
    DWORD err = GetLastError();
    if (!nested && !wl_is_system_caller(WL_CALLER) && first_lookup(mod, name))
    {
        wchar_t path[MAX_PATH] = L"?";
        GetModuleFileNameW(mod, path, MAX_PATH);
        const wchar_t *base = wcsrchr(path, L'\\');
        char m[MAX_PATH], ord[16], where[128];
        if (IS_INTRESOURCE(name))
            snprintf(ord, sizeof ord, "#%u", (unsigned)(ULONG_PTR)name);
        wl_log("GetProcAddress %s!%s -> %s [%s]", wl_u8(base ? base + 1 : path, m, sizeof m), IS_INTRESOURCE(name) ? ord : name, p ? "ok" : "missing",
               where_of(WL_CALLER, where, sizeof where));
    }
    SetLastError(err);
    return p;
}

// ------------------------------------------------------------------------------
// Mutexes
// ------------------------------------------------------------------------------

static void on_mutex(const char *what, const char *name, HANDLE h, DWORD err, const void *caller)
{
    char where[128];
    wl_log("%s \"%s\" -> %s [%s]", what, name, !h ? "failed" : err == ERROR_ALREADY_EXISTS ? "already exists" : "ok", where_of(caller, where, sizeof where));
}

static HANDLE(WINAPI *oCreateMutexA)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
static HANDLE WINAPI hCreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCSTR name)
{
    int nested = wl_busy();
    wl_busy_enter();
    HANDLE h = oCreateMutexA(sa, owner, name);
    wl_busy_leave();
    DWORD err = GetLastError();
    if (!nested && name && !wl_is_system_caller(WL_CALLER))
        on_mutex("CreateMutex", name, h, err, WL_CALLER);
    SetLastError(err);
    return h;
}

static HANDLE(WINAPI *oCreateMutexW)(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR);
static HANDLE WINAPI hCreateMutexW(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCWSTR name)
{
    int nested = wl_busy();
    wl_busy_enter();
    HANDLE h = oCreateMutexW(sa, owner, name);
    wl_busy_leave();
    DWORD err = GetLastError();
    if (!nested && name && !wl_is_system_caller(WL_CALLER))
    {
        char n[256];
        on_mutex("CreateMutex", wl_u8(name, n, sizeof n), h, err, WL_CALLER);
    }
    SetLastError(err);
    return h;
}

static HANDLE(WINAPI *oOpenMutexA)(DWORD, BOOL, LPCSTR);
static HANDLE WINAPI hOpenMutexA(DWORD access, BOOL inherit, LPCSTR name)
{
    int nested = wl_busy();
    wl_busy_enter();
    HANDLE h = oOpenMutexA(access, inherit, name);
    wl_busy_leave();
    DWORD err = GetLastError();
    if (!nested && name && !wl_is_system_caller(WL_CALLER))
        on_mutex("OpenMutex", name, h, err, WL_CALLER);
    SetLastError(err);
    return h;
}

static HANDLE(WINAPI *oOpenMutexW)(DWORD, BOOL, LPCWSTR);
static HANDLE WINAPI hOpenMutexW(DWORD access, BOOL inherit, LPCWSTR name)
{
    int nested = wl_busy();
    wl_busy_enter();
    HANDLE h = oOpenMutexW(access, inherit, name);
    wl_busy_leave();
    DWORD err = GetLastError();
    if (!nested && name && !wl_is_system_caller(WL_CALLER))
    {
        char n[256];
        on_mutex("OpenMutex", wl_u8(name, n, sizeof n), h, err, WL_CALLER);
    }
    SetLastError(err);
    return h;
}

// ------------------------------------------------------------------------------
// Programs started by the game: refused. The game may try a shutdown or its cabinet shell;
// nothing it asks for reaches cmd.exe. A cabinet helper the game module emulates is handed to
// it first (wl_system_set_process_handler).
// ------------------------------------------------------------------------------

static WlProcessHandler g_processHandler;

void wl_system_set_process_handler(WlProcessHandler fn)
{
    g_processHandler = fn;
}

static void process_refused(const char *app, const char *cmd, const char *dir, const void *caller)
{
    char where[128];
    if (g_processHandler && g_processHandler(app ? app : "", cmd ? cmd : ""))
        wl_log("CreateProcess \"%s\" \"%s\": played by windowsloader, not started [%s]", app ? app : "", cmd ? cmd : "",
               where_of(caller, where, sizeof where));
    else
        wl_log("CreateProcess refused: app=\"%s\" cmd=\"%s\" dir=\"%s\" [%s]", app ? app : "", cmd ? cmd : "", dir ? dir : "",
               where_of(caller, where, sizeof where));
    SetLastError(ERROR_ACCESS_DENIED);
}

static BOOL(WINAPI *oCreateProcessA)(LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCSTR, LPSTARTUPINFOA,
                                     LPPROCESS_INFORMATION);
static BOOL WINAPI hCreateProcessA(LPCSTR app, LPSTR cmd, LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags, LPVOID env,
                                   LPCSTR dir, LPSTARTUPINFOA si, LPPROCESS_INFORMATION pi)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oCreateProcessA(app, cmd, pa, ta, inherit, flags, env, dir, si, pi);
    process_refused(app, cmd, dir, WL_CALLER);
    return FALSE;
}

static BOOL(WINAPI *oCreateProcessW)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW,
                                     LPPROCESS_INFORMATION);
static BOOL WINAPI hCreateProcessW(LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags, LPVOID env,
                                   LPCWSTR dir, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oCreateProcessW(app, cmd, pa, ta, inherit, flags, env, dir, si, pi);
    char a[MAX_PATH * 2], c[1024], d[MAX_PATH * 2];
    process_refused(app ? wl_u8(app, a, sizeof a) : NULL, cmd ? wl_u8(cmd, c, sizeof c) : NULL, dir ? wl_u8(dir, d, sizeof d) : NULL, WL_CALLER);
    return FALSE;
}

static UINT(WINAPI *oWinExec)(LPCSTR, UINT);
static UINT WINAPI hWinExec(LPCSTR cmd, UINT show)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oWinExec(cmd, show);
    char where[128];
    wl_log("WinExec refused: \"%s\" [%s]", cmd ? cmd : "", where_of(WL_CALLER, where, sizeof where));
    return ERROR_FILE_NOT_FOUND;
}

static HINSTANCE(WINAPI *oShellExecuteA)(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT);
static HINSTANCE WINAPI hShellExecuteA(HWND wnd, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir, INT show)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oShellExecuteA(wnd, op, file, params, dir, show);
    char where[128];
    wl_log("ShellExecute refused: op=\"%s\" file=\"%s\" params=\"%s\" dir=\"%s\" [%s]", op ? op : "", file ? file : "", params ? params : "",
           dir ? dir : "", where_of(WL_CALLER, where, sizeof where));
    return (HINSTANCE)(INT_PTR)SE_ERR_ACCESSDENIED;
}

static HINSTANCE(WINAPI *oShellExecuteW)(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT);
static HINSTANCE WINAPI hShellExecuteW(HWND wnd, LPCWSTR op, LPCWSTR file, LPCWSTR params, LPCWSTR dir, INT show)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oShellExecuteW(wnd, op, file, params, dir, show);
    char o[64], f[MAX_PATH * 2], p[1024], where[128];
    wl_log("ShellExecute refused: op=\"%s\" file=\"%s\" params=\"%s\" [%s]", op ? wl_u8(op, o, sizeof o) : "", file ? wl_u8(file, f, sizeof f) : "",
           params ? wl_u8(params, p, sizeof p) : "", where_of(WL_CALLER, where, sizeof where));
    return (HINSTANCE)(INT_PTR)SE_ERR_ACCESSDENIED;
}

static BOOL(WINAPI *oShellExecuteExA)(SHELLEXECUTEINFOA *);
static BOOL WINAPI hShellExecuteExA(SHELLEXECUTEINFOA *info)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oShellExecuteExA(info);
    char where[128];
    wl_log("ShellExecuteEx refused: file=\"%s\" params=\"%s\" [%s]", info && info->lpFile ? info->lpFile : "",
           info && info->lpParameters ? info->lpParameters : "", where_of(WL_CALLER, where, sizeof where));
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

static BOOL(WINAPI *oShellExecuteExW)(SHELLEXECUTEINFOW *);
static BOOL WINAPI hShellExecuteExW(SHELLEXECUTEINFOW *info)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oShellExecuteExW(info);
    char f[MAX_PATH * 2], p[1024], where[128];
    wl_log("ShellExecuteEx refused: file=\"%s\" params=\"%s\" [%s]", info && info->lpFile ? wl_u8(info->lpFile, f, sizeof f) : "",
           info && info->lpParameters ? wl_u8(info->lpParameters, p, sizeof p) : "", where_of(WL_CALLER, where, sizeof where));
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

// Shutdown, restart, sleep: never from a game running on someone's PC
static BOOL(WINAPI *oExitWindowsEx)(UINT, DWORD);
static BOOL WINAPI hExitWindowsEx(UINT flags, DWORD reason)
{
    wl_log("ExitWindowsEx(0x%x) refused", flags);
    return TRUE;
}

static BOOL(WINAPI *oInitiateSystemShutdownExA)(LPSTR, LPSTR, DWORD, BOOL, BOOL, DWORD);
static BOOL WINAPI hInitiateSystemShutdownExA(LPSTR machine, LPSTR msg, DWORD timeout, BOOL force, BOOL reboot, DWORD reason)
{
    wl_log("InitiateSystemShutdownEx (reboot=%d) refused", reboot);
    return TRUE;
}

static BOOL(WINAPI *oInitiateSystemShutdownExW)(LPWSTR, LPWSTR, DWORD, BOOL, BOOL, DWORD);
static BOOL WINAPI hInitiateSystemShutdownExW(LPWSTR machine, LPWSTR msg, DWORD timeout, BOOL force, BOOL reboot, DWORD reason)
{
    wl_log("InitiateSystemShutdownEx (reboot=%d) refused", reboot);
    return TRUE;
}

static BOOLEAN(WINAPI *oSetSuspendState)(BOOLEAN, BOOLEAN, BOOLEAN);
static BOOLEAN WINAPI hSetSuspendState(BOOLEAN hibernate, BOOLEAN force, BOOLEAN wakeup)
{
    wl_log("SetSuspendState (hibernate=%d) refused", hibernate);
    return TRUE;
}

// The PC's clock: a cabinet shell sets it from its service menu
static BOOL(WINAPI *oSetLocalTime)(const SYSTEMTIME *);
static BOOL WINAPI hSetLocalTime(const SYSTEMTIME *t)
{
    char where[128];
    if (t)
        wl_log("SetLocalTime %04u-%02u-%02u %02u:%02u:%02u refused [%s]", t->wYear, t->wMonth, t->wDay, t->wHour, t->wMinute, t->wSecond,
               where_of(WL_CALLER, where, sizeof where));
    return TRUE;
}

static BOOL(WINAPI *oSetSystemTime)(const SYSTEMTIME *);
static BOOL WINAPI hSetSystemTime(const SYSTEMTIME *t)
{
    char where[128];
    if (t)
        wl_log("SetSystemTime %04u-%02u-%02u %02u:%02u:%02u refused [%s]", t->wYear, t->wMonth, t->wDay, t->wHour, t->wMinute, t->wSecond,
               where_of(WL_CALLER, where, sizeof where));
    return TRUE;
}

static BOOL(WINAPI *oSetTimeZoneInformation)(const TIME_ZONE_INFORMATION *);
static BOOL WINAPI hSetTimeZoneInformation(const TIME_ZONE_INFORMATION *tz)
{
    char where[128];
    wl_log("SetTimeZoneInformation (bias %ld min) refused [%s]", tz ? tz->Bias : 0, where_of(WL_CALLER, where, sizeof where));
    return TRUE;
}

// ------------------------------------------------------------------------------
// The machine's registry (HKEY_LOCAL_MACHINE): read only for the game. A cabinet shell writes
// there the network card's address, for one; the keys it opens under HKLM are given without
// their write rights, so its writes fail as they would for a user who is not administrator.
// The keys opened under one of those are followed too (a small table of handles). The other
// roots that are not the user's own (HKEY_CLASSES_ROOT, where COM classes are registered;
// HKEY_USERS; HKEY_CURRENT_CONFIG) are treated the same. Deleting a key is refused everywhere,
// a value under those roots too (Valve Limit R links ATL's registrar, RegDeleteKey and all).
// ------------------------------------------------------------------------------

// Keys a game's module makes up under HKEY_LOCAL_MACHINE (wl_registry_add_key): an ALLS cabinet's
// SYSTEM\SEGA\SystemProperty. Opened by path from HKLM or from one of them, their handles are
// ours (never the registry's); values read and written in memory, for this run only.
#define VKEYS 16
#define VVALUES 24

struct VValue
{
    char name[48];
    DWORD type;
    unsigned char data[256];
    DWORD size;
};

static struct
{
    char path[160];
    VValue values[VVALUES];
    int count;
} g_vkeys[VKEYS];
static int g_vkeyCount;
static CRITICAL_SECTION g_vkeyLock;

#define VKEY_BASE 0x5EE00000u

void wl_registry_add_key(const char *path, const WlRegValue *values, int count)
{
    if (g_vkeyCount >= VKEYS)
        return;
    if (!g_vkeyCount)
        InitializeCriticalSection(&g_vkeyLock);
    snprintf(g_vkeys[g_vkeyCount].path, sizeof g_vkeys[0].path, "%s", path);
    for (int i = 0; i < count && i < VVALUES; i++)
    {
        VValue *v = &g_vkeys[g_vkeyCount].values[i];
        snprintf(v->name, sizeof v->name, "%s", values[i].name);
        v->type = values[i].type;
        v->size = values[i].type == REG_SZ ? (DWORD)strlen((const char *)values[i].data) + 1 : values[i].size;
        if (v->size > sizeof v->data)
            v->size = sizeof v->data;
        memcpy(v->data, values[i].data, v->size);
        g_vkeys[g_vkeyCount].count++;
    }
    wl_log("registry: HKLM\\%s made up (%d values)", path, count);
    g_vkeyCount++;
}

static int vkey_of(HKEY key)
{
    ULONG_PTR k = (ULONG_PTR)key;
    if ((k & 0xFFFF0000u) != VKEY_BASE || (k & 3) || (int)((k & 0xFFFF) >> 2) >= g_vkeyCount)
        return -1;
    return (int)((k & 0xFFFF) >> 2);
}

// The key at that path, from HKLM or from one of ours; -1 when not one of ours
static int vkey_find(HKEY key, const char *sub)
{
    if (!g_vkeyCount || (key != HKEY_LOCAL_MACHINE && vkey_of(key) < 0))
        return -1;
    char path[256];
    if (key == HKEY_LOCAL_MACHINE)
        snprintf(path, sizeof path, "%s", sub ? sub : "");
    else
        snprintf(path, sizeof path, "%s%s%s", g_vkeys[vkey_of(key)].path, sub && *sub ? "\\" : "", sub ? sub : "");
    size_t n = strlen(path);
    while (n && path[n - 1] == '\\')
        path[--n] = 0;
    for (int i = 0; i < g_vkeyCount; i++)
        if (!_stricmp(g_vkeys[i].path, path))
            return i;
    return -1;
}

static LSTATUS vkey_open(int i, PHKEY result, LPDWORD disposition)
{
    if (result)
        *result = (HKEY)(ULONG_PTR)(VKEY_BASE | (unsigned)i << 2);
    if (disposition)
        *disposition = REG_OPENED_EXISTING_KEY;
    return ERROR_SUCCESS;
}

// A value read: REG_SZ kept in 8 bits, given in 16 to the W functions
static LSTATUS vkey_query(int k, const char *name, LPDWORD type, LPBYTE data, LPDWORD size, int wide)
{
    EnterCriticalSection(&g_vkeyLock);
    const VValue *v = NULL;
    for (int i = 0; i < g_vkeys[k].count; i++)
        if (!_stricmp(g_vkeys[k].values[i].name, name ? name : ""))
            v = &g_vkeys[k].values[i];
    LSTATUS r = ERROR_FILE_NOT_FOUND;
    if (v)
    {
        DWORD need = v->type == REG_SZ && wide ? v->size * 2 : v->size;
        if (type)
            *type = v->type;
        r = ERROR_SUCCESS;
        if (data && size && *size < need)
            r = ERROR_MORE_DATA;
        else if (data && v->type == REG_SZ && wide)
            MultiByteToWideChar(CP_ACP, 0, (const char *)v->data, (int)v->size, (wchar_t *)data, (int)v->size);
        else if (data)
            memcpy(data, v->data, v->size);
        if (size)
            *size = need;
    }
    LeaveCriticalSection(&g_vkeyLock);
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 200)
        wl_log("registry: HKLM\\%s %s read -> %s", g_vkeys[k].path, name ? name : "(default)", r == ERROR_SUCCESS ? "ok" : r == ERROR_MORE_DATA ? "more data" : "none");
    return r;
}

static LSTATUS vkey_set(int k, const char *name, DWORD type, const BYTE *data, DWORD size, int wide)
{
    EnterCriticalSection(&g_vkeyLock);
    VValue *v = NULL;
    for (int i = 0; i < g_vkeys[k].count; i++)
        if (!_stricmp(g_vkeys[k].values[i].name, name ? name : ""))
            v = &g_vkeys[k].values[i];
    if (!v && g_vkeys[k].count < VVALUES)
    {
        v = &g_vkeys[k].values[g_vkeys[k].count++];
        snprintf(v->name, sizeof v->name, "%s", name ? name : "");
    }
    if (v)
    {
        v->type = type;
        if (type == REG_SZ && wide)
            v->size = (DWORD)WideCharToMultiByte(CP_ACP, 0, (const wchar_t *)data, (int)(size / 2), (char *)v->data, sizeof v->data, NULL, NULL);
        else
        {
            v->size = size < sizeof v->data ? size : sizeof v->data;
            memcpy(v->data, data, v->size);
        }
    }
    LeaveCriticalSection(&g_vkeyLock);
    wl_log("registry: HKLM\\%s %s written (type %lu, %lu bytes, kept for this run)", g_vkeys[k].path, name ? name : "(default)", type, size);
    return ERROR_SUCCESS;
}

static LSTATUS(WINAPI *oRegQueryValueExA)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
static LSTATUS WINAPI hRegQueryValueExA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size)
{
    int k = vkey_of(key);
    if (k < 0)
        return oRegQueryValueExA(key, name, reserved, type, data, size);
    return vkey_query(k, name, type, data, size, 0);
}

static LSTATUS(WINAPI *oRegQueryValueExW)(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
static LSTATUS WINAPI hRegQueryValueExW(HKEY key, LPCWSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size)
{
    int k = vkey_of(key);
    if (k < 0)
        return oRegQueryValueExW(key, name, reserved, type, data, size);
    char n[64];
    return vkey_query(k, name ? wl_u8(name, n, sizeof n) : NULL, type, data, size, 1);
}

static LSTATUS(WINAPI *oRegSetValueExA)(HKEY, LPCSTR, DWORD, DWORD, const BYTE *, DWORD);
static LSTATUS WINAPI hRegSetValueExA(HKEY key, LPCSTR name, DWORD reserved, DWORD type, const BYTE *data, DWORD size)
{
    int k = vkey_of(key);
    if (k < 0)
        return oRegSetValueExA(key, name, reserved, type, data, size);
    return vkey_set(k, name, type, data, size, 0);
}

static LSTATUS(WINAPI *oRegSetValueExW)(HKEY, LPCWSTR, DWORD, DWORD, const BYTE *, DWORD);
static LSTATUS WINAPI hRegSetValueExW(HKEY key, LPCWSTR name, DWORD reserved, DWORD type, const BYTE *data, DWORD size)
{
    int k = vkey_of(key);
    if (k < 0)
        return oRegSetValueExW(key, name, reserved, type, data, size);
    char n[64];
    return vkey_set(k, name ? wl_u8(name, n, sizeof n) : NULL, type, data, size, 1);
}

// The values one by one (the list of serial ports, SERIALCOMM, is read so)
static LSTATUS vkey_enum(int k, DWORD index, void *name, LPDWORD nameLength, LPDWORD type, LPBYTE data, LPDWORD size, int wide)
{
    EnterCriticalSection(&g_vkeyLock);
    LSTATUS r = ERROR_NO_MORE_ITEMS;
    if ((int)index < g_vkeys[k].count)
    {
        const VValue *v = &g_vkeys[k].values[index];
        DWORD n = (DWORD)strlen(v->name);
        r = ERROR_SUCCESS;
        if (!nameLength || *nameLength <= n)
            r = ERROR_MORE_DATA;
        else if (wide)
            MultiByteToWideChar(CP_ACP, 0, v->name, -1, (wchar_t *)name, (int)*nameLength);
        else
            memcpy(name, v->name, n + 1);
        if (nameLength)
            *nameLength = n;
        LeaveCriticalSection(&g_vkeyLock);
        if (r == ERROR_SUCCESS)
            r = vkey_query(k, v->name, type, data, size, wide);
        return r;
    }
    LeaveCriticalSection(&g_vkeyLock);
    return r;
}

// Its counts: no subkeys (none listed), its values, their longest name and data
static LSTATUS vkey_info(int k, LPDWORD subkeys, LPDWORD maxSubkey, LPDWORD values, LPDWORD maxName, LPDWORD maxData, int wide)
{
    EnterCriticalSection(&g_vkeyLock);
    DWORD longestName = 0, longestData = 0;
    for (int i = 0; i < g_vkeys[k].count; i++)
    {
        const VValue *v = &g_vkeys[k].values[i];
        DWORD n = (DWORD)strlen(v->name), size = v->type == REG_SZ && wide ? v->size * 2 : v->size;
        longestName = n > longestName ? n : longestName;
        longestData = size > longestData ? size : longestData;
    }
    if (subkeys)
        *subkeys = 0;
    if (maxSubkey)
        *maxSubkey = 0;
    if (values)
        *values = (DWORD)g_vkeys[k].count;
    if (maxName)
        *maxName = longestName;
    if (maxData)
        *maxData = longestData;
    LeaveCriticalSection(&g_vkeyLock);
    return ERROR_SUCCESS;
}

static LSTATUS(WINAPI *oRegQueryInfoKeyA)(HKEY, LPSTR, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, PFILETIME);
static LSTATUS WINAPI hRegQueryInfoKeyA(HKEY key, LPSTR cls, LPDWORD clsLength, LPDWORD reserved, LPDWORD subkeys, LPDWORD maxSubkey, LPDWORD maxClass,
                                        LPDWORD values, LPDWORD maxName, LPDWORD maxData, LPDWORD security, PFILETIME written)
{
    int k = vkey_of(key);
    if (k < 0)
        return oRegQueryInfoKeyA(key, cls, clsLength, reserved, subkeys, maxSubkey, maxClass, values, maxName, maxData, security, written);
    if (clsLength)
        *clsLength = 0;
    if (cls)
        cls[0] = 0;
    if (maxClass)
        *maxClass = 0;
    if (security)
        *security = 0;
    if (written)
        GetSystemTimeAsFileTime(written);
    return vkey_info(k, subkeys, maxSubkey, values, maxName, maxData, 0);
}

static LSTATUS(WINAPI *oRegQueryInfoKeyW)(HKEY, LPWSTR, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, PFILETIME);
static LSTATUS WINAPI hRegQueryInfoKeyW(HKEY key, LPWSTR cls, LPDWORD clsLength, LPDWORD reserved, LPDWORD subkeys, LPDWORD maxSubkey, LPDWORD maxClass,
                                        LPDWORD values, LPDWORD maxName, LPDWORD maxData, LPDWORD security, PFILETIME written)
{
    int k = vkey_of(key);
    if (k < 0)
        return oRegQueryInfoKeyW(key, cls, clsLength, reserved, subkeys, maxSubkey, maxClass, values, maxName, maxData, security, written);
    if (clsLength)
        *clsLength = 0;
    if (cls)
        cls[0] = 0;
    if (maxClass)
        *maxClass = 0;
    if (security)
        *security = 0;
    if (written)
        GetSystemTimeAsFileTime(written);
    return vkey_info(k, subkeys, maxSubkey, values, maxName, maxData, 1);
}

static LSTATUS(WINAPI *oRegEnumValueA)(HKEY, DWORD, LPSTR, LPDWORD, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
static LSTATUS WINAPI hRegEnumValueA(HKEY key, DWORD index, LPSTR name, LPDWORD nameLength, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size)
{
    int k = vkey_of(key);
    if (k < 0)
        return oRegEnumValueA(key, index, name, nameLength, reserved, type, data, size);
    return vkey_enum(k, index, name, nameLength, type, data, size, 0);
}

static LSTATUS(WINAPI *oRegEnumValueW)(HKEY, DWORD, LPWSTR, LPDWORD, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
static LSTATUS WINAPI hRegEnumValueW(HKEY key, DWORD index, LPWSTR name, LPDWORD nameLength, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size)
{
    int k = vkey_of(key);
    if (k < 0)
        return oRegEnumValueW(key, index, name, nameLength, reserved, type, data, size);
    return vkey_enum(k, index, name, nameLength, type, data, size, 1);
}

#define KEY_WRITES (KEY_SET_VALUE | KEY_CREATE_SUB_KEY | KEY_CREATE_LINK | DELETE | WRITE_DAC | WRITE_OWNER)

static HKEY g_hklmKeys[64];

static int is_hklm(HKEY key)
{
    if (key == HKEY_LOCAL_MACHINE || key == HKEY_CLASSES_ROOT || key == HKEY_USERS || key == HKEY_CURRENT_CONFIG)
        return 1;
    for (int i = 0; i < 64; i++)
        if (key && g_hklmKeys[i] == key)
            return 1;
    return 0;
}

static void hklm_add(HKEY key)
{
    for (int i = 0; i < 64; i++)
        if (!g_hklmKeys[i] && InterlockedCompareExchangePointer((PVOID *)&g_hklmKeys[i], key, NULL) == NULL)
            return;
}

static REGSAM hklm_rights(REGSAM sam, const char *fn, const char *sub, const void *caller)
{
    if (!(sam & KEY_WRITES))
        return sam;
    char where[128];
    wl_log("%s %s (the machine's registry): opened read only [%s]", fn, sub ? sub : "", where_of(caller, where, sizeof where));
    sam &= ~(REGSAM)KEY_WRITES;
    return (sam & (KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | KEY_NOTIFY)) ? sam : sam | KEY_READ;
}

static LSTATUS(WINAPI *oRegOpenKeyExA)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
static LSTATUS WINAPI hRegOpenKeyExA(HKEY key, LPCSTR sub, DWORD options, REGSAM sam, PHKEY result)
{
    int v = wl_busy() ? -1 : vkey_find(key, sub);
    if (v >= 0)
        return vkey_open(v, result, NULL);
    if (vkey_of(key) >= 0)
        return ERROR_FILE_NOT_FOUND;
    if (wl_busy() || wl_is_system_caller(WL_CALLER) || !is_hklm(key))
        return oRegOpenKeyExA(key, sub, options, sam, result);
    LSTATUS r = oRegOpenKeyExA(key, sub, options, hklm_rights(sam, "RegOpenKeyEx", sub, WL_CALLER), result);
    if (r == ERROR_SUCCESS && result)
        hklm_add(*result);
    return r;
}

static LSTATUS(WINAPI *oRegOpenKeyExW)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
static LSTATUS WINAPI hRegOpenKeyExW(HKEY key, LPCWSTR sub, DWORD options, REGSAM sam, PHKEY result)
{
    char s[512];
    int v = wl_busy() ? -1 : vkey_find(key, sub ? wl_u8(sub, s, sizeof s) : NULL);
    if (v >= 0)
        return vkey_open(v, result, NULL);
    if (vkey_of(key) >= 0)
        return ERROR_FILE_NOT_FOUND;
    if (wl_busy() || wl_is_system_caller(WL_CALLER) || !is_hklm(key))
        return oRegOpenKeyExW(key, sub, options, sam, result);
    LSTATUS r = oRegOpenKeyExW(key, sub, options, hklm_rights(sam, "RegOpenKeyEx", sub ? wl_u8(sub, s, sizeof s) : NULL, WL_CALLER), result);
    if (r == ERROR_SUCCESS && result)
        hklm_add(*result);
    return r;
}

// A key created under HKLM: only an existing one is opened, read only
static LSTATUS(WINAPI *oRegCreateKeyExA)(HKEY, LPCSTR, DWORD, LPSTR, DWORD, REGSAM, const LPSECURITY_ATTRIBUTES, PHKEY, LPDWORD);
static LSTATUS WINAPI hRegCreateKeyExA(HKEY key, LPCSTR sub, DWORD reserved, LPSTR cls, DWORD options, REGSAM sam, const LPSECURITY_ATTRIBUTES sa,
                                       PHKEY result, LPDWORD disposition)
{
    int v = wl_busy() ? -1 : vkey_find(key, sub);
    if (v >= 0)
        return vkey_open(v, result, disposition);
    if (vkey_of(key) >= 0)
        return ERROR_ACCESS_DENIED;
    if (wl_busy() || wl_is_system_caller(WL_CALLER) || !is_hklm(key))
        return oRegCreateKeyExA(key, sub, reserved, cls, options, sam, sa, result, disposition);
    LSTATUS r = oRegOpenKeyExA(key, sub, 0, hklm_rights(sam | KEY_CREATE_SUB_KEY, "RegCreateKeyEx", sub, WL_CALLER), result);
    if (r == ERROR_SUCCESS && result)
        hklm_add(*result);
    if (r == ERROR_SUCCESS && disposition)
        *disposition = REG_OPENED_EXISTING_KEY;
    return r == ERROR_FILE_NOT_FOUND ? ERROR_ACCESS_DENIED : r;
}

static LSTATUS(WINAPI *oRegCreateKeyExW)(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD, REGSAM, const LPSECURITY_ATTRIBUTES, PHKEY, LPDWORD);
static LSTATUS WINAPI hRegCreateKeyExW(HKEY key, LPCWSTR sub, DWORD reserved, LPWSTR cls, DWORD options, REGSAM sam, const LPSECURITY_ATTRIBUTES sa,
                                       PHKEY result, LPDWORD disposition)
{
    char s[512];
    int v = wl_busy() ? -1 : vkey_find(key, sub ? wl_u8(sub, s, sizeof s) : NULL);
    if (v >= 0)
        return vkey_open(v, result, disposition);
    if (vkey_of(key) >= 0)
        return ERROR_ACCESS_DENIED;
    if (wl_busy() || wl_is_system_caller(WL_CALLER) || !is_hklm(key))
        return oRegCreateKeyExW(key, sub, reserved, cls, options, sam, sa, result, disposition);
    LSTATUS r = oRegOpenKeyExW(key, sub, 0, hklm_rights(sam | KEY_CREATE_SUB_KEY, "RegCreateKeyEx", sub ? wl_u8(sub, s, sizeof s) : NULL, WL_CALLER),
                               result);
    if (r == ERROR_SUCCESS && result)
        hklm_add(*result);
    if (r == ERROR_SUCCESS && disposition)
        *disposition = REG_OPENED_EXISTING_KEY;
    return r == ERROR_FILE_NOT_FOUND ? ERROR_ACCESS_DENIED : r;
}

static void delete_refused(const char *fn, const char *sub, const void *caller)
{
    char where[128];
    wl_log("%s %s refused: the registry is left alone [%s]", fn, sub ? sub : "", where_of(caller, where, sizeof where));
}

static LSTATUS(WINAPI *oRegDeleteKeyA)(HKEY, LPCSTR);
static LSTATUS WINAPI hRegDeleteKeyA(HKEY key, LPCSTR sub)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oRegDeleteKeyA(key, sub);
    delete_refused("RegDeleteKey", sub, WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

static LSTATUS(WINAPI *oRegDeleteKeyW)(HKEY, LPCWSTR);
static LSTATUS WINAPI hRegDeleteKeyW(HKEY key, LPCWSTR sub)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oRegDeleteKeyW(key, sub);
    char s[512];
    delete_refused("RegDeleteKey", sub ? wl_u8(sub, s, sizeof s) : NULL, WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

static LSTATUS(WINAPI *oRegDeleteKeyExA)(HKEY, LPCSTR, REGSAM, DWORD);
static LSTATUS WINAPI hRegDeleteKeyExA(HKEY key, LPCSTR sub, REGSAM sam, DWORD reserved)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oRegDeleteKeyExA(key, sub, sam, reserved);
    delete_refused("RegDeleteKeyEx", sub, WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

static LSTATUS(WINAPI *oRegDeleteKeyExW)(HKEY, LPCWSTR, REGSAM, DWORD);
static LSTATUS WINAPI hRegDeleteKeyExW(HKEY key, LPCWSTR sub, REGSAM sam, DWORD reserved)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oRegDeleteKeyExW(key, sub, sam, reserved);
    char s[512];
    delete_refused("RegDeleteKeyEx", sub ? wl_u8(sub, s, sizeof s) : NULL, WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

static LSTATUS(WINAPI *oRegDeleteTreeA)(HKEY, LPCSTR);
static LSTATUS WINAPI hRegDeleteTreeA(HKEY key, LPCSTR sub)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oRegDeleteTreeA(key, sub);
    delete_refused("RegDeleteTree", sub, WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

static LSTATUS(WINAPI *oRegDeleteTreeW)(HKEY, LPCWSTR);
static LSTATUS WINAPI hRegDeleteTreeW(HKEY key, LPCWSTR sub)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oRegDeleteTreeW(key, sub);
    char s[512];
    delete_refused("RegDeleteTree", sub ? wl_u8(sub, s, sizeof s) : NULL, WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

static LSTATUS(WINAPI *oRegDeleteValueA)(HKEY, LPCSTR);
static LSTATUS WINAPI hRegDeleteValueA(HKEY key, LPCSTR sub)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER) || !is_hklm(key))
        return oRegDeleteValueA(key, sub);
    delete_refused("RegDeleteValue", sub, WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

static LSTATUS(WINAPI *oRegDeleteValueW)(HKEY, LPCWSTR);
static LSTATUS WINAPI hRegDeleteValueW(HKEY key, LPCWSTR sub)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER) || !is_hklm(key))
        return oRegDeleteValueW(key, sub);
    char s[512];
    delete_refused("RegDeleteValue", sub ? wl_u8(sub, s, sizeof s) : NULL, WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

static LSTATUS(WINAPI *oRegCloseKey)(HKEY);
static LSTATUS WINAPI hRegCloseKey(HKEY key)
{
    if (vkey_of(key) >= 0)
        return ERROR_SUCCESS;
    for (int i = 0; i < 64; i++)
        if (key && g_hklmKeys[i] == key)
            InterlockedCompareExchangePointer((PVOID *)&g_hklmKeys[i], NULL, key);
    return oRegCloseKey(key);
}

// ------------------------------------------------------------------------------
// System settings and global input
// ------------------------------------------------------------------------------

// SPI_SET* actions that change the user's session: screen saver, accessibility keys, mouse...
static int spi_changes_session(UINT action, UINT winIni)
{
    if (winIni & (SPIF_UPDATEINIFILE | SPIF_SENDCHANGE))
        return 1;
    switch (action)
    {
    case SPI_SETMOUSE:
    case SPI_SETSCREENSAVETIMEOUT:
    case SPI_SETSCREENSAVEACTIVE:
    case SPI_SETDESKWALLPAPER:
    case SPI_SETFILTERKEYS:
    case SPI_SETTOGGLEKEYS:
    case SPI_SETSTICKYKEYS:
    case SPI_SETCURSORS:
    case SPI_SETMOUSESPEED:
    case SPI_SETFOREGROUNDLOCKTIMEOUT:
        return 1;
    default:
        return 0;
    }
}

static BOOL(WINAPI *oSystemParametersInfoA)(UINT, UINT, PVOID, UINT);
static BOOL WINAPI hSystemParametersInfoA(UINT action, UINT param, PVOID pv, UINT winIni)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oSystemParametersInfoA(action, param, pv, winIni);
    char where[128];
    if (g_wl.windowed && spi_changes_session(action, winIni))
    {
        wl_log("SystemParametersInfo(0x%04x, %u, flags 0x%x) refused [%s]", action, param, winIni, where_of(WL_CALLER, where, sizeof where));
        return TRUE;
    }
    wl_busy_enter();
    BOOL ok = oSystemParametersInfoA(action, param, pv, winIni);
    wl_busy_leave();
    wl_log("SystemParametersInfo(0x%04x, %u, flags 0x%x) -> %s [%s]", action, param, winIni, ok ? "ok" : "failed", where_of(WL_CALLER, where, sizeof where));
    return ok;
}

static BOOL(WINAPI *oSystemParametersInfoW)(UINT, UINT, PVOID, UINT);
static BOOL WINAPI hSystemParametersInfoW(UINT action, UINT param, PVOID pv, UINT winIni)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oSystemParametersInfoW(action, param, pv, winIni);
    char where[128];
    if (g_wl.windowed && spi_changes_session(action, winIni))
    {
        wl_log("SystemParametersInfo(0x%04x, %u, flags 0x%x) refused [%s]", action, param, winIni, where_of(WL_CALLER, where, sizeof where));
        return TRUE;
    }
    wl_busy_enter();
    BOOL ok = oSystemParametersInfoW(action, param, pv, winIni);
    wl_busy_leave();
    wl_log("SystemParametersInfo(0x%04x, %u, flags 0x%x) -> %s [%s]", action, param, winIni, ok ? "ok" : "failed", where_of(WL_CALLER, where, sizeof where));
    return ok;
}

// Low-level keyboard and mouse hooks see (and can swallow) the input of the whole desktop
#define WL_FAKE_HOOK ((HHOOK)(ULONG_PTR)0x574C4B00)

static HHOOK set_hook(HHOOK(WINAPI *orig)(int, HOOKPROC, HINSTANCE, DWORD), int id, HOOKPROC proc, HINSTANCE mod, DWORD tid, const void *caller)
{
    if (wl_busy() || wl_is_system_caller(caller))
        return orig(id, proc, mod, tid);
    char where[128];
    if (g_wl.windowed && (id == WH_KEYBOARD_LL || id == WH_MOUSE_LL))
    {
        wl_log("SetWindowsHookEx(%s) refused [%s]", id == WH_KEYBOARD_LL ? "WH_KEYBOARD_LL" : "WH_MOUSE_LL", where_of(caller, where, sizeof where));
        return WL_FAKE_HOOK;
    }
    wl_busy_enter();
    HHOOK h = orig(id, proc, mod, tid);
    wl_busy_leave();
    wl_log("SetWindowsHookEx(%d, thread %lu) -> %p [%s]", id, tid, h, where_of(caller, where, sizeof where));
    return h;
}

static HHOOK(WINAPI *oSetWindowsHookExA)(int, HOOKPROC, HINSTANCE, DWORD);
static HHOOK WINAPI hSetWindowsHookExA(int id, HOOKPROC proc, HINSTANCE mod, DWORD tid)
{
    return set_hook(oSetWindowsHookExA, id, proc, mod, tid, WL_CALLER);
}

static HHOOK(WINAPI *oSetWindowsHookExW)(int, HOOKPROC, HINSTANCE, DWORD);
static HHOOK WINAPI hSetWindowsHookExW(int id, HOOKPROC proc, HINSTANCE mod, DWORD tid)
{
    return set_hook(oSetWindowsHookExW, id, proc, mod, tid, WL_CALLER);
}

static BOOL(WINAPI *oUnhookWindowsHookEx)(HHOOK);
static BOOL WINAPI hUnhookWindowsHookEx(HHOOK h)
{
    if (h == WL_FAKE_HOOK)
        return TRUE;
    return oUnhookWindowsHookEx(h);
}

static UINT(WINAPI *oSendInput)(UINT, LPINPUT, int);
static UINT WINAPI hSendInput(UINT count, LPINPUT inputs, int size)
{
    static volatile LONG logged;
    if (wl_busy() || wl_is_system_caller(WL_CALLER) || !g_wl.windowed)
        return oSendInput(count, inputs, size);
    if (InterlockedIncrement(&logged) <= 5)
    {
        char where[128];
        wl_log("SendInput(%u events, first type %lu) refused [%s]", count, count && inputs ? inputs[0].type : 0, where_of(WL_CALLER, where, sizeof where));
    }
    return count;
}

// ------------------------------------------------------------------------------
// Messages from the game
// ------------------------------------------------------------------------------

// The 2000 first messages; those holding one of the module's quiet strings are left out and not
// counted (Daytona's thousands of asset loads, "....nrfc, QueSize 529", hid its network messages)
static const char *const *g_quietDebug;

void wl_system_quiet_debug(const char *const *strings)
{
    g_quietDebug = strings;
}

static void OutputDebugString_log(const char *text)
{
    if (wl_log_to_debugger())
        return; // the log is the debugger's output already: the line is there
    for (const char *const *q = g_quietDebug; q && *q; q++)
        if (strstr(text, *q))
            return;
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 2000)
        wl_log("debug: %s", text);
}

static void(WINAPI *oOutputDebugStringA)(LPCSTR);
static void WINAPI hOutputDebugStringA(LPCSTR text)
{
    if (!wl_busy() && text)
        OutputDebugString_log(text);
    wl_busy_enter();
    oOutputDebugStringA(text);
    wl_busy_leave();
}

static void(WINAPI *oOutputDebugStringW)(LPCWSTR);
static void WINAPI hOutputDebugStringW(LPCWSTR text)
{
    if (!wl_busy() && text)
    {
        char t[1024];
        OutputDebugString_log(wl_u8(text, t, sizeof t));
    }
    wl_busy_enter();
    oOutputDebugStringW(text);
    wl_busy_leave();
}

static int(WINAPI *oMessageBoxA)(HWND, LPCSTR, LPCSTR, UINT);
static int WINAPI hMessageBoxA(HWND wnd, LPCSTR text, LPCSTR caption, UINT type)
{
    int nested = wl_busy();
    if (!nested)
        wl_log("MessageBox \"%s\": %s", caption ? caption : "", text ? text : "");
    wl_busy_enter();
    int r = oMessageBoxA(wnd, text, caption, type);
    wl_busy_leave();
    if (!nested)
        wl_log("MessageBox answered %d", r);
    return r;
}

static int(WINAPI *oMessageBoxW)(HWND, LPCWSTR, LPCWSTR, UINT);
static int WINAPI hMessageBoxW(HWND wnd, LPCWSTR text, LPCWSTR caption, UINT type)
{
    int nested = wl_busy();
    if (!nested)
    {
        char c[256], t[1024];
        wl_log("MessageBox \"%s\": %s", caption ? wl_u8(caption, c, sizeof c) : "", text ? wl_u8(text, t, sizeof t) : "");
    }
    wl_busy_enter();
    int r = oMessageBoxW(wnd, text, caption, type);
    wl_busy_leave();
    if (!nested)
        wl_log("MessageBox answered %d", r);
    return r;
}

static BOOL(WINAPI *oIsDebuggerPresent)(void);
static BOOL WINAPI hIsDebuggerPresent(void)
{
    static volatile LONG logged;
    BOOL r = oIsDebuggerPresent();
    if (!wl_busy() && !wl_is_system_caller(WL_CALLER) && InterlockedIncrement(&logged) == 1)
    {
        char where[128];
        wl_log("IsDebuggerPresent -> %d [%s]", r, where_of(WL_CALLER, where, sizeof where));
    }
    return r;
}

// Games refuse to run in a Remote Desktop session ("Remote Sessions not supported" for
// Sega Rally 3); a cabinet never is one.
static int(WINAPI *oGetSystemMetrics)(int);
static int WINAPI hGetSystemMetrics(int index)
{
    static volatile LONG logged;
    int r = oGetSystemMetrics(index);
    if ((index == SM_REMOTESESSION || index == SM_REMOTECONTROL) && r && !wl_busy() && !wl_is_system_caller(WL_CALLER))
    {
        if (InterlockedIncrement(&logged) <= 3)
        {
            char where[128];
            wl_log("GetSystemMetrics(%s) -> 0 instead of %d [%s]", index == SM_REMOTESESSION ? "SM_REMOTESESSION" : "SM_REMOTECONTROL", r,
                   where_of(WL_CALLER, where, sizeof where));
        }
        return 0;
    }
    return r;
}

// ------------------------------------------------------------------------------
// The PC's camera: some cabinets photograph their player (Daytona Championship USA's face
// pictures through Video for Windows and DirectShow, Mario Kart Arcade GP DX's through Media
// Foundation). [Emulation] CAMERA: AUTO (the default) gives no camera, unless the game's own
// camera option (camera.h, [Game.<name>] CAMERA) chooses one; true always gives the PC's cameras;
// false never (a game with an emulated camera gets that one).
// ------------------------------------------------------------------------------

static volatile LONG g_cameraAllowed = -1, g_cameraAsked;

// The setting, read once: 1 true, 0 false, -1 AUTO
static int camera_setting(void)
{
    static volatile LONG setting = -2;
    if (setting == -2)
    {
        char v[16];
        wl_settings_get("Emulation", "CAMERA", "AUTO", v, sizeof v);
        InterlockedExchange(&setting, !_stricmp(v, "AUTO") ? -1 : wl_settings_get_bool("Emulation", "CAMERA", 0));
    }
    return setting;
}

void wl_system_allow_camera(const char *why)
{
    if (camera_setting() == 0)
    {
        wl_log("camera: no camera for the game, although %s ([Emulation] CAMERA = false)", why);
        return;
    }
    InterlockedExchange(&g_cameraAsked, 1);
    if (InterlockedExchange(&g_cameraAllowed, 1) != 1)
        wl_log("camera: the PC's cameras given to the game (%s)", why);
}

static int camera_allowed(void)
{
    volatile LONG &allowed = g_cameraAllowed;
    if (allowed < 0)
    {
        InterlockedExchange(&allowed, camera_setting() == 1 || g_cameraAsked);
        if (camera_setting() == 1)
            wl_log("camera: the PC's cameras given to the game ([Emulation] CAMERA = true)");
    }
    return allowed;
}

int wl_system_camera_allowed(void)
{
    return camera_allowed();
}

static void camera_refused(const char *what, const void *caller)
{
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 10)
    {
        char where[128];
        wl_log("%s refused: no camera for the game [%s]", what, where_of(caller, where, sizeof where));
    }
}

static HWND(WINAPI *ocapCreateCaptureWindowA)(LPCSTR, DWORD, int, int, int, int, HWND, int);
static HWND WINAPI hcapCreateCaptureWindowA(LPCSTR name, DWORD style, int x, int y, int w, int h, HWND parent, int id)
{
    if (camera_allowed())
        return ocapCreateCaptureWindowA(name, style, x, y, w, h, parent, id);
    camera_refused("capCreateCaptureWindow", WL_CALLER);
    return NULL;
}

static HWND(WINAPI *ocapCreateCaptureWindowW)(LPCWSTR, DWORD, int, int, int, int, HWND, int);
static HWND WINAPI hcapCreateCaptureWindowW(LPCWSTR name, DWORD style, int x, int y, int w, int h, HWND parent, int id)
{
    if (camera_allowed())
        return ocapCreateCaptureWindowW(name, style, x, y, w, h, parent, id);
    camera_refused("capCreateCaptureWindow", WL_CALLER);
    return NULL;
}

static BOOL(WINAPI *ocapGetDriverDescriptionA)(UINT, LPSTR, int, LPSTR, int);
static BOOL WINAPI hcapGetDriverDescriptionA(UINT index, LPSTR name, int nameSize, LPSTR ver, int verSize)
{
    if (camera_allowed())
        return ocapGetDriverDescriptionA(index, name, nameSize, ver, verSize);
    camera_refused("capGetDriverDescription", WL_CALLER);
    return FALSE;
}

static BOOL(WINAPI *ocapGetDriverDescriptionW)(UINT, LPWSTR, int, LPWSTR, int);
static BOOL WINAPI hcapGetDriverDescriptionW(UINT index, LPWSTR name, int nameSize, LPWSTR ver, int verSize)
{
    if (camera_allowed())
        return ocapGetDriverDescriptionW(index, name, nameSize, ver, verSize);
    camera_refused("capGetDriverDescription", WL_CALLER);
    return FALSE;
}

// Media Foundation's capture devices (Mario Kart Arcade GP DX's player photos): none listed, none
// opened
static HRESULT(WINAPI *oMFEnumDeviceSources)(void *, void ***, UINT32 *);
static HRESULT WINAPI hMFEnumDeviceSources(void *attributes, void ***sources, UINT32 *count)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER) || camera_allowed())
        return oMFEnumDeviceSources(attributes, sources, count);
    camera_refused("MFEnumDeviceSources", WL_CALLER);
    if (sources)
        *sources = NULL;
    if (count)
        *count = 0;
    return S_OK;
}

static HRESULT(WINAPI *oMFCreateDeviceSource)(void *, void **);
static HRESULT WINAPI hMFCreateDeviceSource(void *attributes, void **source)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER) || camera_allowed())
        return oMFCreateDeviceSource(attributes, source);
    camera_refused("MFCreateDeviceSource", WL_CALLER);
    if (source)
        *source = NULL;
    return E_ACCESSDENIED;
}

// COM objects the game creates: DirectShow's device enumerator would list the cameras too
static const GUID g_systemDeviceEnum = {0x62BE5D10, 0x60EB, 0x11D0, {0xBD, 0x3B, 0x00, 0xA0, 0xC9, 0x11, 0xCE, 0x86}};

// DirectShow's filter graph, for a game's movies: its source filters open the file from Windows'
// own DLLs, past the file hooks, so the path given to the graph is redirected here (Battle Gear 4's
// "E:/data/.../attract_demo.wmv", E:\ being its folder)
static const GUID g_filterGraph = {0xE436EBB3, 0x524F, 0x11CE, {0x9F, 0x53, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}};

// DirectInput 7 made through COM (Immersion's IFC23.dll): its devices handled as DirectInput 8's
// (dinputhook.cpp), for its ANSI interfaces
static const GUID g_directInput = {0x25E609E0, 0xB259, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID g_directInputA = {0x89521360, 0xAA8A, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID g_directInput2A = {0x5944E662, 0xAA8A, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID g_directInput7A = {0x9A4CB684, 0x236D, 0x11D3, {0x8E, 0x9D, 0x00, 0xC0, 0x4F, 0x68, 0x44, 0xAE}};

static void log_movie(const char *what, LPCWSTR file, HRESULT hr, const void *caller)
{
    static volatile LONG logged;
    char where[128];
    if (!wl_is_system_caller(caller) && InterlockedIncrement(&logged) <= 20)
        wl_log("DirectShow: %s \"%ls\" -> 0x%08lx [%s]", what, file ? file : L"", hr, where_of(caller, where, sizeof where));
}

static HRESULT(STDMETHODCALLTYPE *oRenderFile)(IGraphBuilder *, LPCWSTR, LPCWSTR);
static HRESULT STDMETHODCALLTYPE hRenderFile(IGraphBuilder *graph, LPCWSTR file, LPCWSTR playList)
{
    wchar_t buf[MAX_PATH * 2];
    const wchar_t *path = file ? wl_files_redirectedW(file, buf, MAX_PATH * 2) : file;
    HRESULT hr = oRenderFile(graph, path, playList);
    log_movie("RenderFile", path, hr, WL_CALLER);
    return hr;
}

static HRESULT(STDMETHODCALLTYPE *oAddSourceFilter)(IGraphBuilder *, LPCWSTR, LPCWSTR, IBaseFilter **);
static HRESULT STDMETHODCALLTYPE hAddSourceFilter(IGraphBuilder *graph, LPCWSTR file, LPCWSTR name, IBaseFilter **filter)
{
    wchar_t buf[MAX_PATH * 2];
    const wchar_t *path = file ? wl_files_redirectedW(file, buf, MAX_PATH * 2) : file;
    HRESULT hr = oAddSourceFilter(graph, path, name, filter);
    log_movie("AddSourceFilter", path, hr, WL_CALLER);
    return hr;
}

static void hook_filter_graph(IUnknown *object)
{
    static volatile LONG hooked;
    IGraphBuilder *graph = NULL;
    if (hooked || FAILED(object->QueryInterface(__uuidof(IGraphBuilder), (void **)&graph)))
        return;
    if (!InterlockedExchange(&hooked, 1))
    {
        void **vt = *(void ***)graph;
        wl_hook_code(vt[13], (void *)hRenderFile, (void **)&oRenderFile, "IGraphBuilder::RenderFile");
        wl_hook_code(vt[14], (void *)hAddSourceFilter, (void **)&oAddSourceFilter, "IGraphBuilder::AddSourceFilter");
    }
    graph->Release();
}

static HRESULT(WINAPI *oCoCreateInstance)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID *);
static HRESULT WINAPI hCoCreateInstance(REFCLSID cls, LPUNKNOWN outer, DWORD ctx, REFIID iid, LPVOID *out)
{
    int nested = wl_busy();
    if (!nested && !wl_is_system_caller(WL_CALLER) && IsEqualCLSID(cls, g_systemDeviceEnum) && !camera_allowed())
    {
        camera_refused("CoCreateInstance(SystemDeviceEnum)", WL_CALLER);
        if (out)
            *out = NULL;
        return REGDB_E_CLASSNOTREG;
    }
    wl_busy_enter();
    HRESULT hr = oCoCreateInstance(cls, outer, ctx, iid, out);
    if (SUCCEEDED(hr) && out && *out && !outer && IsEqualCLSID(cls, g_filterGraph))
        hook_filter_graph((IUnknown *)*out);
    if (SUCCEEDED(hr) && out && *out && IsEqualCLSID(cls, g_directInput) &&
        (IsEqualIID(iid, g_directInputA) || IsEqualIID(iid, g_directInput2A) || IsEqualIID(iid, g_directInput7A)))
        wl_dinput_legacy(*out);
    wl_busy_leave();
    static volatile LONG logged;
    if (!nested && !wl_is_system_caller(WL_CALLER) && InterlockedIncrement(&logged) <= 100)
    {
        char where[128];
        wl_log("CoCreateInstance {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X} -> 0x%08lx [%s]", cls.Data1, cls.Data2, cls.Data3, cls.Data4[0],
               cls.Data4[1], cls.Data4[2], cls.Data4[3], cls.Data4[4], cls.Data4[5], cls.Data4[6], cls.Data4[7], hr, where_of(WL_CALLER, where, sizeof where));
    }
    return hr;
}

// ------------------------------------------------------------------------------
// Sockets: where the game listens and talks (cabinet links)
// ------------------------------------------------------------------------------

static const char *sockaddr_text(const struct sockaddr *a, int len, char *buf, size_t size)
{
    if (!a || len < 8 || ((const unsigned short *)a)[0] != 2 /* AF_INET */)
    {
        snprintf(buf, size, "?");
        return buf;
    }
    const unsigned char *b = (const unsigned char *)a;
    snprintf(buf, size, "%u.%u.%u.%u:%u", b[4], b[5], b[6], b[7], (b[2] << 8) | b[3]);
    return buf;
}

// A game listening on its cabinet's own address (Sonic & Sega All-Stars Racing: 192.168.88.<id>,
// the address it gives itself) listens on this PC's 127.0.0.1 instead, the same port: that address
// is not on this PC, and the game stays off the local network. What it sends to that network (the
// other cabinets, its broadcast) is taken and dropped: the cabinet is alone on it.
static volatile LONG g_cabinetNet; // its first three bytes, 0 for none
static int(WINAPI *obind)(UINT_PTR, const struct sockaddr *, int);
static int WINAPI hbind(UINT_PTR s, const struct sockaddr *a, int len)
{
    int game = !wl_busy() && !wl_is_system_caller(WL_CALLER);
    unsigned char local[16];
    const unsigned char *b = (const unsigned char *)a;
    if (game && a && len >= 8 && len <= (int)sizeof local && ((const unsigned short *)a)[0] == 2 /* AF_INET */ && b[4] != 127 &&
        (b[4] || b[5] || b[6] || b[7]))
    {
        char t[32], where[128];
        wl_log("bind %s: on 127.0.0.1 instead [%s]", sockaddr_text(a, len, t, sizeof t), where_of(WL_CALLER, where, sizeof where));
        memcpy(local, a, len);
        InterlockedExchange(&g_cabinetNet, (LONG)(b[4] << 16 | b[5] << 8 | b[6]));
        local[4] = 127, local[5] = 0, local[6] = 0, local[7] = 1;
        a = (const struct sockaddr *)local;
    }
    int r = obind(s, a, len);
    DWORD err = GetLastError(); // the socket's error (WSAGetLastError), kept for the game
    if (game)
    {
        char t[32], where[128];
        wl_log("bind %s -> %d [%s]", sockaddr_text(a, len, t, sizeof t), r, where_of(WL_CALLER, where, sizeof where));
    }
    SetLastError(err);
    return r;
}

// The game only talks to this PC (127.0.0.0/8): its links with the other cabinets would reach
// the user's own network. Each address refused is logged once.
static int lan_refused(const struct sockaddr *a, int len, const char *what, const void *caller)
{
    if (!a || len < 8 || ((const unsigned short *)a)[0] != 2 /* AF_INET */)
        return 0;
    const unsigned char *b = (const unsigned char *)a;
    if (b[4] == 127 || (!b[4] && !b[5] && !b[6] && !b[7]))
        return 0;
    static unsigned g_seen[32];
    static volatile LONG g_seenCount;
    unsigned key = ((unsigned)b[4] << 24 | (unsigned)b[5] << 16 | (unsigned)b[6] << 8 | b[7]) ^ ((unsigned)b[2] << 8 | b[3]);
    int known = 0;
    for (int i = 0; i < g_seenCount && i < 32; i++)
        known |= g_seen[i] == key;
    if (!known)
    {
        LONG i = InterlockedIncrement(&g_seenCount) - 1;
        if (i < 32)
            g_seen[i] = key;
        char t[32], where[128];
        wl_log("%s %s refused: the game stays off the local network [%s]", what, sockaddr_text(a, len, t, sizeof t), where_of(caller, where, sizeof where));
    }
    SetLastError(WSAENETUNREACH);
    return 1;
}

// A font the game adds stays the game's (FR_PRIVATE): Windows removes it when the game ends. Added
// for the whole session (Let's Go Safari's dat\font), it stayed loaded after the game, its file
// locked until logoff
#define WL_FR_PRIVATE 0x10

static int(WINAPI *oAddFontResourceExA)(LPCSTR, DWORD, PVOID);
static int WINAPI hAddFontResourceExA(LPCSTR name, DWORD flags, PVOID reserved)
{
    if (!wl_busy() && !wl_is_system_caller(WL_CALLER))
        flags |= WL_FR_PRIVATE;
    return oAddFontResourceExA(name, flags, reserved);
}

static int(WINAPI *oAddFontResourceExW)(LPCWSTR, DWORD, PVOID);
static int WINAPI hAddFontResourceExW(LPCWSTR name, DWORD flags, PVOID reserved)
{
    if (!wl_busy() && !wl_is_system_caller(WL_CALLER))
        flags |= WL_FR_PRIVATE;
    return oAddFontResourceExW(name, flags, reserved);
}

static int(WINAPI *oAddFontResourceA)(LPCSTR);
static int WINAPI hAddFontResourceA(LPCSTR name)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oAddFontResourceA(name);
    static int logged;
    if (logged++ < 5)
        wl_log("AddFontResource \"%s\": kept the game's (FR_PRIVATE)", name ? name : "");
    return oAddFontResourceExA(name, WL_FR_PRIVATE, NULL);
}

static int(WINAPI *oAddFontResourceW)(LPCWSTR);
static int WINAPI hAddFontResourceW(LPCWSTR name)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oAddFontResourceW(name);
    static int logged;
    if (logged++ < 5)
        wl_log("AddFontResource \"%S\": kept the game's (FR_PRIVATE)", name ? name : L"");
    return oAddFontResourceExW(name, WL_FR_PRIVATE, NULL);
}

// A UDP socket "connected" to an address out there sends nothing: the game asks it then its own
// address (getsockname), the card it would leave by (Wangan Midnight 5, to 8.8.8.8). With the
// cabinet's card in place of the PC's (netenv.cpp), it is answered so: that card's address
static UINT_PTR g_probes[8];
static volatile LONG g_probeCount;

static int udp_probe(UINT_PTR s, const struct sockaddr *a, int len)
{
    // getsockopt(s, SOL_SOCKET, SO_TYPE): SOCK_DGRAM (winsock2.h is not included here)
    typedef int(WINAPI * GetSockOpt)(UINT_PTR, int, int, char *, int *);
    static GetSockOpt getsockopt_;
    if (!getsockopt_)
        getsockopt_ = (GetSockOpt)GetProcAddress(GetModuleHandleA("ws2_32.dll"), "getsockopt");
    unsigned ip;
    int type = 0, size = sizeof type;
    if (!a || len < 8 || ((const unsigned short *)a)[0] != 2 || !wl_netenv_fake_ip(&ip) || !getsockopt_ ||
        getsockopt_(s, 0xFFFF, 0x1008, (char *)&type, &size) || type != 2)
        return 0;
    LONG i = InterlockedIncrement(&g_probeCount) - 1;
    g_probes[i % 8] = s;
    static int logged;
    if (!logged++)
    {
        char t[32];
        wl_log("connect %s on a UDP socket: its address will be the cabinet's card's", sockaddr_text(a, len, t, sizeof t));
    }
    return 1;
}

static int(WINAPI *ogetsockname)(UINT_PTR, struct sockaddr *, int *);
static int WINAPI hgetsockname(UINT_PTR s, struct sockaddr *a, int *len)
{
    unsigned ip;
    for (int i = 0; i < 8 && i < g_probeCount; i++)
        if (g_probes[i] == s && a && len && *len >= 16 && wl_netenv_fake_ip(&ip))
        {
            memset(a, 0, 16);
            ((unsigned short *)a)[0] = 2;                      // AF_INET
            ((unsigned short *)a)[1] = _byteswap_ushort(50000); // the port, network order
            memcpy((char *)a + 4, &ip, 4);
            *len = 16;
            return 0;
        }
    return ogetsockname(s, a, len);
}

static int(WINAPI *oconnect)(UINT_PTR, const struct sockaddr *, int);
static int WINAPI hconnect(UINT_PTR s, const struct sockaddr *a, int len)
{
    int game = !wl_busy() && !wl_is_system_caller(WL_CALLER);
    if (game && udp_probe(s, a, len))
        return 0;
    if (game && lan_refused(a, len, "connect", WL_CALLER))
        return -1; // SOCKET_ERROR
    int r = oconnect(s, a, len);
    DWORD err = GetLastError(); // WSAEWOULDBLOCK on a non-blocking socket: the game reads it
    if (game)
    {
        char t[32], where[128];
        wl_log("connect %s -> %d [%s]", sockaddr_text(a, len, t, sizeof t), r, where_of(WL_CALLER, where, sizeof where));
    }
    SetLastError(err);
    return r;
}

// A game whose cabinets elect a host by broadcast (Wasteland Racers 2071) hears its own broadcast
// on this PC (127.0.0.1, the same port) when its module asks for it: alone, it becomes the host
static int g_loopbackBroadcast;

void wl_system_loopback_broadcast(int on)
{
    g_loopbackBroadcast = on;
}

static int(WINAPI *osendto)(UINT_PTR, const char *, int, int, const struct sockaddr *, int);
static int WINAPI hsendto(UINT_PTR s, const char *buf, int n, int flags, const struct sockaddr *a, int len)
{
    static volatile LONG logged;
    const unsigned char *b = (const unsigned char *)a;
    if (g_loopbackBroadcast && a && len >= 8 && ((const unsigned short *)a)[0] == 2 /* AF_INET */ && b[7] == 255 && !wl_busy() &&
        !wl_is_system_caller(WL_CALLER))
    {
        // Its length may be anything (Wasteland Racers passes a stray value): an IPv4 address is 16 bytes
        unsigned char local[16];
        len = len > 16 ? 16 : len;
        memcpy(local, a, len);
        local[4] = 127, local[5] = 0, local[6] = 0, local[7] = 1;
        static volatile LONG told;
        if (InterlockedIncrement(&told) == 1)
        {
            char t[32], where[128];
            wl_log("sendto %s: to 127.0.0.1 instead, the cabinet hears its own broadcast [%s]", sockaddr_text(a, len, t, sizeof t),
                   where_of(WL_CALLER, where, sizeof where));
        }
        return osendto(s, buf, n, flags, (const struct sockaddr *)local, len);
    }
    if (g_cabinetNet && a && len >= 8 && ((const unsigned short *)a)[0] == 2 /* AF_INET */ && (b[4] << 16 | b[5] << 8 | b[6]) == g_cabinetNet)
    {
        static volatile LONG dropped;
        if (InterlockedIncrement(&dropped) == 1)
        {
            char t[32], where[128];
            wl_log("sendto %s: dropped, the cabinet is alone on its network [%s]", sockaddr_text(a, len, t, sizeof t), where_of(WL_CALLER, where, sizeof where));
        }
        return n;
    }
    if (!wl_busy() && !wl_is_system_caller(WL_CALLER) && lan_refused(a, len, "sendto", WL_CALLER))
        return -1;
    int r = osendto(s, buf, n, flags, a, len);
    DWORD err = GetLastError();
    if (!wl_busy() && !wl_is_system_caller(WL_CALLER) && InterlockedIncrement(&logged) <= 30)
    {
        char t[32], where[128];
        wl_log("sendto %s (%d bytes) -> %d [%s]", sockaddr_text(a, len, t, sizeof t), n, r, where_of(WL_CALLER, where, sizeof where));
    }
    SetLastError(err);
    return r;
}

// WinHTTP's connections go through Windows' own modules, out of lan_refused's reach: the game's
// own WinHttpConnect is refused anything but this PC (Mario Kart Arcade GP DX's server at
// amk3-cn01.nbgi-amnet.jp, its ALL.Net at 127.0.0.1)
static void *(WINAPI *oWinHttpConnect)(void *, LPCWSTR, WORD, DWORD);
static void *WINAPI hWinHttpConnect(void *session, LPCWSTR server, WORD port, DWORD reserved)
{
    if (!wl_busy() && !wl_is_system_caller(WL_CALLER) && server && _wcsicmp(server, L"localhost") && wcsncmp(server, L"127.", 4))
    {
        static volatile LONG logged;
        if (InterlockedIncrement(&logged) <= 10)
        {
            char where[128], s[256];
            wl_log("WinHttpConnect %s:%u refused: the game stays on this PC [%s]", wl_u8(server, s, sizeof s), port, where_of(WL_CALLER, where, sizeof where));
        }
        SetLastError(12007); // ERROR_WINHTTP_NAME_NOT_RESOLVED
        return NULL;
    }
    return oWinHttpConnect(session, server, port, reserved);
}

// The PC's routing table is not the game's (Mario Kart Arcade GP DX sets the cabinet's routes):
// refused as for a program without the administrator's rights
static void route_refused(const char *what, const void *caller)
{
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 10)
    {
        char where[128];
        wl_log("%s refused: the PC's routes and addresses are left alone [%s]", what, where_of(caller, where, sizeof where));
    }
}

static DWORD(WINAPI *oCreateIpForwardEntry)(void *);
static DWORD WINAPI hCreateIpForwardEntry(void *route)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oCreateIpForwardEntry(route);
    route_refused("CreateIpForwardEntry", WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

static DWORD(WINAPI *oSetIpForwardEntry)(void *);
static DWORD WINAPI hSetIpForwardEntry(void *route)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oSetIpForwardEntry(route);
    route_refused("SetIpForwardEntry", WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

static DWORD(WINAPI *oDeleteIpForwardEntry)(void *);
static DWORD WINAPI hDeleteIpForwardEntry(void *route)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oDeleteIpForwardEntry(route);
    route_refused("DeleteIpForwardEntry", WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

// Nor are the PC's IP addresses (D1GP imports AddIPAddress and DeleteIPAddress, a cabinet's own
// network): refused the same way
static DWORD(WINAPI *oAddIPAddress)(ULONG, ULONG, DWORD, PULONG, PULONG);
static DWORD WINAPI hAddIPAddress(ULONG address, ULONG mask, DWORD ifIndex, PULONG context, PULONG instance)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oAddIPAddress(address, mask, ifIndex, context, instance);
    route_refused("AddIPAddress", WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

static DWORD(WINAPI *oDeleteIPAddress)(ULONG);
static DWORD WINAPI hDeleteIPAddress(ULONG context)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oDeleteIPAddress(context);
    route_refused("DeleteIPAddress", WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

// The PC's network connection, shared drives, services and event log: an ALLS cabinet's
// amdaemon.exe (Sega World Drivers Championship) imports what releases or renews the PC's
// address, joins or leaves a Wi-Fi network, maps or drops a network drive, stops a service and
// clears the event log. Refused as for a program without the administrator's rights.
static void pc_refused(const char *what, const void *caller)
{
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 20)
    {
        char where[128];
        wl_log("%s refused: the PC's settings are left alone [%s]", what, where_of(caller, where, sizeof where));
    }
}

static DWORD(WINAPI *oIpReleaseAddress)(void *);
static DWORD WINAPI hIpReleaseAddress(void *adapter)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oIpReleaseAddress(adapter);
    pc_refused("IpReleaseAddress", WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

static DWORD(WINAPI *oIpRenewAddress)(void *);
static DWORD WINAPI hIpRenewAddress(void *adapter)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oIpRenewAddress(adapter);
    pc_refused("IpRenewAddress", WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

static DWORD(WINAPI *oWlanConnect)(HANDLE, const GUID *, const void *, void *);
static DWORD WINAPI hWlanConnect(HANDLE client, const GUID *nic, const void *params, void *reserved)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oWlanConnect(client, nic, params, reserved);
    pc_refused("WlanConnect", WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

static DWORD(WINAPI *oWlanDisconnect)(HANDLE, const GUID *, void *);
static DWORD WINAPI hWlanDisconnect(HANDLE client, const GUID *nic, void *reserved)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oWlanDisconnect(client, nic, reserved);
    pc_refused("WlanDisconnect", WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

static DWORD(WINAPI *oWlanSetProfile)(HANDLE, const GUID *, DWORD, LPCWSTR, LPCWSTR, BOOL, void *, DWORD *);
static DWORD WINAPI hWlanSetProfile(HANDLE client, const GUID *nic, DWORD flags, LPCWSTR xml, LPCWSTR security, BOOL overwrite, void *reserved,
                                    DWORD *reason)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oWlanSetProfile(client, nic, flags, xml, security, overwrite, reserved, reason);
    pc_refused("WlanSetProfile", WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

static DWORD(WINAPI *oWNetAddConnection2W)(void *, LPCWSTR, LPCWSTR, DWORD);
static DWORD WINAPI hWNetAddConnection2W(void *resource, LPCWSTR password, LPCWSTR user, DWORD flags)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oWNetAddConnection2W(resource, password, user, flags);
    pc_refused("WNetAddConnection2W", WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

static DWORD(WINAPI *oWNetCancelConnection2W)(LPCWSTR, DWORD, BOOL);
static DWORD WINAPI hWNetCancelConnection2W(LPCWSTR name, DWORD flags, BOOL force)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oWNetCancelConnection2W(name, flags, force);
    pc_refused("WNetCancelConnection2W", WL_CALLER);
    return ERROR_ACCESS_DENIED;
}

static BOOL(WINAPI *oControlService)(SC_HANDLE, DWORD, SERVICE_STATUS *);
static BOOL WINAPI hControlService(SC_HANDLE service, DWORD control, SERVICE_STATUS *status)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oControlService(service, control, status);
    pc_refused("ControlService", WL_CALLER);
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

static BOOL(WINAPI *oClearEventLogA)(HANDLE, LPCSTR);
static BOOL WINAPI hClearEventLogA(HANDLE log, LPCSTR backup)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oClearEventLogA(log, backup);
    pc_refused("ClearEventLogA", WL_CALLER);
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

static BOOL(WINAPI *oClearEventLogW)(HANDLE, LPCWSTR);
static BOOL WINAPI hClearEventLogW(HANDLE log, LPCWSTR backup)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER))
        return oClearEventLogW(log, backup);
    pc_refused("ClearEventLogW", WL_CALLER);
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

// The shell's moves and deletes (a folder and all it holds) reach DeleteFile from Windows' own
// DLLs, which the file hooks let through: allowed only inside the program's folder (amdaemon
// cleans its logs up with it)
static int in_own_folder(LPCWSTR list)
{
    wchar_t own[MAX_PATH], full[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, own, MAX_PATH);
    while (n && own[n - 1] != L'\\')
        n--;
    own[n] = 0;
    for (LPCWSTR p = list; p && *p; p += wcslen(p) + 1)
    {
        DWORD len = GetFullPathNameW(p, MAX_PATH, full, NULL);
        if (!n || !len || len >= MAX_PATH || _wcsnicmp(full, own, n) || wcsstr(full + n, L".."))
            return 0;
    }
    return 1;
}

static int(WINAPI *oSHFileOperationW)(SHFILEOPSTRUCTW *);
static int WINAPI hSHFileOperationW(SHFILEOPSTRUCTW *op)
{
    if (wl_busy() || wl_is_system_caller(WL_CALLER) || !op || op->wFunc == FO_COPY ||
        (in_own_folder(op->pFrom) && (op->wFunc == FO_DELETE || in_own_folder(op->pTo))))
        return oSHFileOperationW(op);
    char from[MAX_PATH];
    char what[MAX_PATH + 48];
    snprintf(what, sizeof what, "SHFileOperationW %s \"%s\"", op->wFunc == FO_DELETE ? "delete" : op->wFunc == FO_MOVE ? "move" : "rename",
             op->pFrom ? wl_u8(op->pFrom, from, sizeof from) : "");
    pc_refused(what, WL_CALLER);
    return 0x78; // DE_ACCESSDENIEDSRC
}

// ------------------------------------------------------------------------------
// End of the game
// ------------------------------------------------------------------------------

static void(WINAPI *oExitProcess)(UINT);
static void WINAPI hExitProcess(UINT code)
{
    char where[128];
    wl_log("ExitProcess(%u) [%s]", code, where_of(WL_CALLER, where, sizeof where));
    wl_outputs_stop(); // "mame_stop" to BackForceFeeder, while the server's thread still runs
    oExitProcess(code);
}

static BOOL(WINAPI *oTerminateProcess)(HANDLE, UINT);
static BOOL WINAPI hTerminateProcess(HANDLE process, UINT code)
{
    if (process == GetCurrentProcess() || GetProcessId(process) == GetCurrentProcessId())
    {
        if (!wl_busy())
        {
            char where[128];
            wl_log("TerminateProcess(self, %u) [%s]", code, where_of(WL_CALLER, where, sizeof where));
        }
        wl_outputs_stop(); // the agent's and the menu's "quit" too
    }
    return oTerminateProcess(process, code);
}

static WlHook g_hooks[] = {
    WL_HOOK(L"kernel32.dll", LoadLibraryA),
    WL_HOOK(L"kernel32.dll", LoadLibraryW),
    WL_HOOK(L"kernel32.dll", LoadLibraryExA),
    WL_HOOK(L"kernel32.dll", LoadLibraryExW),
    WL_HOOK(L"kernel32.dll", GetProcAddress),
    WL_HOOK(L"kernel32.dll", CreateMutexA),
    WL_HOOK(L"kernel32.dll", CreateMutexW),
    WL_HOOK(L"kernel32.dll", OpenMutexA),
    WL_HOOK(L"kernel32.dll", OpenMutexW),
    WL_HOOK(L"kernel32.dll", CreateProcessA),
    WL_HOOK(L"kernel32.dll", CreateProcessW),
    WL_HOOK(L"kernel32.dll", WinExec),
    WL_HOOK(L"kernel32.dll", OutputDebugStringA),
    WL_HOOK(L"kernel32.dll", OutputDebugStringW),
    WL_HOOK(L"kernel32.dll", IsDebuggerPresent),
    WL_HOOK(L"kernel32.dll", ExitProcess),
    WL_HOOK(L"kernel32.dll", TerminateProcess),
    WL_HOOK(L"kernel32.dll", SetLocalTime),
    WL_HOOK(L"kernel32.dll", SetSystemTime),
    WL_HOOK(L"kernel32.dll", SetTimeZoneInformation),
    WL_HOOK(L"advapi32.dll", RegOpenKeyExA),
    WL_HOOK(L"advapi32.dll", RegOpenKeyExW),
    WL_HOOK(L"advapi32.dll", RegCreateKeyExA),
    WL_HOOK(L"advapi32.dll", RegCreateKeyExW),
    WL_HOOK(L"advapi32.dll", RegCloseKey),
    WL_HOOK(L"advapi32.dll", RegQueryValueExA),
    WL_HOOK(L"advapi32.dll", RegQueryValueExW),
    WL_HOOK(L"advapi32.dll", RegSetValueExA),
    WL_HOOK(L"advapi32.dll", RegSetValueExW),
    WL_HOOK(L"advapi32.dll", RegEnumValueA),
    WL_HOOK(L"advapi32.dll", RegEnumValueW),
    WL_HOOK(L"advapi32.dll", RegQueryInfoKeyA),
    WL_HOOK(L"advapi32.dll", RegQueryInfoKeyW),
    WL_HOOK(L"advapi32.dll", RegDeleteKeyA),
    WL_HOOK(L"advapi32.dll", RegDeleteKeyW),
    WL_HOOK(L"advapi32.dll", RegDeleteKeyExA),
    WL_HOOK(L"advapi32.dll", RegDeleteKeyExW),
    WL_HOOK(L"advapi32.dll", RegDeleteTreeA),
    WL_HOOK(L"advapi32.dll", RegDeleteTreeW),
    WL_HOOK(L"advapi32.dll", RegDeleteValueA),
    WL_HOOK(L"advapi32.dll", RegDeleteValueW),
    WL_HOOK(L"shell32.dll", ShellExecuteA),
    WL_HOOK(L"shell32.dll", ShellExecuteW),
    WL_HOOK(L"shell32.dll", ShellExecuteExA),
    WL_HOOK(L"shell32.dll", ShellExecuteExW),
    WL_HOOK(L"user32.dll", ExitWindowsEx),
    WL_HOOK(L"user32.dll", SystemParametersInfoA),
    WL_HOOK(L"user32.dll", SystemParametersInfoW),
    WL_HOOK(L"user32.dll", SetWindowsHookExA),
    WL_HOOK(L"user32.dll", SetWindowsHookExW),
    WL_HOOK(L"user32.dll", UnhookWindowsHookEx),
    WL_HOOK(L"user32.dll", SendInput),
    WL_HOOK(L"user32.dll", MessageBoxA),
    WL_HOOK(L"user32.dll", MessageBoxW),
    WL_HOOK(L"user32.dll", GetSystemMetrics),
    WL_HOOK(L"advapi32.dll", InitiateSystemShutdownExA),
    WL_HOOK(L"advapi32.dll", InitiateSystemShutdownExW),
    WL_HOOK(L"powrprof.dll", SetSuspendState),
    WL_HOOK(L"winhttp.dll", WinHttpConnect),
    WL_HOOK(L"iphlpapi.dll", CreateIpForwardEntry),
    WL_HOOK(L"iphlpapi.dll", SetIpForwardEntry),
    WL_HOOK(L"iphlpapi.dll", DeleteIpForwardEntry),
    WL_HOOK(L"iphlpapi.dll", AddIPAddress),
    WL_HOOK(L"iphlpapi.dll", DeleteIPAddress),
    WL_HOOK(L"iphlpapi.dll", IpReleaseAddress),
    WL_HOOK(L"iphlpapi.dll", IpRenewAddress),
    WL_HOOK(L"wlanapi.dll", WlanConnect),
    WL_HOOK(L"wlanapi.dll", WlanDisconnect),
    WL_HOOK(L"wlanapi.dll", WlanSetProfile),
    WL_HOOK(L"mpr.dll", WNetAddConnection2W),
    WL_HOOK(L"mpr.dll", WNetCancelConnection2W),
    WL_HOOK(L"advapi32.dll", ControlService),
    WL_HOOK(L"advapi32.dll", ClearEventLogA),
    WL_HOOK(L"advapi32.dll", ClearEventLogW),
    WL_HOOK(L"shell32.dll", SHFileOperationW),
    WL_HOOK(L"avicap32.dll", capCreateCaptureWindowA),
    WL_HOOK(L"avicap32.dll", capCreateCaptureWindowW),
    WL_HOOK(L"avicap32.dll", capGetDriverDescriptionA),
    WL_HOOK(L"avicap32.dll", capGetDriverDescriptionW),
    WL_HOOK(L"mf.dll", MFEnumDeviceSources),
    WL_HOOK(L"mf.dll", MFCreateDeviceSource),
    WL_HOOK(L"ole32.dll", CoCreateInstance),
    WL_HOOK(L"ws2_32.dll", bind),
    WL_HOOK(L"ws2_32.dll", connect),
    WL_HOOK(L"ws2_32.dll", getsockname),
    WL_HOOK(L"gdi32.dll", AddFontResourceExA),
    WL_HOOK(L"gdi32.dll", AddFontResourceExW),
    WL_HOOK(L"gdi32.dll", AddFontResourceA),
    WL_HOOK(L"gdi32.dll", AddFontResourceW),
    WL_HOOK(L"ws2_32.dll", sendto),
};

void wl_system_hooks(void)
{
    wl_hooks_add(g_hooks, sizeof g_hooks / sizeof g_hooks[0]);
}
