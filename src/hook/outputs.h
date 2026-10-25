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

#include <stddef.h>

// MAME-style network outputs (outputs.cpp), [Outputs] OUTPUTS_NETWORK: lamps and drive board for
// BackForceFeeder or MAMEHooker. Started once the game and the settings are known
void wl_outputs_init(void);
// "mame_stop = 1" sent to the clients, the server stopped: at the game's end (ExitProcess,
// TerminateProcess of itself). Once; harmless when the server never started
void wl_outputs_stop(void);

// A JVS general purpose output byte (lamps): byte 0 holds outputs 1 to 8 in bits 7 to 0
void wl_outputs_gpo(int byteIndex, unsigned char value);
// Bytes the game sends to a Sega drive board, in order
void wl_outputs_drive(const unsigned char *bytes, size_t count);
// The force of the other boards (wl_ffb_set's, -1..1, > 0 pushing left), sent as the Sega servo
// board's torque frames; nothing once a Sega drive board's own bytes were sent
void wl_outputs_force(double force);
