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

// Observer for the cabinet hardware: serial ports (JVS, drive board), USB HID devices and their
// enumeration through SetupAPI. Nothing is emulated yet; this shows what the game expects.

#include <windows.h>
#include <setupapi.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <stddef.h>

#include "devices.h"
#include "hooks.h"
#include "log.h"
#include "vhid.h"

// ------------------------------------------------------------------------------
// Device handles
// ------------------------------------------------------------------------------

struct DevEntry
{
    HANDLE h;
    WlDevice dev;
};

static DevEntry g_devs[64];
static volatile LONG g_devActive; // entries in use, read without the lock as a fast path
static SRWLOCK g_devLock = SRWLOCK_INIT;

int wl_dev_is_device_path(const char *p)
{
    if (!p)
        return 0;
    if (p[0] == '\\' && p[1] == '\\' && (p[2] == '.' || p[2] == '?') && p[3] == '\\')
    {
        const char *rest = p + 4;
        if (isalpha((unsigned char)rest[0]) && rest[1] == ':')
            return 0; // \\?\C:\...
        if (!_strnicmp(rest, "UNC\\", 4))
            return 0;
        return 1;
    }
    if ((!_strnicmp(p, "COM", 3) || !_strnicmp(p, "LPT", 3)) && isdigit((unsigned char)p[3]))
    {
        const char *q = p + 3;
        while (isdigit((unsigned char)*q))
            q++;
        return *q == 0 || (*q == ':' && q[1] == 0);
    }
    return 0;
}

void wl_dev_track(HANDLE h, const char *path)
{
    AcquireSRWLockExclusive(&g_devLock);
    for (int i = 0; i < (int)(sizeof g_devs / sizeof g_devs[0]); i++)
    {
        if (g_devs[i].h)
            continue;
        g_devs[i].h = h;
        memset(&g_devs[i].dev, 0, sizeof g_devs[i].dev);
        strncpy(g_devs[i].dev.name, path, sizeof g_devs[i].dev.name - 1);
        InterlockedIncrement(&g_devActive);
        break;
    }
    ReleaseSRWLockExclusive(&g_devLock);
}

int wl_dev_untrack(HANDLE h, WlDevice *out, int tryOnly)
{
    if (!g_devActive)
        return 0;
    if (tryOnly)
    {
        if (!TryAcquireSRWLockExclusive(&g_devLock))
            return 0;
    }
    else
        AcquireSRWLockExclusive(&g_devLock);
    int found = 0;
    for (int i = 0; i < (int)(sizeof g_devs / sizeof g_devs[0]) && !found; i++)
    {
        if (g_devs[i].h != h)
            continue;
        *out = g_devs[i].dev;
        g_devs[i].h = NULL;
        InterlockedDecrement(&g_devActive);
        found = 1;
    }
    ReleaseSRWLockExclusive(&g_devLock);
    return found;
}

static unsigned counter_limit(WlDevCounter c)
{
    switch (c)
    {
    case WL_DEV_IO:
        return (unsigned)g_wl.ioLimit;
    case WL_DEV_SETUP:
        return 100;
    case WL_DEV_POLL:
        return 5;
    default:
        return 10;
    }
}

int wl_dev_count(HANDLE h, WlDevCounter c, WlDevice *out)
{
    if (!g_devActive || !h || h == INVALID_HANDLE_VALUE)
        return 0;
    unsigned n = 0;
    AcquireSRWLockExclusive(&g_devLock);
    for (int i = 0; i < (int)(sizeof g_devs / sizeof g_devs[0]); i++)
    {
        if (g_devs[i].h != h)
            continue;
        n = ++g_devs[i].dev.count[c];
        *out = g_devs[i].dev;
        break;
    }
    ReleaseSRWLockExclusive(&g_devLock);

    unsigned limit = counter_limit(c);
    if (!n || n > limit + 1)
        return 0;
    if (n == limit + 1)
    {
        static const char *kinds[] = {"reads/writes", "settings calls", "polling calls", "empty reads"};
        wl_log("%s: further %s not logged (limit %u)", out->name, kinds[c], limit);
        return 0;
    }
    return 1;
}

// ------------------------------------------------------------------------------
// Serial ports
// ------------------------------------------------------------------------------

static void log_dcb(const char *what, const WlDevice *d, const DCB *dcb, BOOL ok)
{
    static const char parity[] = "NOEMS";
    static const char *stop[] = {"1", "1.5", "2"};
    if (!ok || !dcb)
    {
        wl_log("%s %s -> failed (error %lu)", what, d->name, GetLastError());
        return;
    }
    wl_log("%s %s: %lu baud %u%c%s rts=%u dtr=%u cts-flow=%u dsr-flow=%u xon=%u", what, d->name, dcb->BaudRate, dcb->ByteSize,
           dcb->Parity < 5 ? parity[dcb->Parity] : '?', dcb->StopBits < 3 ? stop[dcb->StopBits] : "?", dcb->fRtsControl,
           dcb->fDtrControl, dcb->fOutxCtsFlow, dcb->fOutxDsrFlow, dcb->fOutX);
}

static BOOL(WINAPI *oGetCommState)(HANDLE, LPDCB);
static BOOL WINAPI hGetCommState(HANDLE h, LPDCB dcb)
{
    int nested = wl_busy();
    wl_busy_enter();
    BOOL ok = oGetCommState(h, dcb);
    wl_busy_leave();
    WlDevice d;
    if (!nested && wl_dev_count(h, WL_DEV_SETUP, &d))
        log_dcb("GetCommState", &d, dcb, ok);
    return ok;
}

static BOOL(WINAPI *oSetCommState)(HANDLE, LPDCB);
static BOOL WINAPI hSetCommState(HANDLE h, LPDCB dcb)
{
    int nested = wl_busy();
    wl_busy_enter();
    BOOL ok = oSetCommState(h, dcb);
    wl_busy_leave();
    WlDevice d;
    if (!nested && wl_dev_count(h, WL_DEV_SETUP, &d))
        log_dcb("SetCommState", &d, dcb, ok);
    return ok;
}

static BOOL(WINAPI *oSetCommTimeouts)(HANDLE, LPCOMMTIMEOUTS);
static BOOL WINAPI hSetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t)
{
    int nested = wl_busy();
    wl_busy_enter();
    BOOL ok = oSetCommTimeouts(h, t);
    wl_busy_leave();
    WlDevice d;
    if (!nested && t && wl_dev_count(h, WL_DEV_SETUP, &d))
        wl_log("SetCommTimeouts %s: read interval=%lu total=%lu*n+%lu, write total=%lu*n+%lu -> %s", d.name, t->ReadIntervalTimeout,
               t->ReadTotalTimeoutMultiplier, t->ReadTotalTimeoutConstant, t->WriteTotalTimeoutMultiplier, t->WriteTotalTimeoutConstant,
               ok ? "ok" : "failed");
    return ok;
}

static BOOL(WINAPI *oSetupComm)(HANDLE, DWORD, DWORD);
static BOOL WINAPI hSetupComm(HANDLE h, DWORD in, DWORD out)
{
    int nested = wl_busy();
    wl_busy_enter();
    BOOL ok = oSetupComm(h, in, out);
    wl_busy_leave();
    WlDevice d;
    if (!nested && wl_dev_count(h, WL_DEV_SETUP, &d))
        wl_log("SetupComm %s: in=%lu out=%lu -> %s", d.name, in, out, ok ? "ok" : "failed");
    return ok;
}

static BOOL(WINAPI *oPurgeComm)(HANDLE, DWORD);
static BOOL WINAPI hPurgeComm(HANDLE h, DWORD flags)
{
    int nested = wl_busy();
    wl_busy_enter();
    BOOL ok = oPurgeComm(h, flags);
    wl_busy_leave();
    WlDevice d;
    if (!nested && wl_dev_count(h, WL_DEV_SETUP, &d))
        wl_log("PurgeComm %s: 0x%lx -> %s", d.name, flags, ok ? "ok" : "failed");
    return ok;
}

static BOOL(WINAPI *oSetCommMask)(HANDLE, DWORD);
static BOOL WINAPI hSetCommMask(HANDLE h, DWORD mask)
{
    int nested = wl_busy();
    wl_busy_enter();
    BOOL ok = oSetCommMask(h, mask);
    wl_busy_leave();
    WlDevice d;
    if (!nested && wl_dev_count(h, WL_DEV_SETUP, &d))
        wl_log("SetCommMask %s: 0x%lx -> %s", d.name, mask, ok ? "ok" : "failed");
    return ok;
}

static BOOL(WINAPI *oEscapeCommFunction)(HANDLE, DWORD);
static BOOL WINAPI hEscapeCommFunction(HANDLE h, DWORD func)
{
    static const char *names[] = {"?", "SETXOFF", "SETXON", "SETRTS", "CLRRTS", "SETDTR", "CLRDTR", "RESETDEV", "SETBREAK", "CLRBREAK"};
    int nested = wl_busy();
    wl_busy_enter();
    BOOL ok = oEscapeCommFunction(h, func);
    wl_busy_leave();
    WlDevice d;
    if (!nested && wl_dev_count(h, WL_DEV_IO, &d))
        wl_log("EscapeCommFunction %s: %s -> %s", d.name, func < 10 ? names[func] : "?", ok ? "ok" : "failed");
    return ok;
}

static BOOL(WINAPI *oClearCommError)(HANDLE, LPDWORD, LPCOMSTAT);
static BOOL WINAPI hClearCommError(HANDLE h, LPDWORD errors, LPCOMSTAT stat)
{
    int nested = wl_busy();
    wl_busy_enter();
    BOOL ok = oClearCommError(h, errors, stat);
    wl_busy_leave();
    WlDevice d;
    if (!nested && wl_dev_count(h, WL_DEV_POLL, &d))
        wl_log("ClearCommError %s: errors=0x%lx in=%lu out=%lu -> %s", d.name, errors ? *errors : 0, stat ? stat->cbInQue : 0,
               stat ? stat->cbOutQue : 0, ok ? "ok" : "failed");
    return ok;
}

static BOOL(WINAPI *oGetCommModemStatus)(HANDLE, LPDWORD);
static BOOL WINAPI hGetCommModemStatus(HANDLE h, LPDWORD status)
{
    int nested = wl_busy();
    wl_busy_enter();
    BOOL ok = oGetCommModemStatus(h, status);
    wl_busy_leave();
    WlDevice d;
    if (!nested && wl_dev_count(h, WL_DEV_POLL, &d))
        wl_log("GetCommModemStatus %s: 0x%lx -> %s", d.name, status ? *status : 0, ok ? "ok" : "failed");
    return ok;
}

static BOOL(WINAPI *oWaitCommEvent)(HANDLE, LPDWORD, LPOVERLAPPED);
static BOOL WINAPI hWaitCommEvent(HANDLE h, LPDWORD mask, LPOVERLAPPED ov)
{
    int nested = wl_busy();
    wl_busy_enter();
    BOOL ok = oWaitCommEvent(h, mask, ov);
    wl_busy_leave();
    DWORD err = GetLastError();
    WlDevice d;
    if (!nested && wl_dev_count(h, WL_DEV_IO, &d))
        wl_log("WaitCommEvent %s -> %s mask=0x%lx", d.name, ok ? "ok" : err == ERROR_IO_PENDING ? "pending" : "failed", mask && ok ? *mask : 0);
    SetLastError(err);
    return ok;
}

// ------------------------------------------------------------------------------
// Generic device control
// ------------------------------------------------------------------------------

static BOOL(WINAPI *oDeviceIoControl)(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL WINAPI hDeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD inSize, LPVOID out, DWORD outSize, LPDWORD ret, LPOVERLAPPED ov)
{
    int nested = wl_busy();
    wl_busy_enter();
    BOOL ok = oDeviceIoControl(h, code, in, inSize, out, outSize, ret, ov);
    wl_busy_leave();
    DWORD err = GetLastError();
    WlDevice d;
    if (!nested && wl_dev_count(h, WL_DEV_IO, &d))
    {
        char prefix[256];
        snprintf(prefix, sizeof prefix, "DeviceIoControl %s: code 0x%08lx (type 0x%lx function 0x%lx) in=%lu out=%lu -> %s, in:", d.name, code,
                 code >> 16, (code >> 2) & 0xfff, inSize, ok && ret ? *ret : 0, ok ? "ok" : err == ERROR_IO_PENDING ? "pending" : "failed");
        wl_log_hex(prefix, in, in ? inSize : 0);
        if (ok && out && ret && *ret)
            wl_log_hex("  out:", out, *ret);
    }
    SetLastError(err);
    return ok;
}

// ------------------------------------------------------------------------------
// SetupAPI: which device classes the game enumerates, and which device paths it gets
// ------------------------------------------------------------------------------

static const char *guid_str(const GUID *g, char *buf, size_t size)
{
    if (!g)
        return "(all classes)";
    snprintf(buf, size, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g->Data1, g->Data2, g->Data3, g->Data4[0], g->Data4[1],
             g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
    return buf;
}

// With an emulated HID board, the game's enumerations of the HID class list that board only
static const GUID g_hidGuid = {0x4D1E55B2, 0xF16F, 0x11CF, {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
#define VHID_MAGIC ((ULONG_PTR)0x574C4844) // SP_DEVICE_INTERFACE_DATA.Reserved of the emulated board

static HDEVINFO g_hidSets[8];
static int g_hidSetNext;

static void add_hid_set(HDEVINFO set)
{
    g_hidSets[g_hidSetNext++ % (sizeof g_hidSets / sizeof g_hidSets[0])] = set;
}

static int is_hid_set(HDEVINFO set)
{
    for (int i = 0; i < (int)(sizeof g_hidSets / sizeof g_hidSets[0]); i++)
        if (g_hidSets[i] && g_hidSets[i] == set)
            return 1;
    return 0;
}

static void on_class_devs(const GUID *cls, const char *enumerator, DWORD flags, HDEVINFO r, const void *caller)
{
    char g[48], where[128];
    int emulated = wl_vhid_active() && cls && IsEqualGUID(*cls, g_hidGuid) && r != INVALID_HANDLE_VALUE;
    if (emulated)
        add_hid_set(r);
    wl_log("SetupDiGetClassDevs %s enumerator=%s flags=0x%lx%s [%s]", guid_str(cls, g, sizeof g), enumerator ? enumerator : "-", flags,
           emulated ? " -> the emulated board only" : "", wl_where(caller, where, sizeof where));
}

static HDEVINFO(WINAPI *oSetupDiGetClassDevsA)(const GUID *, PCSTR, HWND, DWORD);
static HDEVINFO WINAPI hSetupDiGetClassDevsA(const GUID *cls, PCSTR enumerator, HWND parent, DWORD flags)
{
    int nested = wl_busy();
    wl_busy_enter();
    HDEVINFO r = oSetupDiGetClassDevsA(cls, enumerator, parent, flags);
    wl_busy_leave();
    if (!nested && !wl_is_system_caller(WL_CALLER))
        on_class_devs(cls, enumerator, flags, r, WL_CALLER);
    return r;
}

static HDEVINFO(WINAPI *oSetupDiGetClassDevsW)(const GUID *, PCWSTR, HWND, DWORD);
static HDEVINFO WINAPI hSetupDiGetClassDevsW(const GUID *cls, PCWSTR enumerator, HWND parent, DWORD flags)
{
    int nested = wl_busy();
    wl_busy_enter();
    HDEVINFO r = oSetupDiGetClassDevsW(cls, enumerator, parent, flags);
    wl_busy_leave();
    if (!nested && !wl_is_system_caller(WL_CALLER))
    {
        char e[128];
        on_class_devs(cls, enumerator ? wl_u8(enumerator, e, sizeof e) : NULL, flags, r, WL_CALLER);
    }
    return r;
}

static BOOL(WINAPI *oSetupDiEnumDeviceInterfaces)(HDEVINFO, PSP_DEVINFO_DATA, const GUID *, DWORD, PSP_DEVICE_INTERFACE_DATA);
static BOOL WINAPI hSetupDiEnumDeviceInterfaces(HDEVINFO set, PSP_DEVINFO_DATA info, const GUID *cls, DWORD index, PSP_DEVICE_INTERFACE_DATA data)
{
    if (wl_busy() || !is_hid_set(set))
        return oSetupDiEnumDeviceInterfaces(set, info, cls, index, data);
    if (index > 0)
    {
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }
    if (!data || data->cbSize != sizeof(SP_DEVICE_INTERFACE_DATA))
    {
        SetLastError(ERROR_INVALID_USER_BUFFER);
        return FALSE;
    }
    data->InterfaceClassGuid = g_hidGuid;
    data->Flags = SPINT_ACTIVE;
    data->Reserved = VHID_MAGIC;
    return TRUE;
}

// Path of the emulated board for SetupDiGetDeviceInterfaceDetail; 0 with the error set when the
// game's buffer cannot take it (the first call only asks for the size)
static int vhid_detail(DWORD detailCbSize, DWORD expectedCbSize, DWORD pathOffset, DWORD charSize, DWORD size,
                       PDWORD required, PSP_DEVINFO_DATA info, int hasDetail)
{
    DWORD needed = pathOffset + (DWORD)(strlen(wl_vhid_device()->path) + 1) * charSize;
    if (required)
        *required = needed;
    if (!hasDetail || size < needed)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    if (detailCbSize != expectedCbSize)
    {
        SetLastError(ERROR_INVALID_USER_BUFFER);
        return 0;
    }
    if (info && info->cbSize == sizeof(SP_DEVINFO_DATA))
    {
        info->ClassGuid = g_hidGuid;
        info->DevInst = 0;
        info->Reserved = VHID_MAGIC;
    }
    return 1;
}

static BOOL(WINAPI *oSetupDiGetDeviceInterfaceDetailA)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA_A, DWORD, PDWORD,
                                                       PSP_DEVINFO_DATA);
static BOOL WINAPI hSetupDiGetDeviceInterfaceDetailA(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA itf, PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail,
                                                     DWORD size, PDWORD required, PSP_DEVINFO_DATA info)
{
    int nested = wl_busy();
    if (!nested && itf && itf->Reserved == VHID_MAGIC && is_hid_set(set))
    {
        if (!vhid_detail(detail ? detail->cbSize : 0, sizeof *detail, offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_A, DevicePath), 1, size, required,
                         info, detail != NULL))
            return FALSE;
        strcpy(detail->DevicePath, wl_vhid_device()->path);
        wl_log("SetupDi device: %s (emulated)", detail->DevicePath);
        return TRUE;
    }
    wl_busy_enter();
    BOOL ok = oSetupDiGetDeviceInterfaceDetailA(set, itf, detail, size, required, info);
    wl_busy_leave();
    if (!nested && ok && detail && !wl_is_system_caller(WL_CALLER))
        wl_log("SetupDi device: %s", detail->DevicePath);
    return ok;
}

static BOOL(WINAPI *oSetupDiGetDeviceInterfaceDetailW)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA_W, DWORD, PDWORD,
                                                       PSP_DEVINFO_DATA);
static BOOL WINAPI hSetupDiGetDeviceInterfaceDetailW(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA itf, PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail,
                                                     DWORD size, PDWORD required, PSP_DEVINFO_DATA info)
{
    int nested = wl_busy();
    if (!nested && itf && itf->Reserved == VHID_MAGIC && is_hid_set(set))
    {
        if (!vhid_detail(detail ? detail->cbSize : 0, sizeof *detail, offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath), sizeof(WCHAR), size,
                         required, info, detail != NULL))
            return FALSE;
        MultiByteToWideChar(CP_ACP, 0, wl_vhid_device()->path, -1, detail->DevicePath, (int)strlen(wl_vhid_device()->path) + 1);
        wl_log("SetupDi device: %s (emulated)", wl_vhid_device()->path);
        return TRUE;
    }
    wl_busy_enter();
    BOOL ok = oSetupDiGetDeviceInterfaceDetailW(set, itf, detail, size, required, info);
    wl_busy_leave();
    if (!nested && ok && detail && !wl_is_system_caller(WL_CALLER))
    {
        char path[512];
        wl_log("SetupDi device: %s", wl_u8(detail->DevicePath, path, sizeof path));
    }
    return ok;
}

// ------------------------------------------------------------------------------
// HID (hid.dll). Layouts of HIDD_ATTRIBUTES and the start of HIDP_CAPS, so the DDK headers are not needed.
// ------------------------------------------------------------------------------

struct WlHidAttributes
{
    ULONG Size;
    USHORT VendorID;
    USHORT ProductID;
    USHORT VersionNumber;
};

struct WlHidCaps
{
    USHORT Usage;
    USHORT UsagePage;
    USHORT InputReportByteLength;
    USHORT OutputReportByteLength;
    USHORT FeatureReportByteLength;
    USHORT Reserved[17];
    USHORT NumberLinkCollectionNodes;
    USHORT Counts[9]; // button, value and data index counts of the three report kinds
};

#define HIDP_STATUS_SUCCESS 0x00110000
#define HIDP_STATUS_INVALID_PREPARSED_DATA ((LONG)0xC0110001)

// What HidD_GetPreparsedData hands out for the emulated board; only HidP_GetCaps reads it
static char g_vhidPreparsed[64];

static BOOLEAN(WINAPI *oHidD_GetAttributes)(HANDLE, WlHidAttributes *);
static BOOLEAN WINAPI hHidD_GetAttributes(HANDLE h, WlHidAttributes *a)
{
    int nested = wl_busy();
    if (!nested && wl_vhid_is_handle(h))
    {
        const WlVirtualHid *v = wl_vhid_device();
        if (!a)
            return FALSE;
        a->Size = sizeof *a;
        a->VendorID = v->vendorId;
        a->ProductID = v->productId;
        a->VersionNumber = v->version;
        wl_log("HidD_GetAttributes (emulated board): VID_%04X PID_%04X", v->vendorId, v->productId);
        return TRUE;
    }
    wl_busy_enter();
    BOOLEAN ok = oHidD_GetAttributes(h, a);
    wl_busy_leave();
    WlDevice d;
    if (!nested && wl_dev_count(h, WL_DEV_SETUP, &d))
    {
        if (ok && a)
            wl_log("HidD_GetAttributes %s: VID_%04X PID_%04X version 0x%04X", d.name, a->VendorID, a->ProductID, a->VersionNumber);
        else
            wl_log("HidD_GetAttributes %s -> failed", d.name);
    }
    return ok;
}

static BOOLEAN(WINAPI *oHidD_GetPreparsedData)(HANDLE, void **);
static BOOLEAN WINAPI hHidD_GetPreparsedData(HANDLE h, void **preparsed)
{
    if (!wl_busy() && wl_vhid_is_handle(h))
    {
        if (!preparsed)
            return FALSE;
        *preparsed = g_vhidPreparsed;
        return TRUE;
    }
    return oHidD_GetPreparsedData(h, preparsed);
}

static BOOLEAN(WINAPI *oHidD_FreePreparsedData)(void *);
static BOOLEAN WINAPI hHidD_FreePreparsedData(void *preparsed)
{
    if (preparsed == g_vhidPreparsed)
        return TRUE;
    return oHidD_FreePreparsedData(preparsed);
}

static LONG(WINAPI *oHidP_GetCaps)(void *, WlHidCaps *);
static LONG WINAPI hHidP_GetCaps(void *preparsed, WlHidCaps *caps)
{
    static volatile LONG logged;
    if (preparsed == g_vhidPreparsed)
    {
        if (!caps)
            return HIDP_STATUS_INVALID_PREPARSED_DATA;
        const WlVirtualHid *v = wl_vhid_device();
        memset(caps, 0, sizeof *caps);
        caps->Usage = v->usage;
        caps->UsagePage = v->usagePage;
        caps->InputReportByteLength = v->inputReportLength;
        caps->NumberLinkCollectionNodes = 1;
        return HIDP_STATUS_SUCCESS;
    }
    int nested = wl_busy();
    wl_busy_enter();
    LONG st = oHidP_GetCaps(preparsed, caps);
    wl_busy_leave();
    if (!nested && caps && !wl_is_system_caller(WL_CALLER) && InterlockedIncrement(&logged) <= 32)
        wl_log("HidP_GetCaps: usage page 0x%04X usage 0x%04X, reports in=%u out=%u feature=%u -> 0x%08lx", caps->UsagePage, caps->Usage,
               caps->InputReportByteLength, caps->OutputReportByteLength, caps->FeatureReportByteLength, st);
    return st;
}

static BOOLEAN hid_report(const char *what, BOOLEAN(WINAPI *orig)(HANDLE, PVOID, ULONG), HANDLE h, PVOID buf, ULONG len, int isRead)
{
    if (wl_vhid_is_handle(h))
    {
        // The emulated board has neither feature nor output reports
        static volatile LONG logged;
        if (InterlockedIncrement(&logged) <= 10)
            wl_log("%s on the emulated board (%lu bytes): not supported", what, len);
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }
    int nested = wl_busy();
    wl_busy_enter();
    BOOLEAN ok = orig(h, buf, len);
    wl_busy_leave();
    WlDevice d;
    if (!nested && wl_dev_count(h, WL_DEV_IO, &d))
    {
        char prefix[160];
        snprintf(prefix, sizeof prefix, "%s %s (%lu) -> %s%s", what, d.name, len, ok ? "ok" : "failed", ok || !isRead ? ":" : "");
        wl_log_hex(prefix, ok || !isRead ? buf : NULL, ok || !isRead ? len : 0);
    }
    return ok;
}

static BOOLEAN(WINAPI *oHidD_GetFeature)(HANDLE, PVOID, ULONG);
static BOOLEAN WINAPI hHidD_GetFeature(HANDLE h, PVOID buf, ULONG len)
{
    return hid_report("HidD_GetFeature", oHidD_GetFeature, h, buf, len, 1);
}

static BOOLEAN(WINAPI *oHidD_SetFeature)(HANDLE, PVOID, ULONG);
static BOOLEAN WINAPI hHidD_SetFeature(HANDLE h, PVOID buf, ULONG len)
{
    return hid_report("HidD_SetFeature", oHidD_SetFeature, h, buf, len, 0);
}

static BOOLEAN(WINAPI *oHidD_GetInputReport)(HANDLE, PVOID, ULONG);
static BOOLEAN WINAPI hHidD_GetInputReport(HANDLE h, PVOID buf, ULONG len)
{
    return hid_report("HidD_GetInputReport", oHidD_GetInputReport, h, buf, len, 1);
}

static BOOLEAN(WINAPI *oHidD_SetOutputReport)(HANDLE, PVOID, ULONG);
static BOOLEAN WINAPI hHidD_SetOutputReport(HANDLE h, PVOID buf, ULONG len)
{
    return hid_report("HidD_SetOutputReport", oHidD_SetOutputReport, h, buf, len, 0);
}

static WlHook g_hooks[] = {
    WL_HOOK(L"kernel32.dll", GetCommState),
    WL_HOOK(L"kernel32.dll", SetCommState),
    WL_HOOK(L"kernel32.dll", SetCommTimeouts),
    WL_HOOK(L"kernel32.dll", SetupComm),
    WL_HOOK(L"kernel32.dll", PurgeComm),
    WL_HOOK(L"kernel32.dll", SetCommMask),
    WL_HOOK(L"kernel32.dll", EscapeCommFunction),
    WL_HOOK(L"kernel32.dll", ClearCommError),
    WL_HOOK(L"kernel32.dll", GetCommModemStatus),
    WL_HOOK(L"kernel32.dll", WaitCommEvent),
    WL_HOOK(L"kernel32.dll", DeviceIoControl),
    WL_HOOK(L"setupapi.dll", SetupDiGetClassDevsA),
    WL_HOOK(L"setupapi.dll", SetupDiGetClassDevsW),
    WL_HOOK(L"setupapi.dll", SetupDiEnumDeviceInterfaces),
    WL_HOOK(L"setupapi.dll", SetupDiGetDeviceInterfaceDetailA),
    WL_HOOK(L"setupapi.dll", SetupDiGetDeviceInterfaceDetailW),
    WL_HOOK(L"hid.dll", HidD_GetAttributes),
    WL_HOOK(L"hid.dll", HidD_GetPreparsedData),
    WL_HOOK(L"hid.dll", HidD_FreePreparsedData),
    WL_HOOK(L"hid.dll", HidP_GetCaps),
    WL_HOOK(L"hid.dll", HidD_GetFeature),
    WL_HOOK(L"hid.dll", HidD_SetFeature),
    WL_HOOK(L"hid.dll", HidD_GetInputReport),
    WL_HOOK(L"hid.dll", HidD_SetOutputReport),
};

void wl_devices_hooks(void)
{
    wl_hooks_add(g_hooks, sizeof g_hooks / sizeof g_hooks[0]);
}
