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
#include "outputs.h"
#include "settings.h"

#define FFB_EVENTS 512

static SRWLOCK g_lock = SRWLOCK_INIT;
static double g_force;
static unsigned g_serial;
static WlFfbEvent g_events[FFB_EVENTS]; // ring of the changes, for the agent
static int g_first, g_count;

// The gains of the four kinds of effect, 1 for 100 %
static double g_gainConstant = 1, g_gainSpring = 1, g_gainDamper = 1, g_gainSine = 1;

static double read_gain(const char *key)
{
    int v = wl_settings_get_int("ForceFeedback", key, 100);
    return (v < 0 ? 0 : v > 200 ? 200 : v) / 100.0;
}

void wl_ffb_settings(void)
{
    g_gainConstant = read_gain("FFB_GAIN_CONSTANT");
    g_gainSpring = read_gain("FFB_GAIN_SPRING");
    g_gainDamper = read_gain("FFB_GAIN_DAMPER");
    g_gainSine = read_gain("FFB_GAIN_SINE");
    wl_log("ffb: gains constant %.0f%%, spring %.0f%%, damper %.0f%%, sine %.0f%% ([ForceFeedback] FFB_GAIN_*)", g_gainConstant * 100, g_gainSpring * 100,
           g_gainDamper * 100, g_gainSine * 100);
}

static void set_force(double force);

void wl_ffb_set(double force)
{
    set_force(force * g_gainConstant);
}

void wl_ffb_set_parts(double constant, double spring, double damper, double sine)
{
    set_force(constant * g_gainConstant + spring * g_gainSpring + damper * g_gainDamper + sine * g_gainSine);
}

static void set_force(double force)
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
    wl_outputs_force(force); // as the servo board's frames, for BackForceFeeder ([Outputs] OUTPUTS_NETWORK)
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

static double g_spring, g_vibration;
static int g_period;
static unsigned g_effectsSerial;

void wl_ffb_set_effects(double spring, double vibration, int periodMs)
{
    spring *= g_gainSpring;
    vibration *= g_gainSine;
    spring = spring < -1 ? -1 : spring > 1 ? 1 : spring;
    vibration = vibration < 0 ? 0 : vibration > 1 ? 1 : vibration;
    AcquireSRWLockExclusive(&g_lock);
    if (spring != g_spring || vibration != g_vibration || (vibration && periodMs != g_period))
    {
        g_spring = spring;
        g_vibration = vibration;
        g_period = periodMs;
        g_effectsSerial++;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void wl_ffb_get_effects(double *spring, double *vibration, int *periodMs, unsigned *serial)
{
    AcquireSRWLockShared(&g_lock);
    *spring = g_spring;
    *vibration = g_vibration;
    *periodMs = g_period;
    if (serial)
        *serial = g_effectsSerial;
    ReleaseSRWLockShared(&g_lock);
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
