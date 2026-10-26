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

// What the game can send, for the menu's "Sorties et réseau" page. The boards declare theirs as
// they are registered; a game whose motor is another board (wl_game_motor) sends the servo board's
// frames made from its force
void wl_outputs_declare_lamps(int outputs); // the JVS board's general purpose outputs (16: lamp0..lamp15)
void wl_outputs_declare_drive(void);        // the game's own bytes to a Sega drive board

// The lamps of another board, sent as a JVS board's (RawLamps..., lamp<n>): declared with the
// board's name ("la carte Trio Tech (SetDigitalOut)") and the lamps' own, or NULL; bit n of bits
// is lamp n. The names are for the menu (a JVS board's lamps may be named too: board NULL)
void wl_outputs_name_lamps(const char *board, const char *const *names, int count);
void wl_outputs_lamps(unsigned bits, int count);

// A value of the game's own that is not a lamp's bit (a light's brightness, a colour), sent as
// "name = value\r" when it changes: declared once (its name, MAME's way, its type and what it
// is, for the menu), then set by its number; -1 when the list is full
int wl_outputs_declare_value(const char *name, const char *type, const char *about);
void wl_outputs_value(int index, int value);

struct WlOutputInfo
{
    char name[32];
    const char *type;
    char about[128];  // in French, for the menu
    int known, value; // the last value, once the game gave one
    const char *text; // a text instead of a value (mame_start's game), or NULL
};

// The values the game can send, with their last ones (kept without OUTPUTS_NETWORK too); their count
int wl_outputs_list(WlOutputInfo *out, int max);
// 1 when the server listens: its TCP port, the UDP port of the announce (0 for none), the clients
int wl_outputs_server(int *tcpPort, int *udpPort, int *clients);
