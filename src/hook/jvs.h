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

// A JVS I/O board on an emulated serial port or the RingWide's \\.\mxjvs driver (vdev.cpp): Sega's
// JVS bus, packets E0 <node> <length> <data...> <sum> with D0 as escape. The board answers the host's commands
// (reset, address, identity, features, switches, coins, analog channels, outputs) from the state
// the game module gives.

struct WlJvsState
{
    unsigned char system;        // bit 7 Test
    unsigned short player[2];    // per player, the two switch bytes: byte 0 in bits 15-8
    unsigned char player3[2];    // per player, a third switch byte (Battle Gear 4 reads 3)
    unsigned short coins[2];     // coin counters
    unsigned short analog[10];   // 16-bit values, the significant bits on top (Chase H.Q. 2 reads 10)
    unsigned char misc[4];       // general purpose inputs, command 0x26 (Battle Gear 4 Pro's second board)
};

struct WlJvsBoard
{
    const char *port; // "COM4", "\\\\.\\mxjvs"
    const char *id;   // "SEGA ENTERPRISES,LTD.;I/O BD JVS;837-14572;Ver1.00;2005/10"
    int players, switchBytes, coinSlots, analogChannels, analogBits, outputs;
    void (*read)(WlJvsState *state);
    void (*output)(const unsigned char *data, int length); // 0x32 general purpose outputs (lamps), may be NULL
    int ctsWhenAddressed; // the sense line as Taito's Type X reads it: CTS off until the board has its address
    void (*analogOutput)(const unsigned short *values, int channels); // 0x33 (Chase H.Q. 2's motor), may be NULL
    // The manufacturer's commands (0x60..0x7F) the board knows, before windowsloader's: given the
    // request from the command on (n bytes left), it adds its reports to r and returns the bytes
    // the command takes, 0 when it is not one of its own. May be NULL.
    int (*vendor)(const unsigned char *p, int n, unsigned char *r, int *k);
};

void wl_jvs_register(const WlJvsBoard *board);
void wl_jvs_add_port(const char *port); // the same board under a second name
// A second board on the same bus (its port not used): node 2 once the host gives the addresses, the
// sense line set when both have one (Battle Gear 4 Pro's)
void wl_jvs_add_node(const WlJvsBoard *board);
int wl_jvs_is_port(const char *name); // one of the JVS board's ports ("COM2")
