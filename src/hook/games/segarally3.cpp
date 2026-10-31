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

// Sega Rally 3 (Sega Europa-R, 2008), Rally\Rally.exe.
//
// Its I/O board is a USB HID device, VID 0x1043 PID 0x0000, found through SetupAPI
// (Rally.exe+0x17b2f0). A thread ("Input HID Thread", +0x17ba30) reads 15-byte input reports
// with ReadFile (+0x17b640):
//   byte 0      report ID
//   byte 1      steering, against [CalibrationData] SteeringLeft..SteeringRight of ShellData.ini
//   byte 3, 4   the pedals, against BrakeUp..BrakeDown and AccelUp..AccelDown
//   bytes 6-8   24 switches, bit 0 of byte 6 first (the game's buttons 18 to 41): bit 0 Start,
//               1 view change, 2 handbrake, 3 gear up, 4 gear down (seen in the game); 5 to 7
//               do nothing in a race, the others nothing on the title screen (not tried in a race)
// Nothing is written back to the board. The serial ports COM1 and COM2 (38400 8E1, +0x22ded0)
// are only opened with the motion base ([gamesettings] MotionBaseOK=1 of ShellData.ini).
//
// The wheel motor is an I/O port: the game loads ..\Shell\inpout32.dll (+0x2582a8) and writes
// one byte to port 0x1020 with Out32 (+0x1c866f): 0 for no force (|f| <= 1/15), else the low
// nibble (int)(15 - |f| * 14), 1 the strongest to 14 the weakest, and bit 4 set when f >= 0.
// windowsloader never loads inpout32 (ioports.cpp): the writes come here.

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
#include "lang.h"
#include "link.h"
#include "log.h"
#include "vhid.h"

enum
{
    IN_STEERING,
    IN_ACCEL,
    IN_BRAKE,
    IN_BIT0,
    IN_COUNT = IN_BIT0 + 24
};

static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 10, 245, 128},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 40, 210, 40},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 40, 210, 40},
};

// The switches whose role is known, by bit; the others keep the name BitN and no binding
static const struct
{
    int bit;
    const char *name, *action;
} g_switches[] = {
    {0, "Start", "P1_Start"},
    {1, "ViewChange", "ViewChange"},
    {2, "Handbrake", "Handbrake"},
    {3, "GearUp", "GearUp"},
    {4, "GearDown", "GearDown"},
};
static char g_bitNames[24][8];

static void fill_input(unsigned char *r, int length)
{
    r[0] = 0;
    r[1] = (unsigned char)wl_input_get(&g_inputs[IN_STEERING]);
    r[3] = (unsigned char)wl_input_get(&g_inputs[IN_BRAKE]);
    r[4] = (unsigned char)wl_input_get(&g_inputs[IN_ACCEL]);
    unsigned bits = 0;
    for (int i = 0; i < 24; i++)
        if (wl_input_get(&g_inputs[IN_BIT0 + i]))
            bits |= 1u << i;
    r[6] = (unsigned char)bits;
    r[7] = (unsigned char)(bits >> 8);
    r[8] = (unsigned char)(bits >> 16);
}

static const WlVirtualHid g_board = {
    "\\\\?\\hid#vid_1043&pid_0000#7&57a4c0de&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}",
    0x1043,
    0x0000,
    0x0100,
    0x0001, // generic desktop
    0x0004, // joystick
    15,
    4,
    fill_input,
};

// Port 0x1020 back to the force -1..1, > 0 pushing left. Bit 4 pushes the wheel to the right:
// measured with scripts/agent/sr3_ffb.py, the wheel held right gets bit 4 clear, the centring
// force pushing it back to the left.
#define FFB_PORT 0x1020
#define FFB_SIGN -1.0

static void port_write(unsigned short port, unsigned short value)
{
    if (port != FFB_PORT)
        return;
    int nibble = value & 0x0F;
    double f = nibble ? (15 - nibble) / 14.0 : 0;
    if (!(value & 0x10))
        f = -f;
    wl_ffb_set(FFB_SIGN * f);
}

// The render resolution of windowsloader.ini ([Display] WIDTH / HEIGHT) given to the game when
// it reads its own: [Graphics] HorizontalResolution / VerticalResolution of Rally\config.ini.
// Linked, the cabinet's number: [Network] CabinetID of ShellData.ini ("For Link play: must be on
// same LAN and change CabinetID to 1..4"), Enabled
static int ini_override(const char *file, const char *section, const char *key, char *value, size_t size)
{
    size_t n = strlen(file);
    if (wl_link_count() && n >= 13 && !_stricmp(file + n - 13, "ShellData.ini") && !_stricmp(section, "Network"))
    {
        if (!_stricmp(key, "CabinetID"))
            snprintf(value, size, "%d", wl_link_cabinet());
        else if (!_stricmp(key, "Enabled"))
            snprintf(value, size, "1");
        else
            return 0;
        return 1;
    }
    int w, h;
    wl_display_internal(&w, &h);
    if (!w || !h || n < 10 || _stricmp(file + n - 10, "config.ini") || _stricmp(section, "Graphics"))
        return 0;
    if (!_stricmp(key, "HorizontalResolution"))
        snprintf(value, size, "%d", w);
    else if (!_stricmp(key, "VerticalResolution"))
        snprintf(value, size, "%d", h);
    else
        return 0;
    return 1;
}

// The analogue ranges follow the cabinet's calibration, so the full travel of a controller
// covers what the game expects
static void read_calibration(const wchar_t *gameDir)
{
    wchar_t ini[MAX_PATH];
    swprintf(ini, MAX_PATH, L"%s..\\ShellData\\ShellData.ini", gameDir);
    struct
    {
        int input;
        const wchar_t *low, *high;
    } cal[] = {
        {IN_STEERING, L"SteeringLeft", L"SteeringRight"},
        {IN_ACCEL, L"AccelUp", L"AccelDown"},
        {IN_BRAKE, L"BrakeUp", L"BrakeDown"},
    };
    for (int i = 0; i < 3; i++)
    {
        WlInput *in = &g_inputs[cal[i].input];
        in->rawLow = GetPrivateProfileIntW(L"CalibrationData", cal[i].low, in->rawLow, ini);
        in->rawHigh = GetPrivateProfileIntW(L"CalibrationData", cal[i].high, in->rawHigh, ini);
        in->rawRest = in->type == WL_INPUT_AXIS ? (in->rawLow + in->rawHigh + 1) / 2 : in->rawLow;
        wl_log("  %s: %d..%d, rest %d", in->name, in->rawLow, in->rawHigh, in->rawRest);
    }
}

// The link between cabinets (link.h): 1 to 4 (Demonware's LAN play: its cabinets found by a
// broadcast, a session joined; GetAdaptersInfo for its address, Winsock 1 by number)
static const WlGameOption g_options[] = {
    {"CABINET", WL_N("Numéro de la borne (link)"),
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "
          "dans le fichier)."),
     WL_OPTION_INT, NULL, 0, 4, "0", 1, NULL},
};

static void sr3_settings(void)
{
    wl_link_settings(GetModuleHandleW(NULL));
}

int wl_game_segarally3(const wchar_t *exePath)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    if (_wcsicmp(base, L"Rally.exe"))
        return 0;

    wchar_t dir[MAX_PATH];
    wcsncpy_s(dir, MAX_PATH, exePath, base - exePath);
    wl_log("game: Sega Rally 3 (Europa-R)");
    wl_game_set_boards("Controls on USB HID board, Sega Europa-R I/O (VID 1043, PID 0000)",
                       "Motor board: Sega PC parallel-port driver (I/O port 0x1020: strength nibble, direction bit)");

    for (int i = 0; i < 24; i++)
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
    wl_inputs_set_table(g_inputs, IN_COUNT);
    wl_vhid_register(&g_board);
    wl_ioports_set_writer(port_write);
    wl_files_set_ini_override(ini_override);
    wl_display_set_internal_supported(1);
    wl_game_set_options(g_options, (int)(sizeof g_options / sizeof g_options[0]));
    wl_game_on_settings(sr3_settings);
    return 1;
}
