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
#include <string.h>

#include "hooks.h"
#include "log.h"
#include "vdev.h"

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

const WlVirtualDevice *wl_vdev_by_path(const char *path)
{
    if (!path || !g_deviceCount)
        return NULL;
    for (int i = 0; i < g_deviceCount; i++)
    {
        const char *name = g_devices[i]->name;
        if (!_stricmp(path, name))
            return g_devices[i];
        // "COM4" is also "\\.\COM4"
        if (!_strnicmp(path, "\\\\.\\", 4) && !_stricmp(path + 4, name))
            return g_devices[i];
    }
    return NULL;
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
};

void wl_vdev_hooks(void)
{
    wl_hooks_add(g_hooks, sizeof g_hooks / sizeof g_hooks[0]);
}
