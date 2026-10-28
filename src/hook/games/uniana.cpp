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

// Frenzy Express (Uniana, 2001: xwin.exe, x86, OpenGL, 640x480), a delivery bicycle: a handlebar,
// two pedals pushed in turn, a brake lever, six buttons. Its board is read through xio.dll, class
// XIO, whose update() the game calls once a frame; the game then reads the library's globals:
//   g_bKeyImm    the buttons down: 0x01 Start, 0x02 Service, 0x04 Test (opens the test menu), 0x08
//                coin (0x0A, Service or coin, counts a coin), 0x10 select left, 0x20 select right
//                (seen in the test menu's I/O CHECK); g_bKeyExc those pressed since the last update
//   g_bHandle    the handlebar, 0x00 at the left end, 0xFF at the right, 0x80 in the middle
//   g_bBrake     the brake, 0..0xFF
//   g_bLAccel, g_bLAccelDif, g_bLAccelEnd, and the same with R: the left and right pedals (the
//                board gave each pedal's speed, its change, and 1 while it quickens)
// The copy of the cabinets has a keyboard version of the library (GetAsyncKeyState: F3, F2, 1,
// Enter, the arrows, Z, X, Ctrl, Shift). Here update() is windowsloader's and fills the globals
// from the controls, a pedal down setting its three bytes to 1, as OpenParrot does. The game also
// drives (read in xwin.exe):
//   setSteeringBar(v) the handlebar's motor, at each frame (+0x1d3fe): 0 none; the handlebar right
//                     of the middle, odd values 0x0B..0x17, left of it even 0x0A..0x16: seven
//                     strengths of a pull back to the middle, harder the further and the faster
//   lamp(pattern), startLamp(on), selectLamp(on) the cabinet's lights; setVibration(v, on)

#include <windows.h>
#include <string.h>

#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "log.h"
#include "outputs.h"

static int exists(const wchar_t *dir, const wchar_t *name)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s%s", dir, name);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

// ------------------------------------------------------------------------------
// The controls
// ------------------------------------------------------------------------------

enum
{
    IN_STEERING,
    IN_GAS,
    IN_BRAKE,
    IN_PEDAL_LEFT,
    IN_PEDAL_RIGHT,
    IN_TEST,
    IN_SERVICE,
    IN_COIN,
    IN_START,
    IN_LEFT,
    IN_RIGHT,
    IN_COUNT
};

static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0x00, 0xFF, 0x80},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 0xFF, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 0xFF, 0},
    {"PedalLeft", "PedalLeft", WL_INPUT_SWITCH, 0, 1, 0},
    {"PedalRight", "PedalRight", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "P1_Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"Left", "SelectLeft", WL_INPUT_SWITCH, 0, 1, 0},
    {"Right", "SelectRight", WL_INPUT_SWITCH, 0, 1, 0},
};

// The buttons in g_bKeyImm's bit order (0x01 to 0x20)
static const int g_buttons[] = {IN_START, IN_SERVICE, IN_TEST, IN_COIN, IN_LEFT, IN_RIGHT};

// The accelerator (a motorbike cabinet's throttle) pushes the pedals in turn: a full turn, left
// then right, in TURN_SLOW frames turned a little, TURN_FAST turned fully
#define TURN_SLOW 40.0
#define TURN_FAST 8.0
static double g_turn;

// ------------------------------------------------------------------------------
// xio.dll
// ------------------------------------------------------------------------------

static struct
{
    unsigned char *keyImm, *keyExc, *handle, *brake;
    unsigned char *accel[2], *accelDif[2], *accelEnd[2]; // left, right
} g_xio;

static int xio_globals(void)
{
    if (g_xio.handle)
        return 1;
    HMODULE m = GetModuleHandleW(L"xio.dll");
    if (!m)
        return 0;
    static const char *names[] = {"?g_bKeyImm@@3EA",   "?g_bKeyExc@@3EA",      "?g_bHandle@@3EA",      "?g_bBrake@@3EA",
                                  "?g_bLAccel@@3EA",   "?g_bRAccel@@3EA",      "?g_bLAccelDif@@3EA",   "?g_bRAccelDif@@3EA",
                                  "?g_bLAccelEnd@@3EA", "?g_bRAccelEnd@@3EA"};
    unsigned char *p[10];
    for (int i = 0; i < 10; i++)
        if (!(p[i] = (unsigned char *)GetProcAddress(m, names[i])))
        {
            wl_log("uniana: xio.dll has no %s", names[i]);
            return 0;
        }
    g_xio.keyImm = p[0];
    g_xio.keyExc = p[1];
    g_xio.brake = p[3];
    g_xio.accel[0] = p[4];
    g_xio.accel[1] = p[5];
    g_xio.accelDif[0] = p[6];
    g_xio.accelDif[1] = p[7];
    g_xio.accelEnd[0] = p[8];
    g_xio.accelEnd[1] = p[9];
    g_xio.handle = p[2];
    return 1;
}

static void(__fastcall *oupdate)(void *, void *);
static void __fastcall hupdate(void *self, void *)
{
    if (!xio_globals())
        return;
    unsigned char keys = 0;
    for (int i = 0; i < 6; i++)
        if (wl_input_get(&g_inputs[g_buttons[i]]))
            keys |= (unsigned char)(1 << i);
    *g_xio.keyExc = keys & (unsigned char)~*g_xio.keyImm;
    *g_xio.keyImm = keys;
    *g_xio.handle = (unsigned char)wl_input_get(&g_inputs[IN_STEERING]);
    *g_xio.brake = (unsigned char)wl_input_get(&g_inputs[IN_BRAKE]);

    int down[2] = {wl_input_get(&g_inputs[IN_PEDAL_LEFT]), wl_input_get(&g_inputs[IN_PEDAL_RIGHT])};
    double gas = wl_input_get(&g_inputs[IN_GAS]) / 255.0;
    if (gas > 0.05)
    {
        g_turn += 1.0 / (TURN_SLOW + (TURN_FAST - TURN_SLOW) * gas);
        if (g_turn >= 1)
            g_turn -= 1;
        down[g_turn < 0.5 ? 0 : 1] = 1;
    }
    for (int p = 0; p < 2; p++)
        *g_xio.accel[p] = *g_xio.accelDif[p] = *g_xio.accelEnd[p] = down[p] ? 1 : 0;
}

// The power-on calibration of the handlebar (xwin.exe+0x25c00, called once a frame until it
// returns 1; its state at [this], its frames at [this+8], 300 for each step): the motor pulls the
// handlebar right (state 1) and g_bHandle is taken as its right end, pulls it left (2) for its left
// end, brings it back to the middle (3) and waits (4); then "ERROR THE STEERING WHEEL" and the
// defaults 0x00, 0x80, 0xFF unless the left end is below the right one. A handlebar the motor does
// not turn (a wheel without force feedback, or with its own, too weak or the other way) gives the
// error, and a weak one a range too short. windowsloader's handlebar already spans 0x00..0xFF: the
// calibration gets the one it expects, at those ends, and keeps them; the motor still plays.
#define FE_CALIBRATE 0x425c00
static const unsigned char g_calibrateStart[] = {0x53, 0x56, 0x8B, 0xF1, 0x57}; // push ebx; push esi; mov esi, ecx; push edi

static int(__fastcall *ocalibrate)(void *, void *);
static int __fastcall hcalibrate(void *self, void *)
{
    int state = *(int *)self;
    if (xio_globals() && state >= 1 && state <= 4)
        *g_xio.handle = (unsigned char)(state == 1 ? 0xFF : state == 2 ? 0x00 : 0x80);
    int done = ocalibrate(self, NULL);
    static int logged;
    if (done && !logged)
    {
        logged = 1;
        const unsigned char *c = (const unsigned char *)self + 0xC;
        wl_log("uniana: handlebar calibrated: left 0x%02x, middle 0x%02x, right 0x%02x", c[0], c[1], c[2]);
    }
    return done;
}

// The outputs: the lights and the motor's value, for the network outputs
enum
{
    OUT_LAMP,
    OUT_START_LAMP,
    OUT_SELECT_LAMP,
    OUT_VIBRATION,
    OUT_MOTOR,
    OUT_COUNT
};
static int g_out[OUT_COUNT] = {-1, -1, -1, -1, -1};

static void out(int which, int value)
{
    if (g_out[which] >= 0)
        wl_outputs_value(g_out[which], value);
}

static void(__fastcall *osetSteeringBar)(void *, void *, unsigned char);
static void __fastcall hsetSteeringBar(void *self, void *, unsigned char v)
{
    double f = 0;
    if (v >= 0x0A && v <= 0x17)
        f = (v & 1 ? 1 : -1) * ((v - (v & 1 ? 0x0B : 0x0A)) / 2 + 1) / 7.0;
    wl_ffb_set(f); // > 0 pushes left: the handlebar right of the middle pulled back
    out(OUT_MOTOR, v);
    osetSteeringBar(self, NULL, v);
}

static void(__fastcall *osetVibration)(void *, void *, unsigned char, int);
static void __fastcall hsetVibration(void *self, void *, unsigned char v, int on)
{
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 8)
        wl_log("uniana: setVibration(%u, %d)", v, on);
    out(OUT_VIBRATION, on ? v : 3); // off: 3, as the library keeps it
    osetVibration(self, NULL, v, on);
}

static void(__fastcall *olamp)(void *, void *, unsigned char);
static void __fastcall hlamp(void *self, void *, unsigned char pattern)
{
    out(OUT_LAMP, pattern);
    olamp(self, NULL, pattern);
}

static void(__fastcall *ostartLamp)(void *, void *, int);
static void __fastcall hstartLamp(void *self, void *, int on)
{
    out(OUT_START_LAMP, on ? 1 : 0);
    ostartLamp(self, NULL, on);
}

static void(__fastcall *oselectLamp)(void *, void *, int);
static void __fastcall hselectLamp(void *self, void *, int on)
{
    out(OUT_SELECT_LAMP, on ? 1 : 0);
    oselectLamp(self, NULL, on);
}

static WlHook g_xioHooks[] = {
    {L"xio.dll", "?update@XIO@@QAEXXZ", (void *)hupdate, (void **)&oupdate, 0},
    {L"xio.dll", "?setSteeringBar@XIO@@QAEXE@Z", (void *)hsetSteeringBar, (void **)&osetSteeringBar, 0},
    {L"xio.dll", "?setVibration@XIO@@QAEXEH@Z", (void *)hsetVibration, (void **)&osetVibration, 0},
    {L"xio.dll", "?lamp@XIO@@QAEXE@Z", (void *)hlamp, (void **)&olamp, 0},
    {L"xio.dll", "?startLamp@XIO@@QAEXH@Z", (void *)hstartLamp, (void **)&ostartLamp, 0},
    {L"xio.dll", "?selectLamp@XIO@@QAEXH@Z", (void *)hselectLamp, (void **)&oselectLamp, 0},
};

// ------------------------------------------------------------------------------

int wl_game_uniana(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    wchar_t dir[MAX_PATH];
    wcsncpy_s(dir, MAX_PATH, exePath, base - exePath);
    if (_wcsicmp(base, L"xwin.exe") || !exists(dir, L"xio.dll"))
        return 0;
    *name = "frenzyexpress";
    *title = "Frenzy Express";
    wl_log("game: %s (Uniana PC)", *title);
    wl_game_set_boards("Controls on Uniana's board (through xio.dll)", "Motor board: the handlebar's motor (through xio.dll)");
    wl_inputs_set_table(g_inputs, IN_COUNT);
    g_out[OUT_LAMP] = wl_outputs_declare_value("lamp", "motif (1, 2, 4, 8)", "Lumières de la borne : le motif choisi par le jeu");
    g_out[OUT_START_LAMP] = wl_outputs_declare_value("start_lamp", "bit (0/1)", "Lumière du bouton Start");
    g_out[OUT_SELECT_LAMP] = wl_outputs_declare_value("select_lamp", "bit (0/1)", "Lumières des boutons de choix (gauche, droite)");
    g_out[OUT_VIBRATION] = wl_outputs_declare_value("vibration", "nombre", "Vibration du guidon (3 : au repos, la valeur de la bibliothèque)");
    g_out[OUT_MOTOR] = wl_outputs_declare_value("handlebar_motor", "nombre (0, 10-23)",
                                                "Moteur du guidon : 0 aucun, impair guidon à droite, pair à gauche, plus fort en montant");
    wl_hooks_add(g_xioHooks, sizeof g_xioHooks / sizeof g_xioHooks[0]);
    MEMORY_BASIC_INFORMATION mi;
    if (VirtualQuery((const void *)FE_CALIBRATE, &mi, sizeof mi) && mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
        !memcmp((const void *)FE_CALIBRATE, g_calibrateStart, sizeof g_calibrateStart))
        wl_hook_code((void *)FE_CALIBRATE, (void *)hcalibrate, (void **)&ocalibrate, "xwin.exe handlebar calibration");
    else
        wl_log("uniana: another xwin.exe (no handlebar calibration at +0x25c00): the calibration left alone");
    wl_gl_picture(640, 480);
    return 1;
}
