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

// Handles of the devices the game opened (COM ports, HID, \\.\ devices). Reads, writes and
// control calls on them are logged, each kind up to its own limit per handle.

enum WlDevCounter
{
    WL_DEV_IO,    // reads, writes, ioctls, RTS/DTR changes: up to g_wl.ioLimit
    WL_DEV_SETUP, // port settings (baud rate, timeouts, purge)
    WL_DEV_POLL,  // polling (ClearCommError, modem status)
    WL_DEV_EMPTY, // reads that returned nothing
    WL_DEV_COUNTERS
};

struct WlDevice
{
    char name[96]; // path given to CreateFile
    unsigned count[WL_DEV_COUNTERS];
};

// True for "COM1", "\\.\COM10", "\\?\hid#...", "\\.\PhysicalDrive0"... and not for "C:\x" or "\\?\C:\x"
int wl_dev_is_device_path(const char *path);

void wl_dev_track(HANDLE h, const char *path);

// Forgets h; returns 1 and its entry when it was followed. With tryOnly, gives up rather than
// wait for the lock (CloseHandle can be called while MinHook holds the other threads frozen).
int wl_dev_untrack(HANDLE h, WlDevice *out, int tryOnly);

// Copy of the entry for h with that counter incremented. Returns 0 when h is not a device we
// follow, or when the counter is past its limit (the first time, a line says so).
int wl_dev_count(HANDLE h, WlDevCounter counter, WlDevice *out);
