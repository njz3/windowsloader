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

// Konami's ACIO bus, as libacio.dll's functions answer it (Road Fighters 3D, Thrill Drive 4): the
// answers that are the same for every game. Each game's boards (their status, lamps, motor) and its
// tables of functions stay in its own file.

// The game's name at the start of the log's lines ("thrill drive 4"), set before the game calls
void wl_acio_name(const char *game);

// The requests answered at once, done (1), failed (0)
int __cdecl wl_acio_true(void);
int __cdecl wl_acio_false(void);

// The bus itself: started, its devices all found, version records, serial line, logs
int __cdecl wl_acio_begin(DWORD a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d, DWORD e);
int __cdecl wl_acio_is_active2(DWORD a, DWORD *status);
void __cdecl wl_acio_update(void);
int __cdecl wl_acio_get_version(unsigned char *out, DWORD device);
int __cdecl wl_acio_get_firmware_update_device_index(void);
void __cdecl wl_acio_get_rs232c_status(unsigned char *out);
DWORD __cdecl wl_acio_get_log(void *out, DWORD size);

// The I/O board's coin box: the coin mech's switch read at each status (a coin at each press), the
// coins counted until the game takes them
void wl_acio_coin_mech(int down);
int __cdecl wl_acio_current_coinstock(int slot, DWORD *stock);
int __cdecl wl_acio_consume_coinstock(int slot, DWORD coins);

// The security plugs: a plug's id as libacio writes it ("0140" and its 8 bytes in hex), the
// cabinet's system plug, the missing plug check passed (8)
int wl_acio_plug_id(char *out, const unsigned char id[8]);
int __cdecl wl_acio_get_systemid(char *out);
int __cdecl wl_acio_secplug_missing_check_isfinished(DWORD *status);

// The ICCA card reader: its workflow's states, the reader ready at once
enum
{
    WF_STEP = 0,
    WF_SLEEP = 1,
    WF_START = 2,
    WF_INIT = 3,
    WF_READY = 4,
    WF_GET_USERID = 5,
    WF_ACTIVE = 6,
    WF_EJECT = 7,
    WF_EJECT_CHECK = 8,
    WF_CLOSE_EJECT = 10,
    WF_CLOSE_END = 12,
};
int __cdecl wl_acio_icca_cardunit_init_isfinished(DWORD device, DWORD *status);
