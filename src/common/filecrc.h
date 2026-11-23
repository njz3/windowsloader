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

#include <windows.h>

// A file's CRC-32 (zlib's, as the cabinets' copies and the game list give them), 0 when it cannot
// be read: how a game whose program keeps the same name in every build is told (Raw Thrills'
// sdaemon.exe), its time stamp may change from one copy to another
DWORD wl_file_crc32(const wchar_t *path);
