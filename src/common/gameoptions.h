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

// A game's own options, as linuxloader has them: section [Game.<short name>] of the settings file
// ([Game.bg4]), keys in capitals; a list's values in capitals, true/false for a box, a number for
// a slider. Their tables are gameoptions.cpp's, one for each game (or games alike), for the DLL
// (game.h: its module takes its table, the menu's "Options du jeu" page) and for the picker,
// which shows and saves them without starting the game.

#pragma once

enum
{
    WL_OPTION_BOOL,   // a box
    WL_OPTION_CHOICE, // a list
    WL_OPTION_INT,    // a slider, min..max
    WL_OPTION_TEXT    // a field (a path)
};

struct WlOptionChoice
{
    const char *value; // in capitals, as in the file
    const char *label;
};

struct WlGameOption
{
    const char *key;   // in capitals
    const char *label; // for the menu
    const char *help;  // a sentence under it, or NULL
    int type;
    const WlOptionChoice *choices; // WL_OPTION_CHOICE: ended by {NULL, NULL}
    int min, max;                  // WL_OPTION_INT
    const char *def;               // the default, as written in the file
    int nextStart;                 // 1: taken at the game's next start
    void (*apply)(const char *value); // the others: told each change, or NULL (the DLL's module sets it)
    // WL_OPTION_CHOICE: the list made when the menu shows it (the PC's cameras), in place of choices
    // (the DLL's module sets it)
    const WlOptionChoice *(*list)(void);
};

#define WL_OPTION_VALUE_SIZE 260 // a path

// A table by its name ("bg4", "mkdx105"): NULL, *count 0, for none
const WlGameOption *wl_game_options_table(const char *id, int *count);
// The table of a game of the picker's list (gamelist.h): its short name and version; NULL for none
const char *wl_game_options_table_of(const char *name, const char *version);

// A setting's value as a box's: 1 for true, yes, on or a number not 0, 0 for false, no, off or 0
// (any case), def for anything else
int wl_parse_bool(const char *v, int def);

// A value as the game reads it: a box's as true or false (true, yes, on or a number not 0), the
// others as they are; empty, the default
void wl_game_option_normalize(const WlGameOption *o, const char *read, char *out, int size);

// The option in Dear ImGui: a box, a list (choices: its values, o->choices when NULL), a slider or a
// field, its label translated; value edited in place (WL_OPTION_VALUE_SIZE bytes). 1 when changed
int wl_game_option_widget(const WlGameOption *o, char *value, const WlOptionChoice *choices);

// The camera's texts and its first two choices (auto, the still picture)
extern const char g_wlCameraLabel[], g_wlCameraHelp[], g_wlCameraPictureLabel[], g_wlCameraPictureHelp[];
extern const WlOptionChoice g_wlCameraChoicesBase[];
