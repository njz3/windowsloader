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

#pragma once

#include <windows.h>

// An emulated USB HID I/O board. When a game enumerates the HID class through SetupAPI it finds
// this device only (the PC's own HID devices are hidden from it: they are not the cabinet's);
// CreateFile on its path, HidD_GetAttributes, HidD_GetPreparsedData, HidP_GetCaps, ReadFile and
// HidD_GetInputReport answer as the board would; a board with output reports gets WriteFile's
// and HidD_SetOutputReport's.

struct WlVirtualHid
{
    const char *path;         // device interface path handed to the game
    USHORT vendorId;
    USHORT productId;
    USHORT version;
    USHORT usagePage;         // top-level collection, as HidP_GetCaps reports it
    USHORT usage;
    USHORT inputReportLength; // including the report ID byte
    DWORD periodMs;           // a blocking ReadFile returns a report every periodMs
    // Fills one input report (inputReportLength bytes, report ID first)
    void (*fillInput)(unsigned char *report, int length);
    // Optional, NULL or 0 when the board has none: its strings (HidD_GetManufacturerString,
    // HidD_GetProductString); its output reports (outputReportLength bytes, report ID first)
    const wchar_t *manufacturer, *product;
    USHORT outputReportLength;
    void (*output)(const unsigned char *report, int length);
    // 1: an overlapped read answers ERROR_IO_PENDING and is done periodMs later, as a device's:
    // an ALLS's amdaemon reads until a read is left pending (it empties the board's queue)
    int pendingReads;
};

void wl_vhid_register(const WlVirtualHid *device);
int wl_vhid_active(void);

// For the hooks in files.cpp and devices.cpp
int wl_vhid_is_path(const char *path);
HANDLE wl_vhid_open(void);
int wl_vhid_is_handle(HANDLE h);
BOOL wl_vhid_read(HANDLE h, void *buffer, DWORD size, DWORD *read, OVERLAPPED *ov);
BOOL wl_vhid_write(HANDLE h, const void *buffer, DWORD size, DWORD *written, OVERLAPPED *ov);
BOOL wl_vhid_cancel(HANDLE h, OVERLAPPED *ov);
void wl_vhid_forget(HANDLE h);
const WlVirtualHid *wl_vhid_device(void);
