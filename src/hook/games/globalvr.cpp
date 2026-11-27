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

// Global VR "Zeus" cabinets: NASCAR Team Racing (2.0.2, build 068) and NASCAR Racing (1.1), the
// race alone: NASCAR_GVR.exe (EA's NASCAR engine, x86, Direct3D 9, fixed base 0x400000), in
// C:\NASCAR\Game on the cabinet. Reimplemented from windowsarcadeloader's findings (aderumier, GPL
// v3, https://github.com/aderumier/windowsarcadeloader, commit 2bfe069: systemprofiles/globalvr/
// nascar-team-racing.yaml and nascar-racing.yaml, crates/payload-globalvr/src: gvrio.rs,
// netcomm.rs; its docs/IMPLEMENTATION.md).
//
// On the cabinet the race is started by the cabinet's frontend (the Anark player AMPlayer.exe
// running C:\NASCAR\Shell\shell.am, its .NET 1.1 plug-ins and SQL database, the Hercules coin
// monitors), for each game: NASCAR_GVR.exe -track <track> -car <car> .... That frontend is not
// played here: the race is started on its own, its track and car from [Game.nascar] TRACK and CAR
// when its command line has no -track (its GetCommandLineA), a race per start.
//
// - GvrIO.dll, the cabinet's message library (Shell\bin on the cabinet's PATH): the race imports
//   its cGvrIO class (constructor, destructor, Initialize) and GvrIOMessageSend, all four pointed
//   at ours. The race calls Initialize(device type, hwnd, callback) and they talk with messages
//   both ways (sMessage: kind, id, value, a pointer, an answer pointer, a handled byte). Ours
//   calls the callback with id 0 once the controls are ready, then, from a thread 60 times a
//   second as the original does, id 0x0B for each button change ("onGvrStartButtonDown",
//   "onGvrNosButtonUp"...). GvrIOMessageSend answers a pointer to an int: id 1 the race's update,
//   id 5 the axes (value 0 the wheel -127..127, 1 the gas 0..255, 2 the brake 0..255), id 0x42 a
//   button by its name (the 4-position shifter: Shifter1..Shifter4, and an H shifter's
//   ShifterLeft / Right / Up / Down), the force feedback (0x1D a spring: force 0..100, its centre
//   -90..90 pointed to, 90 the wheel turned fully right; 0x19 and 0x1B two sines, force 0..100;
//   each with its off message; 0x35 a damper, not sent) and what the frontend asks (its board's
//   name and version, the dongle's game, version and region, the axes' range).
// - The race's HASP dongle, linked into the executable: its reader (the dongle's record at
//   0x7792C0 in 2.0.2, 0x779CC0 in 1.1) made to answer a dongle for NASCAR 2.0 (1.0 for 1.1); the
//   display modes kept up to 16384 pixels wide (the engine dropped every mode over 1600).
//   windowsarcadeloader's patches; here made only where the bytes are the ones found in 2.0.2's
//   program (the disc's NASCAR_GVR.exe, CRC-32 BEBCEA21). NASCAR Racing 1.1's are at its own
//   addresses (its disc 050-0136-01's NASCAR_GVR.exe, CRC-32 999502FC): made only where its code
//   has 2.0.2's instructions. 1.1 has no gvrNetComm.dll.
// - gvrNetComm.dll (the link between cabinets, the cabinet's C:\WINDOWS\system32): the copy's,
//   left as it is: no cabinet linked when the race is not started for a link.
// - HKLM\SOFTWARE\GlobalVR\NASCAR (the race's full screen size, FullScreenWidth and Height, and
//   its version), as the disc's Registry\NASCAR.reg sets it: made up by windowsloader, the
//   screen's size, else the cabinet's 1360x768.
// The launcher puts ..\Shell\bin and ..\..\WINDOWS\system32 on the race's PATH (the disc's layout:
// GvrIO.dll and its USBIOExtreme.dll, gvrNetComm.dll, the 7.1 C runtime msvcr71 / msvcp71).

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "display.h"
#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "log.h"
#include "vscreen.h"

#if defined(_M_IX86)

// ------------------------------------------------------------------------------
// The controls
// ------------------------------------------------------------------------------

enum
{
    IN_STEERING,
    IN_GAS,
    IN_BRAKE,
    IN_START,
    IN_VIEW,
    IN_LOOKBACK,
    IN_NOS,
    IN_MUSIC,
    IN_EBRAKE,
    IN_OPERATOR,
    IN_SHIFT_UP,
    IN_SHIFT_DOWN,
    IN_GEAR1, // .. IN_GEAR1 + 3: the 4-position shifter's gears, held while in gear
    IN_COUNT = IN_GEAR1 + 4
};

static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, -127, 127, 0},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 255, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 255, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"View", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"LookBack", "LookBack", WL_INPUT_SWITCH, 0, 1, 0},
    {"Nos", "Nitro", WL_INPUT_SWITCH, 0, 1, 0},
    {"Music", "MusicUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"Ebrake", "Handbrake", WL_INPUT_SWITCH, 0, 1, 0},
    {"Operator", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftUp", "GearUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftDown", "GearDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear1", "Gear1", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear2", "Gear2", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear3", "Gear3", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear4", "Gear4", WL_INPUT_SWITCH, 0, 1, 0},
};

// The buttons sent as events: onGvr<name>ButtonDown / Up, and onGvr<name>Pressed when pressed
static const struct
{
    int input;
    const char *name;
} g_events[] = {
    {IN_START, "Start"}, {IN_VIEW, "View"},   {IN_LOOKBACK, "LookBack"}, {IN_NOS, "Nos"},
    {IN_MUSIC, "Music"}, {IN_EBRAKE, "Ebrake"}, {IN_OPERATOR, "Operator"},
};
#define EVENT_COUNT ((int)(sizeof g_events / sizeof g_events[0]))

static int held(int i)
{
    return wl_input_get(&g_inputs[i]) != 0;
}

// ------------------------------------------------------------------------------
// GvrIO.dll: the messages
// ------------------------------------------------------------------------------

struct sMessage
{
    DWORD kind;
    DWORD id;
    int value;
    const char *data; // an event's, a button's or a lamp's name; the spring's centre
    void *answer;     // the frontend's selection
    unsigned char handled;
};

typedef void *(__cdecl *GvrCallback)(sMessage *);

enum
{
    ID_READY = 0,
    ID_UPDATE = 1,
    ID_AXIS = 5,
    ID_AXIS_RAW = 6,
    ID_EVENT = 0x0B,
    ID_SINE = 0x19,
    ID_SINE_OFF = 0x1A,
    ID_SINE2 = 0x1B,
    ID_SINE2_OFF = 0x1C,
    ID_SPRING = 0x1D,
    ID_SPRING_OFF = 0x1E,
    ID_DONGLE_CHECK = 0x23,
    ID_DONGLE_CHECKED = 0x24,
    ID_DONGLE_PRESENT = 0x25,
    ID_DONGLE_REGION = 0x26,
    ID_DONGLE_GAME = 0x27,
    ID_DONGLE_VERSION = 0x28,
    ID_DONGLE_ID = 0x29,
    ID_BAD_BUTTONS = 0x2A,
    ID_BOARD_MAJOR = 0x2F,
    ID_BOARD_MINOR = 0x30,
    ID_DAMPER = 0x35,
    ID_DAMPER_OFF = 0x36,
    ID_AXIS_MIN = 0x39,
    ID_AXIS_MAX = 0x3A,
    ID_DISPENSER_STATUS = 0x3E,
    ID_BUTTON = 0x42,
    ID_DONGLE_COUNT = 0x4D,
    ID_AXIS_CALIBRATED = 0x54,
    ID_BOARD_NAME = 0x60,
    ID_BOARD_PRODUCT = 0x6F,
};

static volatile GvrCallback g_callback;
static const char *g_dongleGame = "NASCAR", *g_dongleVersion = "2.0", *g_dongleRegion = "XX";

static void post(DWORD id, int value, const char *name)
{
    GvrCallback cb = g_callback;
    if (!cb)
        return;
    sMessage m = {1, id, value, name, NULL, 0};
    cb(&m);
}

// The gear of the 4-position shifter, 1..4: GearUp and GearDown step it, Gear1..Gear4 set it
static volatile LONG g_gear = 1;
static int g_buttons; // the events' buttons at the last update, a bit each

static void update(void)
{
    int buttons = 0;
    for (int i = 0; i < EVENT_COUNT; i++)
        if (held(g_events[i].input))
            buttons |= 1 << i;
    static int upHeld, downHeld;
    int up = held(IN_SHIFT_UP), down = held(IN_SHIFT_DOWN);
    LONG gear = g_gear;
    if (up && !upHeld && gear < 4)
        gear++;
    if (down && !downHeld && gear > 1)
        gear--;
    upHeld = up;
    downHeld = down;
    for (int g = 0; g < 4; g++)
        if (held(IN_GEAR1 + g))
            gear = g + 1;
    if (gear != g_gear)
    {
        g_gear = gear;
        wl_log("nascar: gear %ld", gear);
    }
    int changed = buttons ^ g_buttons;
    g_buttons = buttons;
    for (int i = 0; i < EVENT_COUNT; i++)
    {
        if (!(changed & (1 << i)))
            continue;
        int isDown = (buttons >> i) & 1;
        char event[64];
        snprintf(event, sizeof event, "onGvr%sButton%s", g_events[i].name, isDown ? "Down" : "Up");
        wl_log("nascar: %s", event);
        post(ID_EVENT, 0, event);
        if (isDown)
        {
            snprintf(event, sizeof event, "onGvr%sPressed", g_events[i].name);
            post(ID_EVENT, 0, event);
        }
    }
}

// The original's message thread: the button events 60 times a second
static volatile LONG g_dongleChecked; // the dongle checked: its answer (0x24) still to send

static DWORD WINAPI events_thread(void *p)
{
    for (;;)
    {
        Sleep(16);
        if (!g_callback)
            continue;
        if (InterlockedExchange(&g_dongleChecked, 0))
            post(ID_DONGLE_CHECKED, 1, NULL);
        update();
    }
}

static int axis(int index)
{
    switch (index)
    {
    case 0:
        return wl_input_get(&g_inputs[IN_STEERING]);
    case 1:
        return wl_input_get(&g_inputs[IN_GAS]);
    case 2:
        return wl_input_get(&g_inputs[IN_BRAKE]);
    default:
        return 0;
    }
}

static int button(const char *name)
{
    static char seen[16][32];
    static int seenCount;
    int known = 0;
    for (int i = 0; i < seenCount; i++)
        known |= !strcmp(seen[i], name);
    if (!known && seenCount < 16)
    {
        snprintf(seen[seenCount++], 32, "%s", name);
        wl_log("nascar: button \"%s\" asked", name);
    }
    LONG gear = g_gear;
    if (!strncmp(name, "Shifter", 7) && name[7] >= '1' && name[7] <= '4' && !name[8])
        return gear == name[7] - '0';
    // an H shifter's place of a gear (1 up-left, 2 down-left, 3 up, 4 down); ShifterRight, for a
    // fifth gear and above, is never held: 0 below
    if (!strcmp(name, "ShifterLeft"))
        return gear <= 2;
    if (!strcmp(name, "ShifterUp"))
        return gear % 2 == 1;
    if (!strcmp(name, "ShifterDown"))
        return gear % 2 == 0;
    for (int i = 0; i < EVENT_COUNT; i++)
        if (!strcmp(g_events[i].name, name))
            return held(g_events[i].input);
    return 0;
}

// The force feedback: the spring around the game's centre, the stronger sine as a vibration
static int g_springForce, g_springCentre, g_sine[2];

static void ffb(DWORD id, int value, const int *param)
{
    int force = value < 0 ? 0 : value > 100 ? 100 : value;
    switch (id)
    {
    case ID_SPRING:
        g_springForce = force;
        g_springCentre = param ? *param : 0;
        break;
    case ID_SPRING_OFF:
        g_springForce = 0;
        break;
    case ID_SINE:
    case ID_SINE2:
        g_sine[id == ID_SINE2] = force;
        break;
    case ID_SINE_OFF:
    case ID_SINE2_OFF:
        g_sine[id == ID_SINE2_OFF] = 0;
        break;
    }
    int centre = g_springCentre < -90 ? -90 : g_springCentre > 90 ? 90 : g_springCentre;
    // the race's centre: +90 the wheel turned fully right; windowsloader's: > 0 to the left
    wl_ffb_set_conditions(g_springForce / 100.0, -centre / 90.0, g_springForce / 100.0, 0);
    int sine = g_sine[0] > g_sine[1] ? g_sine[0] : g_sine[1];
    wl_ffb_set_vibration(sine / 100.0, 50);
}

static volatile LONG g_answer;
static LONG g_zero;
static const unsigned char g_present = 1;
static const DWORD g_dongleId = 0x57414C00;

// void *GvrIOMessageSend(sMessage *): a pointer to the answer
static void *__cdecl gvr_send(sMessage *m)
{
    if (!m)
        return &g_zero;
    int answer = 0;
    switch (m->id)
    {
    case ID_UPDATE:
        break;
    case ID_AXIS:
        answer = axis(m->value);
        break;
    case ID_AXIS_RAW:
    case ID_AXIS_CALIBRATED:
        answer = m->value == 0 ? axis(0) + 128 : axis(m->value); // the wheel as 0..255 too
        break;
    case ID_AXIS_MIN:
        break;
    case ID_AXIS_MAX:
        answer = 255;
        break;
    case ID_BUTTON:
        answer = m->data ? button(m->data) : 0;
        break;
    case ID_SINE:
    case ID_SINE_OFF:
    case ID_SINE2:
    case ID_SINE2_OFF:
    case ID_SPRING:
    case ID_SPRING_OFF:
        ffb(m->id, m->value, (const int *)m->data);
        break;
    case ID_DAMPER:
    case ID_DAMPER_OFF:
        break;
    // the dongle and card dispenser plug-ins' answers (the frontend's), the board's
    case ID_DONGLE_CHECK:
        m->handled = 1;
        InterlockedExchange(&g_dongleChecked, 1); // answered by the events' thread
        return (void *)&g_present;
    case ID_DONGLE_PRESENT:
        m->handled = 1;
        return (void *)&g_present;
    case ID_DONGLE_REGION:
        m->handled = 1;
        return (void *)g_dongleRegion;
    case ID_DONGLE_GAME:
        m->handled = 1;
        return (void *)g_dongleGame;
    case ID_DONGLE_VERSION:
        m->handled = 1;
        return (void *)g_dongleVersion;
    case ID_DONGLE_ID:
        m->handled = 1;
        return (void *)&g_dongleId;
    case ID_DISPENSER_STATUS:
        m->handled = 1;
        return (void *)(ULONG_PTR)1; // no card ready
    case ID_BOARD_NAME:
        m->handled = 1;
        return (void *)"GvrIOmini";
    case ID_BOARD_MAJOR:
        answer = 3; // the firmware 3.03 or later NASCAR's frontend wants
        break;
    case ID_BOARD_MINOR:
        answer = 3;
        break;
    case ID_BOARD_PRODUCT:
        answer = 7021;
        break;
    case ID_BAD_BUTTONS:
        break;
    case ID_DONGLE_COUNT:
        answer = 1;
        break;
    default:
    {
        // Each id, name and value once (the lamps are set by their names): the first 300
        char text[48] = "";
        __try
        {
            // a name when it points somewhere (some kinds carry a number there: 1000, 0x3E8)
            for (int i = 0; (ULONG_PTR)m->data >= 0x10000 && i < 47 && m->data[i] >= 0x20 && m->data[i] < 0x7F; i++)
                text[i] = m->data[i], text[i + 1] = 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            text[0] = 0;
        }
        static struct
        {
            DWORD id;
            int value;
            char name[48];
        } seen[300];
        static int seenCount;
        int known = 0;
        for (int i = 0; i < seenCount && !known; i++)
            known = seen[i].id == m->id && seen[i].value == m->value && !strcmp(seen[i].name, text);
        if (!known && seenCount < 300)
        {
            seen[seenCount].id = m->id;
            seen[seenCount].value = m->value;
            strcpy(seen[seenCount++].name, text);
            wl_log("nascar: GvrIO message 0x%lx (kind %lu, value %d%s%s%s) not handled", m->id, m->kind, m->value, text[0] ? ", \"" : "", text,
                   text[0] ? "\"" : "");
        }
        g_zero = 0;
        return &g_zero;
    }
    }
    m->handled = 1;
    InterlockedExchange(&g_answer, answer);
    return (void *)&g_answer;
}

// The cGvrIO class's methods (thiscall: this in ecx, the callee pops its arguments; __fastcall
// with edx unused takes the same calls)
static void *__fastcall gvr_new(void *self, void *edx)
{
    return self;
}

static void __fastcall gvr_delete(void *self, void *edx)
{
    g_callback = NULL;
}

static bool __fastcall gvr_initialize(void *self, void *edx, DWORD device, HWND wnd, GvrCallback callback)
{
    wl_log("nascar: GvrIO initialized (device type %lu): the controls are windowsloader's", device);
    g_callback = callback;
    post(ID_READY, 0, NULL);
    static volatile LONG started;
    if (!InterlockedExchange(&started, 1))
        CloseHandle(CreateThread(NULL, 0, events_thread, NULL, 0, NULL));
    return true;
}

static bool __fastcall gvr_register_callback(void *self, void *edx, DWORD device, GvrCallback callback)
{
    g_callback = callback;
    return true;
}

// ------------------------------------------------------------------------------
// The race's builds: the HASP dongle's reader and the display modes' width (windowsarcadeloader's
// WAL_PATCHES), each where the code is the one found in 2.0.2's program
// ------------------------------------------------------------------------------

static const struct
{
    const char *title, *version;
    unsigned hasp;           // the dongle reader's address
    unsigned record;         // the dongle's record its code fills (at +0x18: lea esi, [eax + record])
    const char *dongleVersion; // "2.0": written at the record's +8
    unsigned modes;          // the display modes' widest (an immediate 1600)
} g_builds[] = {
    {"NASCAR Team Racing", "2.0.2", 0x675B10, 0x7792C0, "2.0", 0x5079B6},
    {"NASCAR Racing", "1.1", 0x672460, 0x779CC0, "1.0", 0x507C60},
};

// The reader's first instructions in 2.0.2 (its index checked against the dongles' count):
// mov edx, [esp+4]; test edx, edx; jl ...; cmp edx, [count]
static const unsigned char g_haspStart[10] = {0x8B, 0x54, 0x24, 0x04, 0x85, 0xD2, 0x7C, 0x48, 0x3B, 0x15};

static int readable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    return VirtualQuery(p, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT && (ULONG_PTR)p + n <= (ULONG_PTR)mbi.BaseAddress + mbi.RegionSize;
}

// The build whose reader is where windowsarcadeloader found it, or -1
static int which_build(void)
{
    for (int i = 0; i < (int)(sizeof g_builds / sizeof g_builds[0]); i++)
    {
        const unsigned char *at = (const unsigned char *)(ULONG_PTR)g_builds[i].hasp;
        unsigned char lea[6] = {0x8D, 0xB0};
        memcpy(lea + 2, &g_builds[i].record, 4);
        if (readable(at, 0x20) && !memcmp(at, g_haspStart, sizeof g_haspStart) && !memcmp(at + 0x18, lea, 6))
            return i;
    }
    return -1;
}

static void patch_build(int b)
{
    // The dongle's reader: its record filled for this game (present, "NASCAR", its version), 1;
    // 0 for any dongle but the first
    unsigned char code[48] = {
        0x8B, 0x44, 0x24, 0x04,                   // mov eax, [esp+4]
        0x85, 0xC0,                               // test eax, eax
        0x75, 0x23,                               // jne no
        0xB8, 0, 0, 0, 0,                         // mov eax, record
        0xC6, 0x00, 0x01,                         // mov byte [eax], 1
        0xC7, 0x40, 0x01, 'N', 'A', 'S', 'C',     // mov dword [eax+1], "NASC"
        0xC7, 0x40, 0x05, 'A', 'R', 0x00, 0x00,   // mov dword [eax+5], "AR"
        0xC7, 0x40, 0x08, 0, 0, 0, 0,             // mov dword [eax+8], version
        0xB8, 0x01, 0x00, 0x00, 0x00,             // mov eax, 1
        0xC3,                                     // ret
        0x33, 0xC0, 0xC3,                         // no: xor eax, eax; ret
    };
    memcpy(code + 9, &g_builds[b].record, 4);
    strncpy((char *)code + 33, g_builds[b].dongleVersion, 4);
    unsigned char *at = (unsigned char *)(ULONG_PTR)g_builds[b].hasp;
    wl_patch_code(at, at, code, sizeof code, "NASCAR's HASP dongle answered (its record for this game)");
    g_dongleVersion = g_builds[b].dongleVersion;

    // mov dword [edx+0x1F64], 1600: 16384
    unsigned char *modes = (unsigned char *)(ULONG_PTR)g_builds[b].modes;
    static const unsigned char was[10] = {0xC7, 0x82, 0x64, 0x1F, 0x00, 0x00, 0x40, 0x06, 0x00, 0x00};
    static const unsigned char now[4] = {0x00, 0x40, 0x00, 0x00};
    if (readable(modes - 6, 10) && !memcmp(modes - 6, was, 10))
        wl_patch_code(modes, was + 6, now, 4, "NASCAR's display modes up to 16384 pixels wide");
    else
        wl_log("nascar: unexpected code where its display modes are dropped over 1600 pixels: left as it is");
}

// ------------------------------------------------------------------------------
// The command line: a race's track and car when none is given
// ------------------------------------------------------------------------------

static char g_cmdLine[2048];
static LPSTR(WINAPI *oGetCommandLineA)(void);

static LPSTR WINAPI nascar_GetCommandLineA(void)
{
    return g_cmdLine[0] ? g_cmdLine : oGetCommandLineA();
}

// Once the settings are read, before the race's own code runs
static void race_arguments(void)
{
    const char *cmd = GetCommandLineA();
    if (strstr(cmd, "-track"))
        return;
    if (!oGetCommandLineA)
    {
        wl_log("nascar: GetCommandLineA not imported: the race starts without a track");
        return;
    }
    char track[64], car[16];
    wl_game_option("TRACK", track, sizeof track);
    wl_game_option("CAR", car, sizeof car);
    snprintf(g_cmdLine, sizeof g_cmdLine, "%s -track %s -car %s", cmd, track[0] ? track : "daytona", car[0] ? car : "20");
    wl_log("nascar: no track on its command line: -track %s -car %s ([Game.nascar] TRACK, CAR)", track[0] ? track : "daytona",
           car[0] ? car : "20");
}

// HKLM\SOFTWARE\GlobalVR\NASCAR, as the disc's Registry\NASCAR.reg (the full screen's size the
// screen's, else the cabinet's 1360x768)
static char g_width[8], g_height[8];

static void nascar_registry(int b)
{
    int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
    snprintf(g_width, sizeof g_width, "%d", w > 0 ? w : 1360);
    snprintf(g_height, sizeof g_height, "%d", h > 0 ? h : 768);
    static const WlRegValue v202[] = {
        {"Version", REG_SZ, "2.0.2", 0},         {"Build", REG_SZ, "068", 0},     {"CommVersion", REG_SZ, "2.0.1", 0},
        {"Prefix", REG_SZ, "", 0},               {"Suffix", REG_SZ, "", 0},       {"FullScreenWidth", REG_SZ, g_width, 0},
        {"FullScreenHeight", REG_SZ, g_height, 0}, {"FourButtonAbort", REG_SZ, "1", 0}, {"StallMonitorTest", REG_SZ, "0", 0},
    };
    static WlRegValue v11[sizeof v202 / sizeof v202[0]];
    const WlRegValue *values = v202;
    if (b == 1)
    {
        memcpy(v11, v202, sizeof v202);
        v11[0].data = "1.1";
        v11[1].data = "";
        values = v11;
    }
    wl_registry_add_key("SOFTWARE\\GlobalVR", NULL, 0);
    wl_registry_add_key("SOFTWARE\\GlobalVR\\NASCAR", values, (int)(sizeof v202 / sizeof v202[0]));
    wl_log("nascar: HKLM\\SOFTWARE\\GlobalVR\\NASCAR made up (full screen %sx%s)", g_width, g_height);
}

// ------------------------------------------------------------------------------
// Need for Speed Underground (1.1, build 027): its race alone, Underground\UndergroundGVR.exe
// (C:\Underground on the cabinet; x86, Direct3D 9, 800x600, a mixed C++ / .NET 1.1 program, run by
// Windows' CLR 2.0 when .NET 1.1 is not there). Reimplemented from windowsarcadeloader's findings
// (gvrinput.rs, need-for-speed-underground.yaml) and the NFSU_GVR project's notes (iGThomas,
// github.com/iGThomas/NFSU_GVR, docs/technical-notes.md: its command-line options).
//
// On the cabinet the frontend (GvrRoot\UniverShell2.exe, .NET 1.1) starts the race for each game,
// both reading the cabinet's SQL Server database through PLUSDE.dll. Not played here: the race is
// started on its own with -nosql (its own option: PlusDE not initialized; the launcher adds it),
// one race per start. PLUSDE.dll on CLR 2.0 failed (its schema C:\PLUS\SCHEMA\NFSCABINET.XML,
// which the cabinet's installer makes, and its C++ exceptions, NFSU_GVR's notes).
//
// GVRInputRaw.dll (the cabinet's C:\WINDOWS\system32): its 15 functions the race imports pointed
// at ours. GVRInputRawInit(hwnd, state, game) gives the state the race reads after each
// GVRInputRawUpdate(state) (windowsarcadeloader's layout): +0x00 the wheel, i16 -128 (left)..127,
// +0x02 the gas 0..255, +0x34 the brake 0..255, +0x08 the previous wheel, +0x0C / +0x10 the buttons
// and the previous ones, +0x18 / +0x20 this update's and the previous one's
// QueryPerformanceCounter, +0x30 the driver (6: the cabinet's Immersion wheel). The buttons: 0x10
// START (a car reset in a race), 0x20 the operator's, 0x100 music, 0x200 look back, 0x400 the
// nitrous, 0x10000 view, 0x20000 the e-brake; its 6-speed H shifter as bits (1st 0x1, 2nd 0x80001,
// 3rd none, 4th 0x80000, 5th 0x2, 6th 0x80002), stepped by GearUp / GearDown or set by Gear1..6.
// The race writes its force feedback there: +0x88 the spring (u8 on, i32 strength 0..100), +0xA0
// and +0xAC two sines (crashes, terrain). It calls the library from its DirectInput keyboard's
// update: a keyboard device (windowsloader's, no key down) is needed.
// The real GVRInputRawInit found no board here: the race then crashed at +0x1CCED0 (a table of
// its input devices left empty).
// ------------------------------------------------------------------------------

enum
{
    UG_STEERING,
    UG_GAS,
    UG_BRAKE,
    UG_START,
    UG_OPERATOR,
    UG_MUSIC,
    UG_LOOKBACK,
    UG_NOS,
    UG_VIEW,
    UG_EBRAKE,
    UG_SHIFT_UP,
    UG_SHIFT_DOWN,
    UG_GEAR1, // .. UG_GEAR1 + 5
    UG_COUNT = UG_GEAR1 + 6
};

static WlInput g_ugInputs[UG_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, -128, 127, 0},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 255, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 255, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"Operator", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"Music", "MusicUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"LookBack", "LookBack", WL_INPUT_SWITCH, 0, 1, 0},
    {"Nitrous", "Nitro", WL_INPUT_SWITCH, 0, 1, 0},
    {"View", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"Ebrake", "Handbrake", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftUp", "GearUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftDown", "GearDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear1", "Gear1", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear2", "Gear2", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear3", "Gear3", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear4", "Gear4", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear5", "Gear5", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear6", "Gear6", WL_INPUT_SWITCH, 0, 1, 0},
};

static const struct
{
    int input;
    DWORD bit;
} g_ugButtons[] = {
    {UG_START, 0x10},   {UG_OPERATOR, 0x20}, {UG_MUSIC, 0x100},    {UG_LOOKBACK, 0x200},
    {UG_NOS, 0x400},    {UG_VIEW, 0x10000},  {UG_EBRAKE, 0x20000},
};

// The H shifter's bits of each gear, 1st to 6th
static const DWORD g_ugShifter[6] = {0x1, 0x80001, 0, 0x80000, 0x2, 0x80002};
static int g_ugGear = 1, g_ugUpHeld, g_ugDownHeld;
static DWORD g_ugLastButtons = 0xFFFFFFFF;

static void ug_fill(unsigned char *state)
{
    DWORD buttons = 0;
    for (int i = 0; i < (int)(sizeof g_ugButtons / sizeof g_ugButtons[0]); i++)
        if (wl_input_get(&g_ugInputs[g_ugButtons[i].input]))
            buttons |= g_ugButtons[i].bit;
    int up = wl_input_get(&g_ugInputs[UG_SHIFT_UP]) != 0, down = wl_input_get(&g_ugInputs[UG_SHIFT_DOWN]) != 0;
    int gear = g_ugGear;
    if (up && !g_ugUpHeld && gear < 6)
        gear++;
    if (down && !g_ugDownHeld && gear > 1)
        gear--;
    g_ugUpHeld = up;
    g_ugDownHeld = down;
    for (int g = 0; g < 6; g++)
        if (wl_input_get(&g_ugInputs[UG_GEAR1 + g]))
            gear = g + 1;
    if (gear != g_ugGear)
    {
        g_ugGear = gear;
        wl_log("nfsu: gear %d", gear);
    }
    buttons |= g_ugShifter[gear - 1];
    short wheel = (short)wl_input_get(&g_ugInputs[UG_STEERING]), gas = (short)wl_input_get(&g_ugInputs[UG_GAS]),
          brake = (short)wl_input_get(&g_ugInputs[UG_BRAKE]);
    memcpy(state + 0x08, state, 2); // the previous wheel
    memcpy(state + 0x00, &wheel, 2);
    memcpy(state + 0x02, &gas, 2);
    memcpy(state + 0x34, &brake, 2);
    memcpy(state + 0x10, state + 0x0C, 4);
    memcpy(state + 0x0C, &buttons, 4);
    DWORD driver = 6; // the cabinet's wheel
    memcpy(state + 0x30, &driver, 4);
    if (buttons != g_ugLastButtons)
    {
        g_ugLastButtons = buttons;
        wl_log("nfsu: buttons 0x%lx", buttons);
    }
}

// An effect's strength 0..1 (+0 its on byte, +4 its strength 0..100), 0 when off
static double ug_effect(const unsigned char *state, int offset)
{
    if (!state[offset])
        return 0;
    int v;
    memcpy(&v, state + offset + 4, 4);
    return (v < 0 ? 0 : v > 100 ? 100 : v) / 100.0;
}

static void ug_ffb(const unsigned char *state)
{
    static double lastSpring = -1, lastSine = -1;
    double spring = ug_effect(state, 0x88);
    double a = ug_effect(state, 0xA0), b = ug_effect(state, 0xAC), sine = a > b ? a : b;
    if (spring != lastSpring)
    {
        lastSpring = spring;
        wl_ffb_set_conditions(spring, 0, spring, 0);
    }
    if (sine != lastSine)
    {
        lastSine = sine;
        wl_ffb_set_vibration(sine, 50);
    }
}

// int GVRInputRawInit(HWND, state *, const char *game): 0, the wheel ready
static int __cdecl ug_init(HWND wnd, unsigned char *state, const char *game)
{
    wl_log("nfsu: GVRInputRawInit for \"%s\", state at %p: the controls are windowsloader's", game ? game : "", state);
    if (!state)
        return 2;
    ug_fill(state);
    return 0;
}

// int GVRInputRawUpdate(state *): 0, updated
static int __cdecl ug_update(unsigned char *state)
{
    if (!state)
        return 3;
    memcpy(state + 0x20, state + 0x18, 8);
    QueryPerformanceCounter((LARGE_INTEGER *)(state + 0x18));
    ug_fill(state);
    ug_ffb(state);
    return 0;
}

// The cleanup, the fault and calibration counters: nothing to do, no fault
static int __cdecl ug_none(void)
{
    return 0;
}

static const struct
{
    const char *name;
    void *fn;
} g_ugImports[] = {
    {"GVRInputRawInit", (void *)ug_init},
    {"GVRInputRawUpdate", (void *)ug_update},
    {"GVRInputRawCleanup", (void *)ug_none},
    {"GVRInputRawGetStuckAxis", (void *)ug_none},
    {"GVRInputRawIncAxisFault", (void *)ug_none},
    {"GVRInputRawGetAxisFault", (void *)ug_none},
    {"GVRInputRawGetFFFault", (void *)ug_none},
    {"GVRInputRawClearAxisFault", (void *)ug_none},
    {"GVRInputRawClearStuckAxis", (void *)ug_none},
    {"GVRInputRawGetButtonFault", (void *)ug_none},
    {"GVRInputRawGetStuckButtonFault", (void *)ug_none},
    {"GVRInputRawClearButtonFault", (void *)ug_none},
    {"GVRInputRawClearStuckButtonFault", (void *)ug_none},
    {"GVRInputRawIncButtonFault", (void *)ug_none},
    {"GVRInputRawGetUncalibratedAxis", (void *)ug_none},
};

static int nfsu_game(const wchar_t *exePath, const wchar_t *base, const char **name, const char **title)
{
    HMODULE exe = GetModuleHandleW(NULL);
    *name = "nfsu";
    *title = "Need for Speed Underground";
    wl_log("game: %s (Global VR, the race alone, CRC-32 %08lX)", *title, wl_game_exe_crc32());
    wl_game_set_boards("Controls on Global VR's GVRInputRaw.dll (wheel, pedals, H shifter, buttons)",
                       "Motor board: Global VR's GVRInputRaw.dll (the spring and sines the race writes)");
    wl_inputs_set_table(g_ugInputs, UG_COUNT);
    int done = 0, count = (int)(sizeof g_ugImports / sizeof g_ugImports[0]);
    for (int i = 0; i < count; i++)
        done += wl_patch_iat(exe, "GVRInputRaw.dll", g_ugImports[i].name, g_ugImports[i].fn, NULL) != 0;
    wl_log("nfsu: %d of %d GVRInputRaw.dll functions answered", done, count);
    if (!strstr(GetCommandLineA(), "-nosql"))
        wl_log("nfsu: no -nosql on its command line: the race will want the cabinet's database (PLUSDE.dll, SQL Server)");

    // HKLM\SOFTWARE\GlobalVR\Need For Speed UnderGround and the operator's entry for the game, as
    // the disc's _Registry\UGVersion.reg and GVRInput.reg set them (its ini beside the race)
    static char ini[MAX_PATH];
    char folder[MAX_PATH];
    int n = WideCharToMultiByte(CP_ACP, 0, exePath, (int)(base - exePath), folder, sizeof folder - 1, NULL, NULL);
    folder[n > 0 ? n : 0] = 0;
    snprintf(ini, sizeof ini, "%sNFSUnderground.ini", folder);
    static const WlRegValue version[] = {
        {"Version", REG_SZ, "1.1.0", 0}, {"Build", REG_SZ, "027", 0}, {"Prefix", REG_SZ, "", 0}, {"Suffix", REG_SZ, "", 0}};
    static WlRegValue game[] = {{"Ini", REG_SZ, ini, 0}};
    wl_registry_add_key("SOFTWARE\\GlobalVR", NULL, 0);
    wl_registry_add_key("SOFTWARE\\GlobalVR\\Need For Speed UnderGround", version, 4);
    wl_registry_add_key("SOFTWARE\\GVRShell", NULL, 0);
    wl_registry_add_key("SOFTWARE\\GVRShell\\Operator", NULL, 0);
    wl_registry_add_key("SOFTWARE\\GVRShell\\Operator\\Games", NULL, 0);
    wl_registry_add_key("SOFTWARE\\GVRShell\\Operator\\Games\\NFSUNDERGROUND", game, 1);

    // Its audits written at C:\'s root (c:\audits.txt): beside the race instead
    char to[MAX_PATH];
    snprintf(to, sizeof to, "%saudits.txt", folder);
    wl_files_redirect_prefix("C:\\audits.txt", to);
    wl_dinput_quiet_keyboard();

    // Its sizes (+0x3b8640: 640x480, 800x600, 1024x768, 1280x960, 1280x1024), looked for among
    // Direct3D 9's X8R8G8B8 modes at start (+0x1ccd30); with none of them listed (a screen that lists
    // only its own size), it searches past its table (+0x1cced0, an access violation 0.7 s in)
    static const int sizes[][2] = {{640, 480}, {800, 600}, {1024, 768}, {1280, 960}, {1280, 1024}};
    for (int i = 0; i < (int)(sizeof sizes / sizeof sizes[0]); i++)
        wl_vscreen_d3d9_size(sizes[i][0], sizes[i][1]);
    return 1;
}

// ------------------------------------------------------------------------------
// The game
// ------------------------------------------------------------------------------

int wl_game_globalvr(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    if (!_wcsicmp(base, L"UndergroundGVR.exe"))
        return nfsu_game(exePath, base, name, title);
    if (_wcsicmp(base, L"NASCAR_GVR.exe"))
        return 0;
    HMODULE exe = GetModuleHandleW(NULL);
    int b = (ULONG_PTR)exe == 0x400000 ? which_build() : -1;
    *name = "nascar";
    *title = b >= 0 ? g_builds[b].title : "NASCAR (Global VR)";
    wl_log("game: %s %s (Global VR, the race alone, CRC-32 %08lX)", *title, b >= 0 ? g_builds[b].version : "(build not known)", wl_game_exe_crc32());
    wl_game_set_boards("Controls on Global VR's GvrIO.dll (its messages: wheel, pedals, shifter, buttons)",
                       "Motor board: Global VR's GvrIO.dll (the spring and sines the race asks)");
    wl_inputs_set_table(g_inputs, IN_COUNT);
    wl_game_use_options("nascar");

    struct
    {
        const char *name;
        void *fn;
    } io[] = {
        {"??0cGvrIO@GvrIO@Zeus@Gvr@@QAE@XZ", (void *)gvr_new},
        {"??1cGvrIO@GvrIO@Zeus@Gvr@@QAE@XZ", (void *)gvr_delete},
        {"?Initialize@cGvrIO@GvrIO@Zeus@Gvr@@QAE_NW4GVR_DEVICE_TYPES@234@PAUHWND__@@P6APAXPAUsMessage@234@@Z@Z", (void *)gvr_initialize},
        {"?MessageRegisterCallback@cGvrIO@GvrIO@Zeus@Gvr@@QAE_NW4GVR_DEVICE_TYPES@234@P6APAXPAUsMessage@234@@Z@Z", (void *)gvr_register_callback},
        {"?GvrIOMessageSend@GvrIO@Zeus@Gvr@@YAPAXPAUsMessage@123@@Z", (void *)gvr_send},
    };
    int done = 0;
    for (int i = 0; i < (int)(sizeof io / sizeof io[0]); i++)
        done += wl_patch_iat(exe, "GvrIO.dll", io[i].name, io[i].fn, NULL) != 0;
    wl_log("nascar: %d of the race's GvrIO.dll functions answered", done);
    if (done < 4)
        wl_log("nascar: the race does not import GvrIO.dll's cGvrIO as 2.0.2 does: its controls may stay the copy's GvrIO.dll's");

    if (b >= 0)
        patch_build(b);
    else
        wl_log("nascar: its HASP dongle's reader is not where windowsarcadeloader found it (2.0.2 at 0x675B10, 1.1 at 0x672460): left as "
               "it is (without its dongle the race may stop)");
    nascar_registry(b);
    // its command line read by the C runtime's start: the track and car given once the settings are
    if (!wl_patch_iat(exe, "kernel32.dll", "GetCommandLineA", (void *)nascar_GetCommandLineA, (void **)&oGetCommandLineA))
        oGetCommandLineA = NULL;
    wl_game_on_settings(race_arguments);
    // The motion seat's library (TsuMotion.dll, then motionLib.dll, the seat's I/O card): no seat
    // here, refused as windowsarcadeloader hides it (its load held the start 11 s on this PC)
    wl_system_refuse_dll("TsuMotion.dll");
    // Its start time written at C:\'s root (C:\gvrtime.txt): beside the race instead
    char folder[MAX_PATH], to[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, exePath, (int)(base - exePath), folder, sizeof folder - 1, NULL, NULL);
    folder[base - exePath < MAX_PATH - 1 ? base - exePath : MAX_PATH - 1] = 0;
    snprintf(to, sizeof to, "%sgvrtime.txt", folder);
    wl_files_redirect_prefix("C:\\gvrtime.txt", to);
    wl_dinput_quiet_keyboard();
    return 1;
}

#else
int wl_game_globalvr(const wchar_t *, const char **, const char **)
{
    return 0; // the game is 32 bits
}
#endif
