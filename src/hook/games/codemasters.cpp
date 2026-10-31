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

// Codemasters' games on Sega Europe cabinets, x86, beside the Sega shell in C:\Sega:
// - GRID (2008, Europa-R), Grid\GRID.exe, Direct3D 9;
// - DiRT Showdown (2015, "Showdown"), Showdown\Showdown.exe, Direct3D 11.
// What they share:
// - They want to live in C:\Sega\<game> with the shell beside it (C:\Sega\shell,
//   C:\Sega\ShellData: settings and calibration): C:\Sega is redirected to the copy's own folder
//   (the parent of the game's), so nothing is written at the root of this PC's disk.
// - The I/O board is the DirectInput "SEGA IO Controller" (their action maps): X steering,
//   Z brake, Rx accelerator, against [CalibrationData] of ShellData.ini; the buttons per game below.
// - The wheel's motor by Out32 of inpout32 (imported statically, pointed at windowsloader by the
//   launcher). GRID's is the Europa-R's port 0x1020, Showdown's the parallel port's 0x378, both
//   coded as Sega Rally 3's: the low nibble 15 - |f| * 14 (0x0F no force), bit 4 the direction.
// - They render at the desktop's size (GRID's hardware_settings_config.xml does not parse: a quote
//   is missing), 3072x1920 on a laptop's panel, too much for a 32-bit game (GRID ran out of memory
//   in races): EnumDisplaySettings tells them windowsloader's [Display] WIDTH x HEIGHT, at most
//   1920x1080 for AUTO.
// - Showdown's audio: its OpenAL takes the Rapture3D driver (rapture3d_oal.dll), which crashes
//   without its settings in the PC's registry (the cabinet's SetupAudioRegistry.bat writes them):
//   the driver is refused, OpenAL takes its generic DirectSound one (wrap_oal.dll).
// - Showdown talks to the Sega shell as Daytona does (segashell.cpp), frames tagged "DSW", an older
//   numbering (+0x194920 names them): it quits when its GAME_MSG_LAUNCHED_OK is not acknowledged.
// - ShellData.ini may say two linked cabinets: the game is told one (ShellData_single.ini's
//   NumCabinets); GRID waits for the other and stops on purpose (+0x1ab9e5).
// - Their link between cabinets (link.h, [Game.<name>] LINK): CabinetID of ShellData.ini from it.
//   GRID's is its "party": 1 hosts, 2 to 6 join (the lower hosts); its cabinets found by a
//   broadcast on UDP 12000 (its LAN discovery, the searcher finding its own session too), then
//   UDP and TCP on 7342; the address it advertises from gethostbyname(its PC's name).

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "display.h"
#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "lang.h"
#include "link.h"
#include "ioports.h"
#include "log.h"
#include "segashell.h"
#include "vdinput.h"

enum
{
    IN_STEERING,
    IN_ACCEL,
    IN_BRAKE,
    IN_START,
    IN_VIEW,
    IN_RESET,
    IN_GEARUP,
    IN_GEARDOWN,
    IN_REPLAY,
    IN_PAUSE,
    IN_LOOKBACK,
    IN_BOOST,
    IN_MUSICUP,
    IN_MUSICDOWN,
    IN_COUNT
};

static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 10, 245, 127},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 30, 200, 30},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 30, 200, 30},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"ViewChange", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"ResetCar", "ResetCar", WL_INPUT_SWITCH, 0, 1, 0},
    {"GearUp", "GearUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"GearDown", "GearDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"Replay", "Replay", WL_INPUT_SWITCH, 0, 1, 0},
    {"Pause", "Pause", WL_INPUT_SWITCH, 0, 1, 0},
    {"LookBack", "LookBack", WL_INPUT_SWITCH, 0, 1, 0},
    {"Boost", "Boost", WL_INPUT_SWITCH, 0, 1, 0},
    {"MusicUp", "MusicUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"MusicDown", "MusicDown", WL_INPUT_SWITCH, 0, 1, 0},
};

struct ButtonMap
{
    int input, button;
};

// ActionMap\actionMapSEGAController.xml: 0 Start (menus: select), 1 view, 2 reset the car,
// 3 / 4 gear up / down, 6 instant replay, 7 pause, 9 look back
static const ButtonMap g_gridButtons[] = {
    {IN_START, 0}, {IN_VIEW, 1}, {IN_RESET, 2}, {IN_GEARUP, 3}, {IN_GEARDOWN, 4}, {IN_REPLAY, 6}, {IN_PAUSE, 7}, {IN_LOOKBACK, 9}, {-1, 0},
};

// win\actionmap\sega_controller.xml: 0 Start, 1 view, 2 boost, 3 / 4 music up / down, 5 reset
static const ButtonMap g_showdownButtons[] = {
    {IN_START, 0}, {IN_VIEW, 1}, {IN_BOOST, 2}, {IN_MUSICUP, 3}, {IN_MUSICDOWN, 4}, {IN_RESET, 5}, {-1, 0},
};

static const ButtonMap *g_buttons;

static unsigned short axis_of(int raw)
{
    if (raw < 0)
        raw = 0;
    if (raw > 255)
        raw = 255;
    return (unsigned short)(raw * 257);
}

static void fill_board(unsigned short axes[WL_VDINPUT_AXES], unsigned char *buttons)
{
    axes[0] = axis_of(wl_input_get(&g_inputs[IN_STEERING]));
    axes[2] = axis_of(wl_input_get(&g_inputs[IN_BRAKE]));
    axes[3] = axis_of(wl_input_get(&g_inputs[IN_ACCEL]));
    for (const ButtonMap *b = g_buttons; b->input >= 0; b++)
        buttons[b->button] = wl_input_get(&g_inputs[b->input]) != 0;
}

static const WlVirtualJoystick g_board = {
    "SEGA IO Controller",
    0x0CA3,
    0x0001,
    12,
    fill_board,
};

// The motor's port back to the force -1..1, > 0 pushing left, as Sega Rally 3's
static unsigned short g_ffbPort; // GRID 0x1020 (the Europa-R's), Showdown 0x378 (the parallel port's)
#define FFB_SIGN -1.0

static void port_write(unsigned short port, unsigned short value)
{
    if (port != g_ffbPort)
        return;
    int nibble = value & 0x0F;
    double f = nibble ? (15 - nibble) / 14.0 : 0;
    if (!(value & 0x10))
        f = -f;
    wl_ffb_set(FFB_SIGN * f);
}

// [Network] of ShellData.ini: alone, cabinet 1 of 1; linked (link.h), LINK's count and CABINET
static int ini_override(const char *file, const char *section, const char *key, char *value, size_t size)
{
    size_t n = strlen(file);
    if (n < 13 || _stricmp(file + n - 13, "ShellData.ini") || _stricmp(section, "Network"))
        return 0;
    if (!_stricmp(key, "NumCabinets"))
        snprintf(value, size, "%d", wl_link_count() ? wl_link_count() : 1);
    else if (!_stricmp(key, "CabinetID") && wl_link_count())
        snprintf(value, size, "%d", wl_link_cabinet());
    else
        return 0;
    return 1;
}

// Showdown's link between cabinets (link.h): its "linked_group_mode", the sessions it finds on
// its network
static const WlGameOption g_showdownOptions[] = {
    {"CABINET", WL_N("Numéro de la borne (link)"),
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "
          "dans le fichier)."),
     WL_OPTION_INT, NULL, 0, 8, "0", 1, NULL},
};

static void showdown_settings(void)
{
    wl_link_settings(GetModuleHandleW(NULL));
}

// GRID's: cabinets 1 to 6
static const WlGameOption g_gridOptions[] = {
    {"CABINET", WL_N("Numéro de la borne (link)"),
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "
          "dans le fichier)."),
     WL_OPTION_INT, NULL, 0, 6, "0", 1, NULL},
};

// Showdown's shell commands (+0x194920)
static const char *const g_showdownMsgs[] = {
    "GAME_MSG_NOP", "GAME_MSG_ACK", "UNDEFINED", "GAME_MSG_LAUNCHED_OK", "GAME_MSG_ASSETS_LOADED", "GAME_MSG_CREDITUSED",
    "GAME_MSG_GAMESTART", "GAME_MSG_GAMEEND", "GAME_MSG_GAMECONTINUE", "GAME_MSG_SECURITY_CHALLENGE", "GAME_MSG_EVENT",
    "GAME_MSG_STATE", "GAME_MSG_GAMESHUTDOWN", "SHELL_MSG_SETCREDIT", "SHELL_MSG_SECURITY_AUTHENTICATE", "SHELL_MSG_EXIT",
    "SHELL_MSG_SET_SKU", "SHELL_MSG_KEYPAD_PRESSED", "GAME_MSG_BONUS_CODE", "SHELL_MSG_BONUS_CODE_VALID", "SHELL_MSG_BONUS_CODE_INVALID",
    "GAME_MSG_BIT_CODE", "SHELL_MSG_BONUS_CODE", "UNDEFINED", "SHELL_MSG_MOTIONBASE_ENABLED", "SHELL_MSG_QRCODE_READY",
    "SHELL_MSG_SPOOF_BUTTON", "GAME_MSG_KEYBOARD_SPOOF",
};

static WlSegaShell g_shell = {
    "DSW", g_showdownMsgs, (int)(sizeof g_showdownMsgs / sizeof g_showdownMsgs[0]), 0x09, 0x0e, 0x0d, 0x10, 0x05, 0x03, 0x01, 1, NULL, NULL, 1,
};

// The desktop's size the game renders at
static BOOL(WINAPI *oEnumDisplaySettingsA)(LPCSTR, DWORD, DEVMODEA *);
static BOOL WINAPI hEnumDisplaySettingsA(LPCSTR device, DWORD mode, DEVMODEA *dm)
{
    BOOL ok = oEnumDisplaySettingsA(device, mode, dm);
    if (!ok || wl_busy() || mode != ENUM_CURRENT_SETTINGS || !dm)
        return ok;
    int w, h;
    wl_display_internal(&w, &h);
    if (!w || !h)
    {
        w = dm->dmPelsWidth > 1920 ? 1920 : (int)dm->dmPelsWidth;
        h = dm->dmPelsHeight > 1080 ? 1080 : (int)dm->dmPelsHeight;
    }
    static int logged;
    if (!logged++)
        wl_log("display: the desktop given to the game as %dx%d (it is %lux%lu)", w, h, dm->dmPelsWidth, dm->dmPelsHeight);
    dm->dmPelsWidth = (DWORD)w;
    dm->dmPelsHeight = (DWORD)h;
    return ok;
}

static WlHook g_hooks[] = {
    WL_HOOK(L"user32.dll", EnumDisplaySettingsA),
};

// The ranges follow the cabinet's calibration ([CalibrationData] of ShellData.ini)
static void read_calibration(const char *sega)
{
    char ini[MAX_PATH];
    snprintf(ini, MAX_PATH, "%s\\ShellData\\ShellData.ini", sega);
    struct
    {
        int input;
        const char *low, *high;
    } cal[] = {
        {IN_STEERING, "SteeringLeft", "SteeringRight"},
        {IN_ACCEL, "AccelUp", "AccelDown"},
        {IN_BRAKE, "BrakeUp", "BrakeDown"},
    };
    for (int i = 0; i < 3; i++)
    {
        WlInput *in = &g_inputs[cal[i].input];
        in->rawLow = GetPrivateProfileIntA("CalibrationData", cal[i].low, in->rawLow, ini);
        in->rawHigh = GetPrivateProfileIntA("CalibrationData", cal[i].high, in->rawHigh, ini);
        in->rawRest = in->type == WL_INPUT_AXIS ? (in->rawLow + in->rawHigh + 1) / 2 : in->rawLow;
        wl_log("  %s: %d..%d, rest %d", in->name, in->rawLow, in->rawHigh, in->rawRest);
    }
}

int wl_game_codemasters(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    int grid = !_wcsicmp(base, L"GRID.exe"), showdown = !_wcsicmp(base, L"Showdown.exe");
    if (!grid && !showdown)
        return 0;
    *name = grid ? "grid" : "showdown";
    *title = grid ? "GRID" : "DiRT Showdown";
    g_buttons = grid ? g_gridButtons : g_showdownButtons;
    g_ffbPort = grid ? 0x1020 : 0x378;
    wl_log("game: %s", *title);
    wl_game_set_boards("Controls on DirectInput board \"SEGA IO Controller\"",
                       grid ? "Motor board: Sega PC parallel-port driver (I/O port 0x1020 through inpout32: strength nibble, direction bit)"
                            : "Motor board: Sega PC parallel-port driver (port 0x378 through inpout32: strength nibble, direction bit)");

    // The copy's C:\Sega: the parent of the game's folder
    char sega[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, exePath, (int)(base - exePath), sega, MAX_PATH, NULL, NULL);
    sega[base - exePath] = 0;
    char *end = sega + strlen(sega);
    if (end > sega && end[-1] == '\\')
        *--end = 0;
    char *parent = strrchr(sega, '\\');
    if (parent)
        *parent = 0;
    wl_files_redirect("C:\\Sega", sega);

    read_calibration(sega);
    wl_inputs_set_table(g_inputs, IN_COUNT);
    wl_vdinput_register(&g_board);
    wl_ioports_set_writer(port_write);
    wl_files_set_ini_override(ini_override);
    wl_display_set_internal_supported(1);
    wl_hooks_add(g_hooks, sizeof g_hooks / sizeof g_hooks[0]);

    if (showdown)
    {
        wl_system_refuse_dll("rapture3d_oal.dll");
        char ini[MAX_PATH];
        snprintf(ini, MAX_PATH, "%s\\ShellData\\ShellData.ini", sega);
        g_shell.freeplay = GetPrivateProfileIntA("Credit", "Freeplay", 1, ini) != 0;
        wl_segashell_start(&g_shell);
        wl_game_set_options(g_showdownOptions, (int)(sizeof g_showdownOptions / sizeof g_showdownOptions[0]));
        wl_game_on_settings(showdown_settings);
    }
    if (grid)
    {
        // From OpenParrot (Functions/Games/Europa-R/GRID.cpp, GPL v3), the same executable: once the
        // front end is up the game stops on purpose (a write to address 0) when a check fails on
        // this PC; the write is taken out
        static const unsigned char crash[] = {0x89, 0x00}, nop2[] = {0x90, 0x90};
        wl_patch_code((void *)0x5AB9E5, crash, nop2, 2, "the deliberate stop");
        wl_game_set_options(g_gridOptions, (int)(sizeof g_gridOptions / sizeof g_gridOptions[0]));
        wl_game_on_settings(showdown_settings);
    }
    return 1;
}
