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
#include <math.h>
#include <string.h>

#include "inputs.h"

static WlInput *g_inputs;
static int g_count;

void wl_inputs_set_table(WlInput *inputs, int count)
{
    g_inputs = inputs;
    g_count = count;
    for (int i = 0; i < count; i++)
    {
        inputs[i].raw = inputs[i].rawRest;
        inputs[i].held = 0;
    }
}

int wl_inputs_count(void)
{
    return g_count;
}

WlInput *wl_input_at(int index)
{
    return index >= 0 && index < g_count ? &g_inputs[index] : NULL;
}

WlInput *wl_input_find(const char *name)
{
    for (int i = 0; i < g_count; i++)
        if (!_stricmp(g_inputs[i].name, name))
            return &g_inputs[i];
    return NULL;
}

int wl_input_raw_of(const WlInput *in, double v)
{
    switch (in->type)
    {
    case WL_INPUT_AXIS:
        if (v > 1)
            v = 1;
        if (v < -1)
            v = -1;
        // +1 is the left end, like a real wheel turned fully left
        return (int)floor(v >= 0 ? in->rawRest + v * (in->rawLow - in->rawRest) + 0.5 : in->rawRest - v * (in->rawHigh - in->rawRest) + 0.5);
    case WL_INPUT_ANALOG:
        if (v > 1)
            v = 1;
        if (v < 0)
            v = 0;
        return (int)floor(in->rawLow + v * (in->rawHigh - in->rawLow) + 0.5);
    default:
        return v != 0 ? in->rawHigh : in->rawLow;
    }
}

// The agent's hold and the controllers' write take the same lock: a controller that read its
// value before the agent held the input cannot write it over the agent's
static SRWLOCK g_holdLock = SRWLOCK_INIT;

void wl_input_hold(WlInput *in, int raw)
{
    AcquireSRWLockExclusive(&g_holdLock);
    in->held = 1;
    InterlockedExchange(&in->raw, raw);
    ReleaseSRWLockExclusive(&g_holdLock);
}

void wl_input_release(WlInput *in)
{
    AcquireSRWLockExclusive(&g_holdLock);
    in->held = 0;
    InterlockedExchange(&in->raw, in->rawRest);
    ReleaseSRWLockExclusive(&g_holdLock);
}

int wl_input_set_unheld(WlInput *in, int raw)
{
    AcquireSRWLockExclusive(&g_holdLock);
    int held = in->held;
    if (!held)
        InterlockedExchange(&in->raw, raw);
    ReleaseSRWLockExclusive(&g_holdLock);
    return !held;
}

void wl_inputs_release_all(void)
{
    for (int i = 0; i < g_count; i++)
        wl_input_release(&g_inputs[i]);
}
