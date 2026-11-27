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

// Konami's ACIO bus (acio.h), the answers its games share. Reimplemented from windowsarcadeloader's
// findings (aderumier, GPL v3, https://github.com/aderumier/windowsarcadeloader: crates/payload-konami/
// src: acio.rs, icca.rs), as the games' own files are.

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "acio.h"
#include "log.h"

#if defined(_M_IX86)

static const char *g_game = "acio";

void wl_acio_name(const char *game)
{
    g_game = game;
}

// The requests answered at once, done, and the boards' other outputs (coin counters and blockers,
// lamps' brightness, parallel outputs, watchdog): nothing to drive here
int __cdecl wl_acio_true(void)
{
    return 1;
}

int __cdecl wl_acio_false(void)
{
    return 0;
}

// ------------------------------------------------------------------------------
// The bus itself
// ------------------------------------------------------------------------------

// The game's version text, as much of it as can be read
static void read_text(const char *from, char *to, size_t size)
{
    size_t n = 0;
    __try
    {
        for (; from && n + 1 < size && from[n]; n++)
            to[n] = from[n];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    to[n] = 0;
}

// (port, version, out, flags); another game's libacio takes (port, device, version, out, flags):
// the version is the second argument unless it is a small number
int __cdecl wl_acio_begin(DWORD a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d, DWORD e)
{
    char version[33];
    read_text((const char *)(b < 0x10000 ? c : b), version, sizeof version);
    DWORD *out = (DWORD *)(b < 0x10000 ? d : c);
    wl_log("%s: ACIO bus started (game %s)", g_game, version);
    if (out)
        *out = 0;
    return 1;
}

// A bus whose devices were all found
int __cdecl wl_acio_is_active2(DWORD a, DWORD *status)
{
    if (status)
        *status = 6;
    return 1;
}

void __cdecl wl_acio_update(void)
{
}

// A device's version record (0x2C bytes): firmware 2.0.0 (byte 5), the rest zeros
int __cdecl wl_acio_get_version(unsigned char *out, DWORD device)
{
    if (out)
    {
        memset(out, 0, 0x2C);
        out[5] = 2;
    }
    wl_log("%s: ACIO device %lu present", g_game, device);
    return 1;
}

int __cdecl wl_acio_get_firmware_update_device_index(void)
{
    return 0xFF; // none waits for a new firmware
}

// The serial line's statistics (0x58 bytes): nothing lost
void __cdecl wl_acio_get_rs232c_status(unsigned char *out)
{
    if (out)
        memset(out, 0, 0x58);
}

DWORD __cdecl wl_acio_get_log(void *out, DWORD size)
{
    return 0;
}

// ------------------------------------------------------------------------------
// The I/O board's coin box
// ------------------------------------------------------------------------------

static volatile LONG g_coinStock; // coins in, not taken by the game yet
static int g_coinHeld;

void wl_acio_coin_mech(int down)
{
    if (down && !g_coinHeld)
        InterlockedIncrement(&g_coinStock);
    g_coinHeld = down;
}

int __cdecl wl_acio_current_coinstock(int slot, DWORD *stock)
{
    if (stock)
        *stock = (DWORD)g_coinStock;
    return 1;
}

int __cdecl wl_acio_consume_coinstock(int slot, DWORD coins)
{
    for (;;)
    {
        LONG was = g_coinStock, now = was > (LONG)coins ? was - (LONG)coins : 0;
        if (InterlockedCompareExchange(&g_coinStock, now, was) == was)
            return 1;
    }
}

// ------------------------------------------------------------------------------
// The security plugs
// ------------------------------------------------------------------------------

int wl_acio_plug_id(char *out, const unsigned char id[8])
{
    if (!out)
        return 0;
    strcpy(out, "0140");
    for (int i = 0; i < 8; i++)
        sprintf(out + 4 + i * 2, "%02X", id[i]);
    return 1;
}

int __cdecl wl_acio_get_systemid(char *out)
{
    static const unsigned char id[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    return wl_acio_plug_id(out, id);
}

int __cdecl wl_acio_secplug_missing_check_isfinished(DWORD *status)
{
    if (status)
        *status = 8;
    return 1;
}

// ------------------------------------------------------------------------------
// The ICCA card reader
// ------------------------------------------------------------------------------

int __cdecl wl_acio_icca_cardunit_init_isfinished(DWORD device, DWORD *status)
{
    if (status)
        *status = 4; // ready
    return 1;
}

#endif
