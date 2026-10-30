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

// Daytona Championship USA (Sega Amusements, 2017) and its New Season Edition (2019),
// Sega\Daytona\Daytona.exe, x86, Direct3D 11. Built on Sega Racing Classic's code, which keeps
// Daytona USA's Model 2 inputs. Both editions do the same things at other addresses (addresses
// below are the 2017 edition's); windowsloader only plays the cabinet around the game:
//
// - The I/O board is a DirectInput joystick named "SEGA IO Controller" (+0x1d9bb0): the game
//   takes it by name, sets every axis to -1000..1000 and reads a DIJOYSTATE (+0x7cdf0). X is the
//   steering, against [CalibrationData] SteeringLeft / SteeringCentre / SteeringRight of
//   ShellData.ini (0..255 units); Rx the accelerator and Z the brake, uncalibrated (+0x1ddd80
//   puts them in Model 2 order: steering, accelerator, brake). Its buttons, by the game's table
//   (+0xd5c490): 0 Start, 1 2 6 7 the H shifter's gears 1 to 4, 3 gear up, 4 gear down, 8 to 11
//   the view buttons VR1 to VR4 (red, blue, yellow, green). vdinput.cpp plays it.
//
// - The wheel motor is on the parallel port, written by c:\sega\shell\InpWrapper.exe, which the
//   game starts with "-h=<its window> -p=<port>" (+0x128b70). InpWrapper sends the game's window
//   WM_COPYDATA type 3 with its own window handle (+0xdcd80 takes it), then the game sends it
//   WM_COPYDATA type 0x20 with one byte (+0x129570) that goes to the port: 0 for no force, else
//   the low nibble is the strength, 1 the strongest to 15 the weakest, bit 4 set when it pushes
//   the wheel to the right (the centring force +0x1292a0 pushes left a wheel turned right).
//   windowsloader is that InpWrapper: a hidden window of its own, nothing is started.
//
// - Coins, credits, the test menu and the cabinet's security are Sega\shell\Shell.exe's. The game
//   talks to it over TCP on 127.0.0.1:18888 (+0x1826a0), frames of 11 bytes "DYT", the command
//   and the payload length (both big endian), 4 zero bytes, then the payload. The game reports
//   what it does (GAME_MSG_*), the shell sets the credits (SHELL_MSG_SETCREDIT) and answers a
//   challenge sent every 10 s (GAME_MSG_SECURITY_CHALLENGE, 4 bytes): the game quits after five
//   left unanswered (+0x182a00). The New Season Edition also waits for the cabinet's SKU
//   (SHELL_MSG_SET_SKU) before leaving its "network discovery" screen (+0xdd18b; the first edition
//   ignores that message). windowsloader is that shell (segashell.cpp).
//
// The game's settings are left as they are; it only reads some of them differently: no
// exclusive full screen ([FullHD] ENABLE_FULL_SCREEN of GameData\config.ini) and a single
// cabinet, the first ([Network] NumCabinets 1 and CabinetID 1 of ShellData.ini; the links with
// other cabinets would go out on the local network, which system.cpp refuses anyway). The camera
// (the players' photos) is refused by system.cpp too.

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "log.h"
#include "segashell.h"
#include "vdinput.h"

// ------------------------------------------------------------------------------
// The I/O board
// ------------------------------------------------------------------------------

enum
{
    IN_STEERING,
    IN_ACCEL,
    IN_BRAKE,
    IN_START,
    IN_VIEW1,
    IN_VIEW2,
    IN_VIEW3,
    IN_VIEW4,
    IN_GEAR1,
    IN_GEAR2,
    IN_GEAR3,
    IN_GEAR4,
    IN_GEARUP,
    IN_GEARDOWN,
    IN_COIN,    // the shell's
    IN_SERVICE, // the shell's service credit
    IN_COUNT
};

static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 10, 245, 127},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 255, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 255, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"View1", "View1", WL_INPUT_SWITCH, 0, 1, 0},
    {"View2", "View2", WL_INPUT_SWITCH, 0, 1, 0},
    {"View3", "View3", WL_INPUT_SWITCH, 0, 1, 0},
    {"View4", "View4", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear1", "Gear1", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear2", "Gear2", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear3", "Gear3", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear4", "Gear4", WL_INPUT_SWITCH, 0, 1, 0},
    {"GearUp", "GearUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"GearDown", "GearDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "P1_Service", WL_INPUT_SWITCH, 0, 1, 0},
};

// The board's button of each input
static const struct
{
    int input, button;
} g_buttons[] = {
    {IN_START, 0}, {IN_GEAR1, 1}, {IN_GEAR2, 2},  {IN_GEARUP, 3},  {IN_GEARDOWN, 4}, {IN_GEAR3, 6},
    {IN_GEAR4, 7}, {IN_VIEW1, 8}, {IN_VIEW2, 9}, {IN_VIEW3, 10}, {IN_VIEW4, 11},
};

static unsigned short axis_of(int raw)
{
    if (raw < 0)
        raw = 0;
    if (raw > 255)
        raw = 255;
    return (unsigned short)(raw * 257);
}

// The game reads X 0..255 (in -1000..1000) against its calibration, and the pedals released at -1000
static void fill_board(unsigned short axes[WL_VDINPUT_AXES], unsigned char *buttons)
{
    axes[0] = axis_of(wl_input_get(&g_inputs[IN_STEERING]));
    axes[2] = axis_of(wl_input_get(&g_inputs[IN_BRAKE]));
    axes[3] = axis_of(wl_input_get(&g_inputs[IN_ACCEL]));
    for (int i = 0; i < (int)(sizeof g_buttons / sizeof g_buttons[0]); i++)
        buttons[g_buttons[i].button] = wl_input_get(&g_inputs[g_buttons[i].input]) != 0;
}

static const WlVirtualJoystick g_board = {
    "SEGA IO Controller",
    0x0CA3, // Sega's USB vendor ID; the game checks only the name
    0x0001,
    12,
    fill_board,
};

// The steering range follows the cabinet's calibration, so the full travel of a controller
// covers what the game expects
static void read_calibration(const wchar_t *gameDir)
{
    wchar_t ini[MAX_PATH];
    swprintf(ini, MAX_PATH, L"%s..\\ShellData\\ShellData.ini", gameDir);
    WlInput *s = &g_inputs[IN_STEERING];
    s->rawLow = GetPrivateProfileIntW(L"CalibrationData", L"SteeringLeft", 0, ini);
    s->rawHigh = GetPrivateProfileIntW(L"CalibrationData", L"SteeringRight", 255, ini);
    s->rawRest = GetPrivateProfileIntW(L"CalibrationData", L"SteeringCentre", 127, ini);
    wl_log("  %s: %d..%d, centre %d", s->name, s->rawLow, s->rawHigh, s->rawRest);
}

// ------------------------------------------------------------------------------
// InpWrapper.exe: the wheel motor
// ------------------------------------------------------------------------------

enum
{
    COPYDATA_WRAPPER_WINDOW = 3, // InpWrapper to the game: its window
    COPYDATA_PORT_BYTE = 0x20,   // the game to InpWrapper: one byte for the parallel port
};

static HWND g_gameWindow;
static unsigned g_port;

static void motor_byte(unsigned char b)
{
    static int last = -1, logged;
    int q = b & 0x0F;
    double f = q ? (15 - q) / 14.0 : 0;
    if (b & 0x10)
        f = -f; // to the right
    wl_ffb_set(f);
    if (b != last && logged < 40)
    {
        logged++;
        wl_log("motor: port 0x%x <- 0x%02x, force %+.2f", g_port, b, f);
    }
    last = b;
}

static LRESULT CALLBACK wrapper_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COPYDATA)
    {
        const COPYDATASTRUCT *cd = (const COPYDATASTRUCT *)lp;
        if (cd && cd->dwData == COPYDATA_PORT_BYTE && cd->cbData >= 1 && cd->lpData)
            motor_byte(*(const unsigned char *)cd->lpData);
        return 1; // done (2 would ask the game to send it again)
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static DWORD WINAPI wrapper_thread(LPVOID)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wrapper_proc;
    wc.hInstance = g_wlModule;
    wc.lpszClassName = "windowsloader InpWrapper";
    RegisterClassA(&wc);
    HWND w = CreateWindowExA(0, wc.lpszClassName, "InpWrapper", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, g_wlModule, NULL);
    if (!w)
    {
        wl_log("motor: no window for InpWrapper (error %lu), no force feedback", GetLastError());
        return 0;
    }
    // As InpWrapper does it: its window handle in 8 bytes, until the game takes it
    DWORD handle[2] = {(DWORD)(UINT_PTR)w, 0};
    COPYDATASTRUCT cd = {COPYDATA_WRAPPER_WINDOW, sizeof handle, handle};
    for (;;)
    {
        DWORD_PTR result = 0;
        if (SendMessageTimeoutA(g_gameWindow, WM_COPYDATA, 0, (LPARAM)&cd, SMTO_ABORTIFHUNG, 10000, &result) && result == 1)
            break;
        if (!IsWindow(g_gameWindow))
        {
            wl_log("motor: the game's window %p is gone, no force feedback", g_gameWindow);
            return 0;
        }
        Sleep(500);
    }
    wl_log("motor: the game sends its port writes to windowsloader's window %p", w);
    MSG m;
    while (GetMessageA(&m, NULL, 0, 0) > 0)
        DispatchMessageA(&m);
    return 0;
}

// The game starting c:\sega\shell\InpWrapper.exe "-h=<window> -p=<port>": windowsloader plays it
static int process_handler(const char *app, const char *cmd)
{
    size_t n = strlen(app);
    if (n < 14 || _stricmp(app + n - 14, "InpWrapper.exe"))
        return 0;
    const char *h = strstr(cmd, "-h="), *p = strstr(cmd, "-p=");
    if (!h || g_gameWindow)
        return 1;
    g_gameWindow = (HWND)(UINT_PTR)strtoul(h + 3, NULL, 16);
    g_port = p ? (unsigned)strtoul(p + 3, NULL, 16) : 0;
    wl_log("motor: InpWrapper for window %p, parallel port 0x%x", g_gameWindow, g_port);
    CloseHandle(CreateThread(NULL, 0, wrapper_thread, NULL, 0, NULL));
    return 1;
}

// ------------------------------------------------------------------------------
// Shell.exe: credits and the security challenge
// ------------------------------------------------------------------------------

enum
{
    GAME_MSG_KEEPALIVE = 0x00,
    GAME_MSG_START_PRESSED = 0x06,
    GAME_MSG_SECURITY_CHALLENGE = 0x2a,
    SHELL_MSG_SET_SKU = 0x2c,
    SHELL_MSG_SETCREDIT = 0x2d,
    SHELL_MSG_SECURITY_AUTHENTICATE = 0x2e,
    MSG_COUNT = 0x37
};

// The names the game itself logs its messages with (+0x11319c0)
static const char *const g_msgNames[MSG_COUNT] = {
    "GAME_MSG_KEEPALIVE", "GAME_MSG_LAUNCHED_OK", "GAME_MSG_ASSETS_LOADED", "GAME_MSG_ATTRACT_START", "GAME_MSG_ATTRACT_LOGO",
    "GAME_MSG_ATTRACT_VIDEO", "GAME_MSG_START_PRESSED", "GAME_MSG_MODE_ENTERED", "GAME_MSG_MODE_SELECTED", "GAME_MSG_WAITING_FOR_CHALLENGERS",
    "GAME_MSG_ENTRY_CLOSED", "GAME_MSG_PLAYERS_IN_GAME", "GAME_MSG_TRACK_SELECT_ENTERED", "GAME_MSG_TRACK_SELECT_CHANGED",
    "GAME_MSG_TRACK_SELECT_CHOSEN", "GAME_MSG_CAR_SELECT_ENTERED", "GAME_MSG_CAR_SELECT_CHANGED", "GAME_MSG_CAR_SELECT_CHOSEN", "0x12", "0x13",
    "GAME_MSG_TRANSMISSION_SELECT_CHOSEN", "GAME_MSG_TRACK_LOADING", "GAME_MSG_ROLLING_START", "GAME_MSG_COUNTDOWN", "GAME_MSG_CURRENT_POSITIONS",
    "GAME_MSG_WALL_IMPACT", "GAME_MSG_PLAYER_IMPACT", "GAME_MSG_OVERTAKE_STATUS", "GAME_MSG_PIT_STATUS", "GAME_MSG_TIME_EXTENDED",
    "GAME_MSG_TIME_OUT", "GAME_MSG_GEAR_SHIFT", "GAME_MSG_LAP_COMPLETED", "GAME_MSG_RACE_LEADER", "GAME_MSG_RACE_OVER", "GAME_MSG_FINAL_RESULTS",
    "GAME_MSG_GAME_OVER", "GAME_MSG_HIGHSCORE_ACHIEVED", "GAME_MSG_HIGHSCORE_ENTRY", "GAME_MSG_HIGHSCORE_VALUE", "GAME_MSG_INITIAL_CURRENT",
    "GAME_MSG_INITIAL_CONFIRMED", "GAME_MSG_SECURITY_CHALLENGE", "GAME_MSG_SHUTDOWN", "SHELL_MSG_SET_SKU", "SHELL_MSG_SETCREDIT",
    "SHELL_MSG_SECURITY_AUTHENTICATE", "SHELL_MSG_RESET", "SHELL_MSG_EXIT", "SHELL_MSG_PARTY_MODE_REQUEST", "SHELL_MSG_PARTY_MODE_START",
    "SHELL_MSG_PARTY_MODE_END", "GAME_MSG_PARTY_MODE_ACKNOWLEDGE", "GAME_MSG_CONTINUE_OFFERED", "GAME_MSG_CONTINUE_CHOSEN",
};

static WlSegaShell g_shell = {
    "DYT", g_msgNames, MSG_COUNT, GAME_MSG_SECURITY_CHALLENGE, SHELL_MSG_SECURITY_AUTHENTICATE, SHELL_MSG_SETCREDIT, SHELL_MSG_SET_SKU,
    GAME_MSG_START_PRESSED, -1, -1, 0, &g_inputs[IN_COIN], &g_inputs[IN_SERVICE],
};

// ------------------------------------------------------------------------------

static int ends_with(const char *s, const char *end)
{
    size_t n = strlen(s), m = strlen(end);
    return n >= m && !_stricmp(s + n - m, end);
}

static int ini_override(const char *file, const char *section, const char *key, char *value, size_t size)
{
    static const struct
    {
        const char *file, *section, *key, *value;
    } table[] = {
        {"config.ini", "FullHD", "ENABLE_FULL_SCREEN", "0"},
        {"ShellData.ini", "Network", "NumCabinets", "1"},
        // Alone, it is cabinet 1: a second cabinet's file (CabinetID = 2, the link's identity from
        // TeknoParrot) made it wait for cabinet 1 on "network discovery" (racecab2)
        {"ShellData.ini", "Network", "CabinetID", "1"},
    };
    for (int i = 0; i < (int)(sizeof table / sizeof table[0]); i++)
        if (ends_with(file, table[i].file) && !_stricmp(section, table[i].section) && !_stricmp(key, table[i].key))
        {
            snprintf(value, size, "%s", table[i].value);
            return 1;
        }
    return 0;
}

int wl_game_daytona(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    if (_wcsicmp(base, L"Daytona.exe"))
        return 0;

    wchar_t dir[MAX_PATH], ini[MAX_PATH];
    wcsncpy_s(dir, MAX_PATH, exePath, base - exePath);
    // The edition by the version of its Install.ini (171221 the first, 190306 the New Season Edition)
    swprintf(ini, MAX_PATH, L"%sInstall.ini", dir);
    int version = GetPrivateProfileIntW(L"Update", L"Version", 0, ini);
    *title = version >= 190000 ? "Daytona Championship USA - New Season Edition" : "Daytona Championship USA";
    // The cabinets' names of the two editions (AttractPlus, BackForceFeeder: Daytona3, Daytona3NSE)
    *name = version >= 190000 ? "daytona3nse" : "daytona3";
    wl_log("game: %s (Install.ini version %d)", *title, version);
    wl_game_set_boards("Controls on DirectInput board \"SEGA IO Controller\"",
                       "Motor board: Sega PC parallel-port driver (port 0x378 through InpWrapper.exe: strength nibble, direction bit)");

    swprintf(ini, MAX_PATH, L"%s..\\ShellData\\ShellData.ini", dir);
    g_shell.freeplay = GetPrivateProfileIntW(L"Credit", L"Freeplay", 0, ini) != 0;
    read_calibration(dir);
    wl_inputs_set_table(g_inputs, IN_COUNT);
    wl_vdinput_register(&g_board);
    wl_system_set_process_handler(process_handler);
    wl_files_set_ini_override(ini_override);
    // When an allocation fails the game writes all its memory blocks to GameData\debug.txt (it
    // opens "../GameData/\debug.txt"), some 70 MB each time, again and again once it is short of
    // memory
    wl_files_cap("\\debug.txt", 32ull << 20);
    wl_segashell_start(&g_shell);
    return 1;
}
