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

// Ford Racing Full Blown (Sega, 2006), FordRacing\fordracing.exe, x86, Direct3D 9. The cabinet's
// shell (GameShell\GameShell.exe, "Game Shell - Windowed") starts it; the game runs without it
// (free play in ..\data\gamesettings.ini).
//
// - The I/O board is a Happ Controls UGCI, USB HID VID 0x078B PID 0x0010 (+0xde1f0, "Failed
//   InitHappAPI"): the game quits when it finds none. Its "JOYSTICK" interface gives 8-byte
//   reports (+0xddfa0): byte 0 the report ID, 1 the steering, 3 the accelerator, 5 the brake,
//   against [Steering Wheel] Left/Right, [Accelerator] and [Brake] Up/Down of
//   ..\data\calibration.ini (+0xa19f0), bytes 6 and 7 the switches (gathered between two reads;
//   a report whose byte 6 is 7 is skipped). The game turns it into a DirectInput joystick state
//   (+0xa1af0): byte 6 bit 0 is button 5, bit 1 button 6, bit 2 button 2, bit 3 button 4; seen in
//   the game (fr_race.py): bit 0 gear up, 1 gear down, 2 Start, 3 the view (the camera changes at
//   each press). Byte 7 is not read by the game. The shell reads byte 6 too, its switches 1 to 6
//   (+0x6236, table at 0x431628): bits 2, 3, 0, 1, then 4 and 5, which its service menu uses to
//   choose and enter (taken as Test and Service).
//
// - The service menu is the shell's (GameShell.exe, run under windowsloader with this module, its
//   child fordracing.exe refused). It starts, finds the emulated board and gets the CRC its
//   launcher would give it, then stops at "SECURITY FAILURE 4": it wants the cabinet's iButton
//   (a DS1991 key read through IBFS32.DLL, the TMEX drivers), not emulated.
//
// - The wheel motor is the parallel port, through inpout32.dll that the game imports statically
//   (the launcher points that import at windowsloader's DLL): +0x67990 takes a force f, writes
//   Out32(0x378, b) with n = trunc(16 * Strength * f) clamped to -15..15: b = 0 for none, 0x10 - n
//   for n > 0 (0x0F the weakest, 0x01 the strongest), n + 0x1F for n < 0 (0x1E to 0x10).
//   +0x67c80 starts it: Out32(0x37A, 0x10), Out32(0x378, 0).

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "display.h"
#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "ioports.h"
#include "log.h"
#include "vhid.h"

enum
{
    IN_STEERING,
    IN_ACCEL,
    IN_BRAKE,
    IN_BIT0,
    IN_COUNT = IN_BIT0 + 16
};

static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 255, 128},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 255, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 255, 0},
};

// The switches whose role is known, by bit; the others keep the name BitN and no binding
static const struct
{
    int bit;
    const char *name, *action;
} g_switches[] = {
    {0, "GearUp", "GearUp"},
    {1, "GearDown", "GearDown"},
    {2, "Start", "P1_Start"},
    {3, "ViewChange", "ViewChange"},
    {4, "Test", "Test"},
    {5, "Service", "Service"},
};
static char g_bitNames[16][8];

static void fill_input(unsigned char *r, int length)
{
    memset(r, 0, length);
    r[1] = (unsigned char)wl_input_get(&g_inputs[IN_STEERING]);
    r[3] = (unsigned char)wl_input_get(&g_inputs[IN_ACCEL]);
    r[5] = (unsigned char)wl_input_get(&g_inputs[IN_BRAKE]);
    unsigned bits = 0;
    for (int i = 0; i < 16; i++)
        if (wl_input_get(&g_inputs[IN_BIT0 + i]))
            bits |= 1u << i;
    r[6] = (unsigned char)bits;
    r[7] = (unsigned char)(bits >> 8);
}

static const WlVirtualHid g_board = {
    "\\\\?\\hid#vid_078b&pid_0010#7&2f0d6a4b&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}",
    0x078B,
    0x0010,
    0x0100,
    0x0001, // generic desktop
    0x0004, // joystick
    8,
    4,
    fill_input,
};

// Port 0x378 back to the force -1..1, > 0 pushing left: measured with fr_race.py, the wheel held
// right gets more forces to the left (bytes 0x01 to 0x0F), held left more to the right
#define FFB_PORT 0x378
#define FFB_SIGN 1.0

static void port_write(unsigned short port, unsigned short value)
{
    if (port != FFB_PORT)
        return;
    int b = value & 0x1F, n = 0;
    if (b >= 0x01 && b <= 0x0F)
        n = 0x10 - b;
    else if (b >= 0x10 && b <= 0x1E)
        n = b - 0x1F;
    wl_ffb_set(FFB_SIGN * n / 15.0);
}

// The render resolution of windowsloader.ini ([Display] WIDTH / HEIGHT) given to the game when
// it reads its own: [gamesettings] Resolution of ..\data\gamesettings.ini ("640x480")
static int ini_override(const char *file, const char *section, const char *key, char *value, size_t size)
{
    size_t n = strlen(file);
    if (n < 16 || _stricmp(file + n - 16, "gamesettings.ini") || _stricmp(section, "gamesettings"))
        return 0;
    if (!_stricmp(key, "Fullscreen") && g_wl.windowed)
    {
        snprintf(value, size, "off");
        return 1;
    }
    int w, h;
    wl_display_internal(&w, &h);
    if (!_stricmp(key, "Resolution") && w && h)
    {
        snprintf(value, size, "%dx%d", w, h);
        return 1;
    }
    return 0;
}

// The cabinet's launcher starts GameShell with /k=<a CRC of GameShell.exe> that the shell checks
// against its own (+0x6972: "SECURITY FAILURE 2" without it, 3 when it differs): a CRC-32 sent
// high bit first, polynomial 0x8E94A031, from 0x6F5A87D5, over the whole file (+0x2a7c, +0x1bfb)
static DWORD shell_crc(const wchar_t *path)
{
    DWORD table[256];
    for (DWORD i = 0; i < 256; i++)
    {
        DWORD c = i << 24;
        for (int k = 0; k < 8; k++)
            c = (c & 0x80000000) ? (c << 1) ^ 0x8E94A031 : c << 1;
        table[i] = c;
    }
    DWORD crc = 0x6F5A87D5;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    unsigned char buf[4096];
    DWORD rd;
    while (ReadFile(f, buf, sizeof buf, &rd, NULL) && rd)
        for (DWORD i = 0; i < rd; i++)
            crc = (crc << 8) ^ table[((crc >> 24) ^ buf[i]) & 0xFF];
    CloseHandle(f);
    return crc;
}

static char g_commandLine[1024];

static LPSTR(WINAPI *oGetCommandLineA)(void);
static LPSTR WINAPI hGetCommandLineA(void)
{
    return g_commandLine;
}

static WlHook g_shellHooks[] = {
    WL_HOOK(L"kernel32.dll", GetCommandLineA),
};

static void shell_command_line(const wchar_t *exePath)
{
    DWORD crc = shell_crc(exePath);
    snprintf(g_commandLine, sizeof g_commandLine, "%s /k=%08lx", GetCommandLineA(), crc);
    wl_log("  the shell's command line: %s", g_commandLine);
    wl_hooks_add(g_shellHooks, sizeof g_shellHooks / sizeof g_shellHooks[0]);
}

// The analogue ranges follow the cabinet's calibration
static void read_calibration(const wchar_t *gameDir)
{
    wchar_t ini[MAX_PATH];
    swprintf(ini, MAX_PATH, L"%s..\\data\\calibration.ini", gameDir);
    struct
    {
        int input;
        const wchar_t *section, *low, *high;
    } cal[] = {
        {IN_STEERING, L"Steering Wheel", L"Left", L"Right"},
        {IN_ACCEL, L"Accelerator", L"Up", L"Down"},
        {IN_BRAKE, L"Brake", L"Up", L"Down"},
    };
    for (int i = 0; i < 3; i++)
    {
        WlInput *in = &g_inputs[cal[i].input];
        in->rawLow = GetPrivateProfileIntW(cal[i].section, cal[i].low, in->rawLow, ini);
        in->rawHigh = GetPrivateProfileIntW(cal[i].section, cal[i].high, in->rawHigh, ini);
        in->rawRest = in->type == WL_INPUT_AXIS ? (in->rawLow + in->rawHigh + 1) / 2 : in->rawLow;
        wl_log("  %s: %d..%d, rest %d", in->name, in->rawLow, in->rawHigh, in->rawRest);
    }
}

int wl_game_fordracing(const wchar_t *exePath, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    wchar_t dir[MAX_PATH], game[MAX_PATH];
    wcsncpy_s(dir, MAX_PATH, exePath, base - exePath);
    // The shell is GameShell.exe beside the game's folder (its name is not Ford Racing's own)
    swprintf(game, MAX_PATH, L"%s..\\FordRacing\\fordracing.exe", dir);
    int shell = !_wcsicmp(base, L"GameShell.exe") && GetFileAttributesW(game) != INVALID_FILE_ATTRIBUTES;
    if (_wcsicmp(base, L"fordracing.exe") && !shell)
        return 0;

    *title = shell ? "Ford Racing Full Blown, GameShell" : "Ford Racing Full Blown";
    wl_log("game: %s", *title);

    for (int i = 0; i < 16; i++)
    {
        snprintf(g_bitNames[i], sizeof g_bitNames[i], "Bit%d", i);
        WlInput *in = &g_inputs[IN_BIT0 + i];
        in->name = g_bitNames[i];
        in->action = NULL;
        for (int k = 0; k < (int)(sizeof g_switches / sizeof g_switches[0]); k++)
            if (g_switches[k].bit == i)
            {
                in->name = g_switches[k].name;
                in->action = g_switches[k].action;
            }
        in->type = WL_INPUT_SWITCH;
        in->rawLow = 0;
        in->rawHigh = 1;
        in->rawRest = 0;
    }
    read_calibration(dir);
    if (shell)
        shell_command_line(exePath);
    wl_inputs_set_table(g_inputs, IN_COUNT);
    wl_vhid_register(&g_board);
    wl_ioports_set_writer(port_write);
    wl_files_set_ini_override(ini_override);
    wl_display_set_internal_supported(1);
    return 1;
}
