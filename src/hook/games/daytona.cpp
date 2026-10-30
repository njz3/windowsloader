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

// Daytona Championship USA (Sega Amusements, 2017) and its New Season Edition (2019),
// Sega\Daytona\Daytona.exe, x86, Direct3D 11. Built on Sega Racing Classic's code, which keeps
// Daytona USA's Model 2 inputs. Both editions do the same things at other addresses (addresses
// below are the 2017 edition's); windowsloader only plays the cabinet around the game:
//
// - The I/O board is a DirectInput joystick named "SEGA IO Controller" (+0x1d9bb0): the game
//   takes it by name, sets every axis to -1000..1000 and reads a DIJOYSTATE (+0x7cdf0). X is the
//   steering, against [CalibrationData] SteeringLeft / SteeringCentre / SteeringRight of
//   ShellData.ini (0..255 units); Rx the accelerator and Z the brake, uncalibrated (+0x1ddd80
//   puts them in Model 2 order: steering, accelerator, brake). Its buttons, by the game's table
//   (+0xd5c490): 0 Start, 1 2 6 7 the H shifter's gears 1 to 4, 3 gear up, 4 gear down, 8 to 11
//   the view buttons VR1 to VR4 (red, blue, yellow, green). vdinput.cpp plays it.
//
// - The wheel motor is on the parallel port, written by c:\sega\shell\InpWrapper.exe, which the
//   game starts with "-h=<its window> -p=<port>" (+0x128b70). InpWrapper sends the game's window
//   WM_COPYDATA type 3 with its own window handle (+0xdcd80 takes it), then the game sends it
//   WM_COPYDATA type 0x20 with one byte (+0x129570) that goes to the port: 0 for no force, else
//   the low nibble is the strength, 1 the strongest to 15 the weakest, bit 4 set when it pushes
//   the wheel to the right (the centring force +0x1292a0 pushes left a wheel turned right).
//   windowsloader is that InpWrapper: a hidden window of its own, nothing is started.
//
// - Coins, credits, the test menu and the cabinet's security are Sega\shell\Shell.exe's. The game
//   talks to it over TCP on 127.0.0.1:18888 (+0x1826a0), frames of 11 bytes "DYT", the command
//   and the payload length (both big endian), 4 zero bytes, then the payload. The game reports
//   what it does (GAME_MSG_*), the shell sets the credits (SHELL_MSG_SETCREDIT) and answers a
//   challenge sent every 10 s (GAME_MSG_SECURITY_CHALLENGE, 4 bytes): the game quits after five
//   left unanswered (+0x182a00). The New Season Edition also waits for the cabinet's SKU
//   (SHELL_MSG_SET_SKU) before leaving its "network discovery" screen (+0xdd18b; the first edition
//   ignores that message). windowsloader is that shell (segashell.cpp).
//
// The game's settings are left as they are; it only reads some of them differently: no
// exclusive full screen ([FullHD] ENABLE_FULL_SCREEN of GameData\config.ini) and the cabinets of
// the link ([Network] NumCabinets and CabinetID of ShellData.ini: 1 and 1 alone, else from
// [Game.daytona3] LINK and CABINET, see the link below). The camera (the players' photos) is
// refused by system.cpp.

#include <winsock2.h>
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "camera.h"
#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "lang.h"
#include "log.h"
#include "outputs.h"
#include "segashell.h"
#include "vdinput.h"

// ------------------------------------------------------------------------------
// The I/O board
// ------------------------------------------------------------------------------

enum
{
    IN_STEERING,
    IN_ACCEL,
    IN_BRAKE,
    IN_START,
    IN_VIEW1,
    IN_VIEW2,
    IN_VIEW3,
    IN_VIEW4,
    IN_GEAR1,
    IN_GEAR2,
    IN_GEAR3,
    IN_GEAR4,
    IN_GEARUP,
    IN_GEARDOWN,
    IN_COIN,    // the shell's
    IN_SERVICE, // the shell's service credit
    IN_COUNT
};

static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 10, 245, 127},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 255, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 255, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"View1", "View1", WL_INPUT_SWITCH, 0, 1, 0},
    {"View2", "View2", WL_INPUT_SWITCH, 0, 1, 0},
    {"View3", "View3", WL_INPUT_SWITCH, 0, 1, 0},
    {"View4", "View4", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear1", "Gear1", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear2", "Gear2", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear3", "Gear3", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear4", "Gear4", WL_INPUT_SWITCH, 0, 1, 0},
    {"GearUp", "GearUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"GearDown", "GearDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "P1_Service", WL_INPUT_SWITCH, 0, 1, 0},
};

// The board's button of each input
static const struct
{
    int input, button;
} g_buttons[] = {
    {IN_START, 0}, {IN_GEAR1, 1}, {IN_GEAR2, 2},  {IN_GEARUP, 3},  {IN_GEARDOWN, 4}, {IN_GEAR3, 6},
    {IN_GEAR4, 7}, {IN_VIEW1, 8}, {IN_VIEW2, 9}, {IN_VIEW3, 10}, {IN_VIEW4, 11},
};

static unsigned short axis_of(int raw)
{
    if (raw < 0)
        raw = 0;
    if (raw > 255)
        raw = 255;
    return (unsigned short)(raw * 257);
}

// The game reads X 0..255 (in -1000..1000) against its calibration, and the pedals released at -1000
static void fill_board(unsigned short axes[WL_VDINPUT_AXES], unsigned char *buttons)
{
    axes[0] = axis_of(wl_input_get(&g_inputs[IN_STEERING]));
    axes[2] = axis_of(wl_input_get(&g_inputs[IN_BRAKE]));
    axes[3] = axis_of(wl_input_get(&g_inputs[IN_ACCEL]));
    for (int i = 0; i < (int)(sizeof g_buttons / sizeof g_buttons[0]); i++)
        buttons[g_buttons[i].button] = wl_input_get(&g_inputs[g_buttons[i].input]) != 0;
}

static const WlVirtualJoystick g_board = {
    "SEGA IO Controller",
    0x0CA3, // Sega's USB vendor ID; the game checks only the name
    0x0001,
    12,
    fill_board,
};

// The steering range follows the cabinet's calibration, so the full travel of a controller
// covers what the game expects
static void read_calibration(const wchar_t *gameDir)
{
    wchar_t ini[MAX_PATH];
    swprintf(ini, MAX_PATH, L"%s..\\ShellData\\ShellData.ini", gameDir);
    WlInput *s = &g_inputs[IN_STEERING];
    s->rawLow = GetPrivateProfileIntW(L"CalibrationData", L"SteeringLeft", 0, ini);
    s->rawHigh = GetPrivateProfileIntW(L"CalibrationData", L"SteeringRight", 255, ini);
    s->rawRest = GetPrivateProfileIntW(L"CalibrationData", L"SteeringCentre", 127, ini);
    wl_log("  %s: %d..%d, centre %d", s->name, s->rawLow, s->rawHigh, s->rawRest);
}

// ------------------------------------------------------------------------------
// InpWrapper.exe: the wheel motor
// ------------------------------------------------------------------------------

enum
{
    COPYDATA_WRAPPER_WINDOW = 3, // InpWrapper to the game: its window
    COPYDATA_PORT_BYTE = 0x20,   // the game to InpWrapper: one byte for the parallel port
};

static HWND g_gameWindow;
static unsigned g_port;
static volatile LONG g_boardEffects; // the board's commands played (below): the port's byte only logged

static void motor_byte(unsigned char b)
{
    static int last = -1, logged;
    int q = b & 0x0F;
    double f = q ? (15 - q) / 14.0 : 0;
    if (b & 0x10)
        f = -f; // to the right
    if (!g_boardEffects)
        wl_ffb_set(f);
    if (b != last && logged < 40)
    {
        logged++;
        wl_log("motor: port 0x%x <- 0x%02x, force %+.2f", g_port, b, f);
    }
    last = b;
}

// ------------------------------------------------------------------------------
// The motor board's commands. The game is Daytona USA's Model 2 program made again: each frame it
// still fills a Model 2 cabinet's outputs, the lamps' byte and the drive board's command (the
// function at +0x1de100 in the 2017 program keeps a copy of each beside its other Model 2 bytes);
// its src\soc\forcefeedback.cpp (+0x128c50) turns the command into the port's byte, a centring made
// of constant forces a frame late which, with a cabinet's settings (GameData\config.ini [Wheel]
// FORCE2_N 1, PERIOD1 0.1375: 0.86 at 14 % of the steering), swings a strong wheel from one side to
// the other. Here the drive board itself is played, from that command and the wheel's position,
// as sm2-emu plays Daytona USA's (its reading of the board's program, EPROM EPR-16488A: written
// again for windowsloader from sm2-emu's description, github.com/dmanlfc/sm2-emu, Daniel Martin
// (dmanlfc) and the sm2-emu contributors, GPL v3), with the cabinet's panel spring:
//   0x        the forces on (0x0A, or a low nibble of 5 or more) or off
//   1x        nothing; 20-27 hold: a brake against the wheel's movement; 28-2F nothing
//   30-37     centring: dead within 2 steps of the board's 128 a side, full at 0x15
//   38-3F     the same, dead within 7, full at 0x18
//   40-47     uncentring: away from the centre; 50-57 to the left; 60-67 to the right
//   7x        a boost of 2 to 9; 8x to Fx the board's own requests (no force)
// The board's power follows its target by 1 a millisecond, at most 63, as a constant force (or a
// brake when it has no side); the panel's spring is added. All the other way when the race says so
// (its flag at +0x10 of its settings: the steering the game reads is turned over too). The command
// and the lamps also go to the network outputs, as MAME sends Daytona USA's (RawDrive, RawLamps).
// ------------------------------------------------------------------------------

struct DaytonaBuild
{
    DWORD stamp;   // the program's PE time stamp
    DWORD command; // the copy of the drive board's command, from the program's base
    DWORD lamps;   // the copy of the lamps' byte
    DWORD race;    // a pointer to the race's settings, the mirrored flag at +0x10
    DWORD camera;  // cv::VideoCapture::VideoCapture(int), OpenCV 2.4 linked in the program
    const char *name;
};

static const DaytonaBuild g_builds[] = {
    {0x5a3b3168, 0x15b4684, 0x15b4683, 0x11b0148, 0x642df0, "Daytona Championship USA"},
    {0x5c7f7953, 0x15c8804, 0x15c8803, 0x11c22a0, 0x6484f0, "Daytona Championship USA - New Season Edition"},
};
static const DaytonaBuild *g_build;
static volatile LONG g_dip, g_panel = 20; // [Game.daytona3] DRIVE_DIP (0..7), PANEL_SPRING (percent)

// The game's memory, read without failing when it is not there (yet)
static int read_byte(const BYTE *p, BYTE *out)
{
    __try
    {
        *out = *(volatile const BYTE *)p;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

static int read_dword(const BYTE *p, DWORD *out)
{
    __try
    {
        *out = *(volatile const DWORD *)p;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

static int mirrored_race(const BYTE *base)
{
    DWORD p = 0;
    BYTE b = 0;
    return read_dword(base + g_build->race, &p) && p && read_byte((const BYTE *)(UINT_PTR)p + 0x10, &b) && b;
}

enum
{
    SIDE_NONE,
    SIDE_LEFT,
    SIDE_RIGHT
};

struct DriveBoard
{
    int on, effect, boost;
    int side, power, target;  // the output: a side, a power 0..255 (63 at most out)
    double tick;              // milliseconds, the board's interrupts
    double leftCentre;        // when the wheel last left the centre
    double sinceCentre, away; // how long it has been at the centre, away from it
    int atCentre, swings, swinging;
};

// On from the start: the copy is read once a frame and a 0x0A followed by an effect in the same
// frame would be missed (Daytona Championship USA's own translation has no on and off)
static DriveBoard g_board_ = {1};

// The cabinet's panel spring, -1..1 (> 0 pushing left), from the wheel's deflection (-32767..32767,
// > 0 to the right): nothing within 1500, then up to full at 15500
static double panel_spring(double deflection, double full)
{
    double a = deflection < 0 ? -deflection : deflection;
    double f = a <= 1500 ? 0 : a >= 15500 ? full : full * (a - 1500) / 14000;
    return deflection > 0 ? f : -f;
}

static void board_byte(DriveBoard *b, BYTE c)
{
    switch (c & 0xF0)
    {
    case 0x00:
        b->on = c == 0x0A || (c & 7) >= 5;
        if (!b->on)
            b->boost = 0;
        break;
    case 0x70:
        b->boost = ((c & 0x0E) >> 1) + 2;
        break;
    case 0x10:
    case 0x20:
    case 0x30:
    case 0x40:
    case 0x50:
    case 0x60:
        b->effect = c;
        break;
    default: // the board's requests: no force
        break;
    }
}

// A centring spring's power: dead within dead steps, full at full
static int spring_power(const DriveBoard *b, int off, int dead, int full)
{
    int force = 2 * (b->effect & 7) + 4 * g_dip, slope = force >= 0x1E ? 3 : 2;
    if (off >= full)
        return force;
    int p = slope * (off - dead) - b->boost;
    return p < 0 ? 0 : p > force ? force : p;
}

// The wheel through the centre: ten quick swings and the springs let go, until it rests
static void track_swings(DriveBoard *b, int centre, double ms)
{
    if (centre)
    {
        if (!b->atCentre)
        {
            double out = b->tick - b->leftCentre;
            b->swings = out >= 0x31 && out <= 0x125 ? b->swings + 1 : 0;
            if (b->swings >= 10)
                b->swinging = 1;
            b->sinceCentre = 0;
        }
        b->sinceCentre += ms;
        b->away = 0;
        if (b->sinceCentre >= 0x62)
            b->swinging = b->swings = 0;
    }
    else
    {
        if (b->atCentre)
            b->leftCentre = b->tick;
        b->away += ms;
        if (b->away >= (b->swinging ? 0x5B9 : 0x125))
            b->swinging = b->swings = 0;
    }
    b->atCentre = centre;
}

// One step of the board, ms milliseconds after the last: its side and target, then its power
static void board_step(DriveBoard *b, int counts, double ms, int breakaway)
{
    b->tick += ms;
    int off = counts < 0 ? -counts : counts;
    if (off > 128)
        off = 128;
    int side = counts >= 3 ? SIDE_RIGHT : counts <= -3 ? SIDE_LEFT : SIDE_NONE;
    int base = off / 16 + breakaway + b->boost, n = b->effect & 7, dip = 4 * g_dip;
    int newSide = SIDE_NONE, target = 0;
    track_swings(b, side == SIDE_NONE, ms);
    int e = b->effect & 0xF8;
    if (!b->on || (b->effect & 0xF0) == 0x10)
        ;
    else if (e == 0x20)
        target = 2 * n + dip + base; // the hold: a brake, no side
    else if ((e == 0x30 || e == 0x38) && !b->swinging)
    {
        int dead = e == 0x30 ? 2 : 7, full = e == 0x30 ? 0x15 : 0x18;
        if (off > dead)
        {
            newSide = counts > 0 ? SIDE_LEFT : SIDE_RIGHT; // to the centre
            target = spring_power(b, off, dead, full) + base;
        }
        else if (off >= 1)
            newSide = b->side, target = breakaway; // the centre: the side kept
    }
    else if (e == 0x40)
        newSide = side == SIDE_RIGHT ? SIDE_RIGHT : SIDE_LEFT, target = 2 * n + dip + base + 10;
    else if (e == 0x50 || e == 0x60)
        newSide = e == 0x50 ? SIDE_LEFT : SIDE_RIGHT, target = 2 * n + dip + base + 10;
    if ((e == 0x40 || e == 0x50 || e == 0x60) && b->side != SIDE_NONE && newSide != b->side)
        b->power = 0; // a push the other way starts again from nothing
    b->side = newSide;
    b->target = target < 0 ? 0 : target > 255 ? 255 : target;
    int step = (int)(ms + 0.5);
    if (b->power < b->target)
        b->power = b->power + step > b->target ? b->target : b->power + step;
    else if (b->power > b->target)
        b->power = b->power - step < b->target ? b->target : b->power - step;
}

static DWORD WINAPI board_thread(LPVOID)
{
    const BYTE *base = (const BYTE *)GetModuleHandleW(NULL);
    const WlInput *steering = &g_inputs[IN_STEERING];
    int lastCommand = -1, lastLamps = -1, logged = 0;
    LARGE_INTEGER freq, last, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    double lastDeflection = 0, sent = 0;
    for (;;)
    {
        Sleep(4);
        QueryPerformanceCounter(&now);
        double ms = (now.QuadPart - last.QuadPart) * 1000.0 / freq.QuadPart;
        last = now;
        BYTE command, lamps;
        if (!read_byte(base + g_build->command, &command) || !read_byte(base + g_build->lamps, &lamps))
            continue;
        // The network outputs, as MAME's Daytona USA: the drive board's bytes and the lamps
        if (command != lastCommand)
        {
            wl_outputs_drive(&command, 1);
            if (g_boardEffects && logged < 60)
            {
                logged++;
                wl_log("motor: board command 0x%02x", command);
            }
        }
        if (lamps != lastLamps)
            wl_outputs_lamps(lamps, 8);
        lastCommand = command;
        lastLamps = lamps;
        if (!g_boardEffects)
            continue;

        // The wheel as the game reads it, -32767..32767 to the right; turned over on a mirrored
        // race's, as the game turns over its steering
        int raw = wl_input_get(steering), half = raw >= steering->rawRest ? steering->rawHigh - steering->rawRest
                                                                         : steering->rawRest - steering->rawLow;
        double deflection = half > 0 ? (raw - steering->rawRest) * 32767.0 / half : 0;
        int mirrored = mirrored_race(base);
        double seen = mirrored ? -deflection : deflection;
        int counts = (int)(seen * 128 / 32767);
        // The panel spring at full strength (the board's power 63 is 1): PANEL_SPRING percent of
        // Sega Rally's strongest torque (32), which the board's start finds its breakaway against
        double panelFull = 32.0 / 63 * g_panel / 100;
        int breakaway = (int)ceil(panel_spring(16 * 32767.0 / 128, panelFull) * 63 - 1e-9) - 1;
        if (breakaway < 0)
            breakaway = 0;
        board_byte(&g_board_, command);
        board_step(&g_board_, counts, ms, breakaway);
        int power = g_board_.power > 0x3F ? 0x3F : g_board_.power;
        double torque = 0;
        if (g_board_.side != SIDE_NONE)
            torque = power / 63.0 * (g_board_.side == SIDE_LEFT ? 1 : -1) * (mirrored ? -1 : 1);
        else if (power > 0)
        {
            // A brake against the movement: the axis's change in a frame of the game (57.5 Hz)
            double v = ms > 0 ? (deflection - lastDeflection) * (1000.0 / 57.5245) / ms : 0;
            v = v < -1024 ? -1024 : v > 1024 ? 1024 : v;
            torque = power / 63.0 * v / 1024;
        }
        lastDeflection = deflection;
        // Sent when it changes by 256 in 32767 at least, or reaches or leaves 0 (sm2-emu's way):
        // the brake's noise does not flood the wheel's driver
        double out = torque + panel_spring(deflection, panelFull);
        if (fabs(out - sent) * 32767 >= 256 || (out == 0) != (sent == 0))
        {
            sent = out;
            wl_ffb_set(out);
        }
    }
}

// [Game.daytona3] FFB: BOARD the drive board played, PORT the port's byte (the game's own centring)
static void apply_ffb(const char *value)
{
    LONG board = g_build && _stricmp(value, "PORT") != 0;
    if (InterlockedExchange(&g_boardEffects, board) != board)
    {
        wl_ffb_set(0);
        wl_log("motor: force feedback from %s", board ? "the drive board played from the game's commands" : "the port's byte");
    }
}

static void apply_dip(const char *value)
{
    int v = atoi(value);
    InterlockedExchange(&g_dip, v < 0 ? 0 : v > 7 ? 7 : v);
}

static void apply_panel(const char *value)
{
    int v = atoi(value);
    InterlockedExchange(&g_panel, v < 0 ? 0 : v > 100 ? 100 : v);
}

static const WlOptionChoice g_ffbChoices[] = {
    {"BOARD", WL_N("Carte moteur de la borne")},
    {"PORT", WL_N("Octet du port parallèle")},
    {NULL, NULL},
};

static const WlGameOption g_options[] = {
    {"FFB", WL_N("Retour de force"),
     WL_N("Carte moteur de la borne : la carte de Daytona USA jouée d'après les commandes du jeu et la position du volant, avec le "
          "ressort du panneau (comme sm2-emu). Octet du port parallèle : la force que le jeu envoie au moteur, son centrage fait de "
          "forces constantes d'une image à l'autre, qui peut faire osciller un volant puissant."),
     WL_OPTION_CHOICE, g_ffbChoices, 0, 0, "BOARD", 0, apply_ffb},
    {"DRIVE_DIP", WL_N("Puissance de la carte (interrupteurs)"),
     WL_N("Les interrupteurs de la carte moteur : chaque cran ajoute 4 à la puissance des effets."), WL_OPTION_INT, NULL, 0, 7, "0", 0,
     apply_dip},
    {"PANEL_SPRING", WL_N("Ressort du panneau (%)"),
     WL_N("Le ressort mécanique du volant de la borne, qui le ramène au centre sans le moteur."), WL_OPTION_INT, NULL, 0, 100, "20", 0,
     apply_panel},
    {"CABINET", WL_N("Numéro de la borne (link)"),
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "
          "dans le fichier)."),
     WL_OPTION_INT, NULL, 0, 8, "0", 1, NULL},
    WL_CAMERA_OPTIONS,
};

static void read_options(void)
{
    char v[16];
    wl_game_option("DRIVE_DIP", v, sizeof v);
    apply_dip(v);
    wl_game_option("PANEL_SPRING", v, sizeof v);
    apply_panel(v);
    wl_game_option("FFB", v, sizeof v);
    apply_ffb(v);
}

// The Model 2 lamps' byte, as MAME names Daytona USA's (bits 2 to 7)
static const char *g_lampNames[8] = {"-", "-", "Start", "VR1 (red)", "VR2 (blue)", "VR3 (yellow)", "VR4 (green)", "Leader"};

// The program's build, by its PE time stamp: the commands are read only from a known one
static void find_build(void)
{
    const BYTE *base = (const BYTE *)GetModuleHandleW(NULL);
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    DWORD stamp = nt->FileHeader.TimeDateStamp;
    for (size_t i = 0; i < sizeof g_builds / sizeof g_builds[0]; i++)
        if (g_builds[i].stamp == stamp)
            g_build = &g_builds[i];
    if (!g_build)
    {
        wl_log("motor: unknown build (time stamp %08lx): the port's byte only", stamp);
        return;
    }
    wl_log("motor: %s build %08lx, the drive board's command at +0x%lx", g_build->name, stamp, g_build->command);
    wl_outputs_declare_drive();
    wl_outputs_name_lamps(WL_N("la borne Model 2 (lampes)"), g_lampNames, 8);
    HANDLE t = CreateThread(NULL, 0, board_thread, NULL, 0, NULL);
    if (t)
        CloseHandle(t);
}

static LRESULT CALLBACK wrapper_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COPYDATA)
    {
        const COPYDATASTRUCT *cd = (const COPYDATASTRUCT *)lp;
        if (cd && cd->dwData == COPYDATA_PORT_BYTE && cd->cbData >= 1 && cd->lpData)
            motor_byte(*(const unsigned char *)cd->lpData);
        return 1; // done (2 would ask the game to send it again)
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static DWORD WINAPI wrapper_thread(LPVOID)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wrapper_proc;
    wc.hInstance = g_wlModule;
    wc.lpszClassName = "windowsloader InpWrapper";
    RegisterClassA(&wc);
    HWND w = CreateWindowExA(0, wc.lpszClassName, "InpWrapper", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, g_wlModule, NULL);
    if (!w)
    {
        wl_log("motor: no window for InpWrapper (error %lu), no force feedback", GetLastError());
        return 0;
    }
    // As InpWrapper does it: its window handle in 8 bytes, until the game takes it
    DWORD handle[2] = {(DWORD)(UINT_PTR)w, 0};
    COPYDATASTRUCT cd = {COPYDATA_WRAPPER_WINDOW, sizeof handle, handle};
    for (;;)
    {
        DWORD_PTR result = 0;
        if (SendMessageTimeoutA(g_gameWindow, WM_COPYDATA, 0, (LPARAM)&cd, SMTO_ABORTIFHUNG, 10000, &result) && result == 1)
            break;
        if (!IsWindow(g_gameWindow))
        {
            wl_log("motor: the game's window %p is gone, no force feedback", g_gameWindow);
            return 0;
        }
        Sleep(500);
    }
    wl_log("motor: the game sends its port writes to windowsloader's window %p", w);
    MSG m;
    while (GetMessageA(&m, NULL, 0, 0) > 0)
        DispatchMessageA(&m);
    return 0;
}

// The game starting c:\sega\shell\InpWrapper.exe "-h=<window> -p=<port>": windowsloader plays it
static int process_handler(const char *app, const char *cmd)
{
    size_t n = strlen(app);
    if (n < 14 || _stricmp(app + n - 14, "InpWrapper.exe"))
        return 0;
    const char *h = strstr(cmd, "-h="), *p = strstr(cmd, "-p=");
    if (!h || g_gameWindow)
        return 1;
    g_gameWindow = (HWND)(UINT_PTR)strtoul(h + 3, NULL, 16);
    g_port = p ? (unsigned)strtoul(p + 3, NULL, 16) : 0;
    wl_log("motor: InpWrapper for window %p, parallel port 0x%x", g_gameWindow, g_port);
    CloseHandle(CreateThread(NULL, 0, wrapper_thread, NULL, 0, NULL));
    return 1;
}

// ------------------------------------------------------------------------------
// Shell.exe: credits and the security challenge
// ------------------------------------------------------------------------------

enum
{
    GAME_MSG_KEEPALIVE = 0x00,
    GAME_MSG_START_PRESSED = 0x06,
    GAME_MSG_SECURITY_CHALLENGE = 0x2a,
    SHELL_MSG_SET_SKU = 0x2c,
    SHELL_MSG_SETCREDIT = 0x2d,
    SHELL_MSG_SECURITY_AUTHENTICATE = 0x2e,
    MSG_COUNT = 0x37
};

// The names the game itself logs its messages with (+0x11319c0)
static const char *const g_msgNames[MSG_COUNT] = {
    "GAME_MSG_KEEPALIVE", "GAME_MSG_LAUNCHED_OK", "GAME_MSG_ASSETS_LOADED", "GAME_MSG_ATTRACT_START", "GAME_MSG_ATTRACT_LOGO",
    "GAME_MSG_ATTRACT_VIDEO", "GAME_MSG_START_PRESSED", "GAME_MSG_MODE_ENTERED", "GAME_MSG_MODE_SELECTED", "GAME_MSG_WAITING_FOR_CHALLENGERS",
    "GAME_MSG_ENTRY_CLOSED", "GAME_MSG_PLAYERS_IN_GAME", "GAME_MSG_TRACK_SELECT_ENTERED", "GAME_MSG_TRACK_SELECT_CHANGED",
    "GAME_MSG_TRACK_SELECT_CHOSEN", "GAME_MSG_CAR_SELECT_ENTERED", "GAME_MSG_CAR_SELECT_CHANGED", "GAME_MSG_CAR_SELECT_CHOSEN", "0x12", "0x13",
    "GAME_MSG_TRANSMISSION_SELECT_CHOSEN", "GAME_MSG_TRACK_LOADING", "GAME_MSG_ROLLING_START", "GAME_MSG_COUNTDOWN", "GAME_MSG_CURRENT_POSITIONS",
    "GAME_MSG_WALL_IMPACT", "GAME_MSG_PLAYER_IMPACT", "GAME_MSG_OVERTAKE_STATUS", "GAME_MSG_PIT_STATUS", "GAME_MSG_TIME_EXTENDED",
    "GAME_MSG_TIME_OUT", "GAME_MSG_GEAR_SHIFT", "GAME_MSG_LAP_COMPLETED", "GAME_MSG_RACE_LEADER", "GAME_MSG_RACE_OVER", "GAME_MSG_FINAL_RESULTS",
    "GAME_MSG_GAME_OVER", "GAME_MSG_HIGHSCORE_ACHIEVED", "GAME_MSG_HIGHSCORE_ENTRY", "GAME_MSG_HIGHSCORE_VALUE", "GAME_MSG_INITIAL_CURRENT",
    "GAME_MSG_INITIAL_CONFIRMED", "GAME_MSG_SECURITY_CHALLENGE", "GAME_MSG_SHUTDOWN", "SHELL_MSG_SET_SKU", "SHELL_MSG_SETCREDIT",
    "SHELL_MSG_SECURITY_AUTHENTICATE", "SHELL_MSG_RESET", "SHELL_MSG_EXIT", "SHELL_MSG_PARTY_MODE_REQUEST", "SHELL_MSG_PARTY_MODE_START",
    "SHELL_MSG_PARTY_MODE_END", "GAME_MSG_PARTY_MODE_ACKNOWLEDGE", "GAME_MSG_CONTINUE_OFFERED", "GAME_MSG_CONTINUE_CHOSEN",
};

static WlSegaShell g_shell = {
    "DYT", g_msgNames, MSG_COUNT, GAME_MSG_SECURITY_CHALLENGE, SHELL_MSG_SECURITY_AUTHENTICATE, SHELL_MSG_SETCREDIT, SHELL_MSG_SET_SKU,
    GAME_MSG_START_PRESSED, -1, -1, 0, &g_inputs[IN_COIN], &g_inputs[IN_SERVICE],
};

// ------------------------------------------------------------------------------
// The link between cabinets
// ------------------------------------------------------------------------------

// The game finds the other cabinets on its own network, 192.168.1.0/24, written in its code
// (src\soc\ping.cpp; addresses of the 2017 edition, the NSE does the same): its address is the
// last byte of gethostbyname(gethostname()) (+0x1758f0); cabinet 1 ([Network] CabinetID) is the
// main PC, it tries 192.168.1.1 to .8 but its own on TCP 26000 (+0x174d00, +0x176ad0, again every
// 20 ms until it answers); each other cabinet listens there on its address (+0x175770), answers
// its CabinetID, and the main PC sends them all the cabinets' addresses ("Create network group").
// The race then goes between those addresses: a heart beat (TCP, +0x12a5e0, on its address),
// the ranking (TCP 5888, +0x17b840), a broadcast on 192.168.1.255 (UDP, picomm.cpp +0x173fb0).
// For windowsloader cabinet N is 192.168.1.N; [Game.daytona3] LINK lists the cabinets' real
// addresses (cabinet 1's first), CABINET is this one's number (0: the first of LINK's addresses
// that is this PC's). The game's sockets (its import table) carry it between them:
//   gethostbyname(this PC's name)         -> 192.168.1.CABINET
//   bind 192.168.1.CABINET                -> its real address, the same port (another .x: refused)
//   connect, sendto 192.168.1.N           -> cabinet N's real address; the other .x unreachable
//   sendto 192.168.1.255, 255.255.255.255 -> each cabinet's real address, its own too
//   accept, recvfrom, getpeername, getsockname of cabinet N's real address -> 192.168.1.N
// Anything else is left to system.cpp, which keeps the game off the local network. Without LINK
// the cabinet is alone, cabinet 1 of 1: a second cabinet's file (CabinetID = 2, the link's
// identity from TeknoParrot) made it wait for cabinet 1 on "network discovery" (racecab2).
#define LINK_MAX 8
#define LINK_NET 0x0001A8C0u // 192.168.1.0, in network order
#define LINK_MASK 0x00FFFFFFu

static int g_cabinet = 1, g_linkCount; // 0: alone
static ULONG g_linkAddress[LINK_MAX];  // the cabinets' real addresses, network order

static ULONG link_virtual(int cabinet)
{
    return LINK_NET | (ULONG)cabinet << 24;
}

// The cabinet a game's address is, 0 for none
static int link_cabinet_of(ULONG ip)
{
    int n = (int)(ip >> 24);
    return (ip & LINK_MASK) == LINK_NET && n >= 1 && n <= g_linkCount ? n : 0;
}

// The cabinet a real address is, 0 for none
static int link_cabinet_real(ULONG ip)
{
    for (int i = 0; i < g_linkCount; i++)
        if (g_linkAddress[i] == ip)
            return i + 1;
    return 0;
}

static void link_log(const char *what, const sockaddr *a, const sockaddr *to, int r, DWORD err)
{
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) > 200)
        return;
    const unsigned char *b = (const unsigned char *)&((const sockaddr_in *)a)->sin_addr, *c = (const unsigned char *)&((const sockaddr_in *)to)->sin_addr;
    wl_log("daytona: link %s %u.%u.%u.%u:%u -> %u.%u.%u.%u: %d (%lu)", what, b[0], b[1], b[2], b[3], ntohs(((const sockaddr_in *)a)->sin_port),
           c[0], c[1], c[2], c[3], r, r < 0 ? err : 0);
}

static int is_inet(const sockaddr *a, int len)
{
    return a && len >= (int)sizeof(sockaddr_in) && a->sa_family == AF_INET;
}

// An address the game is given back (accept, recvfrom...): a cabinet's real one becomes its own
static void link_to_game(sockaddr *a, const int *len)
{
    if (!g_linkCount || !len || !is_inet(a, *len))
        return;
    sockaddr_in *in = (sockaddr_in *)a;
    if (int n = link_cabinet_real(in->sin_addr.s_addr))
        in->sin_addr.s_addr = link_virtual(n);
}

static hostent *(WSAAPI *olink_gethostbyname)(const char *);
static hostent *WSAAPI link_gethostbyname(const char *name)
{
    char own[256];
    wl_busy_enter();
    int mine = g_linkCount && name && !gethostname(own, sizeof own) && !_stricmp(name, own);
    wl_busy_leave();
    if (!mine)
        return olink_gethostbyname(name);
    static ULONG address;
    static char *list[2] = {(char *)&address, NULL};
    static char *aliases[1] = {NULL};
    static char hostName[256];
    static hostent h;
    address = link_virtual(g_cabinet);
    snprintf(hostName, sizeof hostName, "%s", own);
    h.h_name = hostName;
    h.h_aliases = aliases;
    h.h_addrtype = AF_INET;
    h.h_length = 4;
    h.h_addr_list = list;
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 3)
        wl_log("daytona: link: gethostbyname(\"%s\") -> 192.168.1.%d", name, g_cabinet);
    return &h;
}

static int(WSAAPI *olink_bind)(SOCKET, const sockaddr *, int);
static int WSAAPI link_bind(SOCKET s, const sockaddr *a, int len)
{
    if (!g_linkCount || !is_inet(a, len) || (((const sockaddr_in *)a)->sin_addr.s_addr & LINK_MASK) != LINK_NET)
        return olink_bind(s, a, len);
    sockaddr_in to;
    memcpy(&to, a, sizeof to);
    if (to.sin_addr.s_addr != link_virtual(g_cabinet))
    {
        link_log("bind refused", a, a, -1, WSAEADDRNOTAVAIL);
        WSASetLastError(WSAEADDRNOTAVAIL);
        return SOCKET_ERROR;
    }
    to.sin_addr.s_addr = g_linkAddress[g_cabinet - 1];
    wl_busy_enter(); // the cabinet's real address: not refused by system.cpp
    int r = olink_bind(s, (const sockaddr *)&to, sizeof to);
    DWORD err = WSAGetLastError();
    wl_busy_leave();
    link_log("bind", a, (const sockaddr *)&to, r, err);
    WSASetLastError(err);
    return r;
}

// The link's traffic, logged every 10 s at most: the packets and bytes this cabinet sent to and
// received from each (UDP by address, TCP by its connected sockets), and its TCP connections that
// end (closed, an error)
static volatile LONG g_sentPackets[LINK_MAX], g_sentBytes[LINK_MAX], g_receivedPackets[LINK_MAX], g_receivedBytes[LINK_MAX];

#define LINK_SOCKETS 64
static struct
{
    volatile SOCKET s;
    volatile LONG cabinet;
} g_linkSockets[LINK_SOCKETS];

static void link_socket(SOCKET s, int cabinet)
{
    int free = -1;
    for (int i = 0; i < LINK_SOCKETS; i++)
    {
        if (g_linkSockets[i].cabinet && g_linkSockets[i].s == s)
        {
            g_linkSockets[i].cabinet = cabinet;
            return;
        }
        if (!g_linkSockets[i].cabinet && free < 0)
            free = i;
    }
    if (free >= 0)
    {
        g_linkSockets[free].s = s;
        g_linkSockets[free].cabinet = cabinet;
    }
}

static int link_socket_cabinet(SOCKET s)
{
    for (int i = 0; i < LINK_SOCKETS; i++)
        if (g_linkSockets[i].cabinet && g_linkSockets[i].s == s)
            return g_linkSockets[i].cabinet;
    return 0;
}

static void link_count(int cabinet, int sent, int bytes)
{
    if (cabinet < 1 || cabinet > LINK_MAX || bytes < 0)
        return;
    InterlockedIncrement(sent ? &g_sentPackets[cabinet - 1] : &g_receivedPackets[cabinet - 1]);
    InterlockedExchangeAdd(sent ? &g_sentBytes[cabinet - 1] : &g_receivedBytes[cabinet - 1], bytes);
    static volatile LONG last;
    LONG now = (LONG)GetTickCount(), was = last;
    if (now - was < 10000 || InterlockedCompareExchange(&last, now, was) != was)
        return;
    char line[600];
    int at = snprintf(line, sizeof line, "daytona: link traffic since the last count:");
    for (int i = 0; i < g_linkCount && at < (int)sizeof line; i++)
        at += snprintf(line + at, sizeof line - at, " cabinet %d: sent %ld (%ld bytes), received %ld (%ld bytes);", i + 1,
                       InterlockedExchange(&g_sentPackets[i], 0), InterlockedExchange(&g_sentBytes[i], 0),
                       InterlockedExchange(&g_receivedPackets[i], 0), InterlockedExchange(&g_receivedBytes[i], 0));
    wl_log("%s", line);
}

static void link_tcp_end(SOCKET s, int cabinet, const char *what, DWORD err)
{
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 100)
        wl_log("daytona: link TCP with cabinet %d (socket %Iu): %s (%lu)", cabinet, (ULONG_PTR)s, what, err);
}

static int(WSAAPI *olink_send)(SOCKET, const char *, int, int);
static int WSAAPI link_send(SOCKET s, const char *buf, int size, int flags)
{
    int r = olink_send(s, buf, size, flags);
    int n = g_linkCount ? link_socket_cabinet(s) : 0;
    if (n)
    {
        DWORD err = WSAGetLastError();
        if (r >= 0)
            link_count(n, 1, r);
        else if (err != WSAEWOULDBLOCK)
            link_tcp_end(s, n, "send failed", err);
        WSASetLastError(err);
    }
    return r;
}

static int(WSAAPI *olink_recv)(SOCKET, char *, int, int);
static int WSAAPI link_recv(SOCKET s, char *buf, int size, int flags)
{
    int r = olink_recv(s, buf, size, flags);
    int n = g_linkCount ? link_socket_cabinet(s) : 0;
    if (n)
    {
        DWORD err = WSAGetLastError();
        if (r > 0)
            link_count(n, 0, r);
        else if (r == 0)
            link_tcp_end(s, n, "closed by the other cabinet", 0);
        else if (err != WSAEWOULDBLOCK && err != WSAETIMEDOUT)
            link_tcp_end(s, n, "recv failed", err);
        else if (err == WSAETIMEDOUT)
        {
            static volatile LONG logged;
            if (InterlockedIncrement(&logged) <= 20)
                link_tcp_end(s, n, "recv timed out", err);
        }
        WSASetLastError(err);
    }
    return r;
}

static int(WSAAPI *olink_closesocket)(SOCKET);
static int WSAAPI link_closesocket(SOCKET s)
{
    if (int n = g_linkCount ? link_socket_cabinet(s) : 0)
    {
        link_tcp_end(s, n, "closed by this cabinet", 0);
        for (int i = 0; i < LINK_SOCKETS; i++)
            if (g_linkSockets[i].cabinet && g_linkSockets[i].s == s)
                g_linkSockets[i].cabinet = 0;
    }
    return olink_closesocket(s);
}

static int(WSAAPI *olink_connect)(SOCKET, const sockaddr *, int);
static int WSAAPI link_connect(SOCKET s, const sockaddr *a, int len)
{
    if (!g_linkCount || !is_inet(a, len) || (((const sockaddr_in *)a)->sin_addr.s_addr & LINK_MASK) != LINK_NET)
        return olink_connect(s, a, len);
    int n = link_cabinet_of(((const sockaddr_in *)a)->sin_addr.s_addr);
    if (!n)
    {
        // The main PC tries the 8 addresses every 20 ms: nobody there
        WSASetLastError(WSAECONNREFUSED);
        return SOCKET_ERROR;
    }
    sockaddr_in to;
    memcpy(&to, a, sizeof to);
    to.sin_addr.s_addr = g_linkAddress[n - 1];
    wl_busy_enter();
    int r = olink_connect(s, (const sockaddr *)&to, sizeof to);
    DWORD err = WSAGetLastError();
    wl_busy_leave();
    if (r == 0 || err == WSAEWOULDBLOCK)
        link_socket(s, n);
    // A refused try every 20 ms until the other cabinet listens: the first ones only
    static volatile LONG logged[LINK_MAX];
    if (r == 0 || err == WSAEWOULDBLOCK || InterlockedIncrement(&logged[n - 1]) <= 5)
        link_log("connect", a, (const sockaddr *)&to, r, err);
    WSASetLastError(err);
    return r;
}

static int(WSAAPI *olink_sendto)(SOCKET, const char *, int, int, const sockaddr *, int);
static int WSAAPI link_sendto(SOCKET s, const char *buf, int size, int flags, const sockaddr *a, int len)
{
    if (!g_linkCount || !is_inet(a, len))
        return olink_sendto(s, buf, size, flags, a, len);
    ULONG ip = ((const sockaddr_in *)a)->sin_addr.s_addr;
    int first, last;
    if (ip == INADDR_BROADCAST || ip == (LINK_NET | ~LINK_MASK))
        first = 1, last = g_linkCount;
    else if ((ip & LINK_MASK) == LINK_NET)
    {
        first = last = link_cabinet_of(ip);
        if (!first)
            return size; // nobody there: lost, as on a network
    }
    else
        return olink_sendto(s, buf, size, flags, a, len);
    sockaddr_in to;
    memcpy(&to, a, sizeof to);
    int r = size;
    DWORD err = 0;
    wl_busy_enter();
    for (int n = first; n <= last; n++)
    {
        to.sin_addr.s_addr = g_linkAddress[n - 1];
        int sent = olink_sendto(s, buf, size, flags, (const sockaddr *)&to, sizeof to);
        link_count(n, 1, sent);
        if (sent < 0 && first == last)
            r = sent, err = WSAGetLastError();
        static volatile LONG logged[LINK_MAX];
        if (InterlockedIncrement(&logged[n - 1]) <= 10)
            link_log("sendto", a, (const sockaddr *)&to, sent, WSAGetLastError());
    }
    wl_busy_leave();
    WSASetLastError(err);
    return r;
}

static int(WSAAPI *olink_recvfrom)(SOCKET, char *, int, int, sockaddr *, int *);
static int WSAAPI link_recvfrom(SOCKET s, char *buf, int size, int flags, sockaddr *from, int *fromLen)
{
    int r = olink_recvfrom(s, buf, size, flags, from, fromLen);
    if (r >= 0)
    {
        DWORD err = WSAGetLastError();
        if (g_linkCount && fromLen && is_inet(from, *fromLen))
            link_count(link_cabinet_real(((const sockaddr_in *)from)->sin_addr.s_addr), 0, r);
        link_to_game(from, fromLen);
        WSASetLastError(err);
    }
    return r;
}

static SOCKET(WSAAPI *olink_accept)(SOCKET, sockaddr *, int *);
static SOCKET WSAAPI link_accept(SOCKET s, sockaddr *a, int *len)
{
    SOCKET r = olink_accept(s, a, len);
    if (r != INVALID_SOCKET)
    {
        if (g_linkCount && len && is_inet(a, *len))
            if (int n = link_cabinet_real(((const sockaddr_in *)a)->sin_addr.s_addr))
                link_socket(r, n);
        link_to_game(a, len);
        if (g_linkCount && len && is_inet(a, *len))
            link_log("accept", a, a, 0, 0);
    }
    return r;
}

static int(WSAAPI *olink_getpeername)(SOCKET, sockaddr *, int *);
static int WSAAPI link_getpeername(SOCKET s, sockaddr *a, int *len)
{
    int r = olink_getpeername(s, a, len);
    if (!r)
        link_to_game(a, len);
    return r;
}

static int(WSAAPI *olink_getsockname)(SOCKET, sockaddr *, int *);
static int WSAAPI link_getsockname(SOCKET s, sockaddr *a, int *len)
{
    int r = olink_getsockname(s, a, len);
    if (!r)
        link_to_game(a, len);
    return r;
}

// The link's settings: LINK's addresses, CABINET the cabinet's number (0: the first of LINK's
// addresses that is this PC's)
static void link_settings(void)
{
    char list[256];
    wl_game_option("LINK", list, sizeof list);
    g_cabinet = wl_game_option_int("CABINET");
    char *next = NULL;
    for (char *p = strtok_s(list, ",; ", &next); p && g_linkCount < LINK_MAX; p = strtok_s(NULL, ",; ", &next))
    {
        unsigned b[4];
        char end;
        if (sscanf_s(p, "%u.%u.%u.%u%c", &b[0], &b[1], &b[2], &b[3], &end, 1) != 4 || b[0] > 255 || b[1] > 255 || b[2] > 255 || b[3] > 255 ||
            !b[0])
            wl_log("daytona: link: \"%s\" is not an address, left out", p);
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
            if (t != INVALID_SOCKET)
                closesocket(t);
        }
        wl_busy_leave();
        WSACleanup();
        if (g_cabinet < 1)
            wl_log("daytona: link: none of LINK's addresses is this PC's (CABINET not set): the cabinet stays alone");
    }
    if (g_linkCount && (g_cabinet < 1 || g_cabinet > g_linkCount))
    {
        if (g_cabinet >= 1)
            wl_log("daytona: link: CABINET %d is not one of LINK's %d addresses: the cabinet stays alone", g_cabinet, g_linkCount);
        g_linkCount = 0;
    }
    if (!g_linkCount)
    {
        g_cabinet = 1;
        wl_log("daytona: cabinet 1 of 1, alone");
        return;
    }
    ULONG ip = g_linkAddress[g_cabinet - 1];
    wl_log("daytona: link: cabinet %d of %d (192.168.1.%d), its real address %lu.%lu.%lu.%lu", g_cabinet, g_linkCount, g_cabinet, ip & 0xFF,
           ip >> 8 & 0xFF, ip >> 16 & 0xFF, ip >> 24);
}

static void link_patch(void)
{
    HMODULE exe = GetModuleHandleW(NULL);
    // ws2_32's functions, imported by their number (ws2_32.lib's) or by their name
    static const struct
    {
        int ordinal;
        const char *name;
        void *fn, **original;
    } table[] = {
        {52, "gethostbyname", (void *)link_gethostbyname, (void **)&olink_gethostbyname},
        {2, "bind", (void *)link_bind, (void **)&olink_bind},
        {4, "connect", (void *)link_connect, (void **)&olink_connect},
        {20, "sendto", (void *)link_sendto, (void **)&olink_sendto},
        {17, "recvfrom", (void *)link_recvfrom, (void **)&olink_recvfrom},
        {1, "accept", (void *)link_accept, (void **)&olink_accept},
        {5, "getpeername", (void *)link_getpeername, (void **)&olink_getpeername},
        {6, "getsockname", (void *)link_getsockname, (void **)&olink_getsockname},
        {19, "send", (void *)link_send, (void **)&olink_send},
        {16, "recv", (void *)link_recv, (void **)&olink_recv},
        {3, "closesocket", (void *)link_closesocket, (void **)&olink_closesocket},
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (!wl_patch_iat(exe, "WS2_32.dll", (const char *)(ULONG_PTR)table[i].ordinal, table[i].fn, table[i].original) &&
            !wl_patch_iat(exe, "WS2_32.dll", table[i].name, table[i].fn, table[i].original))
            wl_log("daytona: %s not in the import table: no link", table[i].name);
}

// ------------------------------------------------------------------------------

static int ends_with(const char *s, const char *end)
{
    size_t n = strlen(s), m = strlen(end);
    return n >= m && !_stricmp(s + n - m, end);
}

static int ini_override(const char *file, const char *section, const char *key, char *value, size_t size)
{
    if (ends_with(file, "config.ini") && !_stricmp(section, "FullHD") && !_stricmp(key, "ENABLE_FULL_SCREEN"))
        snprintf(value, size, "0");
    else if (ends_with(file, "ShellData.ini") && !_stricmp(section, "Network") && !_stricmp(key, "NumCabinets"))
        snprintf(value, size, "%d", g_linkCount ? g_linkCount : 1);
    else if (ends_with(file, "ShellData.ini") && !_stricmp(section, "Network") && !_stricmp(key, "CabinetID"))
        snprintf(value, size, "%d", g_cabinet);
    // The cabinets of the link as the cabinet's test menu writes them (+0x11932e), beside the others
    else if (ends_with(file, "GameSettings.ini") && !_stricmp(section, "GameSettings") && !_stricmp(key, "TOTAL CABINETS") && g_linkCount)
        snprintf(value, size, "%d", g_linkCount);
    else
        return 0;
    return 1;
}

// ------------------------------------------------------------------------------
// The camera: the PC's, or an emulated one (camera.h)
// ------------------------------------------------------------------------------

// The link sends each cabinet's player's pictures to the others (OpenCVMgrImpl, ImageComm):
// without a camera, each cabinet drops the other right after "commLink OK" and both stay on
// "POWER-ON TEST NETWORK" (racecab1 and 2; the same under TeknoParrot without a webcam). The game
// opens it as an OpenCV 2.4 cv::VideoCapture (+0x16ab3b: new, then VideoCapture(700), DirectShow's
// camera 0), sets 320x240 and its rate, asks isOpened(), then a thread takes "cap >> frame" into
// an empty cv::Mat about 20 times a second (+0x16bf50). The camera of [Game.daytona3] CAMERA is
// opened instead (its rank in DirectShow's list); the emulated one is an object of windowsloader's,
// open, its frames the still picture from camera.cpp's buffer, which the Mat points at without a
// reference count: nothing for the game to free.
struct CvMat24 // cv::Mat of OpenCV 2.4, 32-bit
{
    int flags, dims, rows, cols;
    unsigned char *data;
    int *refcount;
    unsigned char *datastart, *dataend, *datalimit;
    void *allocator;
    int *sizep;
    size_t *stepp;
    size_t stepbuf[2];
};

#define CAMERA_W 320
#define CAMERA_H 240
static unsigned char *g_cameraFrame; // BGR, as OpenCV's

static void camera_frame(CvMat24 *m)
{
    static volatile LONG logged;
    if (m->refcount && InterlockedIncrement(&logged) == 1)
        wl_log("camera: the game's frame had data of its own, replaced (left to the game)");
    m->flags = 0x42FF0000 | 0x4000 | 16; // Mat's magic, continuous, CV_8UC3
    m->dims = 2;
    m->rows = CAMERA_H;
    m->cols = CAMERA_W;
    m->data = m->datastart = g_cameraFrame;
    m->dataend = m->datalimit = g_cameraFrame + CAMERA_W * CAMERA_H * 3;
    m->refcount = NULL;
    m->allocator = NULL;
    m->sizep = &m->rows;
    m->stepp = m->stepbuf;
    m->stepbuf[0] = CAMERA_W * 3;
    m->stepbuf[1] = 3;
}

// Its virtual functions, in cv::VideoCapture's order (MSVC: open(int) before open(string));
// __fastcall stands for __thiscall, edx unused
static void *__fastcall cam_destroy(void *self, void *, int)
{
    return self;
}

static bool __fastcall cam_open(void *, void *, const void *)
{
    return true;
}

static bool __fastcall cam_true(void *, void *)
{
    return true;
}

static void __fastcall cam_release(void *, void *)
{
}

static bool __fastcall cam_retrieve(void *, void *, CvMat24 *m, int)
{
    camera_frame(m);
    return true;
}

static void *__fastcall cam_take(void *self, void *, CvMat24 *m)
{
    camera_frame(m);
    return self;
}

static bool __fastcall cam_read(void *, void *, CvMat24 *m)
{
    camera_frame(m);
    return true;
}

static bool __fastcall cam_set(void *, void *, int, double)
{
    return true;
}

static double __fastcall cam_get(void *, void *, int prop)
{
    return prop == 3 ? CAMERA_W : prop == 4 ? CAMERA_H : prop == 5 ? 20 : 0;
}

static void *const g_cameraVtable[11] = {
    (void *)cam_destroy,  (void *)cam_open, (void *)cam_open, (void *)cam_true, (void *)cam_release, (void *)cam_true,
    (void *)cam_retrieve, (void *)cam_take, (void *)cam_read, (void *)cam_set,  (void *)cam_get,
};

static void *(__fastcall *o_camera_ctor)(void *, void *, int);
static void *__fastcall camera_ctor(void *self, void *edx, int device)
{
    int rank = wl_camera_device();
    if (rank != WL_CAMERA_EMULATED)
    {
        int wanted = rank >= 0 ? 700 + rank : device; // 700: DirectShow's, OpenCV's CV_CAP_DSHOW
        o_camera_ctor(self, edx, wanted);
        typedef bool(__fastcall * IsOpened)(void *, void *);
        if (((IsOpened)(*(void ***)self)[3])(self, NULL))
        {
            wl_log("camera: the PC's camera %d given to the game", wanted - 700);
            return self;
        }
        wl_log("camera: the PC's camera %d did not open: the emulated one", wanted - 700);
    }
    g_cameraFrame = (unsigned char *)wl_camera_picture(CAMERA_W, CAMERA_H);
    if (!g_cameraFrame)
        return self; // left as the game made it: no camera
    void **object = (void **)self;
    object[0] = (void *)g_cameraVtable;
    object[1] = object[2] = NULL;
    return self;
}

static void camera_patch(void)
{
    if (!g_build || sizeof(void *) != 4 || sizeof(CvMat24) != 0x38)
        return;
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    static const BYTE prologue[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68};
    if (memcmp(base + g_build->camera, prologue, sizeof prologue))
    {
        wl_log("camera: cv::VideoCapture(int) not where expected (+0x%lx): no emulated camera", g_build->camera);
        return;
    }
    wl_hook_code(base + g_build->camera, (void *)camera_ctor, (void **)&o_camera_ctor, "cv::VideoCapture::VideoCapture(int), the camera");
}

static void daytona_settings(void)
{
    read_options();
    link_settings();
    wl_camera_settings();
}

int wl_game_daytona(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    if (_wcsicmp(base, L"Daytona.exe"))
        return 0;

    wchar_t dir[MAX_PATH], ini[MAX_PATH];
    wcsncpy_s(dir, MAX_PATH, exePath, base - exePath);
    // The edition by the version of its Install.ini (171221 the first, 190306 the New Season Edition)
    swprintf(ini, MAX_PATH, L"%sInstall.ini", dir);
    int version = GetPrivateProfileIntW(L"Update", L"Version", 0, ini);
    *title = version >= 190000 ? "Daytona Championship USA - New Season Edition" : "Daytona Championship USA";
    // The cabinets' names of the two editions (AttractPlus, BackForceFeeder: Daytona3, Daytona3NSE)
    *name = version >= 190000 ? "daytona3nse" : "daytona3";
    wl_log("game: %s (Install.ini version %d)", *title, version);
    wl_game_set_boards("Controls on DirectInput board \"SEGA IO Controller\"",
                       "Motor board: Sega PC parallel-port driver (port 0x378 through InpWrapper.exe: strength nibble, direction bit)");

    swprintf(ini, MAX_PATH, L"%s..\\ShellData\\ShellData.ini", dir);
    g_shell.freeplay = GetPrivateProfileIntW(L"Credit", L"Freeplay", 0, ini) != 0;
    read_calibration(dir);
    find_build();
    wl_game_set_options(g_options, (int)(sizeof g_options / sizeof g_options[0]));
    wl_game_on_settings(daytona_settings);
    link_patch();
    camera_patch();
    wl_inputs_set_table(g_inputs, IN_COUNT);
    wl_vdinput_register(&g_board);
    wl_system_set_process_handler(process_handler);
    wl_files_set_ini_override(ini_override);
    // When an allocation fails the game writes all its memory blocks to GameData\debug.txt (it
    // opens "../GameData/\debug.txt"), some 70 MB each time, again and again once it is short of
    // memory
    wl_files_cap("\\debug.txt", 32ull << 20);
    // Its debug messages: thousands of asset loads ("Loaded nrt/dx11/....nrfc, QueSize 529") filled
    // the log's 2000 before the link's own messages
    static const char *const quiet[] = {".nrfc", NULL};
    wl_system_quiet_debug(quiet);
    wl_segashell_start(&g_shell);
    return 1;
}
