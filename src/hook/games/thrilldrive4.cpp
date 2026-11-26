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

// Konami PC: Thrill Drive 4: Crazy Streets (2007, game code F04, the export build
// "F04:E:A:A:20070330"), td4\td4.exe (x86, Direct3D 9, 640x480, fixed base 0x400000; td4_go.exe is
// a patched copy of it, not played). Reimplemented from windowsarcadeloader's findings (aderumier,
// GPL v3, https://github.com/aderumier/windowsarcadeloader, commit 2bfe069: systemprofiles/konami/
// thrill-drive-4.yaml, crates/payload-konami/src: td4.rs, pjei.rs, acio.rs, icca.rs; its
// docs/IMPLEMENTATION.md, "Thrill Drive 4: Crazy Streets"), where a payload takes libacio.dll's
// place. Written first without a copy of the game, then checked on one (td4.exe CRC-32 D363889F,
// time stamp 460B97C6, a TeknoParrot dump with the files at its root): its addresses are
// windowsarcadeloader's, each code change made only where the bytes are the ones it found
// (wl_patch_code), the game's data read only when all of them are. Its lamps: masks 0x01, 0x02, 0x08
// and 0x10, set on and off by ac_io_pjei_control_lamp_on and _off (blinking together in the attract).
//
// Here the cabinet's libraries stay, and the game's calls are answered:
// - libacio.dll, the first ACIO bus (imported by td4.exe): the PJEI I/O board (switches, coin mech,
//   the wheel's and the pedals' potentiometers, the security plug GKF04EAA, lamps) and the ICCA
//   card reader (no card). td4.exe's imports of it pointed at our functions; one we do not know is
//   logged and left to the copy's libacio.dll.
// - libtd4io.dll, the second bus (LoadLibraryA, then GetProcAddress of 27 functions, every one
//   needed): the D44H board, the wheel's motor, and the D44S board, the seat (absent: the boot shows
//   SEATBELT OK ( NO DEVICE )). The DLL is played by windowsloader (wl_system_virtual_dll).
//
// The PJEI status (40 bytes): byte 4 START 0x04 (also the nitro in a race), the shift lever's UP
// 0x20 and DOWN 0x40, VIEW 0x80; byte 16 test 0x20, service 0x10, coin mech 0x04; +8, +10, +12 the
// wheel, the accelerator and the brake (16 bits). The game maps them with its calibration (u32
// arrays: min at 0x7E3974, max at 0x7E39B4, the wheel's centre at 0x7E39A8): the controls are put
// onto the calibration it has. The wheel's potentiometer reads high on the left (the default
// calibration: min 0xFF40 the left, centre 0x7A40, max 0 the right); the test menu's CALIBRATION
// has a neutral one (min 0, centre 0x8000, max 0xFFFF), on which following its min and max showed
// the wheel the wrong way round: the left is always the calibration's higher end. Test menu: LEVER
// UP and DOWN select, START executes; INPUT CHECK ends with LEVER UP + START.
//
// The D44H motor (ac_io_d44h_set_senddata, 4 bytes, GTI Club's HGTH layout): byte 0 the torque
// -127..127 (in a race about +-80 against the wheel's turn: the wheel to the right, the torque
// negative, pushing to the left), byte 1 bits 0-5 a vibration level 0..63 (crashes, decaying), byte
// 3 0..254 from the speed.
//
// Code changes (all at the export build's addresses):
// - the wheel's kind (+0x48460: td4.ini's [IO] HANDLE, KONAMI, HAPP or KEYBOAD, else the test menu's
//   setting, else the cabinet's default, HAPP in this build, a Happ wheel without motor): always
//   KONAMI, the D44H board;
// - the boot's DATA CHECK (+0xD8A47: every file of data\td4check.bin summed, libacio.dll among
//   them, tens of minutes for the 1 GB td4pack.dat): skipped, as HOLD LEVER UP + START does;
// - the operator's clock (getter +0x108640) and the test menu's steering, accelerator and brake
//   calibrations (getter +0x108610), unset on a fresh CONF\NVRAM (PLEASE SET THE CLOCK, PLEASE
//   CALIBRATION BEFORE OPERATION): reported set (the game reads the PC's clock; the controls follow
//   the calibration it has).
// The boot's motorized CALIBRATION (+0x49B30, its step at 0x7E3B0C 1..5 until 0x7E3B10 is set) turns
// the wheel to each end stop and reads the potentiometer there (at least 50000 apart, ERROR
// otherwise): meanwhile the wheel read follows the motor (a negative torque to the left end), and no
// force is sent to the PC's wheel. About 45 s, as on the cabinet.
// td4.ini's [IO] HANDLE (KEYBOAD in windowsarcadeloader's copy, a development mode reading the PC's
// keyboard) is read as KONAMI; the PC's keyboard is kept from the game's DirectInput, and
// GetLogicalDriveStringsA gives C: alone (the boot reads every drive's volume and free space).

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "lang.h"
#include "log.h"
#include "outputs.h"

#if defined(_M_IX86)

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
    IN_START,
    IN_SHIFT_UP,
    IN_SHIFT_DOWN,
    IN_VIEW,
    IN_COUNT
};

// The axes at full range here (0 the wheel's left end); put onto the game's calibration below
static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 65535, 32767},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 65535, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 65535, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "P1_Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftUp", "GearUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftDown", "GearDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"View", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
};

static int held(int i)
{
    return wl_input_get(&g_inputs[i]) != 0;
}

// ------------------------------------------------------------------------------
// The export build's code and data (windowsarcadeloader's td4.rs)
// ------------------------------------------------------------------------------

#define TD4_BASE 0x400000u

// 1: td4.exe at its fixed base with each code change's bytes as found: its data read too
static int g_known;

#define TD4_CAL_MIN 0x7E3974u
#define TD4_CAL_MAX 0x7E39B4u
#define TD4_CAL_CENTER 0x7E39A8u
#define TD4_CAL_STEP 0x7E3B0Cu
#define TD4_CAL_DONE 0x7E3B10u

static const struct
{
    unsigned address;
    unsigned char length;
    unsigned char was[9], now[9];
    const char *what;
} g_patches[] = {
    // mov al, [0x7F2948]; test al, al; je ... -> mov byte [0x7E3A12], 0 (KONAMI); ret
    {0x448460, 9, {0xA0, 0x48, 0x29, 0x7F, 0x00, 0x84, 0xC0, 0x74, 0x0F}, {0xC6, 0x05, 0x12, 0x3A, 0x7E, 0x00, 0x00, 0xC3, 0x90},
     "Thrill Drive 4's wheel: Konami's, with its motor (D44H board)"},
    // call ...; test al, al -> jmp +0x35 (past the DATA CHECK)
    {0x4D8A47, 7, {0xE8, 0x74, 0xE3, 0xF6, 0xFF, 0x84, 0xC0}, {0xEB, 0x35, 0x90, 0x90, 0x90, 0x90, 0x90}, "Thrill Drive 4's boot DATA CHECK skipped"},
    // mov al, [0x98F59C]; test al, al -> mov al, 1; ret
    {0x508610, 7, {0xA0, 0x9C, 0xF5, 0x98, 0x00, 0x84, 0xC0}, {0xB0, 0x01, 0xC3, 0x90, 0x90, 0x90, 0x90},
     "Thrill Drive 4's calibrations reported done"},
    // mov al, [0x98F64C]; ret -> mov al, 1; ret
    {0x508640, 6, {0xA0, 0x4C, 0xF6, 0x98, 0x00, 0xC3}, {0xB0, 0x01, 0xC3, 0x90, 0x90, 0xC3}, "Thrill Drive 4's clock reported set"},
};

static unsigned game_u32(unsigned address)
{
    return *(volatile unsigned *)(ULONG_PTR)address;
}

// The boot's motorized calibration running
static int calibrating(void)
{
    if (!g_known)
        return 0;
    unsigned step = game_u32(TD4_CAL_STEP);
    return step >= 1 && step <= 5 && !*(volatile unsigned char *)(ULONG_PTR)TD4_CAL_DONE;
}

// ------------------------------------------------------------------------------
// The D44H board: the wheel's motor
// ------------------------------------------------------------------------------

static volatile LONG g_lastSend = -1; // the motor's last 4 bytes

static int __cdecl d44h_set_senddata(const unsigned char *data)
{
    if (!data)
        return 1;
    LONG packed;
    memcpy(&packed, data, 4);
    int cal = calibrating();
    if (InterlockedExchange(&g_lastSend, packed) == packed)
        return 1;
    static int logged;
    if (!cal && logged < 60)
    {
        logged++;
        wl_log("thrill drive 4: motor %02x %02x %02x %02x (wheel %d)", data[0], data[1], data[2], data[3], wl_input_get(&g_inputs[IN_STEERING]));
    }
    static int wasCalibrating;
    if (cal)
    {
        // the wheel read follows the motor to its end stops: none of that to the PC's wheel
        if (!wasCalibrating)
        {
            wl_log("thrill drive 4: the boot's wheel calibration (the wheel read follows its motor, no force sent)");
            wl_ffb_set(0);
            wl_ffb_set_vibration(0, 0);
        }
        wasCalibrating = 1;
        return 1;
    }
    wasCalibrating = 0;
    // a negative torque pushes to the left, wl_ffb_set's > 0
    double torque = (signed char)data[0] / 127.0;
    wl_ffb_set(-torque);
    wl_ffb_set_vibration((data[1] & 0x3F) / 63.0, 50);
    return 1;
}

// The board's receive buffer: nothing the game uses (the wheel's position is the PJEI's)
static int __cdecl d44h_get_recvdata(unsigned char *out)
{
    if (out)
        memset(out, 0, 16);
    return 1;
}

// A direct request (its buffer, every frame with the bus up): nothing to do
static int __cdecl d44h_directreq_sendandrecv(unsigned char *buffer)
{
    return 1;
}

// The wheel's travel limit (the motor's end stops), set at once: SET HANDLE LIMIT SUCCESS
static int __cdecl d44h_set_handle_limit(int limit, DWORD *status)
{
    if (status)
        *status = 1;
    return 1;
}

static int __cdecl d44h_set_handle_limit_isfinished(DWORD *status)
{
    if (status)
        *status = 2;
    return 1;
}

// The D44S board (the seat) is absent: its status and diagnosis buffers (8 bytes) empty, failed
static int __cdecl d44s_get_data(unsigned char *out)
{
    if (out)
        memset(out, 0, 8);
    return 0;
}

// ------------------------------------------------------------------------------
// The PJEI board
// ------------------------------------------------------------------------------

static float cal_value(unsigned array, int i)
{
    return (float)game_u32(array + i * 4);
}

// What the game reads for t (the wheel -1 left..1 right, a pedal 0..1): put onto its calibration
// (the left the calibration's higher end), or, unknown, its default one (the wheel 0xFF40 left,
// 0x7A40 centre, 0 right; the pedals at full range)
static unsigned short pot(int i, float t)
{
    float v;
    if (i == 0)
    {
        float low = 0, high = 0xFF40, centre = 0x7A40;
        if (g_known)
        {
            float a = cal_value(TD4_CAL_MIN, 0), b = cal_value(TD4_CAL_MAX, 0);
            low = a < b ? a : b;
            high = a < b ? b : a;
            centre = cal_value(TD4_CAL_CENTER, 0);
            if (!(centre > low && centre < high))
                centre = (low + high) / 2;
        }
        v = t < 0 ? centre - t * (high - centre) : centre - t * (centre - low);
    }
    else
    {
        float min = 0, max = 0xFFFF;
        if (g_known)
        {
            min = cal_value(TD4_CAL_MIN, i);
            max = cal_value(TD4_CAL_MAX, i);
        }
        v = min + t * (max - min);
    }
    return (unsigned short)(v < 0 ? 0 : v > 65535 ? 65535 : v);
}

static unsigned char g_status[40];
static volatile LONG g_coinStock; // coins in, not taken by the game yet
static int g_coinHeld;
static SRWLOCK g_statusLock = SRWLOCK_INIT;

static int __cdecl pjei_update_control_status_buffer(void)
{
    unsigned char s[40] = {0};
    if (held(IN_TEST))
        s[16] |= 0x20;
    if (held(IN_SERVICE))
        s[16] |= 0x10;
    int coin = held(IN_COIN);
    if (coin)
        s[16] |= 0x04;
    if (coin && !g_coinHeld)
        InterlockedIncrement(&g_coinStock);
    g_coinHeld = coin;
    if (held(IN_START))
        s[4] |= 0x04;
    if (held(IN_SHIFT_UP))
        s[4] |= 0x20;
    if (held(IN_SHIFT_DOWN))
        s[4] |= 0x40;
    if (held(IN_VIEW))
        s[4] |= 0x80;
    float t[3] = {(wl_input_get(&g_inputs[IN_STEERING]) - 32767.5f) / 32767.5f, wl_input_get(&g_inputs[IN_GAS]) / 65535.0f,
                  wl_input_get(&g_inputs[IN_BRAKE]) / 65535.0f};
    for (int i = 0; i < 3; i++)
    {
        unsigned short v = pot(i, t[i] < -1 ? -1 : t[i] > 1 ? 1 : t[i]);
        memcpy(s + 8 + i * 2, &v, 2);
    }
    if (calibrating())
    {
        // the wheel turned by the motor: a negative torque to the left end
        signed char torque = (signed char)(g_lastSend & 0xFF);
        unsigned short v = torque < -8 ? 0xFFC0 : torque > 8 ? 0x0040 : 0x8000;
        memcpy(s + 8, &v, 2);
    }
    AcquireSRWLockExclusive(&g_statusLock);
    memcpy(g_status, s, sizeof s);
    ReleaseSRWLockExclusive(&g_statusLock);
    return 1;
}

static int __cdecl pjei_get_control_status_buffer(unsigned char *out)
{
    if (!out)
        return 0;
    AcquireSRWLockShared(&g_statusLock);
    memcpy(out, g_status, sizeof g_status);
    ReleaseSRWLockShared(&g_statusLock);
    return 1;
}

static int __cdecl pjei_current_coinstock(int slot, DWORD *stock)
{
    if (stock)
        *stock = (DWORD)g_coinStock;
    return 1;
}

static int __cdecl pjei_consume_coinstock(int slot, DWORD coins)
{
    for (;;)
    {
        LONG was = g_coinStock, now = was > (LONG)coins ? was - (LONG)coins : 0;
        if (InterlockedCompareExchange(&g_coinStock, now, was) == was)
            return 1;
    }
}

// The security plugs: the one the game asks for found at once (0x14), none missing (8); the
// software id the latest plug checked, at first the game's (GKF04EAA)
#define PLUG_FOUND 0x14
#define PLUGS_PRESENT 8
static char g_plug[9] = "GKF04EAA";

static int __cdecl pjei_req_secplug_check(const char *id)
{
    if (id && strlen(id) == 8)
    {
        if (strcmp(g_plug, id))
            wl_log("thrill drive 4: software plug %s present", id);
        memcpy(g_plug, id, 8);
    }
    return 1;
}

static int __cdecl pjei_req_secplug_check_isfinished(DWORD *status)
{
    if (status)
        *status = PLUG_FOUND;
    return 1;
}

static int __cdecl pjei_req_secplug_missing_check_isfinished(DWORD *status)
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

static int __cdecl pjei_get_softwareid(char *out)
{
    return plug_id(out, (const unsigned char *)g_plug);
}

static int __cdecl pjei_get_systemid(char *out)
{
    static const unsigned char id[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    return plug_id(out, id);
}

// The lamps (8, a bit each), for the network outputs ([Outputs] OUTPUTS_NETWORK)
#define TD4_LAMPS 8
static volatile LONG g_lamps;

// The lamp calls, each kind with its arguments, the first 60
static void lamp_log(const char *what, ULONG_PTR a, ULONG_PTR b)
{
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 60)
        wl_log("thrill drive 4: lamp %s(0x%lx, 0x%lx)", what, (unsigned long)a, (unsigned long)b);
}

static int __cdecl pjei_control_lamp_on(int mask)
{
    LONG was = InterlockedOr(&g_lamps, mask & 0xFF), now = was | (mask & 0xFF);
    if (now != was)
        lamp_log("on", (ULONG_PTR)mask, (ULONG_PTR)now);
    wl_outputs_lamps((unsigned)now, TD4_LAMPS);
    return 1;
}

static int __cdecl pjei_control_lamp_off(int mask)
{
    LONG was = InterlockedAnd(&g_lamps, ~mask & 0xFF), now = was & ~mask & 0xFF;
    if (now != was)
        lamp_log("off", (ULONG_PTR)mask, (ULONG_PTR)now);
    wl_outputs_lamps((unsigned)now, TD4_LAMPS);
    return 1;
}

static int __cdecl pjei_control_lamp_bright(ULONG_PTR a, ULONG_PTR b)
{
    lamp_log("bright", a, b);
    return 1;
}

static int __cdecl pjei_control_lamp_mode(ULONG_PTR a)
{
    lamp_log("mode", a, 0);
    return 1;
}

static int __cdecl pjei_control_parallel(ULONG_PTR a)
{
    lamp_log("parallel", a, 0);
    return 1;
}

// The requests answered at once, done, and the board's other outputs (coin counters and blockers,
// lamps' brightness, parallel outputs, watchdog): nothing to drive here
static int __cdecl acio_true(void)
{
    return 1;
}

static int __cdecl acio_false(void)
{
    return 0;
}

static DWORD __cdecl acio_zero(void)
{
    return 0;
}

// ------------------------------------------------------------------------------
// The bus itself (both: libacio.dll and libtd4io.dll)
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
    wl_log("thrill drive 4: ACIO bus started (game %s)", version);
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
    wl_log("thrill drive 4: ACIO device %lu present", device);
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
// The ICCA card reader: there, never a card
// ------------------------------------------------------------------------------

enum
{
    WF_STEP = 0,
    WF_SLEEP = 1,
    WF_START = 2,
    WF_INIT = 3,
    WF_READY = 4,
    WF_GET_USERID = 5,
    WF_CLOSE_EJECT = 10,
    WF_CLOSE_END = 12,
};

static volatile LONG g_workflow = WF_STEP;

static int __cdecl icca_cardunit_init_isfinished(DWORD device, DWORD *status)
{
    if (status)
        *status = 4; // ready
    return 1;
}

static int __cdecl icca_req_uid_isfinished(DWORD device, DWORD *status)
{
    if (status)
        *status = 0xB; // no card
    return 1;
}

static int __cdecl icca_workflow(int workflow, DWORD device)
{
    LONG state;
    switch (workflow)
    {
    case WF_INIT:
    case WF_START:
        state = WF_READY;
        break;
    case WF_CLOSE_EJECT:
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
// device number. Byte 0 the state, the rest (sensors, card id, keypad) empty
static int __cdecl icca_get_status(ULONG_PTR a, ULONG_PTR b)
{
    unsigned char *out = (unsigned char *)(b > 2 ? b : a);
    if ((ULONG_PTR)out <= 2)
        return 0;
    memset(out, 0, 24);
    out[0] = (unsigned char)g_workflow;
    return 1;
}

static int __cdecl icca_get_uid(DWORD device, unsigned char *out)
{
    if (!out)
        return 0;
    memset(out, 0, 8);
    return 1;
}

static int __cdecl icca_get_uid_felica(DWORD device, unsigned char *out)
{
    if (!out)
        return 0;
    memset(out, 0, 9);
    return 1;
}

static int __cdecl icca_keep_alive_error(DWORD device, DWORD *error)
{
    if (error)
        *error = 0;
    return 1;
}

// ------------------------------------------------------------------------------
// The two buses' functions
// ------------------------------------------------------------------------------

#define FN(name, fn) {#name, 0, (void *)fn}

// The bus's own, on both
#define ACIO_BUS                                                                                                                          \
    FN(ac_io_begin, acio_begin), FN(ac_io_begin_get_status, acio_true), FN(ac_io_end, acio_true), FN(ac_io_end_get_status, acio_true),    \
        FN(ac_io_update, acio_update), FN(ac_io_is_active, acio_true), FN(ac_io_is_active2, acio_is_active2),                            \
        FN(ac_io_is_active_device, acio_true), FN(ac_io_get_version, acio_get_version),                                                  \
        FN(ac_io_get_firmware_update_device_index, acio_get_firmware_update_device_index), FN(ac_io_go_firmware_update, acio_true),      \
        FN(ac_io_get_rs232c_status, acio_get_rs232c_status), FN(ac_io_get_send_log, acio_get_log), FN(ac_io_get_recv_log, acio_get_log), \
        FN(ac_io_reset, acio_true), FN(ac_io_set_get_status_device, acio_true), FN(ac_io_soft_watch_dog_on, acio_true),                  \
        FN(ac_io_soft_watch_dog_off, acio_true), FN(ac_io_set_soft_watch_dog, acio_true), FN(ac_io_enable_game_ios, acio_true),          \
        FN(ac_io_avs_boot, acio_zero), FN(ac_io_avs_shutdown, acio_zero)

// td4.exe's imports of libacio.dll
static const WlVirtualDllFunction g_acio[] = {
    ACIO_BUS,
    FN(ac_io_pjei_update_control_status_buffer, pjei_update_control_status_buffer),
    FN(ac_io_pjei_get_control_status_buffer, pjei_get_control_status_buffer),
    FN(ac_io_pjei_current_coinstock, pjei_current_coinstock),
    FN(ac_io_pjei_consume_coinstock, pjei_consume_coinstock),
    FN(ac_io_pjei_req_secplug_check, pjei_req_secplug_check),
    FN(ac_io_pjei_req_secplug_check_isfinished, pjei_req_secplug_check_isfinished),
    FN(ac_io_pjei_req_secplug_check_softwareplug, pjei_req_secplug_check),
    FN(ac_io_pjei_req_secplug_check_systemplug, acio_true),
    FN(ac_io_pjei_req_get_control_status, acio_true),
    FN(ac_io_pjei_control_reset, acio_true),
    FN(ac_io_pjei_set_watchdog_time, acio_true),
    FN(ac_io_pjei_req_secplug_missing_check, acio_true),
    FN(ac_io_pjei_req_secplug_missing_check_isfinished, pjei_req_secplug_missing_check_isfinished),
    FN(ac_io_pjei_get_softwareid, pjei_get_softwareid),
    FN(ac_io_pjei_get_systemid, pjei_get_systemid),
    FN(ac_io_pjei_get_watchdog_status, acio_zero),
    FN(ac_io_pjei_get_coin_slot_noise_flag, acio_false),
    FN(ac_io_pjei_reset_coin_slot_noise_flag, acio_true),
    FN(ac_io_pjei_lock_coincounter, acio_true),
    FN(ac_io_pjei_unlock_coincounter, acio_true),
    FN(ac_io_pjei_control_coin_blocker_on, acio_true),
    FN(ac_io_pjei_control_coin_blocker_off, acio_true),
    FN(ac_io_pjei_control_lamp_bright, pjei_control_lamp_bright),
    FN(ac_io_pjei_control_lamp_mode, pjei_control_lamp_mode),
    FN(ac_io_pjei_control_lamp_on, pjei_control_lamp_on),
    FN(ac_io_pjei_control_lamp_off, pjei_control_lamp_off),
    FN(ac_io_pjei_control_parallel_on, pjei_control_parallel),
    FN(ac_io_pjei_control_parallel_off, pjei_control_parallel),
    FN(ac_io_icca_cardunit_init, acio_true),
    FN(ac_io_icca_cardunit_init_isfinished, icca_cardunit_init_isfinished),
    FN(ac_io_icca_req_uid, acio_true),
    FN(ac_io_icca_req_uid_isfinished, icca_req_uid_isfinished),
    FN(ac_io_icca_get_uid, icca_get_uid),
    FN(ac_io_icca_get_uid_felica, icca_get_uid_felica),
    FN(ac_io_icca_workflow, icca_workflow),
    FN(ac_io_icca_get_status, icca_get_status),
    FN(ac_io_icca_crypt_init, acio_true),
    FN(ac_io_icca_device_control_iccard_power_supply_on, acio_true),
    FN(ac_io_icca_device_control_iccard_power_supply_off, acio_true),
    FN(ac_io_icca_device_control_isfinished, acio_true),
    FN(ac_io_icca_get_keep_alive_error, icca_keep_alive_error),
    FN(ac_io_icca_send_keep_alive_packet, acio_zero),
};

// libtd4io.dll's: the bus, the D44H board, the D44S board (absent: every request fails)
static const WlVirtualDllFunction g_td4io[] = {
    ACIO_BUS,
    FN(ac_io_d44h_set_senddata, d44h_set_senddata),
    FN(ac_io_d44h_update_recvdata, acio_true),
    FN(ac_io_d44h_get_recvdata, d44h_get_recvdata),
    FN(ac_io_d44h_directreq_sendandrecv, d44h_directreq_sendandrecv),
    FN(ac_io_d44h_directreq_set_handle_limit, d44h_set_handle_limit),
    FN(ac_io_d44h_directreq_set_handle_limit_isfinished, d44h_set_handle_limit_isfinished),
    FN(ac_io_d44s_directreq_initialize, acio_false),
    FN(ac_io_d44s_directreq_selfdiagnosis, acio_false),
    FN(ac_io_d44s_directreq_gamestart, acio_false),
    FN(ac_io_d44s_directreq_gameover, acio_false),
    FN(ac_io_d44s_directreq_shockstrong, acio_false),
    FN(ac_io_d44s_directreq_shockmild, acio_false),
    FN(ac_io_d44s_directreq_isfinished, acio_false),
    FN(ac_io_d44s_update_recvstatusdata, acio_false),
    FN(ac_io_d44s_get_statusdata, d44s_get_data),
    FN(ac_io_d44s_directreq_getstatusdata, acio_false),
    FN(ac_io_d44s_update_recvdiagnosisdata, acio_false),
    FN(ac_io_d44s_get_diagnosisdata, d44s_get_data),
    FN(ac_io_d44s_directreq_getdiagnosisdata, acio_false),
};

// ------------------------------------------------------------------------------
// The game's imports and the PC's
// ------------------------------------------------------------------------------

// Each of td4.exe's libacio.dll imports pointed at its answer above; the others logged
static void answer_acio(HMODULE exe)
{
    unsigned char *base = (unsigned char *)exe;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int done = 0, unknown = 0;
    for (IMAGE_IMPORT_DESCRIPTOR *d = dir.VirtualAddress ? (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress) : NULL; d && d->Name; d++)
    {
        if (_stricmp((const char *)(base + d->Name), "libacio.dll"))
            continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        for (; names->u1.AddressOfData; names++)
        {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
            {
                wl_log("thrill drive 4: libacio.dll's function %u (by its number) not answered", (unsigned)IMAGE_ORDINAL(names->u1.Ordinal));
                unknown++;
                continue;
            }
            const char *name = (const char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name;
            int i = 0, count = (int)(sizeof g_acio / sizeof g_acio[0]);
            while (i < count && strcmp(g_acio[i].name, name))
                i++;
            if (i < count && wl_patch_iat(exe, "libacio.dll", name, g_acio[i].fn, NULL))
                done++;
            else
            {
                wl_log("thrill drive 4: libacio.dll's %s not answered (the copy's libacio.dll)", name);
                unknown++;
            }
        }
    }
    wl_log("thrill drive 4: %d libacio.dll functions answered (PJEI board, card reader), %d not", done, unknown);
}

// C: alone (the boot reads every drive's volume and free space)
static DWORD WINAPI td4_GetLogicalDriveStringsA(DWORD size, LPSTR out)
{
    static const char drives[] = "C:\\\0";
    if (!out || size < sizeof drives)
        return sizeof drives;
    memcpy(out, drives, sizeof drives);
    return sizeof drives - 1;
}

// td4.ini's [IO] HANDLE (KEYBOAD: the PC's keyboard, a development mode): the cabinet's wheel
static int ini_override(const char *file, const char *section, const char *key, char *value, size_t size)
{
    size_t n = strlen(file);
    if (n < 7 || _stricmp(file + n - 7, "td4.ini") || _stricmp(section, "IO") || _stricmp(key, "HANDLE"))
        return 0;
    snprintf(value, size, "KONAMI");
    return 1;
}

// ------------------------------------------------------------------------------
// The game
// ------------------------------------------------------------------------------

static int exists(const wchar_t *dir, const wchar_t *name)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s%s", dir, name);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

int wl_game_thrilldrive4(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    wchar_t dir[MAX_PATH];
    wcsncpy_s(dir, MAX_PATH, exePath, base - exePath);
    if (_wcsicmp(base, L"td4.exe") || !exists(dir, L"libacio.dll"))
        return 0;
    *name = "td4";
    *title = "Thrill Drive 4: Crazy Streets";
    HMODULE exe = GetModuleHandleW(NULL);
    wl_log("game: %s (Konami PC, CRC-32 %08lX)", *title, wl_game_exe_crc32());
    wl_game_set_boards("Controls on Konami ACIO board \"PJEI\" (libacio.dll: buttons, wheel, pedals)",
                       "Motor board: Konami ACIO board \"D44H\" (libtd4io.dll, the torque the game sends)");
    wl_inputs_set_table(g_inputs, IN_COUNT);

    // The export build: its fixed base and each change's bytes as found
    g_known = (ULONG_PTR)exe == TD4_BASE;
    for (int i = 0; g_known && i < (int)(sizeof g_patches / sizeof g_patches[0]); i++)
        __try
        {
            g_known = !memcmp((const void *)(ULONG_PTR)g_patches[i].address, g_patches[i].was, g_patches[i].length);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_known = 0;
        }
    if (g_known)
        for (int i = 0; i < (int)(sizeof g_patches / sizeof g_patches[0]); i++)
            wl_patch_code((void *)(ULONG_PTR)g_patches[i].address, g_patches[i].was, g_patches[i].now, g_patches[i].length, g_patches[i].what);
    else
        wl_log("thrill drive 4: not the export build windowsloader knows (F04:E:A:A:20070330 at base 0x400000): its code left as it is "
               "(the wheel's kind, the DATA CHECK, the clock and calibration settings), the controls on its default calibration");

    answer_acio(exe);
    if (!wl_patch_iat(exe, "kernel32.dll", "GetLogicalDriveStringsA", (void *)td4_GetLogicalDriveStringsA, NULL))
        wl_log("thrill drive 4: GetLogicalDriveStringsA not imported");
    wl_files_set_ini_override(ini_override);
    wl_system_virtual_dll("libtd4io.dll", g_td4io, (int)(sizeof g_td4io / sizeof g_td4io[0]));
    wl_outputs_name_lamps(WL_N("la carte PJEI, ac_io_pjei_control_lamp_on et _off (libacio.dll)"), NULL, TD4_LAMPS);
    wl_dinput_quiet_keyboard();
    return 1;
}

#else
int wl_game_thrilldrive4(const wchar_t *, const char **, const char **)
{
    return 0; // the game is 32 bits
}
#endif
