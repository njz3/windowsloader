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
// - The I/O data (+0x1b3b90): buttons in bytes 2 and 3, the brake in byte 6 and the accelerator in
//   byte 7 (0..255; seen in a race).
// - The wheel's angle: a raw value the game places between the three of its calibration
//   (Setting_Test_Input.ini beside the exe: IoDataList_iMin_X, iMiddle_X, iMax_X, 127, 64 and 1 in
//   the copy, made with TeknoParrot); the same range here, 1 at the left end (the car selection
//   moves with it as the wheel goes).

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
    IN_ENTER,   // byte 2 bit 0: the red button, confirms in the test menu
    IN_DOWN,    // byte 2 bit 1: the black one ("D"), the next line in the test menu
    IN_TEST,    // byte 2 bit 2: the green one, opens the test menu
    IN_START,   // byte 3 bit 0: starts a game, then the Nitro
    IN_SEAT,    // byte 3 bit 1: "Pause Seat Motion"
    IN_COUNT
};

// Found by bits tried one by one and the test menu's Input Test; byte 3 bits 2 and 7 are read by
// the game too (+0x1b3deb, +0x1b3eee), their use not seen
static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 1, 127, 64},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 255, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 255, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Enter", "SelectEnter", WL_INPUT_SWITCH, 0, 1, 0},
    {"MenuDown", "SelectDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"SeatPause", "SeatPause", WL_INPUT_SWITCH, 0, 1, 0},
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
    static const struct
    {
        int input, byte, bit;
    } bits[] = {{IN_ENTER, 2, 0}, {IN_DOWN, 2, 1}, {IN_TEST, 2, 2}, {IN_START, 3, 0}, {IN_SEAT, 3, 1}};
    for (size_t i = 0; i < sizeof bits / sizeof bits[0]; i++)
        if (wl_input_get(&g_inputs[bits[i].input]))
            data[bits[i].byte] |= (unsigned char)(1 << bits[i].bit);
    data[6] = (unsigned char)wl_input_get(&g_inputs[IN_BRAKE]);
    data[7] = (unsigned char)wl_input_get(&g_inputs[IN_GAS]);
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

// The motor as the game drives it (seen in its attract and a race): "3 64 0 0" when the wheel is
// opened, "0 108 0 0" after its init, "3 108 0 0" from then on, and "3 108 5 0" for a moment at a
// crash. Taken as a centring spring (a = 3) of strength b and a shake c, played at each angle read
// (each frame); the meaning is a guess.
static volatile LONG g_motor[4];

static void motor_force(int raw)
{
    static int flip;
    double left = (64 - raw) / 63.0; // the wheel, +1 at the left end
    double force = g_motor[0] == 3 ? -left * g_motor[1] / 127.0 : 0;
    if (g_motor[2])
        force += (flip ^= 1) ? g_motor[2] / 20.0 : -g_motor[2] / 20.0;
    wl_ffb_set(force > 1 ? 1 : force < -1 ? -1 : force);
}

static int BIND_GetSteeringWheelAngle(void)
{
    int raw = wl_input_get(&g_inputs[IN_STEERING]);
    motor_force(raw);
    return raw;
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
    InterlockedExchange(&g_motor[0], a);
    InterlockedExchange(&g_motor[1], b);
    InterlockedExchange(&g_motor[2], c);
    InterlockedExchange(&g_motor[3], d);
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
    wl_game_set_boards("Controls on UNIS I/O board (serial, through UsbPluginsDll_R.dll)",
                       "Motor board: UNIS wheel board (serial, 6-byte frames 55 a b c d sum, through UsbPluginsDll_R.dll)");
    wl_inputs_set_table(g_inputs, IN_COUNT);
    if (wl_patch_iat(GetModuleHandleW(NULL), "KERNEL32.dll", "GetProcAddress", (void *)game_GetProcAddress, (void **)&oGetProcAddress))
        wl_log("game: UsbPluginsDll_R.dll's board, wheel, coins and key functions given by windowsloader");
    return 1;
}
