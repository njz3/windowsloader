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
#include <wchar.h>

// Log written by the DLL inside the game. Every line goes straight to the file (no buffering):
// the launcher echoes it live, and nothing is lost when the game crashes.

void wl_log_open(const wchar_t *path);
// No file given (the write folder cannot be written): the lines go to the debugger's output
int wl_log_to_debugger(void);
void wl_log_close(void);
void wl_log(const char *fmt, ...);

// prefix followed by the bytes in hex (at most 64, then "...")
void wl_log_hex(const char *prefix, const void *data, size_t len);

// UTF-16 to UTF-8 into buf ("(null)" for NULL)
const char *wl_u8(const wchar_t *w, char *buf, size_t size);

// A path's file name: what follows its last \ or /, the whole path without either
const char *wl_path_name(const char *path);
const wchar_t *wl_path_name(const wchar_t *path);

// An IPv4 address, in network order, as text ("192.168.1.2") into buf
const char *wl_ip_text(unsigned long ip, char *buf, size_t size);
