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
// CreateFile on its path, HidD_GetAttributes, HidD_GetPreparsedData, HidP_GetCaps and ReadFile
// answer as the board would.

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
};

void wl_vhid_register(const WlVirtualHid *device);
int wl_vhid_active(void);

// For the hooks in files.cpp and devices.cpp
int wl_vhid_is_path(const char *path);
HANDLE wl_vhid_open(void);
int wl_vhid_is_handle(HANDLE h);
BOOL wl_vhid_read(HANDLE h, void *buffer, DWORD size, DWORD *read, OVERLAPPED *ov);
void wl_vhid_forget(HANDLE h);
const WlVirtualHid *wl_vhid_device(void);
