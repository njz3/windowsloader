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

// Sega ALLS: Sega World Drivers Championship (App\Todoroki\WindowsNoEditor, x64, Unreal Engine 4).
// Two programs: AMDaemon\amdaemon.exe, the platform's service (keychip, I/O board, card reader,
// network, credits), and Todoroki\Binaries\Win64\Todoroki-Win64-Shipping.exe, the game, which
// talks to it through shared memory ("amdipc_..." mutexes). Both run under windowsloader with
// this module. What amdaemon finds of the cabinet, as segatools gives it (common/platform,
// Unlicense; its keys and values, not its code):
// - the registry's HKLM\SYSTEM\SEGA\SystemProperty: the platform (static), the keychip's
//   identity (keychip), the system's loader (Master), the drives' letters (mount);
// - the keychip's driver, \??\FddDriver, its requests by DeviceIoControl;
// - the cabinet's drives: D: and Y: (the game's data, Y:\SDDS), E: (AMFS, the system's files),
//   folders of the copy (D, Y and AMFS beside game.bat).
// The network services the cabinet reaches, ALL.Net, Aime and the game's title server, are played
// on this PC (allnet.cpp). The launcher starts the game with -launch=MiniCabinet and amdaemon with
// its settings for a cabinet alone (launcher.cpp).

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "display.h"
#include "driveboard.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "jvs.h"
#include "log.h"
#include "outputs.h"
#include "vdev.h"
#include "vhid.h"

// The games, on two of amdaemon's platforms. Sega Nu's, Initial D Arcade Stage Zero: the game and
// amdaemon side by side in the copy's folder, its drives in TeknoParrot\Y and TeknoParrot\AMFS
// (the copy of the cabinets), its I/O board a JVS one through the \\.\mxjvs driver
struct AllsGame
{
    const wchar_t *exe; // the game's program
    const char *name, *title;
    const char *gameId, *platformId, *platformName; // the keychip's (segatools' platform_hook_init)
    int nu;
    const char *aimePort, *aimeHardware; // the card reader: its port, its model
    unsigned char aimeFirmware;          // its firmware's version, as amdaemon's firmware file (no update)
};

static const AllsGame g_games[] = {
    {L"Todoroki-Win64-Shipping.exe", "swdc", "Sega World Drivers Championship", "SDDS", "ACA4", "ALLS MX2.1", 0, "COM3", "837-15396", 0x94},
    {L"InitialD0_DX11_Nu.exe", "idz", "Initial D Arcade Stage Zero", "SDDF", "AAV2", "Nu 1.1", 1, "COM10", "837-15286", 0x92},
};
static const AllsGame *g_game;

static wchar_t g_root[MAX_PATH]; // the copy's folder, where game.bat is (Nu: the game's)

// ------------------------------------------------------------------------------
// The registry: HKLM\SYSTEM\SEGA\SystemProperty (segatools' platform/misc.c and nusec.c)
// ------------------------------------------------------------------------------

static void system_property(void)
{
    const DWORD cpuError = 100, cpuWarning = 95, loaders = 1, model = (DWORD)(g_game->platformId[3] - '0'), region = 1, flag = 0x64;
    static const unsigned char server[4] = {192, 168, 172, 0}, server6[16] = {0};
    static const char keychipId[17] = "A69E-01A88888888"; // segatools' default, 16 characters (amSerialIdConvert)
    static const WlRegValue root[] = {{"OSVersion", REG_SZ, "0_0_0", 0}};
    const WlRegValue stat[] = {
        {"CpuTempError", REG_DWORD, &cpuError, 4},
        {"CpuTempWarning", REG_DWORD, &cpuWarning, 4},
        {"PlatformId", REG_SZ, g_game->platformId, 0},
        {"PlatformName", REG_SZ, g_game->platformName, 0},
    };
    const WlRegValue master[] = {{"AppLoaderCount", REG_DWORD, &loaders, 4}};
    const WlRegValue keychip[] = {
        {"gameId", REG_BINARY, g_game->gameId, 4},
        {"keychipId", REG_BINARY, keychipId, 16},
        {"modelType", REG_DWORD, &model, 4},
        {"platformId", REG_BINARY, g_game->platformId, 3},
        {"region", REG_DWORD, &region, 4},
        {"serverIpIpv4", REG_BINARY, server, 4},
        {"serverIpIpv6", REG_BINARY, server6, 16},
        {"systemFlag", REG_DWORD, &flag, 4},
    };
    wl_registry_add_key("SYSTEM\\SEGA", NULL, 0);
    wl_registry_add_key("SYSTEM\\SEGA\\SystemProperty", root, 1);
    wl_registry_add_key("SYSTEM\\SEGA\\SystemProperty\\static", stat, 4);
    wl_registry_add_key("SYSTEM\\SEGA\\SystemProperty\\Master", master, 1);
    wl_registry_add_key("SYSTEM\\SEGA\\SystemProperty\\keychip", keychip, 8);
    static const WlRegValue mount[] = {{"AMFS", REG_SZ, "E:\\", 0}, {"APPDATA", REG_SZ, "Y:\\", 0}};
    wl_registry_add_key("SYSTEM\\SEGA\\SystemProperty\\mount", mount, 2);

    // The display's driver, a DLL amdaemon loads by the name given here: "$amvideo", played by
    // windowsloader (its amDllVideo* functions below, by ordinal; segatools' platform/amvideo.c);
    // the screens' settings
    static const WlRegValue video[] = {{"name", REG_SZ, "$amvideo", 0}, {"name_x86", REG_SZ, "$amvideo", 0}};
    wl_registry_add_key("SYSTEM\\SEGA\\SystemProperty\\amVideo", video, 2);
    static const DWORD one = 1, zero = 0;
    static const WlRegValue display[] = {
        {"monitor_setting_1", REG_SZ, "0", 0}, {"monitor_setting_2", REG_SZ, "0", 0}, {"port_1", REG_DWORD, &one, 4},
        {"port_2", REG_DWORD, &one, 4},        {"port_3", REG_DWORD, &one, 4},        {"port_4", REG_DWORD, &one, 4},
        {"port_5", REG_DWORD, &one, 4},        {"port_6", REG_DWORD, &one, 4},        {"port_7", REG_DWORD, &one, 4},
        {"port_8", REG_DWORD, &one, 4},        {"resolution_1", REG_SZ, "1920x1080", 0}, {"resolution_2", REG_SZ, "1920x1080", 0},
        {"use_segatiming", REG_DWORD, &zero, 4},
    };
    wl_registry_add_key("SYSTEM\\SEGA\\SystemProperty\\sgsetdisplaysetting", NULL, 0);
    wl_registry_add_key("SYSTEM\\SEGA\\SystemProperty\\sgsetdisplaysetting\\CurrentSetting", display, 13);
}

// The display driver's functions: the PC's display is left as it is
static int video_open(void *context)
{
    wl_log("amvideo: open");
    return 0;
}

static int video_close(void *context)
{
    return 0;
}

static int video_set_resolution(void *context, void *settings)
{
    wl_log("amvideo: resolution asked, the PC's display left alone");
    return 0;
}

static int video_bios_version(void *context, char *version, unsigned size)
{
    if (version && size)
        snprintf(version, size, "%s", "01.02.03.04.05");
    return 0;
}

static const WlVirtualDllFunction g_amvideo[] = {
    {"amDllVideoOpen", 1, (void *)video_open},
    {"amDllVideoClose", 2, (void *)video_close},
    {"amDllVideoSetResolution", 3, (void *)video_set_resolution},
    {"amDllVideoGetVBiosVersion", 4, (void *)video_bios_version},
};

// ------------------------------------------------------------------------------
// The keychip's driver (segatools' platform/nusec.c): requests CTL_CODE(0x22, function,
// METHOD_BUFFERED), little-endian 32-bit values in and out
// ------------------------------------------------------------------------------

static LONG g_playCount;
static LONG g_playLimit = 1024;
static LONG g_nearFull = (1 << 16) + 512; // billing type 1, threshold

static BOOL put32(void *out, DWORD outSize, DWORD *returned, DWORD value)
{
    if (!out || outSize < 4)
        return FALSE;
    *(DWORD *)out = value;
    *returned = 4;
    return TRUE;
}

static BOOL keychip_ioctl(DWORD code, const void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned)
{
    DWORD fn = (code >> 2) & 0xFFF;
    *returned = 0;
    static unsigned logged[0x100];
    if ((code >> 16) == 0x22 && fn >= 0x800 && fn < 0x900 && logged[fn - 0x800]++ < 3)
        wl_log("keychip: request 0x%03lx (%lu bytes in, %lu out)", fn, inSize, outSize);
    DWORD v = in && inSize >= 4 ? *(const DWORD *)in : 0;
    switch (fn)
    {
    case 0x845: // ping
        return TRUE;
    case 0x854: // play count
        return put32(out, outSize, returned, (DWORD)g_playCount);
    case 0x855: // play count added
        return put32(out, outSize, returned, (DWORD)InterlockedAdd(&g_playCount, (LONG)v));
    case 0x881: // play limit
        return put32(out, outSize, returned, (DWORD)g_playLimit);
    case 0x882:
        g_playLimit = (LONG)v;
        return put32(out, outSize, returned, v);
    case 0x883: // near full
        return put32(out, outSize, returned, (DWORD)g_nearFull);
    case 0x884:
        g_nearFull = (LONG)v;
        return put32(out, outSize, returned, v);
    case 0x862: // trace log erased
    case 0x863:
        return TRUE;
    case 0x864: // a trace log record (60 bytes) kept: accepted, not stored
        return TRUE;
    case 0x866: // trace log state: none
        if (!out || outSize < 8)
            return FALSE;
        ((DWORD *)out)[0] = 0;
        ((DWORD *)out)[1] = 0;
        *returned = 8;
        return TRUE;
    case 0x867: // NVRAM records available
        return put32(out, outSize, returned, 7154);
    case 0x893: // NVRAM geometry: 10 of 4096 addresses
        if (!out || outSize < 8)
            return FALSE;
        ((DWORD *)out)[0] = 10;
        ((DWORD *)out)[1] = 4096;
        *returned = 8;
        return TRUE;
    default:
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }
}

static const WlVirtualDevice g_keychip = {"\\??\\FddDriver", keychip_ioctl, NULL, NULL, NULL, NULL}; // its NT name

// ------------------------------------------------------------------------------
// The board's hardware monitor (segatools' platform/hwmon.c): the CPU's temperature only
// ------------------------------------------------------------------------------

static BOOL hwmon_ioctl(DWORD code, const void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned)
{
    *returned = 0;
    if (code != 0x80006000 || !out || outSize < 4) // CTL_CODE(0x8000, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)
    {
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }
    *(DWORD *)out = 52;
    *returned = 4;
    return TRUE;
}

static const WlVirtualDevice g_hwmon = {"\\\\.\\sghwmonitor", hwmon_ioctl, NULL, NULL, NULL, NULL};

// ------------------------------------------------------------------------------
// The I/O board, Sega 837-15257 "IO4", USB HID (segatools' board/io4.c and games/swdchook/io4.c):
// 64-byte reports. In, ID 01: 8 ADCs (16 bits: the wheel, 0x8000 in the middle and more to the
// right, the accelerator, the brake), 4 rotary encoders, 2 coin counters (the coins << 8), the
// switches of two players (16 bits each), the board's status. Out, ID 10: a command and its data
// (01 and 02 set the board up: status 0x30; 03 clears its status; 04 the lamps). amdaemon finds it
// by its product string. SWDC's switches (amdaemon's config.json): player 1 bit 9 Test, 7 Start,
// 6 Service, 5..2 up, down, left, right, 1 view change; player 2 bits 15..12 the wheel's blue,
// green, red and yellow buttons, 1 and 0 the paddles.
// ------------------------------------------------------------------------------

enum
{
    IN_STEERING,
    IN_GAS,
    IN_BRAKE,
    IN_COIN,
    IN_TEST,
    IN_SERVICE,
    IN_START,
    IN_VIEW,
    IN_UP,
    IN_DOWN,
    IN_LEFT,
    IN_RIGHT,
    IN_PADDLE_LEFT,
    IN_PADDLE_RIGHT,
    IN_BLUE,
    IN_GREEN,
    IN_RED,
    IN_YELLOW,
    IN_COUNT
};

static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 0xFFFF, 0x8000},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 0xFFFF, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 0xFFFF, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"ViewChange", "P1_View", WL_INPUT_SWITCH, 0, 1, 0},
    {"Up", "P1_Up", WL_INPUT_SWITCH, 0, 1, 0},
    {"Down", "P1_Down", WL_INPUT_SWITCH, 0, 1, 0},
    {"Left", "P1_Left", WL_INPUT_SWITCH, 0, 1, 0},
    {"Right", "P1_Right", WL_INPUT_SWITCH, 0, 1, 0},
    {"PaddleLeft", "P1_ShiftDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"PaddleRight", "P1_ShiftUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"Blue", "P1_Blue", WL_INPUT_SWITCH, 0, 1, 0},
    {"Green", "P1_Green", WL_INPUT_SWITCH, 0, 1, 0},
    {"Red", "P1_Red", WL_INPUT_SWITCH, 0, 1, 0},
    {"Yellow", "P1_Yellow", WL_INPUT_SWITCH, 0, 1, 0},
};

static const struct
{
    int input, player, bit;
} g_switches[] = {
    {IN_TEST, 0, 9}, {IN_START, 0, 7}, {IN_SERVICE, 0, 6}, {IN_UP, 0, 5}, {IN_DOWN, 0, 4}, {IN_LEFT, 0, 3}, {IN_RIGHT, 0, 2},
    {IN_VIEW, 0, 1}, {IN_BLUE, 1, 15}, {IN_GREEN, 1, 14}, {IN_RED, 1, 13}, {IN_YELLOW, 1, 12}, {IN_PADDLE_LEFT, 1, 1}, {IN_PADDLE_RIGHT, 1, 0},
};

static volatile LONG g_io4Status = 0x02;
static LONG g_coins;
static int g_coinWas;

static void put16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

// The inputs are the game's process's (its controllers, the agent, the settings menu): it copies them
// every 2 ms into a shared memory that amdaemon's board reads; without the game there (amdaemon
// alone), the board reads amdaemon's own table. The lamps go the other way: amdaemon's board writes
// them there, the game's process sends them to the network outputs
#define SHARED_INPUTS L"Local\\windowsloader-alls-inputs"
#define SHARED_MAX 32

struct SharedInputs
{
    volatile LONG writer; // the game's process id, 0 before it writes
    volatile LONG raw[SHARED_MAX];
    volatile LONG lamps; // the board's general outputs, command 04's first 4 bytes (byte 0 in bits 0-7)
};

// The game's table of inputs: SWDC's (g_inputs) or Initial D Zero's (g_idzInputs)
static WlInput *g_table;
static int g_tableCount;

// The 14 general outputs ("GOUT=14"), as a JVS board's: lamp n is bit 7 - n % 8 of byte n / 8.
// In the attract mode the game lights lamp 0 and puts it out as Start is pressed: Start's, it seems
#define IO4_LAMPS 14
static const char *const g_io4LampNames[IO4_LAMPS] = {"Start (semble-t-il)"};

static SharedInputs *g_shared;

static SharedInputs *shared_inputs(void)
{
    if (!g_shared)
    {
        HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(SharedInputs), SHARED_INPUTS);
        if (map)
            g_shared = (SharedInputs *)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedInputs));
    }
    return g_shared;
}

static DWORD WINAPI inputs_writer(void *unused)
{
    SharedInputs *s = shared_inputs();
    if (!s)
        return 0;
    InterlockedExchange(&s->writer, (LONG)GetCurrentProcessId());
    wl_log("alls: the inputs go to amdaemon's I/O board (shared memory)");
    for (;;)
    {
        for (int i = 0; i < g_tableCount; i++)
            InterlockedExchange(&s->raw[i], (LONG)wl_input_get(&g_table[i]));
        unsigned lamps = (unsigned)s->lamps;
        for (int i = 0; i * 8 < IO4_LAMPS; i++)
            wl_outputs_gpo(i, (unsigned char)(lamps >> 8 * i)); // for BackForceFeeder ([Outputs] OUTPUTS_NETWORK)
        Sleep(2);
    }
}

static int input_raw(int i)
{
    SharedInputs *s = g_shared ? g_shared : shared_inputs();
    return s && s->writer ? (int)s->raw[i] : wl_input_get(&g_table[i]);
}

static void io4_input(unsigned char *r, int length)
{
    if (length < 64)
        return;
    r[0] = 0x01;
    put16(r + 1, (unsigned)input_raw(IN_STEERING));
    put16(r + 3, (unsigned)input_raw(IN_GAS));
    put16(r + 5, (unsigned)input_raw(IN_BRAKE));
    int coin = input_raw(IN_COIN);
    if (coin && !g_coinWas)
        InterlockedIncrement(&g_coins);
    g_coinWas = coin;
    put16(r + 25, (unsigned)(g_coins << 8));
    unsigned buttons[2] = {0, 0};
    for (int i = 0; i < (int)(sizeof g_switches / sizeof g_switches[0]); i++)
        if (input_raw(g_switches[i].input))
            buttons[g_switches[i].player] |= 1u << g_switches[i].bit;
    put16(r + 29, buttons[0]);
    put16(r + 31, buttons[1]);
    r[33] = (unsigned char)g_io4Status;
}

static void io4_output(const unsigned char *r, int length)
{
    if (length < 2 || r[0] != 0x10)
        return;
    static unsigned logged[256];
    if (logged[r[1]]++ < 3)
        wl_log_hex("io4: command", r, length < 8 ? length : 8);
    switch (r[1])
    {
    case 0x01: // communication timeout
    case 0x02: // sampling count
        g_io4Status = 0x30;
        break;
    case 0x03: // board status cleared
        g_io4Status = 0x00;
        break;
    case 0x04: // the general outputs (lamps), to the game's process
        if (length >= 6)
        {
            SharedInputs *s = g_shared ? g_shared : shared_inputs();
            if (s)
                InterlockedExchange(&s->lamps, (LONG)(r[2] | r[3] << 8 | r[4] << 16 | (unsigned)r[5] << 24));
        }
        break;
    default: // 05 PWM, 41 unique outputs: not shown yet
        break;
    }
}

static const WlVirtualHid g_io4 = {
    "\\\\?\\hid#vid_0ca3&pid_0021#7&1a2b3c4d&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}",
    0x0CA3,
    0x0021,
    0x0100,
    0xFFA0, // vendor defined
    0x0001,
    64,
    1, // a report a millisecond: amdaemon finds one waiting at each of its reads
    io4_input,
    L"SEGA",
    L"I/O CONTROL BD;15257;01;90;1831;6679A;00;GOUT=14_ADIN=8,E_ROTIN=4_COININ=2_SWIN=2,E_UQ1=41,6",
    64,
    io4_output,
    1,
};

// ------------------------------------------------------------------------------
// Sega Nu's I/O board (Initial D Arcade Stage Zero): a JVS one, Sega 837-14572, reached through
// the \\.\mxjvs driver (amdaemon's amJvsInitEx; segatools' amex/jvs.c and games/idzhook/jvs.c).
// As amdaemon's config.json reads it: Test the system byte's bit 7; player 1's byte 0 bit 7 Start,
// 6 Service, 5 to 2 up, down, left and right (the menus), 1 the view; player 2's byte 0 bits 5 to 2
// the 6-speed H shifter's up, down, left and right; analog 0 the wheel, 1 the accelerator, 2 the
// brake.
// ------------------------------------------------------------------------------

enum
{
    IDZ_STEERING,
    IDZ_GAS,
    IDZ_BRAKE,
    IDZ_COIN,
    IDZ_TEST,
    IDZ_SERVICE,
    IDZ_START,
    IDZ_VIEW,
    IDZ_UP,
    IDZ_DOWN,
    IDZ_LEFT,
    IDZ_RIGHT,
    IDZ_GEAR1,
    IDZ_GEAR_UP = IDZ_GEAR1 + 6,
    IDZ_GEAR_DOWN,
    IDZ_COUNT
};

static WlInput g_idzInputs[IDZ_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 0xFFFF, 0x8000},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 0xFFFF, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 0xFFFF, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "P1_Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"ViewChange", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"Up", "SelectUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"Down", "SelectDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"Left", "SelectLeft", WL_INPUT_SWITCH, 0, 1, 0},
    {"Right", "SelectRight", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear1", "Gear1", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear2", "Gear2", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear3", "Gear3", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear4", "Gear4", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear5", "Gear5", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear6", "Gear6", WL_INPUT_SWITCH, 0, 1, 0},
    {"GearUp", "GearUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"GearDown", "GearDown", WL_INPUT_SWITCH, 0, 1, 0},
};

static const struct
{
    int input;
    unsigned short bit;
} g_idzSwitches[] = {{IDZ_START, 0x8000}, {IDZ_SERVICE, 0x4000}, {IDZ_UP, 0x2000}, {IDZ_DOWN, 0x1000},
                     {IDZ_LEFT, 0x0800},  {IDZ_RIGHT, 0x0400},   {IDZ_VIEW, 0x0200}};

// The H shifter's gear, 0 (neutral) to 6, on player 2's byte 0: 1 up and left, 2 down and left, 3 up,
// 4 down, 5 up and right, 6 down and right. A real lever stays in its gear: the gear here is kept,
// chosen by Gear1..Gear6 or stepped by GearUp / GearDown
static unsigned short idz_shifter(void)
{
    static int gear, was[8];
    for (int i = 0; i < 8; i++)
    {
        int now = input_raw(IDZ_GEAR1 + i);
        if (now && !was[i])
        {
            if (i < 6)
                gear = i + 1;
            else if (i == 6 && gear < 6)
                gear++;
            else if (i == 7 && gear > 0)
                gear--;
        }
        was[i] = now;
    }
    static const unsigned short bits[7] = {0, 0x2800, 0x1800, 0x2000, 0x1000, 0x2400, 0x1400};
    return bits[gear];
}

static void idz_jvs_state(WlJvsState *s)
{
    memset(s, 0, sizeof *s);
    s->system = input_raw(IDZ_TEST) ? 0x80 : 0;
    for (int i = 0; i < (int)(sizeof g_idzSwitches / sizeof g_idzSwitches[0]); i++)
        if (input_raw(g_idzSwitches[i].input))
            s->player[0] |= g_idzSwitches[i].bit;
    s->player[1] = idz_shifter();
    int coin = input_raw(IDZ_COIN);
    if (coin && !g_coinWas)
        InterlockedIncrement(&g_coins);
    g_coinWas = coin;
    s->coins[0] = (unsigned short)g_coins;
    s->analog[0] = (unsigned short)input_raw(IDZ_STEERING);
    s->analog[1] = (unsigned short)input_raw(IDZ_GAS);
    s->analog[2] = (unsigned short)input_raw(IDZ_BRAKE);
}

static WlJvsBoard g_idzJvs = {"\\\\.\\mxjvs", "SEGA ENTERPRISES,LTD.;I/O BD JVS;837-14572;Ver1.00;2005/10", 2, 2, 2, 8, 10, 20, idz_jvs_state, NULL};

// ------------------------------------------------------------------------------
// Sega Nu's AMEX board (segatools' amex/sram.c, eeprom.c, ds.c and gpio.c), found by its device
// interfaces; each driver tells its version (0x80006000: 0x0100):
// - the battery-backed SRAM, 512 KB kept in wl_sram.bin beside the game, read and written as a
//   disk of 4-byte sectors;
// - the EEPROM, 8 KB kept in wl_eeprom.bin, a disk of 7 KB in 1-byte sectors;
// - the DS chip, the board's identity: a 32-byte record (its CRC-32, the region, the serial
//   number), whatever the sector read;
// - the GPIO: the DIP switches (1 on: the first cabinet, whose settings are
//   configDHCP_Final_EX_ST1.json), the board's two push buttons (Test and Service: those of the
//   JVS board are used), its LEDs.
// ------------------------------------------------------------------------------

static const GUID g_sramGuid = {0x741B5FCA, 0x4635, 0x4443, {0xA7, 0xA0, 0x57, 0xCA, 0x7B, 0x50, 0x6A, 0x49}};
static const GUID g_eepromGuid = {0xB7970F0C, 0x31C4, 0x45FF, {0x96, 0x18, 0x0A, 0x24, 0x00, 0x94, 0xB2, 0x71}};
static const GUID g_dsGuid = {0x279A9F67, 0x348F, 0x41C9, {0xA4, 0xC4, 0xDF, 0xDB, 0x8A, 0xE8, 0xE5, 0xE0}};
static const GUID g_gpioGuid = {0xE9A26688, 0xF522, 0x44FA, {0xBF, 0xEE, 0x59, 0xDD, 0x16, 0x15, 0x56, 0x6C}};
static const GUID g_jvsGuid = {0xDB6BBB45, 0xCC96, 0x4288, {0xAA, 0x00, 0x6C, 0x00, 0xD7, 0x67, 0xBD, 0xBF}};

struct NvFile
{
    const wchar_t *name;
    LONGLONG size;
    HANDLE file;
};

static NvFile g_sramNv = {L"wl_sram.bin", 0x80000, INVALID_HANDLE_VALUE};
static NvFile g_eepromNv = {L"wl_eeprom.bin", 0x2000, INVALID_HANDLE_VALUE};

static HANDLE nv_file(NvFile *nv)
{
    if (nv->file == INVALID_HANDLE_VALUE)
    {
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%s\\%s", g_root, nv->name);
        wl_busy_enter();
        nv->file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
        LARGE_INTEGER size;
        if (nv->file != INVALID_HANDLE_VALUE && GetFileSizeEx(nv->file, &size) && size.QuadPart < nv->size)
        {
            LARGE_INTEGER end;
            end.QuadPart = nv->size;
            SetFilePointerEx(nv->file, end, NULL, FILE_BEGIN);
            SetEndOfFile(nv->file);
        }
        wl_busy_leave();
    }
    return nv->file;
}

static BOOL nv_io(NvFile *nv, void *buffer, DWORD size, DWORD *done, LONGLONG offset, int write)
{
    HANDLE f = nv_file(nv);
    *done = 0;
    if (f == INVALID_HANDLE_VALUE || offset >= nv->size)
        return f != INVALID_HANDLE_VALUE;
    if (offset + size > nv->size)
        size = (DWORD)(nv->size - offset);
    OVERLAPPED ov;
    memset(&ov, 0, sizeof ov);
    ov.Offset = (DWORD)offset;
    wl_busy_enter();
    BOOL ok = write ? WriteFile(f, buffer, size, done, &ov) : ReadFile(f, buffer, size, done, &ov);
    wl_busy_leave();
    return ok;
}

static BOOL sram_read(void *buffer, DWORD size, DWORD *read, LONGLONG offset)
{
    return nv_io(&g_sramNv, buffer, size, read, offset, 0);
}

static BOOL sram_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    return nv_io(&g_sramNv, (void *)buffer, size, written, offset, 1);
}

static BOOL eeprom_read(void *buffer, DWORD size, DWORD *read, LONGLONG offset)
{
    return nv_io(&g_eepromNv, buffer, size, read, offset, 0);
}

static BOOL eeprom_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    return nv_io(&g_eepromNv, (void *)buffer, size, written, offset, 1);
}

// The version, the geometry (cylinders x tracks x sectors of n bytes), the DS's setup (0x80006004)
static BOOL amex_ioctl(DWORD code, void *out, DWORD outSize, DWORD *returned, LONGLONG cylinders, DWORD tracks, DWORD sectors, DWORD bytes)
{
    *returned = 0;
    if (code == 0x80006000 && out && outSize >= 2)
    {
        *(WORD *)out = 0x0100;
        *returned = 2;
        return TRUE;
    }
    if (code == IOCTL_DISK_GET_DRIVE_GEOMETRY && out && outSize >= sizeof(DISK_GEOMETRY))
    {
        DISK_GEOMETRY *g = (DISK_GEOMETRY *)out;
        memset(g, 0, sizeof *g);
        g->Cylinders.QuadPart = cylinders;
        g->MediaType = tracks > 1 ? FixedMedia : Unknown;
        g->TracksPerCylinder = tracks;
        g->SectorsPerTrack = sectors;
        g->BytesPerSector = bytes;
        *returned = sizeof *g;
        return TRUE;
    }
    SetLastError(ERROR_INVALID_FUNCTION);
    return FALSE;
}

static BOOL sram_ioctl(DWORD code, const void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned)
{
    return amex_ioctl(code, out, outSize, returned, 0x80000 / 4, 1, 1, 4);
}

static BOOL eeprom_ioctl(DWORD code, const void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned)
{
    return amex_ioctl(code, out, outSize, returned, 1, 224, 32, 1);
}

static unsigned crc32(const unsigned char *p, int n)
{
    unsigned c = 0xFFFFFFFF;
    while (n-- > 0)
    {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = c >> 1 ^ (0xEDB88320 & (0 - (c & 1)));
    }
    return ~c;
}

static BOOL ds_ioctl(DWORD code, const void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned)
{
    *returned = 0;
    if (code == 0x80006004) // set up
        return TRUE;
    if (code == 0x80006010 && out && outSize >= 32) // a sector: the record
    {
        unsigned char *r = (unsigned char *)out;
        memset(r, 0, 32);
        r[8] = 4; // the region: 1 Japan, 4 export (amdaemon's "EXP")
        memcpy(r + 9, "AAVE-01A99999999", 16); // segatools' default serial number
        *(unsigned *)r = crc32(r + 4, 28);
        *returned = 32;
        return TRUE;
    }
    return amex_ioctl(code, out, outSize, returned, 1, 1, 2, 32);
}

static BOOL gpio_ioctl(DWORD code, const void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned)
{
    *returned = 0;
    switch (code)
    {
    case 0x8000A004: // the LEDs
        return TRUE;
    case 0x80006008: // the push buttons: bit 0 Test, 1 Service
    case 0x8000600C: // the DIP switches
        if (!out || outSize < 4)
            break;
        *(DWORD *)out = code == 0x8000600C ? 0x01 : 0;
        *returned = 4;
        return TRUE;
    case 0x80006014: // what the board has: a byte, then 32 ports (a byte, their count, their type)
        if (!out || outSize < 129)
            break;
        {
            unsigned char *p = (unsigned char *)out;
            memset(p, 0, 129);
            static const unsigned char ports[3][2] = {{2, 1}, {8, 2}, {2, 3}}; // 2 LEDs, 8 DIP switches, 2 push buttons
            for (int i = 0; i < 3; i++)
            {
                p[1 + 4 * i + 1] = ports[i][0];
                p[1 + 4 * i + 2] = ports[i][1];
            }
            *returned = 129;
        }
        return TRUE;
    }
    SetLastError(ERROR_INVALID_FUNCTION);
    return FALSE;
}

static const WlVirtualDevice g_nuSram = {"\\\\.\\amexsram", sram_ioctl, sram_read, sram_write, NULL, NULL};
static const WlVirtualDevice g_nuEeprom = {"\\\\.\\amexeeprom", eeprom_ioctl, eeprom_read, eeprom_write, NULL, NULL};
static const WlVirtualDevice g_nuDs = {"\\\\.\\amexds", ds_ioctl, NULL, NULL, NULL, NULL};
static const WlVirtualDevice g_nuGpio = {"\\\\.\\amexgpio", gpio_ioctl, NULL, NULL, NULL, NULL};

// ------------------------------------------------------------------------------
// The VFD of the e-money reader, Futaba GP1232A02A on COM4 (segatools' board/vfd.c): text and
// pictures written to it; it answers only its version request (1B 5B): 02 "01.20" 03
// ------------------------------------------------------------------------------

static unsigned char g_vfdAnswer[16];
static int g_vfdAnswerLength;
static CRITICAL_SECTION g_vfdLock;

static BOOL vfd_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    const unsigned char *b = (const unsigned char *)buffer;
    *written = size;
    EnterCriticalSection(&g_vfdLock);
    for (DWORD i = 0; i + 1 < size; i++)
        if (b[i] == 0x1B && b[i + 1] == 0x5B)
        {
            static const unsigned char version[] = {0x02, '0', '1', '.', '2', '0', 0x03};
            memcpy(g_vfdAnswer, version, sizeof version);
            g_vfdAnswerLength = sizeof version;
        }
    LeaveCriticalSection(&g_vfdLock);
    static int logged;
    if (logged++ < 10)
        wl_log_hex("vfd: written", buffer, size < 16 ? size : 16);
    return TRUE;
}

static BOOL vfd_read(void *buffer, DWORD size, DWORD *read, LONGLONG offset)
{
    EnterCriticalSection(&g_vfdLock);
    DWORD n = (DWORD)g_vfdAnswerLength < size ? (DWORD)g_vfdAnswerLength : size;
    memcpy(buffer, g_vfdAnswer, n);
    memmove(g_vfdAnswer, g_vfdAnswer + n, g_vfdAnswerLength - n);
    g_vfdAnswerLength -= n;
    LeaveCriticalSection(&g_vfdLock);
    *read = n;
    return TRUE;
}

static DWORD vfd_modem(void)
{
    return MS_CTS_ON | MS_DSR_ON;
}

static DWORD vfd_waiting(void)
{
    return (DWORD)g_vfdAnswerLength;
}

static WlVirtualDevice g_vfd = {"COM4", NULL, vfd_read, vfd_write, vfd_modem, vfd_waiting};

// ------------------------------------------------------------------------------
// The card reader, Sega 837-15396 "Aime" (gen 3) on COM3, Initial D Zero's 837-15286 on COM10,
// 115200 bauds (segatools' board/sg-frame.c, sg-nfc.c, sg-led.c). Frames: E0, then the length
// (the bytes up to the checksum), the address (0 the NFC reader, 8 its LEDs), a sequence number,
// the command, the payload's length (an answer: its status first), the payload, an 8-bit sum; D0
// escapes D0 and E0 (the byte minus 1 follows). No card is ever on it yet: a poll finds none.
// ------------------------------------------------------------------------------

static unsigned char g_aimeIn[512], g_aimeOut[1024];
static int g_aimeInLength, g_aimeOutLength;
static CRITICAL_SECTION g_aimeLock;

static void aime_put(unsigned char b)
{
    if (g_aimeOutLength + 2 > (int)sizeof g_aimeOut)
        return;
    if (b == 0xE0 || b == 0xD0)
    {
        g_aimeOut[g_aimeOutLength++] = 0xD0;
        b--;
    }
    g_aimeOut[g_aimeOutLength++] = b;
}

static void aime_answer(const unsigned char *req, int status, const void *payload, int n)
{
    unsigned char h[6] = {(unsigned char)(6 + n), req[1], req[2], req[3], (unsigned char)status, (unsigned char)n};
    unsigned char sum = 0;
    if (g_aimeOutLength < (int)sizeof g_aimeOut)
        g_aimeOut[g_aimeOutLength++] = 0xE0;
    for (int i = 0; i < 6; i++)
    {
        sum += h[i];
        aime_put(h[i]);
    }
    for (int i = 0; i < n; i++)
    {
        sum += ((const unsigned char *)payload)[i];
        aime_put(((const unsigned char *)payload)[i]);
    }
    aime_put(sum);
}

// One request (unescaped, its checksum checked): length, address, sequence, command, payload
// length, payload
static void aime_request(const unsigned char *r, int length)
{
    int addr = r[1], cmd = r[3], n = length > 4 ? r[4] : 0;
    static unsigned logged[256];
    if (logged[cmd]++ < 3)
        wl_log("aime: %s command %02x (%d bytes)", addr == 8 ? "LEDs" : "reader", cmd, n);
    if (addr == 8) // the LEDs
    {
        static const char info[] = "000-00000\xFF\x11\x40";
        static const unsigned char zero = 0;
        if (cmd == 0xF0) // board info
            aime_answer(r, 0, info, 12);
        else if (cmd == 0xF5) // reset
            aime_answer(r, 0, &zero, 1);
        else if (cmd != 0x81) // a colour (0x81) gets no answer
            aime_answer(r, 0, NULL, 0);
        return;
    }
    static const unsigned char none = 0;
    static const unsigned char block[16] = {0};
    switch (cmd)
    {
    case 0x62: // reset
        aime_answer(r, 3, NULL, 0);
        break;
    case 0x30: // firmware version, as amdaemon's firmware file (SWDC's update_15396_6728_94.bin, Initial D
               // Zero's update_15286exp_6731_92.bin)
        aime_answer(r, 0, &g_game->aimeFirmware, 1);
        break;
    case 0x32: // hardware version
        aime_answer(r, 0, g_game->aimeHardware, (int)strlen(g_game->aimeHardware));
        break;
    case 0x42: // poll: no card
        aime_answer(r, 0, &none, 1);
        break;
    case 0x52: // a card's block (none)
        aime_answer(r, 0, block, 16);
        break;
    case 0x61: // hex data
        aime_answer(r, n == 0x2B ? 0x20 : 0, NULL, 0);
        break;
    default: // 40/41 radio on/off, 43 select, 50/51/54/55 keys, 60 update mode...
        aime_answer(r, 0, NULL, 0);
        break;
    }
}

static BOOL aime_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    const unsigned char *b = (const unsigned char *)buffer;
    *written = size;
    EnterCriticalSection(&g_aimeLock);
    static int escaped;
    for (DWORD i = 0; i < size; i++)
    {
        unsigned char c = b[i];
        if (c == 0xE0) // a frame starts
        {
            g_aimeInLength = 0;
            escaped = 0;
            continue;
        }
        if (c == 0xD0)
        {
            escaped = 1;
            continue;
        }
        if (escaped)
        {
            c++;
            escaped = 0;
        }
        if (g_aimeInLength < (int)sizeof g_aimeIn)
            g_aimeIn[g_aimeInLength++] = c;
        // Complete: the length's bytes then the checksum
        if (g_aimeInLength >= 2 && g_aimeInLength == g_aimeIn[0] + 1)
        {
            unsigned char sum = 0;
            for (int k = 0; k < g_aimeInLength - 1; k++)
                sum += g_aimeIn[k];
            if (sum == g_aimeIn[g_aimeInLength - 1] && g_aimeInLength >= 5)
                aime_request(g_aimeIn, g_aimeInLength - 1);
            g_aimeInLength = 0;
        }
    }
    LeaveCriticalSection(&g_aimeLock);
    return TRUE;
}

static BOOL aime_read(void *buffer, DWORD size, DWORD *read, LONGLONG offset)
{
    EnterCriticalSection(&g_aimeLock);
    DWORD n = (DWORD)g_aimeOutLength < size ? (DWORD)g_aimeOutLength : size;
    memcpy(buffer, g_aimeOut, n);
    memmove(g_aimeOut, g_aimeOut + n, g_aimeOutLength - n);
    g_aimeOutLength -= n;
    LeaveCriticalSection(&g_aimeLock);
    *read = n;
    return TRUE;
}

static DWORD aime_waiting(void)
{
    return (DWORD)g_aimeOutLength;
}

static WlVirtualDevice g_aime = {"COM3", NULL, aime_read, aime_write, vfd_modem, aime_waiting};

// ------------------------------------------------------------------------------
// The game's LED board, Sega 837-15070 on COM2 (segatools' board/led15070.c, led15070-frame.c):
// frames E0, destination, source (the host 01, the board 00 for SWDC), the length, the data, an 8-bit sum
// (after E0); the same escaping as the reader's. An answer: status 01, the command, report 01,
// its data. After a reset (10) every command is answered. Used in Cabinet mode only.
// ------------------------------------------------------------------------------

static unsigned char g_ledIn[300], g_ledOut[512];
static int g_ledInLength, g_ledOutLength, g_ledAcks;
static unsigned char g_ledHost = 0x01, g_ledBoard = 0x11; // the last request's source and destination
static CRITICAL_SECTION g_ledLock;

static void led_answer(int cmd, const unsigned char *data, int n)
{
    unsigned char f[48] = {g_ledHost, g_ledBoard, (unsigned char)(3 + n), 0x01, (unsigned char)cmd, 0x01};
    memcpy(f + 6, data, n);
    unsigned char sum = 0;
    if (g_ledOutLength < (int)sizeof g_ledOut)
        g_ledOut[g_ledOutLength++] = 0xE0;
    for (int i = 0; i < 6 + n; i++)
    {
        sum += f[i];
        if (g_ledOutLength + 2 > (int)sizeof g_ledOut)
            return;
        unsigned char b = f[i];
        if (b == 0xE0 || b == 0xD0)
        {
            g_ledOut[g_ledOutLength++] = 0xD0;
            b--;
        }
        g_ledOut[g_ledOutLength++] = b;
    }
    if (sum == 0xE0 || sum == 0xD0)
    {
        g_ledOut[g_ledOutLength++] = 0xD0;
        sum--;
    }
    g_ledOut[g_ledOutLength++] = sum;
}

// The lamps, sent to the network outputs as each update (3C) shows them: led0_r to led8_b, the 9 RGB
// LEDs SWDC lights (0-255), and fet0 to fet2 its 3 power outputs (39). The colours: 31 one LED
// (index, R, G, B), 32 flashing and 33 fading ones (first, count, step, R, G, B, speed), the
// flashing and the fading not played: their colour shown as it is set
#define LED_BOARD_LEDS 32
#define LED_SWDC_LEDS 9
static unsigned char g_ledColour[LED_BOARD_LEDS][3], g_ledFet[3];
static int g_ledValue = -1; // led0_r's number in the network outputs, -1 when not declared

static void led_declare(void)
{
    static const char *const colour[3] = {"r", "g", "b"}, *const label[3] = {"rouge", "vert", "bleu"};
    for (int led = 0; led < LED_SWDC_LEDS; led++)
        for (int c = 0; c < 3; c++)
        {
            char name[32], about[128];
            snprintf(name, sizeof name, "led%d_%s", led, colour[c]);
            snprintf(about, sizeof about, "Carte LED 837-15070 (mode Cabinet) : LED %d, %s, 0 éteinte à 255", led + 1, label[c]);
            int i = wl_outputs_declare_value(name, "intensité (0-255)", about);
            if (!led && !c)
                g_ledValue = i;
        }
    for (int f = 0; f < 3; f++)
    {
        char name[32], about[128];
        snprintf(name, sizeof name, "fet%d", f);
        snprintf(about, sizeof about, "Carte LED 837-15070 (mode Cabinet) : sortie de puissance %d (commande 39), 0 à 255", f + 1);
        wl_outputs_declare_value(name, "intensité (0-255)", about);
    }
}

static void led_lamps(int cmd, const unsigned char *p, int n)
{
    if (cmd == 0x31 && n >= 4 && p[0] < LED_BOARD_LEDS)
        memcpy(g_ledColour[p[0]], p + 1, 3);
    else if ((cmd == 0x32 || cmd == 0x33) && n >= 6)
    {
        int step = p[2] ? p[2] : 1;
        for (int i = p[0]; i < p[0] + p[1] && i < LED_BOARD_LEDS; i += step)
            memcpy(g_ledColour[i], p + 3, 3);
    }
    else if (cmd == 0x39 && n >= 3)
        memcpy(g_ledFet, p, 3);
    else if (cmd == 0x3C && g_ledValue >= 0)
    {
        for (int i = 0; i < LED_SWDC_LEDS * 3; i++)
            wl_outputs_value(g_ledValue + i, g_ledColour[i / 3][i % 3]);
        for (int f = 0; f < 3; f++)
            wl_outputs_value(g_ledValue + LED_SWDC_LEDS * 3 + f, g_ledFet[f]);
    }
}

// destination, source, length, command, payload
static void led_request(const unsigned char *r, int length)
{
    int cmd = r[3];
    g_ledBoard = r[0]; // SWDC's board answers as 00
    g_ledHost = r[1];
    static unsigned logged[256];
    if (logged[cmd]++ < 3)
        wl_log("led: command %02x (%d bytes)", cmd, r[2]);
    led_lamps(cmd, r + 4, length - 4);
    static const unsigned char info[10] = {'1', '5', '0', '7', '0', '-', '0', '4', 0xFF, 0x90}; // board 15070-04, firmware 90
    static const unsigned char status[4] = {0}, sum[2] = {0xAD, 0xF7}, protocol[3] = {1, 1, 4};
    switch (cmd)
    {
    case 0x10: // reset: the commands answered from now on
        g_ledAcks = 1;
        led_answer(cmd, NULL, 0);
        break;
    case 0xF0:
        led_answer(cmd, info, 10);
        break;
    case 0xF1:
        led_answer(cmd, status, 4);
        break;
    case 0xF2:
        led_answer(cmd, sum, 2);
        break;
    case 0xF3:
        led_answer(cmd, protocol, 3);
        break;
    case 0x7C: // EEPROM read: 0
    {
        static const unsigned char zero = 0;
        led_answer(cmd, &zero, 1);
        break;
    }
    case 0x7D:
        g_ledAcks = 1;
        led_answer(cmd, NULL, 0);
        break;
    case 0x7E:
        g_ledAcks = 0;
        break;
    default:
        if (g_ledAcks)
            led_answer(cmd, length > 5 && cmd == 0x7B ? r + 4 : NULL, cmd == 0x7B ? 2 : 0);
        break;
    }
}

static BOOL led_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    const unsigned char *b = (const unsigned char *)buffer;
    *written = size;
    EnterCriticalSection(&g_ledLock);
    static int escaped;
    for (DWORD i = 0; i < size; i++)
    {
        unsigned char c = b[i];
        if (c == 0xE0)
        {
            g_ledInLength = 0;
            escaped = 0;
            continue;
        }
        if (c == 0xD0)
        {
            escaped = 1;
            continue;
        }
        if (escaped)
        {
            c++;
            escaped = 0;
        }
        if (g_ledInLength < (int)sizeof g_ledIn)
            g_ledIn[g_ledInLength++] = c;
        // destination, source, length, the length's bytes, the sum
        if (g_ledInLength >= 4 && g_ledInLength == g_ledIn[2] + 4)
        {
            unsigned char sum = 0;
            for (int k = 0; k < g_ledInLength - 1; k++)
                sum += g_ledIn[k];
            if (sum == g_ledIn[g_ledInLength - 1])
                led_request(g_ledIn, g_ledInLength - 1);
            g_ledInLength = 0;
        }
    }
    LeaveCriticalSection(&g_ledLock);
    return TRUE;
}

static BOOL led_read(void *buffer, DWORD size, DWORD *read, LONGLONG offset)
{
    EnterCriticalSection(&g_ledLock);
    DWORD n = (DWORD)g_ledOutLength < size ? (DWORD)g_ledOutLength : size;
    memcpy(buffer, g_ledOut, n);
    memmove(g_ledOut, g_ledOut + n, g_ledOutLength - n);
    g_ledOutLength -= n;
    LeaveCriticalSection(&g_ledLock);
    *read = n;
    return TRUE;
}

static DWORD led_waiting(void)
{
    return (DWORD)g_ledOutLength;
}

static WlVirtualDevice g_led = {"COM2", NULL, led_read, led_write, vfd_modem, led_waiting};

// ------------------------------------------------------------------------------
// The dashboard, Sega 838-15415 "Indicator BD" on a USB serial port (COM20 to COM29, the first
// found in Windows' list), used in Cabinet mode only: a text line ended by \r\n, its fields
// separated by ':' (BP_DashBoard), "pr" alone about every 17 ms, the values with it about every
// 35 ms in a race: "sp1*0*2*" the speed (three digits, each followed by '*'; '*' for a blank
// digit), "gr2" the gear, "le11110000" 8 lamps (a bar filled from the left with the revs, it
// seems), "rp******" the revs (blank in a race). Each line is answered "ps" and the wheel's six
// buttons, '0' or '1' (yellow, red, green, blue, right paddle, left paddle: the input test's).
// The values go to the network outputs: speed, gear, rpm, dash_lamp0 to dash_lamp7
// ------------------------------------------------------------------------------

static char g_dashLine[256], g_dashOut[256];
static int g_dashLineLength, g_dashOutLength;
static CRITICAL_SECTION g_dashLock;
static int g_dashValue = -1; // speed's number in the network outputs, then gear, rpm and the lamps

#define DASH_LAMPS 8

static void dash_declare(void)
{
    g_dashValue = wl_outputs_declare_value("speed", "nombre (0-999)", "Tableau de bord (mode Cabinet) : la vitesse affichée, km/h");
    wl_outputs_declare_value("gear", "nombre", "Tableau de bord (mode Cabinet) : le rapport affiché");
    wl_outputs_declare_value("rpm", "nombre", "Tableau de bord (mode Cabinet) : le régime affiché (vide en course : 0)");
    for (int i = 0; i < DASH_LAMPS; i++)
    {
        char name[32], about[128];
        snprintf(name, sizeof name, "dash_lamp%d", i);
        snprintf(about, sizeof about, "Tableau de bord (mode Cabinet) : lampe %d de la barre (allumée de gauche à droite avec le régime, semble-t-il)",
                 i + 1);
        wl_outputs_declare_value(name, "bit (0/1)", about);
    }
}

// A field's digits, the blanks ('*') left out: "1*0*2*" 102, "**9*7*" 97, "******" 0
static int dash_number(const char *p, int n)
{
    int v = 0;
    for (int i = 0; i < n && p[i] && p[i] != ':' && p[i] != '\r'; i++)
        if (p[i] >= '0' && p[i] <= '9')
            v = v * 10 + p[i] - '0';
    return v;
}

static void dash_line(const char *line, int length)
{
    if (g_dashValue < 0)
        return;
    for (const char *p = line; p < line + length;)
    {
        const char *end = (const char *)memchr(p, ':', line + length - p);
        int n = (int)((end ? end : line + length) - p);
        if (n > 2 && !memcmp(p, "sp", 2))
            wl_outputs_value(g_dashValue, dash_number(p + 2, n - 2));
        else if (n > 2 && !memcmp(p, "gr", 2))
            wl_outputs_value(g_dashValue + 1, dash_number(p + 2, n - 2));
        else if (n > 2 && !memcmp(p, "rp", 2))
            wl_outputs_value(g_dashValue + 2, dash_number(p + 2, n - 2));
        else if (n > 2 && !memcmp(p, "le", 2))
            for (int i = 0; i < DASH_LAMPS && i < n - 2; i++)
                wl_outputs_value(g_dashValue + 3 + i, p[2 + i] == '1');
        if (!end)
            break;
        p = end + 1;
    }
}

static BOOL dash_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    const char *b = (const char *)buffer;
    *written = size;
    EnterCriticalSection(&g_dashLock);
    static int logged;
    if (logged++ < 5)
        wl_log("dashboard: \"%.*s\"", (int)size, b);
    for (DWORD i = 0; i < size; i++)
    {
        if (g_dashLineLength < (int)sizeof g_dashLine - 1)
            g_dashLine[g_dashLineLength++] = b[i];
        if (b[i] != '\n')
            continue;
        dash_line(g_dashLine, g_dashLineLength - 2 > 0 ? g_dashLineLength - 2 : 0);
        g_dashLineLength = 0;
        static const int buttons[6] = {IN_YELLOW, IN_RED, IN_GREEN, IN_BLUE, IN_PADDLE_RIGHT, IN_PADDLE_LEFT};
        char status[11] = "ps";
        for (int k = 0; k < 6; k++)
            status[2 + k] = input_raw(buttons[k]) ? '1' : '0';
        memcpy(status + 8, "\r\n", 3);
        if (g_dashOutLength + 10 <= (int)sizeof g_dashOut)
        {
            memcpy(g_dashOut + g_dashOutLength, status, 10);
            g_dashOutLength += 10;
        }
    }
    LeaveCriticalSection(&g_dashLock);
    return TRUE;
}

static BOOL dash_read(void *buffer, DWORD size, DWORD *read, LONGLONG offset)
{
    EnterCriticalSection(&g_dashLock);
    DWORD n = (DWORD)g_dashOutLength < size ? (DWORD)g_dashOutLength : size;
    memcpy(buffer, g_dashOut, n);
    memmove(g_dashOut, g_dashOut + n, g_dashOutLength - n);
    g_dashOutLength -= n;
    LeaveCriticalSection(&g_dashLock);
    *read = n;
    return TRUE;
}

static DWORD dash_waiting(void)
{
    return (DWORD)g_dashOutLength;
}

static WlVirtualDevice g_dash = {"COM20", NULL, dash_read, dash_write, vfd_modem, dash_waiting};

// ------------------------------------------------------------------------------

// The board's serial number is the PC's name (segatools' platform/pcbid.c): 15 characters
#define BOARD_SERIAL "ACAE01A99999999" // made up

static BOOL(WINAPI *oGetComputerNameA)(LPSTR, LPDWORD);
static BOOL WINAPI hGetComputerNameA(LPSTR name, LPDWORD size)
{
    if (!name || !size)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD n = (DWORD)strlen(BOARD_SERIAL);
    if (*size <= n)
    {
        *size = n + 1;
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    memcpy(name, BOARD_SERIAL, n + 1);
    *size = n;
    return TRUE;
}

static WlHook g_daemonHooks[] = {
    WL_HOOK(L"kernel32.dll", GetComputerNameA),
};

// The PC's CD/DVD drives and removable disks hidden from amdaemon and the game: amdaemon looks
// for them among the volumes (FindFirstVolumeW, QueryDosDeviceW's \Device\CdRom, GetDriveTypeW)
// and stops on "Error 0919 DVD Drive Remain Error" (0917 for a USB flash drive), a drive left in
// the cabinet after an install. D:, E: and Y: are the copy's folders (drive()): fixed disks,
// whatever the PC has under these letters (a racecab's DVD drive is its E:)
static int copy_letter(wchar_t c)
{
    c = (wchar_t)towupper(c);
    return c == L'D' || c == L'E' || c == L'Y';
}

static UINT(WINAPI *oGetDriveTypeW)(LPCWSTR);

static int hidden_drive(LPCWSTR root)
{
    UINT type = oGetDriveTypeW ? oGetDriveTypeW(root) : GetDriveTypeW(root);
    if (type != DRIVE_CDROM && type != DRIVE_REMOVABLE)
        return 0;
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 8)
        wl_log("alls: %ls hidden (%s)", root ? root : L"(current drive)", type == DRIVE_CDROM ? "CD/DVD drive" : "removable disk");
    return 1;
}

static UINT WINAPI hGetDriveTypeW(LPCWSTR root)
{
    if (root && root[0] && root[1] == L':' && copy_letter(root[0]))
        return DRIVE_FIXED;
    return hidden_drive(root) ? DRIVE_NO_ROOT_DIR : oGetDriveTypeW(root);
}

static UINT(WINAPI *oGetDriveTypeA)(LPCSTR);
static UINT WINAPI hGetDriveTypeA(LPCSTR root)
{
    wchar_t w[MAX_PATH];
    if (!root || !MultiByteToWideChar(CP_ACP, 0, root, -1, w, MAX_PATH))
        return hGetDriveTypeW(NULL);
    return hGetDriveTypeW(w);
}

static BOOL(WINAPI *oFindNextVolumeW)(HANDLE, LPWSTR, DWORD);
static BOOL WINAPI hFindNextVolumeW(HANDLE find, LPWSTR name, DWORD length)
{
    BOOL ok;
    while ((ok = oFindNextVolumeW(find, name, length)) && hidden_drive(name))
        ;
    return ok;
}

static HANDLE(WINAPI *oFindFirstVolumeW)(LPWSTR, DWORD);
static HANDLE WINAPI hFindFirstVolumeW(LPWSTR name, DWORD length)
{
    HANDLE find = oFindFirstVolumeW(name, length);
    if (find != INVALID_HANDLE_VALUE && hidden_drive(name) && !hFindNextVolumeW(find, name, length))
    {
        DWORD error = GetLastError();
        FindVolumeClose(find);
        SetLastError(error);
        return INVALID_HANDLE_VALUE;
    }
    return find;
}

static DWORD(WINAPI *oQueryDosDeviceW)(LPCWSTR, LPWSTR, DWORD);
static DWORD WINAPI hQueryDosDeviceW(LPCWSTR name, LPWSTR target, DWORD max)
{
    DWORD n = oQueryDosDeviceW(name, target, max);
    if (name && n && target && !_wcsnicmp(target, L"\\Device\\CdRom", 13))
    {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return 0;
    }
    return n;
}

static WlHook g_driveHooks[] = {
    WL_HOOK(L"kernel32.dll", GetDriveTypeW),
    WL_HOOK(L"kernel32.dll", GetDriveTypeA),
    WL_HOOK(L"kernel32.dll", FindFirstVolumeW),
    WL_HOOK(L"kernel32.dll", FindNextVolumeW),
    WL_HOOK(L"kernel32.dll", QueryDosDeviceW),
};

// ------------------------------------------------------------------------------

// The copy's folder: the first one up from the program's holding game.bat
static int find_root(const wchar_t *exePath)
{
    wcsncpy_s(g_root, MAX_PATH, exePath, _TRUNCATE);
    for (int up = 0; up < 8; up++)
    {
        wchar_t *slash = wcsrchr(g_root, L'\\');
        if (!slash)
            return 0;
        *slash = 0;
        wchar_t bat[MAX_PATH];
        swprintf(bat, MAX_PATH, L"%s\\game.bat", g_root);
        if (GetFileAttributesW(bat) != INVALID_FILE_ATTRIBUTES)
            return 1;
    }
    return 0;
}

// Sega Nu's: the program's folder, when the game is there (amdaemon beside it)
static int nu_root(const wchar_t *exePath)
{
    wcsncpy_s(g_root, MAX_PATH, exePath, _TRUNCATE);
    wchar_t *slash = wcsrchr(g_root, L'\\');
    if (!slash)
        return 0;
    *slash = 0;
    wchar_t game[MAX_PATH];
    swprintf(game, MAX_PATH, L"%s\\%s", g_root, g_games[1].exe);
    return GetFileAttributesW(game) != INVALID_FILE_ATTRIBUTES;
}

static void drive(const char *letter, const wchar_t *folder)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s\\%s", g_root, folder);
    CreateDirectoryW(path, NULL);
    char to[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, path, -1, to, sizeof to, NULL, NULL);
    wl_files_redirect(letter, to);
}

// amdaemon's settings for a cabinet alone, beside the game's config.json: the services that need
// Sega's network or other cabinets off (ALL.Net's accounting, the network delivery, the electronic
// money, ABaaS's logs, the LAN install), and the language of its own messages (errors, warnings:
// am_resources). The launcher gives amdaemon this file (-c); written here, in amdaemon, before it
// reads its settings. The game's own texts are Japanese whatever the language.

// amdaemon's languages, those of its resources (am_resources\error.<language>.dll), by their English names
static const WlOptionChoice g_languages[] = {
    {"ENGLISH", "English"},
    {"JAPANESE", "Japanese"},
    {"CHINESE", "Chinese"},
    {NULL, NULL},
};

// The cabinet the game plays: the launcher gives it -launch= (launcher.cpp, g_gameOptionArgs)
static const WlOptionChoice g_cabinets[] = {
    {"CABINET", "Cabinet"},
    {"MINI", "MiniCabinet"},
    {NULL, NULL},
};

static const WlGameOption g_swdcOptions[] = {
    {"CABINET", "Type de borne",
     "Cabinet : la borne complète, avec son tableau de bord (les boutons du volant, la vitesse) et sa carte LED, et leurs sorties "
     "réseau. MiniCabinet : la petite borne, comme sous TeknoParrot, sans eux. Pris au prochain lancement du jeu.",
     WL_OPTION_CHOICE, g_cabinets, 0, 0, "CABINET", 1, NULL},
    {"LANGUAGE", "Langue des messages d'amdaemon",
     "Les écrans d'erreur et d'avertissement de la plateforme (Error 4103...). Les textes du jeu restent en japonais.",
     WL_OPTION_CHOICE, g_languages, 0, 0, "ENGLISH", 1, NULL},
};

// Initial D Zero's: the same language of amdaemon's messages
static const WlGameOption g_idzOptions[] = {
    {"LANGUAGE", "Langue des messages d'amdaemon", "Les écrans d'erreur et d'avertissement de la plateforme.", WL_OPTION_CHOICE, g_languages,
     0, 0, "ENGLISH", 1, NULL},
};

static void amdaemon_settings(void)
{
    char language[32];
    wl_game_option("LANGUAGE", language, sizeof language);
    const char *name = "english";
    if (!_stricmp(language, "JAPANESE"))
        name = "japanese";
    else if (!_stricmp(language, "CHINESE"))
        name = "chinese";
    wchar_t path[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, path);
    if (!n || n + 32 > MAX_PATH)
        return;
    swprintf(path + n, MAX_PATH - n, L"windowsloader-%S-amdaemon.json", g_game->name);
    char json[512];
    int len = snprintf(json, sizeof json,
                       "{\r\n"
                       "    \"common\" : { \"max_player\" : 1, \"language\" : \"%s\", \"custom_resource_id\" : [\"common\"] },\r\n"
                       "    \"allnet_accounting\" : { \"enable\" : false },\r\n"
                       "    \"net_delivery\" : { \"enable\" : false },\r\n"
                       "    \"emoney\" : { \"enable\" : false },\r\n"
                       "    \"abaas_log\" : { \"enable\" : false },\r\n"
                       "    \"lan_install\" : { \"enable\" : false }\r\n"
                       "}\r\n",
                       name);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    if (f == INVALID_HANDLE_VALUE || !WriteFile(f, json, (DWORD)len, &written, NULL) || (int)written != len)
        wl_log("alls: amdaemon's settings could not be written to the temporary folder");
    else
        wl_log("alls: amdaemon's settings for a cabinet alone (language: %s)", name);
    if (f != INVALID_HANDLE_VALUE)
        CloseHandle(f);
}

// Sega Nu's Initial D Arcade Stage Zero: amdaemon with the JVS board, the game with its controls,
// the shop's server box (ServerBoxD8_Nu_x64.exe, the cabinets' link to the game's online server)
static void nu_setup(int daemon, int box)
{
    // The copy's drives as TeknoParrot keeps them: Y:\SDDF the game's data, E: AMFS (the system's files)
    wchar_t tp[MAX_PATH];
    swprintf(tp, MAX_PATH, L"%s\\TeknoParrot\\AMFS", g_root);
    int teknoparrot = GetFileAttributesW(tp) != INVALID_FILE_ATTRIBUTES;
    drive("E:", teknoparrot ? L"TeknoParrot\\AMFS" : L"AMFS");
    drive("Y:", teknoparrot ? L"TeknoParrot\\Y" : L"Y");
    wl_hooks_add(g_driveHooks, sizeof g_driveHooks / sizeof g_driveHooks[0]);
    static const unsigned char mac[6] = {0x00, 0xD0, 0xF1, 0x01, 0x02, 0x04};
    wl_netenv_fake(0xC0A8AC0B, 0xC0A8AC01, mac);
    g_table = g_idzInputs;
    g_tableCount = IDZ_COUNT;
    wl_inputs_set_table(g_idzInputs, IDZ_COUNT);
    if (daemon)
    {
        wl_allnet_start(0xC0A8AC01);
        system_property();
        wl_system_virtual_dll("$amvideo", g_amvideo, 4);
        wl_vdev_register(&g_keychip);
        wl_vdev_register(&g_hwmon);
        wl_hooks_add(g_daemonHooks, sizeof g_daemonHooks / sizeof g_daemonHooks[0]);
        wl_jvs_register(&g_idzJvs);
        wl_vdev_interface(&g_jvsGuid, g_idzJvs.port);
        wl_vdev_register(&g_nuSram);
        wl_vdev_interface(&g_sramGuid, g_nuSram.name);
        wl_vdev_register(&g_nuEeprom);
        wl_vdev_interface(&g_eepromGuid, g_nuEeprom.name);
        wl_vdev_register(&g_nuDs);
        wl_vdev_interface(&g_dsGuid, g_nuDs.name);
        wl_vdev_register(&g_nuGpio);
        wl_vdev_interface(&g_gpioGuid, g_nuGpio.name);
        InitializeCriticalSection(&g_aimeLock);
        g_aime.name = g_game->aimePort;
        wl_vdev_register(&g_aime);
        wl_vdev_label(g_aime.name, "Lecteur de cartes Aime");
        return;
    }
    // The game and the server box find each other by broadcasts: here, this PC's
    wl_system_loopback_broadcast(1);
    if (box)
    {
        wl_display_hidden(); // its window, a view of the shop's server, is not the cabinet's
        return;
    }
    CloseHandle(CreateThread(NULL, 0, inputs_writer, NULL, 0, NULL));
    // A board of the 837-15070 family on COM11 (the game opens \\.\COM11 at its start): the same
    // frames as SWDC's LED board, its address 11
    InitializeCriticalSection(&g_ledLock);
    g_led.name = "COM11";
    wl_vdev_register(&g_led);
    wl_vdev_label("COM11", "Carte des lampes (837-15070)");
    // The wheel's motor on COM1, opened at the game's start: Sega's drive board, as Initial D 6 to 8's
    wl_driveboard_register("COM1");
}

int wl_game_alls(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    int daemon = !_wcsicmp(base, L"amdaemon.exe"), box = !_wcsicmp(base, L"ServerBoxD8_Nu_x64.exe");
    g_game = NULL;
    if (daemon || box || !_wcsicmp(base, g_games[1].exe))
        g_game = nu_root(exePath) ? &g_games[1] : NULL;
    if (!g_game && (daemon || !_wcsicmp(base, g_games[0].exe)) && find_root(exePath))
        g_game = &g_games[0];
    if (!g_game)
        return 0;
    *name = g_game->name;
    *title = g_game->title;
    wl_log("game: %s (Sega %s), %s", *title, g_game->nu ? "Nu" : "ALLS",
           daemon ? "amdaemon.exe, the platform's service" : box ? "the shop's server box" : "the game");
    if (g_game->nu)
    {
        wl_game_set_boards("Controls on JVS board, Sega 837-14572, JVS 3.0 (mxjvs driver, amdaemon)", "Motor board: (the game)");
        wl_game_set_options(g_idzOptions, (int)(sizeof g_idzOptions / sizeof g_idzOptions[0]));
        if (daemon)
            wl_game_on_settings(amdaemon_settings);
        nu_setup(daemon, box);
        return 1;
    }
    g_table = g_inputs;
    g_tableCount = IN_COUNT;
    wl_game_set_boards("Controls on USB I/O board, Sega 837-15257 \"IO4\" (amdaemon)", "Motor board: (COM1, the game)");
    wl_game_set_options(g_swdcOptions, (int)(sizeof g_swdcOptions / sizeof g_swdcOptions[0]));
    if (daemon)
        wl_game_on_settings(amdaemon_settings);
    drive("D:", L"D");
    drive("E:", L"AMFS");
    drive("Y:", L"Y");
    wl_hooks_add(g_driveHooks, sizeof g_driveHooks / sizeof g_driveHooks[0]);
    // The main cabinet (the only one here): the game knows it by this empty file
    wchar_t lanServer[MAX_PATH];
    swprintf(lanServer, MAX_PATH, L"%s\\Y\\SDDS", g_root);
    CreateDirectoryW(lanServer, NULL);
    wcscat_s(lanServer, MAX_PATH, L"\\LanServer.dat");
    HANDLE f = CreateFileW(lanServer, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE)
        CloseHandle(f);
    // The cabinet's network card, in the keychip's subnet (serverIpIpv4): 192.168.172.11, the
    // gateway .1 (segatools' netenv defaults); a MAC address of SEGA's (00:D0:F1)
    static const unsigned char mac[6] = {0x00, 0xD0, 0xF1, 0x01, 0x02, 0x04};
    wl_netenv_fake(0xC0A8AC0B, 0xC0A8AC01, mac);
    if (daemon)
    {
        // ALL.Net and the Aime server, on this PC (allnet.cpp)
        wl_allnet_start(0xC0A8AC01);
        system_property();
        wl_system_virtual_dll("$amvideo", g_amvideo, 4);
        wl_vdev_register(&g_keychip);
        wl_vdev_register(&g_hwmon);
        wl_hooks_add(g_daemonHooks, sizeof g_daemonHooks / sizeof g_daemonHooks[0]);
        wl_inputs_set_table(g_inputs, IN_COUNT);
        wl_vhid_register(&g_io4);
        InitializeCriticalSection(&g_vfdLock);
        wl_vdev_register(&g_vfd);
        wl_vdev_label("COM4", "Afficheur du lecteur de paiement (VFD)");
        InitializeCriticalSection(&g_aimeLock);
        wl_vdev_register(&g_aime);
        wl_vdev_label("COM3", "Lecteur de cartes Aime");
    }
    else
    {
        // The game's own boards: the wheel's motor on COM1, Sega's drive board protocol (80 on,
        // 84 torque, 85 rumble, 86 damper; segatools' board/ffb.c); the LEDs' on COM2
        wl_driveboard_register("COM1");
        InitializeCriticalSection(&g_ledLock);
        wl_vdev_register(&g_led);
        wl_vdev_label("COM2", "Carte des lampes (837-15070)");
        InitializeCriticalSection(&g_dashLock);
        wl_vdev_register(&g_dash);
        wl_vdev_label("COM20", "Tableau de bord (838-15415)");
        // Windows' list of the serial ports, which Unreal's SerialPort plugin reads
        // (GetPortNames): the cabinet's, not the PC's
        static const WlRegValue ports[] = {{"\\Device\\Serial0", REG_SZ, "COM1", 0},
                                           {"\\Device\\Serial1", REG_SZ, "COM2", 0},
                                           {"\\Device\\VCP0", REG_SZ, "COM20", 0}};
        wl_registry_add_key("HARDWARE\\DEVICEMAP", NULL, 0);
        wl_registry_add_key("HARDWARE\\DEVICEMAP\\SERIALCOMM", ports, 3);
        wl_inputs_set_table(g_inputs, IN_COUNT);
        wl_outputs_name_lamps("la carte d'E/S IO4 837-15257, via amdaemon", g_io4LampNames, IO4_LAMPS);
        // The cabinet the game plays, as its command line says (the launcher's -launch= from the
        // option CABINET, or one given by hand): the full cabinet's dashboard and LED board send
        // their own outputs too
        const wchar_t *cmdLine = GetCommandLineW();
        int mini = cmdLine && wcsstr(cmdLine, L"-launch=MiniCabinet") != NULL;
        wl_log("alls: the game plays %s", mini ? "a mini cabinet (-launch=MiniCabinet)" : "a full cabinet: its dashboard's and LED board's outputs");
        if (!mini)
        {
            dash_declare();
            led_declare();
        }
        CloseHandle(CreateThread(NULL, 0, inputs_writer, NULL, 0, NULL));
        // The title server's connections to this PC's server, with its AES key (allnet.cpp)
        wl_allnet_game();
        // The main cabinet finds itself by its broadcast (UMainDetector, UDP 40257 to
        // 192.168.172.255): alone, it hears its own ("CONNECT MAIN CABINET")
        wl_system_loopback_broadcast(1);
    }
    return 1;
}
