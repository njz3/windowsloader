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

// The IC card reader-writer of Initial D 7 and 8 (RingEdge): a card unit with a printer and an
// RFID reader, on a serial port of picodaemon.exe, the daemon the cabinet's game.bat starts beside
// the game. The game reaches the daemon by shared memory (zmShare00000532); without it, error 5101
// "no answer from the card reader-writer" after the coin. windowsloader starts picodaemon too
// (launcher.cpp, the companions) and plays the unit here, read in picodaemon.exe:
//   picodaemon -> unit  STX, length (the data's + 6), command, 0, 0, 0, data, ETX, the XOR of the
//                       bytes from the length to ETX (+0x404850); the unit answers ACK
//   picodaemon -> unit  ENQ (+0x404710); the unit answers STX, length, then that many bytes: the
//                       command, its three status bytes (R the card, P the printer, S the unit),
//                       the data, ETX and the XOR
// No card is ever inserted. R is a bit field here: picodaemon keeps it for the game (+0x4010e0:
// bits 0-1 a card inside) and, after an eject, waits for its bits 0-4 to clear, the card taken
// (+0x402696): 0x20 is no card, 0x30 ('0') would be a card at the slot. P and S are '0' (no error,
// no job), no data. After an eject (0x80) the game says "take your card": the card stays 2 s at
// the slot (0x34), then is taken. A read (0x33) waits for a card (S '4') until the game cancels it:
// read as done, it would be an empty card (error 5110, "the card's version differs").

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "vdev.h"

#define STX 0x02
#define ETX 0x03
#define ENQ 0x05
#define ACK 0x06

static unsigned char g_in[512], g_out[512];
static int g_inLength, g_outLength;
static unsigned char g_command; // the last command acknowledged, answered at the next ENQ
static int g_pending;
static DWORD g_ejected; // when the last eject was asked, 0 when none
static CRITICAL_SECTION g_lock;

static void queue(const unsigned char *bytes, int n)
{
    if (g_outLength + n <= (int)sizeof g_out)
    {
        memcpy(g_out + g_outLength, bytes, n);
        g_outLength += n;
    }
}

static void answer(unsigned char command)
{
    unsigned char card = 0x20;
    if (g_ejected && GetTickCount() - g_ejected < 2000)
        card = 0x34;
    unsigned char job = command == 0x33 ? '4' : '0';
    unsigned char r[8] = {STX, 6, command, card, '0', job, ETX, 0};
    for (int i = 1; i < 7; i++)
        r[7] ^= r[i];
    queue(r, sizeof r);
}

static BOOL unit_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    const unsigned char *b = (const unsigned char *)buffer;
    *written = size;
    EnterCriticalSection(&g_lock);
    for (DWORD i = 0; i < size; i++)
    {
        // Each ENQ is answered, the last command's state: picodaemon asks again while a command
        // runs (the cleaning, 0xa0)
        if (g_inLength == 0 && b[i] == ENQ)
        {
            if (g_pending)
                answer(g_command);
            continue;
        }
        if (g_inLength == 0 && b[i] != STX)
            continue;
        if (g_inLength < (int)sizeof g_in)
            g_in[g_inLength++] = b[i];
        // STX, the length, then that many bytes
        if (g_inLength >= 2 && g_inLength == g_in[1] + 2)
        {
            static int logged;
            if (logged++ < 200)
            {
                char hex[3 * 64 + 1];
                int n = 0;
                for (int k = 0; k < g_inLength && k < 64; k++)
                    n += snprintf(hex + n, sizeof hex - n, "%02x ", g_in[k]);
                wl_log("card unit: command %02x: %s", g_in[2], hex);
            }
            g_command = g_in[2];
            g_pending = 1;
            if (g_command == 0x80)
                g_ejected = GetTickCount();
            g_inLength = 0;
            unsigned char ack = ACK;
            queue(&ack, 1);
        }
    }
    LeaveCriticalSection(&g_lock);
    return TRUE;
}

static BOOL unit_read(void *buffer, DWORD size, DWORD *read, LONGLONG offset)
{
    EnterCriticalSection(&g_lock);
    DWORD n = (DWORD)g_outLength < size ? (DWORD)g_outLength : size;
    memcpy(buffer, g_out, n);
    memmove(g_out, g_out + n, g_outLength - n);
    g_outLength -= n;
    LeaveCriticalSection(&g_lock);
    *read = n;
    return TRUE;
}

static DWORD unit_modem(void)
{
    return MS_CTS_ON | MS_DSR_ON;
}

static DWORD unit_waiting(void)
{
    return (DWORD)g_outLength;
}

// picodaemon tries COM2, then COM1 (+0x40d30c)
static WlVirtualDevice g_unit2 = {"COM2", NULL, unit_read, unit_write, unit_modem, unit_waiting};
static WlVirtualDevice g_unit1 = {"COM1", NULL, unit_read, unit_write, unit_modem, unit_waiting};

int wl_game_cardrw(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    if (_wcsicmp(base, L"picodaemon.exe"))
        return 0;
    *name = "picodaemon";
    *title = "picodaemon (Initial D card reader-writer)";
    wl_log("game: the card reader-writer's daemon (picodaemon.exe), no card inserted");
    InitializeCriticalSection(&g_lock);
    wl_vdev_register(&g_unit2);
    wl_vdev_register(&g_unit1);
    return 1;
}
