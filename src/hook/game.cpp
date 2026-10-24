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

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "settings.h"

static const char *g_name = "unknown";
static const char *g_title = "jeu inconnu";
static const char *g_controls, *g_motor;
static void (*g_onSettings)(void);

const char *wl_game_name(void)
{
    return g_name;
}

const char *wl_game_title(void)
{
    return g_title;
}

void wl_game_set_boards(const char *controls, const char *motor)
{
    g_controls = controls;
    g_motor = motor;
    wl_log("game: %s; %s", controls ? controls : "no controls board", motor ? motor : "no motor board");
}

const char *wl_game_controls(void)
{
    return g_controls;
}

const char *wl_game_motor(void)
{
    return g_motor;
}

void wl_game_init(void)
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (wl_game_segarally3(exe))
    {
        g_name = "segarally3";
        g_title = "Sega Rally 3";
    }
    else if (wl_game_daytona(exe, &g_title))
        g_name = "daytona";
    else if (wl_game_fordracing(exe, &g_title))
        g_name = "fordracing";
    else if (!wl_game_codemasters(exe, &g_name, &g_title) && !wl_game_ringedge(exe, &g_name, &g_title) &&
             !wl_game_taito(exe, &g_name, &g_title) && !wl_game_konami(exe, &g_name, &g_title) &&
             !wl_game_triotech(exe, &g_name, &g_title) && !wl_game_namco(exe, &g_name, &g_title) &&
             !wl_game_unis(exe, &g_name, &g_title) && !wl_game_alls(exe, &g_name, &g_title))
        wl_log("game not recognized: observing only");
}

void wl_game_on_settings(void (*fn)(void))
{
    g_onSettings = fn;
}

// ------------------------------------------------------------------------------
// The game's own options
// ------------------------------------------------------------------------------

#define MAX_OPTIONS 32

static const WlGameOption *g_options;
static int g_optionCount;
static char g_started[MAX_OPTIONS][WL_OPTION_VALUE_SIZE], g_edited[MAX_OPTIONS][WL_OPTION_VALUE_SIZE];
static char g_section[64];

void wl_game_set_options(const WlGameOption *options, int count)
{
    g_options = options;
    g_optionCount = count < MAX_OPTIONS ? count : MAX_OPTIONS;
}

const char *wl_game_option_section(void)
{
    if (!g_section[0])
        snprintf(g_section, sizeof g_section, "Game.%s", g_name);
    return g_section;
}

static const WlGameOption *find_option(const char *key)
{
    for (int i = 0; i < g_optionCount; i++)
        if (!_stricmp(g_options[i].key, key))
            return &g_options[i];
    return NULL;
}

void wl_game_option(const char *key, char *out, size_t size)
{
    const WlGameOption *o = find_option(key);
    wl_settings_get(wl_game_option_section(), key, o && o->def ? o->def : "", out, size);
}

int wl_game_option_bool(const char *key)
{
    const WlGameOption *o = find_option(key);
    return wl_settings_get_bool(wl_game_option_section(), key, o && o->def && !_stricmp(o->def, "true"));
}

int wl_game_option_int(const char *key)
{
    const WlGameOption *o = find_option(key);
    return wl_settings_get_int(wl_game_option_section(), key, o && o->def ? atoi(o->def) : 0);
}

int wl_game_option_count(void)
{
    return g_optionCount;
}

const WlGameOption *wl_game_option_at(int i)
{
    return i >= 0 && i < g_optionCount ? &g_options[i] : NULL;
}

char *wl_game_option_edit(int i)
{
    return g_edited[i];
}

const char *wl_game_option_started(int i)
{
    return g_started[i];
}

// The edited values, in the game's section of the file
int wl_game_options_save(const wchar_t *file)
{
    int ok = 1;
    for (int i = 0; i < g_optionCount; i++)
        ok &= wl_settings_write(file, wl_game_option_section(), g_options[i].key, g_edited[i]);
    return ok;
}

// The values the game starts with (a box as true/false), the menu's edits from them, then the
// module's part that reads them
void wl_game_settings(void)
{
    for (int i = 0; i < g_optionCount; i++)
    {
        const WlGameOption *o = &g_options[i];
        if (o->type == WL_OPTION_BOOL)
            snprintf(g_started[i], WL_OPTION_VALUE_SIZE, "%s", wl_game_option_bool(o->key) ? "true" : "false");
        else
            wl_game_option(o->key, g_started[i], WL_OPTION_VALUE_SIZE);
        memcpy(g_edited[i], g_started[i], WL_OPTION_VALUE_SIZE);
        wl_log("game: option [%s] %s = %s", wl_game_option_section(), o->key, g_started[i]);
    }
    if (g_onSettings)
        g_onSettings();
}
