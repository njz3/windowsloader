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

#include "agent.h"
#include "display.h"
#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "log.h"
#include "outputs.h"
#include "settings.h"

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

// [Display] WIDTH x HEIGHT, read once the settings file is known (after the game is recognized).
// AUTO leaves the cabinet's 1360x768. The two writes are the only immediates of that size: the
// rest of the game reads the fields they fill (0x91F30C / 0x91F310 and the copies beside them),
// its render targets and 3D then at that size. Its 2D stays placed on a 1360x768 screen.
static void render_resolution(void)
{
    int w = wl_settings_get_int("Display", "WIDTH", 0);
    int h = wl_settings_get_int("Display", "HEIGHT", 0);
    if (w < 320 || h < 240)
        return;
    // +0x4d5a8: mov ecx, 1360 / mov eax, 768, then stored in every resolution field
    unsigned char wasInit[10] = {0xB9, 0x50, 0x05, 0x00, 0x00, 0xB8, 0x00, 0x03, 0x00, 0x00};
    unsigned char nowInit[10] = {0xB9, 0x00, 0x00, 0x00, 0x00, 0xB8, 0x00, 0x00, 0x00, 0x00};
    memcpy(nowInit + 1, &w, 4);
    memcpy(nowInit + 6, &h, 4);
    // +0x28fc: mov [0x91F310], 768 / mov [0x91F30C], 1360, then copied into the device object
    unsigned char wasDev[20] = {0xC7, 0x05, 0x10, 0xF3, 0x91, 0x00, 0x00, 0x03, 0x00, 0x00,
                                0xC7, 0x05, 0x0C, 0xF3, 0x91, 0x00, 0x50, 0x05, 0x00, 0x00};
    unsigned char nowDev[20];
    memcpy(nowDev, wasDev, sizeof nowDev);
    memcpy(nowDev + 6, &h, 4);
    memcpy(nowDev + 16, &w, 4);
    int ok = wl_patch_code((void *)0x44D5A8, wasInit, nowInit, sizeof nowInit, "render resolution");
    ok &= wl_patch_code((void *)0x4028FC, wasDev, nowDev, sizeof nowDev, "render resolution on the device");
    // The places converting the resolution to floats map the game's 1360x768 screen coordinates
    // to the picture: they read the cabinet's own size (an unused pair, 0x8C0A24 / 0x8C0A28)
    static const unsigned int widthAt[] = {0x40A87A, 0x4148B4, 0x414D7A, 0x41E0AE, 0x426A49, 0x426F19, 0x4275E4, 0x4284A8, 0x429503,
                                           0x4428A6, 0x4475D6, 0x447B0F, 0x447B8F, 0x447C0F, 0x44F91B, 0x459BA0, 0x4FD424, 0x501151,
                                           0x5050BB, 0x52801E, 0x52926A, 0x529F4B, 0x52A002, 0x52A0B9, 0x52A187, 0x54E326, 0x54EB33,
                                           0x5CDE17};
    static const unsigned int heightAt[] = {0x40A8B7, 0x413BD4, 0x413C34, 0x414046, 0x41479B, 0x41487F, 0x414D41, 0x4180F1, 0x41E0FA,
                                            0x426A5C, 0x426F2C, 0x4275EC, 0x428484, 0x42950B, 0x42EF79, 0x4428BA, 0x444F59, 0x4475DE,
                                            0x447B23, 0x447BA3, 0x447C23, 0x44CB00, 0x44F8F7, 0x459C99, 0x49212C, 0x4922CE, 0x4D8BF9,
                                            0x4D985B, 0x4DA705, 0x4DAEA7, 0x4DB468, 0x4DB5A9, 0x4DB906, 0x4DBEC3, 0x4DF526, 0x4DFE1B,
                                            0x4FD447, 0x505097, 0x528026, 0x529279, 0x529CA4, 0x529F5C, 0x52A013, 0x52A0CA, 0x52A195,
                                            0x52AE37, 0x54F6F1, 0x54FF15, 0x550360, 0x5505A8, 0x551892, 0x5CDE1F, 0x5CE2A8, 0x5CE4EA,
                                            0x5CF637, 0x5D094F};
    const unsigned int realW = 0x91F30C, realH = 0x91F310, ownW = 0x8C0A24, ownH = 0x8C0A28;
    int moved = 0;
    for (int pass = 0; pass < 2; pass++)
    {
        const unsigned int *at = pass ? heightAt : widthAt;
        int count = pass ? (int)(sizeof heightAt / sizeof heightAt[0]) : (int)(sizeof widthAt / sizeof widthAt[0]);
        for (int i = 0; i < count; i++)
            moved += wl_patch_code((void *)(ULONG_PTR)at[i], pass ? &realH : &realW, pass ? &ownH : &ownW, 4, "screen coordinates at 1360x768");
    }
    wl_log("  %d places keep the 1360x768 screen coordinates", moved);
    // The 2D layers (+0xbb60) draw their sprites in pixels of that screen, within scissor rectangles
    wl_d3d9_scale_2d(1360, 768);
    if (ok)
        wl_log("  render resolution %dx%d (the game's own is 1360x768)", w, h);
}

// The lamps, read in libacio.dll: 8 lamps, a bit each. ac_io_hbhi_control_lamp_on and _off take a
// mask (on and off kept only out of the brightness mode, ac_io_hbhi_control_lamp_mode 1; off puts
// the lamps' brightness to 0 too), _bright a mask and a level 0..31 (more counts as 0). Kept here
// for the network outputs (lamp0..lamp7, bright0..bright7), then given to libacio
#define GTI_LAMPS 8
static int(__cdecl *oLampOn)(int), (__cdecl *oLampOff)(int), (__cdecl *oLampMode)(int), (__cdecl *oLampBright)(int, int);
static int g_lampMask, g_lampBrightMode, g_brightValue = -1;

static void lamp_levels(int mask, int level)
{
    for (int i = 0; i < GTI_LAMPS && g_brightValue >= 0; i++)
        if (mask >> i & 1)
            wl_outputs_value(g_brightValue + i, level);
}

static int __cdecl lamp_on(int mask)
{
    if (!g_lampBrightMode)
        g_lampMask |= mask & 0xFF;
    wl_outputs_lamps((unsigned)g_lampMask, GTI_LAMPS); // for BackForceFeeder ([Outputs] OUTPUTS_NETWORK)
    return oLampOn(mask);
}

static int __cdecl lamp_off(int mask)
{
    if (!g_lampBrightMode)
        g_lampMask &= ~mask & 0xFF;
    wl_outputs_lamps((unsigned)g_lampMask, GTI_LAMPS);
    lamp_levels(mask, 0);
    return oLampOff(mask);
}

static int __cdecl lamp_bright(int mask, int level)
{
    lamp_levels(mask, (unsigned char)level > 31 ? 0 : (unsigned char)level);
    return oLampBright(mask, level);
}

static int __cdecl lamp_mode(int mode)
{
    if (mode == 0 || mode == 1)
        g_lampBrightMode = mode;
    return oLampMode(mode);
}

static void lamps_hook(void)
{
    HMODULE exe = GetModuleHandleW(NULL);
    int n = (wl_patch_iat(exe, "libacio.dll", "ac_io_hbhi_control_lamp_on", (void *)lamp_on, (void **)&oLampOn) != 0) +
            (wl_patch_iat(exe, "libacio.dll", "ac_io_hbhi_control_lamp_off", (void *)lamp_off, (void **)&oLampOff) != 0) +
            (wl_patch_iat(exe, "libacio.dll", "ac_io_hbhi_control_lamp_bright", (void *)lamp_bright, (void **)&oLampBright) != 0) +
            (wl_patch_iat(exe, "libacio.dll", "ac_io_hbhi_control_lamp_mode", (void *)lamp_mode, (void **)&oLampMode) != 0);
    if (n != 4)
    {
        wl_log("gti club 3: %d of libacio's 4 lamp functions found, the lamps not sent", n);
        return;
    }
    wl_outputs_name_lamps("la carte HBHI, ac_io_hbhi_control_lamp_on et _off (libacio.dll)", NULL, GTI_LAMPS);
    for (int i = 0; i < GTI_LAMPS; i++)
    {
        char value[16], about[128];
        snprintf(value, sizeof value, "bright%d", i);
        snprintf(about, sizeof about, "Intensité de la lampe %d, ac_io_hbhi_control_lamp_bright (libacio.dll), 0 à 31", i);
        int v = wl_outputs_declare_value(value, "intensité (0-31)", about);
        if (!i)
            g_brightValue = v;
    }
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
    wl_game_set_boards("Controls on Konami ACIO board \"HBHI\" (COM2, through libacio.dll)",
                       "Motor board: Konami ACIO board \"HBHI\" (the force the game leaves for it)");

    char folder[MAX_PATH], to[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, dir, -1, folder, sizeof folder, NULL, NULL);
    snprintf(to, sizeof to, "%sEUP", folder);
    wl_files_redirect("E:\\UP", to);
    snprintf(to, sizeof to, "%sFUP", folder);
    wl_files_redirect("F:\\UP", to);
    wl_inputs_set_table(g_inputs, IN_COUNT);
    lamps_hook();
    wl_display_set_internal_supported(1);
    wl_game_on_settings(render_resolution);

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
