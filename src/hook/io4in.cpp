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

// A real Sega 837-15257 "IO4" I/O board on USB, read as a controller. Its input report is the one
// alls.cpp makes for the games that want the board (segatools' board/io4.c): ID 01, 64 bytes, 8
// ADCs (16 bits, a wheel 0x8000 in the middle), 4 rotary encoders, 2 coin counters (the coins << 8),
// the switches of two players (16 bits each), the board's status. It becomes SDL's virtual joystick
// "SEGA IO4 837-15257", found by the controls as any controller (JOY<n>, its GUID in
// [ControllerGUIDs]), bound and captured in the settings menu as one:
//   axes 0..7     ADC 0..7, 0x0000..0xFFFF as -32768..32767
//   buttons 0..15 player 1's switches, bit n; 16..31 player 2's; 32 and 33 a coin in chute 1 or 2
//                 (pressed 100 ms for each coin counted)
// SWDC's cabinet wires (amdaemon's config.json): ADC 0 the wheel, 1 the accelerator, 2 the brake;
// player 1 bit 9 Test, 7 Start, 6 Service, 5..2 up, down, left, right, 1 the view; player 2 bits
// 15..12 the wheel's blue, green, red, yellow buttons, 1 and 0 the paddles.
// BackForceFeeder reads it the same way (IOCommAgents/IO4Sega.cs, the same offsets and bits).
// Read only: no command is sent (the board sends its reports by itself, as BackForceFeeder finds it),
// the board's lamps stay the game's. Its switches are taken as they
// come, a pressed one at 1; a board whose first report has most of them at 1 (the unwired inputs)
// gives them the other way: they are then inverted (told in the log).

#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "hooks.h"
#include "io4in.h"
#include "log.h"

#define IO4_VID 0x0CA3
#define IO4_PID 0x0021
#define IO4_AXES 8
#define IO4_SWITCHES 32
#define IO4_BUTTONS (IO4_SWITCHES + 2)
#define COIN_MS 100
#define SCAN_MS 3000

// The last report, written by the reader's thread, given to SDL by the joystick's update (the
// controls' thread, as SDL polls)
static volatile LONG g_axes[IO4_AXES];
static volatile LONG g_switches;      // player 1 in bits 0-15, player 2 in 16-31, a pressed one at 1
static volatile LONG g_coinUntil[2];  // GetTickCount's time the coin's button is released
static volatile LONG g_id;            // the virtual joystick, 0 when the board is not there

static void SDLCALL io4_update(void *)
{
    SDL_Joystick *j = SDL_GetJoystickFromID((SDL_JoystickID)g_id);
    if (!j)
        return;
    for (int a = 0; a < IO4_AXES; a++)
        SDL_SetJoystickVirtualAxis(j, a, (Sint16)(g_axes[a] - 32768));
    unsigned sw = (unsigned)g_switches;
    for (int b = 0; b < IO4_SWITCHES; b++)
        SDL_SetJoystickVirtualButton(j, b, (sw >> b) & 1);
    DWORD now = GetTickCount();
    for (int c = 0; c < 2; c++)
        SDL_SetJoystickVirtualButton(j, IO4_SWITCHES + c, (LONG)(now - (DWORD)g_coinUntil[c]) < 0);
}

static void attach(void)
{
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_UNKNOWN; // not a gamepad: its own axes and buttons, JOY<n>_
    desc.vendor_id = IO4_VID;
    desc.product_id = IO4_PID;
    desc.naxes = IO4_AXES;
    desc.nbuttons = IO4_BUTTONS;
    desc.name = "SEGA IO4 837-15257";
    desc.Update = io4_update;
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    InterlockedExchange(&g_id, (LONG)id);
    if (id)
        wl_log("io4: the board is the controller \"%s\" (%d axes, %d buttons)", desc.name, IO4_AXES, IO4_BUTTONS);
    else
        wl_log("io4: no virtual joystick (%s): the board is not read", SDL_GetError());
}

static void detach(void)
{
    SDL_JoystickID id = (SDL_JoystickID)InterlockedExchange(&g_id, 0);
    if (id)
    {
        SDL_DetachVirtualJoystick(id);
        wl_log("io4: the board is gone");
    }
}

// The board's HID interface, opened to be read; its path in path
static HANDLE find_board(wchar_t *path, size_t size)
{
    GUID hid;
    HidD_GetHidGuid(&hid);
    HDEVINFO set = SetupDiGetClassDevsW(&hid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    HANDLE found = INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DATA iface = {sizeof iface};
    for (DWORD i = 0; found == INVALID_HANDLE_VALUE && SetupDiEnumDeviceInterfaces(set, NULL, &hid, i, &iface); i++)
    {
        union
        {
            SP_DEVICE_INTERFACE_DETAIL_DATA_W detail;
            char space[sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + 512 * sizeof(wchar_t)];
        } d;
        d.detail.cbSize = sizeof d.detail;
        if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, &d.detail, sizeof d, NULL, NULL))
            continue;
        wchar_t lower[512];
        wcsncpy_s(lower, 512, d.detail.DevicePath, _TRUNCATE);
        _wcslwr_s(lower, 512);
        if (!wcsstr(lower, L"vid_0ca3&pid_0021"))
            continue;
        // Shared: the game (amdaemon) or BackForceFeeder may read it too, each gets the reports
        HANDLE h = CreateFileW(d.detail.DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
        if (h == INVALID_HANDLE_VALUE)
        {
            wl_log("io4: the board's interface cannot be opened (error %lu)", GetLastError());
            continue;
        }
        PHIDP_PREPARSED_DATA pre = NULL;
        HIDP_CAPS caps = {};
        if (HidD_GetPreparsedData(h, &pre))
        {
            HidP_GetCaps(pre, &caps);
            HidD_FreePreparsedData(pre);
        }
        if (caps.InputReportByteLength < 34)
        {
            CloseHandle(h); // another collection of the board's, without its inputs
            continue;
        }
        wcsncpy_s(path, size, d.detail.DevicePath, _TRUNCATE);
        found = h;
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

static unsigned get16(const unsigned char *p)
{
    return p[0] | p[1] << 8;
}

// Its reports until it is unplugged
static void read_board(HANDLE h)
{
    wchar_t product[128] = L"";
    HidD_GetProductString(h, product, sizeof product);
    wl_log("io4: board found, \"%ls\"", product);

    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    unsigned char r[256]; // at least the interface's report length, whatever it is
    int first = 1, invert = 0, waited = 0;
    unsigned coins[2] = {0, 0};
    for (;;)
    {
        DWORD got = 0;
        ResetEvent(ov.hEvent);
        if (!ReadFile(h, r, sizeof r, NULL, &ov) && GetLastError() != ERROR_IO_PENDING)
            break;
        if (WaitForSingleObject(ov.hEvent, 1000) == WAIT_TIMEOUT)
        {
            CancelIo(h);
            GetOverlappedResult(h, &ov, &got, TRUE);
            if (++waited == 3 && first)
                wl_log("io4: no report from the board yet (it may wait for amdaemon's setup)");
            continue;
        }
        if (!GetOverlappedResult(h, &ov, &got, FALSE))
            break;
        if (got < 34 || r[0] != 0x01)
            continue;
        unsigned sw = get16(r + 29) | get16(r + 31) << 16;
        if (first)
        {
            wl_log_hex("io4: first report", r, got);
            // Most inputs are not wired: at 1, the board gives its switches the other way
            int ones = 0;
            for (int b = 0; b < IO4_SWITCHES; b++)
                ones += (sw >> b) & 1;
            invert = ones > IO4_SWITCHES * 3 / 4;
            if (invert)
                wl_log("io4: %d of its %d switches at 1 at rest: taken inverted (a pressed one at 0)", ones, IO4_SWITCHES);
            coins[0] = get16(r + 25) >> 8;
            coins[1] = get16(r + 27) >> 8;
        }
        for (int a = 0; a < IO4_AXES; a++)
            InterlockedExchange(&g_axes[a], (LONG)get16(r + 1 + 2 * a));
        InterlockedExchange(&g_switches, (LONG)(invert ? ~sw : sw));
        for (int c = 0; c < 2; c++)
        {
            unsigned count = get16(r + 25 + 2 * c) >> 8;
            if (count != coins[c])
                InterlockedExchange(&g_coinUntil[c], (LONG)(GetTickCount() + COIN_MS));
            coins[c] = count;
        }
        if (first)
        {
            first = 0;
            attach();
        }
    }
    wl_log("io4: the board stopped answering (error %lu)", GetLastError());
    CloseHandle(ov.hEvent);
}

static DWORD WINAPI io4_thread(LPVOID)
{
    wl_busy_enter(); // the real board: windowsloader's device hooks are the game's
    for (;;)
    {
        wchar_t path[512];
        HANDLE h = find_board(path, 512);
        if (h != INVALID_HANDLE_VALUE)
        {
            read_board(h);
            CloseHandle(h);
            detach();
        }
        Sleep(SCAN_MS);
    }
}

void wl_io4_start(void)
{
    HANDLE t = CreateThread(NULL, 0, io4_thread, NULL, 0, NULL);
    if (t)
        CloseHandle(t);
}
