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
#include <mmsystem.h>
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

static void queue_reset(void); // the reports' queue, below

HANDLE wl_vhid_open(void)
{
    HANDLE h = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!h)
        return INVALID_HANDLE_VALUE;
    queue_reset(); // opened: the reports pile up from now
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

// The overlapped reads in flight (pendingReads): answered ERROR_IO_PENDING and done by a thread
// of ours after the board's period, as a device's are; their buffer and OVERLAPPED are the game's
// until then. A game that reads until a read is left pending (an ALLS's amdaemon empties the
// board's queue so) needs them so.
#define PENDING_READS 8
#define STATUS_PENDING_ 0x103
#define STATUS_CANCELLED_ 0xC0000120

static struct
{
    HANDLE h;
    unsigned char *buffer;
    OVERLAPPED *ov;
    ULONGLONG due;
} g_pending[PENDING_READS];
static SRWLOCK g_pendingLock = SRWLOCK_INIT;

// Milliseconds, precise (GetTickCount64 moves by 15.6 ms: the reports would come in bursts)
static ULONGLONG now_ms(void)
{
    static LARGE_INTEGER frequency;
    if (!frequency.QuadPart)
        QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (ULONGLONG)(now.QuadPart * 1000 / frequency.QuadPart);
}

static void pending_done(int i, LONG_PTR status)
{
    OVERLAPPED *ov = g_pending[i].ov;
    if (!status)
    {
        memset(g_pending[i].buffer, 0, g_device->inputReportLength);
        g_device->fillInput(g_pending[i].buffer, g_device->inputReportLength);
    }
    ov->InternalHigh = status ? 0 : g_device->inputReportLength;
    ov->Internal = (ULONG_PTR)status;
    if (!status)
        queue_reset(); // the newest report given: the queue is empty
    SetEvent(ov->hEvent ? ov->hEvent : g_pending[i].h);
    g_pending[i].h = NULL;
}

static DWORD WINAPI pending_thread(void *unused)
{
    timeBeginPeriod(1); // Sleep(1) a millisecond, not the 15.6 ms of Windows' default timer
    for (;;)
    {
        Sleep(1);
        ULONGLONG now = now_ms();
        AcquireSRWLockExclusive(&g_pendingLock);
        for (int i = 0; i < PENDING_READS; i++)
            if (g_pending[i].h && g_pending[i].due <= now)
                pending_done(i, 0);
        ReleaseSRWLockExclusive(&g_pendingLock);
    }
}

// The reports Windows' HID driver holds for the game, as a board streams them (32 at most, its
// default): they pile up at the board's rate while nobody reads. A read takes one at once; when
// there is none, it waits for the next. amdaemon empties that queue after opening the board and
// gives up when it finds it empty.
#define QUEUE_MAX 32
static ULONGLONG g_queueTick; // when the queue was last counted
static int g_queued;

static void queue_reset(void)
{
    g_queueTick = now_ms();
    g_queued = 0;
}

static int queue_take(void)
{
    ULONGLONG now = now_ms();
    DWORD period = g_device->periodMs ? g_device->periodMs : 1;
    ULONGLONG n = (now - g_queueTick) / period;
    if (n)
    {
        g_queued = g_queued + n > QUEUE_MAX ? QUEUE_MAX : g_queued + (int)n;
        g_queueTick += n * period;
    }
    if (!g_queued)
        return 0;
    g_queued--;
    return 1;
}

static int pending_start(HANDLE h, void *buffer, OVERLAPPED *ov)
{
    static volatile LONG started;
    if (!InterlockedExchange(&started, 1))
        CloseHandle(CreateThread(NULL, 0, pending_thread, NULL, 0, NULL));
    int queued = 0;
    AcquireSRWLockExclusive(&g_pendingLock);
    for (int i = 0; i < PENDING_READS && !queued; i++)
        if (!g_pending[i].h)
        {
            ov->Internal = STATUS_PENDING_;
            ov->InternalHigh = 0;
            ResetEvent(ov->hEvent ? ov->hEvent : h);
            g_pending[i].h = h;
            g_pending[i].buffer = (unsigned char *)buffer;
            g_pending[i].ov = ov;
            g_pending[i].due = now_ms() + g_device->periodMs;
            queued = 1;
        }
    ReleaseSRWLockExclusive(&g_pendingLock);
    return queued;
}

// CancelIo(Ex) on the board (vdev.cpp's hook): its reads done as cancelled
BOOL wl_vhid_cancel(HANDLE h, OVERLAPPED *ov)
{
    int found = 0;
    AcquireSRWLockExclusive(&g_pendingLock);
    for (int i = 0; i < PENDING_READS; i++)
        if (g_pending[i].h == h && (!ov || g_pending[i].ov == ov))
        {
            pending_done(i, (LONG_PTR)STATUS_CANCELLED_);
            found = 1;
        }
    ReleaseSRWLockExclusive(&g_pendingLock);
    if (!found)
        SetLastError(ERROR_NOT_FOUND);
    return found;
}

void wl_vhid_forget(HANDLE h)
{
    AcquireSRWLockExclusive(&g_pendingLock); // a read in flight dropped: the game closes the handle
    for (int i = 0; i < PENDING_READS; i++)
        if (g_pending[i].h == h)
            g_pending[i].h = NULL;
    ReleaseSRWLockExclusive(&g_pendingLock);
    AcquireSRWLockExclusive(&g_lock);
    for (int i = 0; i < (int)(sizeof g_handles / sizeof g_handles[0]); i++)
        if (g_handles[i] == h)
            g_handles[i] = NULL;
    ReleaseSRWLockExclusive(&g_lock);
}

BOOL wl_vhid_read(HANDLE h, void *buffer, DWORD size, DWORD *read, OVERLAPPED *ov)
{
    const WlVirtualHid *d = g_device;
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 3)
        wl_log("ReadFile (emulated board): %lu bytes%s", size, ov ? ", overlapped" : "");
    if (size < d->inputReportLength)
    {
        SetLastError(ERROR_INVALID_USER_BUFFER);
        return FALSE;
    }
    if (ov && d->pendingReads)
    {
        AcquireSRWLockExclusive(&g_pendingLock);
        int ready = queue_take();
        ReleaseSRWLockExclusive(&g_pendingLock);
        if (!ready && pending_start(h, buffer, ov))
        {
            SetLastError(ERROR_IO_PENDING);
            return FALSE;
        }
    }
    else
    {
        // A real board answers at its polling rate: a read waits for the next report, an
        // overlapped one too (done when it returns), or a game reading again at once spins
        Sleep(d->periodMs);
    }
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

// An output report: handed to the board's function, done at once
BOOL wl_vhid_write(HANDLE h, const void *buffer, DWORD size, DWORD *written, OVERLAPPED *ov)
{
    const WlVirtualHid *d = g_device;
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 3)
        wl_log("WriteFile (emulated board): %lu bytes%s", size, ov ? ", overlapped" : "");
    if (!d->output || !buffer)
    {
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }
    d->output((const unsigned char *)buffer, (int)size);
    if (written)
        *written = size;
    if (ov)
    {
        ov->Internal = 0;
        ov->InternalHigh = size;
        if (ov->hEvent)
            SetEvent(ov->hEvent);
    }
    return TRUE;
}
