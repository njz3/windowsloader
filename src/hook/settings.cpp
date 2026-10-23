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

// windowsloader.ini, the game's own <short name>.ini and --set (settings.h). Written for
// windowsloader with the rules of linuxloader's config.c (useGameConfigFile, applyOverrides).

#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "settings.h"
#include "wlshared.h"

static wchar_t g_file[MAX_PATH];
static wchar_t g_gameFile[MAX_PATH];

// --set's settings, in their order (the last one of a key wins)
enum
{
    MAX_OVERRIDES = 32
};
static struct
{
    char section[32];
    char key[64];
    char value[192];
} g_set[MAX_OVERRIDES];
static int g_setCount;

static const wchar_t *base_name(const wchar_t *path)
{
    const wchar_t *slash = wcsrchr(path, L'\\'), *other = wcsrchr(path, L'/');
    if (other > slash)
        slash = other;
    return slash ? slash + 1 : path;
}

static int exists(const wchar_t *path)
{
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

// <short name>.ini next to the settings file; 0 when no game is known
static int game_file_path(wchar_t *out)
{
    const char *name = wl_game_name();
    if (!g_file[0] || !name || !name[0] || !strcmp(name, "unknown"))
        return 0;
    const wchar_t *base = base_name(g_file);
    swprintf(out, MAX_PATH, L"%.*s%S.ini", (int)(base - g_file), g_file, name);
    return 1;
}

static void parse_overrides(void)
{
    static const char *sections[] = {"Display"}; // windowsloader.ini's, as it writes them
    wchar_t all[8192];
    DWORD n = GetEnvironmentVariableW(WL_ENV_SET, all, 8192);
    if (!n || n >= 8192)
        return;
    char text[16384];
    WideCharToMultiByte(CP_UTF8, 0, all, -1, text, sizeof text, NULL, NULL);
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n"))
    {
        char *equals = strchr(line, '='), *dot = strchr(line, '.');
        if (!equals || !dot || dot > equals)
        {
            wl_log("settings: --set %s: expected SECTION.KEY=VALUE", line);
            continue;
        }
        if (g_setCount == MAX_OVERRIDES)
        {
            wl_log("settings: more than %d --set, %s ignored", MAX_OVERRIDES, line);
            continue;
        }
        *dot = *equals = 0;
        snprintf(g_set[g_setCount].section, sizeof g_set[0].section, "%s", line);
        for (size_t s = 0; s < sizeof sections / sizeof sections[0]; s++)
            if (!_stricmp(line, sections[s]))
                snprintf(g_set[g_setCount].section, sizeof g_set[0].section, "%s", sections[s]);
        // Game.KEY: the game launched's own section, [Game.<short name>] (as linuxloader's)
        if (!_stricmp(line, "Game"))
            snprintf(g_set[g_setCount].section, sizeof g_set[0].section, "Game.%s", wl_game_name());
        snprintf(g_set[g_setCount].key, sizeof g_set[0].key, "%s", dot + 1);
        for (char *c = g_set[g_setCount].key; *c; c++)
            *c = (char)toupper((unsigned char)*c);
        snprintf(g_set[g_setCount].value, sizeof g_set[0].value, "%s", equals + 1);
        wl_log("settings: --set %s.%s = %s", g_set[g_setCount].section, g_set[g_setCount].key, g_set[g_setCount].value);
        g_setCount++;
    }
}

void wl_settings_init(void)
{
    char u[MAX_PATH * 2];
    if (!GetEnvironmentVariableW(WL_ENV_INI, g_file, MAX_PATH))
        g_file[0] = 0;
    parse_overrides();
    wchar_t own[MAX_PATH];
    if (wl_settings_common() && game_file_path(own) && exists(own))
    {
        wcscpy_s(g_file, MAX_PATH, own);
        wl_log("settings: the game's own file %s", wl_u8(g_file, u, sizeof u));
    }
    else if (g_file[0])
        wl_log("settings: %s%s", wl_u8(g_file, u, sizeof u), exists(g_file) ? "" : " (missing: the defaults)");
}

const wchar_t *wl_settings_file(void)
{
    return g_file;
}

int wl_settings_common(void)
{
    return g_file[0] && !_wcsicmp(base_name(g_file), L"windowsloader.ini");
}

void wl_settings_get(const char *section, const char *key, const char *def, char *out, size_t size)
{
    for (int i = g_setCount - 1; i >= 0; i--)
        if (!_stricmp(g_set[i].section, section) && !_stricmp(g_set[i].key, key))
        {
            snprintf(out, size, "%s", g_set[i].value);
            return;
        }
    snprintf(out, size, "%s", def);
    if (!g_file[0])
        return;
    wchar_t s[64], k[64], d[256], v[256];
    MultiByteToWideChar(CP_UTF8, 0, section, -1, s, 64);
    MultiByteToWideChar(CP_UTF8, 0, key, -1, k, 64);
    MultiByteToWideChar(CP_UTF8, 0, def, -1, d, 256);
    GetPrivateProfileStringW(s, k, d, v, 256, g_file);
    WideCharToMultiByte(CP_UTF8, 0, v, -1, out, (int)size, NULL, NULL);
}

int wl_settings_get_int(const char *section, const char *key, int def)
{
    char v[64];
    wl_settings_get(section, key, "", v, sizeof v);
    char *end;
    long n = strtol(v, &end, 10);
    return end == v ? def : (int)n;
}

int wl_settings_get_bool(const char *section, const char *key, int def)
{
    char v[64];
    wl_settings_get(section, key, "", v, sizeof v);
    if (!_stricmp(v, "true") || !_stricmp(v, "yes") || !_stricmp(v, "on"))
        return 1;
    if (!_stricmp(v, "false") || !_stricmp(v, "no") || !_stricmp(v, "off"))
        return 0;
    char *end;
    long n = strtol(v, &end, 10);
    return end == v ? def : n != 0;
}

int wl_settings_write(const wchar_t *file, const char *section, const char *key, const char *value)
{
    if (!file || !file[0])
        return 0;
    wchar_t s[64], k[64], v[256];
    MultiByteToWideChar(CP_UTF8, 0, section, -1, s, 64);
    MultiByteToWideChar(CP_UTF8, 0, key, -1, k, 64);
    MultiByteToWideChar(CP_UTF8, 0, value, -1, v, 256);
    return WritePrivateProfileStringW(s, k, v, file) != 0;
}

const wchar_t *wl_settings_game_file(int create)
{
    if (!game_file_path(g_gameFile))
        return NULL;
    if (create && !exists(g_gameFile))
    {
        // The file read copied, its other settings kept (as linuxloader's "Save for this game")
        char u[MAX_PATH * 2];
        if (exists(g_file) && !CopyFileW(g_file, g_gameFile, TRUE))
        {
            wl_log("settings: cannot copy the settings to %s (error %lu)", wl_u8(g_gameFile, u, sizeof u), GetLastError());
            return NULL;
        }
        wl_log("settings: the game's own file %s made", wl_u8(g_gameFile, u, sizeof u));
    }
    if (create)
        wcscpy_s(g_file, MAX_PATH, g_gameFile); // the next saves go there
    return g_gameFile;
}
