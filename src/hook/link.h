// windowsloader - runs Windows arcade games with emulated cabinet hardware
// Copyright (C) 2026 Benjamin Maurin - njz3
// License: GNU General Public License version 3 (GPL v3)
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE
// file for the full text. The author reserves the right to change the license of this code.

// The link between cabinets of a game whose cabinets find each other on their own network, by a
// broadcast, then talk to each other's addresses (Ford Racing, GTI Club, DiRT Showdown): the PCs
// of [Game.<name>] LINK are its network (link.cpp). Without LINK the game stays alone, off the
// local network (system.cpp).

#pragma once

#include <windows.h>

// [Game.<name>] LINK and CABINET read (from the module's settings callback), and linked, the game's
// sockets made the link's (its import table's: ws2_32 by number or by name, iphlpapi); alone, they
// stay system.cpp's (off the local network). Returns the cabinets' count, 0 alone
int wl_link_settings(HMODULE exe);
// This cabinet's number, 1.. (CABINET, or its PC's place in LINK); 1 alone
int wl_link_cabinet(void);
int wl_link_count(void);
