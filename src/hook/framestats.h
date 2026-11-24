// windowsloader - runs Windows arcade games with emulated cabinet hardware
// Copyright (C) 2026 Benjamin Maurin - njz3
// License: GNU General Public License version 3 (GPL v3)
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE
// file for the full text. The author reserves the right to change the license of this code.

#pragma once

// [Display] FRAME_STATS = true: where the presenting thread's time goes, logged every 10 s (the
// frames' length, Present, the game's Sleep and waits by caller, its CPU time, the rest: blocked
// elsewhere, in the graphics driver mostly). Called by the Present hooks, around the game's Present.
void wl_framestats_present_begin(void);
void wl_framestats_present_end(void);
