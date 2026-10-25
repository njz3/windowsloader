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

// The write folder, linuxloader's WRITE_DIR (--write-dir, else [Paths] WRITE_DIR of the settings
// file): the launcher's folder and the games' are then only read. Its root holds windowsloader.ini
// and controls.ini (copied there from their first place the first time) and the log; the DLL makes
// WRITE_DIR\<game> for what the game writes (src/hook/files.cpp).

// The root, absolute and made, from arg, else from iniPath's [Paths] WRITE_DIR (relative to the
// launcher's folder selfDir): 1, 0 when there is none, -1 when it cannot be made (root says which)
int wl_write_dir_root(const wchar_t *selfDir, const wchar_t *arg, const wchar_t *iniPath, wchar_t *root, size_t size);
// A settings file at the root: copied there from *path the first time; path becomes the root's
void wl_write_dir_file(const wchar_t *root, const wchar_t *name, wchar_t *path, size_t size);
