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

// Taito Type X, X+ and X2: Battle Gear 4 (Tuned and the first edition), Chase H.Q. 2, D1GP
// (game.exe) and Valve Limit R (launcher.exe), x86, Direct3D 9. The game finds its settings and
// saves on D:\ (and Battle Gear 4 its data in E:\data): its own folder. Its JVS I/O board is on
// COM2; Battle Gear 4's and Valve Limit R's wheel drive board on COM1; D1GP's wheel is a DirectInput
// one.

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <wchar.h>
#include <mmsystem.h>
#include <dsound.h>

#include "display.h"
#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "jvs.h"
#include "log.h"
#include "settings.h"
#include "vdev.h"
#include "vdinput.h"

enum
{
    IN_STEERING,
    IN_GAS,
    IN_BRAKE,
    IN_START,
    IN_SERVICE,
    IN_TEST,
    IN_COIN,
    IN_VIEW, // Battle Gear 4's (its I/O test): player 1's bits 13, 12, 11, 9, 8, 7
    IN_HAZARD,
    IN_OVERTAKE,
    IN_SIDE_BRAKE,
    IN_SHIFT_UP, // also Chase H.Q. 2's lever: down → LOW, up → HIGH
    IN_SHIFT_DOWN,
    IN_NITRO, // Chase H.Q. 2's (its I/O test): PATO-NITRO bit 2, SHIFT-NITRO bit 12
    IN_SHIFT_NITRO,
    IN_CLUTCH, // Battle Gear 4 Pro's
    IN_GEAR1,  // Battle Gear 4 Pro's H shifter, 1 to 6
    IN_COUNT = IN_GEAR1 + 6
};

static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 255, 128},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 255, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 255, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "P1_Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"View", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"Hazard", "Hazard", WL_INPUT_SWITCH, 0, 1, 0},
    {"Overtake", "Overtake", WL_INPUT_SWITCH, 0, 1, 0},
    {"SideBrake", "Handbrake", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftUp", "GearUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftDown", "GearDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"Nitro", "Nitro", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftNitro", "ShiftNitro", WL_INPUT_SWITCH, 0, 1, 0},
    {"Clutch", "P1_Clutch", WL_INPUT_ANALOG, 0, 255, 0},
    {"Gear1", "Gear1", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear2", "Gear2", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear3", "Gear3", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear4", "Gear4", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear5", "Gear5", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear6", "Gear6", WL_INPUT_SWITCH, 0, 1, 0},
};

enum
{
    BG4_TUNED = 1,
    BG4_ORIGINAL // the first edition, 2006
};
static int g_bg4; // the edition, 0 for Chase H.Q. 2 and D1GP
static int g_d1gp;
static int g_vlimitr;
static void bg4_tick(void);
static int g_pro;            // Battle Gear 4 Pro's cabinet ([Game] CABINET = PRO)
static unsigned char g_lever; // its second board's inputs: the H shifter's columns

// Both games read 3 switch bytes for one player and 10 analog channels (Chase H.Q. 2; Battle Gear
// 4 asks 8); the accelerator is channel 3 and the brake 4 in both
static void jvs_state(WlJvsState *s)
{
    static int coinWas, low;
    static unsigned short coins;
    int coin = wl_input_get(&g_inputs[IN_COIN]);
    if (coin && !coinWas)
        coins++;
    coinWas = coin;

    s->system = wl_input_get(&g_inputs[IN_TEST]) ? 0x80 : 0;
    if (wl_input_get(&g_inputs[IN_SERVICE]))
        s->player[0] |= 0x4000;
    s->coins[0] = coins;
    s->analog[3] = (unsigned short)(wl_input_get(&g_inputs[IN_GAS]) << 8);
    s->analog[4] = (unsigned short)(wl_input_get(&g_inputs[IN_BRAKE]) << 8);
    if (g_bg4)
    {
        // Battle Gear 4: Start bit 15; the steering is on COM1
        bg4_tick();
        static const unsigned short bits[] = {1u << 13, 1u << 12, 1u << 11, 1u << 9, 1u << 8, 1u << 7};
        if (wl_input_get(&g_inputs[IN_START]))
            s->player[0] |= 0x8000;
        for (int i = 0; i < (g_pro ? 4 : 6); i++)
            if (wl_input_get(&g_inputs[IN_VIEW + i]))
                s->player[0] |= bits[i];
        if (!g_pro)
            return;
        // The Pro's H shifter, as the game reads it (+0x48adf2): its rows on bits 8 ("up": 2, 4,
        // 6) and 7 ("down": 1, 3, 5), active low on this cabinet, none of them the neutral; its
        // columns on the second board, left (1, 2) 0x80 and right (5, 6) 0x40. ShiftUp and
        // ShiftDown give the rows alone: the sequential modes (AT, MT) and the test menu.
        int gear = 0;
        for (int i = 0; i < 6; i++)
            if (wl_input_get(&g_inputs[IN_GEAR1 + i]))
                gear = i + 1;
        int up = (gear && !(gear & 1)) || wl_input_get(&g_inputs[IN_SHIFT_UP]);
        int down = (gear & 1) || wl_input_get(&g_inputs[IN_SHIFT_DOWN]);
        if (!up)
            s->player[0] |= 1u << 8;
        if (!down)
            s->player[0] |= 1u << 7;
        unsigned char lever = 0;
        if (gear == 1 || gear == 2)
            lever |= 0x80;
        if (gear == 5 || gear == 6)
            lever |= 0x40;
        g_lever = lever;
        // The clutch on channel 6 (the game's analog words from +0xde: the accelerator +0xe4, the
        // brake +0xe6, the clutch +0xea read at +0x41e860, only in a race; the test menu's
        // ADJUSTMENT calibrates it)
        s->analog[6] = (unsigned short)(wl_input_get(&g_inputs[IN_CLUTCH]) << 8);
        return;
    }
    if (g_d1gp)
    {
        // D1GP (its I/O test): START bit 3, VIEW CHANGE 2, SHIFT DOWN 8, SHIFT UP 9, SIDE BRAKE
        // 10, SERVICE 14 (above); the wheel and the pedals on its DirectInput wheel (below)
        if (wl_input_get(&g_inputs[IN_START]))
            s->player[0] |= 1u << 3;
        if (wl_input_get(&g_inputs[IN_VIEW]))
            s->player[0] |= 1u << 2;
        if (wl_input_get(&g_inputs[IN_SHIFT_DOWN]))
            s->player[0] |= 1u << 8;
        if (wl_input_get(&g_inputs[IN_SHIFT_UP]))
            s->player[0] |= 1u << 9;
        if (wl_input_get(&g_inputs[IN_SIDE_BRAKE]))
            s->player[0] |= 1u << 10;
        return;
    }
    if (g_vlimitr)
    {
        // Valve Limit R (its I/O check): START bit 15, SERVICE 14 (above), NITRO 13, VIEW 12, SIDE
        // BRAKE 11, SHIFT UP 10, SHIFT DOWN 9; the accelerator on channel 1, the brake on 2 (its
        // "BG3 I/O" library takes 1 to 3, +0x41ac50); the steering on COM1
        static const unsigned short bits[] = {1u << 15, 1u << 13, 1u << 12, 1u << 11, 1u << 10, 1u << 9};
        const int inputs[] = {IN_START, IN_NITRO, IN_VIEW, IN_SIDE_BRAKE, IN_SHIFT_UP, IN_SHIFT_DOWN};
        for (int i = 0; i < 6; i++)
            if (wl_input_get(&g_inputs[inputs[i]]))
                s->player[0] |= bits[i];
        s->analog[1] = (unsigned short)(wl_input_get(&g_inputs[IN_GAS]) << 8);
        s->analog[2] = (unsigned short)(wl_input_get(&g_inputs[IN_BRAKE]) << 8);
        s->analog[3] = s->analog[4] = 0;
        return;
    }
    // Chase H.Q. 2: Start bit 3, the steering on channel 2, the two-position lever held where it
    // was put (bit 13 set in LOW)
    if (wl_input_get(&g_inputs[IN_SHIFT_DOWN]))
        low = 1;
    if (wl_input_get(&g_inputs[IN_SHIFT_UP]))
        low = 0;
    if (wl_input_get(&g_inputs[IN_START]))
        s->player[0] |= 1u << 3;
    if (low)
        s->player[0] |= 1u << 13;
    if (wl_input_get(&g_inputs[IN_NITRO]))
        s->player[0] |= 1u << 2;
    if (wl_input_get(&g_inputs[IN_SHIFT_NITRO]))
        s->player[0] |= 1u << 12;
    s->analog[2] = (unsigned short)(wl_input_get(&g_inputs[IN_STEERING]) << 8);
}

// The outputs logged on change, to work out Chase H.Q. 2's steering motor (I/O test: "STEERING
// MOTOR", both nitro buttons held)
static void jvs_analog_output(const unsigned short *values, int channels)
{
    static unsigned short last[16];
    static int logged;
    if (memcmp(last, values, channels * sizeof values[0]) && logged++ < 400)
    {
        char line[160];
        int n = 0;
        for (int ch = 0; ch < channels; ch++)
            n += snprintf(line + n, sizeof line - n, " %04x", values[ch]);
        wl_log("jvs: analog outputs%s", line);
    }
    memcpy(last, values, channels * sizeof values[0]);
}

// Chase H.Q. 2's steering motor is in its general outputs: a torque of 0..15 in four bits (byte 0
// 0x10, byte 1 0x40, byte 0 0x08, byte 1 0x20, lowest first) and its direction (byte 0 0x20), seen
// in its I/O test's motor test (both nitro buttons held: 0 to 15 and back, then the other way)
static void jvs_output(const unsigned char *data, int length)
{
    static unsigned char last[8];
    static int logged;
    // Chase H.Q. 2's motor (D1GP's is its DirectInput wheel's)
    if (!g_bg4 && !g_d1gp && !g_vlimitr && length >= 2)
    {
        int torque = (data[0] >> 4 & 1) | (data[1] >> 6 & 1) << 1 | (data[0] >> 3 & 1) << 2 | (data[1] >> 5 & 1) << 3;
        if (data[0] & 0x20)
            torque = -torque;
        static int lastTorque, torqueLogged;
        if (torque != lastTorque && torqueLogged++ < 300)
            wl_log("steering motor: %d (steering %d)", torque, wl_input_get(&g_inputs[IN_STEERING]));
        lastTorque = torque;
        wl_ffb_set(torque / 15.0);
    }
    if (length > 8)
        length = 8;
    if (memcmp(last, data, length) && logged++ < 60)
    {
        char line[64];
        int n = 0;
        for (int i = 0; i < length; i++)
            n += snprintf(line + n, sizeof line - n, " %02x", data[i]);
        wl_log("jvs: outputs%s", line);
    }
    memcpy(last, data, length);
}

static WlJvsBoard g_jvs = {"COM2", "SEGA ENTERPRISES,LTD.;I/O BD JVS;837-14572;Ver1.00;2005/10", 2, 2, 2, 10, 10, 20, jvs_state, jvs_output, 1, jvs_analog_output};

// Battle Gear 4 Pro's second board: the shifter's columns (set by the first board's poll) and its
// gate, moved by a motor between the sequential modes (AT, MT) and the H of "6-speed MT". The game
// drives it by the outputs 0x80 and 0x40 in turn, 4 s each (+0x41f098), until its two sensors give
// the position it wants, "SHIFT SEN1" (0x20) alone or "SEN2" (0x10) alone, both active low
// (+0x41df70, +0x41e0a0), then stops it: here the gate is where the last of them sent it, 0x80
// SEN1's side.
static int g_gate = 1; // 1 SEN1's side, 2 SEN2's

static void pro_state(WlJvsState *s)
{
    s->misc[0] = (unsigned char)(g_lever | (g_gate == 1 ? 0x10 : 0x20));
}

static void pro_output(const unsigned char *data, int length)
{
    if (length < 1)
        return;
    int gate = data[0] & 0x80 ? 1 : data[0] & 0x40 ? 2 : g_gate;
    if (gate != g_gate)
        wl_log("shifter gate: to %s's side", gate == 1 ? "SEN1" : "SEN2");
    g_gate = gate;
}

static WlJvsBoard g_jvsPro = {"COM2", "SEGA ENTERPRISES,LTD.;I/O BD JVS;837-14572;Ver1.00;2005/10", 1, 2, 0, 0, 0, 16, pro_state, pro_output, 1, NULL};

// The cabinet's drives D: and E: are there, fixed disks, and E:\ is the current folder (as
// OpenParrot's TypeX2Generic.cpp gives them, GPL v3: the game quits at once without them)
static DWORD(WINAPI *oGetLogicalDrives)(void);
static DWORD WINAPI hGetLogicalDrives(void)
{
    return oGetLogicalDrives() | 1u << 3 | 1u << 4;
}

static UINT(WINAPI *oGetDriveTypeA)(LPCSTR);
static UINT WINAPI hGetDriveTypeA(LPCSTR root)
{
    if (root && (root[0] == 'D' || root[0] == 'd' || root[0] == 'E' || root[0] == 'e') && root[1] == ':')
        return DRIVE_FIXED;
    return oGetDriveTypeA(root);
}

static DWORD(WINAPI *oGetCurrentDirectoryA)(DWORD, LPSTR);
static DWORD WINAPI hGetCurrentDirectoryA(DWORD size, LPSTR buffer)
{
    if (wl_busy())
        return oGetCurrentDirectoryA(size, buffer);
    if (!buffer || size < 4)
        return 4;
    memcpy(buffer, "E:\\", 4);
    return 3;
}

static WlHook g_hooks[] = {
    WL_HOOK(L"kernel32.dll", GetLogicalDrives),
    WL_HOOK(L"kernel32.dll", GetDriveTypeA),
    WL_HOOK(L"kernel32.dll", GetCurrentDirectoryA),
};

// The game's window always the one in front: Battle Gear 4's sound starts only then (+0x255bf0
// compares GetForegroundWindow with its window, and the boot waits for the sound forever, black).
// Only the game's own calls (its import table): windowsloader's keyboard and menu still see the
// real one.
static HWND(WINAPI *oGetForegroundWindow)(void);
static HWND WINAPI hGetForegroundWindow(void)
{
    HWND w = wl_display_hwnd();
    return w ? w : oGetForegroundWindow();
}

// ------------------------------------------------------------------------------
// Sound (Battle Gear 4): the game asks DirectSound for 7.1 speakers (SetSpeakerConfig, a setting of
// the PC on older Windows: ignored), then builds its primary format with as many channels as
// GetSpeakerConfig gives, in a plain WAVEFORMATEX that DirectSound refuses past 2 channels
// (SetFormat: DSERR_ALLOCATED, on a PC set to 5.1 or 7.1); the boot then waits for the sound
// forever, black. It is told stereo: its mixer makes two mono buffers, side left and right.
// ------------------------------------------------------------------------------

static HRESULT(STDMETHODCALLTYPE *oGetSpeakerConfig)(void *, DWORD *);
static HRESULT STDMETHODCALLTYPE hGetSpeakerConfig(void *ds, DWORD *config)
{
    HRESULT hr = oGetSpeakerConfig(ds, config);
    if (SUCCEEDED(hr) && config)
    {
        static int logged;
        if (!logged++ && (*config & 0xFFFF) != DSSPEAKER_STEREO)
            wl_log("sound: speakers 0x%lx given to the game as stereo", *config);
        *config = DSSPEAKER_STEREO;
    }
    return hr;
}

static HRESULT(STDMETHODCALLTYPE *oSetSpeakerConfig)(void *, DWORD);
static HRESULT STDMETHODCALLTYPE hSetSpeakerConfig(void *ds, DWORD config)
{
    return DS_OK;
}

static HRESULT(WINAPI *oDirectSoundCreate8)(LPCGUID, LPDIRECTSOUND8 *, LPUNKNOWN);
static HRESULT WINAPI hDirectSoundCreate8(LPCGUID device, LPDIRECTSOUND8 *out, LPUNKNOWN outer)
{
    HRESULT hr = oDirectSoundCreate8(device, out, outer);
    static int hooked;
    if (SUCCEEDED(hr) && out && *out && !hooked++)
    {
        void **vt = *(void ***)*out; // IDirectSound8: GetSpeakerConfig 8, SetSpeakerConfig 9
        wl_hook_code(vt[8], (void *)hGetSpeakerConfig, (void **)&oGetSpeakerConfig, "IDirectSound8::GetSpeakerConfig");
        wl_hook_code(vt[9], (void *)hSetSpeakerConfig, (void **)&oSetSpeakerConfig, "IDirectSound8::SetSpeakerConfig");
    }
    return hr;
}

static WlHook g_bg4Hooks[] = {
    WL_HOOK(L"dsound.dll", DirectSoundCreate8),
};

// ------------------------------------------------------------------------------
// Chase H.Q. 2's 3D sound: each frame it moves the DirectSound 3D listener, then commits the
// deferred settings. Where DirectSound 3D goes through the audio service for each buffer (this
// laptop's Windows 11: about 0.3 s to create a 3D buffer, a second for a commit with the game's
// forty), the game draws a frame a second. Once a commit has taken more than 100 ms, the next ones
// are skipped: the sounds keep the places they had then (no 3D movement), the game its speed.
// ------------------------------------------------------------------------------

static int g_slowCommits;

static HRESULT(STDMETHODCALLTYPE *oCommitDeferredSettings)(IDirectSound3DListener *);
static HRESULT STDMETHODCALLTYPE hCommitDeferredSettings(IDirectSound3DListener *listener)
{
    if (g_slowCommits)
        return DS_OK;
    ULONGLONG start = GetTickCount64();
    HRESULT hr = oCommitDeferredSettings(listener);
    ULONGLONG ms = GetTickCount64() - start;
    if (ms > 100)
    {
        g_slowCommits = 1;
        wl_log("sound: a 3D update took %llu ms (DirectSound 3D slow on this PC): the next ones skipped, sounds left where they are", ms);
    }
    return hr;
}

static HRESULT(STDMETHODCALLTYPE *oCreateSoundBuffer)(IDirectSound *, LPCDSBUFFERDESC, LPDIRECTSOUNDBUFFER *, LPUNKNOWN);
static HRESULT STDMETHODCALLTYPE hCreateSoundBuffer(IDirectSound *ds, LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER *out, LPUNKNOWN outer)
{
    HRESULT hr = oCreateSoundBuffer(ds, desc, out, outer);
    static int hooked;
    // The listener is the primary buffer's: its vtable hooked once (IDirectSound3DListener: 17)
    if (SUCCEEDED(hr) && !hooked && desc && (desc->dwFlags & DSBCAPS_PRIMARYBUFFER) && (desc->dwFlags & DSBCAPS_CTRL3D))
    {
        IDirectSound3DListener *listener = NULL;
        if (SUCCEEDED((*out)->QueryInterface(IID_IDirectSound3DListener, (void **)&listener)) && listener)
        {
            hooked = 1;
            wl_hook_code((*(void ***)listener)[17], (void *)hCommitDeferredSettings, (void **)&oCommitDeferredSettings,
                         "IDirectSound3DListener::CommitDeferredSettings");
            listener->Release();
        }
    }
    return hr;
}

static HRESULT(WINAPI *oDirectSoundCreate)(LPCGUID, LPDIRECTSOUND *, LPUNKNOWN);
static HRESULT WINAPI hDirectSoundCreate(LPCGUID device, LPDIRECTSOUND *out, LPUNKNOWN outer)
{
    HRESULT hr = oDirectSoundCreate(device, out, outer);
    static int hooked;
    if (SUCCEEDED(hr) && out && *out && !hooked++) // IDirectSound: CreateSoundBuffer 3
        wl_hook_code((*(void ***)*out)[3], (void *)hCreateSoundBuffer, (void **)&oCreateSoundBuffer, "IDirectSound::CreateSoundBuffer");
    return hr;
}

static WlHook g_chq2Hooks[] = {
    WL_HOOK(L"dsound.dll", DirectSoundCreate),
};

// ------------------------------------------------------------------------------
// Battle Gear 4's steering board on COM1 (as OpenParrot's TypeX2Generic.cpp gives it, GPL v3):
// 2-byte commands, each answered by 2 bytes. 0x20 (reset) → A0 00, and the position is no longer
// reported; 0x11 → the position, reported from then on for every command; 0x1F (motor off) → 1F 00
// while it is not reported; any other command → 8C A0 while it is not. The position: the steering
// 0..255 (left to right) as 1024..2044, big-endian (the car turns the way the wheel does, seen in a
// race). 0x0D is the motor's torque, 0x80 none, above it pushing left (a centering spring against
// the wheel turned right, seen in a race); the others set it up (logged).
// ------------------------------------------------------------------------------

static CRITICAL_SECTION g_wheelLock;
static unsigned char g_wheelOut[64], g_wheelCmd;
static int g_wheelCount, g_wheelReport, g_wheelHalf;

static void wheel_answer(unsigned char a, unsigned char b)
{
    if (g_wheelCount + 2 > (int)sizeof g_wheelOut)
        return;
    g_wheelOut[g_wheelCount++] = a;
    g_wheelOut[g_wheelCount++] = b;
}

// Valve Limit R drives the board as Battle Gear 3 does, the board making the forces from their
// parameters (BackForceFeeder's DRIVEBOARD.md, Taito board): 01 a constant torque (0x80 none,
// above to the left), 03 a spring, 05 and 06 a vibration's frequency (Hz) and amplitude; 02 and 04,
// friction and damper, are left out (the wheel's speed is not known). Played at each command (the
// game polls about 30 times a second). The spring's and the vibration's scales are guesses.
// Battle Gear 4 streams its own torque (0x0D) instead.
static void vlr_effects(unsigned char cmd, unsigned char arg)
{
    static int constant = 0x80, spring, sineHz, sineAmp;
    if (cmd == 0x01)
        constant = arg;
    else if (cmd == 0x03)
        spring = arg;
    else if (cmd == 0x05)
        sineHz = arg;
    else if (cmd == 0x06)
        sineAmp = arg;
    double left = (128 - wl_input_get(&g_inputs[IN_STEERING])) / 127.0; // the wheel, +1 at the left end
    double force = (constant - 0x80) / 127.0 - left * spring / 64.0;
    if (sineHz && sineAmp)
        force += sineAmp / 255.0 * sin(GetTickCount() * 0.001 * sineHz * 6.283185307179586);
    wl_ffb_set(force > 1 ? 1 : force < -1 ? -1 : force);
}

static void wheel_command(unsigned char cmd, unsigned char arg)
{
    static int logged;
    static unsigned char last[256];
    static int seen[256];
    if ((!seen[cmd] || last[cmd] != arg) && logged++ < 600)
        wl_log("steering board: command %02x %02x (steering %d)", cmd, arg, wl_input_get(&g_inputs[IN_STEERING]));
    seen[cmd] = 1;
    last[cmd] = arg;
    if (cmd == 0x0D) // the motor's torque, 0x80 none
        wl_ffb_set((arg - 0x80) / 127.0);
    if (g_vlimitr)
        vlr_effects(cmd, arg);
    if (cmd == 0x20)
    {
        g_wheelReport = 0;
        wheel_answer(0xA0, 0x00);
        return;
    }
    if (cmd == 0x11)
        g_wheelReport = 1;
    if (g_wheelReport)
    {
        int v = wl_input_get(&g_inputs[IN_STEERING]) * 4 + 1024;
        wheel_answer((unsigned char)(v >> 8), (unsigned char)v);
    }
    else if (cmd == 0x1F)
        wheel_answer(0x1F, 0x00);
    else
        wheel_answer(0x8C, 0xA0);
}

static BOOL wheel_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    const unsigned char *p = (const unsigned char *)buffer;
    EnterCriticalSection(&g_wheelLock);
    for (DWORD i = 0; i < size; i++)
    {
        if (!g_wheelHalf)
            g_wheelCmd = p[i];
        else
            wheel_command(g_wheelCmd, p[i]);
        g_wheelHalf = !g_wheelHalf;
    }
    LeaveCriticalSection(&g_wheelLock);
    *written = size;
    return TRUE;
}

static BOOL wheel_read(void *buffer, DWORD size, DWORD *read, LONGLONG offset)
{
    EnterCriticalSection(&g_wheelLock);
    DWORD n = size < (DWORD)g_wheelCount ? size : (DWORD)g_wheelCount;
    memcpy(buffer, g_wheelOut, n);
    memmove(g_wheelOut, g_wheelOut + n, g_wheelCount - n);
    g_wheelCount -= n;
    LeaveCriticalSection(&g_wheelLock);
    *read = n;
    return TRUE;
}

static DWORD wheel_modem(void)
{
    return MS_CTS_ON | MS_DSR_ON;
}

static DWORD wheel_waiting(void)
{
    return (DWORD)g_wheelCount;
}

static WlVirtualDevice g_wheel = {"COM1", NULL, wheel_read, wheel_write, wheel_modem, wheel_waiting};

// Each JVS poll (OpenParrot's BG4General, BattleGear4InputMisc.cpp, GPL v3): once the game's
// screen +0x4e3668 has been 8, its pause flag +0x42d29c held at 1 (Tuned's addresses)
static void bg4_tick(void)
{
    static int stopPausing;
    if (g_bg4 != BG4_TUNED)
        return;
    if (*(volatile unsigned char *)0x8E3668 == 8)
        stopPausing = 1;
    if (stopPausing)
        *(volatile unsigned char *)0x82D29C = 1;
}

// Its options ([Game.bg4], the menu's "Options du jeu"): CABINET = PRO, the Professional
// edition's cabinet (wide monitor, clutch, H shifter), its second I/O board on the bus. The game counts the boards at each start (+0x60cb37, +0x600d8c):
// two, a "WIDE MONITOR & CLUTCH TYPE" cabinet (+0x50f464); after a change it rewrites its
// config.bin and closes once. HOLD_GEAR: the gear stays engaged once the shifter leaves it
// (OpenParrot's "Hold Gear", TypeX2Generic.cpp, GPL v3: the neutral's store at +0x8adff taken
// out), for shifters whose positions do not stay.
static const WlOptionChoice g_cabinets[] = {
    {"STANDARD", "Standard"},
    {"PRO", "Pro"},
    {NULL, NULL},
};

static const WlGameOption g_bg4Options[] = {
    {"CABINET", "Borne",
     "Standard : 4:3, boîte AT ou MT séquentielle. Pro : écran large, embrayage et levier en H (une seconde carte d'E/S). Le jeu change "
     "ses propres réglages au lancement suivant et se ferme une fois : le relancer.",
     WL_OPTION_CHOICE, g_cabinets, 0, 0, "STANDARD", 1, NULL},
    {"HOLD_GEAR", "Garder le rapport (borne Pro)", "Le rapport reste engagé quand le levier en H est lâché, pour un levier dont les positions ne tiennent pas.",
     WL_OPTION_BOOL, NULL, 0, 0, "false", 1, NULL},
};

static void bg4_settings(void)
{
    char v[32];
    wl_game_option("CABINET", v, sizeof v);
    if (_stricmp(v, "PRO"))
        return;
    g_pro = 1;
    wl_jvs_add_node(&g_jvsPro);
    wl_log("game: Battle Gear 4 Pro's cabinet");
    if (wl_game_option_bool("HOLD_GEAR"))
    {
        static const unsigned char neutral[10] = {0xC7, 0x81, 0x60, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        static const unsigned char nops[10] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        wl_patch_code((void *)0x48ADFF, neutral, nops, 10, "hold gear");
    }
}

// D1GP's wheel: a DirectInput force feedback wheel the game finds by its product name, the
// cabinet's Immersion one ("<product name>.txt" beside the game maps its buttons and axes, the
// copy's every entry 0); its motor is driven by Immersion's effects (IFC23.dll, data/game.ifr)
static void d1gp_wheel(unsigned short axes[WL_VDINPUT_AXES], unsigned char *buttons)
{
    axes[0] = (unsigned short)(wl_input_get(&g_inputs[IN_STEERING]) * 257);
    axes[1] = (unsigned short)(wl_input_get(&g_inputs[IN_GAS]) * 257);
    axes[2] = (unsigned short)(wl_input_get(&g_inputs[IN_BRAKE]) * 257);
}

// VID and PID made up
static const WlVirtualJoystick g_d1gpWheel = {"Immersion TouchSense Steering Wheel", 0x0001, 0x0001, 16, d1gp_wheel, 1};

static int exists(const wchar_t *dir, const wchar_t *name)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s%s", dir, name);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

int wl_game_taito(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    wchar_t dir[MAX_PATH];
    wcsncpy_s(dir, MAX_PATH, exePath, base - exePath);
    int bg4 = (!_wcsicmp(base, L"game.exe") || !_wcsicmp(base, L"game4.exe")) && exists(dir, L"game.inf") && exists(dir, L"data\\drivedata");
    int chq2 = !_wcsicmp(base, L"game.exe") && exists(dir, L"chq2.ini");
    int d1gp = !bg4 && !_wcsicmp(base, L"game.exe") && exists(dir, L"IFC23.dll") && exists(dir, L"game.inf");
    int vlimitr = !_wcsicmp(base, L"launcher.exe") && exists(dir, L"game.inf") && exists(dir, L"data\\bin2");
    if (!bg4 && !chq2 && !d1gp && !vlimitr)
        return 0;
    // Battle Gear 4's two editions, told apart by their exe's time stamp: Tuned (2007, v2.08, its
    // Pro cabinet included) and the first one (2006, OpenParrot's "BG4_Eng"); anything else is
    // left alone
    if (bg4)
    {
        const unsigned char *exe = (const unsigned char *)GetModuleHandleW(NULL);
        DWORD stamp = *(const DWORD *)(exe + *(const DWORD *)(exe + 0x3C) + 8);
        bg4 = stamp == 0x4675EF6F ? BG4_TUNED : stamp == 0x446D0EFA ? BG4_ORIGINAL : 0;
        if (!bg4)
        {
            wl_log("game: Battle Gear 4, edition not known (time stamp %08lx): observing only", stamp);
            return 0;
        }
    }
    *name = bg4 == BG4_TUNED ? "bg4" : bg4 ? "bg4eng" : d1gp ? "d1gp" : vlimitr ? "vlimitr" : "chasehq2";
    *title = bg4 == BG4_TUNED ? "Battle Gear 4 Tuned" : bg4 ? "Battle Gear 4" : d1gp ? "D1GP Arcade" : vlimitr ? "Valve Limit R" : "Chase H.Q. 2";
    wl_log("game: %s (Taito Type X%s)", *title, bg4 ? "+" : vlimitr ? "" : "2");
    if (bg4 == BG4_TUNED)
        wl_game_set_boards("Controls on JVS board, answered as Sega 837-14572, JVS 3.0 (COM2); a second JVS board on the Pro cabinet "
                           "(H shifter, gate motor)",
                           "Motor board: Taito steering board, Battle Gear protocol (COM1, 38400 bauds)");
    else if (bg4)
        wl_game_set_boards("Controls on JVS board, answered as Sega 837-14572, JVS 3.0 (COM2)",
                           "Motor board: Taito steering board, Battle Gear protocol (COM1, 38400 bauds)");
    else if (vlimitr)
        wl_game_set_boards("Controls on JVS board, answered as Sega 837-14572, JVS 3.0 (COM2)",
                           "Motor board: Taito steering board, Battle Gear protocol (COM1, 38400 bauds)");
    else if (d1gp)
        wl_game_set_boards("Controls on JVS board, answered as Sega 837-14572, JVS 3.0 (COM2); wheel and pedals on a DirectInput wheel "
                           "(\"Immersion TouchSense Steering Wheel\")",
                           "Motor board: none, Immersion TouchSense force feedback (DirectInput effects) on the wheel");
    else
        wl_game_set_boards("Controls on JVS board, answered as Sega 837-14572, JVS 3.0 (COM2)",
                           "Motor board: none, the JVS board's outputs drive the motor (torque 0..15, direction)");

    // D:\ and E:\ are the game's folder
    char folder[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, dir, -1, folder, sizeof folder, NULL, NULL);
    size_t n = strlen(folder);
    if (n && folder[n - 1] == '\\')
        folder[n - 1] = 0;
    wl_files_redirect("D:", folder);
    wl_files_redirect("E:", folder);
    wl_hooks_add(g_hooks, sizeof g_hooks / sizeof g_hooks[0]);
    wl_inputs_set_table(g_inputs, IN_COUNT);
    wl_jvs_register(&g_jvs);
    if (wl_patch_iat(GetModuleHandleW(NULL), "user32.dll", "GetForegroundWindow", (void *)hGetForegroundWindow, (void **)&oGetForegroundWindow))
        wl_log("game: its window always the one in front (GetForegroundWindow)");
    if (chq2)
    {
        // The steering calibration at start skipped (OpenParrot's TypeX2Generic.cpp, GPL v3)
        static const unsigned char jne = 0x75, jmp = 0xEB;
        wl_patch_code((void *)0x4107E3, &jne, &jmp, 1, "calibration skipped");
        wl_hooks_add(g_chq2Hooks, sizeof g_chq2Hooks / sizeof g_chq2Hooks[0]);
        return 1;
    }
    if (d1gp)
    {
        wl_vdinput_register(&g_d1gpWheel);
        g_d1gp = 1;
        return 1;
    }
    if (vlimitr)
    {
        wl_dinput_hide_controllers();
        InitializeCriticalSection(&g_wheelLock);
        wl_vdev_register(&g_wheel);
        wl_vdev_label("COM1", "Carte du volant");
        g_vlimitr = 1;
        return 1;
    }

    // No menu bar on the window (its LoadMenu, Tuned +0x1f21b0, the first edition +0x1b8030,
    // returns at once: OpenParrot's "windowed off", TypeX2Generic.cpp, GPL v3)
    int tuned = bg4 == BG4_TUNED;
    static const unsigned char menuWas[3] = {0x0F, 0xB7, 0x44}, ret4[3] = {0xC2, 0x04, 0x00};
    wl_patch_code((void *)(ULONG_PTR)(tuned ? 0x5F21B0 : 0x5B8030), menuWas, ret4, 3, "no menu bar");
    // The intro, black and stuck without it: a field the game leaves unset is set to 1.0 where it
    // stores its neighbour (OpenParrot's IntroFix, TypeX2Generic.cpp, GPL v3). Code put in padding
    // (Tuned +0x57ace, the first edition +0x46e3e), jumped to from the store (+0x57f01, +0x471b5).
    static const unsigned char pad15[15] = {0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    static const unsigned char cave[15] = {0x89, 0x68, 0x14, 0xC7, 0x40, 0x08, 0x00, 0x00, 0x80, 0x3F, 0x83, 0xC1, 0x08, 0xEB, 0x0A};
    static const unsigned char backTuned[5] = {0xE9, 0x1B, 0x04, 0x00, 0x00}, backFirst[5] = {0xE9, 0x5F, 0x03, 0x00, 0x00};
    static const unsigned char storeWas[6] = {0x89, 0x68, 0x14, 0x83, 0xC1, 0x08};
    static const unsigned char toCaveTuned[6] = {0xE9, 0xC8, 0xFB, 0xFF, 0xFF, 0x90}, toCaveFirst[6] = {0xE9, 0x84, 0xFC, 0xFF, 0xFF, 0x90};
    unsigned char *at = (unsigned char *)(ULONG_PTR)(tuned ? 0x457ACE : 0x446E3E);
    if (wl_patch_code(at, pad15, cave, 15, "intro fix") && wl_patch_code(at + 0x19, pad15, tuned ? backTuned : backFirst, 5, "intro fix, back"))
        wl_patch_code((void *)(ULONG_PTR)(tuned ? 0x457F01 : 0x4471B5), storeWas, tuned ? toCaveTuned : toCaveFirst, 6, "intro fix, jump");
    wl_hooks_add(g_bg4Hooks, sizeof g_bg4Hooks / sizeof g_bg4Hooks[0]);
    // The game takes the first DirectInput game controller, exclusively (vJoy on the test PC)
    wl_dinput_hide_controllers();
    InitializeCriticalSection(&g_wheelLock);
    wl_vdev_register(&g_wheel);
    wl_vdev_label("COM1", "Carte du volant");
    wl_log("emulated steering board on COM1 (Taito)");
    if (tuned)
    {
        wl_game_set_options(g_bg4Options, sizeof g_bg4Options / sizeof g_bg4Options[0]);
        wl_game_on_settings(bg4_settings);
    }
    g_bg4 = bg4;
    return 1;
}
