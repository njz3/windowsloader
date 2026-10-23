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

// A DirectInput joystick played by windowsloader, for an I/O board the game reads through
// DirectInput (Daytona Championship USA's "SEGA IO Controller"). Once a game module registers
// it, it is the only game controller the game's DirectInput lists: the PC's own pads and wheels,
// vJoy among them, stay hidden from the game (windowsloader reads them itself, controls.cpp).
// Opening it gives the game a device whose state the module fills.

#define WL_VDINPUT_AXES 6

struct WlVirtualJoystick
{
    const char *name;        // product and instance name the game looks for
    unsigned short vid, pid; // for DIPROP_VIDPID and the product GUID
    int buttons;             // up to 32
    // The state now: axes X, Y, Z, Rx, Ry, Rz in 0..65535 (scaled to the range the game sets),
    // buttons 0 or 1
    void (*fill)(unsigned short axes[WL_VDINPUT_AXES], unsigned char *buttons);
    // 1: a force feedback wheel (X its steering, low values to the left): the effects the game
    // creates on it are played as windowsloader's force (wl_ffb_set)
    int ffb;
};

void wl_vdinput_register(const WlVirtualJoystick *joy);
int wl_vdinput_active(void);
int wl_vdinput_ffb(void); // the joystick is a force feedback wheel

// For dinputhook.cpp
#ifdef DIRECTINPUT_VERSION
void wl_vdinput_instance(DIDEVICEINSTANCEA *out);
int wl_vdinput_is_ours(REFGUID instance);
IDirectInputDevice8A *wl_vdinput_create(void);
#endif
