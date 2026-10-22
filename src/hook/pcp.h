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

#include <stddef.h>

// PCP, the text protocol of Sega's amLib (libpcp) between a game and the services of its
// platform: on a RingEdge or RingWide the keychip, storage and network daemons listen on
// 127.0.0.1:40100 and up. The server sends the prompt ">", the game a request line
// "keyword=value&keyword=value" (a value "?" asks for it), the server its answer line in the same
// form, then a new prompt.

// Answers one request line (without its end of line) into response; returns 0 to close the
// connection
typedef int (*WlPcpHandler)(const char *request, char *response, size_t size);

// A server on 127.0.0.1:port, in a thread of its own
void wl_pcp_serve(unsigned short port, const char *name, WlPcpHandler handler);

// The value of keyword in a request ("" when absent); returns value
const char *wl_pcp_value(const char *request, const char *keyword, char *value, size_t size);
