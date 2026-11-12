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
#include <string.h>

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

static WlFfbEffects g_effects;
static unsigned g_effectsSerial;

static double clamp(double v, double lo, double hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// Under g_lock: the effects changed, a new serial (the first 200 changes logged)
static void effects_changed(const WlFfbEffects *e)
{
    if (memcmp(e, &g_effects, sizeof *e))
    {
        g_effects = *e;
        g_effectsSerial++;
        static int logged;
        if (logged++ < 200)
            wl_log("ffb: effects: spring %.3f at %+.3f (at most %.3f), damper %.3f, vibration %.3f (%d ms)", e->spring, e->springCentre, e->springSaturation,
                   e->damper, e->vibration, e->periodMs);
    }
}

void wl_ffb_set_effects(double spring, double vibration, int periodMs)
{
    spring = clamp(spring * g_gainSpring, -1, 1);
    vibration = clamp(vibration * g_gainSine, 0, 1);
    AcquireSRWLockExclusive(&g_lock);
    WlFfbEffects e = g_effects;
    e.spring = spring;
    e.springCentre = 0;
    e.springSaturation = spring < 0 ? -spring : spring;
    e.vibration = vibration;
    e.periodMs = vibration ? periodMs : g_effects.periodMs;
    effects_changed(&e);
    ReleaseSRWLockExclusive(&g_lock);
}

void wl_ffb_set_conditions(double spring, double springCentre, double springSaturation, double damper)
{
    AcquireSRWLockExclusive(&g_lock);
    WlFfbEffects e = g_effects;
    e.spring = clamp(spring * g_gainSpring, -1, 1);
    e.springCentre = clamp(springCentre, -1, 1);
    e.springSaturation = clamp(springSaturation * g_gainSpring, 0, 1);
    e.damper = clamp(damper * g_gainDamper, 0, 1);
    effects_changed(&e);
    ReleaseSRWLockExclusive(&g_lock);
}

void wl_ffb_set_vibration(double vibration, int periodMs)
{
    AcquireSRWLockExclusive(&g_lock);
    WlFfbEffects e = g_effects;
    e.vibration = clamp(vibration * g_gainSine, 0, 1);
    e.periodMs = e.vibration ? periodMs : g_effects.periodMs;
    effects_changed(&e);
    ReleaseSRWLockExclusive(&g_lock);
}

void wl_ffb_get_effects(WlFfbEffects *e, unsigned *serial)
{
    AcquireSRWLockShared(&g_lock);
    *e = g_effects;
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
