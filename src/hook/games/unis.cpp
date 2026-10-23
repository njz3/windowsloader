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

// UNIS PC: Crazy Ride (CrazyTaxiGame-Win64-Shipping.exe, x64, Unreal Engine 4). The cabinet's
// hardware is reached through Plugins\UsbPluginsDll_R.dll, whose BIND_* functions the game asks
// one by one by GetProcAddress (its own import, +0x3abe90): the I/O board on a serial port (the
// board's last frame, 17 bytes, BIND_GetIoData), a wheel of its own (angle, motor), coins,
// lamps, a protection key ("SSNK"), and some that act on the PC itself (its clock, a restart,
// deleting a folder, the sound volume, a ping). windowsloader gives the game its own functions
// for those; the others (ini files, text conversions, the DLL's own state) stay the DLL's.
//
// - The key: the game calls BIND_InitDongle, then BIND_IsDongleVerify with 8 fixed bytes
//   (+0x1aa600; the DLL has the key compute on them and compares) and keeps only the answer:
//   present and verified, as linuxloader answers its games' keys (their library's functions).
// - The I/O data (+0x1b3b90): buttons in bytes 2 and 3, two values 0..255 in bytes 6 and 7.
// - The wheel's angle: a raw value the game places between the three of its calibration
//   (left, middle, right, its settings); 0..4095 here.

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "log.h"

enum
{
    IN_STEERING,
    IN_GAS,
    IN_BRAKE,
    IN_COIN,
    IN_PROBE, // PROBE: bytes 2 and 3 of the I/O data, bit by bit, until their meaning is known
    IN_COUNT = IN_PROBE + 16
};

static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 4095, 2048},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 255, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 255, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"A0", "A0", WL_INPUT_SWITCH, 0, 1, 0}, {"A1", "A1", WL_INPUT_SWITCH, 0, 1, 0}, {"A2", "A2", WL_INPUT_SWITCH, 0, 1, 0}, {"A3", "A3", WL_INPUT_SWITCH, 0, 1, 0},
    {"A4", "A4", WL_INPUT_SWITCH, 0, 1, 0}, {"A5", "A5", WL_INPUT_SWITCH, 0, 1, 0}, {"A6", "A6", WL_INPUT_SWITCH, 0, 1, 0}, {"A7", "A7", WL_INPUT_SWITCH, 0, 1, 0},
    {"B0", "B0", WL_INPUT_SWITCH, 0, 1, 0}, {"B1", "B1", WL_INPUT_SWITCH, 0, 1, 0}, {"B2", "B2", WL_INPUT_SWITCH, 0, 1, 0}, {"B3", "B3", WL_INPUT_SWITCH, 0, 1, 0},
    {"B4", "B4", WL_INPUT_SWITCH, 0, 1, 0}, {"B5", "B5", WL_INPUT_SWITCH, 0, 1, 0}, {"B6", "B6", WL_INPUT_SWITCH, 0, 1, 0}, {"B7", "B7", WL_INPUT_SWITCH, 0, 1, 0},
};

static volatile LONG g_coins; // coins the game has not taken yet
static int g_coinWas;

// The first call of each function, logged with its caller
static void first_call(const char *name)
{
    static const char *seen[64];
    static int count;
    for (int i = 0; i < count; i++)
        if (seen[i] == name)
            return;
    if (count < 64)
        seen[count++] = name;
    wl_log("unis: %s called by the game", name);
}

// ------------------------------------------------------------------------------
// The protection key: present and verified
// ------------------------------------------------------------------------------

static bool BIND_InitDongle(void)
{
    first_call("BIND_InitDongle");
    return true;
}

static bool BIND_IsDongleExist(void)
{
    first_call("BIND_IsDongleExist");
    return true;
}

static bool BIND_IsDongleVerify(const unsigned char *key)
{
    first_call("BIND_IsDongleVerify");
    return true;
}

static bool BIND_GetDongleVerifyLic(unsigned long long *licence)
{
    first_call("BIND_GetDongleVerifyLic");
    if (licence)
        *licence = 0x57696E6C6F616472ull; // made up: the game does not ask it
    return true;
}

static bool BIND_UnInitDongle(void)
{
    first_call("BIND_UnInitDongle");
    return true;
}

// ------------------------------------------------------------------------------
// The I/O board and the coins
// ------------------------------------------------------------------------------

static bool BIND_InitUnisIo(void)
{
    first_call("BIND_InitUnisIo");
    return true;
}

static bool BIND_IsIoWorking(void)
{
    return true;
}

static void count_coins(void)
{
    int coin = wl_input_get(&g_inputs[IN_COIN]);
    if (coin && !g_coinWas)
        InterlockedIncrement(&g_coins);
    g_coinWas = coin;
}

static bool BIND_GetIoData(unsigned char *data)
{
    first_call("BIND_GetIoData");
    if (!data)
        return false;
    memset(data, 0, 17);
    for (int i = 0; i < 16; i++)
        if (wl_input_get(&g_inputs[IN_PROBE + i]))
            data[2 + i / 8] |= (unsigned char)(1 << (i % 8));
    data[6] = (unsigned char)wl_input_get(&g_inputs[IN_GAS]);
    data[7] = (unsigned char)wl_input_get(&g_inputs[IN_BRAKE]);
    count_coins();
    return true;
}

static int BIND_GetInternalCoins(void)
{
    count_coins();
    return (int)g_coins;
}

static bool BIND_DecCoins(int count)
{
    first_call("BIND_DecCoins");
    InterlockedExchangeAdd(&g_coins, -count);
    return true;
}

static bool BIND_AddKeyboardCoins(int count)
{
    first_call("BIND_AddKeyboardCoins");
    InterlockedExchangeAdd(&g_coins, count);
    return true;
}

// ------------------------------------------------------------------------------
// The wheel: its angle, its motor (6-byte frames 55 a b c d sum on the real one, their meaning
// still to be seen: logged), the seat's shaker
// ------------------------------------------------------------------------------

static bool BIND_IsExistSteeringWheel(void)
{
    first_call("BIND_IsExistSteeringWheel");
    return true;
}

static bool BIND_OpenSteeringWheelDevice(int port)
{
    first_call("BIND_OpenSteeringWheelDevice");
    return true;
}

static bool BIND_CloseSteeringWheelDevice(void)
{
    return true;
}

static int BIND_GetSteeringWheelAngle(void)
{
    return wl_input_get(&g_inputs[IN_STEERING]);
}

static int BIND_GetSteeringWheelLevel(void)
{
    return 0;
}

static bool BIND_SetSteeringWheelData(int a, int b, int c, int d)
{
    static int last[4] = {-1, -1, -1, -1}, logged;
    if ((a != last[0] || b != last[1] || c != last[2] || d != last[3]) && logged++ < 200)
        wl_log("unis: wheel motor %d %d %d %d (wheel %d)", a, b, c, d, wl_input_get(&g_inputs[IN_STEERING]));
    last[0] = a, last[1] = b, last[2] = c, last[3] = d;
    return true;
}

static bool BIND_SetShakePower(int a, int b)
{
    static int logged;
    if (logged++ < 20)
        wl_log("unis: shaker %d %d", a, b);
    return true;
}

static bool BIND_SetShakePower_Game(int a, int b)
{
    return BIND_SetShakePower(a, b);
}

static bool BIND_IsExistZlb(void)
{
    first_call("BIND_IsExistZlb");
    return false;
}

static bool BIND_OpenZlbDevice(int port)
{
    first_call("BIND_OpenZlbDevice");
    return false;
}

static bool BIND_CloseZlbDevice(void)
{
    return true;
}

// ------------------------------------------------------------------------------
// Those that act on the PC: nothing done, success answered (the ping: no network)
// ------------------------------------------------------------------------------

static bool BIND_SetLocalTime(int year, int month, int day, int hour, int minute, int second)
{
    wl_log("unis: BIND_SetLocalTime %04d-%02d-%02d %02d:%02d refused, the PC's clock left alone", year, month, day, hour, minute);
    return true;
}

static bool BIND_ResetMachine(void)
{
    wl_log("unis: BIND_ResetMachine refused, the PC left alone");
    return true;
}

static int BIND_SHDeleteFolder(const wchar_t *path)
{
    char u[MAX_PATH * 2];
    wl_log("unis: BIND_SHDeleteFolder \"%s\" refused", path ? wl_u8(path, u, sizeof u) : "");
    return 1;
}

static int BIND_SetSystemVolume(int a, int b)
{
    static int logged;
    if (logged++ < 5)
        wl_log("unis: BIND_SetSystemVolume %d %d refused, the PC's volume left alone", a, b);
    return 1;
}

static int BIND_ICMPPing(const char *host, int *time)
{
    if (time)
        *time = -1;
    return -1;
}

#define BIND(name) {#name, (void *)name}
static const struct
{
    const char *name;
    void *fn;
} g_binds[] = {
    BIND(BIND_InitDongle),        BIND(BIND_IsDongleExist),         BIND(BIND_IsDongleVerify),           BIND(BIND_GetDongleVerifyLic),
    BIND(BIND_UnInitDongle),      BIND(BIND_InitUnisIo),            BIND(BIND_IsIoWorking),              BIND(BIND_GetIoData),
    BIND(BIND_GetInternalCoins),  BIND(BIND_DecCoins),              BIND(BIND_AddKeyboardCoins),         BIND(BIND_IsExistSteeringWheel),
    BIND(BIND_OpenSteeringWheelDevice), BIND(BIND_CloseSteeringWheelDevice), BIND(BIND_GetSteeringWheelAngle), BIND(BIND_GetSteeringWheelLevel),
    BIND(BIND_SetSteeringWheelData), BIND(BIND_SetShakePower),      BIND(BIND_SetShakePower_Game),       BIND(BIND_IsExistZlb),
    BIND(BIND_OpenZlbDevice),     BIND(BIND_CloseZlbDevice),        BIND(BIND_SetLocalTime),             BIND(BIND_ResetMachine),
    BIND(BIND_SHDeleteFolder),    BIND(BIND_SetSystemVolume),       BIND(BIND_ICMPPing),
};

// The game's own GetProcAddress: UsbPluginsDll_R.dll's functions above are windowsloader's
static FARPROC(WINAPI *oGetProcAddress)(HMODULE, LPCSTR);
static FARPROC WINAPI game_GetProcAddress(HMODULE mod, LPCSTR name)
{
    if (mod && HIWORD((ULONG_PTR)name) && mod == GetModuleHandleA("UsbPluginsDll_R.dll"))
        for (size_t i = 0; i < sizeof g_binds / sizeof g_binds[0]; i++)
            if (!strcmp(name, g_binds[i].name))
                return (FARPROC)g_binds[i].fn;
    return oGetProcAddress(mod, name);
}

int wl_game_unis(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    if (_wcsicmp(base, L"CrazyTaxiGame-Win64-Shipping.exe"))
        return 0;
    *name = "crazyride";
    *title = "Crazy Ride";
    wl_log("game: %s (UNIS PC, Unreal Engine 4)", *title);
    wl_inputs_set_table(g_inputs, IN_COUNT);
    if (wl_patch_iat(GetModuleHandleW(NULL), "KERNEL32.dll", "GetProcAddress", (void *)game_GetProcAddress, (void **)&oGetProcAddress))
        wl_log("game: UsbPluginsDll_R.dll's board, wheel, coins and key functions given by windowsloader");
    return 1;
}
