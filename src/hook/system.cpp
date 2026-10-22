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
#include <stdio.h>

struct sockaddr; // read as bytes: winsock's headers are not needed

#include "hooks.h"
#include "ioports.h"
#include "log.h"

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

static HMODULE(WINAPI *oLoadLibraryA)(LPCSTR);
static HMODULE WINAPI hLoadLibraryA(LPCSTR name)
{
    int nested = wl_busy();
    if (!nested && wl_ioports_is_driver_dll(name) && !wl_is_system_caller(WL_CALLER))
        return port_driver(name, WL_CALLER);
    if (!nested && refused_dll(name, WL_CALLER))
        return NULL;
    wl_busy_enter();
    HMODULE mod = oLoadLibraryA(name);
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
    if (!nested && name && refused_dll(wl_u8(name, n8, sizeof n8), WL_CALLER))
        return NULL;
    wl_busy_enter();
    HMODULE mod = oLoadLibraryW(name);
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
    if (!nested && refused_dll(name, WL_CALLER))
        return NULL;
    wl_busy_enter();
    HMODULE mod = oLoadLibraryExA(name, file, flags);
    wl_busy_leave();
    DWORD err = GetLastError();
    if (!nested && name)
        on_load(name, mod, err, WL_CALLER);
    SetLastError(err);
    return mod;
}

static HMODULE(WINAPI *oLoadLibraryExW)(LPCWSTR, HANDLE, DWORD);
static HMODULE WINAPI hLoadLibraryExW(LPCWSTR name, HANDLE file, DWORD flags)
{
    int nested = wl_busy();
    char n8[MAX_PATH * 2];
    if (!nested && name && wl_ioports_is_driver_dll(wl_u8(name, n8, sizeof n8)) && !wl_is_system_caller(WL_CALLER))
        return port_driver(n8, WL_CALLER);
    if (!nested && name && refused_dll(wl_u8(name, n8, sizeof n8), WL_CALLER))
        return NULL;
    wl_busy_enter();
    HMODULE mod = oLoadLibraryExW(name, file, flags);
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
// The keys opened under one of those are followed too (a small table of handles).
// ------------------------------------------------------------------------------

#define KEY_WRITES (KEY_SET_VALUE | KEY_CREATE_SUB_KEY | KEY_CREATE_LINK | DELETE | WRITE_DAC | WRITE_OWNER)

static HKEY g_hklmKeys[64];

static int is_hklm(HKEY key)
{
    if (key == HKEY_LOCAL_MACHINE)
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
    wl_log("%s HKLM\\...\\%s: opened read only [%s]", fn, sub ? sub : "", where_of(caller, where, sizeof where));
    sam &= ~(REGSAM)KEY_WRITES;
    return (sam & (KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | KEY_NOTIFY)) ? sam : sam | KEY_READ;
}

static LSTATUS(WINAPI *oRegOpenKeyExA)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
static LSTATUS WINAPI hRegOpenKeyExA(HKEY key, LPCSTR sub, DWORD options, REGSAM sam, PHKEY result)
{
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
    if (wl_busy() || wl_is_system_caller(WL_CALLER) || !is_hklm(key))
        return oRegOpenKeyExW(key, sub, options, sam, result);
    char s[512];
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
    if (wl_busy() || wl_is_system_caller(WL_CALLER) || !is_hklm(key))
        return oRegCreateKeyExW(key, sub, reserved, cls, options, sam, sa, result, disposition);
    char s[512];
    LSTATUS r = oRegOpenKeyExW(key, sub, 0, hklm_rights(sam | KEY_CREATE_SUB_KEY, "RegCreateKeyEx", sub ? wl_u8(sub, s, sizeof s) : NULL, WL_CALLER),
                               result);
    if (r == ERROR_SUCCESS && result)
        hklm_add(*result);
    if (r == ERROR_SUCCESS && disposition)
        *disposition = REG_OPENED_EXISTING_KEY;
    return r == ERROR_FILE_NOT_FOUND ? ERROR_ACCESS_DENIED : r;
}

static LSTATUS(WINAPI *oRegCloseKey)(HKEY);
static LSTATUS WINAPI hRegCloseKey(HKEY key)
{
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

static void OutputDebugString_log(const char *text)
{
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
// pictures through Video for Windows). A game running on someone's PC gets no camera.
// ------------------------------------------------------------------------------

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
    camera_refused("capCreateCaptureWindow", WL_CALLER);
    return NULL;
}

static HWND(WINAPI *ocapCreateCaptureWindowW)(LPCWSTR, DWORD, int, int, int, int, HWND, int);
static HWND WINAPI hcapCreateCaptureWindowW(LPCWSTR name, DWORD style, int x, int y, int w, int h, HWND parent, int id)
{
    camera_refused("capCreateCaptureWindow", WL_CALLER);
    return NULL;
}

static BOOL(WINAPI *ocapGetDriverDescriptionA)(UINT, LPSTR, int, LPSTR, int);
static BOOL WINAPI hcapGetDriverDescriptionA(UINT index, LPSTR name, int nameSize, LPSTR ver, int verSize)
{
    camera_refused("capGetDriverDescription", WL_CALLER);
    return FALSE;
}

static BOOL(WINAPI *ocapGetDriverDescriptionW)(UINT, LPWSTR, int, LPWSTR, int);
static BOOL WINAPI hcapGetDriverDescriptionW(UINT index, LPWSTR name, int nameSize, LPWSTR ver, int verSize)
{
    camera_refused("capGetDriverDescription", WL_CALLER);
    return FALSE;
}

// COM objects the game creates: DirectShow's device enumerator would list the cameras too
static const GUID g_systemDeviceEnum = {0x62BE5D10, 0x60EB, 0x11D0, {0xBD, 0x3B, 0x00, 0xA0, 0xC9, 0x11, 0xCE, 0x86}};

static HRESULT(WINAPI *oCoCreateInstance)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID *);
static HRESULT WINAPI hCoCreateInstance(REFCLSID cls, LPUNKNOWN outer, DWORD ctx, REFIID iid, LPVOID *out)
{
    int nested = wl_busy();
    if (!nested && !wl_is_system_caller(WL_CALLER) && IsEqualCLSID(cls, g_systemDeviceEnum))
    {
        camera_refused("CoCreateInstance(SystemDeviceEnum)", WL_CALLER);
        if (out)
            *out = NULL;
        return REGDB_E_CLASSNOTREG;
    }
    wl_busy_enter();
    HRESULT hr = oCoCreateInstance(cls, outer, ctx, iid, out);
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

static int(WINAPI *oconnect)(UINT_PTR, const struct sockaddr *, int);
static int WINAPI hconnect(UINT_PTR s, const struct sockaddr *a, int len)
{
    int game = !wl_busy() && !wl_is_system_caller(WL_CALLER);
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

static int(WINAPI *osendto)(UINT_PTR, const char *, int, int, const struct sockaddr *, int);
static int WINAPI hsendto(UINT_PTR s, const char *buf, int n, int flags, const struct sockaddr *a, int len)
{
    static volatile LONG logged;
    const unsigned char *b = (const unsigned char *)a;
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

// Network adapters: some games check a MAC address
static ULONG(WINAPI *oGetAdaptersInfo)(void *, PULONG);
static ULONG WINAPI hGetAdaptersInfo(void *info, PULONG size)
{
    int nested = wl_busy();
    wl_busy_enter();
    ULONG r = oGetAdaptersInfo(info, size);
    wl_busy_leave();
    if (!nested && !wl_is_system_caller(WL_CALLER))
    {
        char where[128];
        wl_log("GetAdaptersInfo(%s) -> %lu, size %lu [%s]", info ? "buffer" : "size query", r, size ? *size : 0, where_of(WL_CALLER, where, sizeof where));
    }
    return r;
}

// ------------------------------------------------------------------------------
// End of the game
// ------------------------------------------------------------------------------

static void(WINAPI *oExitProcess)(UINT);
static void WINAPI hExitProcess(UINT code)
{
    char where[128];
    wl_log("ExitProcess(%u) [%s]", code, where_of(WL_CALLER, where, sizeof where));
    oExitProcess(code);
}

static BOOL(WINAPI *oTerminateProcess)(HANDLE, UINT);
static BOOL WINAPI hTerminateProcess(HANDLE process, UINT code)
{
    if (!wl_busy() && (process == GetCurrentProcess() || GetProcessId(process) == GetCurrentProcessId()))
    {
        char where[128];
        wl_log("TerminateProcess(self, %u) [%s]", code, where_of(WL_CALLER, where, sizeof where));
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
    WL_HOOK(L"iphlpapi.dll", GetAdaptersInfo),
    WL_HOOK(L"avicap32.dll", capCreateCaptureWindowA),
    WL_HOOK(L"avicap32.dll", capCreateCaptureWindowW),
    WL_HOOK(L"avicap32.dll", capGetDriverDescriptionA),
    WL_HOOK(L"avicap32.dll", capGetDriverDescriptionW),
    WL_HOOK(L"ole32.dll", CoCreateInstance),
    WL_HOOK(L"ws2_32.dll", bind),
    WL_HOOK(L"ws2_32.dll", connect),
    WL_HOOK(L"ws2_32.dll", sendto),
};

void wl_system_hooks(void)
{
    wl_hooks_add(g_hooks, sizeof g_hooks / sizeof g_hooks[0]);
}
