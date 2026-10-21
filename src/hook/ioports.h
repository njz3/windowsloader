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

// Emulated I/O ports. Some arcade games write their cabinet's outputs (lamps, wheel motor) to
// x86 I/O ports through inpout32.dll, which installs a kernel driver to reach the hardware. On a
// PC that is not the cabinet those ports can be anything, so the game never gets the real DLL:
// LoadLibrary of inpout32 gives it windowsloader's own module, whose Out32 / Inp32 /
// DlPortWritePortUchar... land here.

typedef void (*WlPortWriter)(unsigned short port, unsigned short value);

// The game module's handler for the writes (the force feedback for Sega Rally 3)
void wl_ioports_set_writer(WlPortWriter writer);

// For the LoadLibrary and GetProcAddress hooks (system.cpp)
int wl_ioports_is_driver_dll(const char *name);
HMODULE wl_ioports_module(void);
FARPROC wl_ioports_proc(HMODULE mod, LPCSTR name);
