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

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>

#include "hooks.h"
#include "jvs.h"
#include "log.h"
#include "settings.h"
#include "vdev.h"
#include "vhid.h"

static const WlVirtualDevice *g_devices[16];
static int g_deviceCount;

struct Open
{
    HANDLE h;
    const WlVirtualDevice *device;
    LONGLONG position;
};

static Open g_open[32];
static SRWLOCK g_lock = SRWLOCK_INIT;

void wl_vdev_register(const WlVirtualDevice *device)
{
    if (g_deviceCount < (int)(sizeof g_devices / sizeof g_devices[0]))
        g_devices[g_deviceCount++] = device;
    wl_log("emulated device: %s", device->name);
}

// The device a path names, passed through or not
static int device_index(const char *path)
{
    if (!path)
        return -1;
    for (int i = 0; i < g_deviceCount; i++)
    {
        const char *name = g_devices[i]->name;
        if (!_stricmp(path, name))
            return i;
        // "COM4" is also "\\.\COM4"
        if (!_strnicmp(path, "\\\\.\\", 4) && !_stricmp(path + 4, name))
            return i;
    }
    return -1;
}

// ------------------------------------------------------------------------------
// Passthrough: a serial port the game would find emulated goes to a real board on one of the PC's
// ports instead, as linuxloader does: [Emulation] EMULATE_JVS = false and [Paths] JVS_PATH for
// the JVS board, [Emulation] EMULATE_DRIVEBOARD = false and [Paths] SERIAL_<n>_PATH for the
// game's other ports (its COM<n>: a wheel's or a motor's board). Read once the settings are; the
// game's port then opens the PC's, and everything it does with it is the real port's.
// ------------------------------------------------------------------------------

static char g_through[16][32];                   // per device: the PC's port, "\\.\COM7", "" when emulated
static char g_label[16][64];                     // per device: what the menu calls it
static char g_started[16][32], g_edited[16][32]; // per device: the PC's port ("COM7") at the start, in the menu

const WlVirtualDevice *wl_vdev_by_path(const char *path)
{
    int i = device_index(path);
    return i >= 0 && !g_through[i][0] ? g_devices[i] : NULL;
}

const char *wl_vdev_real_port(const char *path)
{
    int i = device_index(path);
    return i >= 0 && g_through[i][0] ? g_through[i] : NULL;
}

// "COM4" -> 4, 0 for a name that is not a serial port
static int com_number(const char *name)
{
    if (_strnicmp(name, "COM", 3) || !isdigit((unsigned char)name[3]))
        return 0;
    return atoi(name + 3);
}

void wl_vdev_label(const char *name, const char *label)
{
    int i = device_index(name);
    if (i >= 0)
        snprintf(g_label[i], sizeof g_label[i], "%s", label);
}

// The k-th device that is a serial port
static int serial_index(int k)
{
    for (int i = 0; i < g_deviceCount; i++)
        if (com_number(g_devices[i]->name) && !k--)
            return i;
    return -1;
}

int wl_serial_count(void)
{
    int n = 0;
    while (serial_index(n) >= 0)
        n++;
    return n;
}

const char *wl_serial_name(int k)
{
    return g_devices[serial_index(k)]->name;
}

const char *wl_serial_label(int k)
{
    int i = serial_index(k);
    if (!g_label[i][0])
        snprintf(g_label[i], sizeof g_label[i], "%s", wl_jvs_is_port(g_devices[i]->name) ? "Carte JVS" : "Carte série");
    return g_label[i];
}

int wl_serial_is_jvs(int k)
{
    return wl_jvs_is_port(g_devices[serial_index(k)]->name);
}

char *wl_serial_edit(int k)
{
    return g_edited[serial_index(k)];
}

const char *wl_serial_started(int k)
{
    return g_started[serial_index(k)];
}

void wl_vdev_settings(void)
{
    int jvs = wl_settings_get_bool("Emulation", "EMULATE_JVS", 1), board = wl_settings_get_bool("Emulation", "EMULATE_DRIVEBOARD", 1);
    for (int i = 0; i < g_deviceCount; i++)
    {
        int n = com_number(g_devices[i]->name);
        if (!n)
            continue;
        char key[32], real[32];
        int isJvs = wl_jvs_is_port(g_devices[i]->name);
        snprintf(key, sizeof key, "SERIAL_%d_PATH", n);
        wl_settings_get("Paths", isJvs ? "JVS_PATH" : key, "", real, sizeof real);
        // "COM7", or "\\.\COM7" (the form Windows needs from COM10 on)
        const char *bare = !_strnicmp(real, "\\\\.\\", 4) ? real + 4 : real;
        if ((isJvs ? jvs : board) || !com_number(bare))
            continue;
        snprintf(g_through[i], sizeof g_through[i], "\\\\.\\%s", bare);
        snprintf(g_started[i], sizeof g_started[i], "%s", bare);
        memcpy(g_edited[i], g_started[i], sizeof g_edited[i]);
        wl_log("passthrough: the game's %s goes to the PC's %s (%s)", g_devices[i]->name, bare, isJvs ? "its JVS board" : "its board");
    }
}

// The menu's choices, written as linuxloader reads them: EMULATE_JVS / EMULATE_DRIVEBOARD false
// with a port to go to, SERIAL_<n>_PATH emptied for a port left emulated beside one passed
int wl_serial_save(const wchar_t *file)
{
    int ok = 1, jvs = 1, board = 1, count = wl_serial_count();
    if (!count)
        return 1;
    for (int k = 0; k < count; k++)
    {
        int i = serial_index(k);
        if (!g_edited[i][0])
            continue;
        if (wl_jvs_is_port(g_devices[i]->name))
            jvs = 0, ok &= wl_settings_write(file, "Paths", "JVS_PATH", g_edited[i]);
        else
            board = 0;
    }
    ok &= wl_settings_write(file, "Emulation", "EMULATE_JVS", jvs ? "true" : "false");
    ok &= wl_settings_write(file, "Emulation", "EMULATE_DRIVEBOARD", board ? "true" : "false");
    for (int k = 0; k < count && !board; k++)
    {
        int i = serial_index(k);
        char key[32];
        snprintf(key, sizeof key, "SERIAL_%d_PATH", com_number(g_devices[i]->name));
        if (!wl_jvs_is_port(g_devices[i]->name))
            ok &= wl_settings_write(file, "Paths", key, g_edited[i]);
    }
    return ok;
}

HANDLE wl_vdev_open(const WlVirtualDevice *device)
{
    HANDLE h = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!h)
        return INVALID_HANDLE_VALUE;
    AcquireSRWLockExclusive(&g_lock);
    for (int i = 0; i < (int)(sizeof g_open / sizeof g_open[0]); i++)
        if (!g_open[i].h)
        {
            g_open[i].h = h;
            g_open[i].device = device;
            g_open[i].position = 0;
            break;
        }
    ReleaseSRWLockExclusive(&g_lock);
    return h;
}

static Open *find(HANDLE h)
{
    if (!h || !g_deviceCount)
        return NULL;
    for (int i = 0; i < (int)(sizeof g_open / sizeof g_open[0]); i++)
        if (g_open[i].h == h)
            return &g_open[i];
    return NULL;
}

const WlVirtualDevice *wl_vdev_of(HANDLE h)
{
    if (!g_deviceCount)
        return NULL;
    AcquireSRWLockShared(&g_lock);
    Open *o = find(h);
    const WlVirtualDevice *d = o ? o->device : NULL;
    ReleaseSRWLockShared(&g_lock);
    return d;
}

void wl_vdev_forget(HANDLE h)
{
    AcquireSRWLockExclusive(&g_lock);
    Open *o = find(h);
    if (o)
        o->h = NULL;
    ReleaseSRWLockExclusive(&g_lock);
}

static LONGLONG position_of(HANDLE h, OVERLAPPED *ov)
{
    if (ov)
        return ((LONGLONG)ov->OffsetHigh << 32) | ov->Offset;
    AcquireSRWLockShared(&g_lock);
    Open *o = find(h);
    LONGLONG p = o ? o->position : 0;
    ReleaseSRWLockShared(&g_lock);
    return p;
}

static void advance(HANDLE h, OVERLAPPED *ov, DWORD n)
{
    if (ov)
        return;
    AcquireSRWLockExclusive(&g_lock);
    Open *o = find(h);
    if (o)
        o->position += n;
    ReleaseSRWLockExclusive(&g_lock);
}

// An overlapped call is done at once: the OVERLAPPED says so, its event is set
static BOOL complete(OVERLAPPED *ov, BOOL ok, DWORD n)
{
    if (ov)
    {
        ov->Internal = ok ? 0 : (ULONG_PTR)0xC0000001; // STATUS_SUCCESS, STATUS_UNSUCCESSFUL
        ov->InternalHigh = n;
        if (ov->hEvent)
            SetEvent(ov->hEvent);
    }
    return ok;
}

BOOL wl_vdev_read(HANDLE h, void *buffer, DWORD size, DWORD *read, OVERLAPPED *ov)
{
    const WlVirtualDevice *d = wl_vdev_of(h);
    DWORD n = 0;
    if (!d->read)
    {
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }
    BOOL ok = d->read(buffer, size, &n, position_of(h, ov));
    if (ok)
        advance(h, ov, n);
    if (read)
        *read = n;
    return complete(ov, ok, n);
}

BOOL wl_vdev_write(HANDLE h, const void *buffer, DWORD size, DWORD *written, OVERLAPPED *ov)
{
    const WlVirtualDevice *d = wl_vdev_of(h);
    DWORD n = 0;
    if (!d->write)
    {
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }
    BOOL ok = d->write(buffer, size, &n, position_of(h, ov));
    if (ok)
        advance(h, ov, n);
    if (written)
        *written = n;
    return complete(ov, ok, n);
}

BOOL wl_vdev_ioctl(HANDLE h, DWORD code, const void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned, OVERLAPPED *ov)
{
    const WlVirtualDevice *d = wl_vdev_of(h);
    DWORD n = 0;
    if (!d->ioctl)
    {
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }
    BOOL ok = d->ioctl(code, in, inSize, out, outSize, &n);
    if (returned)
        *returned = n;
    return complete(ov, ok, n);
}

// ------------------------------------------------------------------------------
// The file position, and the serial port settings the devices.cpp observer does not hook
// ------------------------------------------------------------------------------

static BOOL(WINAPI *oSetFilePointerEx)(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD);
static BOOL WINAPI hSetFilePointerEx(HANDLE h, LARGE_INTEGER distance, PLARGE_INTEGER result, DWORD method)
{
    if (!g_deviceCount || !wl_vdev_of(h))
        return oSetFilePointerEx(h, distance, result, method);
    AcquireSRWLockExclusive(&g_lock);
    Open *o = find(h);
    if (o)
    {
        // FILE_END has no meaning on a device: taken as from the start
        o->position = (method == FILE_CURRENT ? o->position : 0) + distance.QuadPart;
        if (result)
            result->QuadPart = o->position;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return TRUE;
}

static DWORD(WINAPI *oSetFilePointer)(HANDLE, LONG, PLONG, DWORD);
static DWORD WINAPI hSetFilePointer(HANDLE h, LONG low, PLONG high, DWORD method)
{
    if (!g_deviceCount || !wl_vdev_of(h))
        return oSetFilePointer(h, low, high, method);
    LARGE_INTEGER distance, result;
    distance.QuadPart = high ? ((LONGLONG)*high << 32) | (DWORD)low : low;
    hSetFilePointerEx(h, distance, &result, method);
    if (high)
        *high = result.HighPart;
    return result.LowPart;
}

// ReadFileEx / WriteFileEx (Mario Kart Arcade GP DX's JVS): done at once, the game's routine
// called as Windows calls it, an APC of the calling thread run at its next alertable wait. The
// OVERLAPPED's event is the game's own here: not set.
struct ApcCall
{
    LPOVERLAPPED_COMPLETION_ROUTINE fn;
    DWORD error, bytes;
    OVERLAPPED *ov;
};

static void CALLBACK apc_complete(ULONG_PTR p)
{
    ApcCall *c = (ApcCall *)p;
    c->fn(c->error, c->bytes, c->ov);
    free(c);
}

static BOOL queue_completion_to(HANDLE thread, LPOVERLAPPED_COMPLETION_ROUTINE fn, DWORD error, DWORD n, OVERLAPPED *ov)
{
    if (ov)
    {
        ov->Internal = error ? (ULONG_PTR)0xC0000001 : 0;
        ov->InternalHigh = n;
    }
    ApcCall *c = (ApcCall *)malloc(sizeof *c);
    if (!c || !fn)
    {
        free(c);
        return !error;
    }
    c->fn = fn;
    c->error = error;
    c->bytes = n;
    c->ov = ov;
    if (!QueueUserAPC(apc_complete, thread, (ULONG_PTR)c))
    {
        free(c);
        return FALSE;
    }
    return TRUE;
}

static BOOL queue_completion(LPOVERLAPPED_COMPLETION_ROUTINE fn, BOOL ok, DWORD n, OVERLAPPED *ov)
{
    return queue_completion_to(GetCurrentThread(), fn, ok ? 0 : GetLastError(), n, ov);
}

// A read with nothing to read yet stays pending, as on a serial port: done when the device has
// data (its answer to the next write) or after the read timeout, by a service thread
#define PENDING_READS 8
#define PENDING_TIMEOUT_MS 600 // Mario Kart Arcade GP DX's: read interval 600, total 2*n+600
static struct
{
    HANDLE h, thread;
    void *buf;
    DWORD size;
    OVERLAPPED *ov;
    LPOVERLAPPED_COMPLETION_ROUTINE fn;
    ULONGLONG deadline;
    int aborted;
} g_pending[PENDING_READS];
static SRWLOCK g_pendingLock = SRWLOCK_INIT;
static HANDLE g_pendingThread;

static DWORD WINAPI pending_reads(LPVOID)
{
    for (;;)
    {
        Sleep(1);
        AcquireSRWLockExclusive(&g_pendingLock);
        for (int i = 0; i < PENDING_READS; i++)
        {
            if (!g_pending[i].h)
                continue;
            DWORD n = 0, error = 0;
            int gone = !wl_vdev_of(g_pending[i].h);
            if (g_pending[i].aborted || gone)
                error = ERROR_OPERATION_ABORTED;
            else if (!wl_vdev_read(g_pending[i].h, g_pending[i].buf, g_pending[i].size, &n, NULL))
                error = GetLastError();
            if (!error && !n && GetTickCount64() < g_pending[i].deadline)
                continue;
            static volatile LONG logged;
            if (InterlockedIncrement(&logged) <= 8)
                wl_log("ReadFileEx (emulated): done, %lu of %lu bytes, error %lu", n, g_pending[i].size, error);
            queue_completion_to(g_pending[i].thread, g_pending[i].fn, error, n, g_pending[i].ov);
            CloseHandle(g_pending[i].thread);
            g_pending[i].h = NULL;
        }
        ReleaseSRWLockExclusive(&g_pendingLock);
    }
}

static BOOL(WINAPI *oReadFileEx)(HANDLE, LPVOID, DWORD, LPOVERLAPPED, LPOVERLAPPED_COMPLETION_ROUTINE);
static BOOL WINAPI hReadFileEx(HANDLE h, LPVOID buf, DWORD size, LPOVERLAPPED ov, LPOVERLAPPED_COMPLETION_ROUTINE fn)
{
    if (!g_deviceCount || wl_busy() || !wl_vdev_of(h))
        return oReadFileEx(h, buf, size, ov, fn);
    DWORD n = 0;
    BOOL ok = wl_vdev_read(h, buf, size, &n, NULL);
    if (!ok || n)
        return queue_completion(fn, ok, n, ov);
    HANDLE thread = OpenThread(THREAD_SET_CONTEXT, FALSE, GetCurrentThreadId());
    AcquireSRWLockExclusive(&g_pendingLock);
    int slot = -1;
    for (int i = 0; i < PENDING_READS && slot < 0; i++)
        if (!g_pending[i].h)
            slot = i;
    if (slot >= 0 && thread)
    {
        g_pending[slot].h = h;
        g_pending[slot].thread = thread;
        g_pending[slot].buf = buf;
        g_pending[slot].size = size;
        g_pending[slot].ov = ov;
        g_pending[slot].fn = fn;
        g_pending[slot].deadline = GetTickCount64() + PENDING_TIMEOUT_MS;
        g_pending[slot].aborted = 0;
        if (!g_pendingThread)
        {
            wl_busy_enter();
            g_pendingThread = CreateThread(NULL, 0, pending_reads, NULL, 0, NULL);
            wl_busy_leave();
        }
    }
    ReleaseSRWLockExclusive(&g_pendingLock);
    if (slot < 0 || !thread)
    {
        if (thread)
            CloseHandle(thread);
        return queue_completion(fn, TRUE, 0, ov); // no room: a timed out read
    }
    if (ov)
        ov->Internal = (ULONG_PTR)0x103; // STATUS_PENDING
    return TRUE;
}

static BOOL(WINAPI *oWriteFileEx)(HANDLE, LPCVOID, DWORD, LPOVERLAPPED, LPOVERLAPPED_COMPLETION_ROUTINE);
static BOOL WINAPI hWriteFileEx(HANDLE h, LPCVOID buf, DWORD size, LPOVERLAPPED ov, LPOVERLAPPED_COMPLETION_ROUTINE fn)
{
    if (!g_deviceCount || wl_busy() || !wl_vdev_of(h))
        return oWriteFileEx(h, buf, size, ov, fn);
    DWORD n = 0;
    BOOL ok = wl_vdev_write(h, buf, size, &n, NULL);
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 12)
        wl_log_hex("WriteFileEx (emulated):", buf, size);
    return queue_completion(fn, ok, n, ov);
}

// A pending read cancelled: done with ERROR_OPERATION_ABORTED
static BOOL(WINAPI *oCancelIoEx)(HANDLE, LPOVERLAPPED);
static BOOL WINAPI hCancelIoEx(HANDLE h, LPOVERLAPPED ov)
{
    if (!wl_busy() && wl_vhid_is_handle(h))
        return wl_vhid_cancel(h, ov);
    if (!g_deviceCount || !wl_vdev_of(h))
        return oCancelIoEx(h, ov);
    int found = 0;
    AcquireSRWLockExclusive(&g_pendingLock);
    for (int i = 0; i < PENDING_READS; i++)
        if (g_pending[i].h == h && (!ov || g_pending[i].ov == ov))
        {
            g_pending[i].aborted = 1;
            found = 1;
        }
    ReleaseSRWLockExclusive(&g_pendingLock);
    if (!found)
        SetLastError(ERROR_NOT_FOUND);
    return found;
}

static BOOL(WINAPI *oGetCommTimeouts)(HANDLE, LPCOMMTIMEOUTS);
static BOOL WINAPI hGetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t)
{
    if (!g_deviceCount || !wl_vdev_of(h))
        return oGetCommTimeouts(h, t);
    if (t)
        memset(t, 0, sizeof *t);
    return TRUE;
}

static WlHook g_hooks[] = {
    WL_HOOK(L"kernel32.dll", SetFilePointerEx),
    WL_HOOK(L"kernel32.dll", SetFilePointer),
    WL_HOOK(L"kernel32.dll", GetCommTimeouts),
    WL_HOOK(L"kernel32.dll", ReadFileEx),
    WL_HOOK(L"kernel32.dll", WriteFileEx),
    WL_HOOK(L"kernel32.dll", CancelIoEx),
};

void wl_vdev_hooks(void)
{
    wl_hooks_add(g_hooks, sizeof g_hooks / sizeof g_hooks[0]);
}
