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

// Game modules: each one recognizes its game and sets up the hardware it emulates for it.
// Returns 1 when the game is its own.

int wl_game_segarally3(const wchar_t *exePath);
int wl_game_daytona(const wchar_t *exePath, const char **title); // both editions
int wl_game_fordracing(const wchar_t *exePath, const char **title); // the game or its GameShell
int wl_game_codemasters(const wchar_t *exePath, const char **name, const char **title); // GRID, DiRT Showdown
int wl_game_ringedge(const wchar_t *exePath, const char **name, const char **title); // Sega RingEdge and RingWide games
int wl_game_taito(const wchar_t *exePath, const char **name, const char **title);    // Taito Type X+ (Battle Gear 4)
int wl_game_konami(const wchar_t *exePath, const char **name, const char **title);   // Konami PC (GTI Club 3)
int wl_game_triotech(const wchar_t *exePath, const char **name, const char **title); // Trio Tech PC (Wasteland Racers 2071)
int wl_game_namco(const wchar_t *exePath, const char **name, const char **title);    // Namco System ES3 (Mario Kart Arcade GP DX), ES3X (Wangan Midnight 5, x64)
int wl_game_unis(const wchar_t *exePath, const char **name, const char **title);     // UNIS PC (Crazy Ride, x64)
int wl_game_alls(const wchar_t *exePath, const char **name, const char **title);     // Sega ALLS (SWDC, x64: amdaemon and the game)
void wl_allnet_start(unsigned gateway); // allnet.cpp, in amdaemon: ALL.Net and the title server on this PC (gateway 0xC0A8AC01: 192.168.172.1)
void wl_allnet_game(void);              // allnet.cpp, in the game: its title server's connections to that server
int wl_game_stormracer(const wchar_t *exePath, const char **name, const char **title); // Storm Racer G (usb_io.dll)
int wl_game_cardrw(const wchar_t *exePath, const char **name, const char **title);     // Initial D 7 and 8's card unit (picodaemon.exe)

// The game recognized by wl_game_init, "unknown" when none: only the observer then runs
const char *wl_game_name(void);
const char *wl_game_title(void); // "Sega Rally 3", for the menu
// A companion (the launcher's, started beside the game: picodaemon, amdaemon): the program of the
// game it goes with (WINDOWSLOADER_COMPANION_OF), NULL for the game itself
const wchar_t *wl_game_companion_of(void);
// The name of the game's folder in the write folder (WRITE_DIR\<it>): the game's short name; a
// companion's module gives its game's, so both write in the same place
const char *wl_game_write_name(void);
void wl_game_set_write_name(const char *name);
void wl_game_init(void);

// The cabinet's boards, for the menu's "Jeu" page, given by the game's module: how the controls
// reach the game ("Controls on JVS board, Sega 837-14572, JVS 3.0 (COM4)") and the wheel's motor
// ("Motor board: Sega-like servoboard (COM1)"); NULL for none
void wl_game_set_boards(const char *controls, const char *motor);
const char *wl_game_controls(void);
const char *wl_game_motor(void);

// A game module's part that needs the settings (its options): given by the module while it
// recognizes its game, called once the settings are read, before the game runs
void wl_game_on_settings(void (*fn)(void));
void wl_game_settings(void);

// A game's own options, as linuxloader has them: section [Game.<short name>] of the settings file
// ([Game.bg4]), keys in capitals; a list's values in capitals, true/false for a box, a number for
// a slider; --set Game.KEY=VALUE for the game launched. The menu's "Options du jeu" page lists
// them; "Sauvegarder" and "Sauvegarder pour ce jeu" write them with the other settings.
enum
{
    WL_OPTION_BOOL,   // a box
    WL_OPTION_CHOICE, // a list
    WL_OPTION_INT     // a slider, min..max
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
    void (*apply)(const char *value); // the others: told each change, or NULL
};

// Given by the module while it recognizes its game
void wl_game_set_options(const WlGameOption *options, int count);
// A value: --set's, else the file's, else the default
void wl_game_option(const char *key, char *out, size_t size);
int wl_game_option_bool(const char *key);
int wl_game_option_int(const char *key);

// For the menu: the options, the value being edited (first the one read at the start) and the one
// at the start, the section's name, and the edited values written to a file
int wl_game_option_count(void);
const WlGameOption *wl_game_option_at(int i);
char *wl_game_option_edit(int i); // WL_OPTION_VALUE_SIZE bytes
const char *wl_game_option_started(int i);
const char *wl_game_option_section(void); // "Game.bg4"
int wl_game_options_save(const wchar_t *file);
#define WL_OPTION_VALUE_SIZE 64
