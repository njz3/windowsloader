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

// Namco System ES3: Mario Kart Arcade GP DX, MK_AGP3_FINAL.exe (x86, Direct3D 9Ex, relocated: the
// offsets below are from its base), v1.18 and v1.10 (2015; told by v1.18's bngrw.dll, absent).
// v1.10 has the same key, authentication and offline functions at other places (found by the
// key's interface GUID, the "last_auth.bin" they read, the same bytes) and its card reader on COM4
// instead of bngrw.dll. Being worked on:
// - ShareK, the cabinet's content distribution, is reached through a .NET COM class of ipccl.dll
//   that the cabinet registers (RegAsm, project.ps1): windowsloader leaves the registry alone, the
//   class is missing and the game's handler of that failure reads a null pointer: the ShareK
//   client is not started (+0x7e80d, the test of its flag made a jump);
// - the game takes the first DirectInput game controller as its wheel (vJoy on the test PC): a
//   wheel of windowsloader's is the only one listed.

#include <winsock2.h>
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
    IN_ITEM = IN_BUTTONS,
    IN_MARIO,
    IN_START,
    IN_COIN,
    IN_TEST,
    IN_SERVICE,
    IN_UP,
    IN_DOWN,
    IN_ENTER,
    IN_COUNT
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
    {"MenuUp", "SelectUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"MenuDown", "SelectDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"Enter", "SelectEnter", WL_INPUT_SWITCH, 0, 1, 0},
};

static void fill_wheel(unsigned short axes[WL_VDINPUT_AXES], unsigned char *buttons)
{
    axes[0] = (unsigned short)wl_input_get(&g_inputs[IN_STEERING]);
    axes[1] = (unsigned short)wl_input_get(&g_inputs[IN_GAS]);
    axes[2] = (unsigned short)wl_input_get(&g_inputs[IN_BRAKE]);
    for (int i = 0; i < 6; i++)
        buttons[i] = (unsigned char)(wl_input_get(&g_inputs[IN_BUTTONS + i]) != 0);
}

static const WlVirtualJoystick g_wheel = {"Namco Steering Wheel", 0x0B9A, 0x0001, 8, fill_wheel};

// The cabinet's JVS board on COM3 at 115200 bauds (the game's JVS library, +0x4ac1b0, its host
// "NBGI.;WinArc;Ver2.01;JPN"; the sense line as Taito's: CTS off until every board has its
// address, +0x4ac636), Namco's NA-JV, 10-bit analog channels. Its layout, seen in the game's switch
// test: the test switch in the system byte (a switch: the test menu lasts while it is on), Service
// 0x4000, the select switch up 0x2000 and down 0x1000, Enter 0x0200, the item button 0x0020 (0x0010
// too), the Mario button 0x0002; Start 0x8000 has no use. Steering (left low), accelerator, brake
// on channels 0, 1, 2, against the calibration of the test menu's "analog volume" page.
static void jvs_state(WlJvsState *s)
{
    static int coinWas;
    static unsigned short coins;
    int coin = wl_input_get(&g_inputs[IN_COIN]);
    if (coin && !coinWas)
        coins++;
    coinWas = coin;
    s->system = wl_input_get(&g_inputs[IN_TEST]) ? 0x80 : 0;
    static const struct
    {
        int input;
        unsigned short bit;
    } bits[] = {{IN_START, 0x8000}, {IN_SERVICE, 0x4000}, {IN_UP, 0x2000}, {IN_DOWN, 0x1000},
                {IN_ENTER, 0x0200}, {IN_ITEM, 0x0020}, {IN_MARIO, 0x0002}};
    for (int i = 0; i < (int)(sizeof bits / sizeof bits[0]); i++)
        if (wl_input_get(&g_inputs[bits[i].input]))
            s->player[0] |= bits[i].bit;
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

// Namco's own JVS commands, after the board's identity, and the replies the game's JVS library
// reads (its parser of a poll's reply, +0x4adf90, and of the start's, +0x4aefa0):
// - 0x70 0x18 "PL" then 0x02 and 2 bytes, 0x14 and 1 byte, or 0x80 and 1 byte (built at +0x4af6b1
//   and +0x4af7c1): report 01 and one byte (kept at +0x36b of the board's state, not read again;
//   01 as TeknoParrot gives). The start's "70 18 PL 14 xx" is followed by "70 18 PL 80 yy" when
//   yy is set, else by 6 bytes left in the game's buffer (its length is fixed): skipped, the game
//   checks only the first reply;
// - a serial link through the board: 0x70 0x16 and 2 bytes sets it up (report only), 0x70 0x05, a
//   count and the bytes sends (report 01, 01, then 00: sent), 0x70 0x15 and 2 bytes reads (report
//   01, a count, the bytes; "70 15 00 FF", the first after 0x16, wants a count of 0). Nothing is on
//   it here: count 0;
// - 0x78, 0x79, 0x7A, 14 bytes each (12-bit values in threes: the lamps, it seems), 0x7F alone:
//   report only.
static int namco_vendor(const unsigned char *p, int n, unsigned char *r, int *k)
{
    int used = 0, data = -1, data2 = -1;
    if (p[0] == 0x70 && n >= 2)
    {
        if (p[1] == 0x18 && n >= 5 && p[2] == 'P' && p[3] == 'L')
        {
            used = p[4] == 0x02 ? 7 : p[4] == 0x14 || p[4] == 0x80 ? 6 : 0;
            data = 0x01;
            if (p[4] == 0x14 && used < n && p[used] != 0x70) // the start's, with the buffer's leftovers
                used = n;
        }
        else if (p[1] == 0x16)
            used = 4;
        else if (p[1] == 0x15)
            used = 4, data = 0x00;
        else if (p[1] == 0x05 && n >= 3)
            used = 3 + p[2], data = 0x01, data2 = 0x00;
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
    static int seen[256];
    unsigned char id = p[0] == 0x70 ? p[1] : p[0];
    if (!seen[id]++)
        wl_log_hex("namco: JVS command", p, used);
    static int sends;
    if (p[0] == 0x70 && p[1] == 0x05 && sends++ < 40)
        wl_log_hex("namco: JVS serial link, sent", p + 3, p[2]);
    r[(*k)++] = 0x01;
    if (data >= 0)
        r[(*k)++] = (unsigned char)data;
    if (data2 >= 0)
        r[(*k)++] = (unsigned char)data2;
    return used;
}

static WlJvsBoard g_jvs = {"COM3", "namco ltd.;NA-JV;Ver6.01;JPN,Multipurpose.", 1, 2, 2, 8, 10, 16, jvs_state, jvs_output, 1, NULL, namco_vendor};

// Wangan Midnight 5's controls, its own table: no Start nor item buttons (the accelerator confirms,
// the steering chooses), the shift lever, the view and interruption buttons. The same NA-JV board
// as Mario Kart's; the layout read in the game's switch test (スイッチテスト): the test switch in the
// system byte, Service 0x4000, select up 0x2000 and down 0x1000, Enter 0x0200, the view button
// (視点切替) 0x0002, the interruption button (乱入切替) 0x0001, steering, accelerator, brake on
// channels 0 to 2. The shift lever (シフト) is an H read by four switches: 0x0080 up, 0x0040 down,
// 0x0020 left, 0x0010 right; 1 up-left, 2 down-left, 3 up, 4 down, 5 up-right, 6 down-right, none
// neutral. Its gears: held (Gear1..Gear6, an H shifter), or stepped (ShiftUp, ShiftDown, a
// sequential shifter or keys), [Game.wmmt5] HOLD_GEAR keeping a held gear once let go.
// WL_WMMT5_PROBE: one more input per bit of player 1's two switch bytes, for the switch test
enum
{
    W_STEERING,
    W_GAS,
    W_BRAKE,
    W_VIEW,
    W_INTERRUPT,
    W_SHIFT_UP,
    W_SHIFT_DOWN,
    W_GEAR1,
    W_COIN = W_GEAR1 + 6,
    W_TEST,
    W_SERVICE,
    W_UP,
    W_DOWN,
    W_ENTER,
    W_PROBE,
    W_COUNT = W_PROBE + 16
};

static WlInput g_wmmt5Inputs[W_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 65535, 32768},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 65535, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 65535, 0},
    {"View", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"Interruption", "Action", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftUp", "GearUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftDown", "GearDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear1", "Gear1", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear2", "Gear2", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear3", "Gear3", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear4", "Gear4", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear5", "Gear5", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear6", "Gear6", WL_INPUT_SWITCH, 0, 1, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "P1_Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"MenuUp", "SelectUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"MenuDown", "SelectDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"Enter", "SelectEnter", WL_INPUT_SWITCH, 0, 1, 0},
    {"Bit0", "Probe0", WL_INPUT_SWITCH, 0, 1, 0},
    {"Bit1", "Probe1", WL_INPUT_SWITCH, 0, 1, 0},
    {"Bit2", "Probe2", WL_INPUT_SWITCH, 0, 1, 0},
    {"Bit3", "Probe3", WL_INPUT_SWITCH, 0, 1, 0},
    {"Bit4", "Probe4", WL_INPUT_SWITCH, 0, 1, 0},
    {"Bit5", "Probe5", WL_INPUT_SWITCH, 0, 1, 0},
    {"Bit6", "Probe6", WL_INPUT_SWITCH, 0, 1, 0},
    {"Bit7", "Probe7", WL_INPUT_SWITCH, 0, 1, 0},
    {"Bit8", "Probe8", WL_INPUT_SWITCH, 0, 1, 0},
    {"Bit9", "Probe9", WL_INPUT_SWITCH, 0, 1, 0},
    {"Bit10", "Probe10", WL_INPUT_SWITCH, 0, 1, 0},
    {"Bit11", "Probe11", WL_INPUT_SWITCH, 0, 1, 0},
    {"Bit12", "Probe12", WL_INPUT_SWITCH, 0, 1, 0},
    {"Bit13", "Probe13", WL_INPUT_SWITCH, 0, 1, 0},
    {"Bit14", "Probe14", WL_INPUT_SWITCH, 0, 1, 0},
    {"Bit15", "Probe15", WL_INPUT_SWITCH, 0, 1, 0},
};
static int g_wmmt5Probe;
static volatile LONG g_holdGear; // [Game.wmmt5] HOLD_GEAR

// The gear engaged, 0 for neutral: a held H position first; else the stepped gear, which ShiftUp
// and ShiftDown move (1 to 6, down from 1 to neutral) and, with HOLD_GEAR, the last H position sets
static int wmmt5_gear(void)
{
    static int stepped, upWas, downWas;
    int held = 0;
    for (int g = 0; g < 6; g++)
        if (wl_input_get(&g_wmmt5Inputs[W_GEAR1 + g]))
            held = g + 1;
    int up = wl_input_get(&g_wmmt5Inputs[W_SHIFT_UP]) != 0, down = wl_input_get(&g_wmmt5Inputs[W_SHIFT_DOWN]) != 0;
    if (up && !upWas && stepped < 6)
        stepped++;
    if (down && !downWas && stepped > 0)
        stepped--;
    upWas = up, downWas = down;
    if (held && g_holdGear)
        stepped = held;
    return held ? held : stepped;
}

static void wmmt5_state(WlJvsState *s)
{
    static int coinWas;
    static unsigned short coins;
    int coin = wl_input_get(&g_wmmt5Inputs[W_COIN]);
    if (coin && !coinWas)
        coins++;
    coinWas = coin;
    s->system = wl_input_get(&g_wmmt5Inputs[W_TEST]) ? 0x80 : 0;
    static const struct
    {
        int input;
        unsigned short bit;
    } bits[] = {{W_SERVICE, 0x4000}, {W_UP, 0x2000}, {W_DOWN, 0x1000}, {W_ENTER, 0x0200}, {W_VIEW, 0x0002}, {W_INTERRUPT, 0x0001}};
    for (int i = 0; i < (int)(sizeof bits / sizeof bits[0]); i++)
        if (wl_input_get(&g_wmmt5Inputs[bits[i].input]))
            s->player[0] |= bits[i].bit;
    // The H's four switches: up 0x80, down 0x40, left 0x20, right 0x10 (gears 1 to 6, neutral)
    static const unsigned short shifts[7] = {0, 0x00A0, 0x0060, 0x0080, 0x0040, 0x0090, 0x0050};
    s->player[0] |= shifts[wmmt5_gear()];
    for (int b = 0; g_wmmt5Probe && b < 16; b++)
        if (wl_input_get(&g_wmmt5Inputs[W_PROBE + b]))
            s->player[0] |= (unsigned short)(1 << b);
    s->coins[0] = coins;
    s->analog[0] = (unsigned short)wl_input_get(&g_wmmt5Inputs[W_STEERING]);
    s->analog[1] = (unsigned short)wl_input_get(&g_wmmt5Inputs[W_GAS]);
    s->analog[2] = (unsigned short)wl_input_get(&g_wmmt5Inputs[W_BRAKE]);
}

static void hold_gear_apply(const char *value)
{
    InterlockedExchange(&g_holdGear, value && !_stricmp(value, "true"));
}

static void wmmt5_settings(void)
{
    hold_gear_apply(wl_game_option_bool("HOLD_GEAR") ? "true" : "false");
}

static const WlGameOption g_wmmt5Options[] = {
    {"HOLD_GEAR", "Garder le rapport", "Le rapport reste engagé quand le levier en H est lâché, pour un levier dont les positions ne tiennent pas.",
     WL_OPTION_BOOL, NULL, 0, 0, "false", 0, hold_gear_apply},
};

static void wmmt5_output(const unsigned char *data, int length);
static WlJvsBoard g_jvsWmmt5 = {"COM3", "namco ltd.;NA-JV;Ver6.01;JPN,Multipurpose.", 1, 2, 2, 8, 10, 16, wmmt5_state, wmmt5_output, 1, NULL, namco_vendor};

// The steering the motor board's spring is measured against: the game's own table's
static WlInput *g_steering = &g_inputs[IN_STEERING];

// The steering motor's board on COM1 at 19200 bauds (class clWaJvIo's Pimpl, its thread +0x1f57d0).
// The board speaks in 3 characters, read 3 at a time (+0x1f5d00): "C01" its motor ready (the game
// builds its commands only then, +0x1f64e5), "C06" stopped, "E00".."E20" its errors. Once the game
// is up (+0x202ec4) it sends every 8 ms 10 bytes (+0x1f5c80, built at +0x1f6630): FF FF, a target
// position (low byte, then 6 bits) in the steering's own unit, 0x200 the middle (0x1FF seen with
// the wheel at rest), a spring's stiffness 0..127, 0,
// a damper 0..127, 0, a vibration -128..127, 0. Played here as a spring towards the target plus
// the vibration (the sign to check at the wheel).
static CRITICAL_SECTION g_motorLock;
static int g_motorReady; // "C01" given

// A command of 10 bytes, FF FF first, played; 0 for another
static int motor_command(const unsigned char *b, DWORD size)
{
    if (size < 10 || b[0] != 0xFF || b[1] != 0xFF)
    {
        static int logged;
        if (logged++ < 20)
            wl_log_hex("namco: motor board, unknown command", b, size);
        return 0;
    }
    int target = b[2] | (b[3] & 0x3F) << 8;
    int stiffness = b[4], damper = b[6], vibration = (signed char)b[8];
    double wheel = (wl_input_get(g_steering) - 32768) / 32768.0;
    double force = stiffness / 127.0 * (wheel - (target - 0x200) / 512.0) + vibration / 128.0;
    wl_ffb_set(force < -1 ? -1 : force > 1 ? 1 : force);
    static int last[4] = {-1, -1, -1, -1}, logged;
    if ((target != last[0] || stiffness != last[1] || damper != last[2] || vibration != last[3]) && logged++ < 200)
        wl_log("namco: motor target %04x stiffness %d damper %d vibration %d (wheel %.2f)", target, stiffness, damper, vibration, wheel);
    last[0] = target, last[1] = stiffness, last[2] = damper, last[3] = vibration;
    return 1;
}

static BOOL com1_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    *written = size;
    motor_command((const unsigned char *)buffer, size);
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

// Wangan Midnight Maximum Tune 5's steering board ("STR3", class device::FFBIo) on COM1 at 115200
// bauds, a boost::asio serial port. Its answers are 3 characters (+0x723e20): "E00" ready after a
// reset (the wait after the start, +0x727020, then the motor's centre 0x1ff), "C01" the motor on,
// "C06" off, "E02".."E20" its errors. The motor's power is the JVS board's output 0x80 of the first
// byte (m_JvioGOUT's cMOTOR_BIT): cut after E00 (State_PowerOff, +0x725fe0, then C06 awaited), given
// for the game (State_PowerOn, +0x726e70, then C01 awaited); 60 s without, E2212 "STR PCB Link error".
// The motor on, the game sends Mario Kart's commands of 10 bytes (+0x726a30), each answered by "H"
// and the wheel's position on 10 bits, high byte first, 0x1ff the middle (+0x72426a): without, E20
// and E2212 again 3 s later (+0x7260d0)
static unsigned char g_strReply[64];
static int g_strReplyLength;
static int g_strMotor = -1; // the motor's power as the board last told it, -1 not yet

static void str3_answer(const char *reply)
{
    EnterCriticalSection(&g_motorLock);
    if (g_strReplyLength + 3 <= (int)sizeof g_strReply)
    {
        memcpy(g_strReply + g_strReplyLength, reply, 3);
        g_strReplyLength += 3;
    }
    LeaveCriticalSection(&g_motorLock);
}

static BOOL str3_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    *written = size;
    if (motor_command((const unsigned char *)buffer, size))
    {
        int position = wl_input_get(g_steering) >> 6;
        char reply[3] = {'H', (char)(position >> 8), (char)position};
        str3_answer(reply);
    }
    return TRUE;
}

static BOOL str3_read(void *buffer, DWORD size, DWORD *read, LONGLONG offset)
{
    // The board speaks first, once the port is open (no command before, 60 s then E2212 "STR PCB
    // Link error")
    if (!g_motorReady)
    {
        g_motorReady = 1;
        str3_answer("E00");
    }
    EnterCriticalSection(&g_motorLock);
    DWORD n = size < (DWORD)g_strReplyLength ? size : (DWORD)g_strReplyLength;
    memcpy(buffer, g_strReply, n);
    memmove(g_strReply, g_strReply + n, g_strReplyLength - n);
    g_strReplyLength -= n;
    LeaveCriticalSection(&g_motorLock);
    *read = n;
    static int logged;
    if (n && ((const char *)buffer)[0] != 'H' && logged++ < 40)
        wl_log("namco: steering board answers %.*s", (int)n, (const char *)buffer);
    return TRUE;
}

static DWORD str3_waiting(void)
{
    return (DWORD)g_strReplyLength;
}

// The JVS outputs: the motor's power told by the board once it changes
static void wmmt5_output(const unsigned char *data, int length)
{
    jvs_output(data, length);
    int motor = length > 0 && (data[0] & 0x80);
    if (g_motorReady && motor != g_strMotor)
    {
        g_strMotor = motor;
        str3_answer(motor ? "C01" : "C06");
    }
}

static WlVirtualDevice g_str3 = {"COM1", NULL, str3_read, str3_write, com1_modem, str3_waiting};

// v1.10's card reader on COM4, 38400 bauds (v1.18's goes through bngrw.dll): an NFC reader
// speaking NXP's PN532 frames (Sony's RC-S956, Banapass's readers): 00 00 FF, the length (TFI,
// command, data) and its complement, D4 then the command and its data, their sum's complement, 00.
// Each command is acknowledged (00 00 FF 00 FF 00), then answered D5 and the command + 1. No card
// is on it: a poll (4A) finds no target.
static unsigned char g_nfcIn[300], g_nfcOut[1024];
static int g_nfcInLength, g_nfcOutLength;
static CRITICAL_SECTION g_nfcLock;

static void nfc_put(const unsigned char *b, int n)
{
    if (g_nfcOutLength + n <= (int)sizeof g_nfcOut)
    {
        memcpy(g_nfcOut + g_nfcOutLength, b, n);
        g_nfcOutLength += n;
    }
}

static void nfc_answer(int cmd, const unsigned char *data, int n)
{
    static const unsigned char ack[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    nfc_put(ack, 6);
    unsigned char f[64] = {0x00, 0x00, 0xFF, (unsigned char)(2 + n), (unsigned char)(0x100 - (2 + n)), 0xD5, (unsigned char)(cmd + 1)};
    memcpy(f + 7, data, n);
    unsigned char sum = 0;
    for (int i = 5; i < 7 + n; i++)
        sum += f[i];
    f[7 + n] = (unsigned char)(0x100 - sum);
    f[8 + n] = 0x00;
    nfc_put(f, 9 + n);
}

static void nfc_command(const unsigned char *d, int n) // D4, the command, its data
{
    int cmd = d[1];
    static unsigned logged[256];
    if (logged[cmd]++ < 3)
        wl_log_hex("namco: card reader, command", d + 1, n - 1 < 16 ? n - 1 : 16);
    static const unsigned char firmware[4] = {0x32, 0x01, 0x06, 0x07}; // PN532 1.6
    static const unsigned char zero[8] = {0};
    switch (cmd)
    {
    case 0x02: // firmware version
        nfc_answer(cmd, firmware, 4);
        break;
    case 0x4A: // poll for targets: none
    case 0x52: // release
    case 0x16: // power down
        nfc_answer(cmd, zero, 1);
        break;
    case 0x06: // registers read: their values (0s)
        nfc_answer(cmd, zero, n - 2 > 0 ? (n - 2) / 2 : 1);
        break;
    case 0x08: // registers written: a status
        nfc_answer(cmd, zero, 1);
        break;
    case 0x40: // data exchange, communicate through: no target (error 01)
    case 0x42:
    {
        static const unsigned char error = 0x01;
        nfc_answer(cmd, &error, 1);
        break;
    }
    default: // 18 serial speed, 12 parameters, 14 SAM, 32 RF settings, 08 registers written...
        nfc_answer(cmd, NULL, 0);
        break;
    }
}

static BOOL com4_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    const unsigned char *b = (const unsigned char *)buffer;
    *written = size;
    EnterCriticalSection(&g_nfcLock);
    for (DWORD i = 0; i < size; i++)
    {
        if (g_nfcInLength < (int)sizeof g_nfcIn)
            g_nfcIn[g_nfcInLength++] = b[i];
        // A frame: 00 00 FF LEN LCS data[LEN] DCS 00 (an ACK has LEN 0: the host's abort)
        unsigned char *f = g_nfcIn;
        int n = g_nfcInLength;
        while (n >= 3 && !(f[0] == 0x00 && f[1] == 0x00 && f[2] == 0xFF))
            f++, n--; // the wake-up's 55s and anything before a frame
        if (f != g_nfcIn)
        {
            memmove(g_nfcIn, f, n);
            g_nfcInLength = n;
        }
        if (n >= 5 && g_nfcIn[3] == 0 && g_nfcIn[4] == 0xFF)
        {
            g_nfcInLength = n > 6 ? n - 6 : 0; // an ACK
            memmove(g_nfcIn, g_nfcIn + 6, g_nfcInLength);
        }
        else if (n >= 5 && n >= 5 + g_nfcIn[3] + 2)
        {
            if (g_nfcIn[3] >= 2 && g_nfcIn[5] == 0xD4)
                nfc_command(g_nfcIn + 5, g_nfcIn[3]);
            int used = 5 + g_nfcIn[3] + 2;
            g_nfcInLength = n - used;
            memmove(g_nfcIn, g_nfcIn + used, g_nfcInLength);
        }
    }
    LeaveCriticalSection(&g_nfcLock);
    return TRUE;
}

static BOOL com4_read(void *buffer, DWORD size, DWORD *read, LONGLONG offset)
{
    EnterCriticalSection(&g_nfcLock);
    DWORD n = (DWORD)g_nfcOutLength < size ? (DWORD)g_nfcOutLength : size;
    memcpy(buffer, g_nfcOut, n);
    memmove(g_nfcOut, g_nfcOut + n, g_nfcOutLength - n);
    g_nfcOutLength -= n;
    LeaveCriticalSection(&g_nfcLock);
    *read = n;
    return TRUE;
}

static DWORD com4_waiting(void)
{
    return (DWORD)g_nfcOutLength;
}

static WlVirtualDevice g_com4 = {"COM4", NULL, com4_read, com4_write, com1_modem, com4_waiting};

// The Banapass card reader, through bngrw.dll (Namco's library for it, imported by the game): no
// reader on this PC. The game's boot check attaches it (+0x202d5b, +0x201426) and waits for it to
// be ready (+0x201450): the requests (attach, reset, LED, beep, URL) take the reader's number, a
// callback and its context, and the library calls back callback(number, result, context) once
// done; the game's callbacks (+0x2012c0, +0x201330) keep the result, 0 for done, -301 an error.
// BngRwIsCmdExec is 1 while a request runs. Here every request is done at once; no card is ever
// touched (BngRwReqWaitTouch -1, as OpenParrot's OpenBanapass answers it).
typedef void(__cdecl *BngCallback)(int number, int result, void *context);

static int WINAPI bng_init(void)
{
    wl_log("namco: BngRwInit (the card reader, emulated: no card)");
    return 0;
}

static int WINAPI bng_attach(UINT number, char *port, int a, int b, BngCallback callback, void *context)
{
    wl_log("namco: BngRwAttach %u \"%s\": attached", number, port ? port : "");
    if (callback)
        callback((int)number, 0, context);
    return 1;
}

static int WINAPI bng_is_cmd_exec(UINT number)
{
    return 0;
}

static int WINAPI bng_wait_touch(UINT number, int a, UINT b, void *callback, void *context)
{
    return -1; // no card
}

static int WINAPI bng_cancel(UINT number)
{
    return number > 7 ? -100 : 1;
}

static int WINAPI bng_set_led_power(UINT a, UINT b, UINT c)
{
    return 0;
}

static int WINAPI bng_dev_reset(UINT number, BngCallback callback, void *context)
{
    if (callback)
        callback((int)number, 0, context);
    return 1;
}

static int WINAPI bng_request(UINT number, UINT value, BngCallback callback, void *context) // LED, beep
{
    if (callback)
        callback((int)number, 0, context);
    return 1;
}

static int WINAPI bng_send_url(UINT number, int a, UINT b, int *c, char *d, char *e, BngCallback callback, void *context)
{
    if (number > 7 || !d)
        return -100;
    if (callback)
        callback((int)number, 0, context);
    return 1;
}

static void WINAPI bng_fin(void)
{
}

// Wangan Midnight 6's (64 bits) two more: the library's version, an action (as LED and beep)
static unsigned int WINAPI bng_get_version(void)
{
    return 0x1000000;
}

static int WINAPI bng_action(UINT number, UINT action, BngCallback callback, void *context)
{
    if (callback)
        callback((int)number, 0, context);
    return 1;
}

// Mario Kart DX 1.18 (32 bits) imports them decorated (__stdcall), Wangan 6 (64 bits) by their names
#ifdef _WIN64
#define BNG(name, bytes) #name
#else
#define BNG(name, bytes) "_" #name "@" #bytes
#endif

static void bngrw_patch(void)
{
    static const struct
    {
        const char *name;
        void *fn;
    } fns[] = {{BNG(BngRwInit, 0), (void *)bng_init},
               {BNG(BngRwAttach, 24), (void *)bng_attach},
               {BNG(BngRwIsCmdExec, 4), (void *)bng_is_cmd_exec},
               {BNG(BngRwReqWaitTouch, 20), (void *)bng_wait_touch},
               {BNG(BngRwReqCancel, 4), (void *)bng_cancel},
               {BNG(BngRwSetLedPower, 12), (void *)bng_set_led_power},
               {BNG(BngRwDevReset, 12), (void *)bng_dev_reset},
               {BNG(BngRwReqLed, 16), (void *)bng_request},
               {BNG(BngRwReqBeep, 16), (void *)bng_request},
               {BNG(BngRwReqSendUrlTo, 32), (void *)bng_send_url},
               {BNG(BngRwFin, 0), (void *)bng_fin},
               {BNG(BngRwGetVersion, 0), (void *)bng_get_version},
               {BNG(BngRwReqAction, 16), (void *)bng_action}};
    int n = 0;
    for (int i = 0; i < (int)(sizeof fns / sizeof fns[0]); i++)
    {
        void *was;
        n += wl_patch_iat(GetModuleHandleW(NULL), "bngrw.dll", fns[i].name, fns[i].fn, &was) != 0;
    }
    wl_log("namco: %d of bngrw.dll's functions (the card reader) given by windowsloader", n);
}

// The cabinet's security key, a USB device of its own (interface {3ABF6F2D-71C4-462a-8A92-
// 1E6861E6AF27}, its driver's DeviceIoControl 0x220408 and 0x220410, signed data): +0x4b17b0 finds
// it, reads it and checks it, then fills the cabinet's information (0x628 bytes) the boot check's
// "drive cabinet" step reads (+0x207c50): at +0x428 the key's 12 digits, "2710" the game, a digit,
// a digit of 0..3, 4 or 9, "02", 4 digits. Answered as a present and verified key, as linuxloader
// answers its games' keys: the information filled with such a number, nothing else. Its last digit
// is the cabinet's number in the link (below), each cabinet its own key.
static int g_cabinet = 1; // the cabinet's number in the link, 1..4

static int __cdecl cabinet_key(void *info)
{
    memset(info, 0, 0x628);
    wchar_t id[13];
    swprintf(id, 13, L"27101102000%d", g_cabinet);
    wcscpy_s((wchar_t *)((char *)info + 0x428), 13, id);
    static int logged;
    if (!logged++)
        wl_log("namco: the cabinet's security key, emulated: present, verified (%S)", id);
    return 1;
}

// The cabinets' network: the boot check's link step (+0x31cb20) wants the cabinet's address in
// 192.168.112.11..20 (errors 0x11, 0x13 otherwise) and a gateway (error 0x10); the address is the
// last interface in 192.168. that WSAIoctl(SIO_GET_INTERFACE_LIST) gives (+0x31c3c0), the gateway
// GetAdaptersInfo's. The game's own calls (its imports) see one interface, cabinet N's
// 192.168.112.(10 + N) behind 192.168.112.1; the PC's network is left as it is (the game's attempts
// to change it, ipconfig and routes, are refused by system.cpp).
#define CABINET_NET 0x0070A8C0u  // 192.168.112.0, in network order
#define CABINET_MASK 0x00FFFFFFu // 255.255.255.0

static ULONG cabinet_address(int n)
{
    return CABINET_NET | (ULONG)(10 + n) << 24;
}

struct WlInterfaceInfo // INTERFACE_INFO, 76 bytes
{
    ULONG flags; // IFF_UP 1, IFF_BROADCAST 2, IFF_LOOPBACK 4
    struct
    {
        USHORT family, port;
        ULONG address;
        unsigned char rest[16];
    } address, broadcast, mask;
};

struct WlIpAddrString // IP_ADDR_STRING
{
    WlIpAddrString *next;
    char address[16], mask[16];
    DWORD context;
};

struct WlAdapterInfo // IP_ADAPTER_INFO
{
    WlAdapterInfo *next;
    DWORD comboIndex;
    char name[260], description[132];
    UINT addressLength;
    BYTE mac[8];
    DWORD index;
    UINT type, dhcpEnabled;
    WlIpAddrString *current;
    WlIpAddrString ipAddressList, gatewayList, dhcpServer;
    BOOL haveWins;
    WlIpAddrString primaryWins, secondaryWins;
    __int64 leaseObtained, leaseExpires;
};

static int(WINAPI *oWSAIoctl)(UINT_PTR, DWORD, void *, DWORD, void *, DWORD, DWORD *, void *, void *);
static int WINAPI cabinet_WSAIoctl(UINT_PTR s, DWORD code, void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned, void *ov, void *cr)
{
    if (code != 0x4004747F || !out || outSize < sizeof(WlInterfaceInfo)) // SIO_GET_INTERFACE_LIST
        return oWSAIoctl(s, code, in, inSize, out, outSize, returned, ov, cr);
    memset(out, 0, outSize); // the game counts its entries wrongly (bytes / 24): the others read as down
    WlInterfaceInfo *i = (WlInterfaceInfo *)out;
    i->flags = 1 | 2;
    i->address.family = i->broadcast.family = i->mask.family = 2; // AF_INET
    i->address.address = cabinet_address(g_cabinet);
    i->broadcast.address = 0xFFFFFFFFu;
    i->mask.address = CABINET_MASK;
    if (returned)
        *returned = sizeof(WlInterfaceInfo);
    static int logged;
    if (!logged++)
        wl_log("namco: the cabinet's network interface given to the game: 192.168.112.%d/24", 10 + g_cabinet);
    return 0;
}

static ULONG WINAPI cabinet_GetAdaptersInfo(void *info, ULONG *size)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    if (!info || *size < sizeof(WlAdapterInfo))
    {
        *size = sizeof(WlAdapterInfo);
        return ERROR_BUFFER_OVERFLOW;
    }
    WlAdapterInfo *a = (WlAdapterInfo *)info;
    memset(a, 0, sizeof *a);
    strcpy_s(a->name, sizeof a->name, "{00000000-0000-0000-0000-000000000011}");
    strcpy_s(a->description, sizeof a->description, "Cabinet network (windowsloader)");
    const BYTE mac[6] = {0x00, 0xD0, 0xF1, 0x01, 0x02, (BYTE)(0x10 + g_cabinet)};
    a->addressLength = 6;
    memcpy(a->mac, mac, 6);
    a->index = 11;
    a->type = 6; // MIB_IF_TYPE_ETHERNET
    snprintf(a->ipAddressList.address, 16, "192.168.112.%d", 10 + g_cabinet);
    strcpy_s(a->ipAddressList.mask, 16, "255.255.255.0");
    strcpy_s(a->gatewayList.address, 16, "192.168.112.1");
    strcpy_s(a->gatewayList.mask, 16, "255.255.255.255");
    *size = sizeof *a;
    return 0;
}

// The game then makes the cabinets' gateway the PC's default route (+0x31c180: each default route
// deleted, one set again) and stops on error 0x10 when that fails: its two calls are taken as
// done, nothing is changed
static DWORD WINAPI cabinet_route(void *row)
{
    static int logged;
    if (!logged++)
        wl_log("namco: the game's change of the default route (the cabinets' gateway) taken as done, the PC's routes left alone");
    return 0;
}

// And pings the cabinets' router, 192.168.112.254 (+0x31c9c0, 4 tries of 100 ms), wanting its data
// back: the router, the cabinet itself and the other cabinets of the link answer; any other address
// is a time-out, nothing is sent
#define CABINET_ROUTER 0xFE70A8C0u // 192.168.112.254, in network order

#define LINK_MAX 4
static int g_linkCount;                // the cabinets of the link, 0: alone
static ULONG g_linkAddress[LINK_MAX]; // their real addresses, cabinet 1's first, network order

struct WlEchoReply // ICMP_ECHO_REPLY
{
    ULONG address, status, roundTrip;
    USHORT dataSize, reserved;
    void *data;
    struct
    {
        UCHAR ttl, tos, flags, optionsSize;
        UCHAR *options;
    } options;
};

static DWORD WINAPI cabinet_IcmpSendEcho(HANDLE icmp, ULONG to, void *data, WORD size, void *options, void *reply, DWORD replySize, DWORD timeout)
{
    static int logged;
    int cabinet = (to & CABINET_MASK) == CABINET_NET ? (int)(to >> 24) - 10 : 0;
    int known = to == CABINET_ROUTER || cabinet == g_cabinet || (cabinet >= 1 && cabinet <= g_linkCount);
    if (!known || !reply || replySize < sizeof(WlEchoReply) + size) // the game gives no more
    {
        if (logged++ < 10)
            wl_log("namco: ping %lu.%lu.%lu.%lu: no answer (the cabinets' network is the router and this cabinet)", to & 0xFF, to >> 8 & 0xFF,
                   to >> 16 & 0xFF, to >> 24);
        Sleep(timeout < 1000 ? timeout : 1000);
        SetLastError(11010); // IP_REQ_TIMED_OUT
        return 0;
    }
    WlEchoReply *r = (WlEchoReply *)reply;
    memset(r, 0, sizeof *r);
    r->address = to;
    r->roundTrip = 1;
    r->dataSize = size;
    r->data = r + 1;
    r->options.ttl = 128;
    if (size)
        memcpy(r + 1, data, size);
    if (logged++ < 10)
        wl_log("namco: ping %lu.%lu.%lu.%lu answered", to & 0xFF, to >> 8 & 0xFF, to >> 16 & 0xFF, to >> 24);
    return 1;
}

// The network authentication: without its server, the game plays offline only while its last
// authentication (F:/system/last_auth.bin, 56 bytes, "ABGN") is recent enough, +0x62e60, asked at
// 13 places; error 0x39 at boot otherwise. The servers of these cabinets are gone: answered as a
// recent authentication, as the key above
static int WINAPI cabinet_auth_recent(void *auth)
{
    static int logged;
    if (!logged++)
        wl_log("namco: the network authentication taken as recent (offline play allowed)");
    return 1;
}

// Offline, the game also stops 36 hours (129600 s) after it went offline, a date kept in
// F:/system/boot_limit.bin (+0x68430, "オフライン状態開始から規定時間経過したため、起動制限が発生", error
// 0x37): taken as not reached. Its object comes in eax and nothing on the stack: a function
// without arguments answers.
static int __cdecl cabinet_offline_limit(void)
{
    static int logged;
    if (!logged++)
        wl_log("namco: the offline time limit taken as not reached");
    return 0;
}

// The link, up to 4 cabinets on UDP 20199 (+0x318c40, its sockets' only sendto and recvfrom): each
// cabinet broadcasts "MK3" 0x12 0x10, its mode, its group and its PCB-ID - 1, about every 10 s
// (+0x319280); one heard from the cabinets' network (the sender's address) is a cabinet of the link,
// its packets sent to that address on UDP 20100 + its group. The PCB-ID (1..4) is the game's own
// setting, in its test menu: two cabinets with the same, error E24-03 "PCB ID already in use" (as on
// the cabinets). Its number for windowsloader is [Game.mkdx] CABINET, its address 192.168.112.(10 +
// N); LINK lists the cabinets' real addresses (cabinet 1's first), the game's packets carried
// between them:
//   bind 0.0.0.0 or its own address       -> its real address, the same port
//   bind another cabinet's address        -> refused, as on the cabinet (not its address)
//   sendto 255.255.255.255, 192.168.112.255 -> each cabinet's real address, its own too
//   sendto 192.168.112.(10 + N)           -> cabinet N's real address; the other .112 dropped
//   recvfrom cabinet N's real address     -> from 192.168.112.(10 + N); any other not heard
// Without LINK the cabinet is alone, as before: its broadcast heard from 127.0.0.1 (system.cpp).
// A link on one PC (a test): LINK = 127.0.0.1,127.0.0.2, each game its CABINET.
static int(WSAAPI *olink_bind)(SOCKET, const sockaddr *, int);
static int(WSAAPI *olink_sendto)(SOCKET, const char *, int, int, const sockaddr *, int);
static int(WSAAPI *olink_recvfrom)(SOCKET, char *, int, int, sockaddr *, int *);

// A function of ws2_32's, imported by its number (the game's) or by its name
static int patch_ws2(HMODULE exe, int ordinal, const char *name, void *fn, void **original)
{
    return wl_patch_iat(exe, "WS2_32.dll", (const char *)(ULONG_PTR)ordinal, fn, original) || wl_patch_iat(exe, "WS2_32.dll", name, fn, original);
}

static void link_log(const char *what, const sockaddr_in *a, const sockaddr_in *to, int r, DWORD err, const char *data = NULL)
{
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) > 200)
        return;
    const unsigned char *b = (const unsigned char *)&a->sin_addr, *c = (const unsigned char *)&to->sin_addr;
    char hex[3 * 8 + 1] = "";
    for (int i = 0; data && i < r && i < 8; i++)
        snprintf(hex + 3 * i, sizeof hex - 3 * i, "%02x ", (unsigned char)data[i]);
    wl_log("namco: link %s %u.%u.%u.%u:%u -> %u.%u.%u.%u: %d (%lu) %s", what, b[0], b[1], b[2], b[3], ntohs(a->sin_port), c[0], c[1], c[2],
           c[3], r, r < 0 ? err : 0, hex);
}

static int WSAAPI link_bind(SOCKET s, const sockaddr *a, int len)
{
    if (!g_linkCount || !a || len < (int)sizeof(sockaddr_in) || a->sa_family != AF_INET)
        return olink_bind(s, a, len);
    sockaddr_in to;
    memcpy(&to, a, sizeof to);
    ULONG ip = to.sin_addr.s_addr;
    if (ip != INADDR_ANY && ip != cabinet_address(g_cabinet))
    {
        if ((ip & CABINET_MASK) != CABINET_NET)
            return olink_bind(s, a, len);
        link_log("bind refused", &to, &to, -1, WSAEADDRNOTAVAIL);
        WSASetLastError(WSAEADDRNOTAVAIL);
        return SOCKET_ERROR;
    }
    to.sin_addr.s_addr = g_linkAddress[g_cabinet - 1];
    wl_busy_enter(); // the cabinet's real address: not refused by system.cpp
    int r = olink_bind(s, (const sockaddr *)&to, sizeof to);
    DWORD err = WSAGetLastError();
    wl_busy_leave();
    link_log("bind", (const sockaddr_in *)a, &to, r, err);
    WSASetLastError(err);
    return r;
}

static int WSAAPI link_sendto(SOCKET s, const char *buf, int n, int flags, const sockaddr *a, int len)
{
    if (!g_linkCount || !a || len < (int)sizeof(sockaddr_in) || a->sa_family != AF_INET)
        return olink_sendto(s, buf, n, flags, a, len);
    sockaddr_in to;
    memcpy(&to, a, sizeof to);
    ULONG ip = to.sin_addr.s_addr;
    int first, last;
    if (ip == INADDR_BROADCAST || ip == (CABINET_NET | ~CABINET_MASK))
        first = 0, last = g_linkCount - 1;
    else if ((ip & CABINET_MASK) == CABINET_NET)
    {
        int cabinet = (int)(ip >> 24) - 10;
        if (cabinet < 1 || cabinet > g_linkCount)
            return n; // no such cabinet
        first = last = cabinet - 1;
    }
    else
        return olink_sendto(s, buf, n, flags, a, len);
    wl_busy_enter();
    // Sent from the cabinet's real address (a socket not bound yet is bound to it): a link on one
    // PC tells its games apart by it
    sockaddr_in self;
    int selfLen = sizeof self;
    if (getsockname(s, (sockaddr *)&self, &selfLen) && WSAGetLastError() == WSAEINVAL)
    {
        memset(&self, 0, sizeof self);
        self.sin_family = AF_INET;
        self.sin_addr.s_addr = g_linkAddress[g_cabinet - 1];
        bind(s, (const sockaddr *)&self, sizeof self);
    }
    int r = n;
    DWORD err = 0;
    for (int i = first; i <= last; i++)
    {
        to.sin_addr.s_addr = g_linkAddress[i];
        int sent = olink_sendto(s, buf, n, flags, (const sockaddr *)&to, sizeof to);
        if (sent < 0 && first == last)
            r = sent, err = WSAGetLastError();
        static volatile LONG logged[LINK_MAX];
        if (InterlockedIncrement(&logged[i]) <= 20)
            link_log("sendto", (const sockaddr_in *)a, &to, sent, WSAGetLastError(), buf);
    }
    wl_busy_leave();
    WSASetLastError(err);
    return r;
}

static int WSAAPI link_recvfrom(SOCKET s, char *buf, int n, int flags, sockaddr *from, int *fromLen)
{
    int r = olink_recvfrom(s, buf, n, flags, from, fromLen);
    if (!g_linkCount || r < 0 || !from || !fromLen || *fromLen < (int)sizeof(sockaddr_in) || from->sa_family != AF_INET)
        return r;
    sockaddr_in *f = (sockaddr_in *)from;
    for (int i = 0; i < g_linkCount; i++)
        if (f->sin_addr.s_addr == g_linkAddress[i])
        {
            sockaddr_in real = *f;
            f->sin_addr.s_addr = cabinet_address(i + 1);
            static volatile LONG logged[LINK_MAX];
            if (InterlockedIncrement(&logged[i]) <= 20)
                link_log("recvfrom", &real, f, r, 0, buf);
            return r;
        }
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 10)
        link_log("recvfrom, not a cabinet's: not heard", f, f, r, 0);
    WSASetLastError(WSAEWOULDBLOCK);
    return SOCKET_ERROR;
}

// A link on one PC: the game runs once per PC (its mutex "MKAGP3", +0x36f000); each cabinet's game
// has its own then
static int link_on_this_pc(void)
{
    return g_linkCount && (g_linkAddress[g_cabinet - 1] & 0xFF) == 127;
}

static HANDLE(WINAPI *olink_CreateMutexA)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
static HANDLE WINAPI link_CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCSTR name)
{
    if (!link_on_this_pc() || !name || strcmp(name, "MKAGP3"))
        return olink_CreateMutexA(sa, owner, name);
    char own[16];
    snprintf(own, sizeof own, "MKAGP3-%d", g_cabinet);
    wl_log("namco: link on this PC: the game's mutex %s", own);
    return olink_CreateMutexA(sa, owner, own);
}

static HANDLE(WINAPI *olink_CreateMutexW)(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR);
static HANDLE WINAPI link_CreateMutexW(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCWSTR name)
{
    if (!link_on_this_pc() || !name || wcscmp(name, L"MKAGP3"))
        return olink_CreateMutexW(sa, owner, name);
    wchar_t own[16];
    swprintf(own, 16, L"MKAGP3-%d", g_cabinet);
    wl_log("namco: link on this PC: the game's mutex %S", own);
    return olink_CreateMutexW(sa, owner, own);
}

// The link's settings: LINK's addresses, CABINET the cabinet's number (0: the first of LINK's
// addresses that is this PC's)
static void mkdx_settings(void)
{
    char list[256];
    wl_game_option("LINK", list, sizeof list);
    g_cabinet = wl_game_option_int("CABINET");
    for (char *p = strtok(list, ",; "); p && g_linkCount < LINK_MAX; p = strtok(NULL, ",; "))
    {
        unsigned b[4];
        char end;
        if (sscanf_s(p, "%u.%u.%u.%u%c", &b[0], &b[1], &b[2], &b[3], &end, 1) != 4 || b[0] > 255 || b[1] > 255 || b[2] > 255 || b[3] > 255 ||
            !b[0])
            wl_log("namco: link: \"%s\" is not an address, left out", p);
        else
            g_linkAddress[g_linkCount++] = b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24;
    }
    if (g_linkCount && g_cabinet < 1)
    {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        wl_busy_enter();
        for (int i = 0; i < g_linkCount && g_cabinet < 1; i++)
        {
            SOCKET t = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            sockaddr_in a = {};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = g_linkAddress[i];
            if (t != INVALID_SOCKET && !bind(t, (const sockaddr *)&a, sizeof a))
                g_cabinet = i + 1;
            closesocket(t);
        }
        wl_busy_leave();
        WSACleanup();
        if (g_cabinet < 1)
        {
            wl_log("namco: link: none of LINK's addresses is this PC's (CABINET not set): the cabinet stays alone");
            g_linkCount = 0;
        }
    }
    if (g_cabinet < 1 || g_cabinet > LINK_MAX || (g_linkCount && g_cabinet > g_linkCount))
    {
        if (g_linkCount)
            wl_log("namco: link: CABINET %d is not one of LINK's %d addresses: the cabinet stays alone", g_cabinet, g_linkCount);
        g_cabinet = g_cabinet >= 1 && g_cabinet <= LINK_MAX ? g_cabinet : 1;
        g_linkCount = 0;
    }
    if (g_linkCount)
    {
        ULONG ip = g_linkAddress[g_cabinet - 1];
        wl_log("namco: link: cabinet %d of %d (192.168.112.%d), its real address %lu.%lu.%lu.%lu", g_cabinet, g_linkCount, 10 + g_cabinet,
               ip & 0xFF, ip >> 8 & 0xFF, ip >> 16 & 0xFF, ip >> 24);
    }
    else
        wl_log("namco: cabinet %d (192.168.112.%d), alone", g_cabinet, 10 + g_cabinet);
}

static const WlGameOption g_mkdxOptions[] = {
    {"CABINET", "Numéro de la borne (link)",
     "Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "
     "dans le fichier).",
     WL_OPTION_INT, NULL, 0, LINK_MAX, "0", 1, NULL},
};

// ------------------------------------------------------------------------------
// System ES3X: Wangan Midnight Maximum Tune 5 (wmn5r.exe, x64, Direct3D 11, 2014) and 5 DX+ (the
// same program's name, 2016). The cabinet's Sentinel HASP key is reached through
// hasp_windows_x64_109906.dll (5 DX+: _106482), imported by number: login, logout, get_size, read,
// decrypt and write (5 DX+) are windowsloader's (the import table's entries). The key's memory:
// 0xD40 bytes, the cabinet's ID at 0xD00 with its sum after it, two records at 0x10 and 0x20 (their
// values, the size and the places from OpenParrot's WMMT5.cpp; decrypt leaves the data as it is,
// as there)
// ------------------------------------------------------------------------------

#define HASP_STATUS_OK 0
#define DONGLE_SIZE 0xD40

static unsigned char g_dongle[DONGLE_SIZE];

static unsigned int __stdcall hasp_login(unsigned int feature, void *vendorCode, unsigned int *handle)
{
    wl_log("hasp: login, feature %u", feature);
    if (handle)
        *handle = 1;
    return HASP_STATUS_OK;
}

static unsigned int __stdcall hasp_logout(unsigned int handle)
{
    wl_log("hasp: logout");
    return HASP_STATUS_OK;
}

static unsigned int __stdcall hasp_get_size(unsigned int handle, unsigned int file, unsigned int *size)
{
    wl_log("hasp: get_size, file %u", file);
    if (size)
        *size = DONGLE_SIZE;
    return HASP_STATUS_OK;
}

static unsigned int __stdcall hasp_read(unsigned int handle, unsigned int file, unsigned int offset, unsigned int length, void *buffer)
{
    static int logged;
    if (logged++ < 50)
        wl_log("hasp: read, file %u, offset 0x%x, %u bytes", file, offset, length);
    if (!buffer)
        return HASP_STATUS_OK;
    memset(buffer, 0, length);
    if (offset < DONGLE_SIZE)
        memcpy(buffer, g_dongle + offset, offset + length <= DONGLE_SIZE ? length : DONGLE_SIZE - offset);
    return HASP_STATUS_OK;
}

static unsigned int __stdcall hasp_decrypt(unsigned int handle, void *buffer, unsigned int length)
{
    static int logged;
    if (logged++ < 20)
        wl_log("hasp: decrypt, %u bytes (left as they are)", length);
    return HASP_STATUS_OK;
}

// 5 DX+: written into the memory above (not kept once the game ends)
static unsigned int __stdcall hasp_write(unsigned int handle, unsigned int file, unsigned int offset, unsigned int length, const void *buffer)
{
    static int logged;
    if (logged++ < 50)
        wl_log("hasp: write, file %u, offset 0x%x, %u bytes", file, offset, length);
    if (buffer && offset < DONGLE_SIZE)
        memcpy(g_dongle + offset, buffer, offset + length <= DONGLE_SIZE ? length : DONGLE_SIZE - offset);
    return HASP_STATUS_OK;
}

// dll: the key's library the game imports, hasp_windows_x64_109906.dll (5) or _106482 (5 DX+); id:
// the cabinet's, its first four digits the game's (2722 the 5's, 2783 5 DX+'s, compared with 0xadf
// at +0x89db0d: E1911 otherwise)
static void wangan_dongle(const char *dll, const char *id)
{
    static const unsigned char record[16] = {0, 0, 0, 0x01, 0, 0, 0, 0x0A, 0, 0, 0, 0x04, 0x3B, 0x6B, 0x40, 0x87};
    g_dongle[0] = 0x01;
    memcpy(g_dongle + 0x10, record, sizeof record);
    memcpy(g_dongle + 0x20, record, sizeof record);
    // The cabinet's ID (not a terminal: 13 at its 5th place, 11 for a terminal; OpenParrot's), its
    // bytes' sum and that sum's complement at the memory's end
    unsigned sum = 0;
    for (int i = 0; i < 12; i++)
        sum += (unsigned char)id[i];
    memcpy(g_dongle + 0xD00, id, 12);
    g_dongle[0xD3E] = (unsigned char)sum;
    g_dongle[0xD3F] = (unsigned char)~sum;
    static const struct
    {
        int ordinal;
        void *function;
        const char *name;
    } hasp[] = {
        {13, (void *)hasp_login, "hasp_login"}, {14, (void *)hasp_logout, "hasp_logout"}, {7, (void *)hasp_get_size, "hasp_get_size"},
        {15, (void *)hasp_read, "hasp_read"},   {2, (void *)hasp_decrypt, "hasp_decrypt"},  {17, (void *)hasp_write, "hasp_write"},
    };
    HMODULE exe = GetModuleHandleW(NULL);
    for (int i = 0; i < (int)(sizeof hasp / sizeof hasp[0]); i++)
        if (!wl_patch_iat(exe, dll, MAKEINTRESOURCEA(hasp[i].ordinal), hasp[i].function, NULL) && hasp[i].function != (void *)hasp_write)
            wl_log("hasp: %s (#%d) not in the import table", hasp[i].name, hasp[i].ordinal); // write: 5 DX+'s only
}

// ------------------------------------------------------------------------------
// Wangan Midnight 5's terminal: the arcade's card machine (192.168.92.20), which the cabinets hear
// on the multicast group 225.0.0.1, port 50765 (the game listens there on all its addresses,
// +0x87b5a5). Silent, the cabinet stops 35 s after its boot check on E0505 "Network disconnection
// error". Its six packets in free play are those of OpenParrot's terminal emulator (OpenParrot,
// src/Functions/Games/ES3X/WMMT5.cpp, GPL-3.0, Teknogods): a 4-byte header (01, the sender's kind
// 04, the length that follows), a protobuf message with the terminal's serial (272211990002), its
// packet's number (offset 33) and its settings, then its CRC-32 (MSB first, 0x04C11DB7 from
// 0xFFFFFFFF, big-endian), which the game checks (+0x87bb42). They are sent as OpenParrot sends
// them, one every 8 ms, but to this PC only (127.0.0.1:50765): nothing reaches the local network.
// Heard, the terminal is kept in the game's list: OpenParrot's three NOPs in its scanner
// (+0x87bd6e) are not needed
// ------------------------------------------------------------------------------

static const unsigned char g_terminal1[79] = {
    0x01, 0x04, 0x4B, 0x00, 0x12, 0x14, 0x0A, 0x00, 0x10, 0x04, 0x18, 0x00, 0x20, 0x00, 0x28, 0x00,
    0x30, 0x00, 0x38, 0x00, 0x40, 0x00, 0x48, 0x00, 0x50, 0x00, 0x1A, 0x02, 0x5A, 0x00, 0x2A, 0x12,
    0x08, 0x12, 0x12, 0x0C, 0x32, 0x37, 0x32, 0x32, 0x31, 0x31, 0x39, 0x39, 0x30, 0x30, 0x30, 0x32,
    0x18, 0x00, 0x30, 0x03, 0x4A, 0x08, 0x08, 0x01, 0x10, 0x01, 0x18, 0x00, 0x20, 0x00, 0x52, 0x0B,
    0x08, 0x64, 0x10, 0xDE, 0x0F, 0x18, 0x05, 0x20, 0x00, 0x28, 0x00, 0xEC, 0x72, 0x00, 0x41,
};
static const unsigned char g_terminal2[139] = {
    0x01, 0x04, 0x87, 0x00, 0x12, 0x14, 0x0A, 0x00, 0x10, 0x04, 0x18, 0x00, 0x20, 0x00, 0x28, 0x00,
    0x30, 0x00, 0x38, 0x00, 0x40, 0x00, 0x48, 0x00, 0x50, 0x00, 0x1A, 0x02, 0x5A, 0x00, 0x2A, 0x12,
    0x08, 0x14, 0x12, 0x0C, 0x32, 0x37, 0x32, 0x32, 0x31, 0x31, 0x39, 0x39, 0x30, 0x30, 0x30, 0x32,
    0x18, 0x00, 0x30, 0x03, 0x42, 0x3A, 0x08, 0x01, 0x10, 0x03, 0x18, 0x02, 0x20, 0x02, 0x28, 0x04,
    0x30, 0x01, 0x38, 0x01, 0x40, 0x00, 0x48, 0x00, 0x50, 0x02, 0x58, 0x60, 0x60, 0x60, 0x68, 0x60,
    0x70, 0x60, 0x78, 0x60, 0x80, 0x01, 0x60, 0x88, 0x01, 0x60, 0x90, 0x01, 0x60, 0x98, 0x01, 0x00,
    0xA0, 0x01, 0xE2, 0xBA, 0xAC, 0xD4, 0x05, 0xA8, 0x01, 0x04, 0xB0, 0x01, 0x24, 0xB8, 0x01, 0x00,
    0x4A, 0x08, 0x08, 0x01, 0x10, 0x01, 0x18, 0x00, 0x20, 0x00, 0x52, 0x0B, 0x08, 0x64, 0x10, 0xDE,
    0x0F, 0x18, 0x05, 0x20, 0x00, 0x28, 0x00, 0x99, 0x4E, 0xC6, 0x14,
};
static const unsigned char g_terminal3[79] = {
    0x01, 0x04, 0x4B, 0x00, 0x12, 0x14, 0x0A, 0x00, 0x10, 0x04, 0x18, 0x00, 0x20, 0x00, 0x28, 0x00,
    0x30, 0x00, 0x38, 0x00, 0x40, 0x00, 0x48, 0x00, 0x50, 0x00, 0x1A, 0x02, 0x5A, 0x00, 0x2A, 0x12,
    0x08, 0x19, 0x12, 0x0C, 0x32, 0x37, 0x32, 0x32, 0x31, 0x31, 0x39, 0x39, 0x30, 0x30, 0x30, 0x32,
    0x18, 0x00, 0x30, 0x03, 0x4A, 0x08, 0x08, 0x01, 0x10, 0x01, 0x18, 0x00, 0x20, 0x00, 0x52, 0x0B,
    0x08, 0x64, 0x10, 0xDE, 0x0F, 0x18, 0x05, 0x20, 0x00, 0x28, 0x00, 0x89, 0x93, 0x3A, 0x22,
};
static const unsigned char g_terminal4[139] = {
    0x01, 0x04, 0x87, 0x00, 0x12, 0x14, 0x0A, 0x00, 0x10, 0x04, 0x18, 0x00, 0x20, 0x00, 0x28, 0x00,
    0x30, 0x00, 0x38, 0x00, 0x40, 0x00, 0x48, 0x00, 0x50, 0x00, 0x1A, 0x02, 0x5A, 0x00, 0x2A, 0x12,
    0x08, 0x2E, 0x12, 0x0C, 0x32, 0x37, 0x32, 0x32, 0x31, 0x31, 0x39, 0x39, 0x30, 0x30, 0x30, 0x32,
    0x18, 0x00, 0x30, 0x03, 0x42, 0x3A, 0x08, 0x01, 0x10, 0x03, 0x18, 0x02, 0x20, 0x02, 0x28, 0x04,
    0x30, 0x01, 0x38, 0x01, 0x40, 0x00, 0x48, 0x00, 0x50, 0x02, 0x58, 0x60, 0x60, 0x60, 0x68, 0x60,
    0x70, 0x60, 0x78, 0x60, 0x80, 0x01, 0x60, 0x88, 0x01, 0x60, 0x90, 0x01, 0x60, 0x98, 0x01, 0x00,
    0xA0, 0x01, 0xF0, 0xBA, 0xAC, 0xD4, 0x05, 0xA8, 0x01, 0x04, 0xB0, 0x01, 0x24, 0xB8, 0x01, 0x00,
    0x4A, 0x08, 0x08, 0x01, 0x10, 0x01, 0x18, 0x00, 0x20, 0x00, 0x52, 0x0B, 0x08, 0x64, 0x10, 0xDE,
    0x0F, 0x18, 0x05, 0x20, 0x00, 0x28, 0x00, 0x55, 0x42, 0x47, 0xD5,
};
static const unsigned char g_terminal5[79] = {
    0x01, 0x04, 0x4B, 0x00, 0x12, 0x14, 0x0A, 0x00, 0x10, 0x04, 0x18, 0x00, 0x20, 0x00, 0x28, 0x00,
    0x30, 0x00, 0x38, 0x00, 0x40, 0x00, 0x48, 0x00, 0x50, 0x00, 0x1A, 0x02, 0x5A, 0x00, 0x2A, 0x12,
    0x08, 0x2F, 0x12, 0x0C, 0x32, 0x37, 0x32, 0x32, 0x31, 0x31, 0x39, 0x39, 0x30, 0x30, 0x30, 0x32,
    0x18, 0x00, 0x30, 0x03, 0x4A, 0x08, 0x08, 0x01, 0x10, 0x01, 0x18, 0x00, 0x20, 0x00, 0x52, 0x0B,
    0x08, 0x64, 0x10, 0xDE, 0x0F, 0x18, 0x05, 0x20, 0x00, 0x28, 0x00, 0x9C, 0xC9, 0xE0, 0x73,
};
static const unsigned char g_terminal6[139] = {
    0x01, 0x04, 0x87, 0x00, 0x12, 0x14, 0x0A, 0x00, 0x10, 0x04, 0x18, 0x00, 0x20, 0x00, 0x28, 0x00,
    0x30, 0x00, 0x38, 0x00, 0x40, 0x00, 0x48, 0x00, 0x50, 0x00, 0x1A, 0x02, 0x5A, 0x00, 0x2A, 0x12,
    0x08, 0x6A, 0x12, 0x0C, 0x32, 0x37, 0x32, 0x32, 0x31, 0x31, 0x39, 0x39, 0x30, 0x30, 0x30, 0x32,
    0x18, 0x00, 0x30, 0x03, 0x42, 0x3A, 0x08, 0x01, 0x10, 0x03, 0x18, 0x02, 0x20, 0x02, 0x28, 0x04,
    0x30, 0x01, 0x38, 0x01, 0x40, 0x00, 0x48, 0x00, 0x50, 0x02, 0x58, 0x60, 0x60, 0x60, 0x68, 0x60,
    0x70, 0x60, 0x78, 0x60, 0x80, 0x01, 0x60, 0x88, 0x01, 0x60, 0x90, 0x01, 0x60, 0x98, 0x01, 0x00,
    0xA0, 0x01, 0xF1, 0xBA, 0xAC, 0xD4, 0x05, 0xA8, 0x01, 0x04, 0xB0, 0x01, 0x24, 0xB8, 0x01, 0x00,
    0x4A, 0x08, 0x08, 0x01, 0x10, 0x01, 0x18, 0x00, 0x20, 0x00, 0x52, 0x0B, 0x08, 0x64, 0x10, 0xDE,
    0x0F, 0x18, 0x05, 0x20, 0x00, 0x28, 0x00, 0x26, 0xB7, 0x89, 0xD0,
};

// The packets sent: the six above, with the terminal's serial of the game (bytes 36 to 47). 5 DX+
// shows the 5's in red (TERMINAL UNIT S/N, then "TERMINAL UNIT LINK NG"): its terminals are 2783
// ones, OpenParrot's 278311042069. The CRC made again over the packet but its last four bytes
static struct
{
    unsigned char bytes[139];
    int size;
} g_terminalPackets[6];

static unsigned terminal_crc(const unsigned char *data, int size)
{
    unsigned c = 0xFFFFFFFF;
    for (int i = 0; i < size; i++)
    {
        c ^= (unsigned)data[i] << 24;
        for (int k = 0; k < 8; k++)
            c = c & 0x80000000 ? c << 1 ^ 0x04C11DB7 : c << 1;
    }
    return c;
}

static void terminal_packets(const char *serial)
{
    static const struct
    {
        const unsigned char *bytes;
        int size;
    } packets[] = {
        {g_terminal1, sizeof g_terminal1}, {g_terminal2, sizeof g_terminal2}, {g_terminal3, sizeof g_terminal3},
        {g_terminal4, sizeof g_terminal4}, {g_terminal5, sizeof g_terminal5}, {g_terminal6, sizeof g_terminal6},
    };
    for (int i = 0; i < 6; i++)
    {
        unsigned char *b = g_terminalPackets[i].bytes;
        int size = packets[i].size;
        memcpy(b, packets[i].bytes, size);
        memcpy(b + 36, serial, 12);
        unsigned crc = terminal_crc(b, size - 4);
        b[size - 4] = (unsigned char)(crc >> 24);
        b[size - 3] = (unsigned char)(crc >> 16);
        b[size - 2] = (unsigned char)(crc >> 8);
        b[size - 1] = (unsigned char)crc;
        g_terminalPackets[i].size = size;
    }
}

#define SINK_PORT 50766 // the terminal's socket, where the game's sends to other machines go

static DWORD WINAPI terminal_thread(LPVOID)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa))
        return 0;
    wl_busy_enter(); // the terminal's socket is not the game's
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
    {
        wl_log("namco: the terminal's socket: error %d", WSAGetLastError());
        return 0;
    }
    // On 127.0.0.1:50766, it is also where the game's sends to other machines go (cabinet_WSASendTo):
    // it reads nothing, its small buffer once full drops them
    sockaddr_in me = {};
    me.sin_family = AF_INET;
    me.sin_port = htons(SINK_PORT);
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int small = 4096;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char *)&small, sizeof small);
    if (bind(s, (const sockaddr *)&me, sizeof me))
        wl_log("namco: the terminal's socket on 127.0.0.1:%d: error %d", SINK_PORT, WSAGetLastError());
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(50765);
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    wl_log("namco: the arcade's terminal (free play) sends its packets to 127.0.0.1:50765");
    int errors = 0;
    for (unsigned n = 0;; n++)
    {
        int i = n % 6;
        const char *packet = (const char *)g_terminalPackets[i].bytes;
        if (sendto(s, packet, g_terminalPackets[i].size, 0, (const sockaddr *)&to, sizeof to) != g_terminalPackets[i].size && errors++ < 5)
            wl_log("namco: the terminal's packet %d: error %d", i + 1, WSAGetLastError());
        Sleep(8);
    }
}

// The packets come from 127.0.0.1, and the game keeps the terminals it hears by their address
// (+0x87bc65): it is shown the terminal's, 192.168.92.20. Its receives on port 50765 (boost::asio,
// overlapped, a completion port) are noted with the place of their sender's address, rewritten
// when GetQueuedCompletionStatus gives them back
#define TERMINAL_PORT 50765
#define TERMINAL_ADDRESS 0x145CA8C0u // 192.168.92.20, in network order

static SRWLOCK g_recvLock = SRWLOCK_INIT;
static struct
{
    LPWSAOVERLAPPED ov;
    sockaddr_in *from;
} g_recvs[32];
static SOCKET g_terminalSockets[4];

static int terminal_socket(SOCKET s)
{
    for (int i = 0; i < 4; i++)
        if (g_terminalSockets[i] == s)
            return 1;
    sockaddr_in me = {};
    int size = sizeof me;
    if (getsockname(s, (sockaddr *)&me, &size) || me.sin_family != AF_INET || ntohs(me.sin_port) != TERMINAL_PORT)
        return 0;
    for (int i = 0; i < 4; i++)
        if (!g_terminalSockets[i])
        {
            g_terminalSockets[i] = s;
            break;
        }
    return 1;
}

// The cabinet alone (25/10): its packets on port 50765 from another machine are not given to the
// game (0 bytes, which it drops). Two Wangan on the same network with the same PCB ID saw each
// other: E2403 "Duplicate Board ID error" here, "同一の基板IDの筐体が存在します" on racecab1.
// 1: the packet is to be dropped
static int from_terminal(sockaddr_in *from)
{
    if (from->sin_family != AF_INET || from->sin_addr.s_addr == TERMINAL_ADDRESS) // the terminal's, shown already
        return 0;
    if (from->sin_addr.s_addr != htonl(INADDR_LOOPBACK))
    {
        static unsigned seen[8];
        static int seenCount;
        int known = 0;
        for (int i = 0; i < seenCount; i++)
            known |= seen[i] == from->sin_addr.s_addr;
        if (!known && seenCount < 8)
        {
            seen[seenCount++] = from->sin_addr.s_addr;
            const unsigned char *a = (const unsigned char *)&from->sin_addr.s_addr;
            wl_log("namco: a packet from %u.%u.%u.%u (another cabinet on the network) not given to the game", a[0], a[1], a[2], a[3]);
        }
        return 1;
    }
    from->sin_addr.s_addr = TERMINAL_ADDRESS;
    static int logged;
    if (!logged++)
        wl_log("namco: the terminal's packets shown to the game from 192.168.92.20");
    return 0;
}

// The game's own sends (boost::asio's WSASendTo, out of system.cpp's reach: its announcements to the
// other cabinets, its answers to the terminal) left the PC. Those to another machine go to
// 127.0.0.1:50766 instead, the terminal's socket, which reads nothing: they complete as sent, and no
// ICMP comes back (a closed port would fail the socket's next receive on WSAECONNRESET)
static int(WSAAPI *oWSASendTo)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, const sockaddr *, int, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
static int WSAAPI cabinet_WSASendTo(SOCKET s, LPWSABUF buffers, DWORD count, LPDWORD sent, DWORD flags, const sockaddr *to, int toLen,
                                    LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cr)
{
    const sockaddr_in *in = (const sockaddr_in *)to;
    if (!to || toLen < (int)sizeof(sockaddr_in) || in->sin_family != AF_INET || (ntohl(in->sin_addr.s_addr) >> 24) == 127)
        return oWSASendTo(s, buffers, count, sent, flags, to, toLen, ov, cr);
    static unsigned seen[8];
    static int seenCount;
    int known = 0;
    for (int i = 0; i < seenCount; i++)
        known |= seen[i] == in->sin_addr.s_addr;
    if (!known && seenCount < 8)
    {
        seen[seenCount++] = in->sin_addr.s_addr;
        const unsigned char *a = (const unsigned char *)&in->sin_addr.s_addr;
        wl_log("namco: the game's packets to %u.%u.%u.%u:%u kept on this PC", a[0], a[1], a[2], a[3], ntohs(in->sin_port));
    }
    sockaddr_in sink = {};
    sink.sin_family = AF_INET;
    sink.sin_port = htons(SINK_PORT);
    sink.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return oWSASendTo(s, buffers, count, sent, flags, (const sockaddr *)&sink, sizeof sink, ov, cr);
}

static int(WSAAPI *oWSARecvFrom)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD, sockaddr *, LPINT, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
static int WSAAPI terminal_WSARecvFrom(SOCKET s, LPWSABUF buffers, DWORD count, LPDWORD received, LPDWORD flags, sockaddr *from, LPINT fromLen,
                                       LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cr)
{
    int ours = from && ov && !cr && terminal_socket(s);
    if (ours)
    {
        AcquireSRWLockExclusive(&g_recvLock);
        for (int i = 0; i < 32; i++)
            if (!g_recvs[i].ov)
            {
                g_recvs[i].ov = ov;
                g_recvs[i].from = (sockaddr_in *)from;
                break;
            }
        ReleaseSRWLockExclusive(&g_recvLock);
    }
    int r = oWSARecvFrom(s, buffers, count, received, flags, from, fromLen, ov, cr);
    if (ours && !r) // done at once: its address is there already (the completion comes too)
        from_terminal((sockaddr_in *)from);
    return r;
}

static BOOL(WINAPI *oGetQueuedCompletionStatus)(HANDLE, LPDWORD, PULONG_PTR, LPOVERLAPPED *, DWORD);
static BOOL WINAPI terminal_GetQueuedCompletionStatus(HANDLE port, LPDWORD bytes, PULONG_PTR key, LPOVERLAPPED *ov, DWORD timeout)
{
    BOOL r = oGetQueuedCompletionStatus(port, bytes, key, ov, timeout);
    DWORD err = GetLastError();
    if (ov && *ov)
    {
        sockaddr_in *from = NULL;
        AcquireSRWLockExclusive(&g_recvLock);
        for (int i = 0; i < 32; i++)
            if (g_recvs[i].ov == (LPWSAOVERLAPPED)*ov)
            {
                from = g_recvs[i].from;
                g_recvs[i].ov = NULL;
                break;
            }
        ReleaseSRWLockExclusive(&g_recvLock);
        if (from && r && from_terminal(from) && bytes)
            *bytes = 0;
    }
    SetLastError(err);
    return r;
}

static int exists(const wchar_t *dir, const wchar_t *name)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s%s", dir, name);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

// The names the game looks up (its servers, its routers), each logged once
static int(WSAAPI *ogetaddrinfo)(PCSTR, PCSTR, const ADDRINFOA *, PADDRINFOA *);
static int WSAAPI wangan_getaddrinfo(PCSTR node, PCSTR service, const ADDRINFOA *hints, PADDRINFOA *result)
{
    int r = ogetaddrinfo(node, service, hints, result);
    static char seen[16][64];
    static int seenCount;
    int known = 0;
    for (int i = 0; i < seenCount; i++)
        known |= node && !strcmp(seen[i], node);
    if (!known && seenCount < 16)
    {
        snprintf(seen[seenCount++], sizeof seen[0], "%s", node ? node : "");
        wl_log("namco: getaddrinfo \"%s\" \"%s\" -> %d", node ? node : "", service ? service : "", r);
    }
    return r;
}

// The Wangan Midnight Maximum Tune versions: what differs between them. Their boards, the key's
// memory, the terminal's packets and the game's network are the same (above). The places were
// found by OpenParrot's patterns and the 5's code around them; the IDs are OpenParrot's
struct Wangan
{
    const char *name, *title;
    const char *keyLibrary;     // the key's library the game imports
    const char *cabinetId;      // in the key: its first four digits the game's (E1911 otherwise)
    const char *terminalSerial; // in the terminal's packets (E2403, TERMINAL UNIT LINK NG otherwise)
    DWORD keyCheck;             // mov r12b / sil, 1: the key's ID check's error flag (made 0)
    unsigned char keySet[3];
    DWORD terminalJump; // the terminal's link: its waiting step's jmp, sent to its complete step
    unsigned char toEnd[5], toComplete[5];
};

static const Wangan g_wangans[] = {
    // State +0x660: waiting +0x58e705, complete +0x58e694 (OpenParrot does it for WMMT6R, the same
    // transition and offsets); the key's ID check compares with 2722 at +0x71ae4d
    {"wmmt5", "Wangan Midnight Maximum Tune 5", "hasp_windows_x64_109906.dll", "272213990002", "272211990002", 0x58C6CA, {0x41, 0xB4, 0x01},
     0x58E70F, {0xE9, 0x76, 0x06, 0x00, 0x00}, {0xE9, 0x80, 0xFF, 0xFF, 0xFF}},
    // Its object's fields 0xf0 further (state +0x750): waiting +0x708685, complete +0x708809; 2783
    // compared at +0x89db0d
    {"wmmt5dxp", "Wangan Midnight Maximum Tune 5 DX+", "hasp_windows_x64_106482.dll", "278313042069", "278311042069", 0x706354,
     {0x40, 0xB6, 0x01}, 0x708691, {0xE9, 0x9D, 0x0C, 0x00, 0x00}, {0xE9, 0x73, 0x01, 0x00, 0x00}},
    // State +0x738: waiting +0x6a0c7c, complete +0x6a0d5c; 2808 compared at +0x8bbbc2
    {"wmmt6", "Wangan Midnight Maximum Tune 6", "hasp_windows_x64_28756.dll", "280813990002", "280811990002", 0x69E928, {0x41, 0xB4, 0x01},
     0x6A0C86, {0xE9, 0x4E, 0x0B, 0x00, 0x00}, {0xE9, 0xD1, 0x00, 0x00, 0x00}},
};

int wl_game_namco(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    wchar_t dir[MAX_PATH];
    wcsncpy_s(dir, MAX_PATH, exePath, base - exePath);
    int wmmt5 = !_wcsicmp(base, L"wmn5r.exe"), wmmt6 = !_wcsicmp(base, L"wmn6r.exe");
    if (!wmmt5 && !wmmt6 && _wcsicmp(base, L"MK_AGP3_FINAL.exe"))
        return 0;

    // Its drives F:\ and G:\ (system, bookkeeping, dongle data, test mode settings) are the copy's
    // F* and G* folders and files: F:\system\x is Fsystem\x, G:\boot_limit_N.bin Gboot_limit_N.bin.
    // Wangan 6: both in its TP folder, as OpenParrot keeps them (the copies have them there)
    char folder[MAX_PATH], to[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, dir, -1, folder, sizeof folder, NULL, NULL);
    snprintf(to, sizeof to, wmmt6 ? "%sTP\\" : "%sF", folder);
    wl_files_redirect_prefix("F:\\", to);
    snprintf(to, sizeof to, wmmt6 ? "%sTP\\" : "%sG", folder);
    wl_files_redirect_prefix("G:\\", to);

    if (wmmt5 || wmmt6)
    {
        // 5 DX+ (2016) has the same program's name as the 5, its own key's library beside it
        const Wangan *w = &g_wangans[wmmt6 ? 2 : exists(dir, L"hasp_windows_x64_106482.dll") ? 1 : 0];
        *name = w->name;
        *title = w->title;
        wl_log("game: %s (Namco System ES3X)", *title);
        wl_game_set_boards("Controls on JVS board, Namco NA-JV Ver6.01, JVS 3.0 (COM3)", "Motor board: Namco motor board (COM1)");
        char probe[4];
        g_wmmt5Probe = GetEnvironmentVariableA("WL_WMMT5_PROBE", probe, sizeof probe) != 0;
        wl_inputs_set_table(g_wmmt5Inputs, g_wmmt5Probe ? W_COUNT : W_PROBE);
        g_steering = &g_wmmt5Inputs[W_STEERING];
        wl_jvs_register(&g_jvsWmmt5);
        wl_game_set_options(g_wmmt5Options, (int)(sizeof g_wmmt5Options / sizeof g_wmmt5Options[0]));
        wl_game_on_settings(wmmt5_settings);
        InitializeCriticalSection(&g_motorLock);
        wl_vdev_register(&g_str3);
        wl_vdev_label("COM1", "Carte du moteur du volant");
        wangan_dongle(w->keyLibrary, w->cabinetId);
        if (wmmt6)
            bngrw_patch();
        // E0505 "network: interface not found": the game looks for its card in the cabinets'
        // network, 192.168.92.0/24 (its addresses table, +0x18a7260: .254, .11, .20, .253)
        static const unsigned char mac[6] = {0x00, 0x18, 0xD8, 0x00, 0x00, 0x0B};
        wl_netenv_fake(0xC0A85C0B, 0xC0A85CFE, mac);
        // E1911 "USB DONGLE error2": the key's ID is compared with the cabinet's, given by its
        // "auth" COM class (not registered here: "auth: can't get instance(80040154)"); that one
        // empty is the error (+0x58c63a). The error's flag stays clear (mov r12b, 1 made 0, as
        // OpenParrot's WMMT5.cpp does; 5 DX+: mov sil, 1, the places found by OpenParrot's pattern)
        unsigned char *exe = (unsigned char *)GetModuleHandleW(NULL);
        unsigned char clear[3];
        memcpy(clear, w->keySet, 3);
        clear[2] = 0;
        wl_patch_code(exe + w->keyCheck, w->keySet, clear, 3, "the key's ID left unchecked");
        // The arcade's terminal (above): its packets on port 50765, shown from its address. Without
        // it, E0505 "Network disconnection error" (+0x5814cc: the link's state 4, 35 s after boot)
        terminal_packets(w->terminalSerial);
        CloseHandle(CreateThread(NULL, 0, terminal_thread, NULL, 0, NULL));
        if (!wl_patch_iat((HMODULE)exe, "WS2_32.dll", "WSARecvFrom", (void *)terminal_WSARecvFrom, (void **)&oWSARecvFrom) ||
            !wl_patch_iat((HMODULE)exe, "KERNEL32.dll", "GetQueuedCompletionStatus", (void *)terminal_GetQueuedCompletionStatus,
                          (void **)&oGetQueuedCompletionStatus))
            wl_log("namco: WSARecvFrom or GetQueuedCompletionStatus not in the import table: the terminal's address left as it is");
        if (!wl_patch_iat((HMODULE)exe, "WS2_32.dll", "WSASendTo", (void *)cabinet_WSASendTo, (void **)&oWSASendTo))
            wl_log("namco: WSASendTo not in the import table: the game's packets may reach the network");
        wl_patch_iat((HMODULE)exe, "WS2_32.dll", "getaddrinfo", (void *)wangan_getaddrinfo, (void **)&ogetaddrinfo);
        // The boot check's terminal link: heard, the terminal still answers nothing (TERMINAL UNIT
        // LINK NG, the boot stopped at GAME SERVER CONNECT CHECK). Its "waiting" step's jump goes to
        // the "complete" step instead of the state machine's end, which came back to waiting forever
        wl_patch_code(exe + w->terminalJump, w->toEnd, w->toComplete, 5, "the terminal's link taken as complete");
        // Wangan 6 stopped on CERTIFICATE CHECK (its ALL.Net, Mucha, is not here): the state the
        // terminal's complete step compares with 2 (+0x718) is set 2 instead of 0 when the auth's
        // state is none of its three (jmp over that mov made nops, +0x690876, as OpenParrot does)
        if (wmmt6)
        {
            static const unsigned char jump[2] = {0xEB, 0x22}, nops[2] = {0x90, 0x90};
            wl_patch_code(exe + 0x690876, jump, nops, 2, "the certificate check taken as passed");
            static const unsigned char je[2] = {0x74, 0x5C};
            wl_patch_code(exe + 0xA131B3, je, nops, 2, "OpenParrot's cabinet nop (+0xa131b3)");
            // Then GAME SERVER CONNECT CHECK (state +0x798): it waits for the game server's answer
            // (message 0xe, 3 or 4) from the ALL.Net auth, which E0516 "ALL.Net AUTH ERROR" never
            // sends, or for its object at +0x790 to be done: the step goes on (jne made jmp, +0x6a0df3)
            static const unsigned char jne[2] = {0x75, 0x0D}, jmp[2] = {0xEB, 0x0D};
            wl_patch_code(exe + 0x6A0DF3, jne, jmp, 2, "the game server's check left");
            // E0552 "OFFLINE UPTIME ERROR": 36 h (0x1fa40 s) since the last authentication, which
            // never comes: the limit taken as not reached (jl made jmp, +0x6916bd), as for Mario
            // Kart DX's
            static const unsigned char jl[2] = {0x7C, 0x41}, jmpUptime[2] = {0xEB, 0x41};
            wl_patch_code(exe + 0x6916BD, jl, jmpUptime, 2, "the offline uptime's limit not reached");
            // Its warning an hour before (35 h, +0x68f6a0): "Please connect this unit to the network.
            // The Game will shutdown shortly." over the whole game; never due (setge al made xor al, al)
            static const unsigned char setge[3] = {0x0F, 0x9D, 0xC0}, never[3] = {0x32, 0xC0, 0x90};
            wl_patch_code(exe + 0x68F6D0, setge, never, 3, "the offline uptime's warning not shown");
        }
        return 1;
    }
    // v1.18 has the card reader's bngrw.dll beside it; v1.10 (2015) has not, nor its patches' places
    int v118 = exists(dir, L"bngrw.dll");
    *name = "mkdx";
    *title = "Mario Kart Arcade GP DX";
    wl_log("game: %s %s (Namco System ES3)", *title, v118 ? "v1.18" : "v1.10");
    wl_game_set_boards("Controls on JVS board, Namco NA-JV Ver6.01, JVS 3.0 (COM3)",
                       "Motor board: Namco motor board (COM1, 19200 bauds: position servo with spring, damper, vibration)");
    wl_inputs_set_table(g_inputs, IN_COUNT);
    wl_vdinput_register(&g_wheel);
    wl_jvs_register(&g_jvs);
    InitializeCriticalSection(&g_motorLock);
    wl_vdev_register(&g_com1);
    wl_vdev_label("COM1", "Carte du moteur du volant");

    // ShareK's client not started: je to the function's end made a jmp (nop; jmp rel32). The exe
    // moves (ASLR): addresses from its base
    unsigned char *exe = (unsigned char *)GetModuleHandleW(NULL);
    static const unsigned char je[2] = {0x0F, 0x84}, jmp[2] = {0x90, 0xE9};
    if (v118)
    {
        wl_patch_code(exe + 0x7E80D, je, jmp, 2, "ShareK's client not started (its COM class is not registered)");
        bngrw_patch();
    }
    // The boot check's link step looks for the other cabinets by broadcast on UDP 20199: alone, the
    // cabinet hears itself (nothing leaves the PC); linked, its packets go to the other cabinets
    wl_system_loopback_broadcast(1);
    wl_game_set_options(g_mkdxOptions, (int)(sizeof g_mkdxOptions / sizeof g_mkdxOptions[0]));
    wl_game_on_settings(mkdx_settings);
    if (!patch_ws2((HMODULE)exe, 2, "bind", (void *)link_bind, (void **)&olink_bind) ||
        !patch_ws2((HMODULE)exe, 20, "sendto", (void *)link_sendto, (void **)&olink_sendto) ||
        !patch_ws2((HMODULE)exe, 17, "recvfrom", (void *)link_recvfrom, (void **)&olink_recvfrom))
        wl_log("namco: bind, sendto or recvfrom not in the import table: no link");
    void *was;
    wl_patch_iat((HMODULE)exe, "KERNEL32.dll", "CreateMutexA", (void *)link_CreateMutexA, (void **)&olink_CreateMutexA);
    wl_patch_iat((HMODULE)exe, "KERNEL32.dll", "CreateMutexW", (void *)link_CreateMutexW, (void **)&olink_CreateMutexW);
    if (wl_patch_iat((HMODULE)exe, "WS2_32.dll", "WSAIoctl", (void *)cabinet_WSAIoctl, (void **)&oWSAIoctl) &&
        wl_patch_iat((HMODULE)exe, "IPHLPAPI.DLL", "GetAdaptersInfo", (void *)cabinet_GetAdaptersInfo, &was))
        wl_log("game: the cabinets' network shown to the game (192.168.112.11, gateway 192.168.112.1)");
    wl_patch_iat((HMODULE)exe, "IPHLPAPI.DLL", "DeleteIpForwardEntry", (void *)cabinet_route, &was);
    wl_patch_iat((HMODULE)exe, "IPHLPAPI.DLL", "SetIpForwardEntry", (void *)cabinet_route, &was);
    wl_patch_iat((HMODULE)exe, "IPHLPAPI.DLL", "IcmpSendEcho", (void *)cabinet_IcmpSendEcho, &was);
    if (v118)
    {
        wl_hook_code(exe + 0x4B17B0, (void *)cabinet_key, &was, "the cabinet's security key (MK_AGP3_FINAL.exe+0x4b17b0)");
        wl_hook_code(exe + 0x62E60, (void *)cabinet_auth_recent, &was, "the network authentication's age (MK_AGP3_FINAL.exe+0x62e60)");
        wl_hook_code(exe + 0x68430, (void *)cabinet_offline_limit, &was, "the offline time limit (MK_AGP3_FINAL.exe+0x68430)");
    }
    else // v1.10: the same key's function (found by the key's interface GUID it looks for)
    {
        wl_hook_code(exe + 0x43AEF0, (void *)cabinet_key, &was, "the cabinet's security key (MK_AGP3_FINAL.exe+0x43aef0, v1.10)");
        // The same two as v1.18's: the first use of "last_auth.bin" (v1.18's +0x62e60, the same
        // start), the _time64 compared with the date it went offline (the same bytes)
        wl_hook_code(exe + 0x5B060, (void *)cabinet_auth_recent, &was, "the network authentication's age (MK_AGP3_FINAL.exe+0x5b060, v1.10)");
        wl_hook_code(exe + 0x5F630, (void *)cabinet_offline_limit, &was, "the offline time limit (MK_AGP3_FINAL.exe+0x5f630, v1.10)");
        InitializeCriticalSection(&g_nfcLock);
        wl_vdev_register(&g_com4);
        wl_vdev_label("COM4", "Lecteur de cartes");
    }
    return 1;
}
