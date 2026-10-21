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

#pragma once

// The cabinet's controls as the game reads them: each game module declares its table (names as
// in linuxloader's controls.ini), the emulated board reads the raw values from it, and the
// controllers or the agent write them.

enum WlInputType
{
    WL_INPUT_AXIS,   // steering: -1..1, +1 at the left end
    WL_INPUT_ANALOG, // pedal: 0..1
    WL_INPUT_SWITCH  // button: 0 or 1
};

struct WlInput
{
    const char *name;   // the agent's name
    const char *action; // controls.ini key; an axis also takes ACTION_Left / ACTION_Right, a pedal ACTION_Digital
    WlInputType type;
    int rawLow;  // what the game reads at the left end of an axis, for a released pedal, an open switch
    int rawHigh; // at the right end of an axis, for a pedal pressed down, a closed switch
    int rawRest; // at rest: centre of an axis, released pedal, open switch
    volatile long raw;
    int held;    // set by the agent: the controllers leave it alone
};

void wl_inputs_set_table(WlInput *inputs, int count);
int wl_inputs_count(void);
WlInput *wl_input_at(int index);
WlInput *wl_input_find(const char *name);

// value in the agent's units (axis -1..1 with +1 left, analog 0..1, switch 0/1) to raw
int wl_input_raw_of(const WlInput *in, double value);
void wl_input_hold(WlInput *in, int raw);
void wl_input_release(WlInput *in);
void wl_inputs_release_all(void);

static inline int wl_input_get(const WlInput *in)
{
    return (int)in->raw;
}
