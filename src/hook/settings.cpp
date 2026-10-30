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

// The write folder's settings files older than the launcher folder's ones (WL_ENV_OLDER), UTF-8
static char g_older[2][2][MAX_PATH * 2];
static int g_olderCount;

static void read_older(void)
{
    static wchar_t all[4 * MAX_PATH + 8];
    DWORD n = GetEnvironmentVariableW(WL_ENV_OLDER, all, sizeof all / sizeof all[0]);
    if (!n || n >= sizeof all / sizeof all[0])
        return;
    wchar_t *ctx = NULL;
    for (wchar_t *line = wcstok_s(all, L"\n", &ctx); line && g_olderCount < 2; line = wcstok_s(NULL, L"\n", &ctx))
    {
        wchar_t *bar = wcschr(line, L'|');
        if (!bar)
            continue;
        *bar = 0;
        wl_u8(line, g_older[g_olderCount][0], sizeof g_older[0][0]);
        wl_u8(bar + 1, g_older[g_olderCount][1], sizeof g_older[0][1]);
        wl_log("settings: %s, the one read, is older than %s: the changes made there since are not read (copy that file over "
               "it, or change the one in the write folder)", g_older[g_olderCount][0], g_older[g_olderCount][1]);
        g_olderCount++;
    }
}

int wl_settings_older(int i, const char **copy, const char **first)
{
    if (i < 0 || i >= g_olderCount)
        return 0;
    *copy = g_older[i][0];
    *first = g_older[i][1];
    return 1;
}

void wl_settings_init(void)
{
    char u[MAX_PATH * 2];
    if (!GetEnvironmentVariableW(WL_ENV_INI, g_file, MAX_PATH))
        g_file[0] = 0;
    read_older();
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

const wchar_t *wl_settings_launcher_file(void)
{
    static wchar_t file[MAX_PATH];
    if (!file[0] && !GetEnvironmentVariableW(WL_ENV_INI, file, MAX_PATH))
        file[0] = 0;
    return file;
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

// ------------------------------------------------------------------------------
// wl_ini_merge: the defaults' comments and keys into a settings file
// ------------------------------------------------------------------------------

// A file's lines, without their line ends
struct IniLines
{
    char **line;
    int count, cap;
};

static int lines_insert(IniLines *l, int at, const char *text, size_t len)
{
    if (l->count == l->cap)
    {
        int cap = l->cap ? l->cap * 2 : 64;
        char **grown = (char **)realloc(l->line, cap * sizeof *grown);
        if (!grown)
            return 0;
        l->line = grown;
        l->cap = cap;
    }
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return 0;
    memcpy(copy, text, len);
    copy[len] = 0;
    memmove(l->line + at + 1, l->line + at, (l->count - at) * sizeof *l->line);
    l->line[at] = copy;
    l->count++;
    return 1;
}

static void lines_split(IniLines *l, const char *text)
{
    for (const char *p = text; *p;)
    {
        size_t n = strcspn(p, "\r\n");
        lines_insert(l, l->count, p, n);
        p += n;
        if (*p == '\r')
            p++;
        if (*p == '\n')
            p++;
    }
}

static void lines_free(IniLines *l)
{
    for (int i = 0; i < l->count; i++)
        free(l->line[i]);
    free(l->line);
}

static const char *skip_blanks(const char *s)
{
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) // UTF-8's mark
        s += 3;
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

static int is_blank(const char *s)
{
    return !*skip_blanks(s);
}

static int is_comment(const char *s)
{
    s = skip_blanks(s);
    return *s == '#' || *s == ';';
}

// "[Display]": its name in out
static int is_section(const char *s, char *out, size_t size)
{
    s = skip_blanks(s);
    const char *end = *s == '[' ? strchr(s, ']') : NULL;
    if (!end)
        return 0;
    snprintf(out, size, "%.*s", (int)(end - s - 1), s + 1);
    return 1;
}

// "KEY = value": KEY in out
static int is_key(const char *s, char *out, size_t size)
{
    if (is_comment(s))
        return 0;
    s = skip_blanks(s);
    const char *eq = strchr(s, '=');
    if (!eq || *s == '[')
        return 0;
    size_t len = (size_t)(eq - s);
    while (len && (s[len - 1] == ' ' || s[len - 1] == '\t'))
        len--;
    snprintf(out, size, "%.*s", (int)len, s);
    return len != 0;
}

// The file's line of a section, -1 when it has none; *end the line after its section
static int find_section(const IniLines *l, const char *name, int *end)
{
    char s[96];
    for (int i = 0; i < l->count; i++)
        if (is_section(l->line[i], s, sizeof s) && !_stricmp(s, name))
        {
            int j = i + 1;
            while (j < l->count && !is_section(l->line[j], s, sizeof s))
                j++;
            *end = j;
            return i;
        }
    return -1;
}

static int insert_lines(IniLines *l, int at, char *const *from, int n)
{
    for (int i = 0; i < n; i++)
        lines_insert(l, at + i, from[i], strlen(from[i]));
    return n;
}

int wl_ini_merge(const wchar_t *path, const char *defaults)
{
    char u[MAX_PATH * 2];
    if (!path || !path[0])
        return 0;
    if (!exists(path))
    {
        FILE *f = _wfopen(path, L"wb");
        if (!f)
        {
            wl_log("settings: cannot create %s", wl_u8(path, u, sizeof u));
            return 0;
        }
        fputs(defaults, f);
        fclose(f);
        wl_log("settings: %s created with the defaults", wl_u8(path, u, sizeof u));
        return 1;
    }

    FILE *f = _wfopen(path, L"rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = size >= 0 ? (char *)malloc(size + 1) : NULL;
    size_t got = text ? fread(text, 1, size, f) : 0;
    fclose(f);
    if (!text)
        return 0;
    text[got] = 0;
    const char *firstEnd = strchr(text, '\n');
    int crlf = !firstEnd || (firstEnd > text && firstEnd[-1] == '\r'); // the file's line ends kept
    IniLines l = {0}, d = {0};
    lines_split(&l, text);
    lines_split(&d, defaults);
    free(text);

    int keys = 0, comments = 0;
    char name[96], key[96], s[96];
    // The defaults' heading comment (before their first section), when the file starts without one
    int head = 0, first = 0;
    while (head < d.count && is_comment(d.line[head]))
        head++;
    while (first < l.count && is_blank(l.line[first]))
        first++;
    if (head && (first == l.count || !is_comment(l.line[first])))
    {
        insert_lines(&l, 0, d.line, head);
        lines_insert(&l, head, "", 0);
        comments++;
    }

    char section[96] = "";
    int commentStart = -1; // the defaults' comment lines above the current one
    for (int i = 0; i < d.count; i++)
    {
        const char *line = d.line[i];
        if (is_section(line, name, sizeof name))
        {
            snprintf(section, sizeof section, "%s", name);
            commentStart = -1;
            int end;
            if (find_section(&l, section, &end) >= 0)
                continue;
            // A section missing: the defaults' one whole, at the end
            int j = i + 1;
            while (j < d.count && !is_section(d.line[j], name, sizeof name))
                j++;
            while (j > i + 1 && is_blank(d.line[j - 1]))
                j--;
            if (l.count && !is_blank(l.line[l.count - 1]))
                lines_insert(&l, l.count, "", 0);
            insert_lines(&l, l.count, d.line + i, j - i);
            for (int k = i + 1; k < j; k++)
                keys += is_key(d.line[k], key, sizeof key);
            i = j - 1;
            continue;
        }
        if (is_comment(line))
        {
            if (commentStart < 0)
                commentStart = i;
            continue;
        }
        int nComment = commentStart >= 0 ? i - commentStart : 0;
        char *const *comment = d.line + (commentStart >= 0 ? commentStart : 0);
        commentStart = -1;
        if (!section[0] || !is_key(line, key, sizeof key))
            continue;

        int end, at = find_section(&l, section, &end), found = -1;
        for (int k = at + 1; k < end && found < 0; k++)
            if (is_key(l.line[k], name, sizeof name) && !_stricmp(name, key))
                found = k;
        if (found >= 0)
        {
            // The key there: the defaults' comment above it, unless it has one
            if (!nComment || is_comment(l.line[found - 1]))
                continue;
            int pos = found;
            if (!is_blank(l.line[found - 1]) && !is_section(l.line[found - 1], s, sizeof s))
                lines_insert(&l, pos++, "", 0);
            insert_lines(&l, pos, comment, nComment);
            comments++;
            continue;
        }
        // The key missing: added with its comment after the section's last line
        int pos = end;
        while (pos > at + 1 && is_blank(l.line[pos - 1]))
            pos--;
        if (nComment && !is_section(l.line[pos - 1], s, sizeof s))
            lines_insert(&l, pos++, "", 0);
        pos += insert_lines(&l, pos, comment, nComment);
        lines_insert(&l, pos, line, strlen(line));
        keys++;
    }

    int written = 0;
    if (keys || comments)
    {
        // Written beside the file, then put in its place: a reader never sees half a file
        wchar_t temp[MAX_PATH + 8];
        swprintf(temp, MAX_PATH + 8, L"%s.new", path);
        FILE *out = _wfopen(temp, L"wb");
        if (out)
        {
            for (int i = 0; i < l.count; i++)
                fprintf(out, "%s%s", l.line[i], crlf ? "\r\n" : "\n");
            int ok = !ferror(out);
            fclose(out);
            written = ok && MoveFileExW(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            if (!written)
                DeleteFileW(temp);
        }
        if (written)
            wl_log("settings: %s: %d settings and %d comments of the defaults added", wl_u8(path, u, sizeof u), keys, comments);
        else
            wl_log("settings: %s lacks %d settings and %d comments of the defaults, but cannot be written (error %lu)",
                   wl_u8(path, u, sizeof u), keys, comments, GetLastError());
    }
    lines_free(&l);
    lines_free(&d);
    return written;
}

void wl_settings_merge(const char *defaults)
{
    wchar_t own[MAX_PATH];
    if (wl_settings_common() || (game_file_path(own) && !_wcsicmp(own, g_file)))
        wl_ini_merge(g_file, defaults);
}
