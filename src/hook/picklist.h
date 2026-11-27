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

#include "inputs.h"

// A choice made with the cabinet's controls before the game shows anything (NASCAR's track, which
// its frontend gives on the cabinet): a window of windowsloader's over the whole screen, a list.
// The wheel (or the axis given) turned, or the lever's two positions, move through it; Start
// chooses, once it has been seen released (a press left from the frontend does not). The
// keyboard's arrows and Enter, and the mouse, too. ExitGame still ends the game (controls.cpp).
struct WlPickControls
{
    const WlInput *axis;   // left: up the list, right: down; NULL for none
    const WlInput *up;     // a switch, NULL for none
    const WlInput *down;   // a switch, NULL for none
    const WlInput *choose; // a switch (Start)
};

// title and hint UTF-8 (translated by the caller), labels UTF-8. selected: the one chosen at first.
// timeout: seconds after which the one selected is taken, 0 to wait. The index chosen, or -1 when
// the window could not be made or was closed.
int wl_pick_list(const char *title, const char *hint, const char *const *labels, int count, int selected, const WlPickControls *controls,
                 int timeout);
