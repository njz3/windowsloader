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

// Written for windowsloader; the controls.ini syntax and the action names are linuxloader's
// (docs/Controls-Guide.md there), so one file serves both loaders.

#include <windows.h>
#include <SDL3/SDL.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controls.h"
#include "ffb.h"
#include "hooks.h"
#include "inputs.h"
#include "io4in.h"
#include "log.h"
#include "menu.h"
#include "settings.h"

// ------------------------------------------------------------------------------
// Bindings
// ------------------------------------------------------------------------------

enum SourceKind
{
    SRC_KEY,
    SRC_JOY_BUTTON,
    SRC_JOY_HAT,
    SRC_JOY_AXIS,
    SRC_GC_BUTTON,
    SRC_GC_AXIS
};

enum AxisPart
{
    PART_FULL,
    PART_POSITIVE,      // half an axis as a button
    PART_NEGATIVE,
    PART_POSITIVE_HALF, // half an axis as a pedal: 0 at the axis' centre, 1 at its end, the other half at
    PART_NEGATIVE_HALF  // rest (_POSITIVE_SIDE / _NEGATIVE_SIDE, linuxloader's names; _HALF also read)
};

struct Source
{
    SourceKind kind;
    int player;
    int index; // key: virtual key; button, axis, hat number; gamepad: SDL enum
    int hatDir;
    AxisPart part;
    int inverted;
};

#define MAX_COMBO 3
#define MAX_ALTERNATIVES 8

struct Alternative
{
    Source parts[MAX_COMBO]; // all pressed together ("A + B")
    int count;
};

struct Binding
{
    Alternative alt[MAX_ALTERNATIVES];
    int count;
};

struct InputBinds
{
    Binding main; // ACTION
    Binding low;  // ACTION_Left of an axis, ACTION_Digital of a pedal
    Binding high; // ACTION_Right of an axis
    double deadzone;
};

#define MAX_INPUTS 64
static InputBinds g_binds[MAX_INPUTS];
static wchar_t g_ini[MAX_PATH];

struct KeyName
{
    const char *name;
    int vk;
};

static const KeyName g_keys[] = {
    {"LEFT", VK_LEFT}, {"ARROWLEFT", VK_LEFT}, {"RIGHT", VK_RIGHT}, {"ARROWRIGHT", VK_RIGHT}, {"UP", VK_UP}, {"ARROWUP", VK_UP},
    {"DOWN", VK_DOWN}, {"ARROWDOWN", VK_DOWN}, {"SPACE", VK_SPACE}, {"RETURN", VK_RETURN}, {"ENTER", VK_RETURN}, {"ESCAPE", VK_ESCAPE},
    {"ESC", VK_ESCAPE}, {"TAB", VK_TAB}, {"BACKSPACE", VK_BACK}, {"LSHIFT", VK_LSHIFT}, {"LEFTSHIFT", VK_LSHIFT}, {"RSHIFT", VK_RSHIFT},
    {"RIGHTSHIFT", VK_RSHIFT}, {"LCTRL", VK_LCONTROL}, {"LEFTCTRL", VK_LCONTROL}, {"RCTRL", VK_RCONTROL}, {"RIGHTCTRL", VK_RCONTROL},
    {"LALT", VK_LMENU}, {"LEFTALT", VK_LMENU}, {"RALT", VK_RMENU}, {"RIGHTALT", VK_RMENU}, {"INSERT", VK_INSERT}, {"DELETE", VK_DELETE},
    {"HOME", VK_HOME}, {"END", VK_END}, {"PAGEUP", VK_PRIOR}, {"PAGEDOWN", VK_NEXT}, {"COMMA", VK_OEM_COMMA}, {"PERIOD", VK_OEM_PERIOD},
};

// "Left Shift", "left_shift" and "LEFTSHIFT" are the same name
static void normalize(const char *in, char *out, size_t size)
{
    size_t o = 0;
    for (; *in && o + 1 < size; in++)
        if (*in != ' ' && *in != '_')
            out[o++] = (char)toupper((unsigned char)*in);
    out[o] = 0;
}

static int key_vk(const char *name)
{
    char n[32];
    normalize(name, n, sizeof n);
    if (n[0] && !n[1] && (isalpha((unsigned char)n[0]) || isdigit((unsigned char)n[0])))
        return n[0];
    if (n[0] == 'F' && isdigit((unsigned char)n[1]))
    {
        int f = atoi(n + 1);
        if (f >= 1 && f <= 24)
            return VK_F1 + f - 1;
    }
    if ((!strncmp(n, "KP", 2) || !strncmp(n, "KEYPAD", 6)) && isdigit((unsigned char)n[strlen(n) - 1]))
        return VK_NUMPAD0 + (n[strlen(n) - 1] - '0');
    for (int i = 0; i < (int)(sizeof g_keys / sizeof g_keys[0]); i++)
        if (!strcmp(n, g_keys[i].name))
            return g_keys[i].vk;
    return 0;
}

struct EnumName
{
    const char *name;
    int value;
};

static const EnumName g_gcButtons[] = {
    {"A", SDL_GAMEPAD_BUTTON_SOUTH}, {"B", SDL_GAMEPAD_BUTTON_EAST}, {"X", SDL_GAMEPAD_BUTTON_WEST}, {"Y", SDL_GAMEPAD_BUTTON_NORTH},
    {"BACK", SDL_GAMEPAD_BUTTON_BACK}, {"GUIDE", SDL_GAMEPAD_BUTTON_GUIDE}, {"START", SDL_GAMEPAD_BUTTON_START},
    {"LEFTSTICK", SDL_GAMEPAD_BUTTON_LEFT_STICK}, {"RIGHTSTICK", SDL_GAMEPAD_BUTTON_RIGHT_STICK}, {"LEFTSHOULDER", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"RIGHTSHOULDER", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER}, {"DPUP", SDL_GAMEPAD_BUTTON_DPAD_UP}, {"DPDOWN", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {"DPLEFT", SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {"DPRIGHT", SDL_GAMEPAD_BUTTON_DPAD_RIGHT}, {"DPADUP", SDL_GAMEPAD_BUTTON_DPAD_UP},
    {"DPADDOWN", SDL_GAMEPAD_BUTTON_DPAD_DOWN}, {"DPADLEFT", SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {"DPADRIGHT", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
};

static const EnumName g_gcAxes[] = {
    {"LEFTX", SDL_GAMEPAD_AXIS_LEFTX}, {"LEFTY", SDL_GAMEPAD_AXIS_LEFTY}, {"RIGHTX", SDL_GAMEPAD_AXIS_RIGHTX}, {"RIGHTY", SDL_GAMEPAD_AXIS_RIGHTY},
    {"LEFTTRIGGER", SDL_GAMEPAD_AXIS_LEFT_TRIGGER}, {"TRIGGERLEFT", SDL_GAMEPAD_AXIS_LEFT_TRIGGER}, {"RIGHTTRIGGER", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER},
    {"TRIGGERRIGHT", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER},
};

static int enum_of(const EnumName *table, int count, const char *name)
{
    char n[32];
    normalize(name, n, sizeof n);
    for (int i = 0; i < count; i++)
        if (!strcmp(n, table[i].name))
            return table[i].value;
    return -1;
}

// Strips a known suffix from the end of s, returns 1 when it was there
static int strip_suffix(char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix);
    if (n <= m || _stricmp(s + n - m, suffix))
        return 0;
    s[n - m] = 0;
    return 1;
}

// One binding string ("JOY0_AXIS_1_INVERTED"), 0 when it is not understood
static int parse_source(const char *text, Source *s)
{
    char t[64];
    size_t n = 0;
    while (*text == ' ')
        text++;
    for (; *text && n + 1 < sizeof t; text++)
        t[n++] = *text;
    while (n && t[n - 1] == ' ')
        n--;
    t[n] = 0;
    memset(s, 0, sizeof *s);

    if (!_strnicmp(t, "KEY_", 4))
    {
        s->kind = SRC_KEY;
        s->index = key_vk(t + 4);
        return s->index != 0;
    }

    int gc = !_strnicmp(t, "GC", 2);
    if (!gc && _strnicmp(t, "JOY", 3))
        return 0;
    char *p = t + (gc ? 2 : 3);
    s->player = atoi(p);
    while (isdigit((unsigned char)*p))
        p++;
    if (*p != '_')
        return 0;
    p++;

    s->inverted = strip_suffix(p, "_INVERTED");
    if (strip_suffix(p, "_POSITIVE_SIDE") || strip_suffix(p, "_POSITIVE_HALF"))
        s->part = PART_POSITIVE_HALF;
    else if (strip_suffix(p, "_NEGATIVE_SIDE") || strip_suffix(p, "_NEGATIVE_HALF"))
        s->part = PART_NEGATIVE_HALF;
    else if (strip_suffix(p, "_POSITIVE"))
        s->part = PART_POSITIVE;
    else if (strip_suffix(p, "_NEGATIVE"))
        s->part = PART_NEGATIVE;
    if (!s->inverted)
        s->inverted = strip_suffix(p, "_INVERTED");

    if (!_strnicmp(p, "BUTTON_", 7))
    {
        s->kind = gc ? SRC_GC_BUTTON : SRC_JOY_BUTTON;
        s->index = gc ? enum_of(g_gcButtons, sizeof g_gcButtons / sizeof g_gcButtons[0], p + 7) : atoi(p + 7);
        return s->index >= 0;
    }
    if (!_strnicmp(p, "AXIS_", 5))
    {
        s->kind = gc ? SRC_GC_AXIS : SRC_JOY_AXIS;
        s->index = gc ? enum_of(g_gcAxes, sizeof g_gcAxes / sizeof g_gcAxes[0], p + 5) : atoi(p + 5);
        return s->index >= 0;
    }
    if (!gc && !_strnicmp(p, "HAT", 3))
    {
        s->kind = SRC_JOY_HAT;
        s->index = atoi(p + 3);
        const char *dir = strchr(p, '_');
        if (!dir)
            return 0;
        dir++;
        s->hatDir = !_stricmp(dir, "UP") ? SDL_HAT_UP : !_stricmp(dir, "DOWN") ? SDL_HAT_DOWN : !_stricmp(dir, "LEFT") ? SDL_HAT_LEFT
                                                                                : !_stricmp(dir, "RIGHT") ? SDL_HAT_RIGHT : 0;
        return s->hatDir != 0;
    }
    return 0;
}

static void read_binding(const char *section, const char *action, Binding *b)
{
    memset(b, 0, sizeof *b);
    if (!action)
        return;
    wchar_t ws[64], wk[96], wv[1024];
    char v[1024];
    MultiByteToWideChar(CP_UTF8, 0, section, -1, ws, 64);
    MultiByteToWideChar(CP_UTF8, 0, action, -1, wk, 96);
    GetPrivateProfileStringW(ws, wk, L"", wv, 1024, g_ini);
    WideCharToMultiByte(CP_UTF8, 0, wv, -1, v, sizeof v, NULL, NULL);

    char *ctx = NULL;
    for (char *alt = strtok_s(v, ",", &ctx); alt && b->count < MAX_ALTERNATIVES; alt = strtok_s(NULL, ",", &ctx))
    {
        Alternative *a = &b->alt[b->count];
        a->count = 0;
        char *ctx2 = NULL;
        for (char *part = strtok_s(alt, "+", &ctx2); part && a->count < MAX_COMBO; part = strtok_s(NULL, "+", &ctx2))
        {
            if (parse_source(part, &a->parts[a->count]))
                a->count++;
            else
                wl_log("controls: [%s] %s: \"%s\" not understood", section, action, part);
        }
        if (a->count)
            b->count++;
    }
}

// ------------------------------------------------------------------------------
// Devices: player n is the device whose GUID is P<n+1>_GUID, then the others in SDL's order
// ------------------------------------------------------------------------------

#define MAX_PLAYERS 8

struct Device
{
    SDL_JoystickID id;
    SDL_Joystick *joy;
    SDL_Gamepad *pad;
};

static Device g_players[MAX_PLAYERS];
static int g_keyboardFocus;
// Held to change g_players (the controls thread, exclusive) and by the settings menu to read them
static SRWLOCK g_devicesLock = SRWLOCK_INIT;

// ------------------------------------------------------------------------------
// Force feedback: one constant effect, updated when the game's force changes (ffb.h), on one
// device. [Config] of controls.ini: FFB_Strength (percent, 100), FFB_Invert (0/1), FFB_Device
// (AUTO, or empty: player 1's wheel; else a force feedback device's name as the system gives
// it, compared whole without case or the spaces around; --set ForceFeedback.FFB_DEVICE=
// overrides it, as Supermodel's, Flycast's, linuxloader's and sm2-emu's). The chosen device
// alone plays the effects, whichever device the wheel is read from; of two of the same name,
// player 1's. Not connected: player 1's wheel, until it is plugged in (devices are reassigned
// at each plug). A change from the settings menu takes effect at once: the old device's
// effect closed, the current force sent to the new one.
// ------------------------------------------------------------------------------

static SDL_Haptic *g_haptic;
static int g_hapticEffect = -1;
static int g_hapticPlayer = -1; // 0: the effects are on player 1's device; -1 another one or none
static unsigned g_ffbSerial;
static double g_ffbStrength = 1.0;
static int g_ffbInvert;

#define MAX_FFB_DEVICES 16
static char g_ffbDevice[128];                           // the setting, "" for AUTO
static char g_ffbNames[MAX_FFB_DEVICES][128];           // the connected force feedback devices
static int g_ffbCount;
static char g_ffbPlaying[128];                          // the device the effects go to, "" for none
static volatile LONG g_ffbReopen;                       // the setting changed: reopen in the controls thread
static CRITICAL_SECTION g_ffbLock;                      // the names, read by the settings menu
static INIT_ONCE g_ffbLockOnce = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK init_ffb_lock(PINIT_ONCE, PVOID, PVOID *)
{
    InitializeCriticalSection(&g_ffbLock);
    return TRUE;
}

static void ffb_lock(void)
{
    InitOnceExecuteOnce(&g_ffbLockOnce, init_ffb_lock, NULL, NULL);
    EnterCriticalSection(&g_ffbLock);
}

// A device's name without the spaces around it (in place)
static void trim(char *s)
{
    size_t n = strlen(s), b = 0;
    while (n && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
    while (isspace((unsigned char)s[b]))
        b++;
    memmove(s, s + b, n - b + 1);
}

static int same_device(const char *a, const char *b)
{
    char x[128], y[128];
    snprintf(x, sizeof x, "%s", a ? a : "");
    snprintf(y, sizeof y, "%s", b ? b : "");
    trim(x);
    trim(y);
    return x[0] && !_stricmp(x, y);
}

static void read_ffb_device(void)
{
    char v[128];
    wl_settings_get("ForceFeedback", "FFB_DEVICE", "", v, sizeof v);
    if (!v[0] && g_ini[0])
    {
        wchar_t w[128];
        GetPrivateProfileStringW(L"Config", L"FFB_Device", L"", w, 128, g_ini);
        WideCharToMultiByte(CP_UTF8, 0, w, -1, v, sizeof v, NULL, NULL);
    }
    trim(v);
    ffb_lock();
    snprintf(g_ffbDevice, sizeof g_ffbDevice, "%s", _stricmp(v, "AUTO") ? v : "");
    LeaveCriticalSection(&g_ffbLock);
}

static void close_haptic(void)
{
    if (g_haptic)
        SDL_CloseHaptic(g_haptic);
    g_haptic = NULL;
    g_hapticEffect = -1;
    g_hapticPlayer = -1;
    ffb_lock();
    g_ffbPlaying[0] = 0;
    LeaveCriticalSection(&g_ffbLock);
}

// The connected force feedback devices' names, for the settings menu
static void list_ffb_devices(void)
{
    int count = 0;
    SDL_HapticID *ids = SDL_GetHaptics(&count);
    ffb_lock();
    g_ffbCount = 0;
    for (int i = 0; ids && i < count && g_ffbCount < MAX_FFB_DEVICES; i++)
    {
        const char *name = SDL_GetHapticNameForID(ids[i]);
        snprintf(g_ffbNames[g_ffbCount++], sizeof g_ffbNames[0], "%s", name ? name : "?");
    }
    LeaveCriticalSection(&g_ffbLock);
    SDL_free(ids);
}

// The chosen device, opened; NULL when none is connected by that name. Player 1's own device
// first when it has the name (the one the wheel is read from), then the first of that name
static SDL_Haptic *open_chosen(SDL_Haptic **player1)
{
    char chosen[128];
    ffb_lock();
    snprintf(chosen, sizeof chosen, "%s", g_ffbDevice);
    LeaveCriticalSection(&g_ffbLock);
    if (*player1 && same_device(SDL_GetHapticName(*player1), chosen))
    {
        SDL_Haptic *h = *player1;
        *player1 = NULL;
        return h;
    }
    int count = 0;
    SDL_HapticID *ids = SDL_GetHaptics(&count);
    SDL_Haptic *h = NULL;
    for (int i = 0; ids && i < count && !h; i++)
        if (same_device(SDL_GetHapticNameForID(ids[i]), chosen))
            h = SDL_OpenHaptic(ids[i]);
    SDL_free(ids);
    return h;
}

static void constant_effect(SDL_HapticEffect *e, double level)
{
    memset(e, 0, sizeof *e);
    e->type = SDL_HAPTIC_CONSTANT;
    e->constant.direction.type = SDL_HAPTIC_CARTESIAN;
    e->constant.direction.dir[0] = 1; // along the wheel's axis
    e->constant.length = SDL_HAPTIC_INFINITY;
    e->constant.level = (Sint16)(level * 32767);
}

static void open_haptic(void)
{
    close_haptic();
    list_ffb_devices();
    SDL_Joystick *joy = g_players[0].joy;
    SDL_Haptic *player1 = joy && SDL_IsJoystickHaptic(joy) ? SDL_OpenHapticFromJoystick(joy) : NULL, *own = player1;
    char chosen[128];
    ffb_lock();
    snprintf(chosen, sizeof chosen, "%s", g_ffbDevice);
    LeaveCriticalSection(&g_ffbLock);
    if (chosen[0])
    {
        g_haptic = open_chosen(&player1);
        static char told[128]; // said once for each name
        if (!g_haptic && strcmp(told, chosen))
            wl_log("controls: force feedback: \"%s\" (FFB_Device) not connected: player 1's wheel until it is", chosen);
        snprintf(told, sizeof told, "%s", g_haptic ? "" : chosen);
    }
    if (!g_haptic)
        g_haptic = player1;
    else if (player1)
        SDL_CloseHaptic(player1);
    g_hapticPlayer = g_haptic && g_haptic == own ? 0 : -1;
    if (!g_haptic)
    {
        if (joy && SDL_IsJoystickHaptic(joy))
            wl_log("controls: force feedback: cannot open player 1's device (%s)", SDL_GetError());
        return;
    }
    if (!(SDL_GetHapticFeatures(g_haptic) & SDL_HAPTIC_CONSTANT))
    {
        wl_log("controls: force feedback: %s has no constant force", SDL_GetHapticName(g_haptic));
        close_haptic();
        return;
    }
    SDL_HapticEffect e;
    constant_effect(&e, 0);
    g_hapticEffect = SDL_CreateHapticEffect(g_haptic, &e);
    if (g_hapticEffect < 0 || !SDL_RunHapticEffect(g_haptic, g_hapticEffect, 1))
    {
        wl_log("controls: force feedback: constant effect refused (%s)", SDL_GetError());
        close_haptic();
        return;
    }
    wl_ffb_get(&g_ffbSerial);
    g_ffbSerial--; // the current force is sent at the next update
    const char *name = SDL_GetHapticName(g_haptic);
    ffb_lock();
    snprintf(g_ffbPlaying, sizeof g_ffbPlaying, "%s", name ? name : "?");
    LeaveCriticalSection(&g_ffbLock);
    wl_log("controls: force feedback on %s (%s), strength %d%%%s", g_ffbPlaying, chosen[0] && same_device(name, chosen) ? "FFB_Device" : "player 1",
           (int)(g_ffbStrength * 100), g_ffbInvert ? ", inverted" : "");
}

static void update_haptic(void)
{
    if (!g_haptic)
        return;
    unsigned serial;
    double f = wl_ffb_get(&serial);
    if (serial == g_ffbSerial)
        return;
    g_ffbSerial = serial;
    // > 0 pushes left, as SDL's: its direction is where the force comes from, the cartesian (1, 0) the
    // right (90 degrees in DirectInput, as vJoy reports it), so a positive level pushes to the left
    // (linuxloader's forceFeedback.c the same; seen at a cabinet's wheel on Sega Rally 3, 28/10/2026)
    double level = f * g_ffbStrength * (g_ffbInvert ? -1 : 1);
    if (level > 1)
        level = 1;
    if (level < -1)
        level = -1;
    SDL_HapticEffect e;
    constant_effect(&e, level);
    SDL_UpdateHapticEffect(g_haptic, g_hapticEffect, &e);
}

static char g_playerNames[MAX_PLAYERS][64]; // for the settings menu

static void close_devices(void)
{
    close_haptic();
    memset(g_playerNames, 0, sizeof g_playerNames);
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (g_players[i].pad)
            SDL_CloseGamepad(g_players[i].pad);
        else if (g_players[i].joy)
            SDL_CloseJoystick(g_players[i].joy);
        memset(&g_players[i], 0, sizeof g_players[i]);
    }
}

static void open_devices(void);

static void assign_devices(void)
{
    AcquireSRWLockExclusive(&g_devicesLock);
    open_devices();
    ReleaseSRWLockExclusive(&g_devicesLock);
}

static void open_devices(void)
{
    close_devices();
    int count = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&count);
    if (!ids)
        return;
    int taken[64] = {0};
    char guid[64];

    for (int p = 0; p < MAX_PLAYERS; p++)
    {
        wchar_t key[16], wguid[64];
        swprintf(key, 16, L"P%d_GUID", p + 1);
        if (!GetPrivateProfileStringW(L"ControllerGUIDs", key, L"", wguid, 64, g_ini))
            continue;
        WideCharToMultiByte(CP_UTF8, 0, wguid, -1, guid, sizeof guid, NULL, NULL);
        for (int i = 0; i < count && i < 64; i++)
        {
            char g[64];
            SDL_GUIDToString(SDL_GetJoystickGUIDForID(ids[i]), g, sizeof g);
            if (!taken[i] && !_stricmp(g, guid))
            {
                g_players[p].id = ids[i];
                taken[i] = 1;
                break;
            }
        }
    }
    for (int i = 0, p = 0; i < count && i < 64; i++)
    {
        if (taken[i])
            continue;
        while (p < MAX_PLAYERS && g_players[p].id)
            p++;
        if (p == MAX_PLAYERS)
            break;
        g_players[p].id = ids[i];
        taken[i] = 1;
    }
    SDL_free(ids);

    for (int p = 0; p < MAX_PLAYERS; p++)
    {
        Device *d = &g_players[p];
        if (!d->id)
            continue;
        if (SDL_IsGamepad(d->id))
        {
            d->pad = SDL_OpenGamepad(d->id);
            d->joy = d->pad ? SDL_GetGamepadJoystick(d->pad) : NULL;
        }
        if (!d->joy)
            d->joy = SDL_OpenJoystick(d->id);
        SDL_GUIDToString(SDL_GetJoystickGUIDForID(d->id), guid, sizeof guid);
        wl_log("controls: player %d = %s (%s, GUID %s)", p + 1, SDL_GetJoystickNameForID(d->id), d->pad ? "gamepad" : "joystick", guid);
        snprintf(g_playerNames[p], sizeof g_playerNames[p], "%s", SDL_GetJoystickNameForID(d->id));
    }
    open_haptic();
}

// ------------------------------------------------------------------------------
// Reading
// ------------------------------------------------------------------------------

static double axis_value(const Source *s)
{
    Device *d = &g_players[s->player < MAX_PLAYERS ? s->player : 0];
    double a = 0;
    if (s->kind == SRC_JOY_AXIS && d->joy)
        a = SDL_GetJoystickAxis(d->joy, s->index) / 32767.0;
    else if (s->kind == SRC_GC_AXIS && d->pad)
        a = SDL_GetGamepadAxis(d->pad, (SDL_GamepadAxis)s->index) / 32767.0;
    if (a < -1)
        a = -1;
    return s->inverted ? -a : a;
}

static int pressed(const Source *s)
{
    Device *d = &g_players[s->player < MAX_PLAYERS ? s->player : 0];
    switch (s->kind)
    {
    case SRC_KEY:
        return g_keyboardFocus && (GetAsyncKeyState(s->index) & 0x8000);
    case SRC_JOY_BUTTON:
        return d->joy && SDL_GetJoystickButton(d->joy, s->index);
    case SRC_JOY_HAT:
        return d->joy && (SDL_GetJoystickHat(d->joy, s->index) & s->hatDir);
    case SRC_GC_BUTTON:
        return d->pad && SDL_GetGamepadButton(d->pad, (SDL_GamepadButton)s->index);
    default:
    {
        double a = axis_value(s);
        return s->part == PART_NEGATIVE || s->part == PART_NEGATIVE_HALF ? a < -0.5 : a > 0.5;
    }
    }
}

static int binding_pressed(const Binding *b)
{
    for (int i = 0; i < b->count; i++)
    {
        int all = 1;
        for (int j = 0; j < b->alt[i].count && all; j++)
            all = pressed(&b->alt[i].parts[j]);
        if (all)
            return 1;
    }
    return 0;
}

// A pedal 0..1 from one source: a trigger's own range, a joystick axis from end to end, half an axis
static double pedal_of(const Source *s)
{
    if (s->kind != SRC_JOY_AXIS && s->kind != SRC_GC_AXIS)
        return pressed(s) ? 1 : 0;
    double a = axis_value(s);
    switch (s->part)
    {
    case PART_POSITIVE_HALF:
        return a > 0 ? a : 0;
    case PART_NEGATIVE_HALF:
        return a < 0 ? -a : 0;
    case PART_POSITIVE:
    case PART_NEGATIVE:
        return pressed(s) ? 1 : 0;
    default:
        if (s->kind == SRC_GC_AXIS && (s->index == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || s->index == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER))
            return a > 0 ? a : 0;
        return (a + 1) / 2;
    }
}

static double apply_deadzone(double v, double dz)
{
    double m = fabs(v);
    if (m <= dz)
        return 0;
    return (v > 0 ? 1 : -1) * (m - dz) / (1 - dz);
}

// The input's value in the agent's units (axis -1..1 with +1 left, pedal 0..1, switch 0/1)
static double input_value(const WlInput *in, const InputBinds *b)
{
    if (in->type == WL_INPUT_SWITCH)
        return binding_pressed(&b->main);

    if (in->type == WL_INPUT_ANALOG)
    {
        double v = binding_pressed(&b->low) ? 1 : 0;
        for (int i = 0; i < b->main.count; i++)
        {
            double p = apply_deadzone(pedal_of(&b->main.alt[i].parts[0]), b->deadzone);
            if (p > v)
                v = p;
        }
        return v;
    }

    // Axis: the source turned furthest wins; SDL's left is negative, ours positive
    double v = 0;
    for (int i = 0; i < b->main.count; i++)
    {
        const Source *s = &b->main.alt[i].parts[0];
        double a = (s->kind == SRC_JOY_AXIS || s->kind == SRC_GC_AXIS) && s->part == PART_FULL ? -apply_deadzone(axis_value(s), b->deadzone)
                                                                                                : (pressed(s) ? 1 : 0);
        if (fabs(a) > fabs(v))
            v = a;
    }
    if (binding_pressed(&b->low))
        v = 1;
    if (binding_pressed(&b->high))
        v = -1;
    return v;
}

static int focus_is_ours(void)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// ------------------------------------------------------------------------------
// controls.ini
// ------------------------------------------------------------------------------

static const char *section_of(const WlInput *in)
{
    // linuxloader's [Common] holds the actions every game has
    static const char *common[] = {"Test", "P1_Coin", "P2_Coin", "P1_Start", "P2_Start", "P1_Service", "P2_Service"};
    for (int i = 0; i < (int)(sizeof common / sizeof common[0]); i++)
        if (!_stricmp(in->action, common[i]))
            return "Common";
    return "Driving";
}

// The pedals: a gamepad's triggers, or the first joystick's Y and Z from their middle to their low
// end (BackForceFeeder's vJoy: the accelerator on Y-, the brake on Z-, at rest in the middle)
static const char *g_defaults =
    "# windowsloader controls, linuxloader's syntax (one file serves both). A line starting with # is a\r\n"
    "# comment. The comments and actions missing are added back at the next start, the bindings written\r\n"
    "# here are kept. The settings menu's controls page (Tab) binds them by pressing the input.\r\n"
    "#\r\n"
    "# ACTION followed by an equals sign and its bindings, separated by commas: any of them works;\r\n"
    "# A + B: both pressed together. Empty: the action is not bound.\r\n"
    "#   KEY_<name>                  a key (A to Z, 0 to 9, F1 to F24, KP0 to KP9, Left, Right, Up, Down,\r\n"
    "#                               Space, Return, Escape, Backspace, LShift, RShift, LCtrl, RCtrl, LAlt,\r\n"
    "#                               RAlt, Insert, Delete, Home, End, PageUp, PageDown, Comma, Period), read\r\n"
    "#                               only while the game's window has the focus\r\n"
    "#   JOY<n>_BUTTON_<b>           button b of player n's controller, numbered from 0 as SDL does (the\r\n"
    "#                               button Windows and vJoy call 1 is JOY0_BUTTON_0)\r\n"
    "#   JOY<n>_HAT<h>_UP            a hat's direction: _UP, _DOWN, _LEFT, _RIGHT\r\n"
    "#   JOY<n>_AXIS_<a>             axis a, from 0 (X, Y, Z, RX, RY, RZ, sliders)\r\n"
    "#   GC<n>_BUTTON_<name>         a gamepad's button: A, B, X, Y, BACK, START, GUIDE, LEFTSTICK,\r\n"
    "#                               RIGHTSTICK, LEFTSHOULDER, RIGHTSHOULDER, DPADUP, DPADDOWN, DPADLEFT,\r\n"
    "#                               DPADRIGHT\r\n"
    "#   GC<n>_AXIS_<name>           a gamepad's axis: LEFTX, LEFTY, RIGHTX, RIGHTY, LEFTTRIGGER,\r\n"
    "#                               RIGHTTRIGGER\r\n"
    "# An axis can end with: _INVERTED (turned over); _POSITIVE or _NEGATIVE (half of it as a button,\r\n"
    "# pressed past its middle); _POSITIVE_HALF or _NEGATIVE_HALF (half of it as a pedal, 0 at the middle);\r\n"
    "# _POSITIVE_SIDE or _NEGATIVE_SIDE (the same for a pedal resting in the middle of its axis, as\r\n"
    "# BackForceFeeder's vJoy pedals).\r\n"
    "# <n> is the player, from 0: JOY0 and GC0 are player 1's controller ([ControllerGUIDs] P1_GUID), the\r\n"
    "# other controllers follow in the order Windows gives them.\r\n"
    "\r\n"
    "[Config]\r\n"
    "# Dead zone of the steering wheel's axis, 0 to 32767 (32767 is the whole axis). Default: 500\r\n"
    "Steer_DeadZone = 500\r\n"
    "\r\n"
    "# Dead zone of the accelerator pedal's axis, 0 to 32767. Default: 500\r\n"
    "Gas_DeadZone = 500\r\n"
    "\r\n"
    "# Dead zone of the brake pedal's axis, 0 to 32767. Default: 500\r\n"
    "Brake_DeadZone = 500\r\n"
    "\r\n"
    "# Force feedback strength, in percent of the game's force. Default: 100\r\n"
    "FFB_Strength = 100\r\n"
    "\r\n"
    "# 1 turns the force feedback's direction over, for a wheel that pushes the wrong way. Default: 0\r\n"
    "FFB_Invert = 0\r\n"
    "\r\n"
    "# The device the force feedback goes to: AUTO (player 1's wheel) or a device's name as the system\r\n"
    "# gives it (\"vJoy Device\"); while it is not connected, player 1's wheel. Default: AUTO\r\n"
    "FFB_Device = AUTO\r\n"
    "\r\n"
    "[Common]\r\n"
    "# The cabinet's test button (the game's test menu). Default: KEY_T\r\n"
    "Test = KEY_T\r\n"
    "\r\n"
    "# Player 1's coin. Default: KEY_5\r\n"
    "P1_Coin = KEY_5\r\n"
    "\r\n"
    "# Player 1's start button. Default: KEY_1, GC0_BUTTON_START\r\n"
    "P1_Start = KEY_1, GC0_BUTTON_START\r\n"
    "\r\n"
    "# Player 1's service button. Default: KEY_S\r\n"
    "P1_Service = KEY_S\r\n"
    "\r\n"
    "# Ends the game and the programs started with it, as the settings menu's quit button. Default:\r\n"
    "# KEY_Escape, GC0_BUTTON_START + GC0_BUTTON_BACK, JOY0_BUTTON_11 + JOY0_BUTTON_10\r\n"
    "ExitGame = KEY_Escape, GC0_BUTTON_START + GC0_BUTTON_BACK, JOY0_BUTTON_11 + JOY0_BUTTON_10\r\n"
    "\r\n"
    "[Driving]\r\n"
    "# The steering wheel turned fully left, as a button. Default: KEY_Left\r\n"
    "P1_Steer_Left = KEY_Left\r\n"
    "\r\n"
    "# The steering wheel turned fully right, as a button. Default: KEY_Right\r\n"
    "P1_Steer_Right = KEY_Right\r\n"
    "\r\n"
    "# The accelerator pressed fully, as a button. Default: KEY_Up\r\n"
    "P1_Gas_Digital = KEY_Up\r\n"
    "\r\n"
    "# The brake pressed fully, as a button. Default: KEY_Down\r\n"
    "P1_Brake_Digital = KEY_Down\r\n"
    "\r\n"
    "# Changes the view (Sega Rally 3, Daytona...). Default: KEY_V, GC0_BUTTON_Y\r\n"
    "ViewChange = KEY_V, GC0_BUTTON_Y\r\n"
    "\r\n"
    "# Handbrake (Sega Rally 3, Battle Gear 4...). Default: KEY_Space, GC0_BUTTON_X\r\n"
    "Handbrake = KEY_Space, GC0_BUTTON_X\r\n"
    "\r\n"
    "# Gear up, sequential shifter. Default: KEY_A, GC0_BUTTON_RIGHTSHOULDER\r\n"
    "GearUp = KEY_A, GC0_BUTTON_RIGHTSHOULDER\r\n"
    "\r\n"
    "# Gear down, sequential shifter. Default: KEY_Z, GC0_BUTTON_LEFTSHOULDER\r\n"
    "GearDown = KEY_Z, GC0_BUTTON_LEFTSHOULDER\r\n"
    "\r\n"
    "# Hazard lights button (Taito games: Battle Gear 4...). Default: KEY_H\r\n"
    "Hazard = KEY_H\r\n"
    "\r\n"
    "# Overtake button (Taito games). Default: KEY_O, GC0_BUTTON_DPADUP\r\n"
    "Overtake = KEY_O, GC0_BUTTON_DPADUP\r\n"
    "\r\n"
    "# Nitro or boost button. Default: KEY_N, GC0_BUTTON_A\r\n"
    "Nitro = KEY_N, GC0_BUTTON_A\r\n"
    "\r\n"
    "# Nitro on the shifter (Taito games). Default: KEY_J\r\n"
    "ShiftNitro = KEY_J\r\n"
    "\r\n"
    "# The game's action button: an item, a weapon (GTI Club 3, Mario Kart DX). Default: KEY_C\r\n"
    "Action = KEY_C\r\n"
    "\r\n"
    "# Selection up, in the menus of the games that have selection buttons. Default: KEY_PageUp\r\n"
    "SelectUp = KEY_PageUp\r\n"
    "\r\n"
    "# Selection down. Default: KEY_PageDown\r\n"
    "SelectDown = KEY_PageDown\r\n"
    "\r\n"
    "# Selection left. Default: KEY_Home\r\n"
    "SelectLeft = KEY_Home\r\n"
    "\r\n"
    "# Selection right. Default: KEY_End\r\n"
    "SelectRight = KEY_End\r\n"
    "\r\n"
    "# Selection confirmed. Default: KEY_Return\r\n"
    "SelectEnter = KEY_Return\r\n"
    "\r\n"
    "# View button 1, red (Daytona, Initial D...). Default: KEY_B, GC0_BUTTON_A\r\n"
    "View1 = KEY_B, GC0_BUTTON_A\r\n"
    "\r\n"
    "# View button 2, blue (Daytona, Initial D...). Default: KEY_N, GC0_BUTTON_B\r\n"
    "View2 = KEY_N, GC0_BUTTON_B\r\n"
    "\r\n"
    "# View button 3, yellow (Daytona, Initial D...). Default: KEY_M, GC0_BUTTON_X\r\n"
    "View3 = KEY_M, GC0_BUTTON_X\r\n"
    "\r\n"
    "# View button 4, green (Daytona, Initial D...). Default: KEY_Comma, GC0_BUTTON_Y\r\n"
    "View4 = KEY_Comma, GC0_BUTTON_Y\r\n"
    "\r\n"
    "# H shifter, 1st gear (held while in gear). Default: KEY_Q\r\n"
    "Gear1 = KEY_Q\r\n"
    "\r\n"
    "# H shifter, 2nd gear. Default: KEY_W\r\n"
    "Gear2 = KEY_W\r\n"
    "\r\n"
    "# H shifter, 3rd gear. Default: KEY_E\r\n"
    "Gear3 = KEY_E\r\n"
    "\r\n"
    "# H shifter, 4th gear. Default: KEY_R\r\n"
    "Gear4 = KEY_R\r\n"
    "\r\n"
    "# H shifter, 5th gear. Default: KEY_Y\r\n"
    "Gear5 = KEY_Y\r\n"
    "\r\n"
    "# H shifter, 6th gear. Default: KEY_U\r\n"
    "Gear6 = KEY_U\r\n"
    "\r\n"
    "# The clutch pressed fully, as a button. Default: KEY_X\r\n"
    "P1_Clutch_Digital = KEY_X\r\n"
    "\r\n"
    "# Left foot pedal of a motorbike (Frenzy Express). Default: KEY_Z, GC0_BUTTON_LEFTSHOULDER\r\n"
    "PedalLeft = KEY_Z, GC0_BUTTON_LEFTSHOULDER\r\n"
    "\r\n"
    "# Right foot pedal of a motorbike (Frenzy Express). Default: KEY_X, GC0_BUTTON_RIGHTSHOULDER\r\n"
    "PedalRight = KEY_X, GC0_BUTTON_RIGHTSHOULDER\r\n"
    "\r\n"
    "# The steering wheel, an axis turned left to right (a gamepad's left stick, else the first\r\n"
    "# controller's X). Default: GC0_AXIS_LEFTX, JOY0_AXIS_0\r\n"
    "P1_Steer = GC0_AXIS_LEFTX, JOY0_AXIS_0\r\n"
    "\r\n"
    "# The accelerator pedal: a gamepad's right trigger, else the first controller's Y from its middle\r\n"
    "# (released) to its low end (pressed), as BackForceFeeder's vJoy. Default: GC0_AXIS_RIGHTTRIGGER,\r\n"
    "# JOY0_AXIS_1_NEGATIVE_SIDE\r\n"
    "P1_Gas = GC0_AXIS_RIGHTTRIGGER, JOY0_AXIS_1_NEGATIVE_SIDE\r\n"
    "\r\n"
    "# The brake pedal: a gamepad's left trigger, else the first controller's Z from its middle (released)\r\n"
    "# to its low end (pressed). Default: GC0_AXIS_LEFTTRIGGER, JOY0_AXIS_2_NEGATIVE_SIDE\r\n"
    "P1_Brake = GC0_AXIS_LEFTTRIGGER, JOY0_AXIS_2_NEGATIVE_SIDE\r\n"
    "\r\n"
    "[ControllerGUIDs]\r\n"
    "# P1_GUID, P2_GUID...: player <n>'s controller by its SDL GUID (the log gives each controller's), written\r\n"
    "# by hand. None: the controllers in the order Windows gives them.\r\n";

// A missing controls.ini is written with the defaults; an existing one gets the default lines it
// lacks (a new action, a new setting) and their comments, the user's own lines left as they are
static void write_defaults(void)
{
    wl_ini_merge(g_ini, g_defaults);
}

// [Common] ExitGame, linuxloader's: the game ends (Escape, as under TeknoParrot)
static Binding g_exitGame;

static void load_bindings(void)
{
    char s[96];
    read_binding("Common", "ExitGame", &g_exitGame);
    wl_log("controls: ExitGame = [Common] ExitGame (%d bindings)", g_exitGame.count);
    for (int i = 0; i < wl_inputs_count() && i < MAX_INPUTS; i++)
    {
        WlInput *in = wl_input_at(i);
        InputBinds *b = &g_binds[i];
        memset(b, 0, sizeof *b);
        if (!in->action)
            continue;
        const char *section = section_of(in);
        read_binding(section, in->action, &b->main);
        if (in->type == WL_INPUT_AXIS)
        {
            snprintf(s, sizeof s, "%s_Left", in->action);
            read_binding(section, s, &b->low);
            snprintf(s, sizeof s, "%s_Right", in->action);
            read_binding(section, s, &b->high);
        }
        else if (in->type == WL_INPUT_ANALOG)
        {
            snprintf(s, sizeof s, "%s_Digital", in->action);
            read_binding(section, s, &b->low);
        }

        // Steer_DeadZone for P1_Steer, as linuxloader's [Config]
        const char *bare = !_strnicmp(in->action, "P1_", 3) ? in->action + 3 : in->action;
        wchar_t key[96];
        swprintf(key, 96, L"%hs_DeadZone", bare);
        b->deadzone = GetPrivateProfileIntW(L"Config", key, in->type == WL_INPUT_SWITCH ? 0 : 500, g_ini) / 32767.0;
        wl_log("controls: %s = [%s] %s (%d bindings)", in->name, section, in->action, b->main.count + b->low.count + b->high.count);
    }
}

// ------------------------------------------------------------------------------
// Thread
// ------------------------------------------------------------------------------

static volatile LONG g_running;
static volatile LONG g_reload; // controls.ini changed by the settings menu: reload the bindings
static void capture_step(void);

// ExitGame pressed: the game ends at once, as with the menu's "Quitter le jeu" (the launcher then
// closes the programs it started with it). Not while the settings menu is open (Escape is its own
// there), nor for a press already held before: only a new one counts
static void exit_step(void)
{
    static int was = 1;
    int down = binding_pressed(&g_exitGame);
    if (down && !was && !wl_menu_is_open())
    {
        wl_log("controls: ExitGame: the game ends");
        TerminateProcess(GetCurrentProcess(), 0);
    }
    was = down;
}

static DWORD WINAPI controls_thread(LPVOID)
{
    InterlockedExchange(&g_running, 1);
    wl_busy_enter(); // SDL's own file and device calls go through the hooks untouched

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC))
    {
        wl_log("controls: SDL_Init failed (%s), keyboard only", SDL_GetError());
    }
    else
        wl_io4_start(); // a real IO4 board, one more controller once found
    assign_devices();

    for (;;)
    {
        int changed = 0;
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_EVENT_JOYSTICK_ADDED || e.type == SDL_EVENT_JOYSTICK_REMOVED)
                changed = 1;
        if (changed)
            assign_devices();
        else if (InterlockedExchange(&g_ffbReopen, 0)) // FFB_Device changed in the settings menu
            open_haptic();
        g_keyboardFocus = focus_is_ours();
        if (InterlockedExchange(&g_reload, 0))
            load_bindings();
        capture_step();
        exit_step();

        for (int i = 0; i < wl_inputs_count() && i < MAX_INPUTS; i++)
        {
            WlInput *in = wl_input_at(i);
            if (!in->action || in->held)
                continue;
            InterlockedExchange(&in->raw, wl_input_raw_of(in, input_value(in, &g_binds[i])));
        }
        update_haptic();
        Sleep(2);
    }
}

void wl_controls_load(const wchar_t *iniPath)
{
    if (!wl_inputs_count())
        return;
    wcsncpy_s(g_ini, MAX_PATH, iniPath, _TRUNCATE);
    write_defaults();
    load_bindings();
    g_ffbStrength = GetPrivateProfileIntW(L"Config", L"FFB_Strength", 100, g_ini) / 100.0;
    g_ffbInvert = GetPrivateProfileIntW(L"Config", L"FFB_Invert", 0, g_ini) != 0;
    read_ffb_device();
}

int wl_controls_has_file(void)
{
    return g_ini[0] != 0;
}

void wl_controls_start(const wchar_t *iniPath)
{
    wl_controls_load(iniPath);
    if (!wl_inputs_count())
        return;
    HANDLE t = CreateThread(NULL, 0, controls_thread, NULL, 0, NULL);
    if (t)
        CloseHandle(t);
}

// ------------------------------------------------------------------------------
// For the settings menu (menu.cpp), which runs on the game's render thread: the bindings as text,
// their capture, the force feedback settings. What the controls thread owns (the bindings, SDL)
// is changed by request and done in that thread.
// ------------------------------------------------------------------------------

static volatile LONG g_capturing; // a capture asked: g_captureDevice's input for a row of g_captureKind
static volatile LONG g_captureDevice;
static volatile LONG g_captureKind;
static volatile LONG g_captureDone;
static char g_captured[64];

int wl_controls_running(void)
{
    return g_running;
}

const char *wl_controls_player_name(int player)
{
    return player >= 0 && player < MAX_PLAYERS ? g_playerNames[player] : "";
}

int wl_controls_devices(WlControlDevice *out, int max)
{
    int n = 0;
    if (n < max)
    {
        memset(&out[n], 0, sizeof out[n]);
        out[n].player = -1;
        n++;
    }
    AcquireSRWLockShared(&g_devicesLock);
    for (int p = 0; p < MAX_PLAYERS && n < max; p++)
    {
        if (!g_players[p].joy)
            continue;
        WlControlDevice *d = &out[n++];
        d->player = p;
        d->gamepad = g_players[p].pad != NULL;
        d->ffb = g_hapticPlayer == p && g_haptic;
        snprintf(d->name, sizeof d->name, "%s", g_playerNames[p][0] ? g_playerNames[p] : "?");
    }
    ReleaseSRWLockShared(&g_devicesLock);
    return n;
}

static void trim_both(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = 0;
    size_t b = 0;
    while (s[b] == ' ' || s[b] == '\t')
        b++;
    memmove(s, s + b, n - b + 1);
}

int wl_controls_rows(const char *section, WlControlRow *rows, int max)
{
    if (!g_ini[0])
        return 0;
    static wchar_t all[32768];
    wchar_t ws[64];
    MultiByteToWideChar(CP_UTF8, 0, section, -1, ws, 64);
    if (!GetPrivateProfileSectionW(ws, all, sizeof all / sizeof all[0], g_ini))
        return 0;
    int n = 0;
    for (const wchar_t *line = all; *line && n < max; line += wcslen(line) + 1)
    {
        char text[1024];
        WideCharToMultiByte(CP_UTF8, 0, line, -1, text, sizeof text, NULL, NULL);
        char *eq = strchr(text, '=');
        if (!eq || text[0] == ';' || text[0] == '#')
            continue;
        *eq = 0;
        snprintf(rows[n].key, sizeof rows[n].key, "%s", text);
        snprintf(rows[n].value, sizeof rows[n].value, "%s", eq + 1);
        trim_both(rows[n].key);
        trim_both(rows[n].value);
        if (rows[n].key[0])
            n++;
    }
    return n;
}

void wl_controls_set_row(const char *section, const char *key, const char *value)
{
    if (!g_ini[0])
        return;
    wchar_t ws[64], wk[96], wv[1024];
    MultiByteToWideChar(CP_UTF8, 0, section, -1, ws, 64);
    MultiByteToWideChar(CP_UTF8, 0, key, -1, wk, 96);
    MultiByteToWideChar(CP_UTF8, 0, value, -1, wv, 1024);
    WritePrivateProfileStringW(ws, wk, wv, g_ini);
    wl_log("controls: [%s] %s = %s (settings menu)", section, key, value);
    InterlockedExchange(&g_reload, 1);
}

int wl_controls_default_row(const char *section, const char *key, char *out, size_t size)
{
    char line[256], current[64] = "";
    for (const char *d = g_defaults; *d;)
    {
        size_t n = strcspn(d, "\r\n");
        snprintf(line, sizeof line, "%.*s", (int)n, d);
        d += n;
        while (*d == '\r' || *d == '\n')
            d++;
        if (line[0] == '[')
        {
            snprintf(current, sizeof current, "%s", line + 1);
            char *end = strchr(current, ']');
            if (end)
                *end = 0;
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#' || _stricmp(current, section))
            continue;
        *eq = 0;
        trim_both(line);
        if (_stricmp(line, key))
            continue;
        snprintf(out, size, "%s", eq + 1);
        trim_both(out);
        return 1;
    }
    out[0] = 0;
    return 0;
}

int wl_controls_input_state(const char *token, int *on, int *raw)
{
    *on = 0;
    *raw = 0;
    char t[192];
    snprintf(t, sizeof t, "%s", token);
    Source parts[MAX_COMBO];
    int count = 0;
    char *ctx = NULL;
    for (char *part = strtok_s(t, "+", &ctx); part && count < MAX_COMBO; part = strtok_s(NULL, "+", &ctx))
        if (parse_source(part, &parts[count]))
            count++;
    if (!count)
        return 0;
    AcquireSRWLockShared(&g_devicesLock);
    int kind = 1;
    const Source *s = &parts[0];
    if (count == 1 && (s->kind == SRC_JOY_AXIS || s->kind == SRC_GC_AXIS) && s->part != PART_POSITIVE && s->part != PART_NEGATIVE)
    {
        Device *d = &g_players[s->player < MAX_PLAYERS ? s->player : 0];
        kind = d->joy ? 2 : 0;
        if (s->kind == SRC_JOY_AXIS && d->joy)
            *raw = SDL_GetJoystickAxis(d->joy, s->index);
        else if (d->pad)
            *raw = SDL_GetGamepadAxis(d->pad, (SDL_GamepadAxis)s->index);
    }
    else
    {
        *on = 1;
        for (int i = 0; i < count; i++)
            *on = *on && pressed(&parts[i]);
    }
    ReleaseSRWLockShared(&g_devicesLock);
    return kind;
}

void wl_controls_capture(int player, int kind)
{
    g_captureDone = 0;
    InterlockedExchange(&g_captureDevice, player);
    InterlockedExchange(&g_captureKind, kind);
    InterlockedExchange(&g_capturing, 1);
}

int wl_controls_capture_result(char *out, size_t size)
{
    if (!g_captureDone)
        return 0;
    snprintf(out, size, "%s", g_captured);
    g_captureDone = 0;
    return 1;
}

void wl_controls_capture_cancel(void)
{
    InterlockedExchange(&g_capturing, 0);
}

void wl_controls_ffb_settings(double *strength, int *invert)
{
    *strength = g_ffbStrength;
    *invert = g_ffbInvert;
}

void wl_controls_set_ffb(double strength, int invert)
{
    g_ffbStrength = strength;
    g_ffbInvert = invert;
    g_ffbSerial--; // the current force is sent again with the new settings
    if (!g_ini[0])
        return;
    wchar_t v[16];
    swprintf(v, 16, L"%d", (int)(strength * 100 + 0.5));
    WritePrivateProfileStringW(L"Config", L"FFB_Strength", v, g_ini);
    WritePrivateProfileStringW(L"Config", L"FFB_Invert", invert ? L"1" : L"0", g_ini);
}

int wl_controls_ffb_device(char *out, size_t size)
{
    ffb_lock();
    snprintf(out, size, "%s", g_ffbDevice);
    int connected = !g_ffbDevice[0];
    for (int i = 0; i < g_ffbCount && !connected; i++)
        connected = same_device(g_ffbNames[i], g_ffbDevice);
    LeaveCriticalSection(&g_ffbLock);
    return connected;
}

int wl_controls_ffb_devices(char (*names)[128], int max)
{
    ffb_lock();
    int n = g_ffbCount < max ? g_ffbCount : max;
    for (int i = 0; i < n; i++)
        snprintf(names[i], 128, "%s", g_ffbNames[i]);
    LeaveCriticalSection(&g_ffbLock);
    return n;
}

void wl_controls_ffb_playing(char *out, size_t size)
{
    ffb_lock();
    snprintf(out, size, "%s", g_ffbPlaying);
    LeaveCriticalSection(&g_ffbLock);
}

void wl_controls_set_ffb_device(const char *name)
{
    char v[128];
    snprintf(v, sizeof v, "%s", name ? name : "");
    trim(v);
    ffb_lock();
    snprintf(g_ffbDevice, sizeof g_ffbDevice, "%s", _stricmp(v, "AUTO") ? v : "");
    LeaveCriticalSection(&g_ffbLock);
    InterlockedExchange(&g_ffbReopen, 1);
    wl_log("controls: FFB_Device = %s (settings menu)", v[0] && _stricmp(v, "AUTO") ? v : "AUTO");
    if (!g_ini[0])
        return;
    wchar_t w[128];
    MultiByteToWideChar(CP_UTF8, 0, v[0] ? v : "AUTO", -1, w, 128);
    WritePrivateProfileStringW(L"Config", L"FFB_Device", w, g_ini);
}

// Key names written by the capture, read back by key_vk
static const KeyName g_keyNamesOut[] = {
    {"Left", VK_LEFT},      {"Right", VK_RIGHT},    {"Up", VK_UP},          {"Down", VK_DOWN},     {"Space", VK_SPACE},
    {"Return", VK_RETURN},  {"Tab", VK_TAB},        {"Backspace", VK_BACK}, {"LShift", VK_LSHIFT}, {"RShift", VK_RSHIFT},
    {"LCtrl", VK_LCONTROL}, {"RCtrl", VK_RCONTROL}, {"LAlt", VK_LMENU},     {"RAlt", VK_RMENU},    {"Insert", VK_INSERT},
    {"Delete", VK_DELETE},  {"Home", VK_HOME},      {"End", VK_END},        {"PageUp", VK_PRIOR},  {"PageDown", VK_NEXT},
    {"Comma", VK_OEM_COMMA}, {"Period", VK_OEM_PERIOD},
};

static int vk_name(int vk, char *out, size_t size)
{
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
    {
        snprintf(out, size, "%c", vk);
        return 1;
    }
    if (vk >= VK_F1 && vk <= VK_F24)
    {
        snprintf(out, size, "F%d", vk - VK_F1 + 1);
        return 1;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
    {
        snprintf(out, size, "KP%d", vk - VK_NUMPAD0);
        return 1;
    }
    for (int i = 0; i < (int)(sizeof g_keyNamesOut / sizeof g_keyNamesOut[0]); i++)
        if (g_keyNamesOut[i].vk == vk && vk != VK_TAB) // Tab is the menu's
        {
            snprintf(out, size, "%s", g_keyNamesOut[i].name);
            return 1;
        }
    return 0;
}

// What the controllers were doing when the capture started: only a change counts
struct CaptureBase
{
    int started;
    unsigned char keys[256];
    double axes[MAX_PLAYERS][16];
    unsigned char buttons[MAX_PLAYERS][64];
    unsigned char hats[MAX_PLAYERS][4];
};
static CaptureBase g_base;

// The settings menu's capture (wl_controls_capture): the first input of the device asked that
// changes, a key for the keyboard (-1), else a button, a hat or an axis of player n's device. An
// axis for the steering is the whole axis (_INVERTED when it was turned right, the left being
// asked), for a pedal its side from where it rested (or the whole axis, _INVERTED when it rested
// at the top), for a button half of it (_POSITIVE / _NEGATIVE)
static void capture_step(void)
{
    if (!g_capturing)
    {
        g_base.started = 0;
        return;
    }
    int device = g_captureDevice, kind = g_captureKind;

    if (!g_base.started)
    {
        memset(&g_base, 0, sizeof g_base);
        g_base.started = 1;
        for (int vk = 8; vk < 256; vk++)
            g_base.keys[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
        for (int p = 0; p < MAX_PLAYERS; p++)
        {
            SDL_Joystick *j = g_players[p].joy;
            for (int a = 0; j && a < SDL_GetNumJoystickAxes(j) && a < 16; a++)
                g_base.axes[p][a] = SDL_GetJoystickAxis(j, a) / 32767.0;
            for (int b = 0; j && b < SDL_GetNumJoystickButtons(j) && b < 64; b++)
                g_base.buttons[p][b] = SDL_GetJoystickButton(j, b);
            for (int h = 0; j && h < SDL_GetNumJoystickHats(j) && h < 4; h++)
                g_base.hats[p][h] = SDL_GetJoystickHat(j, h);
        }
        return;
    }

    char found[64] = "", name[32];
    if (device < 0 && g_keyboardFocus)
        for (int vk = 8; vk < 256 && !found[0]; vk++)
        {
            int down = (GetAsyncKeyState(vk) & 0x8000) != 0;
            if (down && !g_base.keys[vk] && vk != VK_ESCAPE && vk_name(vk, name, sizeof name))
                snprintf(found, sizeof found, "KEY_%s", name);
            if (!down)
                g_base.keys[vk] = 0;
        }

    static const char *dirs[] = {"UP", "RIGHT", "DOWN", "LEFT"};
    for (int p = 0; p < MAX_PLAYERS && !found[0]; p++)
    {
        SDL_Joystick *j = g_players[p].joy;
        if (!j || p != device)
            continue;
        for (int b = 0; b < SDL_GetNumJoystickButtons(j) && b < 64 && !found[0]; b++)
            if (SDL_GetJoystickButton(j, b) && !g_base.buttons[p][b])
                snprintf(found, sizeof found, "JOY%d_BUTTON_%d", p, b);
        for (int h = 0; h < SDL_GetNumJoystickHats(j) && h < 4 && !found[0]; h++)
        {
            int now = SDL_GetJoystickHat(j, h) & ~g_base.hats[p][h];
            for (int d = 0; d < 4 && !found[0]; d++)
                if (now & (1 << d))
                    snprintf(found, sizeof found, "JOY%d_HAT%d_%s", p, h, dirs[d]);
        }
        for (int a = 0; a < SDL_GetNumJoystickAxes(j) && a < 16 && !found[0]; a++)
        {
            double a0 = g_base.axes[p][a], a1 = SDL_GetJoystickAxis(j, a) / 32767.0;
            if (fabs(a1 - a0) < 0.5)
                continue;
            const char *suffix;
            if (kind == WL_CAPTURE_STEERING)
                suffix = a1 < a0 ? "" : "_INVERTED"; // turned left: SDL's left is negative
            else if (kind == WL_CAPTURE_PEDAL)
                suffix = fabs(a0) > 0.8 ? (a0 > 0 ? "_INVERTED" : "") : (a1 > a0 ? "_POSITIVE_SIDE" : "_NEGATIVE_SIDE");
            else
                suffix = a1 > a0 ? "_POSITIVE" : "_NEGATIVE";
            snprintf(found, sizeof found, "JOY%d_AXIS_%d%s", p, a, suffix);
        }
    }

    if (found[0])
    {
        snprintf(g_captured, sizeof g_captured, "%s", found);
        g_captureDone = 1;
        InterlockedExchange(&g_capturing, 0);
        g_base.started = 0;
        wl_log("controls: captured %s", found);
    }
}
