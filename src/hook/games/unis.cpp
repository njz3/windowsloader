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
// lights (to the network outputs too, below), a protection key ("SSNK"), and some that act on the PC
// itself (its clock, a restart, deleting a folder, the sound volume, a ping). windowsloader gives
// the game its own functions for those; the others (ini files, text conversions, the DLL's own
// state) stay the DLL's.
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

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "lang.h"
#include "link.h"
#include "log.h"
#include "vscreen.h"
#include "outputs.h"
#include "vdev.h"

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
    {"SeatPause", "SeatStop", WL_INPUT_SWITCH, 0, 1, 0},
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
    double spring = g_motor[0] == 3 ? -left * g_motor[1] / 127.0 : 0, shake = 0;
    if (g_motor[2])
        shake = (flip ^= 1) ? g_motor[2] / 20.0 : -g_motor[2] / 20.0;
    wl_ffb_set_parts(0, spring, 0, shake);
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
// Those that act on the PC: nothing done, success answered (the ping: no network alone)
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

// The game's ping (its APing: StartPing, +0x21c2a0, then +0x210c20; DefaultSettingData.ini PingIP,
// empty: the PC's network's .1), asked at the start (during the WARNING screen):
// BIND_ICMPPing(host, &state, &time, &ttl, count, timeout). The DLL sets state 1 and returns 1; its
// thread (+0xe0e0) pings count times (IcmpSendEcho2), then writes the last reply's status (0, or
// the error when all failed), the mean time in ms and the mean TTL. The game: -1 returned, it sets
// 2 and calls its end (PingEnd, +0x210ba0); otherwise its tick (+0x21edc0) calls that end once
// state is no longer 1. Alone the cabinet has no network (system.cpp): failed. Linked: done, 0,
// 1 ms, TTL 128 (state left at 1 before: the matching screen waited and never opened
// McastUDP.dll's sockets)
static int BIND_ICMPPing(const wchar_t *host, int *state, int *time, int *ttl, int count, int timeout)
{
    static int logged;
    int linked = wl_link_count() != 0;
    if (logged++ < 3)
    {
        char u[256];
        wl_log("unis: BIND_ICMPPing \"%s\" (%d times, timeout %d): %s", host ? wl_u8(host, u, sizeof u) : "", count, timeout,
               linked ? "answered, 1 ms (linked)" : "failed (alone, no network)");
    }
    if (!linked)
        return -1;
    if (state)
        *state = 0;
    if (time)
        *time = 1;
    if (ttl)
        *ttl = 128;
    return 1;
}

// The cabinet's lights, read in UsbPluginsDll_R.dll: each function keeps its values and sends the
// board's frame (+0xfc60): one bit a colour for SetBoxLight, SetEdgeLight, SetSideLight and
// SetMachineSideLight (three each: red, green, blue, it seems), two bits for SetSpeedLight,
// SetStopLight and SetBrakeLight, a byte's low bits for SetGameStateLight, and SetStartLightState.
// They are kept here for the network outputs, then given to the DLL's own function
enum
{
    LIGHT_BOX,
    LIGHT_EDGE,
    LIGHT_SIDE,
    LIGHT_MACHINE_SIDE,
    LIGHT_RGB_COUNT,
    LIGHT_SPEED = 0,
    LIGHT_STOP,
    LIGHT_BRAKE,
    LIGHT_GAME_STATE,
    LIGHT_ONE_COUNT
};
static const char *const g_rgbLights[LIGHT_RGB_COUNT] = {"BoxLight", "EdgeLight", "SideLight", "MachineSideLight"};
static const char *const g_oneLights[LIGHT_ONE_COUNT] = {"SpeedLight", "StopLight", "BrakeLight", "GameStateLight"};
static int (*oRgbLight[LIGHT_RGB_COUNT])(int, int, int);
static int (*oOneLight[LIGHT_ONE_COUNT])(int);
static bool (*oStartLight)(bool);
static int g_lightValue = -1; // BoxLight_r's number in the network outputs: the RGB lights, the others, Start

static int rgb_light(int which, int r, int g, int b)
{
    if (g_lightValue >= 0)
    {
        wl_outputs_value(g_lightValue + which * 3, r);
        wl_outputs_value(g_lightValue + which * 3 + 1, g);
        wl_outputs_value(g_lightValue + which * 3 + 2, b);
    }
    return oRgbLight[which] ? oRgbLight[which](r, g, b) : 1;
}

static int one_light(int which, int value)
{
    if (g_lightValue >= 0)
        wl_outputs_value(g_lightValue + LIGHT_RGB_COUNT * 3 + which, value);
    return oOneLight[which] ? oOneLight[which](value) : 1;
}

static int BIND_SetBoxLight(int r, int g, int b)
{
    return rgb_light(LIGHT_BOX, r, g, b);
}

static int BIND_SetEdgeLight(int r, int g, int b)
{
    return rgb_light(LIGHT_EDGE, r, g, b);
}

static int BIND_SetSideLight(int r, int g, int b)
{
    return rgb_light(LIGHT_SIDE, r, g, b);
}

static int BIND_SetMachineSideLight(int r, int g, int b)
{
    return rgb_light(LIGHT_MACHINE_SIDE, r, g, b);
}

static int BIND_SetSpeedLight(int value)
{
    return one_light(LIGHT_SPEED, value);
}

static int BIND_SetStopLight(int value)
{
    return one_light(LIGHT_STOP, value);
}

static int BIND_SetBrakeLight(int value)
{
    return one_light(LIGHT_BRAKE, value);
}

static int BIND_SetGameStateLight(int value)
{
    return one_light(LIGHT_GAME_STATE, value);
}

static bool BIND_SetStartLightState(bool on)
{
    if (g_lightValue >= 0)
        wl_outputs_value(g_lightValue + LIGHT_RGB_COUNT * 3 + LIGHT_ONE_COUNT, on);
    return oStartLight ? oStartLight(on) : true;
}

static void lights_declare(void)
{
    static const char *const colour[3] = {"r", "g", "b"}, *const label[3] = {WL_N("rouge"), WL_N("vert"), WL_N("bleu")};
    char name[32], about[128];
    for (int l = 0; l < LIGHT_RGB_COUNT; l++)
        for (int c = 0; c < 3; c++)
        {
            snprintf(name, sizeof name, "%s_%s", g_rgbLights[l], colour[c]);
            snprintf(about, sizeof about, wl_tr("Set%s, %s (ordre supposé), allumé ou non (UsbPluginsDll_R.dll)"), g_rgbLights[l], wl_tr(label[c]));
            int v = wl_outputs_declare_value(name, "bit (0/1)", about);
            if (!l && !c)
                g_lightValue = v;
        }
    for (int l = 0; l < LIGHT_ONE_COUNT; l++)
    {
        snprintf(about, sizeof about, "Set%s (UsbPluginsDll_R.dll)%s", g_oneLights[l], l == LIGHT_GAME_STATE ? "" : wl_tr(", deux bits"));
        wl_outputs_declare_value(g_oneLights[l], l == LIGHT_GAME_STATE ? wl_tr("nombre") : wl_tr("0 à 3"), about);
    }
    wl_outputs_declare_value("StartLight", "bit (0/1)", wl_tr("SetStartLightState, la lampe de Start (UsbPluginsDll_R.dll)"));
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

// The lights: windowsloader's, the DLL's kept to be called after
#define LIGHT(name, original) {#name, (void *)name, (void **)&original}
static const struct
{
    const char *name;
    void *fn;
    void **original;
} g_lights[] = {
    LIGHT(BIND_SetBoxLight, oRgbLight[LIGHT_BOX]),     LIGHT(BIND_SetEdgeLight, oRgbLight[LIGHT_EDGE]),
    LIGHT(BIND_SetSideLight, oRgbLight[LIGHT_SIDE]),   LIGHT(BIND_SetMachineSideLight, oRgbLight[LIGHT_MACHINE_SIDE]),
    LIGHT(BIND_SetSpeedLight, oOneLight[LIGHT_SPEED]), LIGHT(BIND_SetStopLight, oOneLight[LIGHT_STOP]),
    LIGHT(BIND_SetBrakeLight, oOneLight[LIGHT_BRAKE]), LIGHT(BIND_SetGameStateLight, oOneLight[LIGHT_GAME_STATE]),
    LIGHT(BIND_SetStartLightState, oStartLight),
};

// The link between cabinets (link.h, [Game.crazyride] LINK): Plugins\McastUDP.dll, whose BIND_*
// functions the game asks the same way, has the sockets. Its receiving one on UDP 8888 (any
// address) joins the group 234.2.2.1 (BIND_InitSocket_Recv, BIND_JoinMcast), its sending one sends
// there with a TTL of 1 (BIND_InitSocket_Send, a thread at +0x3cf0), then a TCP connection to the
// address and port the game gives it (+0x4630; BIND_InitTCPSocketListen for the other side). Its
// imports made the link's when the game first asks for one of its functions: its multicast goes
// to each PC of LINK, its connections to LINK's PCs only

static void link_mcast(HMODULE mod)
{
    static volatile LONG done;
    if (mod && !InterlockedExchange(&done, 1))
        wl_link_module(mod);
}

static void settings(void)
{
    int n = wl_link_settings(GetModuleHandleW(NULL));
    if (!n)
        return;
    wl_log("game: Crazy Ride linked: cabinet %d of %d; its multicast (Plugins\\McastUDP.dll) goes to LINK's PCs", wl_link_cabinet(), n);
    link_mcast(GetModuleHandleA("McastUDP.dll")); // already loaded
}

// The game's settings files (Binaries\Win64\Setting_*.ini) are read through Plugins\IniFile.dll
// (seen on the cabinets): BIND_ReadInI_WChar(text, text, value[260], text, flag) with the file,
// the section and the key (+0x1989d0), the value's length when found. The matching screen,
// Matching2, whose Blueprint opens McastUDP.dll's sockets, is shown only when Setting_System's
// bEnableOnline is true (+0x1dd5e0, read at +0x20a471; false when missing, as on the cabinets):
// linked, the game reads it true
static int (*oReadInI)(const wchar_t *, const wchar_t *, wchar_t *, const wchar_t *, char);
static int BIND_ReadInI_WChar(const wchar_t *a, const wchar_t *b, wchar_t *value, const wchar_t *c, char flag)
{
    static const wchar_t key[] = L"bEnableOnline";
    if (wl_link_count() && value && ((a && !_wcsicmp(a, key)) || (b && !_wcsicmp(b, key)) || (c && !_wcsicmp(c, key))))
    {
        static int logged;
        if (!logged++)
            wl_log("unis: bEnableOnline read as true (linked): the matching screen opens the network");
        wcscpy_s(value, 260, L"true");
        return 4;
    }
    return oReadInI ? oReadInI(a, b, value, c, flag) : 0;
}

// The game's own GetProcAddress: UsbPluginsDll_R.dll's functions above are windowsloader's
static FARPROC(WINAPI *oGetProcAddress)(HMODULE, LPCSTR);
static FARPROC WINAPI game_GetProcAddress(HMODULE mod, LPCSTR name)
{
    if (mod && wl_link_count() && mod == GetModuleHandleA("McastUDP.dll"))
        link_mcast(mod);
    if (mod && HIWORD((ULONG_PTR)name) && !strcmp(name, "BIND_ReadInI_WChar") && mod == GetModuleHandleA("IniFile.dll"))
    {
        oReadInI = (int (*)(const wchar_t *, const wchar_t *, wchar_t *, const wchar_t *, char))oGetProcAddress(mod, name);
        return oReadInI ? (FARPROC)BIND_ReadInI_WChar : NULL;
    }
    if (mod && HIWORD((ULONG_PTR)name) && mod == GetModuleHandleA("UsbPluginsDll_R.dll"))
    {
        for (size_t i = 0; i < sizeof g_binds / sizeof g_binds[0]; i++)
            if (!strcmp(name, g_binds[i].name))
                return (FARPROC)g_binds[i].fn;
        for (size_t i = 0; i < sizeof g_lights / sizeof g_lights[0]; i++)
            if (!strcmp(name, g_lights[i].name))
            {
                *g_lights[i].original = (void *)oGetProcAddress(mod, name);
                return (FARPROC)g_lights[i].fn;
            }
    }
    return oGetProcAddress(mod, name);
}

// ------------------------------------------------------------------------------
// Crazy Speed Arcade (release\Client_r.exe, 32 bits, Ogre, 2010): UNIS's "game232" driving
// cabinet. Reimplemented from windowsarcadeloader's findings (aderumier, GPL v3,
// https://github.com/aderumier/windowsarcadeloader, crates/payload-unis/src/game232.rs and its
// docs/IMPLEMENTATION.md): the board's protocol, the game's places, what each one needs.
//
// - The board (configure\ApplicationCfg.ini UseGame232 = 1): COM1, 19200 bauds 8N1, read a byte at
//   a time (overlapped). It sends its frames on its own: AA AA, 10 bytes, their CRC-16/Modbus (low
//   byte first). Bytes 0..5: the wheel, the accelerator, the brake (16 bits, 0..1024, the game
//   takes 1 - raw / 1024); bytes 6..8: 20 buttons (bit n of the 24), byte 8 bit 7 a coin (counted
//   at each frame it is set: one frame per press). Through configure\js_game232_map.dat: button 5
//   START/NOS (confirms, the nitro), 4 VIEW (the camera; the game card's renewal on the title), 0
//   and 1 the vertical menus' up and down, 9 the operator's (it quits the game: not given). The
//   PC's frames (AA AA, a length: two settings at the start, the lamps, the wheel's force: a
//   constant force and a vibration, speed_force) need no answer.
// - Its actions (64-byte blocks) are RSA-encrypted by the game and decrypted by its dongle
//   (dic32u.dll): both functions return at once (+0x7f1d0, +0x7f2e0), the actions in clear.
// - Its licence bound to the cabinet ("UNIDENTIFIED SYSTEM ID", +0xf7640): answered valid.
// - WMI: the memory size (Win32_LogicalMemoryConfiguration, gone since Windows Vista) and the video
//   card read at the start; an empty enumeration passed its check and the game read a null
//   object: the checks after Next made jne (+0x102c06, +0x102ccc).
// - Offline: the network engine's connection thread (to a server on the internet, every 30 s)
//   crashed on its queue: it ends at once (+0x347dc0), its shutdown skips the queue's release
//   (+0x3473c5), and its connect to an address outside the PC and its LAN is refused. The game
//   shows OFFLINE and plays Tournament and Time Attack.
// - Its picture: a 1360x768 full screen mode (in its code), which Ogre refuses on a screen without
//   it: VIRTUAL_DISPLAY (its minimum 1360x768) adds it to Direct3D 9's modes.
// ------------------------------------------------------------------------------

enum
{
    CS_STEERING,
    CS_GAS,
    CS_BRAKE,
    CS_COIN,
    CS_START, // button 5: START/NOS
    CS_VIEW,  // button 4
    CS_UP,    // button 0: the vertical menus
    CS_DOWN,  // button 1
    CS_COUNT
};

static WlInput g_speedInputs[CS_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 1024, 0, 512},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 1024, 0, 1024},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 1024, 0, 1024},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"View", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"MenuUp", "SelectUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"MenuDown", "SelectDown", WL_INPUT_SWITCH, 0, 1, 0},
};

static CRITICAL_SECTION g_speedLock;
static unsigned char g_speedOut[64];
static int g_speedCount, g_speedCoinHeld;
static ULONGLONG g_speedLast;
static LONG g_speedFrames;

static unsigned short crc16_modbus(const unsigned char *d, int n)
{
    unsigned short crc = 0xFFFF;
    for (int i = 0; i < n; i++)
    {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (unsigned short)((crc >> 1) ^ 0xA001) : (unsigned short)(crc >> 1);
    }
    return crc;
}

static void speed_undo_loader(unsigned char *exe);

// The board's next frame, at most one each 8 ms (the read waits for it)
static void speed_frame(void)
{
    ULONGLONG now = GetTickCount64();
    if (now - g_speedLast < 8)
    {
        Sleep((DWORD)(8 - (now - g_speedLast)));
        now = GetTickCount64();
    }
    g_speedLast = now;
    unsigned char f[14] = {0xAA, 0xAA};
    unsigned char *data = f + 2;
    int axes[3] = {wl_input_get(&g_speedInputs[CS_STEERING]), wl_input_get(&g_speedInputs[CS_GAS]), wl_input_get(&g_speedInputs[CS_BRAKE])};
    static int lastAxes[3] = {-1, -1, -1}, axesLogged;
    if (memcmp(axes, lastAxes, sizeof axes) && axesLogged < 40)
    {
        axesLogged++;
        wl_log("unis: Crazy Speed's board: wheel %d, accelerator %d, brake %d (0..1024, the game takes 1 - raw / 1024)", axes[0], axes[1],
               axes[2]);
        memcpy(lastAxes, axes, sizeof axes);
    }
    for (int a = 0; a < 3; a++)
    {
        data[2 * a] = (unsigned char)(axes[a] & 0xFF);
        data[2 * a + 1] = (unsigned char)(axes[a] >> 8);
    }
    unsigned buttons = 0;
    if (wl_input_get(&g_speedInputs[CS_UP]))
        buttons |= 1u << 0;
    if (wl_input_get(&g_speedInputs[CS_DOWN]))
        buttons |= 1u << 1;
    if (wl_input_get(&g_speedInputs[CS_VIEW]))
        buttons |= 1u << 4;
    if (wl_input_get(&g_speedInputs[CS_START]))
        buttons |= 1u << 5;
    static unsigned lastButtons;
    static int buttonsLogged;
    if (buttons != lastButtons && buttonsLogged < 60)
    {
        buttonsLogged++;
        wl_log("unis: Crazy Speed's board: buttons 0x%06x (0/1 the menus' up/down, 4 VIEW, 5 START/NOS)", buttons);
        lastButtons = buttons;
    }
    data[6] = (unsigned char)buttons;
    data[7] = (unsigned char)(buttons >> 8);
    data[8] = (unsigned char)(buttons >> 16);
    int coin = wl_input_get(&g_speedInputs[CS_COIN]) != 0;
    if (coin && !g_speedCoinHeld)
        data[8] |= 0x80; // one frame per press
    g_speedCoinHeld = coin;
    unsigned short crc = crc16_modbus(data, 10);
    f[12] = (unsigned char)(crc & 0xFF);
    f[13] = (unsigned char)(crc >> 8);
    memcpy(g_speedOut, f, sizeof f);
    g_speedCount = (int)sizeof f;
    if (InterlockedIncrement(&g_speedFrames) == 1)
    {
        wl_log("unis: Crazy Speed's board, first frame to the game");
        speed_undo_loader((unsigned char *)GetModuleHandleW(NULL)); // its DLLs' starts long done
    }
}

static BOOL speed_read(void *buffer, DWORD size, DWORD *read, LONGLONG offset)
{
    EnterCriticalSection(&g_speedLock);
    if (!g_speedCount)
        speed_frame();
    DWORD n = size < (DWORD)g_speedCount ? size : (DWORD)g_speedCount;
    memcpy(buffer, g_speedOut, n);
    memmove(g_speedOut, g_speedOut + n, g_speedCount - n);
    g_speedCount -= n;
    LeaveCriticalSection(&g_speedLock);
    *read = n;
    return TRUE;
}

// The wheel's force: GAME232::cFeedbackInterface's frames (the game's CForceFeedbackEffect_Game232,
// its OIS effects made the board's), after AA AA, their length (the kind, the data, the CRC):
//   kind 6, a constant force (+0x53dcc0): two durations (u16, 1/900 s: 0.15 s each frame), the
//          force (s16, -10000..10000, the game's gain applied), resent while it lasts
//   kind 7, a vibration (+0x53dd10): its duration (u16, 1/900 s, 70 s and more: until stopped),
//          a second time, its strength (s16, -10000..10000) and its frequency (u16, 1/25 Hz,
//          0.1..9 Hz)
// The force > 0 pushes the wheel to the right as the board reads it (the wheel 0 at its left end):
// made windowsloader's (> 0 to the left)
static void speed_force(const unsigned char *f)
{
    short force;
    memcpy(&force, f + 6, 2);
    static int told;
    if (force && told++ < 5)
        wl_log("unis: Crazy Speed's wheel force %d (the board's, -10000..10000; wheel %d)", force,
               wl_input_get(&g_speedInputs[CS_STEERING]));
    wl_ffb_set(-force / 10000.0);
}

static void speed_vibration(const unsigned char *f)
{
    short strength;
    unsigned short rate;
    memcpy(&strength, f + 6, 2);
    memcpy(&rate, f + 8, 2);
    double hz = rate / 25.0, v = (strength < 0 ? -strength : strength) / 10000.0;
    static int told;
    if (strength && told++ < 5)
        wl_log("unis: Crazy Speed's wheel vibration %d at %.1f Hz", strength, hz);
    wl_ffb_set_vibration(v > 1 ? 1 : v, hz > 0.05 ? (int)(1000.0 / hz) : 0);
}

static BOOL speed_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    *written = size; // nothing to answer
    static int logged;
    if (logged++ < 10)
        wl_log_hex("unis: Crazy Speed's board, from the game", buffer, size);
    const unsigned char *b = (const unsigned char *)buffer;
    for (DWORD at = 0; at + 4 <= size;)
    {
        if (b[at] != 0xAA || b[at + 1] != 0xAA)
        {
            at++;
            continue;
        }
        DWORD len = b[at + 2]; // the kind, its data, the CRC
        if (at + 3 + len > size || len < 3 || crc16_modbus(b + at + 2, (int)len - 1) != (unsigned short)(b[at + 1 + len] | b[at + 2 + len] << 8))
            break;
        const unsigned char *f = b + at + 2; // its length, its kind, its data
        if (f[1] == 6 && len >= 9)
            speed_force(f);
        else if (f[1] == 7 && len >= 11)
            speed_vibration(f);
        at += 3 + len;
    }
    return TRUE;
}

static DWORD speed_modem(void)
{
    return MS_CTS_ON | MS_DSR_ON;
}

static DWORD speed_waiting(void)
{
    return g_speedCount ? (DWORD)g_speedCount : 14;
}

static WlVirtualDevice g_speedBoard = {"COM1", NULL, speed_read, speed_write, speed_modem, speed_waiting};

// connect (ws2_32 #4): an address outside the PC and its LAN refused (the game offline)
static int(WINAPI *oSpeedConnect)(SOCKET, const struct sockaddr *, int);
static int WINAPI speed_connect(SOCKET s, const struct sockaddr *addr, int len)
{
    if (addr && len >= 8 && addr->sa_family == AF_INET)
    {
        const unsigned char *ip = (const unsigned char *)&((const struct sockaddr_in *)addr)->sin_addr;
        int local = ip[0] == 127 || ip[0] == 10 || (ip[0] == 192 && ip[1] == 168) || (ip[0] == 169 && ip[1] == 254) ||
                    (ip[0] == 172 && ip[1] >= 16 && ip[1] < 32);
        if (!local)
        {
            static int logged;
            if (!logged++)
                wl_log("unis: Crazy Speed: its connection to %u.%u.%u.%u refused (offline)", ip[0], ip[1], ip[2], ip[3]);
            WSASetLastError(WSAECONNREFUSED);
            return SOCKET_ERROR;
        }
    }
    return oSpeedConnect(s, addr, len);
}

// The link (link.h): [Game.crazyspeed] LINK and CABINET. Its LAN mode (CLANInterface: a host and
// its guests, found by a broadcast, then TCP) on LINK's PCs; its connections to the internet stay
// refused (speed_connect, under the link's)
static void speed_settings(void)
{
    int n = wl_link_settings(GetModuleHandleW(NULL));
    if (n)
        wl_log("game: Crazy Speed linked: cabinet %d of %d (its LAN mode)", wl_link_cabinet(), n);
}

// The cabinets' release\dic32u.dll is not the dongle's library but another loader's (its dic32u.ini:
// the controls from an XInput pad, x360ce started first): its DllMain rewrites the game's input
// update (+0xa7ff0) so that its own thread writes the controls there, by WriteProcessMemory: the
// memsets and the copy of the previous state emptied (+0xa8004, +0xa8012, +0xa8032), the stores
// of the calibrated wheel, accelerator and brake removed (+0xa84fa, +0xa85c7, +0xa8694). The
// board's accelerator then never reached the car (000 km/h, its NOS charging). Its writes into the
// game made nothing, and those six places put back as the program has them
static BOOL WINAPI dic_WriteProcessMemory(HANDLE process, LPVOID address, LPCVOID data, SIZE_T size, SIZE_T *written)
{
    if (written)
        *written = size;
    return TRUE;
}

static void speed_undo_loader(unsigned char *exe)
{
    static volatile LONG hooked;
    HMODULE dic = GetModuleHandleW(L"dic32u.dll");
    if (!dic || GetProcAddress(dic, "TransmitAPDU") == NULL)
        return; // the real dongle's library, or none
    if (!InterlockedExchange(&hooked, 1) && wl_patch_iat(dic, "KERNEL32.dll", "WriteProcessMemory", (void *)dic_WriteProcessMemory, NULL))
        wl_log("game: Crazy Speed: release\\dic32u.dll is another loader's (XInput controls): its writes into the game made nothing");
    static const struct
    {
        unsigned rva;
        unsigned char loader[6], program[6];
        int size;
        const char *what;
    } undo[] = {
        {0xA8004, {0x00}, {0x01}, 1, "Crazy Speed's input update, its controls cleared (put back)"},
        {0xA8012, {0x00}, {0x40}, 1, "Crazy Speed's input update, its previous controls kept (put back)"},
        {0xA8032, {0x00}, {0x80}, 1, "Crazy Speed's input update, its analog values cleared (put back)"},
        {0xA84FA, {0xDD, 0xD8, 0x90, 0x90, 0x90, 0x90}, {0xD9, 0x9B, 0x94, 0x02, 0x00, 0x00}, 6, "Crazy Speed's wheel stored (put back)"},
        {0xA85C7, {0xDD, 0xD8, 0x90, 0x90, 0x90, 0x90}, {0xD9, 0x9B, 0x98, 0x02, 0x00, 0x00}, 6, "Crazy Speed's accelerator stored (put back)"},
        {0xA8694, {0xDD, 0xD8, 0x90, 0x90, 0x90, 0x90}, {0xD9, 0x9B, 0x9C, 0x02, 0x00, 0x00}, 6, "Crazy Speed's brake stored (put back)"},
    };
    for (size_t i = 0; i < sizeof undo / sizeof undo[0]; i++)
        if (!memcmp(exe + undo[i].rva, undo[i].loader, undo[i].size))
            wl_patch_code(exe + undo[i].rva, undo[i].loader, undo[i].program, undo[i].size, undo[i].what);
}

static void speed_patches(unsigned char *exe)
{
    // The checks after WMI's Next: jl made jne (short and near)
    static const unsigned char wmi1[] = {0x85, 0xC0, 0x7C, 0x4D}, wmi1New[] = {0x85, 0xC0, 0x75, 0x4D};
    static const unsigned char wmi2[] = {0x85, 0xC0, 0x0F, 0x8C}, wmi2New[] = {0x85, 0xC0, 0x0F, 0x85};
    wl_patch_code(exe + 0x102C06, wmi1, wmi1New, sizeof wmi1, "Crazy Speed's memory size check (WMI)");
    wl_patch_code(exe + 0x102CCC, wmi2, wmi2New, sizeof wmi2, "Crazy Speed's video card check (WMI)");
    // The licence bound to the cabinet: mov al, 1; ret
    static const unsigned char id[] = {0x6A, 0xFF, 0x68}, idNew[] = {0xB0, 0x01, 0xC3};
    wl_patch_code(exe + 0xF7640, id, idNew, sizeof id, "Crazy Speed's cabinet licence check");
    // The connection thread ends at once: xor eax, eax; ret 4
    static const unsigned char thread[] = {0x53, 0x8B, 0x1D, 0x08, 0x8A}, threadNew[] = {0x31, 0xC0, 0xC2, 0x04, 0x00};
    wl_patch_code(exe + 0x347DC0, thread, threadNew, sizeof thread, "Crazy Speed's connection thread (offline)");
    // Its shutdown jumps over the queue's release
    static const unsigned char shutdown[] = {0x8B, 0x0D}, shutdownNew[] = {0xEB, 0x2E};
    if (!memcmp(exe + 0x3473C5, "\x8B\x0D\x6C\x9D\x8C\x00", 6))
        wl_patch_code(exe + 0x3473C5, shutdown, shutdownNew, 2, "Crazy Speed's network shutdown (its queue left)");
    // The actions' encryption and the dongle's decryption: ret
    static const unsigned char enc[] = {0x81, 0xEC, 0x04, 0x01, 0x00, 0x00}, dec[] = {0x81, 0xEC, 0x08, 0x06, 0x00, 0x00}, ret[] = {0xC3};
    if (!memcmp(exe + 0x7F1D0, enc, sizeof enc))
        wl_patch_code(exe + 0x7F1D0, enc, ret, 1, "Crazy Speed's actions encryption (in clear)");
    if (!memcmp(exe + 0x7F2E0, dec, sizeof dec))
        wl_patch_code(exe + 0x7F2E0, dec, ret, 1, "Crazy Speed's dongle decryption (in clear)");
}

static int crazy_speed(const wchar_t *exePath, const wchar_t *base, const char **name, const char **title)
{
    if (_wcsicmp(base, L"Client_r.exe"))
        return 0;
    wchar_t marker[MAX_PATH];
    swprintf(marker, MAX_PATH, L"%.*sReferenceAppLayer.dll", (int)(base - exePath), exePath);
    if (GetFileAttributesW(marker) == INVALID_FILE_ATTRIBUTES)
        return 0;
    *name = "crazyspeed";
    *title = "Crazy Speed Arcade";
    unsigned char *exe = (unsigned char *)GetModuleHandleW(NULL);
    DWORD crc = wl_game_exe_crc32();
    wl_log("game: %s (UNIS PC, Ogre, Client_r.exe CRC32 %08lX)", *title, crc);
    wl_game_set_boards("Controls on UNIS game232 board (COM1, 19200 bauds, frames AA AA, 10 bytes, CRC-16/Modbus)", NULL);
    wl_inputs_set_table(g_speedInputs, CS_COUNT);
    InitializeCriticalSection(&g_speedLock);
    wl_vdev_register(&g_speedBoard);
    wl_vdev_label("COM1", WL_N("Carte d'E/S"));
    // The PC's own joysticks (vJoy on the cabinets) kept from the game's DirectInput (OIS): its
    // controls are the board's, windowsloader reads them itself
    wl_dinput_hide_controllers();
    if (crc == 0x42B6AC2D)
        speed_patches(exe);
    else
        wl_log("game: %s, not the program this was made for (CRC32 %08lX, not 42B6AC2D): its dongle, licence and network left as they are", *title, crc);
    if (wl_patch_iat((HMODULE)exe, "WS2_32.dll", MAKEINTRESOURCEA(4), (void *)speed_connect, (void **)&oSpeedConnect))
        wl_log("game: %s: connections outside the PC and its LAN refused (offline)", *title);
    wl_game_use_options("crazyspeed");
    wl_game_on_settings(speed_settings);
    wl_vscreen_minimum(1360, 768);
    return 1;
}

int wl_game_unis(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    if (crazy_speed(exePath, base, name, title))
        return 1;
    if (_wcsicmp(base, L"CrazyTaxiGame-Win64-Shipping.exe"))
        return 0;
    *name = "crazyride";
    *title = "Crazy Ride";
    wl_log("game: %s (UNIS PC, Unreal Engine 4)", *title);
    wl_game_set_boards("Controls on UNIS I/O board (serial, through UsbPluginsDll_R.dll)",
                       "Motor board: UNIS wheel board (serial, 6-byte frames 55 a b c d sum, through UsbPluginsDll_R.dll)");
    wl_inputs_set_table(g_inputs, IN_COUNT);
    lights_declare();
    if (wl_patch_iat(GetModuleHandleW(NULL), "KERNEL32.dll", "GetProcAddress", (void *)game_GetProcAddress, (void **)&oGetProcAddress))
        wl_log("game: UsbPluginsDll_R.dll's board, wheel, coins and key functions given by windowsloader");
    wl_game_use_options("crazyride");
    // Unreal Engine 4's launch refuses a screen under 640x480 (GetSystemMetrics, +0x3b45ef: "The
    // current resolution is too low to run this game"); in its full screen window it draws at the
    // screen's size (640x480: its 16:9 picture cut on the right). Under 1920x1080 it draws its 3D view
    // at 4:3 in the left three quarters of its 16:9 picture (its interface across the whole of it):
    // told 1920x1080 (vscreen.h; checked on a cabinet at 1024x384, the view whole at 61 frames/s)
    wl_vscreen_minimum(1920, 1080);
    wl_game_on_settings(settings);
    return 1;
}
