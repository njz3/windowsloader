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

// The force the game asks of the wheel motor, as a constant force -1..1, > 0 pushing the wheel
// to the left (linuxloader's convention). The game module sets it from the emulated hardware;
// controls.cpp plays it on the player's wheel (SDL haptic), the agent reads its changes.

void wl_ffb_set(double force); // the game's own torque: a constant force
double wl_ffb_get(unsigned *serial); // serial changes with each new value

// The same force made of parts, each under its gain ([ForceFeedback] FFB_GAIN_CONSTANT, _SPRING,
// _DAMPER, _SINE of the settings file, percent, 100 by default, 0..200), summed then held to -1..1:
// a constant force (a torque, a ramp), a spring (also one windowsloader makes from a stiffness and
// a position), a damper (friction, inertia, a brake against the wheel's movement) and a vibration
// (a sine or any periodic wave, a shake)
void wl_ffb_set_parts(double constant, double spring, double damper, double sine);

// The gains read (once the settings are known, from DllMain)
void wl_ffb_settings(void);

// Effects the wheel holds by itself, beside the constant force, for a game whose cabinet's motor
// board plays such effects: a spring pulling the wheel to its centre, 0 none to 1 the strongest
// (below 0 pushing it away from the centre), and a vibration, 0 none to 1 the strongest, of a
// period in milliseconds. The wheel answers its own position at once, where a spring made of
// constant forces by the game answers a frame late, through the whole chain, and can swing it
void wl_ffb_set_effects(double spring, double vibration, int periodMs);

// The same spring in full, and a damper, for a board that sets them itself (Sega's servoboard): the
// spring's stiffness (0..1, below 0 pushing away), the point it pulls towards (-1..1 of the wheel's
// half travel, > 0 to the left, 0 the centre) and the most it pulls (0..1); the damper's
// coefficient (0 none to 1 the strongest), a force against the wheel's speed. The vibration is
// left as wl_ffb_set_effects set it
void wl_ffb_set_conditions(double spring, double springCentre, double springSaturation, double damper);

// The vibration alone (0 none to 1 the strongest, its period in milliseconds), the spring and the
// damper left as they are
void wl_ffb_set_vibration(double vibration, int periodMs);

struct WlFfbEffects
{
    double spring, springCentre, springSaturation; // springSaturation: |spring| for wl_ffb_set_effects
    double damper;
    double vibration;
    int periodMs;
};
void wl_ffb_get_effects(WlFfbEffects *e, unsigned *serial); // serial may be NULL

struct WlFfbEvent
{
    unsigned frame;
    double force;
};

// The changes since the last call, oldest first (the agent's "ffb")
int wl_ffb_drain(WlFfbEvent *out, int max);
