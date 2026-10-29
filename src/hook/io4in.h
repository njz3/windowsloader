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

// A real Sega 837-15257 "IO4" I/O board on USB, read as one of the player's controllers (io4in.cpp):
// SDL's virtual joystick "SEGA IO4 837-15257", a JOY<n> of controls.ini as the others. Started by
// the controls' thread once SDL is up; the board may be plugged in or out at any time.
void wl_io4_start(void);
