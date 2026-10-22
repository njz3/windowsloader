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

// Konami PC: GTI Club: Supermini Festa! (GTI Club 3, 2008), gti3.exe (x86, Direct3D 9, XACT 2.10
// sound, fixed base 0x400000). Its I/O board ("HBHI", Konami's ACIO protocol on COM2) is reached
// through libacio.dll; the board is not emulated: as OpenParrot does it (TypeX2/GTIClub3.cpp,
// GPL v3), the boot's I/O checks are passed and the inputs written where the game keeps them, each
// time it talks to the board. Its update drives E:\UP and F:\UP are the copy's EUP and FUP.

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "log.h"

enum
{
    IN_STEERING,
    IN_GAS,
    IN_BRAKE,
    IN_BUTTONS, // the button word's bits, in its order (the service menu's INPUT CHECK)
    IN_COUNT = IN_BUTTONS + 11
};

static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 255, 128},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 255, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 255, 0},
    {"Service", "P1_Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftDown", "GearDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftUp", "GearUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"Action", "Action", WL_INPUT_SWITCH, 0, 1, 0},
    {"SelectUp", "SelectUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"SelectDown", "SelectDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"SelectLeft", "SelectLeft", WL_INPUT_SWITCH, 0, 1, 0},
    {"SelectRight", "SelectRight", WL_INPUT_SWITCH, 0, 1, 0},
};

// Their bits: SERVICE, TEST, COIN MECH SWITCH, START, the lever's SHIFT DOWN and SHIFT UP, ACTION,
// SELECT UP, DOWN, LEFT and RIGHT (bit 3 is not shown; 0x20, 0x40, 0x200 and 0x400 are the
// pedals and the wheel seen as buttons, below)
static const unsigned short g_bits[IN_COUNT - IN_BUTTONS] = {0x0001, 0x0002, 0x0004, 0x0010, 0x0080, 0x0100,
                                                             0x0800, 0x1000, 0x2000, 0x4000, 0x8000};

// Where the game keeps its inputs (OpenParrot's InjectKeys): the button word, the values of the
// test menu (words) and those the game drives with (floats), and the buttons newly pressed
#define GTI_BUTTONS ((volatile unsigned short *)0xA7E784)
#define GTI_PRESSED ((volatile unsigned int *)0xA7E788)
#define GTI_INPUT_ON ((volatile unsigned char *)0xA7E75B)
#define GTI_RAW ((volatile unsigned short *)0x918C8C) // wheel, accelerator, brake
#define GTI_VALUES ((volatile float *)0x918C80)       // wheel -1..1, accelerator and brake 0..1

static void inject_inputs(void)
{
    static unsigned short previous;
    int wheel = wl_input_get(&g_inputs[IN_STEERING]);
    int gas = wl_input_get(&g_inputs[IN_GAS]);
    int brake = wl_input_get(&g_inputs[IN_BRAKE]);
    unsigned short b = 0;
    for (int i = 0; i < IN_COUNT - IN_BUTTONS; i++)
        if (wl_input_get(&g_inputs[IN_BUTTONS + i]))
            b |= g_bits[i];
    // The menus read the pedals and the wheel as buttons too (OpenParrot's thresholds)
    if (wheel < 0x60)
        b |= 0x200;
    if (wheel > 0xA0)
        b |= 0x400;
    if (gas > 0xA0)
        b |= 0x20;
    if (brake > 0xA0)
        b |= 0x40;
    *GTI_BUTTONS = b;
    *GTI_PRESSED = b & ~previous;
    previous = b;
    GTI_RAW[0] = (unsigned short)wheel;
    GTI_RAW[2] = (unsigned short)gas;
    GTI_RAW[4] = (unsigned short)brake;
    GTI_VALUES[0] = wheel / 255.0f * 2 - 1;
    GTI_VALUES[1] = gas / 255.0f;
    GTI_VALUES[2] = brake / 255.0f;
}

// The wheel's motor, as the game leaves it for the board (the addresses FFBArcadePlugin's
// GTIClub3.cpp reads): +0x518cbc, 0 or 0x80 no force, 0x81 (the strongest) to 0xFF one way,
// 0x7F (the strongest) to 0x01 the other; nothing while +0x518e8e (a menu) is set
#define GTI_MOTOR ((volatile unsigned char *)0x918CBC)
#define GTI_MENU ((volatile unsigned char *)0x918E8E)

static void motor_out(void)
{
    int v = *GTI_MOTOR;
    double f = 0;
    if (!*GTI_MENU && v > 0x80)
        f = (257 - v) / 128.0;
    else if (!*GTI_MENU && v > 0 && v < 0x80)
        f = -(v / 127.0);
    if (f > 1)
        f = 1;
    wl_ffb_set(f); // > 0 pushes left: a centering spring against the wheel turned right (seen in a race)
}

// ac_io_hbhi_COMUNICATE (+0x409c0), the game's exchange with the board, once a frame
static int(__cdecl *oCommunicate)(void);
static int __cdecl hCommunicate(void)
{
    int r = oCommunicate();
    // After it: the game's own update of these values, which needs the board, would clear them.
    // The inputs enabled each time (+0x440660, the I/O's start again, turns them off)
    *GTI_INPUT_ON = 1;
    inject_inputs();
    motor_out();
    return r;
}

// The boot's checks of the board (four entries of the table at +0x4c0e00): passed, the inputs
// enabled (OpenParrot's IgnoreFunc)
static char *__cdecl pass_check(void *a, char *state)
{
    state[8] = 1;
    *GTI_INPUT_ON = 1;
    return state;
}

static int exists(const wchar_t *dir, const wchar_t *name)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s%s", dir, name);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

int wl_game_konami(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    wchar_t dir[MAX_PATH];
    wcsncpy_s(dir, MAX_PATH, exePath, base - exePath);
    if ((_wcsicmp(base, L"gti3.exe") && _wcsicmp(base, L"gti3japkey.exe")) || !exists(dir, L"libacio.dll"))
        return 0;
    *name = "gticlub3";
    *title = "GTI Club: Supermini Festa!";
    wl_log("game: %s (Konami PC)", *title);

    char folder[MAX_PATH], to[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, dir, -1, folder, sizeof folder, NULL, NULL);
    snprintf(to, sizeof to, "%sEUP", folder);
    wl_files_redirect("E:\\UP", to);
    snprintf(to, sizeof to, "%sFUP", folder);
    wl_files_redirect("F:\\UP", to);
    wl_inputs_set_table(g_inputs, IN_COUNT);

    static const unsigned int checks[] = {0x8C0E0C, 0x8C0E14, 0x8C0E1C, 0x8C0E3C};
    static const unsigned int was[] = {0x44B120, 0x44B1D0, 0x44B1E0, 0x44BA90};
    for (int i = 0; i < 4; i++)
    {
        unsigned int pass = (unsigned int)(ULONG_PTR)pass_check;
        if (!wl_patch_code((void *)(ULONG_PTR)checks[i], &was[i], &pass, 4, "a board check passed"))
            return 1;
    }
    wl_hook_code((void *)0x4409C0, (void *)hCommunicate, (void **)&oCommunicate, "ac_io_hbhi_COMUNICATE");
    return 1;
}
