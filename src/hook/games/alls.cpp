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

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "driveboard.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "log.h"
#include "vdev.h"
#include "vhid.h"

#define GAME_ID "SDDS"
#define PLATFORM_ID "ACA4"

static wchar_t g_root[MAX_PATH]; // the copy's folder, where game.bat is

// ------------------------------------------------------------------------------
// The registry: HKLM\SYSTEM\SEGA\SystemProperty (segatools' platform/misc.c and nusec.c)
// ------------------------------------------------------------------------------

static void system_property(void)
{
    static const DWORD cpuError = 100, cpuWarning = 95, loaders = 1, model = PLATFORM_ID[3] - '0', region = 1, flag = 0x64;
    static const unsigned char server[4] = {192, 168, 172, 0}, server6[16] = {0};
    static const char keychipId[17] = "A69E-01A88888888"; // segatools' default, 16 characters (amSerialIdConvert)
    static const WlRegValue root[] = {{"OSVersion", REG_SZ, "0_0_0", 0}};
    static const WlRegValue stat[] = {
        {"CpuTempError", REG_DWORD, &cpuError, 4},
        {"CpuTempWarning", REG_DWORD, &cpuWarning, 4},
        {"PlatformId", REG_SZ, PLATFORM_ID, 0},
        {"PlatformName", REG_SZ, "ALLS MX2.1", 0},
    };
    static const WlRegValue master[] = {{"AppLoaderCount", REG_DWORD, &loaders, 4}};
    static const WlRegValue keychip[] = {
        {"gameId", REG_BINARY, GAME_ID, 4},
        {"keychipId", REG_BINARY, keychipId, 16},
        {"modelType", REG_DWORD, &model, 4},
        {"platformId", REG_BINARY, PLATFORM_ID, 3},
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
// alone), the board reads amdaemon's own table
#define SHARED_INPUTS L"Local\\windowsloader-alls-inputs"

struct SharedInputs
{
    volatile LONG writer; // the game's process id, 0 before it writes
    volatile LONG raw[IN_COUNT];
};

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
        for (int i = 0; i < IN_COUNT; i++)
            InterlockedExchange(&s->raw[i], (LONG)wl_input_get(&g_inputs[i]));
        Sleep(2);
    }
}

static int input_raw(int i)
{
    SharedInputs *s = g_shared ? g_shared : shared_inputs();
    return s && s->writer ? (int)s->raw[i] : wl_input_get(&g_inputs[i]);
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
    default: // 04 lamps, 05 PWM, 41 unique outputs: not shown yet
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
// The card reader, Sega 837-15396 "Aime" (gen 3) on COM3, 115200 bauds (segatools'
// board/sg-frame.c, sg-nfc.c, sg-led.c). Frames: E0, then the length (the bytes up to the
// checksum), the address (0 the NFC reader, 8 its LEDs), a sequence number, the command, the
// payload's length (an answer: its status first), the payload, an 8-bit sum; D0 escapes D0 and E0
// (the byte minus 1 follows). No card is ever on it yet: a poll finds none.
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
    static const unsigned char firmware = 0x94, none = 0;
    static const unsigned char block[16] = {0};
    switch (cmd)
    {
    case 0x62: // reset
        aime_answer(r, 3, NULL, 0);
        break;
    case 0x30: // firmware version: 94, as amdaemon's firmware file (update_15396_6728_94.bin)
        aime_answer(r, 0, &firmware, 1);
        break;
    case 0x32: // hardware version
        aime_answer(r, 0, "837-15396", 9);
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
// its data. After a reset (10) every command is answered; the lamps themselves are not shown yet.
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

// destination, source, length, command, payload
static void led_request(const unsigned char *r, int length)
{
    int cmd = r[3];
    g_ledBoard = r[0]; // SWDC's board answers as 00
    g_ledHost = r[1];
    static unsigned logged[256];
    if (logged[cmd]++ < 3)
        wl_log("led: command %02x (%d bytes)", cmd, r[2]);
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
// found in Windows' list): text commands (BP_DashBoard: ":sp" speed, ":rp" revs, ":gr" gear,
// ":le" lamps) and its answer, "ps" and its buttons' states, a line ended by \r\n
// ------------------------------------------------------------------------------

static char g_dashLine[256], g_dashOut[256];
static int g_dashLineLength, g_dashOutLength;
static CRITICAL_SECTION g_dashLock;

static BOOL dash_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    const char *b = (const char *)buffer;
    *written = size;
    EnterCriticalSection(&g_dashLock);
    static int logged;
    if (logged++ < 40)
        wl_log("dashboard: \"%.*s\"", (int)size, b);
    for (DWORD i = 0; i < size; i++)
    {
        if (g_dashLineLength < (int)sizeof g_dashLine - 1)
            g_dashLine[g_dashLineLength++] = b[i];
        if (b[i] != '\n')
            continue;
        g_dashLineLength = 0;
        static const char status[] = "ps000000\r\n";
        if (g_dashOutLength + (int)sizeof status - 1 <= (int)sizeof g_dashOut)
        {
            memcpy(g_dashOut + g_dashOutLength, status, sizeof status - 1);
            g_dashOutLength += sizeof status - 1;
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

static void drive(const char *letter, const wchar_t *folder)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s\\%s", g_root, folder);
    CreateDirectoryW(path, NULL);
    char to[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, path, -1, to, sizeof to, NULL, NULL);
    wl_files_redirect(letter, to);
}

int wl_game_alls(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    int daemon = !_wcsicmp(base, L"amdaemon.exe");
    if ((!daemon && _wcsicmp(base, L"Todoroki-Win64-Shipping.exe")) || !find_root(exePath))
        return 0;
    *name = "swdc";
    *title = "Sega World Drivers Championship";
    wl_log("game: %s (Sega ALLS), %s", *title, daemon ? "amdaemon.exe, the platform's service" : "the game");
    wl_game_set_boards("Controls on USB I/O board, Sega 837-15257 \"IO4\" (amdaemon)", "Motor board: (COM1, the game)");
    drive("D:", L"D");
    drive("E:", L"AMFS");
    drive("Y:", L"Y");
    // The main cabinet (the only one here): the game knows it by this empty file, and the
    // launcher gives amdaemon the main cabinet's settings (config_LanServer.json), as the
    // cabinet's script does when the file is there
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
        CloseHandle(CreateThread(NULL, 0, inputs_writer, NULL, 0, NULL));
    }
    return 1;
}
