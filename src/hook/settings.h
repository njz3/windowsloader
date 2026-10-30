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

// The settings file, as linuxloader chooses and reads its own:
// - windowsloader.ini next to the launcher, or the file of the launcher's -c / --config;
// - the game's own file, <short name>.ini in the same folder ("gticlub3.ini"), read in its place
//   when it exists and the file is called windowsloader.ini (a -c file of another name is read as
//   it is); a whole file, not an overlay: the menu's "Sauvegarder pour ce jeu" makes it the first
//   time from a copy of the file read;
// - the launcher's --set SECTION.KEY=VALUE over the file's values (the section in any case, the
//   key in capitals), never written to the file by themselves.

// Once the game is known (its short name): the file to read
void wl_settings_init(void);

// The file read, and saved to by the menu's "Sauvegarder"; L"" when the launcher gave none
const wchar_t *wl_settings_file(void);
// The launcher's file (windowsloader.ini or -c's), even when the game's own file is read: [Launcher]
// LANGUAGE's, read by the launcher only from it; L"" when the launcher gave none
const wchar_t *wl_settings_launcher_file(void);
// 1 when it is called windowsloader.ini (the common one or a -c file of that name): only then is a
// game's own file looked for, and the defaults written when it is missing
int wl_settings_common(void);
// With a write folder: its i-th settings file (windowsloader.ini, controls.ini) older than the one of
// the launcher's folder it was copied from, whose changes since are not read (the launcher's check,
// logged at the start, shown by the menu); 0 past the last
int wl_settings_older(int i, const char **copy, const char **first);

// A value: --set's, else the file's, else def (UTF-8)
void wl_settings_get(const char *section, const char *key, const char *def, char *out, size_t size);
int wl_settings_get_int(const char *section, const char *key, int def);
int wl_settings_get_bool(const char *section, const char *key, int def); // true/false, yes/no, on/off, 1/0

// Writes a value to the file (not to --set's)
int wl_settings_write(const wchar_t *file, const char *section, const char *key, const char *value);

// The game's own file, <short name>.ini next to the settings file: made from a copy of the file
// read when missing, then the file the next saves go to. Its path, or NULL (no game known)
const wchar_t *wl_settings_game_file(int create);

// A settings file as its defaults text (the file written new) has it, its values kept: written new
// when missing; else each section and key it lacks is added with the comment the defaults give it,
// and a key of the file without a comment above it gets that comment. The file's own lines, values
// and order are kept; nothing is written when nothing is missing. 1 when the file was written
int wl_ini_merge(const wchar_t *path, const char *defaults);

// The settings file read (windowsloader.ini or the game's own) merged with the defaults; a -c file of
// another name is left as it is
void wl_settings_merge(const char *defaults);
