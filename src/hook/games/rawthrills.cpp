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

// Raw Thrills PC: the games of sdaemon.exe (x86), each told by its program's CRC-32 (every game's
// program has that name, its time stamp may change). Reimplemented from windowsarcadeloader's
// findings (aderumier, GPL v3, https://github.com/aderumier/windowsarcadeloader, crates/
// payload-rawthrills/src: sdaemon.rs, common.rs, h2o.rs, sb2.rs, dd.rs, cars.rs, batman.rs,
// rio.rs, nicktoons_3dnow.rs): each game's places, its board, what it needs. Batman's wheel force
// as found by FFBArcadePlugin (Boomslangnz, GPL v3, Game Files/Batman.cpp). Six families:
//
// - The JAMMA USB board read by the game itself (Direct3D 8): The Fast and the Furious (2004,
//   built 2006), its Drift (2007, built 2010), Super Bikes (2006, built 2010), Nicktoons Nitro
//   Racing (2009). The game reads its board in an I/O thread: each 4-byte report goes to its
//   parser, which keeps a held flag and a pressed-once latch per input and the analog bytes.
//   Reports (byte 0 = 4, byte 1 the kind):
//   - 0x80: byte 2 switches, active low (0x01 coin 1, 0x02 start, 0x04 coin 2, 0x08 brake or
//     handbrake, 0x10 service, 0x20 test, 0x80 bill); byte 3 active low (0x04 volume up, 0x08 down)
//   - 0xA0: byte 2 the accelerator, byte 3 the brake (1 released: the test menu's calibration
//     takes the smallest value but 0 as the minimum)
//   - 0xE0: byte 2 the DIP switches, active low (0x80 the link, 0x07 the cabinet - 1), byte 3 1
//   - 0xC0 + the keypad's key (0xF none): byte 2 the wheel (0x80 centred), byte 3 active low
//     (0x01 view, 0x02 button 2, 0x04 button 3, 0x08 tunes), active high its gear switches
//     0x10..0x80
//   The board is not opened: its init is replaced by a thread of ours handing the parser the
//   reports every 4 ms; the per-frame check of the board's status always finds it working. The
//   game's keyboard controls (developer keys) are turned off, the board's only. The base path of
//   its files (prefixed in fixed buffers) left empty: its names relative to the current
//   directory. The force (a byte to the motor by the parallel port): its function replaced, the
//   force played. Nicktoons also checks its board object and its HASP key's feature, both
//   answered; its vector maths in AMD 3DNow!, absent from today's processors, are translated to
//   SSE2 (each block checked byte for byte first).
// - H2Overdrive (2009, the 2010 "Triton" build, Direct3D 9): the same board as an object of the
//   game: its Open answered, its per-frame poll and its status block made of our reports. Service
//   and test also come through the game's press detector (its keyboard poller is off in this
//   build), the keys it reads with GetAsyncKeyState answered. Its PortTalk driver ("\\.\PortTalk",
//   absent) answered. The wheel's force and its spring read from the game.
// - SetControl (The Fast and the Furious SuperCars, 2010, the 2014 build, Direct3D 8): the game's
//   inputs behind its SetControl(code, value), called by a thread of ours: switches 0x10000 + n,
//   the analog values 0x10020..0x10022 (0..4095), the gear lever 0x10005..0x10008, the keypad
//   0x10080..0x1008b. Its board's init removed, the board present, its messages dropped.
//   Winter X Games SnoCross (2022 build 1.58, Direct3D 8) the same way on its own path without
//   a board, its protection's traps and its board missing reboot undone.
// - Super Bikes 2 (2010, Direct3D 8): the board's inputs in the game's keyboard table (a byte per
//   code: bit 0 held, bit 1 pressed once) and its analog table (a dword per code, 0..255), written
//   by our replacement of the game's input poll.
// - Dirty Drivin' (2011, build 1.14, Direct3D 9): without its cabinet's hardware the game drives
//   the truck from its developer keyboard: its steering and throttle floats are ours, START and
//   the crank its Space, VIEW its V; the coins by the coin board's callback, test, service and
//   the volume by its switches' latches.
// - The RIO board behind libusb0 (Cars, 2012, its 4 GB build, Direct3D 9; Batman, 2013,
//   Direct3D 11): one bus with the RIO (VID 0C70, PID 0780), every call succeeding, each read a
//   type 2 packet made of our inputs (the switches as 4-bit change counters, the analog values on
//   12 bits). Cars' HASP key (its API linked in the program) answered; Batman's cabinet check.
//
// The cabinet's keypad (12 keys: 1-9, *, 0, #) is the PC's numeric keypad: . or * for *, + or
// Enter for #. D:\ (the audits, settings, calibration, best times) is the game's folder.

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
#include "vdev.h"
#include "vscreen.h"

#if defined(_M_IX86)

// ------------------------------------------------------------------------------
// The places in the program
// ------------------------------------------------------------------------------

struct Site
{
    unsigned va; // the program's address
    unsigned char bytes[16];
    int length;
};

// A site and what replaces its first bytes
struct Fix
{
    Site site;
    unsigned char code[8];
    int length;
};

static const char *g_title;
static unsigned char *g_exe;
static unsigned g_base = 0x400000; // the program's own base (Batman's 0x12D0000)

static unsigned char *at(unsigned va)
{
    return g_exe + (va - g_base);
}

static int site_ok(const Site *s)
{
    return !s->va || !memcmp(at(s->va), s->bytes, s->length);
}

static int sites_ok(const Site *const *sites, int count)
{
    for (int i = 0; i < count; i++)
        if (!site_ok(sites[i]))
        {
            wl_log("rawthrills: %s: unexpected code at 0x%x, not the build this was made for: left alone", g_title, sites[i]->va);
            return 0;
        }
    return 1;
}

// Written over what is there now (its site checked before)
static int put(unsigned va, const void *bytes, int length, const char *what)
{
    char name[160];
    snprintf(name, sizeof name, "%s's %s", g_title, what);
    return wl_patch_code(at(va), at(va), bytes, length, name);
}

// The function at va replaced: mov eax, target; jmp eax
static int redirect(unsigned va, const void *target, const char *what)
{
    unsigned char code[7] = {0xB8, 0, 0, 0, 0, 0xFF, 0xE0};
    *(unsigned *)(code + 1) = (unsigned)(size_t)target;
    return put(va, code, 7, what);
}

static int apply_fixes(const Fix *fixes, int count, const char *what)
{
    for (int i = 0; i < count; i++)
        if (!site_ok(&fixes[i].site))
        {
            wl_log("rawthrills: %s: unexpected code at 0x%x, not the build this was made for: left alone", g_title, fixes[i].site.va);
            return 0;
        }
    for (int i = 0; i < count; i++)
        put(fixes[i].site.va, fixes[i].code, fixes[i].length, what);
    return 1;
}

static const unsigned char NOPS[12] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
static const unsigned char RET0[3] = {0x33, 0xC0, 0xC3};             // xor eax, eax; ret
static const unsigned char RET1[6] = {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3}; // mov eax, 1; ret

// ------------------------------------------------------------------------------
// Shared: the keypad, the force
// ------------------------------------------------------------------------------

static const unsigned char KEYPAD_NONE = 0x0F;

// The keypad's key (1-9, *, 0, #: 0..11) from the PC's numeric keypad, 0xF none
static unsigned char keypad_key(void)
{
    static const int keys[12][2] = {{VK_NUMPAD1, 0}, {VK_NUMPAD2, 0}, {VK_NUMPAD3, 0}, {VK_NUMPAD4, 0}, {VK_NUMPAD5, 0}, {VK_NUMPAD6, 0},
                                    {VK_NUMPAD7, 0}, {VK_NUMPAD8, 0}, {VK_NUMPAD9, 0}, {VK_DECIMAL, VK_MULTIPLY}, {VK_NUMPAD0, 0},
                                    {VK_ADD, VK_RETURN}};
    for (int i = 0; i < 12; i++)
        for (int k = 0; k < 2 && keys[i][k]; k++)
            if (GetAsyncKeyState(keys[i][k]) & 0x8000)
                return (unsigned char)i;
    return KEYPAD_NONE;
}

// The game's force (-1..1, > 0 pushing left), played when it changes
static void force(double f)
{
    static double last = 0;
    if (f != f)
        f = 0;
    f = f < -1 ? -1 : f > 1 ? 1 : f;
    if (f != last)
    {
        last = f;
        wl_ffb_set(f);
    }
}

static int held(WlInput *inputs, int i)
{
    return wl_input_get(&inputs[i]) != 0;
}

// A float of the game, 0 when it cannot be read (its objects not made yet)
static float read_float(unsigned va)
{
    __try
    {
        float f = *(volatile float *)(size_t)va;
        return f == f ? f : 0.0f;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0.0f;
    }
}

static unsigned read_u32(unsigned va)
{
    __try
    {
        return *(volatile unsigned *)(size_t)va;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

// ==============================================================================
// The JAMMA USB board, read by the game (The Fast and the Furious, Drift, Super Bikes, Nicktoons)
// ==============================================================================

enum
{
    RT_STEERING,
    RT_GAS,
    RT_BRAKE,
    RT_COIN,
    RT_START,
    RT_SERVICE,
    RT_TEST,
    RT_HANDBRAKE, // the brake switch (Super Bikes' brake, The Fast and the Furious' handbrake)
    RT_VIEW,
    RT_TUNES,
    RT_GEARUP,
    RT_GEARDOWN,
    RT_VOLUP,
    RT_VOLDOWN,
    RT_COUNT
};

static WlInput g_inputs[RT_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 255, 128},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 1, 255, 1},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 1, 255, 1},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"Handbrake", "Handbrake", WL_INPUT_SWITCH, 0, 1, 0},
    {"View", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"Tunes", "MusicUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"GearUp", "GearUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"GearDown", "GearDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"VolumeUp", "VolumeUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"VolumeDown", "VolumeDown", WL_INPUT_SWITCH, 0, 1, 0},
};

// Nicktoons, a kart: no handbrake, no gears, no tunes
static WlInput g_kartInputs[] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 255, 128},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 1, 255, 1},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 1, 255, 1},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"View", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"VolumeUp", "VolumeUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"VolumeDown", "VolumeDown", WL_INPUT_SWITCH, 0, 1, 0},
};

enum
{
    IO_WRITE, // an instruction writing the flags: removed
    IO_STORE, // an instruction storing them in var: removed, var the board's
    IO_IMM    // mov dword [var], imm32: its immediate the board's
};

enum
{
    GEARS_SEQUENTIAL, // SHIFT up, the developer key Down made input 0x1006 (Super Bikes)
    GEARS_LEVER,      // a 4 position lever (6 with Drift's -6speed), kept here from up and down
    GEARS_NONE        // a kart
};

struct RtGame
{
    Site boardInit, parser, boardCheck, basePath, keyboardHeld, keyboardOnce;
    int ioKind;
    Site io;
    unsigned ioVar;
    int gears;
    Site shiftDownKey; // GEARS_SEQUENTIAL
    unsigned gameState, sixSpeed;
    int brakePedalSwitch; // the brake pedal also presses the brake switch
    Site ffb;             // va 0: none
};

static const RtGame g_superbikes = {
    {0x4c47b0, {0x51, 0xe8, 0x1a, 0x58, 0x00, 0x00, 0x85, 0xc0}, 8},
    {0x4c4900, {0x53, 0x55, 0x56, 0x8b, 0x74, 0x24, 0x10}, 7},
    {0x4b7949, {0x74, 0x04, 0x0b, 0xc8}, 4},
    {0x4b37a0, {0x8b, 0x44, 0x24, 0x04, 0x57, 0x85, 0xc0}, 7},
    {0x4b8006, {0x81, 0xf9, 0xff, 0x00, 0x00, 0x00}, 6},
    {0x4b81df, {0x8a, 0x91, 0xc8, 0x43, 0x59, 0x03}, 6},
    IO_WRITE, {0x44aaf6, {0x36, 0xc7, 0x05, 0x04, 0x45, 0x55, 0x00, 0x00, 0x07, 0x00, 0x00}, 11}, 0,
    GEARS_SEQUENTIAL, {0x45c650, {0x68, 0xd0, 0x00, 0x00, 0x00}, 5}, 0x57eb88, 0,
    1, {0}};

static const RtGame g_fnf = {
    {0x486800, {0x51, 0xe8, 0x8a, 0x8e, 0x01, 0x00, 0x85, 0xc0}, 8},
    {0x486950, {0x53, 0x55, 0x57, 0x8b, 0x7c, 0x24, 0x10}, 7},
    {0x4834b9, {0x74, 0x04, 0x0b, 0xc8}, 4},
    {0x4769e0, {0x8b, 0x44, 0x24, 0x04, 0x57, 0x85, 0xc0}, 7},
    {0x483b76, {0x81, 0xf9, 0xff, 0x00, 0x00, 0x00}, 6},
    {0x483d4f, {0x8a, 0x91, 0x88, 0x69, 0x5b, 0x03}, 6},
    IO_STORE, {0x440240, {0x89, 0x35, 0x94, 0x59, 0x53, 0x00}, 6}, 0x535994,
    GEARS_LEVER, {0}, 0, 0,
    0, {0x46de10, {0xa1, 0xd4, 0x2e, 0x5b, 0x00, 0x85, 0xc0, 0x75, 0x03}, 9}};

static const RtGame g_drift = {
    {0x4d9db0, {0xc3, 0xe8, 0x4a, 0x65, 0x00, 0x00, 0x85, 0xc0}, 8}, // the copy returns at once
    {0x4d9f00, {0x53, 0x55, 0x56, 0x8b, 0x74, 0x24, 0x10}, 7},
    {0x4ccfc9, {0x74, 0x04, 0x0b, 0xc8}, 4},
    {0x4c7c60, {0x8b, 0x44, 0x24, 0x04, 0x57, 0x85, 0xc0}, 7},
    {0x4cd686, {0x81, 0xf9, 0xff, 0x00, 0x00, 0x00}, 6},
    {0x4cd85f, {0x8a, 0x91, 0x20, 0x59, 0x5b, 0x04}, 6},
    IO_IMM, {0x4553f6, {0xc7, 0x05, 0x1c, 0xfc, 0x56, 0x00, 0x00, 0x1f, 0x00, 0x00}, 10}, 0,
    GEARS_LEVER, {0}, 0, 0x537fa4,
    1, {0x48a830, {0xa1, 0xa4, 0x3c, 0x5a, 0x00, 0x85, 0xc0, 0x75, 0x03}, 9}};

static const RtGame g_nicktoons = {
    {0x4a0cc0, {0x51, 0xe8, 0x5a, 0xa0, 0x00, 0x00, 0x85, 0xc0}, 8},
    {0x4a0e10, {0x53, 0x55, 0x56, 0x8b, 0x74, 0x24, 0x10}, 7},
    {0x494d79, {0x74, 0x04, 0x0b, 0xc8}, 4},
    {0x491a40, {0x8b, 0x44, 0x24, 0x04, 0x57, 0x85, 0xc0}, 7},
    {0x49541c, {0x81, 0xf9, 0xff, 0x00, 0x00, 0x00}, 6},
    {0x49560c, {0x8a, 0x91, 0x70, 0xc7, 0xe4, 0x01}, 6},
    IO_IMM, {0x444456, {0xc7, 0x05, 0x14, 0xda, 0x5e, 0x00, 0x00, 0x10, 0x00, 0x00}, 10}, 0,
    GEARS_NONE, {0}, 0, 0,
    0, {0x485010, {0xa1, 0xc8, 0x57, 0x64, 0x00, 0x85, 0xc0, 0x75, 0x03}, 9}};

// Nicktoons: its board object, read at boot and every frame (its lamps and watchdog sent to it),
// and its cabinet's feature check (without the HASP key, its SDK waits on the network licence
// manager)
static const Site NT_BOARD_SEND = {0x4aabd0, {0x56, 0x8b, 0x74, 0x24, 0x08, 0x33, 0xc9}, 7};
static const Site NT_FEATURE_CHECK = {0x44a8e0, {0x51, 0xa1, 0x88, 0xef, 0x5b, 0x00}, 6};
static const unsigned NT_BOARD_STATE = 0x1e57b08;

static const RtGame *g_jamma;
static WlInput *g_jammaInputs;

static int g_lever, g_leverUp, g_leverDown; // the lever kept here, 0 = 1st

static const unsigned char LEVER_4[4] = {0x10, 0x20, 0x40, 0x80};
// Drift's 6 speed gate: its column (0x10 left, 0x20 right) and row (0x40 up, 0x80 down)
static const unsigned char LEVER_6[6] = {0x50, 0x60, 0x10, 0x20, 0x90, 0xa0};

// An input of the JAMMA games' tables by its name (Nicktoons' has fewer)
static int jamma_pressed(int i)
{
    if (g_jammaInputs == g_inputs)
        return held(g_inputs, i);
    WlInput *in = wl_input_find(g_inputs[i].name);
    return in && wl_input_get(in) != 0;
}

static int jamma_value(int i)
{
    if (g_jammaInputs == g_inputs)
        return wl_input_get(&g_inputs[i]);
    WlInput *in = wl_input_find(g_inputs[i].name);
    return in ? wl_input_get(in) : g_inputs[i].rawRest;
}

static void make_reports(unsigned char r[3][4])
{
    unsigned char switches = 0, panel = 0, bike = 0;
    if (jamma_pressed(RT_COIN))
        switches |= 0x01;
    if (jamma_pressed(RT_START))
        switches |= 0x02;
    if (jamma_pressed(RT_HANDBRAKE))
        switches |= 0x08;
    if (jamma_pressed(RT_SERVICE))
        switches |= 0x10;
    if (jamma_pressed(RT_TEST))
        switches |= 0x20;
    if (jamma_pressed(RT_VOLUP))
        panel |= 0x04;
    if (jamma_pressed(RT_VOLDOWN))
        panel |= 0x08;
    if (jamma_pressed(RT_VIEW))
        bike |= 0x01;
    if (jamma_pressed(RT_TUNES))
        bike |= 0x08;
    int up = jamma_pressed(RT_GEARUP), down = jamma_pressed(RT_GEARDOWN);
    if (g_jamma->gears == GEARS_SEQUENTIAL)
    {
        // SHIFT; in a race input 0x1006, else the brake (the garage's neon colour)
        if (up)
            bike |= 0x10;
        if (down)
        {
            unsigned state = *(volatile unsigned *)at(g_jamma->gameState);
            if (state == 0xB || state == 0xC)
                bike |= 0x20;
            else
                switches |= 0x08;
        }
    }
    else if (g_jamma->gears == GEARS_LEVER)
    {
        int six = g_jamma->sixSpeed && *(volatile unsigned *)at(g_jamma->sixSpeed) == 0;
        int last = six ? 5 : 3;
        if (up && !g_leverUp && g_lever < last)
            g_lever++;
        if (down && !g_leverDown && g_lever > 0)
            g_lever--;
        if (g_lever > last)
            g_lever = last;
        bike |= six ? LEVER_6[g_lever] : LEVER_4[g_lever];
    }
    g_leverUp = up;
    g_leverDown = down;
    int brake = jamma_value(RT_BRAKE);
    if (g_jamma->brakePedalSwitch && brake > 1 + 254 / 3)
        switches |= 0x08;
    bike = (unsigned char)((~bike & 0x0F) | (bike & 0xF0)); // the buttons active low, the gear switches high
    r[0][0] = 4, r[0][1] = 0x80, r[0][2] = (unsigned char)~switches, r[0][3] = (unsigned char)~panel;
    r[1][0] = 4, r[1][1] = 0xA0, r[1][2] = (unsigned char)jamma_value(RT_GAS), r[1][3] = (unsigned char)brake;
    r[2][0] = 4, r[2][1] = (unsigned char)(0xC0 | keypad_key()), r[2][2] = (unsigned char)jamma_value(RT_STEERING), r[2][3] = bike;
}

typedef void(__cdecl *ParseFn)(unsigned char *report);

static DWORD WINAPI board_thread(void *)
{
    ParseFn parse = (ParseFn)at(g_jamma->parser.va);
    for (;;)
    {
        unsigned char dips[4] = {4, 0xE0, 0xFF, 1}; // the link off, cabinet 1
        parse(dips);
        unsigned char r[3][4];
        make_reports(r);
        for (int i = 0; i < 3; i++)
            parse(r[i]);
        Sleep(4);
    }
}

// The board's init (cdecl, its result unused): no device, our thread instead of the game's
static unsigned __cdecl board_init(void)
{
    wl_log("rawthrills: %s: the JAMMA USB board's reports from windowsloader's inputs", g_title);
    CloseHandle(CreateThread(NULL, 0, board_thread, NULL, 0, NULL));
    return 0;
}

// The motor's force (cdecl u8(float -1..1, > 0 pushing left)): played, the byte the game would send
static unsigned char __cdecl motor_force(float f)
{
    f = f != f ? 0.0f : f < -1 ? -1.0f : f > 1 ? 1.0f : f;
    force(f);
    unsigned char b = (unsigned char)((f < 0 ? -f : f) * 127.0f) & 0x7F;
    return f < 0 ? (unsigned char)(b | 0x80) : b;
}

// ------------------------------------------------------------------------------
// Nicktoons' 3DNow! maths, in SSE2: only whole straight-line blocks between FEMMS, each one checked
// by its FNV-1a. The XMM registers are kept (the block's are saved on the stack and given back),
// the flags too; the loads and stores stay 64 bits
// ------------------------------------------------------------------------------

struct NtBlock
{
    unsigned va;
    int size;
    unsigned fnv;
};

static const NtBlock NT_BLOCKS[] = {
    {0x427ef2, 41, 0xb7624eed}, {0x427ffa, 41, 0x9adf893d}, {0x42809e, 41, 0x9adf893d}, {0x4281b6, 41, 0x9adf893d},
    {0x4282be, 41, 0x9adf893d}, {0x428362, 41, 0x9adf893d}, {0x4284ad, 41, 0x9adf893d}, {0x42853a, 41, 0x9adf893d},
    {0x428f78, 41, 0x9adf893d}, {0x429080, 41, 0x9adf893d}, {0x429124, 41, 0x9adf893d}, {0x42923c, 41, 0x9adf893d},
    {0x429344, 41, 0x9adf893d}, {0x4293e8, 41, 0x9adf893d}, {0x429544, 41, 0x9adf893d}, {0x429606, 41, 0x9adf893d},
    {0x429729, 41, 0x9adf893d}, {0x429850, 41, 0x9adf893d}, {0x429920, 41, 0x9adf893d}, {0x429a28, 41, 0x9adf893d},
    {0x429acc, 41, 0x9adf893d}, {0x429be4, 41, 0x9adf893d}, {0x429cec, 41, 0x9adf893d}, {0x429d90, 41, 0x9adf893d},
    {0x429fae, 41, 0x6ff678a1}, {0x42a054, 41, 0xf2a08e81}, {0x42a196, 41, 0xf2a08e81}, {0x42a215, 41, 0xf2a08e81},
    {0x42a364, 41, 0xf2a08e81}, {0x42a40a, 41, 0xf2a08e81}, {0x42a508, 41, 0xf2a08e81}, {0x42a5be, 41, 0xf2a08e81},
    {0x42a6b6, 41, 0xf2a08e81}, {0x42a777, 41, 0xf2a08e81}, {0x42a7ec, 41, 0xf2a08e81}, {0x442385, 45, 0xa97d3876},
    {0x4423e0, 47, 0x263a0709}, {0x4424d3, 45, 0xb987f396}, {0x44265f, 45, 0xb987f396}, {0x4426d3, 45, 0xb987f396},
    {0x442838, 48, 0xcb1f63c0}, {0x442a4d, 48, 0x3d448f60}, {0x4909eb, 54, 0xc2574052}, {0x4916a0, 458, 0x17acddb9},
    {0x4b4c10, 47, 0x13282f8b}, {0x4b8278, 45, 0x6c04b1e6}, {0x4b83d2, 41, 0xa9404805}, {0x4b8633, 45, 0x6c04b1e6},
    {0x4b872f, 41, 0xa9404805}, {0x4b8b3d, 41, 0x72ff9bf5}, {0x4b90f4, 41, 0x1157fa85}, {0x4b917c, 41, 0x95fd33a5},
    {0x4b9308, 41, 0x95fd33a5}, {0x4b9343, 41, 0x95fd33a5}, {0x4b93cb, 41, 0x95fd33a5}, {0x4b961d, 41, 0x95fd33a5},
    {0x4b966d, 41, 0x95fd33a5}, {0x4b9b8a, 41, 0xc7b4d12d}, {0x4b9bdb, 41, 0x7fa6879d}, {0x4b9d4f, 41, 0x7fa6879d},
    {0x4b9d8a, 41, 0x7fa6879d}, {0x4b9df8, 41, 0x7fa6879d}, {0x4ba087, 41, 0x1157fa85}, {0x4ba32b, 41, 0x86bac159},
    {0x4ba496, 45, 0x0d71ea3a}, {0x4ba50a, 45, 0x98e6a522}, {0x4ba57a, 45, 0x98e6a522}, {0x4ba62e, 41, 0x86bac159},
    {0x4ba666, 41, 0x120d9a41}, {0x4ba749, 41, 0x86bac159}, {0x4ba781, 41, 0x86bac159}, {0x4bbf59, 41, 0x47580435},
    {0x4bc049, 41, 0x47580435}, {0x4bc194, 41, 0x47580435}, {0x4bc83c, 45, 0xecb22022}, {0x4bcb20, 45, 0x8604d3de},
    {0x4bcc6f, 45, 0xb5d368da}, {0x4bcd79, 45, 0xadf2ab1a}, {0x4bcecb, 45, 0xadf2ab1a}, {0x4c763d, 45, 0x6c04b1e6},
    {0x4c778d, 41, 0xa9404805}, {0x4c79d7, 45, 0x6c04b1e6}, {0x4c7ac3, 41, 0xa9404805}, {0x4c7d8e, 41, 0x2b220175},
    {0x4c7de5, 45, 0x688b27e2}, {0x4c7e99, 41, 0x2b220175}, {0x4c7ed1, 41, 0x7163eced}, {0x4c80a9, 45, 0x74e11dfa},
    {0x4c8120, 45, 0x688b27e2}, {0x4c823e, 41, 0x2b220175}, {0x4c8276, 41, 0x2b220175}, {0x4c88d4, 44, 0xd275922b},
    {0x4c8a4f, 41, 0x1157fa85}, {0x4c8a8a, 41, 0x1157fa85}, {0x4c8b13, 41, 0x1157fa85}, {0x4c8d46, 41, 0x1157fa85},
    {0x4c8d9b, 41, 0x1157fa85}, {0x4d4a14, 45, 0xa9edaae6}, {0x4d4b0f, 45, 0x409a9f86}, {0x4d4cf5, 45, 0x1c998922},
    {0x4d4da0, 41, 0x5978a66d}, {0x4d7e7c, 41, 0x26aa5e79}, {0x4d7ea5, 41, 0x2827905d}, {0x4d800e, 41, 0x26aa5e79},
    {0x4d8037, 41, 0xde56f615},
};

static const int NT_STACK = 144; // 8 XMM registers and a spare one

struct Code
{
    unsigned char b[2048];
    int n;
    int ok;
};

static void emit(Code *c, const void *bytes, int length)
{
    if (c->n + length > (int)sizeof c->b)
    {
        c->ok = 0;
        return;
    }
    memcpy(c->b + c->n, bytes, length);
    c->n += length;
}

static void emit1(Code *c, unsigned char b)
{
    emit(c, &b, 1);
}

static void emit32(Code *c, int v)
{
    emit(c, &v, 4);
}

// One ModRM operand copied (out, its length), the references to the original stack moved by the
// space reserved for the XMM registers; 0 for an unusual ESP addressing
static int nt_operand(const unsigned char *code, int avail, unsigned char *out, int *outLen, int *used)
{
    if (avail < 1)
        return 0;
    unsigned char m = code[0];
    int mode = m >> 6, rm = m & 7, n = 1, stack = 0, absolute = mode == 0 && rm == 5;
    int o = 0;
    out[o++] = m;
    if (mode != 3 && rm == 4)
    {
        if (avail < 2)
            return 0;
        unsigned char sib = code[1];
        n = 2;
        out[o++] = sib;
        stack = (sib & 7) == 4;
        if (stack && ((sib >> 3) & 7) != 4)
            return 0;
        absolute = mode == 0 && (sib & 7) == 5;
    }
    int count = mode == 1 ? 1 : (mode == 2 || absolute) ? 4 : 0;
    if (n + count > avail)
        return 0;
    if (stack)
    {
        int disp = count == 0 ? 0 : count == 1 ? (signed char)code[n] : *(const int *)(code + n);
        out[0] = (unsigned char)((m & 0x3f) | 0x80);
        disp += NT_STACK;
        memcpy(out + o, &disp, 4);
        o += 4;
    }
    else
    {
        memcpy(out + o, code + n, count);
        o += count;
    }
    *outLen = o;
    *used = n + count;
    return 1;
}

// movups [esp + offset], xmm (store) or movups xmm, [esp + offset] (load)
static void xmm_stack(Code *c, int reg, int offset, int load)
{
    unsigned char op[4] = {0x0f, (unsigned char)(load ? 0x10 : 0x11), (unsigned char)(0x84 | (reg << 3)), 0x24};
    emit(c, op, 4);
    emit32(c, offset);
}

static int nt_translate(const unsigned char *code, int len, Code *c)
{
    c->n = 0;
    c->ok = 1;
    if (len < 4 || code[0] != 0x0f || code[1] != 0x0e || code[len - 2] != 0x0f || code[len - 1] != 0x0e)
        return 0;
    static const unsigned char head[5] = {0x0f, 0x77, 0x8d, 0xa4, 0x24}; // emms; lea esp, [esp - NT_STACK]
    emit(c, head, 5);
    emit32(c, -NT_STACK);
    for (int r = 0; r < 8; r++)
        xmm_stack(c, r, r * 16, 0);
    int p = 2;
    while (p < len - 2)
    {
        unsigned char op = code[p++];
        unsigned char arg[8];
        int argLen, used;
        if (op >= 0xb8 && op <= 0xbf) // mov r32, imm32
        {
            if (p + 4 > len)
                return 0;
            emit1(c, op);
            emit(c, code + p, 4);
            p += 4;
        }
        else if (op == 0x8b) // mov r32, r/m32
        {
            if (!nt_operand(code + p, len - p, arg, &argLen, &used))
                return 0;
            emit1(c, op);
            emit(c, arg, argLen);
            p += used;
        }
        else if (op == 0x0f)
        {
            if (p >= len)
                return 0;
            unsigned char op2 = code[p++];
            if (!nt_operand(code + p, len - p, arg, &argLen, &used))
                return 0;
            p += used;
            int reg = (arg[0] >> 3) & 7, regForm = (arg[0] >> 6) == 3;
            if (op2 == 0x0d) // AMD's prefetch, only a hint
            {
            }
            else if (op2 == 0x6f) // movq mm, mm/m64: movq xmm, xmm/m64
            {
                static const unsigned char o[3] = {0xf3, 0x0f, 0x7e};
                emit(c, o, 3);
                emit(c, arg, argLen);
            }
            else if (op2 == 0x7f) // movq mm/m64, mm: movq xmm/m64, xmm
            {
                static const unsigned char o[3] = {0x66, 0x0f, 0xd6};
                emit(c, o, 3);
                emit(c, arg, argLen);
            }
            else if (op2 == 0x62 && regForm) // punpckldq
            {
                static const unsigned char o[3] = {0x66, 0x0f, 0x62};
                emit(c, o, 3);
                emit(c, arg, argLen);
            }
            else if (op2 == 0x6a && regForm) // punpckhdq: MMX's high dword is lane 1, SSE's high half lanes 2-3
            {
                unsigned char s[4] = {0x0f, 0xc6, arg[0], 0x55};                                        // shufps reg, rm, 0x55
                unsigned char d[5] = {0x66, 0x0f, 0x70, (unsigned char)(0xc0 | (reg << 3) | reg), 0xd8}; // pshufd reg, reg, 0xd8
                emit(c, s, 4);
                emit(c, d, 5);
            }
            else if (op2 == 0x0f) // 3DNow!: its operation after the operand
            {
                if (p >= len)
                    return 0;
                unsigned char math = code[p] == 0x9e ? 0x58 : code[p] == 0x9a ? 0x5c : code[p] == 0xb4 ? 0x59 : 0; // pfadd, pfsub, pfmul
                if (!math)
                    return 0;
                p++;
                if (regForm)
                {
                    emit1(c, 0x0f);
                    emit1(c, math);
                    emit(c, arg, argLen);
                }
                else
                {
                    // SSE's packed maths would read 16 bytes: the 8 bytes loaded in a spare register first
                    int temp = (reg + 1) & 7;
                    xmm_stack(c, temp, 128, 0);
                    unsigned char o[4] = {0xf3, 0x0f, 0x7e, (unsigned char)((arg[0] & 0xc7) | (temp << 3))};
                    emit(c, o, 4);
                    emit(c, arg + 1, argLen - 1);
                    unsigned char m[3] = {0x0f, math, (unsigned char)(0xc0 | (reg << 3) | temp)};
                    emit(c, m, 3);
                    xmm_stack(c, temp, 128, 1);
                }
            }
            else
                return 0;
        }
        else
            return 0;
    }
    if (p != len - 2)
        return 0;
    for (int r = 0; r < 8; r++)
        xmm_stack(c, r, r * 16, 1);
    static const unsigned char tail[3] = {0x8d, 0xa4, 0x24}; // lea esp, [esp + NT_STACK]
    emit(c, tail, 3);
    emit32(c, NT_STACK);
    static const unsigned char emms[2] = {0x0f, 0x77};
    emit(c, emms, 2);
    return c->ok;
}

static int nicktoons_3dnow(void)
{
    if (IsProcessorFeaturePresent(PF_3DNOW_INSTRUCTIONS_AVAILABLE))
    {
        wl_log("rawthrills: %s: this processor has 3DNow!, the game's maths left as they are", g_title);
        return 1;
    }
    const int count = (int)(sizeof NT_BLOCKS / sizeof NT_BLOCKS[0]);
    static Code blocks[sizeof NT_BLOCKS / sizeof NT_BLOCKS[0]];
    int total = 0;
    for (int i = 0; i < count; i++)
    {
        const unsigned char *code = at(NT_BLOCKS[i].va);
        unsigned h = 2166136261u;
        for (int k = 0; k < NT_BLOCKS[i].size; k++)
            h = (h ^ code[k]) * 16777619u;
        if (h != NT_BLOCKS[i].fnv || !nt_translate(code, NT_BLOCKS[i].size, &blocks[i]))
        {
            wl_log("rawthrills: %s: unexpected 3DNow! block at 0x%x: none translated", g_title, NT_BLOCKS[i].va);
            return 0;
        }
        total += blocks[i].n + 5;
    }
    unsigned char *mem = (unsigned char *)VirtualAlloc(NULL, total, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem)
        return 0;
    unsigned char *dest = mem;
    for (int i = 0; i < count; i++)
    {
        memcpy(dest, blocks[i].b, blocks[i].n);
        unsigned char *jmp = dest + blocks[i].n;
        unsigned back = NT_BLOCKS[i].va + NT_BLOCKS[i].size;
        jmp[0] = 0xE9;
        *(int *)(jmp + 1) = (int)(back - ((unsigned)(size_t)jmp + 5));
        blocks[i].n = (int)(dest - mem); // now its place in mem
        dest = jmp + 5;
    }
    DWORD old;
    VirtualProtect(mem, total, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), mem, total);
    for (int i = 0; i < count; i++)
    {
        unsigned char *site = at(NT_BLOCKS[i].va);
        unsigned char jmp[5] = {0xE9};
        *(int *)(jmp + 1) = (int)((unsigned)(size_t)(mem + blocks[i].n) - ((unsigned)(size_t)site + 5));
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy(site, jmp, 5);
        VirtualProtect(site, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    wl_log("rawthrills: %s: its %d 3DNow! blocks played in SSE2 (this processor has no 3DNow!)", g_title, count);
    return 1;
}

static int install_jamma(const RtGame *g, int nicktoons)
{
    const Site *sites[] = {&g->boardInit, &g->parser, &g->boardCheck, &g->basePath, &g->keyboardHeld, &g->keyboardOnce,
                           &g->io, &g->shiftDownKey, &g->ffb, nicktoons ? &NT_BOARD_SEND : &g->ffb, nicktoons ? &NT_FEATURE_CHECK : &g->ffb};
    if (!sites_ok(sites, (int)(sizeof sites / sizeof sites[0])))
        return 0;
    if (nicktoons && !nicktoons_3dnow())
        return 0;
    g_jamma = g;
    if (nicktoons)
    {
        // a real object, the game's status paths may read it
        static unsigned char device[256];
        unsigned p = (unsigned)(size_t)device;
        put(NT_BOARD_STATE, &p, 4, "JAMMA USB board object (present)");
        put(NT_BOARD_SEND.va, RET0, 3, "JAMMA USB board's lamps and watchdog (taken)");
        put(NT_FEATURE_CHECK.va, RET1, 6, "cabinet's HASP feature check (passed)");
    }
    redirect(g->boardInit.va, (void *)board_init, "JAMMA USB board init");
    put(g->boardCheck.va, NOPS, 2, "board status check");
    put(g->basePath.va, RET0, 3, "files' base path (left empty)");
    put(g->keyboardHeld.va, RET0, 3, "keyboard controls (held)");
    put(g->keyboardOnce.va, RET0, 3, "keyboard controls (pressed once)");
    if (g->gears == GEARS_SEQUENTIAL)
    {
        static const unsigned char push1006[5] = {0x68, 0x06, 0x10, 0x00, 0x00};
        put(g->shiftDownKey.va, push1006, 5, "shift down (input 0x1006)");
    }
    if (g->ffb.va)
        redirect(g->ffb.va, (void *)motor_force, "wheel motor force");
    unsigned board = 0x1000; // the board's controls only
    if (g->ioKind == IO_WRITE)
        put(g->io.va, NOPS, g->io.length, "keyboard and mouse controls (off)");
    else if (g->ioKind == IO_STORE)
    {
        if (put(g->io.va, NOPS, g->io.length, "keyboard and mouse controls (off)"))
            *(volatile unsigned *)at(g->ioVar) = board;
    }
    else
    {
        unsigned char code[16];
        memcpy(code, g->io.bytes, g->io.length);
        memcpy(code + g->io.length - 4, &board, 4);
        put(g->io.va, code, g->io.length, "keyboard and mouse controls (off)");
    }
    g_jammaInputs = nicktoons ? g_kartInputs : g_inputs;
    if (nicktoons)
        wl_inputs_set_table(g_kartInputs, (int)(sizeof g_kartInputs / sizeof g_kartInputs[0]));
    else
        wl_inputs_set_table(g_inputs, RT_COUNT);
    wl_game_set_boards("Controls on Raw Thrills JAMMA USB board (its reports given to the game's parser)",
                       g->ffb.va ? "Motor board: the wheel's motor by the parallel port (its force played)" : NULL);
    return 1;
}

// ==============================================================================
// H2Overdrive: the JAMMA USB board as an object of the game
// ==============================================================================

enum
{
    H2_STEERING,
    H2_GAS, // the throttle lever pushed forward
    H2_BRAKE, // pulled back into reverse
    H2_COIN,
    H2_START,
    H2_SERVICE,
    H2_TEST,
    H2_VIEW,
    H2_VIEW2,
    H2_VIEW3,
    H2_BOOST, // START's input too
    H2_VOLUP,
    H2_VOLDOWN,
    H2_COUNT
};

static WlInput g_h2oInputs[H2_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 255, 128},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 255, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 1, 255, 1},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"View", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"View2", "View2", WL_INPUT_SWITCH, 0, 1, 0},
    {"View3", "View3", WL_INPUT_SWITCH, 0, 1, 0},
    {"Boost", "Nitro", WL_INPUT_SWITCH, 0, 1, 0},
    {"VolumeUp", "VolumeUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"VolumeDown", "VolumeDown", WL_INPUT_SWITCH, 0, 1, 0},
};

static const Site H2_OPEN = {0x441c20, {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x81, 0xec, 0x4c, 0x02, 0x00, 0x00}, 12};
static const Site H2_POLL = {0x482b20, {0x83, 0xec, 0x24, 0xa1, 0x38, 0x69, 0x69, 0x00, 0x33, 0xc4}, 10}; // 181 bytes long
static const Site H2_PARSER = {0x4825f0, {0x56, 0x8b, 0xf1, 0x80, 0x3e, 0x04, 0x75, 0x5a, 0x0f, 0xb6, 0x4e, 0x01}, 12}; // eax device, ecx report
static const Site H2_STATUS = {0x441b30, {0x51, 0xa1, 0x94, 0xca, 0x74, 0x00}, 6};
static const Site H2_PRESSDET = {0x42d590, {0xa1, 0x18, 0xca, 0x6a, 0x00, 0x85, 0xc0}, 7};
static const Site H2_MOVIE_SOUND = {0x501b70, {0x74, 0x0b, 0x8d, 0x4c, 0x24, 0x0c}, 6};
static const unsigned H2_PRESENT = 0x74ca94, H2_CRITICAL = 0x74cc98, H2_FRAME_TS = 0x6aca18, H2_INPUT = 0x78b064;
static const unsigned H2_FORCE = 0x792c58; // the wheel's force (> 0 pushing right), its spring's stiffness before it (0..1.25)

static volatile unsigned g_h2oDevice;
static unsigned char g_h2oReport[12];

static int h2o_pressed(int i)
{
    return held(g_h2oInputs, i);
}

// The board's reports, called by our poll (the game's thread) before it hands them to the parser
static void __cdecl h2o_build(void)
{
    static int first;
    if (!first++)
        wl_log("rawthrills: %s: first board poll (device 0x%x)", g_title, g_h2oDevice);
    force(-read_float((unsigned)(size_t)at(H2_FORCE)));
    static double lastSpring = -1;
    double spring = read_float((unsigned)(size_t)at(H2_FORCE - 4)) / 1.25;
    spring = spring < 0 ? 0 : spring > 1 ? 1 : spring;
    if (spring != lastSpring)
    {
        lastSpring = spring;
        wl_ffb_set_effects(spring, 0, 0);
    }
    // this build swaps test and service, and the volume buttons, against the older games'
    unsigned char switches = 0, panel = 0, views = 0;
    if (h2o_pressed(H2_COIN))
        switches |= 0x01;
    if (h2o_pressed(H2_START) || h2o_pressed(H2_BOOST))
        switches |= 0x02;
    if (h2o_pressed(H2_TEST))
        switches |= 0x10;
    if (h2o_pressed(H2_SERVICE))
        switches |= 0x20;
    if (h2o_pressed(H2_VOLUP))
        panel |= 0x08;
    if (h2o_pressed(H2_VOLDOWN))
        panel |= 0x04;
    if (h2o_pressed(H2_VIEW))
        views |= 0x01;
    if (h2o_pressed(H2_VIEW2))
        views |= 0x02;
    if (h2o_pressed(H2_VIEW3))
        views |= 0x04;
    // one throttle lever: neutral at rest in the middle, forward with the gas, back with the brake
    int gas = wl_input_get(&g_h2oInputs[H2_GAS]), brake = wl_input_get(&g_h2oInputs[H2_BRAKE]);
    int lever = 128 + gas * 127 / 255 - (brake > 1 ? brake : 0) * 128 / 255;
    lever = lever < 0 ? 0 : lever > 255 ? 255 : lever;
    unsigned char r[12] = {4, 0x80, (unsigned char)~switches, (unsigned char)~panel,
                           4, 0xA0, (unsigned char)lever, (unsigned char)(brake < 1 ? 1 : brake),
                           // the view buttons active low, the gear switches (high nibble) released
                           4, (unsigned char)(0xC0 | keypad_key()), (unsigned char)wl_input_get(&g_h2oInputs[H2_STEERING]), (unsigned char)(~views & 0x0f)};
    memcpy(g_h2oReport, r, 12);
}

// The board's Open (thiscall, ecx the device): the device kept, the board marked open (+0xF0); the
// game's I/O thread not started
static unsigned __fastcall h2o_open(unsigned char *device, void *)
{
    g_h2oDevice = (unsigned)(size_t)device;
    wl_log("rawthrills: %s: board opened, device 0x%x", g_title, g_h2oDevice);
    *(volatile unsigned *)(device + 0xf0) = 1;
    return 1;
}

// GetStatusBlock (thiscall, the copy's destination on the stack, ret 4): the only board read the
// game's per-frame update makes once the board is present. Our poll, then the original copy (15
// dwords and a word from +0xF4, under the board's critical section while the board is present)
static unsigned __fastcall h2o_status_block(unsigned char *device, void *, unsigned char *out)
{
    if (!out)
        return 0;
    ((void(__cdecl *)(void))at(H2_POLL.va))();
    unsigned present = *(volatile unsigned *)at(H2_PRESENT);
    CRITICAL_SECTION *cs = (CRITICAL_SECTION *)(size_t) * (volatile unsigned *)at(H2_CRITICAL);
    if (present && cs)
        EnterCriticalSection(cs);
    memcpy(out, device + 0xf4, 62);
    if (present && cs)
        LeaveCriticalSection(cs);
    return 1;
}

// The press detector: every "service / test pressed" check reads a press edge in the event
// object's ring, which the game's keyboard poller writes (off in this build). Our trampoline calls
// this first: the edges of our service and test written there, the object's +0x1C gate opened
static int g_pressLast[2];

static void __cdecl h2o_press_det(unsigned char *obj)
{
    unsigned ts = *(volatile unsigned *)at(H2_FRAME_TS);
    if (!ts)
        return; // the detector stops there too
    unsigned gi = *(volatile unsigned *)at(H2_INPUT);
    unsigned o = (unsigned)(size_t)obj;
    // the input device's Service channel (+0x19CC) and Test channel (+0x17A4)
    int svc = gi ? o == gi + 0x19cc : o == 0x78b298, test = gi ? o == gi + 0x17a4 : o == 0x78b070;
    if (!svc && !test)
        return;
    if (!*(volatile unsigned *)(obj + 0x1c))
        *(volatile unsigned *)(obj + 0x1c) = 1;
    int k = svc ? 0 : 1;
    int down = h2o_pressed(svc ? H2_SERVICE : H2_TEST);
    if (down == g_pressLast[k])
        return;
    // an edge: the ring's cursor moved (wrapping at 0x40), {ts, state} written, as the poller does
    unsigned cur = *(volatile unsigned *)(obj + 0x220);
    unsigned next = cur + 1 == 0x40 ? 0 : cur + 1;
    *(volatile unsigned *)(obj + 0x220) = next;
    *(volatile unsigned *)(obj + 0x24 + next * 8) = (unsigned)down;
    *(volatile unsigned *)(obj + 0x20 + next * 8) = ts;
    g_pressLast[k] = down;
}

// GetAsyncKeyState: the keys the game reads for service, test and the volume answered from our
// inputs (D / Insert service, E / Delete test, Page Up / Down the volume), for the game's calls
static SHORT(WINAPI *oGetAsyncKeyState)(int);

static SHORT WINAPI h2o_get_async_key_state(int vk)
{
    if (!wl_is_system_caller(WL_CALLER))
    {
        int i = vk == 0x24 || vk == 0x2d ? H2_SERVICE : vk == 0x23 || vk == 0x2e ? H2_TEST : vk == 0x21 ? H2_VOLUP : vk == 0x22 ? H2_VOLDOWN : -1;
        if (i >= 0)
            return h2o_pressed(i) ? (SHORT)0x8000 : 0;
    }
    return oGetAsyncKeyState(vk);
}

// The board's PortTalk driver (\\.\PortTalk, absent): the game's boot needs it open, its controls
// (0x9C402414, results not looked at) succeed
static BOOL porttalk_ioctl(DWORD code, const void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned)
{
    if (out && outSize)
        memset(out, 0, outSize);
    if (returned)
        *returned = outSize;
    return TRUE;
}

static const WlVirtualDevice g_portTalk = {"PortTalk", porttalk_ioctl, NULL, NULL, NULL, NULL};

static void call_rel(unsigned char *code, int *n, unsigned from, const void *to)
{
    code[(*n)++] = 0xE8;
    int rel = (int)((unsigned)(size_t)to - (from + *n + 4));
    memcpy(code + *n, &rel, 4);
    *n += 4;
}

static int install_h2o(void)
{
    const Site *sites[] = {&H2_OPEN, &H2_POLL, &H2_PARSER, &H2_STATUS, &H2_PRESSDET, &H2_MOVIE_SOUND};
    if (!sites_ok(sites, 6))
        return 0;
    // the poll replaced in place (53 bytes of its 181): our reports, each handed to the parser
    // (eax the device, ecx the report), 0 returned (the board works)
    unsigned char code[80];
    int n = 0;
    unsigned poll = (unsigned)(size_t)at(H2_POLL.va);
    call_rel(code, &n, poll, (void *)h2o_build);
    for (int i = 0; i < 3; i++)
    {
        code[n++] = 0xA1; // mov eax, [g_h2oDevice]
        unsigned dev = (unsigned)(size_t)&g_h2oDevice;
        memcpy(code + n, &dev, 4);
        n += 4;
        code[n++] = 0xB9; // mov ecx, report
        unsigned rep = (unsigned)(size_t)(g_h2oReport + i * 4);
        memcpy(code + n, &rep, 4);
        n += 4;
        call_rel(code, &n, poll, at(H2_PARSER.va));
    }
    code[n++] = 0x33, code[n++] = 0xC0, code[n++] = 0xC3;
    int pollLen = n; // 53
    // the poll's free tail: the press detector's trampoline (the event object handed to
    // h2o_press_det, then the detector's first instruction and the rest of it)
    unsigned tramp = poll + pollLen;
    n = 0;
    unsigned char t[32];
    t[n++] = 0xFF, t[n++] = 0x74, t[n++] = 0x24, t[n++] = 0x04; // push dword [esp + 4]
    call_rel(t, &n, tramp, (void *)h2o_press_det);
    t[n++] = 0x83, t[n++] = 0xC4, t[n++] = 0x04;                                           // add esp, 4
    t[n++] = 0xA1, memcpy(t + n, &H2_FRAME_TS, 4), n += 4;                                  // mov eax, [frame ts]
    unsigned resume = (unsigned)(size_t)at(H2_PRESSDET.va) + 5;
    t[n++] = 0xE9;
    int rel = (int)(resume - (tramp + n + 4));
    memcpy(t + n, &rel, 4);
    n += 4;
    unsigned char jmp[5] = {0xE9};
    rel = (int)(tramp - ((unsigned)(size_t)at(H2_PRESSDET.va) + 5));
    memcpy(jmp + 1, &rel, 4);
    redirect(H2_OPEN.va, (void *)h2o_open, "JAMMA USB board's Open (answered)");
    put(H2_POLL.va, code, pollLen, "JAMMA USB board poll (our reports)");
    put(H2_POLL.va + pollLen, t, n, "press detector's trampoline (service, test)");
    put(H2_PRESSDET.va, jmp, 5, "press detector (service, test)");
    redirect(H2_STATUS.va, (void *)h2o_status_block, "JAMMA USB board status block");
    // the movies: Bink opens its own DirectSound, not FMOD's output handed to it
    static const unsigned char jmpShort = 0xEB;
    put(H2_MOVIE_SOUND.va, &jmpShort, 1, "movies' sound (Bink's own)");
    wl_vdev_register(&g_portTalk);
    void *gaks = (void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetAsyncKeyState");
    if (gaks)
        wl_hook_code(gaks, (void *)h2o_get_async_key_state, (void **)&oGetAsyncKeyState, "GetAsyncKeyState (H2Overdrive's service, test, volume)");
    wl_inputs_set_table(g_h2oInputs, H2_COUNT);
    wl_game_set_boards("Controls on Raw Thrills JAMMA USB board (its reports given to the game's board object)",
                       "Motor board: the wheel's force and spring read from the game");
    return 1;
}

// ==============================================================================
// SetControl: The Fast and the Furious SuperCars
// ==============================================================================

enum
{
    SC_STEERING,
    SC_GAS,
    SC_BRAKE,
    SC_COIN,
    SC_START,
    SC_SERVICE,
    SC_TEST,
    SC_BUTTON1, // view
    SC_BUTTON2,
    SC_MUSIC,
    SC_BUTTON4,
    SC_BRAKESWITCH, // the digital brake
    SC_GEARUP,
    SC_GEARDOWN,
    SC_VOLUP,
    SC_VOLDOWN,
    SC_COUNT
};

static WlInput g_scInputs[SC_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 4095, 2048},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 1, 4095, 1},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 1, 4095, 1},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"View", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"Button2", "Nitro", WL_INPUT_SWITCH, 0, 1, 0},
    {"Music", "MusicUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"Button4", "Action", WL_INPUT_SWITCH, 0, 1, 0},
    {"Handbrake", "Handbrake", WL_INPUT_SWITCH, 0, 1, 0},
    {"GearUp", "GearUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"GearDown", "GearDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"VolumeUp", "VolumeUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"VolumeDown", "VolumeDown", WL_INPUT_SWITCH, 0, 1, 0},
};

static const Site SC_SET_CONTROL = {0x4a9510, {0x8b, 0x44, 0x24, 0x04, 0xa9, 0x00, 0x00, 0x01, 0x00}, 9};
static const Site SC_INPUT_INIT = {0x4741db, {0xe8, 0xf0, 0x59, 0x03, 0x00}, 5};
static const Site SC_BOARD_ON = {0x401ab0, {0xa1, 0xb0, 0x0a, 0x7d, 0x04, 0xa8, 0x03}, 7};
static const Site SC_BOARD_SEND = {0x4c27f0, {0x81, 0xec, 0x08, 0x04, 0x00, 0x00}, 6};
static const Site SC_BASE_PATH = {0x4a7ee0, {0x8b, 0x44, 0x24, 0x04, 0x85, 0xc0, 0x57}, 7};
static const Site SC_FFB = {0x402cf0, {0x83, 0xec, 0x08, 0x83, 0x3d, 0xc4, 0x0a, 0x7d, 0x04, 0x00}, 10};

typedef unsigned(__cdecl *SetControlFn)(unsigned code, unsigned value);

// The force board's force (cdecl, -1..1, > 0 pushing left)
static void __cdecl sc_force(float f)
{
    force(f);
}

static DWORD WINAPI sc_thread(void *)
{
    SetControlFn set = (SetControlFn)at(SC_SET_CONTROL.va);
    // switch codes 0x10000 + n of each input
    static const struct
    {
        int input;
        unsigned code;
    } switches[] = {{SC_START, 0x10000}, {SC_BUTTON1, 0x10001}, {SC_BUTTON2, 0x10002}, {SC_MUSIC, 0x10003}, {SC_BUTTON4, 0x10004},
                    {SC_COIN, 0x10009}, {SC_TEST, 0x1000b}, {SC_SERVICE, 0x1000c}, {SC_VOLUP, 0x1000e}, {SC_VOLDOWN, 0x1000f},
                    {SC_BRAKESWITCH, 0x10010}};
    static const unsigned keypadCodes[12] = {0x10081, 0x10082, 0x10083, 0x10084, 0x10085, 0x10086, 0x10087, 0x10088, 0x10089, 0x1008a, 0x10080, 0x1008b};
    unsigned sent = 0xFFFFFFFF; // every switch sent the first time
    unsigned sentKey = 0;
    int gear = 0, sentGear = -1, upHeld = 0, downHeld = 0;
    for (;;)
    {
        unsigned now = 0;
        for (int i = 0; i < (int)(sizeof switches / sizeof switches[0]); i++)
            if (held(g_scInputs, switches[i].input))
                now |= 1u << (switches[i].code - 0x10000);
        if (wl_input_get(&g_scInputs[SC_BRAKE]) > 4095 / 3)
            now |= 1u << 0x10;
        for (int i = 0; i < (int)(sizeof switches / sizeof switches[0]); i++)
        {
            unsigned bit = 1u << (switches[i].code - 0x10000);
            if (sent == 0xFFFFFFFF || ((now ^ sent) & bit))
                set(switches[i].code, (now & bit) != 0);
        }
        sent = now;
        // the 4 position lever, moved by the shifts
        int up = held(g_scInputs, SC_GEARUP), down = held(g_scInputs, SC_GEARDOWN);
        if (up && !upHeld && gear < 3)
            gear++;
        if (down && !downHeld && gear > 0)
            gear--;
        upHeld = up, downHeld = down;
        if (gear != sentGear)
        {
            for (int i = 0; i < 4; i++)
                set(0x10005 + i, i == gear);
            sentGear = gear;
        }
        unsigned char k = keypad_key();
        unsigned key = k == KEYPAD_NONE ? 0 : keypadCodes[k];
        if (key != sentKey)
        {
            if (sentKey)
                set(sentKey, 0);
            if (key)
                set(key, 1);
            sentKey = key;
        }
        set(0x10042, 4095); // the full scale, set by the input init removed
        set(0x10020, (unsigned)wl_input_get(&g_scInputs[SC_STEERING]));
        set(0x10021, (unsigned)wl_input_get(&g_scInputs[SC_GAS]));
        set(0x10022, (unsigned)wl_input_get(&g_scInputs[SC_BRAKE]));
        Sleep(4);
    }
}

static int install_supercars(void)
{
    const Site *sites[] = {&SC_SET_CONTROL, &SC_INPUT_INIT, &SC_BOARD_ON, &SC_BOARD_SEND, &SC_BASE_PATH, &SC_FFB};
    if (!sites_ok(sites, 6))
        return 0;
    put(SC_INPUT_INIT.va, NOPS, 5, "input init (DirectInput, the USB board: not called)");
    put(SC_BOARD_ON.va, RET1, 6, "USB board present");
    static const unsigned char ret = 0xC3;
    put(SC_BOARD_SEND.va, &ret, 1, "USB board's messages (dropped)");
    put(SC_BASE_PATH.va, RET0, 3, "files' base path (left empty)");
    redirect(SC_FFB.va, (void *)sc_force, "force board's force");
    CloseHandle(CreateThread(NULL, 0, sc_thread, NULL, 0, NULL));
    wl_inputs_set_table(g_scInputs, SC_COUNT);
    wl_game_set_boards("Controls on Raw Thrills H2O USB board (the game's SetControl called)",
                       "Motor board: the wheel's motor by the parallel port (its force played)");
    return 1;
}

// ==============================================================================
// SetControl without a board: Winter X Games SnoCross (2022 build 1.58)
// ==============================================================================
//
// The game's own path without a board ("No I/O Board found"), its input init removed and its
// SetControl fed as SuperCars'. The dump's protection traps undone, its board missing reboot and
// displays removed. The handlebar's force the race computes read every poll.

enum
{
    SX_HANDLEBAR,
    SX_THROTTLE,
    SX_BRAKE,
    SX_COIN,
    SX_START,
    SX_SERVICE,
    SX_TEST,
    SX_VIEW,     // TUNES, 0x10004
    SX_SNOWBALL, // button 1, 0x10001
    SX_NITRO,    // START
    SX_BILL,
    SX_VOLUP,
    SX_VOLDOWN,
    SX_COUNT
};

static WlInput g_sxInputs[SX_COUNT] = {
    {"Handlebar", "P1_Steer", WL_INPUT_AXIS, 0, 4095, 2048},
    {"Throttle", "P1_Gas", WL_INPUT_ANALOG, 1, 4095, 1},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 1, 4095, 1},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"View", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"Snowballs", "Action", WL_INPUT_SWITCH, 0, 1, 0},
    {"Nitro", "Nitro", WL_INPUT_SWITCH, 0, 1, 0},
    {"Bill", "Overtake", WL_INPUT_SWITCH, 0, 1, 0},
    {"VolumeUp", "VolumeUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"VolumeDown", "VolumeDown", WL_INPUT_SWITCH, 0, 1, 0},
};

static const Site SX_SET_CONTROL = {0x4cad20, {0x8b, 0x44, 0x24, 0x04, 0xa9, 0x00, 0x00, 0x01, 0x00}, 9};
static const Site SX_INPUT_INIT = {0x4157af, {0xe8, 0x7c, 0x5c, 0x0b, 0x00}, 5};
static const Site SX_BASE_PATH = {0x4befc0, {0x8b, 0x44, 0x24, 0x04, 0x85, 0xc0, 0x57}, 7};
static const unsigned SX_FORCE = 0x99ab6c; // the handlebar's force (0x46d170), -1.2401..1.2401, > 0 left
static const float SX_FORCE_MAX = 1.2401f;

static const Fix SX_FIXES[] = {
    // the protection's traps: the C runtime's heap check made fatal, a jmp $ once a counter
    // reaches 2, a feature check (made 1)
    {{0x76df3d, {0x90, 0x90}, 2}, {0x75, 0x0d}, 2},
    {{0x470de0, {0xeb, 0xfe}, 2}, {0x90, 0x90}, 2},
    {{0x7783ad, {0x83, 0xe0, 0x01}, 3}, {0x31, 0xc0, 0x40}, 3},
    // the cabinet mode (USB restarts, reboots on board errors) not set, a graphics crash skipped
    {{0x46c7b0, {0x83, 0x0d, 0xa0, 0x90, 0x9b, 0x00, 0x01}, 7}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, 7},
    {{0x4c7fdb, {0x74, 0x25}, 2}, {0x90, 0x90}, 2},
    {{0x4c7fe2, {0x74}, 1}, {0xeb}, 1},
    // no board: the I/O init taken as a success, the reboot made its final idle loop, the "I/O
    // BOARD MISSING" displays and the force board's init returning at once
    {{0x471499, {0x0f, 0x85, 0x99, 0x00, 0x00, 0x00}, 6}, {0xe9, 0x9a, 0x00, 0x00, 0x00, 0x90}, 6},
    {{0x46f6d0, {0x83, 0xec, 0x14, 0x56, 0x6a, 0x03}, 6}, {0xe9, 0xcb, 0x00, 0x00, 0x00, 0x90}, 6},
    {{0x46d020, {0x51}, 1}, {0xc3}, 1},
    {{0x46caf0, {0x83, 0xec, 0x08}, 3}, {0xc3, 0x90, 0x90}, 3},
    {{0x46ffe0, {0xd9, 0xee}, 2}, {0xc3, 0x90}, 2},
    // the PC keyboard's developer keys: Space ended the game (made Escape), the Escape box not shown
    {{0x477327, {0x6a, 0x39}, 2}, {0x6a, 0x01}, 2},
    {{0x476999, {0xe8, 0x82, 0xe3, 0xff, 0xff}, 5}, {0x90, 0x90, 0x90, 0x90, 0x90}, 5},
};

static DWORD WINAPI sx_thread(void *)
{
    SetControlFn set = (SetControlFn)at(SX_SET_CONTROL.va);
    static const struct
    {
        int input;
        unsigned code;
    } switches[] = {{SX_START, 0x10000}, {SX_NITRO, 0x10000}, {SX_SNOWBALL, 0x10001}, {SX_VIEW, 0x10004},
                    {SX_COIN, 0x10009}, {SX_TEST, 0x1000b}, {SX_SERVICE, 0x1000c}, {SX_BILL, 0x1000d},
                    {SX_VOLUP, 0x1000e}, {SX_VOLDOWN, 0x1000f}};
    static const unsigned keypadCodes[12] = {0x10081, 0x10082, 0x10083, 0x10084, 0x10085, 0x10086, 0x10087, 0x10088, 0x10089, 0x1008a, 0x10080, 0x1008b};
    unsigned forceVa = (unsigned)(size_t)at(SX_FORCE);
    unsigned sent = 0, used = 0, sentKey = 0;
    int first = 1;
    for (int i = 0; i < (int)(sizeof switches / sizeof switches[0]); i++)
        used |= 1u << (switches[i].code - 0x10000);
    for (;;)
    {
        force(read_float(forceVa) / SX_FORCE_MAX);
        unsigned now = 0;
        for (int i = 0; i < (int)(sizeof switches / sizeof switches[0]); i++)
            if (held(g_sxInputs, switches[i].input))
                now |= 1u << (switches[i].code - 0x10000);
        for (unsigned n = 0; n < 0x10; n++)
        {
            unsigned bit = 1u << n;
            if ((used & bit) && (first || ((now ^ sent) & bit)))
                set(0x10000 + n, (now & bit) != 0);
        }
        sent = now, first = 0;
        unsigned char k = keypad_key();
        unsigned key = k == KEYPAD_NONE ? 0 : keypadCodes[k];
        if (key != sentKey)
        {
            if (sentKey)
                set(sentKey, 0);
            if (key)
                set(key, 1);
            sentKey = key;
        }
        set(0x10020, (unsigned)wl_input_get(&g_sxInputs[SX_HANDLEBAR]));
        set(0x10021, (unsigned)wl_input_get(&g_sxInputs[SX_THROTTLE]));
        set(0x10022, (unsigned)wl_input_get(&g_sxInputs[SX_BRAKE]));
        Sleep(4);
    }
}

static int install_snocross(void)
{
    const Site *sites[] = {&SX_SET_CONTROL, &SX_INPUT_INIT, &SX_BASE_PATH};
    if (!sites_ok(sites, 3) || !apply_fixes(SX_FIXES, (int)(sizeof SX_FIXES / sizeof SX_FIXES[0]), "fix (protection trap, no board)"))
        return 0;
    put(SX_INPUT_INIT.va, NOPS, 5, "input init (DirectInput, the USB board: not called)");
    put(SX_BASE_PATH.va, RET0, 3, "files' base path (left empty)");
    CloseHandle(CreateThread(NULL, 0, sx_thread, NULL, 0, NULL));
    wl_inputs_set_table(g_sxInputs, SX_COUNT);
    wl_game_set_boards("Controls on Raw Thrills H2O USB board (the game's SetControl called, no board found)",
                       "Motor board: the handlebar's force read from the game");
    return 1;
}

// ==============================================================================
// Super Bikes 2: the board's inputs in the game's keyboard and analog tables
// ==============================================================================

enum
{
    SB_STEERING,
    SB_GAS,
    SB_BRAKE,
    SB_COIN,
    SB_START,
    SB_SERVICE,
    SB_TEST,
    SB_VIEW,
    SB_TUNES,
    SB_SHIFT,
    SB_SHIFTDOWN,
    SB_BRAKESWITCH,
    SB_VOLUP,
    SB_VOLDOWN,
    SB_COUNT
};

static WlInput g_sb2Inputs[SB_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 255, 128},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 1, 255, 1},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 1, 255, 1},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"View", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"Tunes", "MusicUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"GearUp", "GearUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"GearDown", "GearDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"Handbrake", "Handbrake", WL_INPUT_SWITCH, 0, 1, 0},
    {"VolumeUp", "VolumeUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"VolumeDown", "VolumeDown", WL_INPUT_SWITCH, 0, 1, 0},
};

static const unsigned SB2_KEYS = 0x8901438;   // a byte per code & 0xFF: bit 0 held, bit 1 pressed once (cleared by its read)
static const unsigned SB2_ANALOG = 0x8901548; // a dword per code & 0xFF
static const Site SB2_INPUT_POLL = {0x4bb6b0, {0x51, 0xe8, 0x7a, 0x5a, 0xf4, 0xff, 0x85, 0xc0}, 8};

static const Fix SB2_FIXES[] = {
    // the input init (DirectInput's keyboard and mouse, the board): removed
    {{0x4126bf, {0xe8, 0x5c, 0x9b, 0x0a, 0x00}, 5}, {0x90, 0x90, 0x90, 0x90, 0x90}, 5},
    // the files' base path left empty
    {{0x4aa8a0, {0x8b, 0x44, 0x24, 0x04, 0x85, 0xc0, 0x57}, 7}, {0x33, 0xc0, 0xc3}, 3},
    // the back buffer's format kept (R5G6B5 crashed)
    {{0x4b7bd2, {0x74, 0x0f}, 2}, {0xeb, 0x0f}, 2},
    // the developer key Space ends the race: an unmapped key instead
    {{0x471a81, {0x6a, 0x39}, 2}, {0x6a, 0x7f}, 2},
    // no error box on close, no Watson, no Escape box
    {{0x411c02, {0x74, 0x12}, 2}, {0xeb, 0x12}, 2},
    {{0x529e62, {0x75, 0x1d}, 2}, {0x90, 0x90}, 2},
    {{0x4714da, {0xe8, 0x81, 0xcd, 0xff, 0xff}, 5}, {0x90, 0x90, 0x90, 0x90, 0x90}, 5},
};

static unsigned char g_sb2Held[256];

// The game's input poll (its main loop and its test menus): our inputs written in its tables, on
// the game's thread, before the game reads them (its press flags then cleared without a race)
static void __cdecl sb2_poll(void)
{
    static const struct
    {
        int input;
        unsigned char key;
    } keys[] = {{SB_START, 0x00}, {SB_START, 0x39}, // START also the developer Space, to leave the attract mode
                {SB_VIEW, 0x01},  {SB_TUNES, 0x04},   {SB_SHIFT, 0x05}, {SB_SHIFTDOWN, 0xd0}, {SB_COIN, 0x09},  {SB_TEST, 0x0b},
                {SB_SERVICE, 0x0c}, {SB_VOLUP, 0x0e}, {SB_VOLDOWN, 0x0f}, {SB_BRAKESWITCH, 0x10}};
    static const unsigned char keypad[12] = {0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x80, 0x8b};
    unsigned char down[256] = {0};
    for (int i = 0; i < (int)(sizeof keys / sizeof keys[0]); i++)
        if (held(g_sb2Inputs, keys[i].input))
            down[keys[i].key] = 1;
    int brake = wl_input_get(&g_sb2Inputs[SB_BRAKE]);
    if (brake > 255 / 3)
        down[0x10] = 1;
    unsigned char k = keypad_key();
    if (k != KEYPAD_NONE)
        down[keypad[k]] = 1;
    volatile unsigned char *table = at(SB2_KEYS);
    for (int i = 0; i < 256; i++)
        if (down[i] || g_sb2Held[i])
        {
            unsigned char v = table[i];
            v = down[i] ? (unsigned char)(v | 1 | (g_sb2Held[i] ? 0 : 2)) : (unsigned char)(v & ~1);
            table[i] = v;
            g_sb2Held[i] = down[i];
        }
    volatile unsigned *analog = (volatile unsigned *)at(SB2_ANALOG);
    analog[0x42] = 255; // the full scale, set by the input init removed
    analog[0x20] = (unsigned)wl_input_get(&g_sb2Inputs[SB_STEERING]);
    analog[0x21] = (unsigned)wl_input_get(&g_sb2Inputs[SB_GAS]);
    analog[0x22] = (unsigned)brake;
}

static int install_superbikes2(void)
{
    const Site *sites[] = {&SB2_INPUT_POLL};
    if (!sites_ok(sites, 1) || !apply_fixes(SB2_FIXES, (int)(sizeof SB2_FIXES / sizeof SB2_FIXES[0]), "fix"))
        return 0;
    redirect(SB2_INPUT_POLL.va, (void *)sb2_poll, "input poll (the board's inputs in its tables)");
    wl_inputs_set_table(g_sb2Inputs, SB_COUNT);
    wl_game_set_boards("Controls on Raw Thrills H2O USB board (written in the game's input tables)", NULL);
    return 1;
}

// ==============================================================================
// Dirty Drivin': its developer keyboard driven
// ==============================================================================

enum
{
    DD_STEERING,
    DD_GAS,
    DD_BRAKE,
    DD_COIN,
    DD_START,
    DD_SERVICE,
    DD_TEST,
    DD_VIEW,
    DD_CRANK,
    DD_VOLUP,
    DD_VOLDOWN,
    DD_COUNT
};

static WlInput g_ddInputs[DD_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, -1000, 1000, 0},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 1000, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 1000, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"View", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"Crank", "Nitro", WL_INPUT_SWITCH, 0, 1, 0},
    {"VolumeUp", "VolumeUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"VolumeDown", "VolumeDown", WL_INPUT_SWITCH, 0, 1, 0},
};

static const Fix DD_FIXES[] = {
    // the cabinet hardware's init result not looked at
    {{0x403b00, {0x75, 0x3b}, 2}, {0xeb, 0x3b}, 2},
    {{0x465447, {0x8b, 0x54, 0x24, 0x10}, 4}, {0xeb, 0x0b, 0x90, 0x90}, 4},
    // no endless wait on a missing device (jmp $)
    {{0x4bc9e8, {0x75, 0x08}, 2}, {0xeb, 0x08}, 2},
    {{0x5b5bf5, {0x74, 0x1c}, 2}, {0x90, 0x90}, 2},
    {{0x639b5c, {0x75, 0x2a}, 2}, {0xeb, 0x2a}, 2},
    {{0x6f236e, {0x8b, 0x46, 0x10}, 3}, {0x90, 0x90, 0x90}, 3},
    {{0x6fb88f, {0x75, 0x07}, 2}, {0x90, 0x90}, 2},
    // the developer keyboard's throttle and steering ramps: the floats are ours
    {{0x4514bb, {0xd9, 0x44, 0x24, 0x08}, 4}, {0x90, 0x90, 0x90, 0x90}, 4},
    {{0x4514c0, {0xd9, 0x5f, 0x48}, 3}, {0x90, 0x90, 0x90}, 3},
    {{0x45159a, {0xd9, 0x04, 0x24, 0xd9, 0x59, 0x4c}, 6}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, 6},
    {{0x4515ac, {0xd9, 0x04, 0x24, 0xd9, 0x59, 0x4c}, 6}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, 6},
};

static const unsigned DD_THROTTLE = 0x8ad0f8, DD_STEER = 0x8ad0fc; // the developer keyboard's floats
static const unsigned DD_COIN_CALLBACK = 0x41eef0;                          // the coin board's callback, cdecl(0 coin 1, 1 coin 2, 2 bill)
static const unsigned DD_LATCHES = 0x98c490;                        // test, service, volume up, volume down
static const unsigned DD_FORCE = 0x886ec4, DD_TRACK = 0x96f770;     // the race's wheel force, the track (8-14 mirrored)

static volatile LONG g_ddKeyState; // bit 0 Space (START, the crank), bit 1 V (VIEW)

// The game's keys: Space and V from our inputs, every other key up (the PC's keyboard no longer
// drives the game), for the game's calls
static SHORT WINAPI dd_get_async_key_state(int vk)
{
    if (!wl_is_system_caller(WL_CALLER))
    {
        if (vk == VK_SPACE)
            return g_ddKeyState & 1 ? (SHORT)0x8000 : 0;
        if (vk == 'V')
            return g_ddKeyState & 2 ? (SHORT)0x8000 : 0;
        return 0;
    }
    return oGetAsyncKeyState(vk);
}

static DWORD WINAPI dd_thread(void *)
{
    void(__cdecl * coin)(unsigned) = (void(__cdecl *)(unsigned))at(DD_COIN_CALLBACK);
    int coinHeld = 0;
    for (;;)
    {
        g_ddKeyState = (held(g_ddInputs, DD_START) || held(g_ddInputs, DD_CRANK) ? 1 : 0) | (held(g_ddInputs, DD_VIEW) ? 2 : 0);
        int c = held(g_ddInputs, DD_COIN);
        if (c && !coinHeld)
            coin(0);
        coinHeld = c;
        static const int latches[4] = {DD_TEST, DD_SERVICE, DD_VOLUP, DD_VOLDOWN};
        for (int i = 0; i < 4; i++)
            if (held(g_ddInputs, latches[i]))
                *(volatile unsigned *)at(DD_LATCHES + i * 4) = 1;
        *(volatile float *)at(DD_STEER) = wl_input_get(&g_ddInputs[DD_STEERING]) / 1000.0f; // -1 left
        *(volatile float *)at(DD_THROTTLE) = (wl_input_get(&g_ddInputs[DD_GAS]) - wl_input_get(&g_ddInputs[DD_BRAKE])) / 1000.0f;
        float f = *(volatile float *)at(DD_FORCE);
        unsigned char track = *(volatile unsigned char *)at(DD_TRACK);
        force(track >= 8 && track <= 14 ? f : -f);
        Sleep(4);
    }
}

static int install_dirtydrivin(void)
{
    if (!apply_fixes(DD_FIXES, (int)(sizeof DD_FIXES / sizeof DD_FIXES[0]), "fix"))
        return 0;
    // Its device's size comes from the screen's height (16:9 of it): on a low screen (1024x384: 680x384)
    // its loading stays on "Loading world". A 1360x768 screen told to it (VIRTUAL_DISPLAY), as
    // windowsarcadeloader's -r 1360x768
    wl_game_use_options("dirtydrivin");
    wl_vscreen_minimum(1360, 768);
    if (!strstr(GetCommandLineA(), "-Windowed"))
        wl_log("rawthrills: Dirty Drivin' started without -WinPC -Windowed -r 1360x768 (its arguments in the game list)");
    void *gaks = (void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetAsyncKeyState");
    if (gaks)
        wl_hook_code(gaks, (void *)dd_get_async_key_state, (void **)&oGetAsyncKeyState, "GetAsyncKeyState (Dirty Drivin's START, VIEW)");
    CloseHandle(CreateThread(NULL, 0, dd_thread, NULL, 0, NULL));
    wl_inputs_set_table(g_ddInputs, DD_COUNT);
    wl_game_set_boards("Controls: the game's developer keyboard and coin board, from windowsloader's inputs",
                       "Motor board: the race's wheel force read from the game");
    return 1;
}

// ==============================================================================
// The RIO board behind libusb0: Cars, Batman
// ==============================================================================

enum
{
    RIO_STEERING,
    RIO_GAS,
    RIO_BRAKE,
    RIO_COIN,
    RIO_START,
    RIO_SERVICE,
    RIO_TEST,
    RIO_VOLUP,
    RIO_VOLDOWN,
    RIO_B1, // Cars VIEW, Batman FIRE
    RIO_B2, // Cars JUMP, Batman VIEW
    RIO_B3, // Cars FLIP, Batman the brake button
    RIO_B4, // Cars CHATTER
    RIO_COUNT
};

static WlInput g_carsInputs[RIO_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 4095, 2048},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 4095, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 4095, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"VolumeUp", "VolumeUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"VolumeDown", "VolumeDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"View", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"Jump", "Action", WL_INPUT_SWITCH, 0, 1, 0},
    {"Flip", "Nitro", WL_INPUT_SWITCH, 0, 1, 0},
    {"Chatter", "Overtake", WL_INPUT_SWITCH, 0, 1, 0},
};

static WlInput g_batmanInputs[RIO_COUNT - 1] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 4095, 2048},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 4095, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 4095, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"VolumeUp", "VolumeUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"VolumeDown", "VolumeDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"Fire", "Action", WL_INPUT_SWITCH, 0, 1, 0},
    {"View", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"Handbrake", "Handbrake", WL_INPUT_SWITCH, 0, 1, 0},
};

static WlInput *g_rio;
static int g_rioBatman;
static int g_rioSwitch[RIO_COUNT]; // each input's switch number on the board, -1 none
static unsigned char g_rioCounters[0x13];
static int g_rioHeld[0x13];
static float (*g_rioForce)(void);

// (switch, byte of the packet after its 3-byte header, high nibble)
static const struct
{
    unsigned char id;
    unsigned char byte;
    unsigned char high;
} RIO_NIBBLES[19] = {{0x00, 0, 1}, {0x01, 0, 0}, {0x02, 1, 1}, {0x12, 1, 0}, {0x04, 2, 1}, {0x03, 2, 0}, {0x05, 3, 1},
                     {0x06, 3, 0}, {0x07, 5, 1}, {0x08, 5, 0}, {0x09, 6, 1}, {0x0a, 6, 0}, {0x0b, 7, 1}, {0x0c, 7, 0},
                     {0x11, 8, 1}, {0x0d, 9, 1}, {0x0e, 9, 0}, {0x0f, 10, 1}, {0x10, 10, 0}};

// libusb-win32's usb_bus and usb_device, the fields the game reads
struct UsbDevice;
struct UsbBus
{
    UsbBus *next, *prev;
    char dirname[512];
    UsbDevice *devices;
    unsigned location;
    UsbDevice *root_dev;
};
struct UsbDevice
{
    UsbDevice *next, *prev;
    char filename[512];
    UsbBus *bus;
    unsigned char descriptor[20]; // usb_device_descriptor (18 bytes, packed)
    void *config;
    void *dev;
    unsigned char devnum, num_children;
    void *children;
};

static UsbBus g_usbBus;
static UsbDevice g_usbDevice;

static int __cdecl husb_init(void)
{
    return 0;
}
static int __cdecl husb_find_busses(void)
{
    return 1;
}
static int __cdecl husb_find_devices(void)
{
    return 1;
}
static UsbBus *__cdecl husb_get_busses(void)
{
    return &g_usbBus;
}
static void *__cdecl husb_open(void *)
{
    wl_log("rawthrills: %s: RIO board opened", g_title);
    return &g_usbBus;
}
static int __cdecl husb_close(void *)
{
    return 0;
}
static int __cdecl husb_set_configuration(void *, int)
{
    return 0;
}
static int __cdecl husb_claim_interface(void *, int)
{
    return 0;
}
static int __cdecl husb_release_interface(void *, int)
{
    return 0;
}
static int __cdecl husb_control_msg(void *, int, int, int, int, char *, int size, int)
{
    return size;
}

// The game's commands (its lamps, its handshake): taken
static int __cdecl husb_write(void *, int, char *, int size, int)
{
    return size;
}

// A type 2 packet: the switches' change counters, the analog values
static int __cdecl husb_read(void *, int, char *bytes, int size, int)
{
    Sleep(4); // the board reports continuously
    int down[0x13] = {0};
    for (int i = 0; i < RIO_COUNT - g_rioBatman; i++)
        if (g_rioSwitch[i] >= 0 && wl_input_get(&g_rio[i]))
            down[g_rioSwitch[i]] = 1;
    if (g_rioBatman && wl_input_get(&g_rio[RIO_BRAKE]) > 4095 / 3)
        down[0x08] = 1; // Batman's brake pedal is also its brake button
    for (int i = 0; i < 0x13; i++)
        if (down[i] != g_rioHeld[i])
        {
            g_rioCounters[i]++;
            g_rioHeld[i] = down[i];
        }
    force(g_rioForce());
    unsigned char packet[64] = {2};
    unsigned char *p = packet + 3;
    for (int i = 0; i < 19; i++)
    {
        unsigned char c = g_rioCounters[RIO_NIBBLES[i].id] & 0x0f;
        p[RIO_NIBBLES[i].byte] |= RIO_NIBBLES[i].high ? (unsigned char)(c << 4) : c;
    }
    unsigned short analogs[4] = {(unsigned short)wl_input_get(&g_rio[RIO_STEERING]), (unsigned short)wl_input_get(&g_rio[RIO_GAS]),
                                 (unsigned short)wl_input_get(&g_rio[RIO_BRAKE]), 0};
    for (int i = 0; i < 4; i++)
    {
        p[11 + i / 2] |= (unsigned char)(((analogs[i] >> 8) & 0x0f) << (i % 2 == 0 ? 4 : 0));
        p[13 + i] = (unsigned char)analogs[i];
    }
    if (g_rioBatman)
        p[17] = (unsigned char)((keypad_key() << 4) | 0x0f); // Batman reads two keypad nibbles after them (0xF released)
    int n = size < 0 ? 0 : size > 64 ? 64 : size;
    memcpy(bytes, packet, n);
    return n;
}

static void *ousb_init, *ousb_find_busses, *ousb_find_devices, *ousb_get_busses, *ousb_open, *ousb_close, *ousb_set_configuration,
    *ousb_claim_interface, *ousb_release_interface, *ousb_control_msg, *ousb_interrupt_write, *ousb_interrupt_read, *ousb_bulk_write,
    *ousb_bulk_read;

#define RIO_HOOK(name, detour) {L"libusb0.dll", #name, (void *)detour, &o##name, 0}
static WlHook g_carsUsb[] = {
    RIO_HOOK(usb_init, husb_init), RIO_HOOK(usb_find_busses, husb_find_busses), RIO_HOOK(usb_find_devices, husb_find_devices),
    RIO_HOOK(usb_get_busses, husb_get_busses), RIO_HOOK(usb_open, husb_open), RIO_HOOK(usb_close, husb_close),
    RIO_HOOK(usb_set_configuration, husb_set_configuration), RIO_HOOK(usb_claim_interface, husb_claim_interface),
    RIO_HOOK(usb_release_interface, husb_release_interface), RIO_HOOK(usb_control_msg, husb_control_msg),
    RIO_HOOK(usb_interrupt_write, husb_write), RIO_HOOK(usb_interrupt_read, husb_read)};
static WlHook g_batmanUsb[] = {
    RIO_HOOK(usb_init, husb_init), RIO_HOOK(usb_find_busses, husb_find_busses), RIO_HOOK(usb_find_devices, husb_find_devices),
    RIO_HOOK(usb_get_busses, husb_get_busses), RIO_HOOK(usb_open, husb_open), RIO_HOOK(usb_close, husb_close),
    RIO_HOOK(usb_set_configuration, husb_set_configuration), RIO_HOOK(usb_claim_interface, husb_claim_interface),
    RIO_HOOK(usb_release_interface, husb_release_interface), RIO_HOOK(usb_bulk_write, husb_write), RIO_HOOK(usb_bulk_read, husb_read)};

static void rio_install(WlInput *inputs, int count, int batman, float (*forceFn)(void))
{
    strcpy_s(g_usbBus.dirname, sizeof g_usbBus.dirname, "bus");
    g_usbBus.devices = &g_usbDevice;
    strcpy_s(g_usbDevice.filename, sizeof g_usbDevice.filename, "\\\\.\\rio");
    g_usbDevice.bus = &g_usbBus;
    g_usbDevice.descriptor[0] = 18;
    g_usbDevice.descriptor[1] = 1;
    g_usbDevice.descriptor[8] = 0x70, g_usbDevice.descriptor[9] = 0x0c;  // VID 0C70
    g_usbDevice.descriptor[10] = 0x80, g_usbDevice.descriptor[11] = 0x07; // PID 0780
    g_usbDevice.descriptor[17] = 1;
    g_usbDevice.devnum = 1;
    g_rio = inputs;
    g_rioBatman = batman;
    g_rioForce = forceFn;
    // the switches: 0 coin 1, 1 coin 2, 2 bill, 3 service, 4 test, 5 / 6 volume up / down; Cars 7
    // START, 8 VIEW, 9 flip, 0xA jump, 0xB chatter; Batman 7 fire, 8 the brake button, 9 START, 0xA VIEW
    for (int i = 0; i < RIO_COUNT; i++)
        g_rioSwitch[i] = -1;
    g_rioSwitch[RIO_COIN] = 0, g_rioSwitch[RIO_SERVICE] = 3, g_rioSwitch[RIO_TEST] = 4;
    g_rioSwitch[RIO_VOLUP] = 5, g_rioSwitch[RIO_VOLDOWN] = 6;
    if (batman)
        g_rioSwitch[RIO_B1] = 7, g_rioSwitch[RIO_B3] = 8, g_rioSwitch[RIO_START] = 9, g_rioSwitch[RIO_B2] = 10;
    else
        g_rioSwitch[RIO_START] = 7, g_rioSwitch[RIO_B1] = 8, g_rioSwitch[RIO_B3] = 9, g_rioSwitch[RIO_B2] = 10, g_rioSwitch[RIO_B4] = 11;
    if (batman)
        wl_hooks_add(g_batmanUsb, (int)(sizeof g_batmanUsb / sizeof g_batmanUsb[0]));
    else
        wl_hooks_add(g_carsUsb, (int)(sizeof g_carsUsb / sizeof g_carsUsb[0]));
    wl_inputs_set_table(inputs, count);
    wl_game_set_boards("Controls on Raw Thrills RIO board (USB, behind libusb0)", "Motor board: the wheel's force read from the game");
}

// ------------------------------------------------------------------------------
// Cars: its HASP key (the API linked in the program) answered; the calibration's clamps made
// unsigned (an infinite scale while calibrating gave a negative index: a crash)
// ------------------------------------------------------------------------------

static const Fix CARS_FIXES[] = {
    {{0x81ef22, {0x7e, 0x07, 0xc7, 0x45, 0xf0, 0xff, 0x0f}, 7}, {0x76}, 1},
    {{0x81f2ee, {0x7e, 0x07, 0xc7, 0x45, 0xf0, 0xff, 0x0f}, 7}, {0x76}, 1},
};

// login, logout, read, write, decrypt, encrypt (stdcall)
static const Site CARS_HASP[6] = {
    {0x147f67d, {0x56, 0x8b, 0x74, 0x24, 0x0c, 0x85, 0xf6}, 7}, {0x14959f0, {0x56, 0x8b, 0x74, 0x24, 0x08, 0x6a, 0x00}, 7},
    {0x1494504, {0x57, 0x8b, 0x7c, 0x24, 0x18, 0x85, 0xff}, 7}, {0x14c049f, {0x57, 0x8b, 0x7c, 0x24, 0x18, 0x85, 0xff}, 7},
    {0x148fd65, {0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x85, 0xdb}, 7}, {0x14c8ab7, {0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x85, 0xdb}, 7},
};

static unsigned char g_dongle[256]; // its memory: the serial number at 0x28, a counter at 0x2C
static CRITICAL_SECTION g_dongleLock;

static unsigned __stdcall hasp_login(unsigned, void *, unsigned *handle)
{
    if (handle)
        *handle = 1;
    return 0;
}
static unsigned __stdcall hasp_logout(unsigned)
{
    return 0;
}
static unsigned __stdcall hasp_read(unsigned, unsigned, unsigned offset, unsigned length, unsigned char *buffer)
{
    EnterCriticalSection(&g_dongleLock);
    for (unsigned i = 0; i < length; i++)
        buffer[i] = offset + i < sizeof g_dongle ? g_dongle[offset + i] : 0;
    LeaveCriticalSection(&g_dongleLock);
    return 0;
}
static unsigned __stdcall hasp_write(unsigned, unsigned, unsigned offset, unsigned length, const unsigned char *buffer)
{
    EnterCriticalSection(&g_dongleLock);
    for (unsigned i = 0; i < length; i++)
        if (offset + i < sizeof g_dongle)
            g_dongle[offset + i] = buffer[i];
    LeaveCriticalSection(&g_dongleLock);
    return 0;
}
// The game encrypts "Flynn Lives" at its start, and checks every few seconds that it decrypts back:
// the data left as it is
static unsigned __stdcall hasp_crypt(unsigned, void *, unsigned)
{
    return 0;
}

// The wheel's force the race (or the test menu) sets: [[0x18AE0D8] + 0x1B4] + 0x258, > 0 pushing left
static float cars_force(void)
{
    unsigned s = read_u32(0x18ae0d8);
    if (s)
        s = read_u32(s + 0x1b4);
    return s ? read_float(s + 0x258) : 0.0f;
}

static int install_cars(void)
{
    const Site *sites[] = {&CARS_HASP[0], &CARS_HASP[1], &CARS_HASP[2], &CARS_HASP[3], &CARS_HASP[4], &CARS_HASP[5]};
    if (!sites_ok(sites, 6) || !apply_fixes(CARS_FIXES, 2, "calibration clamp (unsigned)"))
        return 0;
    InitializeCriticalSection(&g_dongleLock);
    unsigned serial = 1;
    memcpy(g_dongle + 0x28, &serial, 4);
    const void *fakes[6] = {(void *)hasp_login, (void *)hasp_logout, (void *)hasp_read, (void *)hasp_write, (void *)hasp_crypt, (void *)hasp_crypt};
    for (int i = 0; i < 6; i++)
        redirect(CARS_HASP[i].va, fakes[i], "HASP key (answered)");
    rio_install(g_carsInputs, RIO_COUNT, 0, cars_force);
    return 1;
}

// ------------------------------------------------------------------------------
// Batman: its cabinet's HASP check (without it the level's loading spins), the cabinet mode 3 (the
// key of a cabinet; 2 a service key, drawing a green overlay)
// ------------------------------------------------------------------------------

static const Site BATMAN_CHECK = {0x1384dc0, {0x55, 0x8b, 0xec, 0x51, 0xa1, 0x44, 0x0c, 0xa7, 0x01}, 9};
static const Site BATMAN_CHECK_CALL = {0x12fb700, {0xe8, 0xbb, 0x96, 0x08, 0x00, 0x85, 0xc0, 0x75, 0x09}, 9};
static const unsigned BATMAN_MODE = 0x1a70c44;
static const Site BATMAN_FORCE_READ = {0x146b422, {0xf3, 0x0f, 0x10, 0x05, 0x24, 0xf5, 0xab, 0x01}, 8}; // its own debug display reads the force
static const unsigned BATMAN_FORCE = 0x1abf524;

static float batman_force(void)
{
    float f = read_float((unsigned)(size_t)at(BATMAN_FORCE)) / 1.2f;
    return f < -1 ? -1.0f : f > 1 ? 1.0f : f;
}

static float no_force(void)
{
    return 0.0f;
}

static int install_batman(void)
{
    const Site *sites[] = {&BATMAN_CHECK, &BATMAN_CHECK_CALL};
    if (!sites_ok(sites, 2))
        return 0;
    unsigned mode = 3;
    put(BATMAN_MODE, &mode, 4, "cabinet mode (a cabinet's key)");
    put(BATMAN_CHECK.va, RET1, 6, "cabinet's HASP check (passed)");
    int ffb = site_ok(&BATMAN_FORCE_READ);
    if (!ffb)
        wl_log("rawthrills: %s: unexpected code reading its wheel force: no force", g_title);
    rio_install(g_batmanInputs, RIO_COUNT - 1, 1, ffb ? batman_force : no_force);
    return 1;
}

// ==============================================================================
// The games
// ==============================================================================

enum
{
    FAM_JAMMA,
    FAM_NICKTOONS,
    FAM_H2O,
    FAM_SUPERCARS,
    FAM_SUPERBIKES2,
    FAM_DIRTYDRIVIN,
    FAM_CARS,
    FAM_BATMAN,
    FAM_SNOCROSS
};

struct RtKnown
{
    const char *name, *title;
    DWORD crc32; // its sdaemon.exe's (zlib's)
    unsigned base;
    int family;
    const RtGame *jamma;
};

static const RtKnown g_known[] = {
    {"fnfsb", "The Fast and the Furious Super Bikes", 0x25ED956B, 0x400000, FAM_JAMMA, &g_superbikes},
    {"fnf", "The Fast and the Furious", 0x9AEBF60F, 0x400000, FAM_JAMMA, &g_fnf},
    {"fnfdrift", "The Fast and the Furious Drift", 0x8467C744, 0x400000, FAM_JAMMA, &g_drift},
    {"nicktoons", "Nicktoons Nitro Racing", 0xEC2F7B39, 0x400000, FAM_NICKTOONS, &g_nicktoons},
    {"h2overdrive", "H2Overdrive", 0x30E58AB0, 0x400000, FAM_H2O, NULL},
    {"fnfsc", "The Fast and the Furious SuperCars", 0xB01B9D7D, 0x400000, FAM_SUPERCARS, NULL},
    {"fnfsb2", "The Fast and the Furious Super Bikes 2", 0x66E8AD40, 0x400000, FAM_SUPERBIKES2, NULL},
    {"dirtydrivin", "Dirty Drivin'", 0xA933D4E5, 0x400000, FAM_DIRTYDRIVIN, NULL},
    {"cars", "Cars", 0xE0444E25, 0x400000, FAM_CARS, NULL},
    {"batman", "Batman", 0x0F66E24E, 0x12d0000, FAM_BATMAN, NULL},
    {"snocross", "Winter X Games SnoCross", 0x873E033B, 0x400000, FAM_SNOCROSS, NULL},
};

int wl_game_rawthrills(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    if (_wcsicmp(base, L"sdaemon.exe"))
        return 0;
    g_exe = (unsigned char *)GetModuleHandleW(NULL);
    DWORD crc = wl_game_exe_crc32();
    const RtKnown *k = NULL;
    for (int i = 0; i < (int)(sizeof g_known / sizeof g_known[0]); i++)
        if (g_known[i].crc32 == crc)
            k = &g_known[i];
    if (!k)
    {
        wl_log("game: a Raw Thrills sdaemon.exe (CRC32 %08lX) windowsloader does not know yet: observing only", crc);
        return 0;
    }
    *name = k->name;
    *title = g_title = k->title;
    g_base = k->base;
    wl_log("game: %s (Raw Thrills PC, sdaemon.exe CRC32 %08lX)", *title, crc);
    int ok = 0;
    switch (k->family)
    {
    case FAM_JAMMA:
    case FAM_NICKTOONS:
        ok = install_jamma(k->jamma, k->family == FAM_NICKTOONS);
        if (ok && k->jamma == &g_drift)
        {
            // Its screen's size (0x56FC10): the copies' data says 1280x720 or 1920x1080, its menus are
            // drawn for 640x480 in a corner, the 3D over the whole picture; 640x480 as
            // windowsarcadeloader's profile
            unsigned *size = (unsigned *)at(0x56fc10);
            if (size[0] >= 640 && size[0] <= 4096 && size[1] >= 480 && size[1] <= 4096)
            {
                static const unsigned vga[2] = {640, 480};
                wl_log("rawthrills: %s: its screen %ux%u made 640x480 (its menus' size)", g_title, size[0], size[1]);
                put(0x56fc10, vga, 8, "screen size (640x480)");
            }
            else
                wl_log("rawthrills: %s: unexpected screen size at 0x56fc10: left alone", g_title);
        }
        break;
    case FAM_H2O:
        ok = install_h2o();
        break;
    case FAM_SUPERCARS:
        ok = install_supercars();
        break;
    case FAM_SUPERBIKES2:
        ok = install_superbikes2();
        break;
    case FAM_DIRTYDRIVIN:
        ok = install_dirtydrivin();
        break;
    case FAM_CARS:
        ok = install_cars();
        break;
    case FAM_BATMAN:
        ok = install_batman();
        break;
    case FAM_SNOCROSS:
        ok = install_snocross();
        break;
    }
    if (!ok)
        wl_log("game: %s: its board not emulated (see above)", *title);
    // D:\ (its audits, settings, calibration, best times): the game's folder, where the cabinets'
    // copies keep them (H2Overdrive's Audits.xml and Settings.xml, Nicktoons' audits and prefs,
    // Cars' carscade\version.txt)
    char folder[MAX_PATH * 2];
    int n = WideCharToMultiByte(CP_ACP, 0, exePath, (int)(base - exePath), folder, sizeof folder - 2, NULL, NULL);
    n = n > 0 ? n : 0;
    if (n > 3 && folder[n - 1] == '\\')
        n--;
    folder[n] = 0;
    wl_files_redirect("D:", folder);
    wl_log("game: %s: D:\\ is %s", *title, folder);
    return 1;
}

#else
int wl_game_rawthrills(const wchar_t *, const char **, const char **)
{
    return 0; // the games are 32 bits
}
#endif
