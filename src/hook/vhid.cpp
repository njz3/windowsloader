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
#include "vhid.h"

static const WlVirtualHid *g_device;

// The handles the game holds on the device: real event handles, so that whatever we do not
// emulate (CloseHandle, WaitForSingleObject...) still sees a valid handle
static HANDLE g_handles[8];
static SRWLOCK g_lock = SRWLOCK_INIT;

void wl_vhid_register(const WlVirtualHid *device)
{
    g_device = device;
    wl_log("emulated HID device: VID_%04X PID_%04X, usage %04X:%04X, %u-byte input reports every %lu ms", device->vendorId, device->productId,
           device->usagePage, device->usage, device->inputReportLength, device->periodMs);
}

int wl_vhid_active(void)
{
    return g_device != NULL;
}

const WlVirtualHid *wl_vhid_device(void)
{
    return g_device;
}

int wl_vhid_is_path(const char *path)
{
    return g_device && path && !_stricmp(path, g_device->path);
}

HANDLE wl_vhid_open(void)
{
    HANDLE h = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!h)
        return INVALID_HANDLE_VALUE;
    AcquireSRWLockExclusive(&g_lock);
    for (int i = 0; i < (int)(sizeof g_handles / sizeof g_handles[0]); i++)
        if (!g_handles[i])
        {
            g_handles[i] = h;
            break;
        }
    ReleaseSRWLockExclusive(&g_lock);
    return h;
}

int wl_vhid_is_handle(HANDLE h)
{
    if (!g_device || !h)
        return 0;
    int found = 0;
    AcquireSRWLockShared(&g_lock);
    for (int i = 0; i < (int)(sizeof g_handles / sizeof g_handles[0]) && !found; i++)
        found = g_handles[i] == h;
    ReleaseSRWLockShared(&g_lock);
    return found;
}

void wl_vhid_forget(HANDLE h)
{
    AcquireSRWLockExclusive(&g_lock);
    for (int i = 0; i < (int)(sizeof g_handles / sizeof g_handles[0]); i++)
        if (g_handles[i] == h)
            g_handles[i] = NULL;
    ReleaseSRWLockExclusive(&g_lock);
}

BOOL wl_vhid_read(HANDLE h, void *buffer, DWORD size, DWORD *read, OVERLAPPED *ov)
{
    const WlVirtualHid *d = g_device;
    if (size < d->inputReportLength)
    {
        SetLastError(ERROR_INVALID_USER_BUFFER);
        return FALSE;
    }
    // A real board answers at its polling rate; a blocking read waits for the next report
    if (!ov)
        Sleep(d->periodMs);
    memset(buffer, 0, d->inputReportLength);
    d->fillInput((unsigned char *)buffer, d->inputReportLength);
    if (read)
        *read = d->inputReportLength;
    if (ov)
    {
        ov->Internal = 0;
        ov->InternalHigh = d->inputReportLength;
        if (ov->hEvent)
            SetEvent(ov->hEvent);
    }
    return TRUE;
}
