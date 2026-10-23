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

// Namco System ES3: Mario Kart Arcade GP DX (v1.18), MK_AGP3_FINAL.exe (x86, Direct3D 9Ex,
// relocated: the offsets below are from its base). Being worked on:
// - ShareK, the cabinet's content distribution, is reached through a .NET COM class of ipccl.dll
//   that the cabinet registers (RegAsm, project.ps1): windowsloader leaves the registry alone, the
//   class is missing and the game's handler of that failure reads a null pointer: the ShareK
//   client is not started (+0x7e80d, the test of its flag made a jump);
// - the game takes the first DirectInput game controller as its wheel (vJoy on the test PC): a
//   wheel of windowsloader's is the only one listed.

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "jvs.h"
#include "log.h"
#include "vdev.h"
#include "vdinput.h"

enum
{
    IN_STEERING,
    IN_GAS,
    IN_BRAKE,
    IN_BUTTONS,
    IN_COUNT = IN_BUTTONS + 6
};

static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 65535, 32768},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 65535, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 65535, 0},
    {"Item", "Action", WL_INPUT_SWITCH, 0, 1, 0},
    {"Mario", "Nitro", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "P1_Service", WL_INPUT_SWITCH, 0, 1, 0},
};

static void fill_wheel(unsigned short axes[WL_VDINPUT_AXES], unsigned char *buttons)
{
    axes[0] = (unsigned short)wl_input_get(&g_inputs[IN_STEERING]);
    axes[1] = (unsigned short)wl_input_get(&g_inputs[IN_GAS]);
    axes[2] = (unsigned short)wl_input_get(&g_inputs[IN_BRAKE]);
    for (int i = 0; i < IN_COUNT - IN_BUTTONS; i++)
        buttons[i] = (unsigned char)(wl_input_get(&g_inputs[IN_BUTTONS + i]) != 0);
}

static const WlVirtualJoystick g_wheel = {"Namco Steering Wheel", 0x0B9A, 0x0001, 8, fill_wheel};

// The cabinet's JVS board on COM3 at 115200 bauds (the game's JVS library, +0x8ac1b0, its host
// "NBGI.;WinArc;Ver2.01;JPN"; the sense line as Taito's: CTS off until every board has its
// address, +0x8ac636), Namco's NA-JV. First guess of the layout, the usual JVS one: Start 0x8000,
// Service 0x4000, buttons from 0x0200 down; steering, accelerator, brake on analog channels 0, 1, 2.
static void jvs_state(WlJvsState *s)
{
    static int coinWas;
    static unsigned short coins;
    int coin = wl_input_get(&g_inputs[IN_BUTTONS + 3]);
    if (coin && !coinWas)
        coins++;
    coinWas = coin;
    s->system = wl_input_get(&g_inputs[IN_BUTTONS + 4]) ? 0x80 : 0;
    if (wl_input_get(&g_inputs[IN_BUTTONS + 2]))
        s->player[0] |= 0x8000;
    if (wl_input_get(&g_inputs[IN_BUTTONS + 5]))
        s->player[0] |= 0x4000;
    if (wl_input_get(&g_inputs[IN_BUTTONS + 0]))
        s->player[0] |= 0x0200;
    if (wl_input_get(&g_inputs[IN_BUTTONS + 1]))
        s->player[0] |= 0x0100;
    s->coins[0] = coins;
    s->analog[0] = (unsigned short)wl_input_get(&g_inputs[IN_STEERING]);
    s->analog[1] = (unsigned short)wl_input_get(&g_inputs[IN_GAS]);
    s->analog[2] = (unsigned short)wl_input_get(&g_inputs[IN_BRAKE]);
}

static void jvs_output(const unsigned char *data, int length)
{
    static unsigned char was[8];
    if (length > 8)
        length = 8;
    if (memcmp(was, data, length))
    {
        static int logged;
        if (logged++ < 40)
            wl_log("namco: JVS outputs %02x %02x %02x %02x", length > 0 ? data[0] : 0, length > 1 ? data[1] : 0, length > 2 ? data[2] : 0, length > 3 ? data[3] : 0);
        memcpy(was, data, length);
    }
}

// Namco's own JVS commands, as the game sends them after the board's identity (each answered
// "done", report 01, no data):
// - 0x70, a second byte telling which: 0x18 "PL" then 0x02 and 2 bytes, or 0x80 and 1 byte (built
//   at +0x8af7c1); 0x15 and 0x16, 2 bytes;
// - 0x78, 0x79, 0x7A, 14 bytes each (12-bit values in threes: the lamps, it seems), 0x7F alone.
static int namco_vendor(const unsigned char *p, int n, unsigned char *r, int *k)
{
    int used = 0;
    if (p[0] == 0x70 && n >= 2)
    {
        if (p[1] == 0x18 && n >= 5)
            used = p[4] == 0x02 ? 7 : p[4] == 0x80 ? 6 : 0;
        else if (p[1] == 0x15 || p[1] == 0x16)
            used = 4;
    }
    else if (p[0] >= 0x78 && p[0] <= 0x7A)
        used = 15;
    else if (p[0] == 0x7F)
        used = 1;
    if (!used || used > n)
    {
        static int logged;
        if (logged++ < 20)
            wl_log_hex("namco: JVS command not known", p, n);
        return 0;
    }
    static unsigned char seen[256];
    unsigned char id = p[0] == 0x70 ? p[1] : p[0];
    if (!seen[id]++)
        wl_log_hex("namco: JVS command", p, used);
    r[(*k)++] = 0x01;
    return used;
}

static WlJvsBoard g_jvs = {"COM3", "namco ltd.;NA-JV;Ver6.01;JPN,Multipurpose.", 1, 2, 2, 8, 16, 16, jvs_state, jvs_output, 1, NULL, namco_vendor};

// The steering motor's board on COM1 at 19200 bauds (class clWaJvIo's Pimpl, its thread +0x5f57d0).
// The board speaks in 3 characters, read 3 at a time (+0x5f5d00): "C01" its motor ready (the game
// builds its commands only then, +0x5f64e5), "C06" stopped, "E00".."E20" its errors. Once the game
// is up (+0x602ec4) it sends every 8 ms 10 bytes (+0x5f5c80, built at +0x5f6630): FF FF, a target
// position on 14 bits (low byte, then 6 bits; 0x2000 the middle), a spring's stiffness 0..127, 0,
// a damper 0..127, 0, a vibration -128..127, 0. Played here as a spring towards the target plus
// the vibration (the sign to check at the wheel).
static CRITICAL_SECTION g_motorLock;
static int g_motorReady; // "C01" given

static BOOL com1_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    const unsigned char *b = (const unsigned char *)buffer;
    *written = size;
    if (size < 10 || b[0] != 0xFF || b[1] != 0xFF)
    {
        static int logged;
        if (logged++ < 20)
            wl_log_hex("namco: motor board, unknown command", buffer, size);
        return TRUE;
    }
    int target = b[2] | (b[3] & 0x3F) << 8;
    int stiffness = b[4], damper = b[6], vibration = (signed char)b[8];
    double wheel = (wl_input_get(&g_inputs[IN_STEERING]) - 32768) / 32768.0;
    double force = stiffness / 127.0 * (wheel - (target - 0x2000) / 8192.0) + vibration / 128.0;
    wl_ffb_set(force < -1 ? -1 : force > 1 ? 1 : force);
    static int last[4] = {-1, -1, -1, -1}, logged;
    if ((target != last[0] || stiffness != last[1] || damper != last[2] || vibration != last[3]) && logged++ < 200)
        wl_log("namco: motor target %04x stiffness %d damper %d vibration %d (wheel %.2f)", target, stiffness, damper, vibration, wheel);
    last[0] = target, last[1] = stiffness, last[2] = damper, last[3] = vibration;
    return TRUE;
}

static BOOL com1_read(void *buffer, DWORD size, DWORD *read, LONGLONG offset)
{
    *read = 0;
    EnterCriticalSection(&g_motorLock);
    if (!g_motorReady && size >= 3)
    {
        memcpy(buffer, "C01", 3);
        *read = 3;
        g_motorReady = 1;
        wl_log("namco: motor board ready (C01)");
    }
    LeaveCriticalSection(&g_motorLock);
    return TRUE;
}

static DWORD com1_modem(void)
{
    return MS_CTS_ON | MS_DSR_ON;
}

static DWORD com1_waiting(void)
{
    return g_motorReady ? 0 : 3;
}

static WlVirtualDevice g_com1 = {"COM1", NULL, com1_read, com1_write, com1_modem, com1_waiting};

static int exists(const wchar_t *dir, const wchar_t *name)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s%s", dir, name);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

int wl_game_namco(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    wchar_t dir[MAX_PATH];
    wcsncpy_s(dir, MAX_PATH, exePath, base - exePath);
    if (_wcsicmp(base, L"MK_AGP3_FINAL.exe") || !exists(dir, L"bngrw.dll"))
        return 0;
    *name = "mkdx";
    *title = "Mario Kart Arcade GP DX";
    wl_log("game: %s (Namco System ES3)", *title);
    wl_inputs_set_table(g_inputs, IN_COUNT);
    wl_vdinput_register(&g_wheel);
    wl_jvs_register(&g_jvs);
    InitializeCriticalSection(&g_motorLock);
    wl_vdev_register(&g_com1);
    wl_vdev_label("COM1", "Carte du moteur du volant");

    // Its drives F:\ and G:\ (system, bookkeeping, dongle data, test mode settings) are the copy's
    // F* and G* folders and files: F:\system\x is Fsystem\x, G:\boot_limit_N.bin Gboot_limit_N.bin
    char folder[MAX_PATH], to[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, dir, -1, folder, sizeof folder, NULL, NULL);
    snprintf(to, sizeof to, "%sF", folder);
    wl_files_redirect_prefix("F:\\", to);
    snprintf(to, sizeof to, "%sG", folder);
    wl_files_redirect_prefix("G:\\", to);

    // ShareK's client not started: je to the function's end made a jmp (nop; jmp rel32). The exe
    // moves (ASLR): addresses from its base
    unsigned char *exe = (unsigned char *)GetModuleHandleW(NULL);
    static const unsigned char je[2] = {0x0F, 0x84}, jmp[2] = {0x90, 0xE9};
    wl_patch_code(exe + 0x7E80D, je, jmp, 2, "ShareK's client not started (its COM class is not registered)");
    return 1;
}
