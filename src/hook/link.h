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
// A game that gives its cabinets addresses of its own, net.k for cabinet k (Sonic & Sega All-Stars
// Racing: 192.168.88.<its CABINET ID>): net.k is LINK's PC k, net.255 a broadcast to each of them;
// its bind on net.k is on this PC's address of LINK, what it hears from PC k comes from net.k.
// The game's own number (its test menu's) must then be its place in LINK. net: "192.168.88"
void wl_link_virtual_net(const char *net);
// A game that tells the others its PC's name to connect to (Wasteland Racers: its DirectPlay host,
// which Windows' dpnet.dll resolves itself, a VPN's address possibly first): its gethostname gives
// its address of LINK ("192.168.1.4")
void wl_link_name_is_address(void);
// Windows' DirectPlay (dpnet.dll, loaded by the game's CoCreateInstance: D1GP, Wasteland Racers,
// Wacky Races, Valve Limit R) linked: it finds the PC's cards (GetAdaptersAddresses,
// GetAdaptersInfo, gethostbyname of its name) as the game does, the card of LINK alone, so it
// listens there and not on a VPN's. Called after each CoCreateInstance (system.cpp), once
void wl_link_dpnet(void);
// This cabinet's number, 1.. (CABINET, or its PC's place in LINK); 1 alone
int wl_link_cabinet(void);
int wl_link_count(void);
// LINK's PC k (1..) as text ("192.168.1.4"); 0 when there is none
int wl_link_address(int k, char *buf, size_t size);
