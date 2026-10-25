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
#include <shlobj.h>
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#include "devices.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "vdev.h"
#include "vhid.h"
#include "wlshared.h"

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
// The write folder (--write-dir, [Paths] WRITE_DIR, linuxloader's): the game's folders are only
// read. What the game writes goes to WRITE_DIR\<game>\<drive>\<path> (\\server\share\... to
// WRITE_DIR\<game>\UNC\server\share\...), a file opened to be modified copied there first; what it
// reads comes from there when it is there, else from its place; a folder's listing joins both, the
// write folder's first. The system's folders have folders of their own there: the user's AppData in
// WRITE_DIR\<game>\appdata, the temporary folder in temp, ProgramData in programdata, Windows' in
// windows, so a disk under a write filter (UWF) but for the write folder keeps all the game writes.
// The write folder itself and devices are left alone. A file removed that is only in its first
// place stays there (as in linuxloader). Applied after the redirections above, on the full path:
// it takes a game writing above its program's folder (Crazy Ride's Saved) or on a drive redirected
// into it (D:, E:, F:...) as well. Without a write folder, nothing changes
// ------------------------------------------------------------------------------

#define WL_PATH (MAX_PATH * 2)

static wchar_t g_writeGame[MAX_PATH]; // WRITE_DIR\<game>, L"" when not in use

// The system's folders, each in a folder of its own in WRITE_DIR\<game>: appdata, temp,
// programdata, windows, so a disk under a write filter (UWF) but for the write folder keeps them;
// the most precise one decides (the temporary folder is in AppData\Local). The write folder itself
// is left alone (sub NULL)
static struct
{
    wchar_t folder[MAX_PATH];
    const wchar_t *sub;
} g_rules[10];
static int g_ruleCount;

static void add_rule(const wchar_t *path, const wchar_t *sub)
{
    wchar_t full[MAX_PATH];
    if (!path || !path[0] || g_ruleCount >= (int)(sizeof g_rules / sizeof g_rules[0]) || !GetFullPathNameW(path, MAX_PATH, full, NULL))
        return;
    size_t n = wcslen(full);
    while (n > 3 && full[n - 1] == L'\\')
        full[--n] = 0;
    wcscpy_s(g_rules[g_ruleCount].folder, MAX_PATH, full);
    g_rules[g_ruleCount++].sub = sub;
}

// path inside folder (no case): what follows it, else NULL
static const wchar_t *inside(const wchar_t *path, const wchar_t *folder)
{
    size_t n = wcslen(folder);
    if (!n || _wcsnicmp(path, folder, n))
        return NULL;
    if (path[n] == L'\\')
        return path + n + 1;
    return path[n] ? NULL : path + n;
}

void wl_files_write_dir(void)
{
    wchar_t root[MAX_PATH];
    if (!GetEnvironmentVariableW(WL_ENV_WRITE_DIR, root, MAX_PATH) || !root[0])
        return;
    // The game's short name as a folder's, the program's when the game is not known
    char name[64];
    snprintf(name, sizeof name, "%s", wl_game_name());
    if (!strcmp(name, "unknown"))
    {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        const wchar_t *base = wcsrchr(exe, L'\\');
        WideCharToMultiByte(CP_ACP, 0, base ? base + 1 : exe, -1, name, sizeof name, NULL, NULL);
        char *dot = strrchr(name, '.');
        if (dot)
            *dot = 0;
    }
    for (char *c = name; *c; c++)
        if (!isalnum((unsigned char)*c) && *c != '-' && *c != '_' && *c != '.')
            *c = '_';
    swprintf(g_writeGame, MAX_PATH, L"%s\\%S", root, name);
    wl_busy_enter();
    SHCreateDirectoryExW(NULL, g_writeGame, NULL);
    DWORD a = GetFileAttributesW(g_writeGame);
    wl_busy_leave();
    char u[MAX_PATH * 2];
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY))
    {
        wl_log("files: the write folder %s cannot be made: the game writes in place", wl_u8(g_writeGame, u, sizeof u));
        g_writeGame[0] = 0;
        return;
    }
    // The system's folders in folders of their own: AppData (the caches of Windows, of the graphics
    // driver, of the libraries, some games' saves) in appdata (Roaming, Local, LocalLow), the
    // temporary folder in temp, ProgramData in programdata, Windows' in windows. The rest, the
    // user's profile too (a game on the desktop), under its drive's letter
    wchar_t v[MAX_PATH];
    add_rule(root, NULL);
    if (GetWindowsDirectoryW(v, MAX_PATH))
        add_rule(v, L"windows");
    if (GetEnvironmentVariableW(L"ProgramData", v, MAX_PATH))
        add_rule(v, L"programdata");
    if (GetEnvironmentVariableW(L"USERPROFILE", v, MAX_PATH) && !wcscat_s(v, MAX_PATH, L"\\AppData"))
        add_rule(v, L"appdata");
    if (GetEnvironmentVariableW(L"APPDATA", v, MAX_PATH))
        add_rule(v, L"appdata\\Roaming");
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", v, MAX_PATH))
        add_rule(v, L"appdata\\Local");
    if (GetTempPathW(MAX_PATH, v))
        add_rule(v, L"temp");
    wl_log("files: the write folder %s: the game's writes go there (AppData in appdata, the temporary folder in temp, ProgramData in "
           "programdata, Windows' in windows, the rest under its drive's letter), its folders are only read",
           wl_u8(g_writeGame, u, sizeof u));
}

// The write folder's path for path (redirected already), in over, the full path in full: 0 when
// the path is left alone
static int overlay_path(const wchar_t *path, wchar_t *full, wchar_t *over)
{
    if (!g_writeGame[0] || !path || !path[0] || (path[0] == L'\\' && path[1] == L'\\' && (path[2] == L'.' || path[2] == L'?')))
        return 0;
    if (!GetFullPathNameW(path, WL_PATH, full, NULL) || (full[0] == L'\\' && full[1] == L'\\' && (full[2] == L'.' || full[2] == L'?')))
        return 0; // a device too ("NUL", "COM1": \\.\NUL)
    int rule = -1;
    size_t longest = 0;
    for (int i = 0; i < g_ruleCount; i++)
        if (inside(full, g_rules[i].folder) && wcslen(g_rules[i].folder) > longest)
            rule = i, longest = wcslen(g_rules[i].folder);
    if (rule >= 0 && !g_rules[rule].sub)
        return 0;
    int n;
    if (rule >= 0)
    {
        const wchar_t *rest = inside(full, g_rules[rule].folder);
        n = swprintf(over, WL_PATH, L"%s\\%s%s%s", g_writeGame, g_rules[rule].sub, *rest ? L"\\" : L"", rest);
    }
    else if (iswalpha(full[0]) && full[1] == L':' && full[2] == L'\\')
        n = swprintf(over, WL_PATH, L"%s\\%c%s%s", g_writeGame, towupper(full[0]), full[3] ? L"\\" : L"", full + 3);
    else if (full[0] == L'\\' && full[1] == L'\\')
        n = swprintf(over, WL_PATH, L"%s\\UNC\\%s", g_writeGame, full + 2);
    else
        return 0;
    if (n <= 0)
        return 0;
    while (n > 3 && over[n - 1] == L'\\')
        over[--n] = 0;
    // Past MAX_PATH (the write folder's name, then the game's whole path), the \\?\ form, which the
    // file calls take
    if (n >= MAX_PATH - 20)
    {
        wchar_t lng[WL_PATH];
        if (over[1] == L':' && swprintf(lng, WL_PATH, L"\\\\?\\%s", over) > 0)
            wcscpy_s(over, WL_PATH, lng);
        else if (over[0] == L'\\' && over[1] == L'\\' && over[2] != L'?' && swprintf(lng, WL_PATH, L"\\\\?\\UNC\\%s", over + 2) > 0)
            wcscpy_s(over, WL_PATH, lng);
    }
    return 1;
}

// A folder and those above it made, one by one (the \\?\ form too, which SHCreateDirectoryEx does
// not take): 1 when it is there
static int make_dirs(const wchar_t *dir)
{
    wchar_t p[WL_PATH];
    wcscpy_s(p, WL_PATH, dir);
    // Past the root, which is not made: X:\, \\?\X:\, \\server\share\, \\?\UNC\server\share\ .
    wchar_t *c = p;
    int shares = 0;
    if (!wcsncmp(p, L"\\\\?\\UNC\\", 8))
        c = p + 8, shares = 2;
    else if (!wcsncmp(p, L"\\\\?\\", 4))
        c = p + 7;
    else if (p[0] == L'\\' && p[1] == L'\\')
        c = p + 2, shares = 2;
    else if (p[0] && p[1] == L':')
        c = p + 3;
    while (shares && *c)
        if (*c++ == L'\\')
            shares--;
    wl_busy_enter();
    for (; *c; c++)
        if (*c == L'\\')
        {
            *c = 0;
            CreateDirectoryW(p, NULL);
            *c = L'\\';
        }
    CreateDirectoryW(p, NULL);
    DWORD a = GetFileAttributesW(p);
    wl_busy_leave();
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static DWORD attributes(const wchar_t *path)
{
    wl_busy_enter();
    DWORD a = GetFileAttributesW(path);
    wl_busy_leave();
    return a;
}

static int is_dir(DWORD a)
{
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// The folders above path made: 1 when they are there
static int make_parents(const wchar_t *path)
{
    wchar_t dir[WL_PATH];
    wcscpy_s(dir, WL_PATH, path);
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (!slash)
        return 0;
    *slash = 0;
    return is_dir(attributes(dir)) || make_dirs(dir);
}

// For reading: the write folder's file when it is there, else path
static const wchar_t *ov_read(const wchar_t *path, wchar_t *buf)
{
    wchar_t full[WL_PATH];
    if (!overlay_path(path, full, buf))
        return path;
    return attributes(buf) != INVALID_FILE_ATTRIBUTES ? buf : path;
}

const wchar_t *wl_files_read_path(const wchar_t *path, wchar_t *buf, size_t size)
{
    wchar_t over[WL_PATH];
    if (!g_writeGame[0] || !path || (!wcschr(path, L'\\') && !wcschr(path, L'/')))
        return path;
    const wchar_t *r = ov_read(path, over);
    if (r == path || wcscpy_s(buf, size, r))
        return path;
    return buf;
}

// For writing: the write folder's path, its folders made; keep: the file is to be modified, its
// first place's copied there first. Its folders cannot be made: written in place, rather than the
// game stopped
static const wchar_t *ov_write(const wchar_t *path, int keep, wchar_t *buf)
{
    wchar_t full[WL_PATH];
    if (!overlay_path(path, full, buf))
        return path;
    if (!make_parents(buf))
    {
        static volatile LONG logged;
        if (InterlockedIncrement(&logged) <= 10)
        {
            char u[WL_PATH * 2];
            wl_log("files: the write folder's %s cannot be made: written in place", wl_u8(buf, u, sizeof u));
        }
        return path;
    }
    if (keep && attributes(buf) == INVALID_FILE_ATTRIBUTES)
    {
        DWORD a = attributes(full);
        if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY))
        {
            wl_busy_enter();
            BOOL ok = CopyFileW(full, buf, TRUE);
            wl_busy_leave();
            static volatile LONG logged;
            if (InterlockedIncrement(&logged) <= 50)
            {
                char u[WL_PATH * 2];
                wl_log("files: %s copied to the write folder to be modified%s", wl_u8(full, u, sizeof u), ok ? "" : " (failed)");
            }
        }
    }
    return buf;
}

// CreateFile's: written (write access, a creation) goes to the write folder, read comes from it when
// the file is there. A folder's handle is taken where the folder is
static const wchar_t *ov_create(const wchar_t *path, DWORD access, DWORD *disp, wchar_t *buf)
{
    wchar_t full[WL_PATH];
    if (!overlay_path(path, full, buf))
        return path;
    DWORD there = attributes(buf), first = attributes(full);
    if (is_dir(there) || is_dir(first))
        return there != INVALID_FILE_ATTRIBUTES ? buf : path;
    int writes = (access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | DELETE)) ||
                 *disp == CREATE_NEW || *disp == CREATE_ALWAYS || *disp == TRUNCATE_EXISTING ||
                 (*disp == OPEN_ALWAYS && there == INVALID_FILE_ATTRIBUTES && first == INVALID_FILE_ATTRIBUTES);
    if (!writes)
        return there != INVALID_FILE_ATTRIBUTES ? buf : path;
    if (*disp == CREATE_NEW && there == INVALID_FILE_ATTRIBUTES && first != INVALID_FILE_ATTRIBUTES)
        return path; // it exists: CREATE_NEW fails there, as it should
    if (*disp == TRUNCATE_EXISTING && there == INVALID_FILE_ATTRIBUTES && first != INVALID_FILE_ATTRIBUTES)
        *disp = CREATE_ALWAYS;
    return ov_write(path, *disp == OPEN_EXISTING || *disp == OPEN_ALWAYS, buf);
}

// The same for the ANSI calls: the path in the ANSI code page, back and forth
static int to_wide(const char *path, wchar_t *w)
{
    return path && MultiByteToWideChar(CP_ACP, 0, path, -1, w, WL_PATH) > 0;
}

static const char *to_ansi(const wchar_t *mapped, const wchar_t *was, const char *path, char *buf, size_t size)
{
    if (mapped == was || !WideCharToMultiByte(CP_ACP, 0, mapped, -1, buf, (int)size, NULL, NULL))
        return path;
    return buf;
}

static const char *ov_readA(const char *path, char *buf, size_t size)
{
    wchar_t w[WL_PATH], o[WL_PATH];
    if (!g_writeGame[0] || !to_wide(path, w))
        return path;
    return to_ansi(ov_read(w, o), w, path, buf, size);
}

static const char *ov_writeA(const char *path, int keep, char *buf, size_t size)
{
    wchar_t w[WL_PATH], o[WL_PATH];
    if (!g_writeGame[0] || !to_wide(path, w))
        return path;
    return to_ansi(ov_write(w, keep, o), w, path, buf, size);
}

static const char *ov_createA(const char *path, DWORD access, DWORD *disp, char *buf, size_t size)
{
    wchar_t w[WL_PATH], o[WL_PATH];
    if (!g_writeGame[0] || !to_wide(path, w))
        return path;
    return to_ansi(ov_create(w, access, disp, o), w, path, buf, size);
}

// An .ini file named alone ("game.ini") is the Windows folder's to GetPrivateProfile*: left alone
static int ini_has_folder(const wchar_t *file)
{
    return file && (wcschr(file, L'\\') || wcschr(file, L'/') || (file[0] && file[1] == L':'));
}

static int ini_has_folderA(const char *file)
{
    return file && (strchr(file, '\\') || strchr(file, '/') || (file[0] && file[1] == ':'));
}

// A folder's listing joining both places (FindFirstFile*): its entries, the write folder's first.
// Its handle is the list's address; FindNextFile and FindClose know it
struct MergedFind
{
    int count, next;
    WIN32_FIND_DATAW items[1];
};

static SRWLOCK g_findLock = SRWLOCK_INIT;
static MergedFind *g_finds[64];

static int find_is_ours(HANDLE h)
{
    int ours = 0;
    AcquireSRWLockShared(&g_findLock);
    for (int i = 0; i < 64 && !ours; i++)
        ours = g_finds[i] && (HANDLE)g_finds[i] == h;
    ReleaseSRWLockShared(&g_findLock);
    return ours;
}

static void find_add(WIN32_FIND_DATAW **items, int *count, int *size, const WIN32_FIND_DATAW *d)
{
    for (int i = 0; i < *count; i++)
        if (!_wcsicmp((*items)[i].cFileName, d->cFileName))
            return;
    if (*count == *size)
    {
        int grown = *size ? *size * 2 : 32;
        WIN32_FIND_DATAW *more = (WIN32_FIND_DATAW *)realloc(*items, grown * sizeof(WIN32_FIND_DATAW));
        if (!more)
            return;
        *items = more;
        *size = grown;
    }
    (*items)[(*count)++] = *d;
}

static void find_list(const wchar_t *pattern, WIN32_FIND_DATAW **items, int *count, int *size)
{
    WIN32_FIND_DATAW d;
    wl_busy_enter();
    HANDLE h = FindFirstFileW(pattern, &d);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
            find_add(items, count, size, &d);
        while (FindNextFileW(h, &d));
        FindClose(h);
    }
    wl_busy_leave();
}

// NULL when the pattern's folder has nothing in the write folder (the call goes on as it is), else
// the merged listing's handle (INVALID_HANDLE_VALUE, ERROR_FILE_NOT_FOUND, when both are empty)
static HANDLE merged_find(const wchar_t *pattern, WIN32_FIND_DATAW *first)
{
    wchar_t full[WL_PATH], over[WL_PATH], dir[WL_PATH];
    if (!overlay_path(pattern, full, over))
        return NULL;
    wcscpy_s(dir, WL_PATH, over);
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (slash)
        *slash = 0;
    if (!is_dir(attributes(dir)))
        return NULL;
    WIN32_FIND_DATAW *items = NULL;
    int count = 0, size = 0;
    find_list(over, &items, &count, &size);
    find_list(full, &items, &count, &size);
    if (!count)
    {
        free(items);
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    MergedFind *m = (MergedFind *)malloc(sizeof(MergedFind) + (count - 1) * sizeof(WIN32_FIND_DATAW));
    int slot = -1;
    AcquireSRWLockExclusive(&g_findLock);
    for (int i = 0; i < 64 && slot < 0 && m; i++)
        if (!g_finds[i])
            slot = i;
    if (slot >= 0)
        g_finds[slot] = m;
    ReleaseSRWLockExclusive(&g_findLock);
    if (slot < 0) // no room: the first place's listing alone
    {
        free(m);
        free(items);
        return NULL;
    }
    memcpy(m->items, items, count * sizeof(WIN32_FIND_DATAW));
    free(items);
    m->count = count;
    m->next = 1;
    *first = m->items[0];
    return (HANDLE)m;
}

static void find_data_ansi(const WIN32_FIND_DATAW *w, WIN32_FIND_DATAA *a)
{
    memcpy(a, w, offsetof(WIN32_FIND_DATAW, cFileName));
    WideCharToMultiByte(CP_ACP, 0, w->cFileName, -1, a->cFileName, MAX_PATH, NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, w->cAlternateFileName, -1, a->cAlternateFileName, 14, NULL, NULL);
}

// The next entry of a merged listing: 0 at its end (ERROR_NO_MORE_FILES)
static int merged_next(HANDLE h, WIN32_FIND_DATAW *out)
{
    MergedFind *m = (MergedFind *)h;
    if (m->next >= m->count)
    {
        SetLastError(ERROR_NO_MORE_FILES);
        return 0;
    }
    *out = m->items[m->next++];
    return 1;
}

static void merged_close(HANDLE h)
{
    AcquireSRWLockExclusive(&g_findLock);
    for (int i = 0; i < 64; i++)
        if ((HANDLE)g_finds[i] == h)
            g_finds[i] = NULL;
    ReleaseSRWLockExclusive(&g_findLock);
    free(h);
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
    char rbuf[MAX_PATH * 2], obuf[WL_PATH * 2];
    if (!nested)
    {
        name = redir(name, rbuf, sizeof rbuf);
        name = ov_createA(name, access, &disp, obuf, sizeof obuf);
    }
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
    wchar_t rbuf[MAX_PATH * 2], obuf[WL_PATH];
    if (!nested)
    {
        name = redirW(name, rbuf, MAX_PATH * 2);
        if (g_writeGame[0])
            name = ov_create(name, access, &disp, obuf);
    }
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
    if (!nested && wl_vhid_is_handle(h))
        return wl_vhid_write(h, buf, size, written, ov);
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
    char rbuf[MAX_PATH * 2], obuf[WL_PATH * 2];
    if (!nested)
        name = ov_readA(redir(name, rbuf, sizeof rbuf), obuf, sizeof obuf);
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
    wchar_t rbuf[MAX_PATH * 2], obuf[WL_PATH];
    if (!nested)
        name = ov_read(redirW(name, rbuf, MAX_PATH * 2), obuf);
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
    // The write folder's and the first place's entries together
    wchar_t w[WL_PATH];
    WIN32_FIND_DATAW wd;
    HANDLE merged = !nested && g_writeGame[0] && to_wide(pattern, w) ? merged_find(w, &wd) : NULL;
    if (merged)
    {
        if (merged != INVALID_HANDLE_VALUE)
            find_data_ansi(&wd, data);
        return merged;
    }
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
    HANDLE merged = !nested && g_writeGame[0] && data ? merged_find(pattern, data) : NULL;
    if (merged)
        return merged;
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

// The C runtime's and Unreal Engine's: only redirected, and merged with the write folder
static HANDLE(WINAPI *oFindFirstFileExW)(LPCWSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD);
static HANDLE WINAPI hFindFirstFileExW(LPCWSTR pattern, FINDEX_INFO_LEVELS level, LPVOID data, FINDEX_SEARCH_OPS op, LPVOID filter, DWORD flags)
{
    if (wl_busy())
        return oFindFirstFileExW(pattern, level, data, op, filter, flags);
    wchar_t rbuf[MAX_PATH * 2];
    pattern = redirW(pattern, rbuf, MAX_PATH * 2);
    HANDLE merged = g_writeGame[0] && data && !filter ? merged_find(pattern, (WIN32_FIND_DATAW *)data) : NULL;
    if (merged)
        return merged;
    return oFindFirstFileExW(pattern, level, data, op, filter, flags);
}

static BOOL(WINAPI *oFindNextFileA)(HANDLE, LPWIN32_FIND_DATAA);
static BOOL WINAPI hFindNextFileA(HANDLE h, LPWIN32_FIND_DATAA data)
{
    if (!g_writeGame[0] || !find_is_ours(h))
        return oFindNextFileA(h, data);
    WIN32_FIND_DATAW wd;
    if (!merged_next(h, &wd))
        return FALSE;
    find_data_ansi(&wd, data);
    return TRUE;
}

static BOOL(WINAPI *oFindNextFileW)(HANDLE, LPWIN32_FIND_DATAW);
static BOOL WINAPI hFindNextFileW(HANDLE h, LPWIN32_FIND_DATAW data)
{
    if (!g_writeGame[0] || !find_is_ours(h))
        return oFindNextFileW(h, data);
    return merged_next(h, data);
}

static BOOL(WINAPI *oFindClose)(HANDLE);
static BOOL WINAPI hFindClose(HANDLE h)
{
    if (!g_writeGame[0] || !find_is_ours(h))
        return oFindClose(h);
    merged_close(h);
    return TRUE;
}

// A folder made: in the write folder, unless it is already somewhere
static BOOL make_folder_over(const wchar_t *path, BOOL *done)
{
    wchar_t full[WL_PATH], over[WL_PATH];
    if (!overlay_path(path, full, over))
        return FALSE;
    if (attributes(over) != INVALID_FILE_ATTRIBUTES || attributes(full) != INVALID_FILE_ATTRIBUTES)
    {
        SetLastError(ERROR_ALREADY_EXISTS);
        *done = FALSE;
        return TRUE;
    }
    *done = make_dirs(over);
    SetLastError(*done ? 0 : ERROR_PATH_NOT_FOUND);
    return TRUE;
}

static BOOL(WINAPI *oCreateDirectoryA)(LPCSTR, LPSECURITY_ATTRIBUTES);
static BOOL WINAPI hCreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES sa)
{
    int nested = wl_busy();
    char rbuf[MAX_PATH * 2];
    if (!nested)
        path = redir(path, rbuf, sizeof rbuf);
    wchar_t w[WL_PATH];
    BOOL ok;
    if (nested || !g_writeGame[0] || !to_wide(path, w) || !make_folder_over(w, &ok))
    {
        wl_busy_enter();
        ok = oCreateDirectoryA(path, sa);
        wl_busy_leave();
    }
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
    BOOL ok;
    if (nested || !g_writeGame[0] || !make_folder_over(path, &ok))
    {
        wl_busy_enter();
        ok = oCreateDirectoryW(path, sa);
        wl_busy_leave();
    }
    DWORD err = GetLastError();
    if (!nested && path && !wl_is_system_caller(WL_CALLER) && file_event())
    {
        char p[MAX_PATH * 2];
        wl_log("CreateDirectory \"%s\" -> %s", wl_u8(path, p, sizeof p), ok ? "ok" : err == ERROR_ALREADY_EXISTS ? "exists" : "failed");
    }
    SetLastError(err);
    return ok;
}

// The shell's, a path and all its folders: shell32 calls CreateDirectoryW itself, as a system
// caller, so the path is redirected here (an ALLS's amdaemon makes Y:\SDDS so)
static int(WINAPI *oSHCreateDirectoryExW)(HWND, LPCWSTR, const SECURITY_ATTRIBUTES *);
static int WINAPI hSHCreateDirectoryExW(HWND wnd, LPCWSTR path, const SECURITY_ATTRIBUTES *sa)
{
    int nested = wl_busy();
    wchar_t rbuf[MAX_PATH * 2], full[WL_PATH], over[WL_PATH];
    if (!nested)
        path = redirW(path, rbuf, MAX_PATH * 2);
    if (!nested && overlay_path(path, full, over))
    {
        if (attributes(over) != INVALID_FILE_ATTRIBUTES || attributes(full) != INVALID_FILE_ATTRIBUTES)
            return ERROR_ALREADY_EXISTS;
        return make_dirs(over) ? ERROR_SUCCESS : ERROR_PATH_NOT_FOUND;
    }
    int r = oSHCreateDirectoryExW(wnd, path, sa);
    if (!nested && path && !wl_is_system_caller(WL_CALLER) && file_event())
    {
        char p[MAX_PATH * 2];
        wl_log("SHCreateDirectoryEx \"%s\" -> %d", wl_u8(path, p, sizeof p), r);
    }
    return r;
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
static DWORD(WINAPI *oGetPrivateProfileStringW)(LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD, LPCWSTR);
static UINT(WINAPI *oGetPrivateProfileIntW)(LPCWSTR, LPCWSTR, INT, LPCWSTR);
static BOOL(WINAPI *oWritePrivateProfileStringW)(LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR);

// The write folder's .ini of an ANSI call: its path is long (\\?\), which only the profile calls'
// W versions take; NULL when the file is not there
static const wchar_t *ini_overA(const char *file, int write, wchar_t *w, wchar_t *over)
{
    if (!g_writeGame[0] || !ini_has_folderA(file) || !to_wide(file, w))
        return NULL;
    const wchar_t *mapped = write ? ov_write(w, 1, over) : ov_read(w, over);
    return mapped == w ? NULL : mapped;
}

// An ANSI text as the W calls want it, NULL kept
static const wchar_t *wide_of(const char *s, wchar_t *buf, int size)
{
    if (!s)
        return NULL;
    if (!MultiByteToWideChar(CP_ACP, 0, s, -1, buf, size))
        buf[0] = 0;
    return buf;
}

static DWORD WINAPI hGetPrivateProfileStringA(LPCSTR app, LPCSTR key, LPCSTR def, LPSTR ret, DWORD size, LPCSTR file)
{
    int nested = wl_busy();
    char rbuf[MAX_PATH * 2];
    if (!nested)
        file = redir(file, rbuf, sizeof rbuf);
    wchar_t w[WL_PATH], wover[WL_PATH];
    const wchar_t *mapped = nested ? NULL : ini_overA(file, 0, w, wover);
    DWORD n;
    wl_busy_enter();
    if (mapped && ret && size)
    {
        wchar_t a[256], k[256], d[1024];
        wchar_t *r = (wchar_t *)malloc(size * sizeof(wchar_t));
        n = r ? oGetPrivateProfileStringW(wide_of(app, a, 256), wide_of(key, k, 256), wide_of(def, d, 1024), r, size, mapped) : 0;
        if (!r || !WideCharToMultiByte(CP_ACP, 0, r, (int)n + 1, ret, (int)size, NULL, NULL))
            ret[0] = 0;
        free(r);
    }
    else
        n = oGetPrivateProfileStringA(app, key, def, ret, size, file);
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

static DWORD WINAPI hGetPrivateProfileStringW(LPCWSTR app, LPCWSTR key, LPCWSTR def, LPWSTR ret, DWORD size, LPCWSTR file)
{
    int nested = wl_busy();
    wchar_t rbuf[MAX_PATH * 2], obuf[WL_PATH];
    if (!nested)
        file = redirW(file, rbuf, MAX_PATH * 2);
    if (!nested && ini_has_folder(file))
        file = ov_read(file, obuf);
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
    wchar_t w[WL_PATH], wover[WL_PATH], a[256], k[256];
    const wchar_t *mapped = nested ? NULL : ini_overA(file, 0, w, wover);
    wl_busy_enter();
    UINT v = mapped ? oGetPrivateProfileIntW(wide_of(app, a, 256), wide_of(key, k, 256), def, mapped) : oGetPrivateProfileIntA(app, key, def, file);
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

static UINT WINAPI hGetPrivateProfileIntW(LPCWSTR app, LPCWSTR key, INT def, LPCWSTR file)
{
    int nested = wl_busy();
    wchar_t rbuf[MAX_PATH * 2], obuf[WL_PATH];
    if (!nested)
        file = redirW(file, rbuf, MAX_PATH * 2);
    if (!nested && ini_has_folder(file))
        file = ov_read(file, obuf);
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
    wchar_t w[WL_PATH], wover[WL_PATH], a[256], k[256], v[2048];
    const wchar_t *mapped = nested ? NULL : ini_overA(file, 1, w, wover);
    wl_busy_enter();
    BOOL ok = mapped ? oWritePrivateProfileStringW(wide_of(app, a, 256), wide_of(key, k, 256), wide_of(value, v, 2048), mapped)
                     : oWritePrivateProfileStringA(app, key, value, file);
    wl_busy_leave();
    if (!nested && !wl_is_system_caller(WL_CALLER) && file_event())
        wl_log("ini write %s: [%s] %s = \"%s\" -> %s", file ? file : "(win.ini)", app ? app : "", key ? key : "(whole section)", value ? value : "(deleted)",
               ok ? "ok" : "failed");
    return ok;
}

static BOOL WINAPI hWritePrivateProfileStringW(LPCWSTR app, LPCWSTR key, LPCWSTR value, LPCWSTR file)
{
    int nested = wl_busy();
    wchar_t rbuf[MAX_PATH * 2], obuf[WL_PATH];
    if (!nested)
        file = redirW(file, rbuf, MAX_PATH * 2);
    if (!nested && ini_has_folder(file))
        file = ov_write(file, 1, obuf);
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
// The other calls taking a path: redirected (wl_files_redirect), and through the write folder
// ------------------------------------------------------------------------------

// A file or a folder removed: the write folder's; one only in its first place stays there, the
// call told done (as in linuxloader). 0 when the path is not the write folder's business
static int remove_over(const wchar_t *path, int folder, BOOL *done)
{
    wchar_t full[WL_PATH], over[WL_PATH];
    if (!g_writeGame[0] || !overlay_path(path, full, over))
        return 0;
    if (attributes(over) != INVALID_FILE_ATTRIBUTES)
    {
        wl_busy_enter();
        *done = folder ? RemoveDirectoryW(over) : DeleteFileW(over);
        wl_busy_leave();
    }
    else if (attributes(full) != INVALID_FILE_ATTRIBUTES)
        *done = TRUE;
    else
    {
        SetLastError(folder ? ERROR_PATH_NOT_FOUND : ERROR_FILE_NOT_FOUND);
        *done = FALSE;
    }
    return 1;
}

// A file renamed: from where it is to the write folder; one only in its first place is copied there
// (Mario Kart DX saves its settings as F:\TestMode\testmode_<n>.bin.tmp, then renames it)
static int move_over(const wchar_t *from, const wchar_t *to, DWORD flags, BOOL *done)
{
    wchar_t full[WL_PATH], over[WL_PATH], toBuf[WL_PATH];
    if (!g_writeGame[0] || !to)
        return 0;
    int fromOver = overlay_path(from, full, over);
    const wchar_t *dest = ov_write(to, 0, toBuf);
    if (!fromOver && dest == to)
        return 0;
    const wchar_t *source = !fromOver ? from : attributes(over) != INVALID_FILE_ATTRIBUTES ? over : full;
    wl_busy_enter();
    if (fromOver && source == full && !is_dir(attributes(full)))
        *done = CopyFileW(source, dest, !(flags & MOVEFILE_REPLACE_EXISTING));
    else
        *done = MoveFileExW(source, dest, flags | MOVEFILE_COPY_ALLOWED);
    wl_busy_leave();
    return 1;
}

static BOOL(WINAPI *oDeleteFileA)(LPCSTR);
static BOOL WINAPI hDeleteFileA(LPCSTR path)
{
    if (wl_busy())
        return oDeleteFileA(path);
    char rbuf[MAX_PATH * 2];
    path = redir(path, rbuf, sizeof rbuf);
    wchar_t w[WL_PATH];
    BOOL done;
    if (to_wide(path, w) && remove_over(w, 0, &done))
        return done;
    return oDeleteFileA(path);
}

static BOOL(WINAPI *oDeleteFileW)(LPCWSTR);
static BOOL WINAPI hDeleteFileW(LPCWSTR path)
{
    if (wl_busy())
        return oDeleteFileW(path);
    wchar_t rbuf[MAX_PATH * 2];
    path = redirW(path, rbuf, MAX_PATH * 2);
    BOOL done;
    if (remove_over(path, 0, &done))
        return done;
    return oDeleteFileW(path);
}

static BOOL(WINAPI *oRemoveDirectoryA)(LPCSTR);
static BOOL WINAPI hRemoveDirectoryA(LPCSTR path)
{
    if (wl_busy())
        return oRemoveDirectoryA(path);
    char rbuf[MAX_PATH * 2];
    path = redir(path, rbuf, sizeof rbuf);
    wchar_t w[WL_PATH];
    BOOL done;
    if (to_wide(path, w) && remove_over(w, 1, &done))
        return done;
    return oRemoveDirectoryA(path);
}

static BOOL(WINAPI *oRemoveDirectoryW)(LPCWSTR);
static BOOL WINAPI hRemoveDirectoryW(LPCWSTR path)
{
    if (wl_busy())
        return oRemoveDirectoryW(path);
    wchar_t rbuf[MAX_PATH * 2];
    path = redirW(path, rbuf, MAX_PATH * 2);
    BOOL done;
    if (remove_over(path, 1, &done))
        return done;
    return oRemoveDirectoryW(path);
}

static BOOL(WINAPI *oGetFileAttributesExA)(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
static BOOL WINAPI hGetFileAttributesExA(LPCSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID info)
{
    if (wl_busy())
        return oGetFileAttributesExA(path, level, info);
    char rbuf[MAX_PATH * 2], obuf[WL_PATH * 2];
    return oGetFileAttributesExA(ov_readA(redir(path, rbuf, sizeof rbuf), obuf, sizeof obuf), level, info);
}

static BOOL(WINAPI *oGetFileAttributesExW)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
static BOOL WINAPI hGetFileAttributesExW(LPCWSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID info)
{
    if (wl_busy())
        return oGetFileAttributesExW(path, level, info);
    wchar_t rbuf[MAX_PATH * 2], obuf[WL_PATH];
    return oGetFileAttributesExW(ov_read(redirW(path, rbuf, MAX_PATH * 2), obuf), level, info);
}

static BOOL(WINAPI *oSetFileAttributesA)(LPCSTR, DWORD);
static BOOL WINAPI hSetFileAttributesA(LPCSTR path, DWORD attr)
{
    if (wl_busy())
        return oSetFileAttributesA(path, attr);
    char rbuf[MAX_PATH * 2], obuf[WL_PATH * 2];
    return oSetFileAttributesA(ov_writeA(redir(path, rbuf, sizeof rbuf), 1, obuf, sizeof obuf), attr);
}

static BOOL(WINAPI *oSetFileAttributesW)(LPCWSTR, DWORD);
static BOOL WINAPI hSetFileAttributesW(LPCWSTR path, DWORD attr)
{
    if (wl_busy())
        return oSetFileAttributesW(path, attr);
    wchar_t rbuf[MAX_PATH * 2], obuf[WL_PATH];
    return oSetFileAttributesW(ov_write(redirW(path, rbuf, MAX_PATH * 2), 1, obuf), attr);
}

static BOOL(WINAPI *oCopyFileA)(LPCSTR, LPCSTR, BOOL);
static BOOL WINAPI hCopyFileA(LPCSTR from, LPCSTR to, BOOL failIfExists)
{
    char rfrom[MAX_PATH * 2], rto[MAX_PATH * 2], ofrom[WL_PATH * 2], oto[WL_PATH * 2];
    if (wl_busy())
        return oCopyFileA(from, to, failIfExists);
    return oCopyFileA(ov_readA(redir(from, rfrom, sizeof rfrom), ofrom, sizeof ofrom), ov_writeA(redir(to, rto, sizeof rto), 0, oto, sizeof oto),
                      failIfExists);
}

static BOOL(WINAPI *oCopyFileW)(LPCWSTR, LPCWSTR, BOOL);
static BOOL WINAPI hCopyFileW(LPCWSTR from, LPCWSTR to, BOOL failIfExists)
{
    wchar_t rfrom[MAX_PATH * 2], rto[MAX_PATH * 2], ofrom[WL_PATH], oto[WL_PATH];
    if (wl_busy())
        return oCopyFileW(from, to, failIfExists);
    return oCopyFileW(ov_read(redirW(from, rfrom, MAX_PATH * 2), ofrom), ov_write(redirW(to, rto, MAX_PATH * 2), 0, oto), failIfExists);
}

// A file renamed: both names redirected, then through the write folder
static BOOL(WINAPI *oMoveFileExA)(LPCSTR, LPCSTR, DWORD);
static BOOL WINAPI hMoveFileExA(LPCSTR from, LPCSTR to, DWORD flags)
{
    char rfrom[MAX_PATH * 2], rto[MAX_PATH * 2];
    if (wl_busy())
        return oMoveFileExA(from, to, flags);
    from = redir(from, rfrom, sizeof rfrom);
    to = to ? redir(to, rto, sizeof rto) : NULL;
    wchar_t wfrom[WL_PATH], wto[WL_PATH];
    BOOL done;
    if (to && to_wide(from, wfrom) && to_wide(to, wto) && move_over(wfrom, wto, flags, &done))
        return done;
    return oMoveFileExA(from, to, flags);
}

static BOOL(WINAPI *oMoveFileExW)(LPCWSTR, LPCWSTR, DWORD);
static BOOL WINAPI hMoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags)
{
    wchar_t rfrom[MAX_PATH * 2], rto[MAX_PATH * 2];
    if (wl_busy())
        return oMoveFileExW(from, to, flags);
    from = redirW(from, rfrom, MAX_PATH * 2);
    to = to ? redirW(to, rto, MAX_PATH * 2) : NULL;
    BOOL done;
    if (move_over(from, to, flags, &done))
        return done;
    return oMoveFileExW(from, to, flags);
}

static BOOL(WINAPI *oMoveFileA)(LPCSTR, LPCSTR);
static BOOL WINAPI hMoveFileA(LPCSTR from, LPCSTR to)
{
    char rfrom[MAX_PATH * 2], rto[MAX_PATH * 2];
    if (wl_busy())
        return oMoveFileA(from, to);
    from = redir(from, rfrom, sizeof rfrom);
    to = redir(to, rto, sizeof rto);
    wchar_t wfrom[WL_PATH], wto[WL_PATH];
    BOOL done;
    if (to_wide(from, wfrom) && to_wide(to, wto) && move_over(wfrom, wto, 0, &done))
        return done;
    return oMoveFileA(from, to);
}

static BOOL(WINAPI *oMoveFileW)(LPCWSTR, LPCWSTR);
static BOOL WINAPI hMoveFileW(LPCWSTR from, LPCWSTR to)
{
    wchar_t rfrom[MAX_PATH * 2], rto[MAX_PATH * 2];
    if (wl_busy())
        return oMoveFileW(from, to);
    from = redirW(from, rfrom, MAX_PATH * 2);
    to = redirW(to, rto, MAX_PATH * 2);
    BOOL done;
    if (move_over(from, to, 0, &done))
        return done;
    return oMoveFileW(from, to);
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
    WL_HOOK(L"kernel32.dll", SetFileAttributesW),
    WL_HOOK(L"kernel32.dll", CopyFileA),
    WL_HOOK(L"kernel32.dll", CopyFileW),
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
    WL_HOOK(L"kernel32.dll", FindFirstFileExW),
    WL_HOOK(L"kernel32.dll", FindNextFileA),
    WL_HOOK(L"kernel32.dll", FindNextFileW),
    WL_HOOK(L"kernel32.dll", FindClose),
    WL_HOOK(L"kernel32.dll", CreateDirectoryA),
    WL_HOOK(L"kernel32.dll", CreateDirectoryW),
    WL_HOOK(L"shell32.dll", SHCreateDirectoryExW),
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
