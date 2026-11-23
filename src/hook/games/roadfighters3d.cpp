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

// Konami PC: Road Fighters 3D (2010, game code JGT, Direct3D 9, GTI Club's engine). The game has
// no program of its own: it is jgt.dll (exports dll_entry_init and dll_entry_main), which the
// cabinet's launcher boots; here windowsloader's wl_avsboot.exe (src/avsboot/avsboot.cpp), started
// by the launcher with jgt.dll's folder as its working folder and shown to the game as
// <that folder>\wl_avsboot.exe. Reimplemented from windowsarcadeloader's findings (aderumier, GPL
// v3, https://github.com/aderumier/windowsarcadeloader: systemprofiles/konami/road-fighters-3d.yaml,
// crates/payload-konami/src: rf3d.rs, acio.rs, hbhi.rs, hgth.rs, icca.rs, kbt.rs, lib.rs; its
// docs/IMPLEMENTATION.md, "Road Fighters 3D"), where a payload takes libacio.dll's place.
//
// Here the cabinet's libraries stay, and the game's calls are answered:
// - libacio.dll, the first ACIO bus (imported by jgt.dll): the HBHI I/O board (switches, coins,
//   security plugs, lamps) and the ICCA card reader. jgt.dll's 34 imports of it pointed at our
//   functions (its import table, when the host calls dll_entry_init).
// - libacioEx.dll, the second bus (LoadLibraryA, then GetProcAddress of 70 functions, every one
//   needed): the HGTH board, the wheel, the pedals and the wheel's motor (and the HBHI and HDXS
//   functions of that bus). The DLL is played by windowsloader (wl_system_virtual_dll): the dump's
//   libacioex.dll is never loaded.
// - kbt.dll and kld.dll, the cabinet's boot check libraries the game finds with GetModuleHandle
//   (bt_get_ikey_status: its dongles; bt_fcheck_*: its files' check), answered when they are not
//   in the game's folder (the host loads them when they are), as spice2x's stubs answer.
// - The PC's keyboard (GetKeyState, the DirectInput keyboard): no key for the game, only the
//   boards drive it.
//
// The HBHI board's 15-byte status packet: byte 1 test 0x20, service 0x10, coin mech 0x04; byte 8
// VIEW 0x04, 2D/3D 0x08, the shift lever's four switches up 0x10, down 0x20, left 0x40, right 0x80
// (an H pattern of 6 gears: 1 up-left, 2 down-left, 3 up, 4 down, 5 up-right, 6 down-right). No
// START: the accelerator starts and confirms, the wheel selects. Test menu: the lever up and down
// move, VIEW executes. The HGTH board's 32-byte receive buffer: the wheel at +2, the accelerator at
// +4, the brake at +6, 16 bits each at full range (the wheel's centre 0x7FFF). Its motor's 4 bytes:
// bytes 0-1 a 12-bit torque centred on 0x800 (0 the motor off), 0x8F8 with the wheel at full right
// and 0x708 at full left at low speed: the force, against the wheel (bytes 2-3 unused).
//
// Not played: the link between cabinets (the game's CABINET ID; jgt_link_cab2.dll is cabinet 2's
// build), the card reader's keypad (offline, the e-Amusement cards are of no use).

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "ffb.h"
#include "filecrc.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "lang.h"
#include "log.h"
#include "outputs.h"

#if defined(_M_IX86)

// The game's builds, by their CRC-32: jgt.dll (also the cabinet's jgt_single.dll and
// jgt_link_cab1.dll) and jgt_link_cab2.dll (one byte apart, at rva 0x5FB998: 0 in jgt.dll, 1 in
// cabinet 2's)
static const DWORD g_builds[] = {0x68306E6F, 0x94023423};

// ------------------------------------------------------------------------------
// The controls
// ------------------------------------------------------------------------------

enum
{
    IN_STEERING,
    IN_GAS,
    IN_BRAKE,
    IN_TEST,
    IN_SERVICE,
    IN_COIN,
    IN_VIEW,
    IN_3D,
    IN_GEAR_UP,
    IN_GEAR_DOWN,
    IN_GEAR1, // .. IN_GEAR1 + 5: the H pattern's gears, held while in gear
    IN_LEVER_UP = IN_GEAR1 + 6,
    IN_LEVER_DOWN,
    IN_LEVER_LEFT,
    IN_LEVER_RIGHT,
    IN_CARD,
    IN_COUNT
};

static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 65535, 32767},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 65535, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 65535, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "P1_Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"View", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"2D3D", "View1", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftUp", "GearUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftDown", "GearDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear1", "Gear1", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear2", "Gear2", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear3", "Gear3", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear4", "Gear4", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear5", "Gear5", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear6", "Gear6", WL_INPUT_SWITCH, 0, 1, 0},
    {"LeverUp", "SelectUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"LeverDown", "SelectDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"LeverLeft", "SelectLeft", WL_INPUT_SWITCH, 0, 1, 0},
    {"LeverRight", "SelectRight", WL_INPUT_SWITCH, 0, 1, 0},
    {"Card", "EntryKey", WL_INPUT_SWITCH, 0, 1, 0},
};

static int held(int i)
{
    return wl_input_get(&g_inputs[i]) != 0;
}

#define LEVER_UP 0x10
#define LEVER_DOWN 0x20
#define LEVER_LEFT 0x40
#define LEVER_RIGHT 0x80

// The lever's switches in gears 1 to 6
static const unsigned char g_gears[6] = {LEVER_UP | LEVER_LEFT, LEVER_DOWN | LEVER_LEFT, LEVER_UP,
                                         LEVER_DOWN,            LEVER_UP | LEVER_RIGHT, LEVER_DOWN | LEVER_RIGHT};

// ------------------------------------------------------------------------------
// The HBHI board
// ------------------------------------------------------------------------------

static volatile LONG g_coinStock; // coins in, not taken by the game yet
static volatile LONG g_packets;
static volatile LONG64 g_clock; // the packet's time, which the game watches (unchanged 20 s: an I/O error)

// GearUp and GearDown move a sequential gear through the H pattern (0 neutral, 1..6); the lever
// moved by hand (Gear1..Gear6, the four switches) puts it back to neutral
static int g_gear, g_gearUpHeld, g_gearDownHeld, g_coinHeld;

static void hbhi_packet(unsigned char raw[15])
{
    memset(raw, 0, 15);
    if (held(IN_TEST))
        raw[1] |= 0x20;
    if (held(IN_SERVICE))
        raw[1] |= 0x10;
    int coin = held(IN_COIN);
    if (coin)
        raw[1] |= 0x04;
    if (coin && !g_coinHeld)
        InterlockedIncrement(&g_coinStock);
    g_coinHeld = coin;
    if (held(IN_VIEW))
        raw[8] |= 0x04;
    if (held(IN_3D))
        raw[8] |= 0x08;
    unsigned char lever = 0;
    for (int g = 0; g < 6; g++)
        if (held(IN_GEAR1 + g))
            lever |= g_gears[g];
    static const unsigned char switches[4] = {LEVER_UP, LEVER_DOWN, LEVER_LEFT, LEVER_RIGHT};
    for (int s = 0; s < 4; s++)
        if (held(IN_LEVER_UP + s))
            lever |= switches[s];
    int up = held(IN_GEAR_UP), down = held(IN_GEAR_DOWN);
    if (lever)
        g_gear = 0;
    if (up && !g_gearUpHeld && g_gear < 6)
        g_gear++;
    if (down && !g_gearDownHeld && g_gear > 0)
        g_gear--;
    g_gearUpHeld = up;
    g_gearDownHeld = down;
    if (!lever && g_gear > 0)
        lever = g_gears[g_gear - 1];
    raw[8] |= lever;
}

static int __cdecl hbhi_update_control_status_buffer(void)
{
    InterlockedIncrement(&g_packets);
    return 1;
}

// The game's 0x40 bytes: the packet at +4, then what libacio makes of it: +0x14 the 8 analog
// channels (10 bits, left-aligned), +0x24 and +0x25 the coin counters of slots 1 and 2, +0x28 the
// packets' count, +0x38 the packet's time
static void __cdecl hbhi_get_control_status_buffer(unsigned char *out)
{
    if (!out)
        return;
    unsigned char raw[15], buf[0x40] = {0};
    hbhi_packet(raw);
    memcpy(buf + 4, raw, 15);
    unsigned short analog[8] = {
        (unsigned short)(raw[0] << 8 | (raw[1] & 0xC0)),  (unsigned short)(raw[2] << 8 | (raw[3] & 0xC0)),
        (unsigned short)(raw[4] << 8 | (raw[5] & 0xC0)),  (unsigned short)(raw[6] << 8 | (raw[7] & 0xC0)),
        (unsigned short)(raw[10] << 8 | (raw[9] & 0xC0)), (unsigned short)(raw[11] << 8 | (raw[9] & 0x30) << 2),
        (unsigned short)(raw[12] << 8 | (raw[9] & 0x0C) << 4), (unsigned short)(raw[13] << 8 | (raw[9] & 0x03) << 6),
    };
    memcpy(buf + 0x14, analog, sizeof analog);
    buf[0x24] = raw[5] & 0x3F;
    buf[0x25] = raw[7] & 0x3F;
    LONG packets = g_packets;
    memcpy(buf + 0x28, &packets, 4);
    LONG64 clock = InterlockedIncrement64(&g_clock);
    memcpy(buf + 0x38, &clock, 8);
    memcpy(out, buf, sizeof buf);
}

static int __cdecl hbhi_current_coinstock(int slot, DWORD *stock)
{
    if (stock)
        *stock = (DWORD)g_coinStock;
    return 1;
}

static int __cdecl hbhi_consume_coinstock(int slot, DWORD coins)
{
    for (;;)
    {
        LONG was = g_coinStock, now = was > (LONG)coins ? was - (LONG)coins : 0;
        if (InterlockedCompareExchange(&g_coinStock, now, was) == was)
            return 1;
    }
}

// The security plugs (the game's software plug, the cabinet's system plug): found at once, 0x12;
// the missing plug check passed, 8
#define PLUG_FOUND 0x12
#define PLUGS_PRESENT 8

static int __cdecl hbhi_req_secplug_check(const char *id)
{
    static volatile LONG logged;
    if (id && !InterlockedExchange(&logged, 1))
        wl_log("road fighters 3d: software plug %.16s present", id);
    return 1;
}

static int __cdecl hbhi_req_secplug_check_systemplug(void)
{
    return 1;
}

static int __cdecl hbhi_req_secplug_check_isfinished(DWORD *status)
{
    if (status)
        *status = PLUG_FOUND;
    return 1;
}

static int __cdecl hbhi_req_secplug_missing_check_isfinished(DWORD *status)
{
    if (status)
        *status = PLUGS_PRESENT;
    return 1;
}

// A plug's id as libacio writes it: "0140" and its 8 bytes in hex
static int plug_id(char *out, const unsigned char id[8])
{
    if (!out)
        return 0;
    strcpy(out, "0140");
    for (int i = 0; i < 8; i++)
        sprintf(out + 4 + i * 2, "%02X", id[i]);
    return 1;
}

static int __cdecl hbhi_get_softwareid(char *out)
{
    // windowsarcadeloader's answer, with which the game boots (the plug's name in jgt.dll is
    // GQJGTJAA)
    static const unsigned char id[8] = {'G', 'L', 'H', '2', '4', 'J', 'A', 'A'};
    return plug_id(out, id);
}

static int __cdecl hbhi_get_systemid(char *out)
{
    static const unsigned char id[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    return plug_id(out, id);
}

// The lamps (8, a bit each; on and off kept only out of the brightness mode, mode 1), for the
// network outputs ([Outputs] OUTPUTS_NETWORK)
#define RF3D_LAMPS 8
static volatile LONG g_lampMask;
static int g_lampBrightMode;

static int __cdecl hbhi_control_lamp_on(int mask)
{
    if (!g_lampBrightMode)
        InterlockedOr(&g_lampMask, mask & 0xFF);
    wl_outputs_lamps((unsigned)g_lampMask, RF3D_LAMPS);
    return 1;
}

static int __cdecl hbhi_control_lamp_off(int mask)
{
    if (!g_lampBrightMode)
        InterlockedAnd(&g_lampMask, ~mask & 0xFF);
    wl_outputs_lamps((unsigned)g_lampMask, RF3D_LAMPS);
    return 1;
}

static int __cdecl hbhi_control_lamp_mode(int mode)
{
    if (mode == 0 || mode == 1)
        g_lampBrightMode = mode;
    return 1;
}

// The requests answered at once, done, and the board's other outputs (coin counters and blockers,
// lamps' brightness, parallel outputs, volume, watchdog): nothing to drive here
static int __cdecl acio_true(void)
{
    return 1;
}

static int __cdecl acio_false(void)
{
    return 0;
}

static int __cdecl acio_status0(DWORD *status)
{
    if (status)
        *status = 0;
    return 1;
}

// ------------------------------------------------------------------------------
// The bus itself (both: libacio.dll and libacioEx.dll)
// ------------------------------------------------------------------------------

// The game's version text, as much of it as can be read
static void read_text(const char *from, char *to, size_t size)
{
    size_t n = 0;
    __try
    {
        for (; from && n + 1 < size && from[n]; n++)
            to[n] = from[n];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    to[n] = 0;
}

// (port, version, out, flags); another game's libacio takes (port, device, version, out, flags):
// the version is the second argument unless it is a small number
static int __cdecl acio_begin(DWORD a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d, DWORD e)
{
    char version[33];
    read_text((const char *)(b < 0x10000 ? c : b), version, sizeof version);
    DWORD *out = (DWORD *)(b < 0x10000 ? d : c);
    wl_log("road fighters 3d: ACIO bus started (game %s)", version);
    if (out)
        *out = 0;
    return 1;
}

// A bus whose devices were all found
static int __cdecl acio_is_active2(DWORD a, DWORD *status)
{
    if (status)
        *status = 6;
    return 1;
}

static void __cdecl acio_update(void)
{
}

// A device's version record (0x2C bytes): firmware 2.0.0 (byte 5), the rest zeros
static int __cdecl acio_get_version(unsigned char *out, DWORD device)
{
    if (out)
    {
        memset(out, 0, 0x2C);
        out[5] = 2;
    }
    wl_log("road fighters 3d: ACIO device %lu present", device);
    return 1;
}

static int __cdecl acio_get_firmware_update_device_index(void)
{
    return 0xFF; // none waits for a new firmware
}

// The serial line's statistics (0x58 bytes): nothing lost
static void __cdecl acio_get_rs232c_status(unsigned char *out)
{
    if (out)
        memset(out, 0, 0x58);
}

static DWORD __cdecl acio_get_log(void *out, DWORD size)
{
    return 0;
}

// ------------------------------------------------------------------------------
// The HGTH board: the wheel, the pedals, the wheel's motor
// ------------------------------------------------------------------------------

static void __cdecl hgth_get_recvdata(unsigned char *out)
{
    if (!out)
        return;
    unsigned char buf[32] = {0};
    unsigned short v[3] = {(unsigned short)wl_input_get(&g_inputs[IN_STEERING]), (unsigned short)wl_input_get(&g_inputs[IN_GAS]),
                           (unsigned short)wl_input_get(&g_inputs[IN_BRAKE])};
    memcpy(buf + 2, v, sizeof v);
    memcpy(out, buf, sizeof buf);
}

// The motor's bytes: a 12-bit torque centred on 0x800 (0: off), larger with the wheel turned right
// (pushed back to the centre: to the left, wl_ffb_set's > 0), 0x708..0x8F8 its full travel
static int __cdecl hgth_set_senddata(const unsigned char *data)
{
    if (!data)
        return 1;
    static DWORD last = 0xFFFFFFFF;
    static int logged;
    DWORD packed;
    memcpy(&packed, data, 4);
    if (packed != last)
    {
        last = packed;
        if (logged < 30)
        {
            logged++;
            wl_log("road fighters 3d: motor %02x %02x %02x %02x", data[0], data[1], data[2], data[3]);
        }
        // The game's torque stays within 0x708..0x8F8 (full left .. full right, seen on the
        // cabinet): that range is the motor's full force (over the 12 bits' 0x7FF it peaked at 12%)
        int raw = data[0] | data[1] << 8;
        double f = raw ? (raw - 0x800) / 248.0 : 0;
        wl_ffb_set(f < -1 ? -1 : f > 1 ? 1 : f);
    }
    return 1;
}

// The wheel's travel limit, set at once
static int __cdecl hgth_set_handle_limit(int limit, DWORD *status)
{
    if (status)
        *status = 1;
    return 1;
}

static int __cdecl hgth_set_handle_limit_isfinished(DWORD *status)
{
    if (status)
        *status = 2;
    return 1;
}

// The HDXS board (the cabinet's LEDs): not there, its status empty
static void __cdecl hdxs_get_control_status_buffer(unsigned char *out)
{
    if (out)
        memset(out, 0, 0x40);
}

// ------------------------------------------------------------------------------
// The ICCA card reader, its workflow (the game steers it through states, polls its 24-byte
// status). The card (EntryKey) is presented until the game ejects it
// ------------------------------------------------------------------------------

enum
{
    WF_STEP = 0,
    WF_SLEEP = 1,
    WF_START = 2,
    WF_INIT = 3,
    WF_READY = 4,
    WF_GET_USERID = 5,
    WF_ACTIVE = 6,
    WF_EJECT = 7,
    WF_EJECT_CHECK = 8,
    WF_CLOSE_EJECT = 10,
    WF_CLOSE_END = 12,
};

static volatile LONG g_workflow = WF_STEP, g_cardIn;
static int g_cardHeld;

// An e-Amusement card's id (E004...)
static const unsigned char g_cardId[8] = {0xE0, 0x04, 0x01, 0x00, 0x27, 0x10, 0x4B, 0x1D};

static void card_update(void)
{
    int pressed = held(IN_CARD);
    if (pressed && !g_cardHeld)
    {
        if (g_workflow == WF_EJECT_CHECK)
        {
            g_cardIn = 0;
            g_workflow = WF_SLEEP;
            wl_log("road fighters 3d: card taken");
        }
        else
        {
            g_cardIn = 1;
            g_workflow = WF_ACTIVE;
            wl_log("road fighters 3d: card presented");
        }
    }
    g_cardHeld = pressed;
}

static int __cdecl icca_cardunit_init_isfinished(DWORD device, DWORD *status)
{
    if (status)
        *status = 4; // ready
    return 1;
}

static int __cdecl icca_req_uid_isfinished(DWORD device, DWORD *status)
{
    if (status)
        *status = g_cardIn ? 9 : 0xB; // a card, its id readable; no card
    return 1;
}

static int __cdecl icca_workflow(int workflow, DWORD device)
{
    int card = g_cardIn != 0;
    LONG state;
    switch (workflow)
    {
    case WF_INIT:
        state = WF_READY;
        break;
    case WF_START:
        state = card ? WF_ACTIVE : WF_READY;
        break;
    case WF_EJECT:
        g_cardIn = 0;
        state = g_workflow;
        break;
    case WF_CLOSE_EJECT:
        state = card ? WF_EJECT_CHECK : WF_SLEEP;
        break;
    case WF_CLOSE_END:
        state = WF_SLEEP;
        break;
    case WF_STEP:
    case WF_SLEEP:
    case WF_GET_USERID:
        state = workflow;
        break;
    default:
        state = g_workflow;
        break;
    }
    g_workflow = state;
    return state;
}

// (out, device); some games pass (device, out): the pointer is the argument that is not a small
// device number. Byte 0 the state, 2 and 3 the front and rear sensors, 4..11 the card's id, 16..23
// the keypad's edges and levels (none)
static int __cdecl icca_get_status(ULONG_PTR a, ULONG_PTR b)
{
    unsigned char *out = (unsigned char *)(b > 2 ? b : a);
    if ((ULONG_PTR)out <= 2)
        return 0;
    card_update();
    unsigned char status[24] = {0};
    status[0] = (unsigned char)g_workflow;
    if (g_cardIn)
    {
        status[2] = 1;
        status[3] = 1;
        memcpy(status + 4, g_cardId, 8);
    }
    memcpy(out, status, sizeof status);
    return 1;
}

// The card's id and a FeliCa flag (byte 8: an e-Amusement card is not one)
static int __cdecl icca_get_uid_felica(DWORD device, unsigned char *out)
{
    if (!out)
        return 0;
    if (g_cardIn)
        memcpy(out, g_cardId, 8);
    else
        memset(out, 0, 8);
    out[8] = 0;
    return 1;
}

// ------------------------------------------------------------------------------
// The two buses' functions: jgt.dll's imports of libacio.dll, libacioEx.dll's table
// ------------------------------------------------------------------------------

#define FN(name, fn) {#name, 0, (void *)fn}

// jgt.dll's imports of libacio.dll
static const WlVirtualDllFunction g_acio[] = {
    FN(ac_io_begin, acio_begin),
    FN(ac_io_begin_get_status, acio_true),
    FN(ac_io_end, acio_true),
    FN(ac_io_end_get_status, acio_true),
    FN(ac_io_update, acio_update),
    FN(ac_io_is_active2, acio_is_active2),
    FN(ac_io_is_active_device, acio_true),
    FN(ac_io_get_version, acio_get_version),
    FN(ac_io_get_rs232c_status, acio_get_rs232c_status),
    FN(ac_io_soft_watch_dog_on, acio_true),
    FN(ac_io_soft_watch_dog_off, acio_true),
    FN(ac_io_hbhi_update_control_status_buffer, hbhi_update_control_status_buffer),
    FN(ac_io_hbhi_get_control_status_buffer, hbhi_get_control_status_buffer),
    FN(ac_io_hbhi_current_coinstock, hbhi_current_coinstock),
    FN(ac_io_hbhi_consume_coinstock, hbhi_consume_coinstock),
    FN(ac_io_hbhi_lock_coincounter, acio_true),
    FN(ac_io_hbhi_unlock_coincounter, acio_true),
    FN(ac_io_hbhi_control_coin_blocker_open, acio_true),
    FN(ac_io_hbhi_control_coin_blocker_close, acio_true),
    FN(ac_io_hbhi_control_lamp_mode, hbhi_control_lamp_mode),
    FN(ac_io_hbhi_control_lamp_on, hbhi_control_lamp_on),
    FN(ac_io_hbhi_control_lamp_off, hbhi_control_lamp_off),
    FN(ac_io_hbhi_control_lamp_bright, acio_true),
    FN(ac_io_hbhi_get_softwareid, hbhi_get_softwareid),
    FN(ac_io_hbhi_get_systemid, hbhi_get_systemid),
    FN(ac_io_hbhi_get_watchdog_status, acio_true),
    FN(ac_io_hbhi_req_secplug_check_isfinished, hbhi_req_secplug_check_isfinished),
    FN(ac_io_icca_cardunit_init, acio_true),
    FN(ac_io_icca_cardunit_init_isfinished, icca_cardunit_init_isfinished),
    FN(ac_io_icca_req_uid, acio_true),
    FN(ac_io_icca_req_uid_isfinished, icca_req_uid_isfinished),
    FN(ac_io_icca_get_uid_felica, icca_get_uid_felica),
    FN(ac_io_icca_workflow, icca_workflow),
    FN(ac_io_icca_get_status, icca_get_status),
};

// libacioEx.dll's, the names jgt.dll looks up there (in this order, from rva 0x61E244, after the
// DLL's name)
static const WlVirtualDllFunction g_acioEx[] = {
    FN(ac_io_begin, acio_begin),
    FN(ac_io_end, acio_true),
    FN(ac_io_get_rs232c_status, acio_get_rs232c_status),
    FN(ac_io_get_send_log, acio_get_log),
    FN(ac_io_get_recv_log, acio_get_log),
    FN(ac_io_get_version, acio_get_version),
    FN(ac_io_is_active_device, acio_true),
    FN(ac_io_reset, acio_true),
    FN(ac_io_get_firmware_update_device_index, acio_get_firmware_update_device_index),
    FN(ac_io_go_firmware_update, acio_true),
    FN(ac_io_is_active2, acio_is_active2),
    FN(ac_io_begin_get_status, acio_true),
    FN(ac_io_set_get_status_device, acio_true),
    FN(ac_io_update, acio_update),
    FN(ac_io_end_get_status, acio_true),
    FN(ac_io_soft_watch_dog_on, acio_true),
    FN(ac_io_soft_watch_dog_off, acio_true),
    FN(ac_io_set_soft_watch_dog, acio_true),
    FN(ac_io_hbhi_update_control_status_buffer, hbhi_update_control_status_buffer),
    FN(ac_io_hbhi_get_control_status_buffer, hbhi_get_control_status_buffer),
    FN(ac_io_hbhi_current_coinstock, hbhi_current_coinstock),
    FN(ac_io_hbhi_consume_coinstock, hbhi_consume_coinstock),
    FN(ac_io_hbhi_lock_coincounter, acio_true),
    FN(ac_io_hbhi_unlock_coincounter, acio_true),
    FN(ac_io_hbhi_control_coin_blocker_close, acio_true),
    FN(ac_io_hbhi_control_coin_blocker_open, acio_true),
    FN(ac_io_hbhi_control_lamp_mode, hbhi_control_lamp_mode),
    FN(ac_io_hbhi_control_lamp_on, hbhi_control_lamp_on),
    FN(ac_io_hbhi_control_lamp_off, hbhi_control_lamp_off),
    FN(ac_io_hbhi_control_lamp_bright, acio_true),
    FN(ac_io_hbhi_control_parallel_on, acio_true),
    FN(ac_io_hbhi_control_parallel_off, acio_true),
    FN(ac_io_hbhi_control_reset, acio_true),
    FN(ac_io_hbhi_set_framing_err_packet_send_interval, acio_true),
    FN(ac_io_hbhi_req_volume_control, acio_true),
    FN(ac_io_hbhi_req_volume_control_isfinished, acio_status0),
    FN(ac_io_hbhi_set_watchdog_time, acio_true),
    FN(ac_io_hbhi_get_watchdog_status, acio_true),
    FN(ac_io_hbhi_get_watchdog_time_min, acio_false),
    FN(ac_io_hbhi_get_watchdog_time_now, acio_false),
    FN(ac_io_hbhi_watchdog_off, acio_true),
    FN(ac_io_hbhi_get_systemid, hbhi_get_systemid),
    FN(ac_io_hbhi_get_softwareid, hbhi_get_softwareid),
    FN(ac_io_hbhi_req_secplug_check, hbhi_req_secplug_check),
    FN(ac_io_hbhi_req_secplug_check_isfinished, hbhi_req_secplug_check_isfinished),
    FN(ac_io_hbhi_req_secplug_check_softwareplug, hbhi_req_secplug_check),
    FN(ac_io_hbhi_req_secplug_check_systemplug, hbhi_req_secplug_check_systemplug),
    FN(ac_io_hbhi_req_secplug_missing_check, acio_true),
    FN(ac_io_hbhi_req_secplug_missing_check_isfinished, hbhi_req_secplug_missing_check_isfinished),
    FN(ac_io_hbhi_req_get_control_status, acio_true),
    FN(ac_io_hbhi_add_coin, acio_true),
    FN(ac_io_hbhi_req_coin_input_wave, acio_true),
    FN(ac_io_hbhi_get_coin_input_wave_buffer, acio_false),
    // No card dispenser: its requests fail, their status never comes
    FN(ac_io_hbhi_req_carddispenser_get_status, acio_false),
    FN(ac_io_hbhi_req_carddispenser_get_status_isfinished, acio_false),
    FN(ac_io_hbhi_req_carddispenser_init, acio_false),
    FN(ac_io_hbhi_req_carddispenser_init_isfinished, acio_false),
    FN(ac_io_hbhi_req_carddispenser_disburse, acio_false),
    FN(ac_io_hbhi_req_carddispenser_disburse_isfinished, acio_false),
    FN(ac_io_hgth_set_senddata, hgth_set_senddata),
    FN(ac_io_hgth_update_recvdata, acio_true),
    FN(ac_io_hgth_get_recvdata, hgth_get_recvdata),
    FN(ac_io_hgth_directreq_set_handle_limit, hgth_set_handle_limit),
    FN(ac_io_hgth_directreq_set_handle_limit_isfinished, hgth_set_handle_limit_isfinished),
    FN(ac_io_hdxs_update_control_status_buffer, acio_true),
    FN(ac_io_hdxs_get_control_status_buffer, hdxs_get_control_status_buffer),
    FN(ac_io_hdxs_led_set_pattern, acio_true),
    FN(ac_io_hdxs_led_set_rgb_mask, acio_true),
    FN(ac_io_hdxs_led_scroll, acio_true),
    FN(ac_io_hdxs_set_framing_err_packet_send_interval, acio_true),
};

// ------------------------------------------------------------------------------
// kbt.dll and kld.dll, the cabinet's boot checks (spice2x's stubs' answers)
// ------------------------------------------------------------------------------

// Two dongle (iKey) entries of 16 bytes: bytes 4 and 6 set, the last 4 bytes all ones
static int __cdecl bt_get_ikey_status(unsigned char *status)
{
    if (!status)
        return 0;
    memset(status, 0, 32);
    for (int i = 0; i < 2; i++)
    {
        status[i * 16 + 4] = 1;
        status[i * 16 + 6] = 1;
        memset(status + i * 16 + 12, 0xFF, 4);
    }
    return 0;
}

// k_bt0002(func, arg): the game's init func(arg), after the cabinet's checks (none here)
static void __cdecl k_bt0002(void(__cdecl *func)(void *), void *arg)
{
    if (func)
        func(arg);
}

// The files' check: done, nothing wrong
static int __cdecl bt_fcheck_zero(void)
{
    return 0;
}

static const WlVirtualDllFunction g_bootCheck[] = {
    FN(bt_get_ikey_status, bt_get_ikey_status), FN(k_bt0001, bt_get_ikey_status), FN(k_bt0002, k_bt0002),
    FN(bt_fcheck_init, bt_fcheck_zero),         FN(bt_fcheck_main, bt_fcheck_zero), FN(bt_fcheck_finish, bt_fcheck_zero),
};

// The handle the game gets for kbt.dll or kld.dll when they are not there
#define BOOT_CHECK_DLL ((HMODULE)(ULONG_PTR)0x5EE00000u)

static int boot_check_name(const char *name)
{
    const char *base = strrchr(name, '\\');
    base = base ? base + 1 : name;
    return !_stricmp(base, "kbt.dll") || !_stricmp(base, "kld.dll") || !_stricmp(base, "kbt") || !_stricmp(base, "kld");
}

static HMODULE(WINAPI *oGetModuleHandleA)(LPCSTR);
static HMODULE WINAPI rf_GetModuleHandleA(LPCSTR name)
{
    HMODULE m = oGetModuleHandleA(name);
    if (!m && name && boot_check_name(name))
    {
        wl_log("road fighters 3d: GetModuleHandle \"%s\": its boot check functions answered by windowsloader", name);
        return BOOT_CHECK_DLL;
    }
    return m;
}

static HMODULE(WINAPI *oGetModuleHandleW)(LPCWSTR);
static HMODULE WINAPI rf_GetModuleHandleW(LPCWSTR name)
{
    HMODULE m = oGetModuleHandleW(name);
    char n[MAX_PATH * 2];
    if (!m && name && boot_check_name(wl_u8(name, n, sizeof n)))
    {
        wl_log("road fighters 3d: GetModuleHandle \"%s\": its boot check functions answered by windowsloader", n);
        return BOOT_CHECK_DLL;
    }
    return m;
}

static FARPROC(WINAPI *oGetProcAddress)(HMODULE, LPCSTR);
static FARPROC WINAPI rf_GetProcAddress(HMODULE mod, LPCSTR name)
{
    if (mod != BOOT_CHECK_DLL)
        return oGetProcAddress(mod, name);
    for (int i = 0; !IS_INTRESOURCE(name) && i < (int)(sizeof g_bootCheck / sizeof g_bootCheck[0]); i++)
        if (!strcmp(g_bootCheck[i].name, name))
            return (FARPROC)g_bootCheck[i].fn;
    wl_log("road fighters 3d: boot check function %s not known", IS_INTRESOURCE(name) ? "(by number)" : name);
    SetLastError(ERROR_PROC_NOT_FOUND);
    return NULL;
}

// No PC key down for the game: only the boards drive it
static SHORT WINAPI rf_GetKeyState(int key)
{
    return 0;
}

// ------------------------------------------------------------------------------
// When the host calls the game's dll_entry_init: its imports pointed at the answers above
// ------------------------------------------------------------------------------

static wchar_t g_dllName[MAX_PATH]; // the game DLL's file name ("jgt.dll")

// [Game.rf3d] VIEW = 2D. jgt.dll's view object (its labels "VIEW:2D", "VIEW:3D", "VIEW:3D
// Dynamic!", made at RVA 0x4640) keeps the stereo state in its byte +0x54 (1 3D, made so at its
// creation; the 2D/3D button toggles it). Its update (RVA 0x4770, thiscall, every frame) turns
// that into a label index (+0x48: 0 2D, 1 3D, 2 3D Dynamic) and, when the index differs from the
// last one (+0x4C, never set at the creation: 0, the 2D's index), sets the stereo renderer (its
// pointer at RVA 0x94709C, null before it exists), +0x4C written even when the renderer is not
// there yet. Each view object, as the game makes them (the attract's, the car choice's, the
// race's...), so made 2D at its creation: its byte 0 (RVA 0x465D). Its update hooked: before its
// first one (the label index still the creation's -1) and while the renderer is missing, the last
// index made -1, so that the 2D is sent. Both builds the same. (Making only the first object 2D
// later, the 20261123 essai 10, left the others in 3D and the game crashed on a second game.)
static const unsigned RF_VIEW_CREATE_STEREO = 0x465D;
static const unsigned RF_VIEW_UPDATE = 0x4770, RF_RENDERER = 0x94709C;
static unsigned char *g_game;
static void(__fastcall *oViewUpdate)(unsigned char *view, void *edx);
static void __fastcall view_update(unsigned char *view, void *edx)
{
    if (view && *(volatile int *)(view + 0x48) == -1)
        *(volatile int *)(view + 0x4C) = -1; // its first update: the 2D sent
    oViewUpdate(view, edx);
    if (view && !*(void *volatile *)(g_game + RF_RENDERER))
        *(volatile int *)(view + 0x4C) = -1; // sent again once the renderer exists
}

static void view_2d(HMODULE game)
{
    char view[8];
    wl_game_option("VIEW", view, sizeof view);
    if (_stricmp(view, "2D"))
        return;
    g_game = (unsigned char *)game;
    static const unsigned char start[6] = {0x80, 0x79, 0x55, 0x00, 0x75, 0x1C}; // cmp byte [ecx+55h], 0; jne
    static const unsigned char reads[5] = {0xA1, 0x9C, 0x70, 0x94, 0x10}; // mov eax, [renderer] (at the build's own base)
    unsigned char *update = g_game + RF_VIEW_UPDATE;
    unsigned char want[5];
    memcpy(want, reads, 5);
    *(unsigned *)(want + 1) = (unsigned)(size_t)(g_game + RF_RENDERER);
    static const unsigned char stereoWas[4] = {0xC6, 0x45, 0x54, 0x01}, stereoNow[4] = {0xC6, 0x45, 0x54, 0x00};     // mov byte [ebp+54h], 1 -> 0
    static const unsigned char labelUnset[7] = {0xC7, 0x45, 0x48, 0xFF, 0xFF, 0xFF, 0xFF}; // mov dword [ebp+48h], -1 at the creation
    if (memcmp(update, start, 6) || memcmp(update + 0x111, want, 5) || memcmp(g_game + RF_VIEW_CREATE_STEREO, stereoWas, 4) ||
        memcmp(g_game + 0x470D, labelUnset, 7))
    {
        wl_log("road fighters 3d: unexpected code for its view: it starts in 3D");
        return;
    }
    wl_patch_code(g_game + RF_VIEW_CREATE_STEREO, stereoWas, stereoNow, 4, "Road Fighters 3D's view made 2D at its creation");
    wl_hook_code(update, (void *)view_update, (void **)&oViewUpdate, "Road Fighters 3D's view update (sent once the renderer exists)");
}

static bool(__cdecl *odll_entry_init)(char *, void *);
static bool __cdecl hdll_entry_init(char *code, void *param)
{
    HMODULE game = GetModuleHandleW(g_dllName);
    char n[MAX_PATH * 2];
    wl_log("road fighters 3d: dll_entry_init(\"%s\") of %s", code ? code : "", wl_u8(g_dllName, n, sizeof n));
    if (game)
    {
        int count = (int)(sizeof g_acio / sizeof g_acio[0]), done = 0;
        for (int i = 0; i < count; i++)
            if (wl_patch_iat(game, "libacio.dll", g_acio[i].name, g_acio[i].fn, NULL))
                done++;
            else
                wl_log("road fighters 3d: %s not imported from libacio.dll", g_acio[i].name);
        wl_log("road fighters 3d: %d of the game's %d libacio.dll functions answered (HBHI board, card reader)", done, count);
        int k = wl_patch_iat(game, "kernel32.dll", "GetModuleHandleA", (void *)rf_GetModuleHandleA, (void **)&oGetModuleHandleA) +
                wl_patch_iat(game, "kernel32.dll", "GetModuleHandleW", (void *)rf_GetModuleHandleW, (void **)&oGetModuleHandleW) +
                wl_patch_iat(game, "kernel32.dll", "GetProcAddress", (void *)rf_GetProcAddress, (void **)&oGetProcAddress);
        if (k != 3)
            wl_log("road fighters 3d: %d of GetModuleHandleA, GetModuleHandleW and GetProcAddress found: kbt.dll and kld.dll not answered", k);
        if (!wl_patch_iat(game, "user32.dll", "GetKeyState", (void *)rf_GetKeyState, NULL))
            wl_log("road fighters 3d: GetKeyState not imported");
        view_2d(game);
    }
    else
        wl_log("road fighters 3d: the game's DLL is not loaded: its boards not answered");
    return odll_entry_init(code, param);
}

static WlHook g_hooks[] = {
    {NULL, "dll_entry_init", (void *)hdll_entry_init, (void **)&odll_entry_init, 0},
};

// ------------------------------------------------------------------------------
// The game
// ------------------------------------------------------------------------------

// The host's: <the game's folder>\wl_avsboot.exe, the game's DLL its first argument
int wl_game_roadfighters3d(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    if (_wcsicmp(base, L"wl_avsboot.exe"))
        return 0;
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    wchar_t dll[MAX_PATH] = L"";
    if (argv && argc > 1)
        GetFullPathNameW(argv[1], MAX_PATH, dll, NULL);
    if (argv)
        LocalFree(argv);
    DWORD crc = dll[0] ? wl_file_crc32(dll) : 0;
    int known = 0;
    for (int i = 0; i < (int)(sizeof g_builds / sizeof g_builds[0]); i++)
        known |= crc == g_builds[i];
    char u[MAX_PATH * 2];
    if (!known)
    {
        wl_log("wl_avsboot: %s (CRC-32 %08lX) is not a game windowsloader knows", dll[0] ? wl_u8(dll, u, sizeof u) : "no game DLL", crc);
        return 0;
    }
    const wchar_t *file = wcsrchr(dll, L'\\');
    wcscpy_s(g_dllName, MAX_PATH, file ? file + 1 : dll);

    *name = "rf3d";
    *title = "Road Fighters 3D";
    wl_log("game: %s (Konami PC: %s, CRC-32 %08lX%s)", *title, wl_u8(dll, u, sizeof u), crc, crc == g_builds[1] ? ", cabinet 2's build" : "");
    wl_game_set_boards("Controls on Konami ACIO boards \"HBHI\" (libacio.dll) and \"HGTH\" (libacioEx.dll: wheel, pedals)",
                       "Motor board: Konami ACIO board \"HGTH\" (libacioEx.dll, the torque the game sends)");
    wl_inputs_set_table(g_inputs, IN_COUNT);
    wl_game_use_options("rf3d");
    wl_outputs_name_lamps(WL_N("la carte HBHI, ac_io_hbhi_control_lamp_on et _off (libacio.dll)"), NULL, RF3D_LAMPS);
    wl_system_virtual_dll("libacioEx.dll", g_acioEx, (int)(sizeof g_acioEx / sizeof g_acioEx[0]));
    wl_dinput_quiet_keyboard();
    g_hooks[0].module = g_dllName;
    wl_hooks_add(g_hooks, (int)(sizeof g_hooks / sizeof g_hooks[0]));
    return 1;
}

#else
int wl_game_roadfighters3d(const wchar_t *, const char **, const char **)
{
    return 0; // the game is 32 bits
}
#endif
