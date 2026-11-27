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
// The card reader's keypad (its TEN KEY) is the PC's numeric keypad.
//
// The link (link.h): [Game.rf3d] LINK and CABINET, jgt.dll's sockets made the link's (its local
// matching: UDP and TCP between the cabinets). Cabinet 2's build (jgt_link_cab2.dll) differs from
// jgt.dll in one byte, its network options' default CABINET ID (rva 0x5FB998: 0 cabinet 1, 1
// cabinet 2, copied at its start with the rest of its defaults): linked, that byte is CABINET's,
// so either DLL serves each cabinet. The options in use (19 dwords at rva 0x95102C, the CABINET ID
// at +8, minus 1) come from dev\nvram\rfs.dat when it has them: linked, CABINET is written there
// too, over the test menu's NETWORK OPTIONS, and in the network's copy (rva 0x9470BA, taken as
// rfs.dat is read): its announcements (UDP 5836, "JGT0") carry it, and a cabinet that hears
// another one's with its own id (its own announcement told by its address) stops on ERROR 5-1551
// "CABINET ID IS CONFLICTED".

#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <intrin.h>

#include "acio.h"
#include "ffb.h"
#include "filecrc.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "lang.h"
#include "link.h"
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

static volatile LONG g_packets;
static volatile LONG64 g_clock; // the packet's time, which the game watches (unchanged 20 s: an I/O error)

// GearUp and GearDown move a sequential gear through the H pattern (0 neutral, 1..6); the lever
// moved by hand (Gear1..Gear6, the four switches) puts it back to neutral
static int g_gear, g_gearUpHeld, g_gearDownHeld;

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
    wl_acio_coin_mech(coin);
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

// The security plugs (the game's software plug, the cabinet's system plug): found at once, 0x12;
// the missing plug check passed, 8
#define PLUG_FOUND 0x12

static int __cdecl hbhi_req_secplug_check(const char *id)
{
    static volatile LONG logged;
    if (id && !InterlockedExchange(&logged, 1))
        wl_log("road fighters 3d: software plug %.16s present", id);
    return 1;
}

static int __cdecl hbhi_req_secplug_check_isfinished(DWORD *status)
{
    if (status)
        *status = PLUG_FOUND;
    return 1;
}

static int __cdecl hbhi_get_softwareid(char *out)
{
    // windowsarcadeloader's answer, with which the game boots (the plug's name in jgt.dll is
    // GQJGTJAA)
    static const unsigned char id[8] = {'G', 'L', 'H', '2', '4', 'J', 'A', 'A'};
    return wl_acio_plug_id(out, id);
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

// The volume's request, done at once (the other requests: acio.h)
static int __cdecl acio_status0(DWORD *status)
{
    if (status)
        *status = 0;
    return 1;
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

// The reader's keypad (its TEN KEY) from the PC's numeric keypad. Its (edge, level) words,
// spice2x's encoding (windowsarcadeloader's icca.rs): the level the key down's code; the edge,
// kept after the release with its code cleared, the last press: code << 16, 0x80, a 3-bit serial
// << 4, the key's number. spice2x's keys (index: 0-9, 10 the decimal point, 11 the 00 key) are a
// calculator's (7 8 9 on top, 0 00 . at the bottom); this game's TEN KEY is a telephone's (1 2 3
// on top, * 0 # at the bottom) and takes them by their place (its INPUT CHECK shows them). Its
// keys from the PC's: 1-9, 0, * (* or .), # (+ or Enter), each the spice2x key at its place
static void keypad_words(DWORD *edge, DWORD *level)
{
    static const DWORD codes[12] = {0x100, 0x200, 0x2000, 2, 0x400, 0x4000, 4, 0x800, 0x8000, 8, 1, 0x1000};
    static const DWORD nums[12] = {0, 1, 5, 9, 2, 6, 10, 3, 7, 11, 8, 4};
    static const struct
    {
        int vk, vk2, index; // the PC's keys, the spice2x key at the TEN KEY's place
    } keys[12] = {{VK_NUMPAD1, 0, 7}, {VK_NUMPAD2, 0, 8}, {VK_NUMPAD3, 0, 9}, {VK_NUMPAD4, 0, 4}, {VK_NUMPAD5, 0, 5},
                  {VK_NUMPAD6, 0, 6}, {VK_NUMPAD7, 0, 1}, {VK_NUMPAD8, 0, 2}, {VK_NUMPAD9, 0, 3}, {VK_MULTIPLY, VK_DECIMAL, 0},
                  {VK_NUMPAD0, 0, 11}, {VK_ADD, VK_RETURN, 10}};
    static DWORD lastEdge, serial;
    static int was = -1;
    int key = -1;
    for (int i = 0; i < 12 && key < 0; i++)
        if ((GetAsyncKeyState(keys[i].vk) & 0x8000) || (keys[i].vk2 && (GetAsyncKeyState(keys[i].vk2) & 0x8000)))
            key = keys[i].index;
    if (key >= 0)
    {
        if (key != was)
        {
            lastEdge = (codes[key] << 16) | 0x80 | ((serial++ & 7) << 4) | nums[key];
            wl_log("road fighters 3d: keypad %d", key);
        }
        *level = codes[key];
    }
    else
    {
        lastEdge &= 0xFFFF;
        *level = 0;
    }
    was = key;
    *edge = lastEdge;
}

// (out, device); some games pass (device, out): the pointer is the argument that is not a small
// device number. Byte 0 the state, 2 and 3 the front and rear sensors, 4..11 the card's id, 16..23
// the keypad's edge and level
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
    DWORD edge, level;
    keypad_words(&edge, &level);
    memcpy(status + 16, &edge, 4);
    memcpy(status + 20, &level, 4);
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
    FN(ac_io_begin, wl_acio_begin),
    FN(ac_io_begin_get_status, wl_acio_true),
    FN(ac_io_end, wl_acio_true),
    FN(ac_io_end_get_status, wl_acio_true),
    FN(ac_io_update, wl_acio_update),
    FN(ac_io_is_active2, wl_acio_is_active2),
    FN(ac_io_is_active_device, wl_acio_true),
    FN(ac_io_get_version, wl_acio_get_version),
    FN(ac_io_get_rs232c_status, wl_acio_get_rs232c_status),
    FN(ac_io_soft_watch_dog_on, wl_acio_true),
    FN(ac_io_soft_watch_dog_off, wl_acio_true),
    FN(ac_io_hbhi_update_control_status_buffer, hbhi_update_control_status_buffer),
    FN(ac_io_hbhi_get_control_status_buffer, hbhi_get_control_status_buffer),
    FN(ac_io_hbhi_current_coinstock, wl_acio_current_coinstock),
    FN(ac_io_hbhi_consume_coinstock, wl_acio_consume_coinstock),
    FN(ac_io_hbhi_lock_coincounter, wl_acio_true),
    FN(ac_io_hbhi_unlock_coincounter, wl_acio_true),
    FN(ac_io_hbhi_control_coin_blocker_open, wl_acio_true),
    FN(ac_io_hbhi_control_coin_blocker_close, wl_acio_true),
    FN(ac_io_hbhi_control_lamp_mode, hbhi_control_lamp_mode),
    FN(ac_io_hbhi_control_lamp_on, hbhi_control_lamp_on),
    FN(ac_io_hbhi_control_lamp_off, hbhi_control_lamp_off),
    FN(ac_io_hbhi_control_lamp_bright, wl_acio_true),
    FN(ac_io_hbhi_get_softwareid, hbhi_get_softwareid),
    FN(ac_io_hbhi_get_systemid, wl_acio_get_systemid),
    FN(ac_io_hbhi_get_watchdog_status, wl_acio_true),
    FN(ac_io_hbhi_req_secplug_check_isfinished, hbhi_req_secplug_check_isfinished),
    FN(ac_io_icca_cardunit_init, wl_acio_true),
    FN(ac_io_icca_cardunit_init_isfinished, wl_acio_icca_cardunit_init_isfinished),
    FN(ac_io_icca_req_uid, wl_acio_true),
    FN(ac_io_icca_req_uid_isfinished, icca_req_uid_isfinished),
    FN(ac_io_icca_get_uid_felica, icca_get_uid_felica),
    FN(ac_io_icca_workflow, icca_workflow),
    FN(ac_io_icca_get_status, icca_get_status),
};

// libacioEx.dll's, the names jgt.dll looks up there (in this order, from rva 0x61E244, after the
// DLL's name)
static const WlVirtualDllFunction g_acioEx[] = {
    FN(ac_io_begin, wl_acio_begin),
    FN(ac_io_end, wl_acio_true),
    FN(ac_io_get_rs232c_status, wl_acio_get_rs232c_status),
    FN(ac_io_get_send_log, wl_acio_get_log),
    FN(ac_io_get_recv_log, wl_acio_get_log),
    FN(ac_io_get_version, wl_acio_get_version),
    FN(ac_io_is_active_device, wl_acio_true),
    FN(ac_io_reset, wl_acio_true),
    FN(ac_io_get_firmware_update_device_index, wl_acio_get_firmware_update_device_index),
    FN(ac_io_go_firmware_update, wl_acio_true),
    FN(ac_io_is_active2, wl_acio_is_active2),
    FN(ac_io_begin_get_status, wl_acio_true),
    FN(ac_io_set_get_status_device, wl_acio_true),
    FN(ac_io_update, wl_acio_update),
    FN(ac_io_end_get_status, wl_acio_true),
    FN(ac_io_soft_watch_dog_on, wl_acio_true),
    FN(ac_io_soft_watch_dog_off, wl_acio_true),
    FN(ac_io_set_soft_watch_dog, wl_acio_true),
    FN(ac_io_hbhi_update_control_status_buffer, hbhi_update_control_status_buffer),
    FN(ac_io_hbhi_get_control_status_buffer, hbhi_get_control_status_buffer),
    FN(ac_io_hbhi_current_coinstock, wl_acio_current_coinstock),
    FN(ac_io_hbhi_consume_coinstock, wl_acio_consume_coinstock),
    FN(ac_io_hbhi_lock_coincounter, wl_acio_true),
    FN(ac_io_hbhi_unlock_coincounter, wl_acio_true),
    FN(ac_io_hbhi_control_coin_blocker_close, wl_acio_true),
    FN(ac_io_hbhi_control_coin_blocker_open, wl_acio_true),
    FN(ac_io_hbhi_control_lamp_mode, hbhi_control_lamp_mode),
    FN(ac_io_hbhi_control_lamp_on, hbhi_control_lamp_on),
    FN(ac_io_hbhi_control_lamp_off, hbhi_control_lamp_off),
    FN(ac_io_hbhi_control_lamp_bright, wl_acio_true),
    FN(ac_io_hbhi_control_parallel_on, wl_acio_true),
    FN(ac_io_hbhi_control_parallel_off, wl_acio_true),
    FN(ac_io_hbhi_control_reset, wl_acio_true),
    FN(ac_io_hbhi_set_framing_err_packet_send_interval, wl_acio_true),
    FN(ac_io_hbhi_req_volume_control, wl_acio_true),
    FN(ac_io_hbhi_req_volume_control_isfinished, acio_status0),
    FN(ac_io_hbhi_set_watchdog_time, wl_acio_true),
    FN(ac_io_hbhi_get_watchdog_status, wl_acio_true),
    FN(ac_io_hbhi_get_watchdog_time_min, wl_acio_false),
    FN(ac_io_hbhi_get_watchdog_time_now, wl_acio_false),
    FN(ac_io_hbhi_watchdog_off, wl_acio_true),
    FN(ac_io_hbhi_get_systemid, wl_acio_get_systemid),
    FN(ac_io_hbhi_get_softwareid, hbhi_get_softwareid),
    FN(ac_io_hbhi_req_secplug_check, hbhi_req_secplug_check),
    FN(ac_io_hbhi_req_secplug_check_isfinished, hbhi_req_secplug_check_isfinished),
    FN(ac_io_hbhi_req_secplug_check_softwareplug, hbhi_req_secplug_check),
    FN(ac_io_hbhi_req_secplug_check_systemplug, wl_acio_true),
    FN(ac_io_hbhi_req_secplug_missing_check, wl_acio_true),
    FN(ac_io_hbhi_req_secplug_missing_check_isfinished, wl_acio_secplug_missing_check_isfinished),
    FN(ac_io_hbhi_req_get_control_status, wl_acio_true),
    FN(ac_io_hbhi_add_coin, wl_acio_true),
    FN(ac_io_hbhi_req_coin_input_wave, wl_acio_true),
    FN(ac_io_hbhi_get_coin_input_wave_buffer, wl_acio_false),
    // No card dispenser: its requests fail, their status never comes
    FN(ac_io_hbhi_req_carddispenser_get_status, wl_acio_false),
    FN(ac_io_hbhi_req_carddispenser_get_status_isfinished, wl_acio_false),
    FN(ac_io_hbhi_req_carddispenser_init, wl_acio_false),
    FN(ac_io_hbhi_req_carddispenser_init_isfinished, wl_acio_false),
    FN(ac_io_hbhi_req_carddispenser_disburse, wl_acio_false),
    FN(ac_io_hbhi_req_carddispenser_disburse_isfinished, wl_acio_false),
    FN(ac_io_hgth_set_senddata, hgth_set_senddata),
    FN(ac_io_hgth_update_recvdata, wl_acio_true),
    FN(ac_io_hgth_get_recvdata, hgth_get_recvdata),
    FN(ac_io_hgth_directreq_set_handle_limit, hgth_set_handle_limit),
    FN(ac_io_hgth_directreq_set_handle_limit_isfinished, hgth_set_handle_limit_isfinished),
    FN(ac_io_hdxs_update_control_status_buffer, wl_acio_true),
    FN(ac_io_hdxs_get_control_status_buffer, hdxs_get_control_status_buffer),
    FN(ac_io_hdxs_led_set_pattern, wl_acio_true),
    FN(ac_io_hdxs_led_set_rgb_mask, wl_acio_true),
    FN(ac_io_hdxs_led_scroll, wl_acio_true),
    FN(ac_io_hdxs_set_framing_err_packet_send_interval, wl_acio_true),
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
// index made -1, so that the 2D is sent. Both builds the same. (Making only the first object 2D,
// later, left the others in 3D and the game crashed on a second game.)
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
    // The game's view object (its pointer at RVA 0x946DA0, the one the race's view copies, RVA
    // 0x1391CF) set to 3D again after its creation, in two places (ebx is 0 in both): 0 there
    static const unsigned char setWas[4] = {0xC6, 0x40, 0x54, 0x01}, setNow[4] = {0xC6, 0x40, 0x54, 0x00}; // mov byte [eax+54h], 1 -> 0
    static const unsigned char alWas[3] = {0x88, 0x41, 0x54}, blNow[3] = {0x88, 0x59, 0x54};                 // mov [ecx+54h], al -> bl
    if (!wl_patch_code(g_game + 0x2412C, setWas, setNow, 4, "Road Fighters 3D's view kept 2D (its game's start)") ||
        !wl_patch_code(g_game + 0x3E6DB, alWas, blNow, 3, "Road Fighters 3D's view kept 2D (its reset)"))
        wl_log("road fighters 3d: unexpected code where its view is set to 3D: it may come back in 3D");
    wl_hook_code(update, (void *)view_update, (void **)&oViewUpdate, "Road Fighters 3D's view update (sent once the renderer exists)");
}

// The CABINET ID in use, kept at CABINET's (the options read from rfs.dat after the game's start)
static DWORD WINAPI cabinet_keeper(void *p)
{
    // the options in use, the saved ones (rva 0x9504FC, the test menu's) and the network's own
    // copy (rva 0x9470BA, taken from the options in use as soon as rfs.dat is read: the id its
    // announcements carry, each cabinet with the other's id a conflict, 5-1551)
    static const char *const what[3] = {"in use", "saved", "the network's"};
    volatile unsigned char *ids[3] = {(unsigned char *)p + 0x951034, (unsigned char *)p + 0x950504, (unsigned char *)p + 0x9470BA};
    unsigned char want = (unsigned char)(wl_link_cabinet() - 1);
    for (int said = 0;; Sleep(20))
        for (int i = 0; i < 3; i++)
            if (*ids[i] != want)
            {
                if (said++ < 9)
                    wl_log("road fighters 3d: its CABINET ID (%s) was %d: %d (CABINET)", what[i], *ids[i] + 1, want + 1);
                *ids[i] = want;
            }
}

// The game watches its own frame rate (+0x49370, every 16 frames in a race): with more than 40
// readings and more than one in ten under 53 frames a second, it asks for its device to be made
// again when the scene ends (+0x4935e: [+0x970347] = 1; +0xfa2c9, then the framework's Reset,
// +0x183480): its effects and declarations released, Reset, then made again. The cabinets run it at
// 47-49 frames a second: at each end of a game, and at the second one its effects could no longer
// be made (D3DXCreateEffect: E_OUTOFMEMORY), then the crash (+0x1d3804). That request is not made;
// a device really lost is still reset (TestCooperativeLevel, +0x183fa0). Checked here with its
// 53 made 1000 (every reading slow): the Reset at the end of a game without this, none with it
static void perf_reset_off(HMODULE game)
{
    unsigned char *at = (unsigned char *)game + 0x4935e;
    static const unsigned char was[7] = {0xC6, 0x05, 0x47, 0x03, 0x97, 0x10, 0x01}; // mov byte [+0x970347], 1
    unsigned char now[7];
    memcpy(now, was, 7);
    now[6] = 0;
    // its absolute address is relocated with the DLL: checked and written at its place
    unsigned va = (unsigned)(size_t)game + 0x970347;
    memcpy((unsigned char *)now + 2, &va, 4);
    unsigned char expect[7];
    memcpy(expect, was, 7);
    memcpy(expect + 2, &va, 4);
    if (!wl_patch_code(at, expect, now, 7, "Road Fighters 3D's device made again when slow (not asked)"))
        wl_log("road fighters 3d: unexpected code at +0x4935e: its slow frame rate may still reset its device");
}

// The game's frame (+0x191f90): its Present (+0x1920f2), its frame timer started again
// (+0x192113, QueryPerformanceCounter), the scene drawn, then Sleep for 16.64 ms less the time
// since and 3 ms (+0x1920c8), and a wait until 16.667 ms have passed or the screen is in its
// vertical blank (+0x192510), then the next Present. Present's own time is outside that timer:
// a frame lasts 16.67 ms plus Present's, 4 ms on a cabinet's PC (48 fps; 7 ms on a laptop, 42 fps;
// [Display] FRAME_STATS). The timer starts as the Present began instead: 60 fps
// while the frame's work and its Present fit in 16.67 ms
static HMODULE g_jgt;
static BOOL(WINAPI *oQueryPerformanceCounter)(LARGE_INTEGER *);

static BOOL WINAPI rf_QueryPerformanceCounter(LARGE_INTEGER *t)
{
    BOOL ok = oQueryPerformanceCounter(t);
    if (ok && _ReturnAddress() == (unsigned char *)g_jgt + 0x192115)
    {
        LONGLONG began = wl_d3d9_present_began();
        if (began && began <= t->QuadPart)
            t->QuadPart = began;
    }
    return ok;
}

static void frame_pace(HMODULE game)
{
    static const unsigned char code[19] = {0x8D, 0x4C, 0x24, 0x40, 0x51, 0x89, 0x5C, 0x24, 0x54, 0x89,
                                           0x5C, 0x24, 0x58, 0x88, 0x5C, 0x24, 0x5C, 0xFF, 0xD6}; // its timer's QueryPerformanceCounter
    if (memcmp((unsigned char *)game + 0x192102, code, sizeof code))
    {
        wl_log("road fighters 3d: unexpected code at +0x192102: its frames keep Present's time beyond 16.67 ms");
        return;
    }
    g_jgt = game;
    if (wl_patch_iat(game, "kernel32.dll", "QueryPerformanceCounter", (void *)rf_QueryPerformanceCounter, (void **)&oQueryPerformanceCounter) &&
        oQueryPerformanceCounter)
        wl_log("road fighters 3d: its frame timer starts as its Present begins (60 fps, not 16.67 ms plus Present's time)");
    else
        wl_log("road fighters 3d: QueryPerformanceCounter not imported: its frames keep Present's time beyond 16.67 ms");
}

// dev/nvram/rfs.dat, the game's saved settings: 0xB3C bytes, at +4 the CRC-32 of +0x10..0xB3C, then
// blocks of 0x104 bytes from +0x114, each its CRC-32 first. Block 1 (+0x118..0x218) is the I/O's
// calibration: the wheel's and the pedals' (16 bits at +0x11C, +0x120, +0x128, +0x134) and three
// "done" bytes (+0x13C..0x13E). A copy made by another loader can have every block's CRC at 0 and
// that block not done: ERROR 5-1550 "do the I/O's calibration" at the start of a new write
// folder. Such a block is read as a calibrated one, calibrated for the boards windowsloader
// answers for; the file itself is left as it is
static void rfs_read(unsigned char *data, DWORD size, unsigned long long offset)
{
    if (offset != 0 || size < 0xB3C || *(DWORD *)data != 0xB3C)
        return;
    unsigned char *calib = data + 0x118;
    if (*(DWORD *)(data + 0x114) == wl_crc32(calib, 0x100) && calib[0x24] && calib[0x25] && calib[0x26])
        return;
    memset(calib, 0, 0x100);
    *(WORD *)(data + 0x11C) = 0xFFFF;
    *(WORD *)(data + 0x120) = 0x7FFF;
    *(WORD *)(data + 0x128) = 0xFFFF;
    *(WORD *)(data + 0x134) = 0xFFFF;
    data[0x13C] = data[0x13D] = data[0x13E] = 1;
    *(DWORD *)(data + 0x114) = wl_crc32(calib, 0x100);
    *(DWORD *)(data + 4) = wl_crc32(data + 0x10, 0xB3C - 0x10);
    wl_log("road fighters 3d: rfs.dat's I/O calibration missing (ERROR 5-1550): read calibrated");
}

// Linked: the game's sockets the link's, its default CABINET ID and the one in use this cabinet's
static void link_game(HMODULE game)
{
    if (!wl_link_count())
        return;
    wl_link_module(game);
    unsigned char *id = (unsigned char *)game + 0x5FB998;
    if (id[-8] != 1 || id[0] > 1)
    {
        wl_log("road fighters 3d: unexpected network options' defaults at rva 0x5FB990: the default CABINET ID left as it is");
        return;
    }
    unsigned char now = (unsigned char)(wl_link_cabinet() - 1);
    if (id[0] != now)
        wl_patch_code(id, id, &now, 1, "Road Fighters 3D's default CABINET ID (CABINET)");
    CloseHandle(CreateThread(NULL, 0, cabinet_keeper, game, 0, NULL));
    wl_log("road fighters 3d: link: cabinet %d of %d (CABINET ID, over the test menu's NETWORK OPTIONS)", wl_link_cabinet(), wl_link_count());
}

static void rf_settings(void)
{
    wl_link_settings(GetModuleHandleW(NULL));
}

// The game's effects (D3DXCreateEffect, d3dx9_41.dll): made again after each Reset of its device
// (at the end of a game); one that fails is used anyway (jgt.dll+0x1d3804 read 0). A failure is
// logged with its compiler's messages and tried again, a short wait between two attempts (none
// after the last): the caller is given the last attempt's messages only, the others released
typedef HRESULT(WINAPI *CreateEffectFn)(void *dev, const void *src, UINT len, const void *defines, void *include, DWORD flags, void *pool,
                                        void **effect, void **errors);
static CreateEffectFn oCreateEffect;
#define RF_EFFECT_ATTEMPTS 4

static void rf_release(void *unknown) // IUnknown::Release
{
    if (unknown)
        ((ULONG(STDMETHODCALLTYPE *)(void *))(*(void ***)unknown)[2])(unknown);
}

static HRESULT WINAPI rf_CreateEffect(void *dev, const void *src, UINT len, const void *defines, void *include, DWORD flags, void *pool,
                                      void **effect, void **errors)
{
    HRESULT hr = E_FAIL;
    for (int attempt = 0; attempt < RF_EFFECT_ATTEMPTS; attempt++)
    {
        void *local = NULL;
        void **errs = errors ? errors : &local;
        *errs = NULL; // an out parameter: never one of an earlier attempt's, nor what the caller left there
        hr = oCreateEffect(dev, src, len, defines, include, flags, pool, effect, errs);
        if (SUCCEEDED(hr))
        {
            if (attempt)
                wl_log("road fighters 3d: D3DXCreateEffect made at attempt %d", attempt + 1);
            rf_release(local);
            return hr;
        }
        static int logged;
        if (logged++ < 20)
        {
            const char *text = NULL;
            if (*errs)
                text = ((const char *(STDMETHODCALLTYPE *)(void *))(*(void ***)*errs)[3])(*errs); // ID3DXBuffer::GetBufferPointer
            char where[128];
            wl_log("road fighters 3d: D3DXCreateEffect (%u bytes) failed, 0x%08lx%s%.300s [%s]", len, hr, text ? ": " : "", text ? text : "",
                   wl_where(WL_CALLER, where, sizeof where));
        }
        rf_release(local);
        if (attempt + 1 < RF_EFFECT_ATTEMPTS)
        {
            if (errors)
            {
                rf_release(*errors); // replaced by the next attempt's
                *errors = NULL;
            }
            Sleep(50);
        }
    }
    return hr;
}

// e-Amusement's boot check (ea3_get_boot_status, libavs-win32-ea3.dll: 0 while it runs, else done,
// its code in *code): without Konami's servers it ends on EA3_BOOT_CODE_SERVICES_ERROR (2002), which
// the game shows at each start as "ERROR MODE 5-2002-0000" (WARNING, "CANNOT CONNECT ROUTER."),
// waiting for SERVICE: a cabinet without an attendant stood there. That code is read as OK: the
// boot check passes, e-Amusement stays offline (ea3_get_status not online), the game goes on to its
// attract in its local mode, as SERVICE made it. Any other code is left as it is
#define EA3_BOOT_SERVICES_ERROR 2002
static int(__cdecl *oea3_get_boot_status)(int *, int *);
static int __cdecl rf_ea3_get_boot_status(int *code, int *sub)
{
    int done = oea3_get_boot_status(code, sub);
    if (done && code && *code == EA3_BOOT_SERVICES_ERROR)
    {
        static volatile LONG logged;
        if (!InterlockedExchange(&logged, 1))
            wl_log("road fighters 3d: e-Amusement's boot check ended on 5-%04d-%04d (no services: CANNOT CONNECT ROUTER), read as OK: "
                   "the game starts offline without waiting for SERVICE",
                   *code, sub ? *sub : 0);
        *code = 0;
        if (sub)
            *sub = 0;
    }
    return done;
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
        if (!wl_patch_iat(game, "d3dx9_41.dll", "D3DXCreateEffect", (void *)rf_CreateEffect, (void **)&oCreateEffect))
            wl_log("road fighters 3d: D3DXCreateEffect not imported");
        if (!wl_patch_iat(game, "libavs-win32-ea3.dll", "ea3_get_boot_status", (void *)rf_ea3_get_boot_status, (void **)&oea3_get_boot_status))
            wl_log("road fighters 3d: ea3_get_boot_status not imported: the network warning waits for SERVICE");
        view_2d(game);
        link_game(game);
        perf_reset_off(game);
        frame_pace(game);
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
    wl_acio_name("road fighters 3d");
    wl_log("game: %s (Konami PC: %s, CRC-32 %08lX%s)", *title, wl_u8(dll, u, sizeof u), crc, crc == g_builds[1] ? ", cabinet 2's build" : "");
    wl_game_set_boards("Controls on Konami ACIO boards \"HBHI\" (libacio.dll) and \"HGTH\" (libacioEx.dll: wheel, pedals)",
                       "Motor board: Konami ACIO board \"HGTH\" (libacioEx.dll, the torque the game sends)");
    wl_inputs_set_table(g_inputs, IN_COUNT);
    wl_game_use_options("rf3d");
    // its Reset at the end of a game (the device's own settings), after which its effects failed
    // (D3DXCreateEffect: E_OUTOFMEMORY, the cabinets) and it crashed (+0x1d3804): not made
    wl_d3d9_keep_same_reset();
    wl_files_read_filter("rfs.dat", rfs_read);
    // Windows' timer at 1 ms: the game sleeps about 7 ms a frame once its frame timer starts with its
    // Present (frame_pace); the default 15.6 ms timer would lengthen that Sleep past the frame
    if (timeBeginPeriod(1) == TIMERR_NOERROR)
        wl_log("road fighters 3d: Windows' timer at 1 ms (timeBeginPeriod)");
    wl_game_on_settings(rf_settings);
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
