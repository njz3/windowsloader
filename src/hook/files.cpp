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

// Observer for files: what the game opens, looks for, creates and reads from .ini files.
// Device handles (COM, HID) are handed to devices.cpp, which logs their traffic.

#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "devices.h"
#include "hooks.h"
#include "log.h"
#include "vdev.h"
#include "vhid.h"

// Plain file events are many; past this count only devices are still logged
#define FILE_EVENT_LIMIT 5000

static volatile LONG g_fileEvents;

static int file_event(void)
{
    LONG n = InterlockedIncrement(&g_fileEvents);
    if (n == FILE_EVENT_LIMIT + 1)
        wl_log("further file events not logged (limit %d)", FILE_EVENT_LIMIT);
    return n <= FILE_EVENT_LIMIT;
}

static const char *disposition(DWORD d)
{
    switch (d)
    {
    case CREATE_NEW:
        return "create-new";
    case CREATE_ALWAYS:
        return "create";
    case OPEN_EXISTING:
        return "open";
    case OPEN_ALWAYS:
        return "open-always";
    case TRUNCATE_EXISTING:
        return "truncate";
    default:
        return "?";
    }
}

static const char *result(HANDLE h, DWORD err, char *buf, size_t size)
{
    if (h == INVALID_HANDLE_VALUE)
        snprintf(buf, size, "failed (error %lu)", err);
    else
        snprintf(buf, size, "%p", h);
    return buf;
}

// ------------------------------------------------------------------------------
// A game's own log that can grow without end (Daytona's GameData\debug.txt and its memory dumps):
// once a handle on it has had its share written, the game's further writes are dropped (and told
// done), so it cannot fill the disk
// ------------------------------------------------------------------------------

static char g_capName[64];
static unsigned long long g_capBytes;

struct CappedFile
{
    HANDLE h;
    unsigned long long written;
    int told;
};
static CappedFile g_capped[8];
static SRWLOCK g_capLock = SRWLOCK_INIT;

void wl_files_cap(const char *nameEnd, unsigned long long maxBytes)
{
    snprintf(g_capName, sizeof g_capName, "%s", nameEnd);
    g_capBytes = maxBytes;
}

static void cap_track(const char *path, HANDLE h)
{
    size_t n = strlen(path), m = strlen(g_capName);
    if (!m || h == INVALID_HANDLE_VALUE || n < m || _stricmp(path + n - m, g_capName))
        return;
    AcquireSRWLockExclusive(&g_capLock);
    for (int i = 0; i < (int)(sizeof g_capped / sizeof g_capped[0]); i++)
        if (!g_capped[i].h)
        {
            g_capped[i].h = h;
            g_capped[i].written = 0;
            g_capped[i].told = 0;
            break;
        }
    ReleaseSRWLockExclusive(&g_capLock);
}

// 1 when the write is dropped
static int cap_write(HANDLE h, DWORD size)
{
    int drop = 0, tell = 0;
    AcquireSRWLockExclusive(&g_capLock);
    for (int i = 0; i < (int)(sizeof g_capped / sizeof g_capped[0]); i++)
        if (g_capped[i].h == h)
        {
            drop = g_capped[i].written >= g_capBytes;
            if (!drop)
                g_capped[i].written += size;
            else if (!g_capped[i].told)
                tell = g_capped[i].told = 1;
        }
    ReleaseSRWLockExclusive(&g_capLock);
    if (tell)
        wl_log("WriteFile ...%s: %llu MB written, the game's further writes dropped", g_capName, g_capBytes >> 20);
    return drop;
}

static void cap_forget(HANDLE h)
{
    AcquireSRWLockExclusive(&g_capLock);
    for (int i = 0; i < (int)(sizeof g_capped / sizeof g_capped[0]); i++)
        if (g_capped[i].h == h)
            g_capped[i].h = NULL;
    ReleaseSRWLockExclusive(&g_capLock);
}

// ------------------------------------------------------------------------------
// Fixed folders redirected (wl_files_redirect): C:\Sega, a Taito Type X's D:\ and E:\...
// ------------------------------------------------------------------------------

static struct
{
    char from[MAX_PATH], to[MAX_PATH];
    wchar_t toW[MAX_PATH];
    size_t len;
    int prefix; // the text replaced as it is, no separator added (wl_files_redirect_prefix)
} g_redirs[8];
static int g_redirCount;

static void add_redirect(const char *from, const char *to, int prefix)
{
    if (g_redirCount >= (int)(sizeof g_redirs / sizeof g_redirs[0]))
        return;
    snprintf(g_redirs[g_redirCount].from, MAX_PATH, "%s", from);
    snprintf(g_redirs[g_redirCount].to, MAX_PATH, "%s", to);
    size_t len = strlen(g_redirs[g_redirCount].from);
    while (!prefix && len && (g_redirs[g_redirCount].from[len - 1] == '\\' || g_redirs[g_redirCount].from[len - 1] == '/'))
        g_redirs[g_redirCount].from[--len] = 0;
    g_redirs[g_redirCount].len = len;
    g_redirs[g_redirCount].prefix = prefix;
    MultiByteToWideChar(CP_ACP, 0, g_redirs[g_redirCount].to, -1, g_redirs[g_redirCount].toW, MAX_PATH);
    wl_log("files: %s redirected to %s%s", g_redirs[g_redirCount].from, g_redirs[g_redirCount].to, prefix ? " (as a prefix)" : "");
    g_redirCount++;
}

void wl_files_redirect(const char *from, const char *to)
{
    add_redirect(from, to, 0);
}

void wl_files_redirect_prefix(const char *from, const char *to)
{
    add_redirect(from, to, 1);
}

static int same_char(int a, int b)
{
    if (a == '/')
        a = '\\';
    if (b == '/')
        b = '\\';
    return tolower(a) == tolower(b);
}

// A drive ("E:"): "E:.\data" is relative to its current folder, its root here (Chase H.Q. 2's fonts;
// the Taito module gives E:\ as the current folder)
static int is_drive(int r)
{
    return g_redirs[r].len == 2 && g_redirs[r].from[1] == ':';
}

// The path itself, or the same one under the redirected folder (written in buf)
static const char *redir(const char *path, char *buf, size_t size)
{
    for (int r = 0; path && r < g_redirCount; r++)
    {
        size_t i = 0;
        while (i < g_redirs[r].len && same_char((unsigned char)path[i], (unsigned char)g_redirs[r].from[i]))
            i++;
        const char *rest = path + i;
        int relative = *rest && *rest != '\\' && *rest != '/';
        if (i < g_redirs[r].len || (relative && !is_drive(r) && !g_redirs[r].prefix))
            continue;
        snprintf(buf, size, relative && !g_redirs[r].prefix ? "%s\\%s" : "%s%s", g_redirs[r].to, rest);
        return buf;
    }
    return path;
}

static const wchar_t *redirW(const wchar_t *path, wchar_t *buf, size_t size)
{
    for (int r = 0; path && r < g_redirCount; r++)
    {
        size_t i = 0;
        while (i < g_redirs[r].len && path[i] <= 0x7F && same_char(path[i], (unsigned char)g_redirs[r].from[i]))
            i++;
        const wchar_t *rest = path + i;
        int relative = *rest && *rest != L'\\' && *rest != L'/';
        if (i < g_redirs[r].len || (relative && !is_drive(r) && !g_redirs[r].prefix))
            continue;
        swprintf(buf, size, relative && !g_redirs[r].prefix ? L"%s\\%s" : L"%s%s", g_redirs[r].toW, rest);
        return buf;
    }
    return path;
}

const wchar_t *wl_files_redirectedW(const wchar_t *path, wchar_t *buf, size_t size)
{
    return redirW(path, buf, size);
}

// ------------------------------------------------------------------------------
// CreateFile / ReadFile / WriteFile / CloseHandle
// ------------------------------------------------------------------------------

static void on_create(const char *path, DWORD access, DWORD disp, DWORD flags, HANDLE h, DWORD err, const void *caller)
{
    // Devices opened by Windows itself (DirectInput scanning the joysticks) are not the cabinet's
    if (wl_is_system_caller(caller))
        return;
    cap_track(path, h);
    int device = wl_dev_is_device_path(path);
    if (!device && !file_event())
        return;

    char res[48], where[128];
    const char *rw = (access & (GENERIC_READ | GENERIC_ALL | FILE_READ_DATA)) && (access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA)) ? "rw"
                     : access & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA)                                                             ? "w"
                     : access & (GENERIC_READ | FILE_READ_DATA)                                                                                  ? "r"
                                                                                                                                                 : "-";
    wl_log("CreateFile%s \"%s\" %s %s flags=0x%lx -> %s [%s]", device ? " (device)" : "", path, rw, disposition(disp), flags,
           result(h, err, res, sizeof res), wl_where(caller, where, sizeof where));
    if (device && h != INVALID_HANDLE_VALUE)
        wl_dev_track(h, path);
}

static HANDLE open_vdev(const WlVirtualDevice *device, const char *path, const void *caller)
{
    HANDLE h = wl_vdev_open(device);
    char where[128];
    wl_log("CreateFile \"%s\" -> %p (emulated) [%s]", path, h, wl_where(caller, where, sizeof where));
    if (h != INVALID_HANDLE_VALUE)
        wl_dev_track(h, path);
    return h;
}

static HANDLE open_vhid(const char *path, const void *caller)
{
    HANDLE h = wl_vhid_open();
    char where[128];
    wl_log("CreateFile \"%s\" -> %p (emulated board) [%s]", path, h, wl_where(caller, where, sizeof where));
    return h;
}

// A serial or parallel port windowsloader does not emulate is refused to the game, as if missing:
// the cabinet's are not on this PC, and the PC's own lead elsewhere (a com0com pair to another
// program, a Bluetooth link). Mario Kart Arcade GP DX tries COM0 to COM15.
static int port_refused(const char *path, const void *caller)
{
    if (!path || wl_is_system_caller(caller))
        return 0;
    const char *p = path;
    if (p[0] == '\\' && p[1] == '\\' && (p[2] == '.' || p[2] == '?') && p[3] == '\\')
        p += 4;
    if ((_strnicmp(p, "COM", 3) && _strnicmp(p, "LPT", 3)) || !isdigit((unsigned char)p[3]))
        return 0;
    const char *q = p + 3;
    while (isdigit((unsigned char)*q))
        q++;
    if (*q && !(*q == ':' && !q[1]))
        return 0;
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 20)
    {
        char where[128];
        wl_log("CreateFile \"%s\" refused: not emulated, the PC's own ports are not the cabinet's [%s]", path, wl_where(caller, where, sizeof where));
    }
    SetLastError(ERROR_FILE_NOT_FOUND);
    return 1;
}

static HANDLE(WINAPI *oCreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

// A port passed through to a real board (vdev.cpp): the PC's port the settings name is opened
static HANDLE open_real_port(const char *path, const char *real, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags,
                             HANDLE tmpl, const void *caller)
{
    wl_busy_enter();
    HANDLE h = oCreateFileA(real, access, share, sa, disp, flags, tmpl);
    DWORD err = GetLastError();
    wl_busy_leave();
    char where[128];
    wl_log("CreateFile \"%s\" -> %s: %p (passthrough, error %lu) [%s]", path, real, h, h == INVALID_HANDLE_VALUE ? err : 0, wl_where(caller, where, sizeof where));
    SetLastError(err);
    return h;
}

static HANDLE WINAPI hCreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl)
{
    int nested = wl_busy();
    if (!nested && wl_vhid_is_path(name))
        return open_vhid(name, WL_CALLER);
    const char *real = nested ? NULL : wl_vdev_real_port(name);
    if (real)
        return open_real_port(name, real, access, share, sa, disp, flags, tmpl, WL_CALLER);
    const WlVirtualDevice *vdev = nested ? NULL : wl_vdev_by_path(name);
    if (vdev)
        return open_vdev(vdev, name, WL_CALLER);
    if (!nested && port_refused(name, WL_CALLER))
        return INVALID_HANDLE_VALUE;
    char rbuf[MAX_PATH * 2];
    if (!nested)
        name = redir(name, rbuf, sizeof rbuf);
    wl_busy_enter();
    HANDLE h = oCreateFileA(name, access, share, sa, disp, flags, tmpl);
    wl_busy_leave();
    DWORD err = GetLastError();
    if (!nested && name)
        on_create(name, access, disp, flags, h, err, WL_CALLER);
    SetLastError(err);
    return h;
}

static HANDLE(WINAPI *oCreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static HANDLE WINAPI hCreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl)
{
    int nested = wl_busy();
    if (!nested && name && wl_vhid_active())
    {
        char path[MAX_PATH * 2];
        if (wl_vhid_is_path(wl_u8(name, path, sizeof path)))
            return open_vhid(path, WL_CALLER);
    }
    if (!nested && name)
    {
        char path[MAX_PATH * 2];
        const WlVirtualDevice *vdev = wl_vdev_by_path(wl_u8(name, path, sizeof path));
        if (vdev)
            return open_vdev(vdev, path, WL_CALLER);
        const char *real = wl_vdev_real_port(path);
        if (real)
            return open_real_port(path, real, access, share, sa, disp, flags, tmpl, WL_CALLER);
        if (port_refused(path, WL_CALLER))
            return INVALID_HANDLE_VALUE;
    }
    wchar_t rbuf[MAX_PATH * 2];
    if (!nested)
        name = redirW(name, rbuf, MAX_PATH * 2);
    wl_busy_enter();
    HANDLE h = oCreateFileW(name, access, share, sa, disp, flags, tmpl);
    wl_busy_leave();
    DWORD err = GetLastError();
    if (!nested && name)
    {
        char path[MAX_PATH * 2];
        on_create(wl_u8(name, path, sizeof path), access, disp, flags, h, err, WL_CALLER);
    }
    SetLastError(err);
    return h;
}

static BOOL(WINAPI *oReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL WINAPI hReadFile(HANDLE h, LPVOID buf, DWORD size, LPDWORD read, LPOVERLAPPED ov)
{
    int nested = wl_busy();
    if (!nested && wl_vhid_is_handle(h))
        return wl_vhid_read(h, buf, size, read, ov);
    int emulated = !nested && wl_vdev_of(h);
    wl_busy_enter();
    BOOL ok = emulated ? wl_vdev_read(h, buf, size, read, ov) : oReadFile(h, buf, size, read, ov);
    wl_busy_leave();
    if (nested)
        return ok;

    DWORD err = GetLastError();
    DWORD got = ok ? (read ? *read : ov ? (DWORD)ov->InternalHigh : 0) : 0;
    WlDevice d;
    if (!ok && err == ERROR_IO_PENDING)
    {
        if (wl_dev_count(h, WL_DEV_IO, &d))
            wl_log("ReadFile %s (%lu) -> pending", d.name, size);
    }
    else if (!ok || got)
    {
        if (wl_dev_count(h, WL_DEV_IO, &d))
        {
            char prefix[160];
            snprintf(prefix, sizeof prefix, "ReadFile %s (%lu) -> %s %lu:", d.name, size, ok ? "got" : "failed", got);
            wl_log_hex(prefix, buf, got);
        }
    }
    else if (wl_dev_count(h, WL_DEV_EMPTY, &d))
        wl_log("ReadFile %s (%lu) -> nothing", d.name, size);
    SetLastError(err);
    return ok;
}

static BOOL(WINAPI *oWriteFile)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL WINAPI hWriteFile(HANDLE h, LPCVOID buf, DWORD size, LPDWORD written, LPOVERLAPPED ov)
{
    int nested = wl_busy();
    if (!nested && !ov && g_capBytes && cap_write(h, size))
    {
        if (written)
            *written = size;
        return TRUE;
    }
    int emulated = !nested && wl_vdev_of(h);
    wl_busy_enter();
    BOOL ok = emulated ? wl_vdev_write(h, buf, size, written, ov) : oWriteFile(h, buf, size, written, ov);
    wl_busy_leave();
    if (nested)
        return ok;

    DWORD err = GetLastError();
    WlDevice d;
    if (wl_dev_count(h, WL_DEV_IO, &d))
    {
        char prefix[160];
        snprintf(prefix, sizeof prefix, "WriteFile %s (%lu) -> %s:", d.name, size, ok ? "ok" : err == ERROR_IO_PENDING ? "pending" : "failed");
        wl_log_hex(prefix, buf, size);
    }
    SetLastError(err);
    return ok;
}

static BOOL(WINAPI *oCloseHandle)(HANDLE);
static BOOL WINAPI hCloseHandle(HANDLE h)
{
    int nested = wl_busy();
    WlDevice d;
    // Forget the handle before it is closed: its value can be reused right after
    int was = wl_dev_untrack(h, &d, nested);
    if (!nested && g_capBytes)
        cap_forget(h);
    if (was && !nested)
        wl_log("CloseHandle %s", d.name);
    if (!nested && wl_vhid_is_handle(h))
    {
        wl_vhid_forget(h);
        wl_log("CloseHandle %p (emulated board)", h);
    }
    if (!nested && wl_vdev_of(h))
        wl_vdev_forget(h);
    wl_busy_enter();
    BOOL ok = oCloseHandle(h);
    wl_busy_leave();
    return ok;
}

// ------------------------------------------------------------------------------
// Looking for files and folders
// ------------------------------------------------------------------------------

static void on_attributes(const char *path, DWORD attr, const void *caller)
{
    if (wl_is_system_caller(caller) || !file_event())
        return;
    char where[128];
    if (attr == INVALID_FILE_ATTRIBUTES)
        wl_log("GetFileAttributes \"%s\" -> missing [%s]", path, wl_where(caller, where, sizeof where));
    else
        wl_log("GetFileAttributes \"%s\" -> 0x%lx%s [%s]", path, attr, attr & FILE_ATTRIBUTE_DIRECTORY ? " (folder)" : "",
               wl_where(caller, where, sizeof where));
}

static DWORD(WINAPI *oGetFileAttributesA)(LPCSTR);
static DWORD WINAPI hGetFileAttributesA(LPCSTR name)
{
    int nested = wl_busy();
    char rbuf[MAX_PATH * 2];
    if (!nested)
        name = redir(name, rbuf, sizeof rbuf);
    wl_busy_enter();
    DWORD attr = oGetFileAttributesA(name);
    wl_busy_leave();
    DWORD err = GetLastError();
    if (!nested && name)
        on_attributes(name, attr, WL_CALLER);
    SetLastError(err);
    return attr;
}

static DWORD(WINAPI *oGetFileAttributesW)(LPCWSTR);
static DWORD WINAPI hGetFileAttributesW(LPCWSTR name)
{
    int nested = wl_busy();
    wchar_t rbuf[MAX_PATH * 2];
    if (!nested)
        name = redirW(name, rbuf, MAX_PATH * 2);
    wl_busy_enter();
    DWORD attr = oGetFileAttributesW(name);
    wl_busy_leave();
    DWORD err = GetLastError();
    if (!nested && name)
    {
        char path[MAX_PATH * 2];
        on_attributes(wl_u8(name, path, sizeof path), attr, WL_CALLER);
    }
    SetLastError(err);
    return attr;
}

static HANDLE(WINAPI *oFindFirstFileA)(LPCSTR, LPWIN32_FIND_DATAA);
static HANDLE WINAPI hFindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA data)
{
    int nested = wl_busy();
    char rbuf[MAX_PATH * 2];
    if (!nested)
        pattern = redir(pattern, rbuf, sizeof rbuf);
    wl_busy_enter();
    HANDLE h = oFindFirstFileA(pattern, data);
    wl_busy_leave();
    DWORD err = GetLastError();
    if (!nested && pattern && !wl_is_system_caller(WL_CALLER) && file_event())
    {
        char where[128];
        wl_log("FindFirstFile \"%s\" -> %s [%s]", pattern, h == INVALID_HANDLE_VALUE ? "none" : data->cFileName, wl_where(WL_CALLER, where, sizeof where));
    }
    SetLastError(err);
    return h;
}

static HANDLE(WINAPI *oFindFirstFileW)(LPCWSTR, LPWIN32_FIND_DATAW);
static HANDLE WINAPI hFindFirstFileW(LPCWSTR pattern, LPWIN32_FIND_DATAW data)
{
    int nested = wl_busy();
    wchar_t rbuf[MAX_PATH * 2];
    if (!nested)
        pattern = redirW(pattern, rbuf, MAX_PATH * 2);
    wl_busy_enter();
    HANDLE h = oFindFirstFileW(pattern, data);
    wl_busy_leave();
    DWORD err = GetLastError();
    if (!nested && pattern && !wl_is_system_caller(WL_CALLER) && file_event())
    {
        char p[MAX_PATH * 2], first[MAX_PATH * 2], where[128];
        wl_log("FindFirstFile \"%s\" -> %s [%s]", wl_u8(pattern, p, sizeof p), h == INVALID_HANDLE_VALUE ? "none" : wl_u8(data->cFileName, first, sizeof first),
               wl_where(WL_CALLER, where, sizeof where));
    }
    SetLastError(err);
    return h;
}

static BOOL(WINAPI *oCreateDirectoryA)(LPCSTR, LPSECURITY_ATTRIBUTES);
static BOOL WINAPI hCreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES sa)
{
    int nested = wl_busy();
    char rbuf[MAX_PATH * 2];
    if (!nested)
        path = redir(path, rbuf, sizeof rbuf);
    wl_busy_enter();
    BOOL ok = oCreateDirectoryA(path, sa);
    wl_busy_leave();
    DWORD err = GetLastError();
    if (!nested && path && !wl_is_system_caller(WL_CALLER) && file_event())
        wl_log("CreateDirectory \"%s\" -> %s", path, ok ? "ok" : err == ERROR_ALREADY_EXISTS ? "exists" : "failed");
    SetLastError(err);
    return ok;
}

static BOOL(WINAPI *oCreateDirectoryW)(LPCWSTR, LPSECURITY_ATTRIBUTES);
static BOOL WINAPI hCreateDirectoryW(LPCWSTR path, LPSECURITY_ATTRIBUTES sa)
{
    int nested = wl_busy();
    wchar_t rbuf[MAX_PATH * 2];
    if (!nested)
        path = redirW(path, rbuf, MAX_PATH * 2);
    wl_busy_enter();
    BOOL ok = oCreateDirectoryW(path, sa);
    wl_busy_leave();
    DWORD err = GetLastError();
    if (!nested && path && !wl_is_system_caller(WL_CALLER) && file_event())
    {
        char p[MAX_PATH * 2];
        wl_log("CreateDirectory \"%s\" -> %s", wl_u8(path, p, sizeof p), ok ? "ok" : err == ERROR_ALREADY_EXISTS ? "exists" : "failed");
    }
    SetLastError(err);
    return ok;
}

// ------------------------------------------------------------------------------
// .ini files (GetPrivateProfile*)
// ------------------------------------------------------------------------------

static WlIniOverride g_iniOverride;

void wl_files_set_ini_override(WlIniOverride fn)
{
    g_iniOverride = fn;
}

// The module's value for this key, 0 when the game's file keeps its say
static int ini_override(const char *file, const char *app, const char *key, char *value, size_t size)
{
    return g_iniOverride && file && app && key && g_iniOverride(file, app, key, value, size);
}

static DWORD(WINAPI *oGetPrivateProfileStringA)(LPCSTR, LPCSTR, LPCSTR, LPSTR, DWORD, LPCSTR);
static DWORD WINAPI hGetPrivateProfileStringA(LPCSTR app, LPCSTR key, LPCSTR def, LPSTR ret, DWORD size, LPCSTR file)
{
    int nested = wl_busy();
    char rbuf[MAX_PATH * 2];
    if (!nested)
        file = redir(file, rbuf, sizeof rbuf);
    wl_busy_enter();
    DWORD n = oGetPrivateProfileStringA(app, key, def, ret, size, file);
    wl_busy_leave();
    char over[256];
    if (!nested && ret && size && ini_override(file, app, key, over, sizeof over))
    {
        wl_log("ini %s: [%s] %s = \"%s\" from windowsloader (the file says \"%s\")", file, app, key, over, ret);
        lstrcpynA(ret, over, (int)size);
        return (DWORD)strlen(ret);
    }
    if (!nested && !wl_is_system_caller(WL_CALLER) && file_event())
    {
        if (!app || !key)
            wl_log("ini %s: list of %s%s%s", file ? file : "(win.ini)", app ? "keys in [" : "sections", app ? app : "", app ? "]" : "");
        else
            wl_log("ini %s: [%s] %s = \"%s\" (default \"%s\")", file ? file : "(win.ini)", app, key, ret && size ? ret : "", def ? def : "");
    }
    return n;
}

static DWORD(WINAPI *oGetPrivateProfileStringW)(LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD, LPCWSTR);
static DWORD WINAPI hGetPrivateProfileStringW(LPCWSTR app, LPCWSTR key, LPCWSTR def, LPWSTR ret, DWORD size, LPCWSTR file)
{
    int nested = wl_busy();
    wchar_t rbuf[MAX_PATH * 2];
    if (!nested)
        file = redirW(file, rbuf, MAX_PATH * 2);
    wl_busy_enter();
    DWORD n = oGetPrivateProfileStringW(app, key, def, ret, size, file);
    wl_busy_leave();
    if (!nested && app && key && !wl_is_system_caller(WL_CALLER) && file_event())
    {
        char f[MAX_PATH * 2], a[128], k[128], v[512], d[256];
        wl_log("ini %s: [%s] %s = \"%s\" (default \"%s\")", file ? wl_u8(file, f, sizeof f) : "(win.ini)", wl_u8(app, a, sizeof a), wl_u8(key, k, sizeof k),
               ret && size ? wl_u8(ret, v, sizeof v) : "", def ? wl_u8(def, d, sizeof d) : "");
    }
    return n;
}

static UINT(WINAPI *oGetPrivateProfileIntA)(LPCSTR, LPCSTR, INT, LPCSTR);
static UINT WINAPI hGetPrivateProfileIntA(LPCSTR app, LPCSTR key, INT def, LPCSTR file)
{
    int nested = wl_busy();
    char rbuf[MAX_PATH * 2];
    if (!nested)
        file = redir(file, rbuf, sizeof rbuf);
    wl_busy_enter();
    UINT v = oGetPrivateProfileIntA(app, key, def, file);
    wl_busy_leave();
    char over[64];
    if (!nested && ini_override(file, app, key, over, sizeof over))
    {
        wl_log("ini %s: [%s] %s = %s from windowsloader (the file says %u)", file, app, key, over, v);
        return (UINT)atoi(over);
    }
    if (!nested && !wl_is_system_caller(WL_CALLER) && file_event())
        wl_log("ini %s: [%s] %s = %u (default %d)", file ? file : "(win.ini)", app ? app : "", key ? key : "", v, def);
    return v;
}

static UINT(WINAPI *oGetPrivateProfileIntW)(LPCWSTR, LPCWSTR, INT, LPCWSTR);
static UINT WINAPI hGetPrivateProfileIntW(LPCWSTR app, LPCWSTR key, INT def, LPCWSTR file)
{
    int nested = wl_busy();
    wchar_t rbuf[MAX_PATH * 2];
    if (!nested)
        file = redirW(file, rbuf, MAX_PATH * 2);
    wl_busy_enter();
    UINT v = oGetPrivateProfileIntW(app, key, def, file);
    wl_busy_leave();
    if (!nested && !wl_is_system_caller(WL_CALLER) && file_event())
    {
        char f[MAX_PATH * 2], a[128], k[128];
        wl_log("ini %s: [%s] %s = %u (default %d)", file ? wl_u8(file, f, sizeof f) : "(win.ini)", wl_u8(app, a, sizeof a), wl_u8(key, k, sizeof k), v, def);
    }
    return v;
}

static BOOL(WINAPI *oWritePrivateProfileStringA)(LPCSTR, LPCSTR, LPCSTR, LPCSTR);
static BOOL WINAPI hWritePrivateProfileStringA(LPCSTR app, LPCSTR key, LPCSTR value, LPCSTR file)
{
    int nested = wl_busy();
    char rbuf[MAX_PATH * 2];
    if (!nested)
        file = redir(file, rbuf, sizeof rbuf);
    wl_busy_enter();
    BOOL ok = oWritePrivateProfileStringA(app, key, value, file);
    wl_busy_leave();
    if (!nested && !wl_is_system_caller(WL_CALLER) && file_event())
        wl_log("ini write %s: [%s] %s = \"%s\" -> %s", file ? file : "(win.ini)", app ? app : "", key ? key : "(whole section)", value ? value : "(deleted)",
               ok ? "ok" : "failed");
    return ok;
}

static BOOL(WINAPI *oWritePrivateProfileStringW)(LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR);
static BOOL WINAPI hWritePrivateProfileStringW(LPCWSTR app, LPCWSTR key, LPCWSTR value, LPCWSTR file)
{
    int nested = wl_busy();
    wchar_t rbuf[MAX_PATH * 2];
    if (!nested)
        file = redirW(file, rbuf, MAX_PATH * 2);
    wl_busy_enter();
    BOOL ok = oWritePrivateProfileStringW(app, key, value, file);
    wl_busy_leave();
    if (!nested && !wl_is_system_caller(WL_CALLER) && file_event())
    {
        char f[MAX_PATH * 2], a[128], k[128], v[512];
        wl_log("ini write %s: [%s] %s = \"%s\" -> %s", file ? wl_u8(file, f, sizeof f) : "(win.ini)", wl_u8(app, a, sizeof a),
               key ? wl_u8(key, k, sizeof k) : "(whole section)", value ? wl_u8(value, v, sizeof v) : "(deleted)", ok ? "ok" : "failed");
    }
    return ok;
}

// ------------------------------------------------------------------------------
// The other calls taking a path: only redirected (wl_files_redirect)
// ------------------------------------------------------------------------------

static BOOL(WINAPI *oDeleteFileA)(LPCSTR);
static BOOL WINAPI hDeleteFileA(LPCSTR path)
{
    char rbuf[MAX_PATH * 2];
    return oDeleteFileA(wl_busy() ? path : redir(path, rbuf, sizeof rbuf));
}

static BOOL(WINAPI *oDeleteFileW)(LPCWSTR);
static BOOL WINAPI hDeleteFileW(LPCWSTR path)
{
    wchar_t rbuf[MAX_PATH * 2];
    return oDeleteFileW(wl_busy() ? path : redirW(path, rbuf, MAX_PATH * 2));
}

static BOOL(WINAPI *oRemoveDirectoryA)(LPCSTR);
static BOOL WINAPI hRemoveDirectoryA(LPCSTR path)
{
    char rbuf[MAX_PATH * 2];
    return oRemoveDirectoryA(wl_busy() ? path : redir(path, rbuf, sizeof rbuf));
}

static BOOL(WINAPI *oRemoveDirectoryW)(LPCWSTR);
static BOOL WINAPI hRemoveDirectoryW(LPCWSTR path)
{
    wchar_t rbuf[MAX_PATH * 2];
    return oRemoveDirectoryW(wl_busy() ? path : redirW(path, rbuf, MAX_PATH * 2));
}

static BOOL(WINAPI *oGetFileAttributesExA)(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
static BOOL WINAPI hGetFileAttributesExA(LPCSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID info)
{
    char rbuf[MAX_PATH * 2];
    return oGetFileAttributesExA(wl_busy() ? path : redir(path, rbuf, sizeof rbuf), level, info);
}

static BOOL(WINAPI *oGetFileAttributesExW)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
static BOOL WINAPI hGetFileAttributesExW(LPCWSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID info)
{
    wchar_t rbuf[MAX_PATH * 2];
    return oGetFileAttributesExW(wl_busy() ? path : redirW(path, rbuf, MAX_PATH * 2), level, info);
}

static BOOL(WINAPI *oSetFileAttributesA)(LPCSTR, DWORD);
static BOOL WINAPI hSetFileAttributesA(LPCSTR path, DWORD attr)
{
    char rbuf[MAX_PATH * 2];
    return oSetFileAttributesA(wl_busy() ? path : redir(path, rbuf, sizeof rbuf), attr);
}

static BOOL(WINAPI *oCopyFileA)(LPCSTR, LPCSTR, BOOL);
static BOOL WINAPI hCopyFileA(LPCSTR from, LPCSTR to, BOOL failIfExists)
{
    char rfrom[MAX_PATH * 2], rto[MAX_PATH * 2];
    if (wl_busy())
        return oCopyFileA(from, to, failIfExists);
    return oCopyFileA(redir(from, rfrom, sizeof rfrom), redir(to, rto, sizeof rto), failIfExists);
}

// A file renamed (Mario Kart DX saves its settings as F:\TestMode\testmode_<n>.bin.tmp, then
// renames it, _wrename): both names redirected
static BOOL(WINAPI *oMoveFileExA)(LPCSTR, LPCSTR, DWORD);
static BOOL WINAPI hMoveFileExA(LPCSTR from, LPCSTR to, DWORD flags)
{
    char rfrom[MAX_PATH * 2], rto[MAX_PATH * 2];
    if (wl_busy())
        return oMoveFileExA(from, to, flags);
    return oMoveFileExA(redir(from, rfrom, sizeof rfrom), to ? redir(to, rto, sizeof rto) : NULL, flags);
}

static BOOL(WINAPI *oMoveFileExW)(LPCWSTR, LPCWSTR, DWORD);
static BOOL WINAPI hMoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags)
{
    wchar_t rfrom[MAX_PATH * 2], rto[MAX_PATH * 2];
    if (wl_busy())
        return oMoveFileExW(from, to, flags);
    return oMoveFileExW(redirW(from, rfrom, MAX_PATH * 2), to ? redirW(to, rto, MAX_PATH * 2) : NULL, flags);
}

static BOOL(WINAPI *oMoveFileA)(LPCSTR, LPCSTR);
static BOOL WINAPI hMoveFileA(LPCSTR from, LPCSTR to)
{
    char rfrom[MAX_PATH * 2], rto[MAX_PATH * 2];
    if (wl_busy())
        return oMoveFileA(from, to);
    return oMoveFileA(redir(from, rfrom, sizeof rfrom), redir(to, rto, sizeof rto));
}

static BOOL(WINAPI *oMoveFileW)(LPCWSTR, LPCWSTR);
static BOOL WINAPI hMoveFileW(LPCWSTR from, LPCWSTR to)
{
    wchar_t rfrom[MAX_PATH * 2], rto[MAX_PATH * 2];
    if (wl_busy())
        return oMoveFileW(from, to);
    return oMoveFileW(redirW(from, rfrom, MAX_PATH * 2), redirW(to, rto, MAX_PATH * 2));
}

static BOOL(WINAPI *oSetCurrentDirectoryA)(LPCSTR);
static BOOL WINAPI hSetCurrentDirectoryA(LPCSTR path)
{
    char rbuf[MAX_PATH * 2];
    return oSetCurrentDirectoryA(wl_busy() ? path : redir(path, rbuf, sizeof rbuf));
}

static WlHook g_hooks[] = {
    WL_HOOK(L"kernel32.dll", DeleteFileA),
    WL_HOOK(L"kernel32.dll", DeleteFileW),
    WL_HOOK(L"kernel32.dll", RemoveDirectoryA),
    WL_HOOK(L"kernel32.dll", RemoveDirectoryW),
    WL_HOOK(L"kernel32.dll", GetFileAttributesExA),
    WL_HOOK(L"kernel32.dll", GetFileAttributesExW),
    WL_HOOK(L"kernel32.dll", SetFileAttributesA),
    WL_HOOK(L"kernel32.dll", CopyFileA),
    WL_HOOK(L"kernel32.dll", MoveFileA),
    WL_HOOK(L"kernel32.dll", MoveFileW),
    WL_HOOK(L"kernel32.dll", MoveFileExA),
    WL_HOOK(L"kernel32.dll", MoveFileExW),
    WL_HOOK(L"kernel32.dll", SetCurrentDirectoryA),
    WL_HOOK(L"kernel32.dll", CreateFileA),
    WL_HOOK(L"kernel32.dll", CreateFileW),
    WL_HOOK(L"kernel32.dll", ReadFile),
    WL_HOOK(L"kernel32.dll", WriteFile),
    WL_HOOK(L"kernel32.dll", CloseHandle),
    WL_HOOK(L"kernel32.dll", GetFileAttributesA),
    WL_HOOK(L"kernel32.dll", GetFileAttributesW),
    WL_HOOK(L"kernel32.dll", FindFirstFileA),
    WL_HOOK(L"kernel32.dll", FindFirstFileW),
    WL_HOOK(L"kernel32.dll", CreateDirectoryA),
    WL_HOOK(L"kernel32.dll", CreateDirectoryW),
    WL_HOOK(L"kernel32.dll", GetPrivateProfileStringA),
    WL_HOOK(L"kernel32.dll", GetPrivateProfileStringW),
    WL_HOOK(L"kernel32.dll", GetPrivateProfileIntA),
    WL_HOOK(L"kernel32.dll", GetPrivateProfileIntW),
    WL_HOOK(L"kernel32.dll", WritePrivateProfileStringA),
    WL_HOOK(L"kernel32.dll", WritePrivateProfileStringW),
};

void wl_files_hooks(void)
{
    wl_hooks_add(g_hooks, sizeof g_hooks / sizeof g_hooks[0]);
}
