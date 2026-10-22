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

// Sega's drive board (wheel motor) on the emulated serial port named port ("COM1")
void wl_driveboard_register(const char *port);

// K.O. Drive's and Sonic's power-on tests turn the wheel each way with the motor and stop on an
// error (K.O. Drive 1001; Sonic looks for the board on the next port) unless the wheel went. Once
// asked for, from the board's reset to the gain set at the test's end, the wheel is the one the
// test expects: free, turned by the torque. wl_driveboard_free_wheel then returns 1 with its
// steering (0-255, 0 the left).
void wl_driveboard_free_wheel_test(void);
int wl_driveboard_free_wheel(int *steering);
