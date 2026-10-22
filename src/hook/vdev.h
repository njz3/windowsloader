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

// Emulated devices other than the HID board: the drivers of an arcade platform ("\\.\mxsram" on a
// Sega RingEdge) and serial ports (a JVS bus on "COM4"). CreateFile on the name gives the game a
// handle of ours (a real event, so that CloseHandle and waits stay valid); ReadFile, WriteFile,
// SetFilePointer, DeviceIoControl and the serial port calls on it go to the device's functions.
// Overlapped calls complete at once.

struct WlVirtualDevice
{
    const char *name; // as the game gives it to CreateFile; "COM4" also matches "\\.\COM4"
    // Each may be NULL: the call then fails with ERROR_INVALID_FUNCTION. offset is the handle's
    // file position (or the OVERLAPPED's offset), moved by the bytes read or written.
    BOOL (*ioctl)(DWORD code, const void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned);
    BOOL (*read)(void *buffer, DWORD size, DWORD *read, LONGLONG offset);
    BOOL (*write)(const void *buffer, DWORD size, DWORD *written, LONGLONG offset);
    // Serial ports (NULL for the others): the modem lines (MS_CTS_ON...), the bytes waiting
    DWORD (*modemStatus)(void);
    DWORD (*waiting)(void);
};

void wl_vdev_register(const WlVirtualDevice *device);
// A device the game finds by its interface class (SetupDi, devices.cpp) rather than by its name
void wl_vdev_interface(const GUID *cls, const char *path);

// For the hooks in files.cpp and devices.cpp
const WlVirtualDevice *wl_vdev_by_path(const char *path);
HANDLE wl_vdev_open(const WlVirtualDevice *device);
const WlVirtualDevice *wl_vdev_of(HANDLE h); // NULL when h is not one of ours
BOOL wl_vdev_read(HANDLE h, void *buffer, DWORD size, DWORD *read, OVERLAPPED *ov);
BOOL wl_vdev_write(HANDLE h, const void *buffer, DWORD size, DWORD *written, OVERLAPPED *ov);
BOOL wl_vdev_ioctl(HANDLE h, DWORD code, const void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned, OVERLAPPED *ov);
void wl_vdev_forget(HANDLE h);
void wl_vdev_hooks(void); // SetFilePointer(Ex), GetCommTimeouts
