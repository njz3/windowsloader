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

// Taito Type X+ and X2: Battle Gear 4 Tuned and Chase H.Q. 2 (game.exe, x86, Direct3D 9). The game
// finds its settings and saves on D:\ (and Battle Gear 4 its data in E:\data): its own folder. Its
// JVS I/O board is on COM2; Battle Gear 4's wheel drive board on COM1.

#include <windows.h>
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
#include "vdev.h"

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
    IN_COUNT
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
};

static int g_bg4;
static void bg4_tick(void);

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
        for (int i = 0; i < 6; i++)
            if (wl_input_get(&g_inputs[IN_VIEW + i]))
                s->player[0] |= bits[i];
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
    if (!g_bg4 && length >= 2)
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
// screen +0x4e3668 has been 8, its pause flag +0x42d29c held at 1
static void bg4_tick(void)
{
    static int stopPausing;
    if (*(volatile unsigned char *)0x8E3668 == 8)
        stopPausing = 1;
    if (stopPausing)
        *(volatile unsigned char *)0x82D29C = 1;
}

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
    if (!bg4 && !chq2)
        return 0;
    *name = bg4 ? "bg4" : "chasehq2";
    *title = bg4 ? "Battle Gear 4 Tuned" : "Chase H.Q. 2";
    wl_log("game: %s (Taito Type X%s)", *title, bg4 ? "+" : "2");

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

    // No menu bar on the window (its LoadMenu, +0x1f21b0, returns at once: OpenParrot's "windowed
    // off", TypeX2Generic.cpp, GPL v3)
    static const unsigned char menuWas[3] = {0x0F, 0xB7, 0x44}, ret4[3] = {0xC2, 0x04, 0x00};
    wl_patch_code((void *)0x5F21B0, menuWas, ret4, 3, "no menu bar");
    // The intro, black and stuck without it: a field the game leaves unset is set to 1.0 where it
    // stores its neighbour (OpenParrot's IntroFix, TypeX2Generic.cpp, GPL v3). Code put in the
    // padding at +0x57ace, jumped to from +0x57f01.
    static const unsigned char pad15[15] = {0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    static const unsigned char cave[15] = {0x89, 0x68, 0x14, 0xC7, 0x40, 0x08, 0x00, 0x00, 0x80, 0x3F, 0x83, 0xC1, 0x08, 0xEB, 0x0A};
    static const unsigned char back[5] = {0xE9, 0x1B, 0x04, 0x00, 0x00};
    static const unsigned char storeWas[6] = {0x89, 0x68, 0x14, 0x83, 0xC1, 0x08}, toCave[6] = {0xE9, 0xC8, 0xFB, 0xFF, 0xFF, 0x90};
    if (wl_patch_code((void *)0x457ACE, pad15, cave, 15, "intro fix") && wl_patch_code((void *)0x457AE7, pad15, back, 5, "intro fix, back"))
        wl_patch_code((void *)0x457F01, storeWas, toCave, 6, "intro fix, jump");
    wl_hooks_add(g_bg4Hooks, sizeof g_bg4Hooks / sizeof g_bg4Hooks[0]);
    InitializeCriticalSection(&g_wheelLock);
    wl_vdev_register(&g_wheel);
    wl_log("emulated steering board on COM1 (Taito)");
    g_bg4 = 1;
    return 1;
}
