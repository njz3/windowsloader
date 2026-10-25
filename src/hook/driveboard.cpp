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

// Sega's drive board (the wheel's motor board of Lindbergh and RingEdge driving cabinets) on an
// emulated serial port. The game writes 4-byte frames: opcode, two 7-bit values, a checksum; each
// read gets one status byte (0x00 ready, 0x44 busy). The protocol is BackForceFeeder's
// DRIVEBOARD.md ("Sega servoboard") as linuxloader's notes give it:
// - FF reset, 80 effects on/off, 81 maximum force, 83 gain, 84 constant torque, 85 sine, 86 damper,
//   87/88/8B spring, 9D/9E parameters, FB play a page, FC the wheel's bounds test (busy once);
// - 84: torque = ((v1 << 7) | v2) - 0x80, -128..127, > 0 to the left.
// The torque and the sine (85, its amplitude) go to the force feedback (ffb.cpp); the other
// effects are logged only. K.O. Drive (RingWide) reads 4 bytes as its answer: the status, zeros.
// Sonic & Sega All-Stars Racing checks the status byte: 3 bits, repeated in the high nibble; it
// wants 1 (0x11) after the 81 and 82 settings, 0 after a status asked (FD).

#include <windows.h>

#include "driveboard.h"
#include "ffb.h"
#include "log.h"
#include "outputs.h"
#include "vdev.h"

#define BOARD_READY 0x00
#define BOARD_BUSY 0x44
#define BOARD_SET 0x11

static unsigned char g_frame[4];
static int g_used;
static volatile LONG g_status = BOARD_READY;
// One answer per frame received, the bytes waiting for ClearCommError: Sonic & Sega All-Stars
// Racing (+0x41a7b0) reads while there are some. A read with none still gets the status.
static unsigned char g_answers[64];
static int g_answerFirst, g_answerCount;
static CRITICAL_SECTION g_lock;
static int g_boundsTested;
static unsigned g_logged;

// The power-on test's free wheel: K.O. Drive (+0x4a50d0) gives a torque of +8 then -8 and wants
// the wheel's position on each side in turn; Sonic (+0x41b280) waits for it to rest against each
// stop, 0x80 apart at least. A positive torque turns it to the left.
static int g_freeTest;
static volatile LONG g_freeRunning, g_freeSteering = 128;

void wl_driveboard_free_wheel_test(void)
{
    g_freeTest = 1;
}

int wl_driveboard_free_wheel(int *steering)
{
    if (!g_freeRunning)
        return 0;
    *steering = g_freeSteering;
    return 1;
}

static void frame(const unsigned char *f)
{
    int value = ((f[1] & 0x7F) << 7 | (f[2] & 0x7F)) - 0x80;
    if (g_freeTest && (f[0] == 0xFF || f[0] == 0x83) && g_freeRunning != (f[0] == 0xFF))
    {
        g_freeRunning = f[0] == 0xFF; // the reset starts the test, the gain ends it
        g_freeSteering = 128;
        wl_log("driveboard: power-on test %s, the wheel %s", g_freeRunning ? "started" : "done", g_freeRunning ? "left free" : "back to the player");
    }
    // Each command logged when its values change (the status asks, FD, are not)
    static unsigned short last[256];
    unsigned short values = (unsigned short)(f[1] << 8 | f[2] | 0x8000);
    if (f[0] != 0xFD && last[f[0]] != values && g_logged++ < 1000)
        wl_log("driveboard: %02x %02x %02x %02x", f[0], f[1], f[2], f[3]);
    last[f[0]] = values;
    // The force: the torque, and the sine's amplitude turned each way at every frame (the game
    // sends one at each of its frames, 30 a second): K.O. Drive's shocks are 85 1E 0A for a frame
    // or two
    static int torque, sine, side;
    switch (f[0])
    {
    case 0x84:
        if (g_freeRunning && value)
            g_freeSteering = value > 0 ? 0x20 : 0xE0;
        torque = value;
        break;
    case 0x85:
        sine = f[1] & 0x7F;
        break;
    case 0xFC: // bounds test: busy the first time it is asked
        g_status = g_boundsTested++ ? BOARD_READY : BOARD_BUSY;
        return;
    default:
        break;
    }
    if (f[0] == 0x84 || f[0] == 0x85 || sine)
    {
        int force = torque + (sine ? (side ^= 1) ? sine : -sine : 0);
        wl_ffb_set((force > 127 ? 127 : force < -127 ? -127 : force) / 127.0);
    }
    g_status = BOARD_READY;
}

static BOOL board_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    const unsigned char *b = (const unsigned char *)buffer;
    wl_outputs_drive(b, size); // as they come, for BackForceFeeder ([Outputs] OUTPUTS_NETWORK)
    for (DWORD i = 0; i < size; i++)
    {
        g_frame[g_used++] = b[i];
        if (g_used == 4)
        {
            frame(g_frame);
            EnterCriticalSection(&g_lock);
            if (g_answerCount < (int)sizeof g_answers)
                g_answers[(g_answerFirst + g_answerCount++) % sizeof g_answers] =
                    (unsigned char)(g_frame[0] == 0x81 || g_frame[0] == 0x82 ? BOARD_SET : g_status);
            LeaveCriticalSection(&g_lock);
            g_used = 0;
        }
    }
    *written = size;
    return TRUE;
}

static BOOL board_read(void *buffer, DWORD size, DWORD *read, LONGLONG offset)
{
    if (!size)
    {
        *read = 0;
        return TRUE;
    }
    EnterCriticalSection(&g_lock);
    unsigned char answer = (unsigned char)g_status;
    if (g_answerCount)
    {
        answer = g_answers[g_answerFirst];
        g_answerFirst = (g_answerFirst + 1) % sizeof g_answers;
        g_answerCount--;
    }
    LeaveCriticalSection(&g_lock);
    ((unsigned char *)buffer)[0] = answer;
    *read = 1;
    return TRUE;
}

static DWORD board_waiting(void)
{
    return (DWORD)g_answerCount;
}

static DWORD board_modem(void)
{
    return MS_CTS_ON | MS_DSR_ON;
}

static WlVirtualDevice g_port = {NULL, NULL, board_read, board_write, board_modem, board_waiting};

void wl_driveboard_register(const char *port)
{
    InitializeCriticalSection(&g_lock);
    g_port.name = port;
    wl_vdev_register(&g_port);
    wl_log("emulated drive board on %s", port);
}
