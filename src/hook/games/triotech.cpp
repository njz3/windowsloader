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

// Trio Tech PC: Wasteland Racers 2071 (2006), Arcade.exe (x86, Direct3D 8, fmod). Its cabinet's
// board, a force feedback wheel with the pedals, the buttons and the coin slots on a serial port
// (ForceFeedbackWheel.xml: SerialPort 3), is reached through SDC2.dll, which the game loads and
// asks its functions by GetProcAddress (+0x136aa0). The game is given windowsloader's functions
// instead (the game's own GetProcAddress): the board is never opened.
//   Init(port)             -1 for a board not connected
//   GetAxisData(axis)      float -1..1: (raw - 32767) / 32767 of the board's 16-bit value;
//                          Inputs.xml: axis 0 the wheel, 1 the accelerator, 2 the brake; the
//                          pedals 1 released, 0 pressed (the game takes 1 - value, +0x13dcfc)
//   GetDigitalIn()         the buttons, bit n = Inputs.xml's Button<n>: 0 BOOST, 1 CAMERA,
//                          2..5 CHEAT1..4, 7 GEAR, 8 COIN, 11 MAINTENANCE, 12 STOP; active low
//                          (the game inverts the word, +0x136e7d)
//   GetCoin(slot), ClearCoin(slot), SetCoin(slot, count): the coin counters, by input bit (8,
//                          COIN); the free play's start sets it to a game's price (main.dsf)
//   SetForce(word), SetSteeringPos(word): the wheel's motor, sent together, a position servo: the
//                          stiffness and the position it holds the wheel at (FFBArcadePlugin's
//                          WastelandRacers2071.cpp plays them as a spring of SetForce / 32768 and a
//                          force of SetSteeringPos / 128000)
//   SetDigitalOut(word)    the lamps (Inputs.xml's DigitalOutputs): bit n is the network outputs' lamp<n>

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "log.h"
#include "outputs.h"
#include "vdinput.h"

enum
{
    IN_STEERING,
    IN_GAS,
    IN_BRAKE,
    IN_BUTTONS, // GetDigitalIn's bits, in g_bits' order
    IN_COUNT = IN_BUTTONS + 10
};

static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 65534, 32767},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 65535, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 65535, 0},
    {"Boost", "Nitro", WL_INPUT_SWITCH, 0, 1, 0},
    {"Camera", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"Cheat1", "View1", WL_INPUT_SWITCH, 0, 1, 0},
    {"Cheat2", "View2", WL_INPUT_SWITCH, 0, 1, 0},
    {"Cheat3", "View3", WL_INPUT_SWITCH, 0, 1, 0},
    {"Cheat4", "View4", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear", "GearUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"Stop", "Stop", WL_INPUT_SWITCH, 0, 1, 0},
};

// Button0 BOOST, 1 CAMERA, 2..5 CHEAT1..4, 7 GEAR, 8 COIN, 11 MAINTENANCE, 12 STOP
static const unsigned short g_bits[IN_COUNT - IN_BUTTONS] = {0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0080, 0x0100, 0x0800, 0x1000};

// ------------------------------------------------------------------------------
// SDC2.dll's functions, as the board answers them (cdecl)
// ------------------------------------------------------------------------------

static volatile LONG g_coins[16];

static int __cdecl sdc_Init(int port)
{
    wl_log("trio tech: SDC2 Init(COM%d): the board is windowsloader's", port);
    return 0;
}

static void __cdecl sdc_Shutdown(void)
{
}

// The first call of each function the game makes, logged (with its caller)
#define first_call(name, arg) first_call_from(name, arg, WL_CALLER)
static void first_call_from(const char *name, int arg, const void *caller)
{
    static char seen[32][24];
    static int count;
    char key[24];
    snprintf(key, sizeof key, "%s%d", name, arg);
    for (int i = 0; i < count; i++)
        if (!strcmp(seen[i], key))
            return;
    if (count < 32)
        snprintf(seen[count++], sizeof seen[0], "%s", key);
    char where[128];
    wl_log("trio tech: the game calls %s(%d) [%s]", name, arg, wl_where(caller, where, sizeof where));
}

// The wheel's motor holds the wheel at a position (SetSteeringPos) with a stiffness (SetForce):
// the force is that spring's, from where the wheel is (as FFBArcadePlugin plays it: a spring of
// SetForce's strength). Units of the game's axis, -1..1.
static volatile LONG g_stiffness, g_target; // SetForce's and SetSteeringPos's values, signed 16-bit

static void motor_out(double wheel)
{
    double stiffness = (short)g_stiffness / 32768.0, target = (short)g_target / 32768.0;
    if (stiffness < 0)
        stiffness = 0;
    wl_ffb_set(stiffness * (wheel - target)); // > 0 pushes left: the wheel turned right (wheel > target) is pulled back
}

static float __cdecl sdc_GetAxisData(int axis)
{
    first_call("GetAxisData", axis);
    if (axis == 0)
    {
        double wheel = (wl_input_get(&g_inputs[IN_STEERING]) - 32767) / 32767.0;
        motor_out(wheel);
        return (float)wheel;
    }
    // The pedals the other way round: 1 released, 0 pressed down (the game takes 1 - value, +0x13dcfc)
    if (axis == 1)
        return (float)(1.0 - wl_input_get(&g_inputs[IN_GAS]) / 65535.0);
    if (axis == 2)
        return (float)(1.0 - wl_input_get(&g_inputs[IN_BRAKE]) / 65535.0);
    return 0;
}

static unsigned short buttons(void)
{
    unsigned short b = 0;
    for (int i = 0; i < IN_COUNT - IN_BUTTONS; i++)
        if (wl_input_get(&g_inputs[IN_BUTTONS + i]))
            b |= g_bits[i];
    return b;
}

static unsigned short __cdecl sdc_GetDigitalIn(void)
{
    static unsigned short was;
    first_call("GetDigitalIn", 0);
    unsigned short b = buttons();
    if (b != was)
    {
        wl_log("trio tech: buttons 0x%04x", b);
        was = b;
    }
    return (unsigned short)~b; // active low: the game inverts it (+0x136e7d)
}

// The coin counters, one per input bit (the game asks GetCoin(8), COIN's): a coin counts once per
// press; in free play the game sets the counter to a game's price (SetCoin) to start one
static short __cdecl sdc_GetCoin(short slot)
{
    static int coinWas;
    first_call("GetCoin", slot);
    int coin = (buttons() & 0x0100) != 0;
    if (coin && !coinWas)
        InterlockedIncrement(&g_coins[8]);
    coinWas = coin;
    return slot >= 0 && slot < 16 ? (short)g_coins[slot] : 0;
}

static int __cdecl sdc_ClearCoin(short slot)
{
    first_call("ClearCoin", slot);
    if (slot >= 0 && slot < 16)
        InterlockedExchange(&g_coins[slot], 0);
    return 0;
}

static int __cdecl sdc_SetCoin(short slot, int count)
{
    first_call("SetCoin", slot);
    if (slot >= 0 && slot < 16)
        InterlockedExchange(&g_coins[slot], count);
    return 0;
}

static int __cdecl sdc_SetForce(unsigned short force)
{
    static int logged;
    if (InterlockedExchange(&g_stiffness, force) != force && logged < 60)
    {
        logged++;
        wl_log("trio tech: SetForce %d", (short)force);
    }
    return 0;
}

static int __cdecl sdc_SetSteeringPos(unsigned short pos)
{
    static int logged;
    if (InterlockedExchange(&g_target, pos) != pos && logged < 60)
    {
        logged++;
        wl_log("trio tech: SetSteeringPos %d", (short)pos);
    }
    return 0;
}

static int __cdecl sdc_SetDGain(unsigned short gain)
{
    wl_log("trio tech: SetDGain %u", gain);
    return 0;
}

#define TT_LAMPS 16 // SetDigitalOut's bits

static int __cdecl sdc_SetDigitalOut(unsigned short lamps)
{
    static unsigned short was = 0xFFFF;
    if (lamps != was)
    {
        static int logged;
        if (logged++ < 40)
            wl_log("trio tech: lamps 0x%04x", lamps);
        was = lamps;
    }
    wl_outputs_lamps(lamps, TT_LAMPS); // lamp0..lamp15, for BackForceFeeder ([Outputs] OUTPUTS_NETWORK)
    return 0;
}

static int __cdecl sdc_GetErrorCode(void)
{
    return 0;
}

static int __cdecl sdc_IsConnected(void)
{
    return 1;
}

static int __cdecl sdc_GetModel(void)
{
    return 1;
}

static int __cdecl sdc_GetFirmware(void)
{
    return 1;
}

static int __cdecl sdc_GetSerial(void)
{
    return 1;
}

static const struct
{
    const char *name;
    void *fn;
} g_sdc[] = {
    {"Init", (void *)sdc_Init},
    {"Shutdown", (void *)sdc_Shutdown},
    {"GetAxisData", (void *)sdc_GetAxisData},
    {"GetDigitalIn", (void *)sdc_GetDigitalIn},
    {"GetCoin", (void *)sdc_GetCoin},
    {"ClearCoin", (void *)sdc_ClearCoin},
    {"SetCoin", (void *)sdc_SetCoin},
    {"SetForce", (void *)sdc_SetForce},
    {"SetSteeringPos", (void *)sdc_SetSteeringPos},
    {"SetDGain", (void *)sdc_SetDGain},
    {"SetDigitalOut", (void *)sdc_SetDigitalOut},
    {"GetErrorCode", (void *)sdc_GetErrorCode},
    {"IsConnected", (void *)sdc_IsConnected},
    {"GetModel", (void *)sdc_GetModel},
    {"GetFirmware", (void *)sdc_GetFirmware},
    {"GetSerial", (void *)sdc_GetSerial},
};

// The game also opens the first DirectInput joystick it finds (vJoy on the test PC, read nowhere
// with the board in use): a joystick with the board's controls is listed instead, the PC's hidden
static void fill_joystick(unsigned short axes[WL_VDINPUT_AXES], unsigned char *buttons)
{
    axes[0] = (unsigned short)wl_input_get(&g_inputs[IN_STEERING]);
    axes[1] = (unsigned short)wl_input_get(&g_inputs[IN_GAS]);
    axes[2] = (unsigned short)wl_input_get(&g_inputs[IN_BRAKE]);
    for (int i = 0; i < IN_COUNT - IN_BUTTONS; i++)
        for (int bit = 0; bit < 16; bit++)
            if (g_bits[i] == 1 << bit)
                buttons[bit] = (unsigned char)(wl_input_get(&g_inputs[IN_BUTTONS + i]) != 0);
}

static const WlVirtualJoystick g_joystick = {"Trio Tech Force Feedback Wheel", 0x1209, 0x2071, 16, fill_joystick};

// The game's own GetProcAddress: SDC2.dll's functions are windowsloader's
static FARPROC(WINAPI *oGetProcAddress)(HMODULE, LPCSTR);
static FARPROC WINAPI game_GetProcAddress(HMODULE mod, LPCSTR name)
{
    if (mod && mod == GetModuleHandleA("SDC2.dll") && HIWORD((ULONG_PTR)name))
        for (size_t i = 0; i < sizeof g_sdc / sizeof g_sdc[0]; i++)
            if (!strcmp(name, g_sdc[i].name))
                return (FARPROC)g_sdc[i].fn;
    return oGetProcAddress(mod, name);
}

// The lamps' names, for the menu: Inputs.xml's DigitalOutputs (<string name="Output1"
// value="BOOST"/>: bit 1), those UNUSED left out. The copy on racecab1: 1 BOOST, 2 CAMERA, 3 CHEAT1,
// 4 ACTIVEBOOST, 10 BRAKES
static char g_lampText[TT_LAMPS][32];
static const char *g_lampNames[TT_LAMPS];

static void read_lamp_names(const wchar_t *file)
{
    FILE *f = NULL;
    if (_wfopen_s(&f, file, L"rb") || !f)
        return;
    static char text[32768];
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    const char *outputs = strstr(text, "\"DigitalOutputs\"");
    for (int i = 0; outputs && i < TT_LAMPS; i++)
    {
        char key[24];
        snprintf(key, sizeof key, "\"Output%d\"", i); // with its quotes: Output1 is not Output10
        const char *p = strstr(outputs, key), *end;
        if (!p || !(p = strstr(p, "value=\"")) || !(end = strchr(p += 7, '"')) || end == p || end - p >= 32)
            continue;
        snprintf(g_lampText[i], sizeof g_lampText[i], "%.*s", (int)(end - p), p);
        if (_stricmp(g_lampText[i], "UNUSED"))
            g_lampNames[i] = g_lampText[i];
    }
}

static int exists(const wchar_t *dir, const wchar_t *name)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s%s", dir, name);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

int wl_game_triotech(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    wchar_t dir[MAX_PATH];
    wcsncpy_s(dir, MAX_PATH, exePath, base - exePath);
    if (_wcsicmp(base, L"Arcade.exe") || !exists(dir, L"SDC2.dll") || !exists(dir, L"Inputs.xml"))
        return 0;
    *name = "wasteland";
    *title = "Wasteland Racers 2071";
    wl_log("game: %s (Trio Tech PC)", *title);
    wl_game_set_boards("Controls on Trio Tech SDC2 board (serial, through SDC2.dll)",
                       "Motor board: Trio Tech SDC2 board (position servo: stiffness and target)");
    wl_inputs_set_table(g_inputs, IN_COUNT);
    wl_vdinput_register(&g_joystick);
    wchar_t inputs[MAX_PATH];
    swprintf(inputs, MAX_PATH, L"%sInputs.xml", dir);
    read_lamp_names(inputs);
    wl_outputs_name_lamps("la carte SDC2, SetDigitalOut (DigitalOutputs d'Inputs.xml)", g_lampNames, TT_LAMPS);
    // The cabinets elect a host by broadcast on UDP 2070 ("Finding Server..."): alone, it hears itself
    wl_system_loopback_broadcast(1);
    if (wl_patch_iat(GetModuleHandleW(NULL), "KERNEL32.dll", "GetProcAddress", (void *)game_GetProcAddress, (void **)&oGetProcAddress))
        wl_log("game: SDC2.dll's functions (the cabinet's board) given by windowsloader");
    return 1;
}
