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

#include <windows.h>

#include "agent.h"
#include "ffb.h"
#include "log.h"

#define FFB_EVENTS 512

static SRWLOCK g_lock = SRWLOCK_INIT;
static double g_force;
static unsigned g_serial;
static WlFfbEvent g_events[FFB_EVENTS]; // ring of the changes, for the agent
static int g_first, g_count;

void wl_ffb_set(double force)
{
    if (force > 1)
        force = 1;
    if (force < -1)
        force = -1;
    AcquireSRWLockExclusive(&g_lock);
    if (force != g_force)
    {
        g_force = force;
        g_serial++;
        WlFfbEvent *e = &g_events[(g_first + g_count) % FFB_EVENTS];
        e->frame = wl_d3d9_frames();
        e->force = force;
        if (g_count < FFB_EVENTS)
            g_count++;
        else
            g_first = (g_first + 1) % FFB_EVENTS;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

double wl_ffb_get(unsigned *serial)
{
    AcquireSRWLockShared(&g_lock);
    double f = g_force;
    if (serial)
        *serial = g_serial;
    ReleaseSRWLockShared(&g_lock);
    return f;
}

int wl_ffb_drain(WlFfbEvent *out, int max)
{
    AcquireSRWLockExclusive(&g_lock);
    int n = g_count < max ? g_count : max;
    for (int i = 0; i < n; i++)
        out[i] = g_events[(g_first + i) % FFB_EVENTS];
    g_first = (g_first + n) % FFB_EVENTS;
    g_count -= n;
    ReleaseSRWLockExclusive(&g_lock);
    return n;
}
