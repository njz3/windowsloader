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
// WRITE_DIR\<game> for what the game writes (src/hook/files.cpp). A write folder that cannot be
// made, or a file not made, written and removed there, is not used, and nothing is written in its
// stead (the owner's rule for all his emulators, 31/10/2026): the settings and the game's files are
// read in their usual places, never written there, the writes lost, no log.

// The root, absolute and made, from arg, else from iniPath's [Paths] WRITE_DIR (relative to the
// launcher's folder selfDir): 1, 0 when there is none, -1 when it cannot be made, -2 when a file
// cannot be written there (root says which; wl_write_dir_error the error)
int wl_write_dir_root(const wchar_t *selfDir, const wchar_t *arg, const wchar_t *iniPath, wchar_t *root, size_t size);
unsigned long wl_write_dir_error(void);
// A settings file at the root: copied there from *path the first time; path becomes the root's.
// 1 when the copy kept is older than *path's file: the changes made there since are not read
// (linuxloader warns the same way); 0 otherwise; -1 when the copy fails (path left as it was)
int wl_write_dir_file(const wchar_t *root, const wchar_t *name, wchar_t *path, size_t size);

// [Launcher] LOG of the settings file: the log, windowsloader.log, written (1, the default) or not
// (false, 0, no, off). It is beside the launcher, or at the write folder's root when there is one,
// made anew at each start; --log names another. Changed in the picker or the game's menu, it counts
// from the next start
int wl_log_setting(const wchar_t *iniPath);
