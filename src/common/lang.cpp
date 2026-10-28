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

// The interface's languages (lang.h): the gettext catalogues lang\<code>.po read as they are, the
// French texts' translations looked up in a table of their own.
//
// What is read of a .po file: msgctxt, msgid, msgstr (msgstr[0] with a plural, the others left
// out), the continued lines, C's escapes, the "fuzzy" flag; the comments and the obsolete entries
// (#~) are left out. An entry whose translation has other %-conversions than its French text
// (printf's formats: "%d", "%s"...) is left out too, the French text kept: a mistaken catalogue does
// not bring a format that would read the wrong arguments.

#include <windows.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"

#include "lang.h"

enum
{
    MAX_LANGUAGES = 32,
    MAX_MISSING = 512,
    MAX_CATALOGUE = 16 << 20
};

struct Catalog
{
    char code[16];
    char name[64];     // its header's X-Language-Name ("English"), as linuxloader's catalogues
    char *text;        // the file's texts unescaped, one after the other
    const char **ids;  // an entry's French text (with a context: the context, \x04, the text)
    const char **strs; // its translation
    int count, cap;
    int *slots; // open addressing: an entry's index + 1, 0 for none
    unsigned mask;
    Catalog *next;
};

static wchar_t g_dir[MAX_PATH];
static void (*g_log)(const char *message);
static Catalog *g_catalogs;       // each one read, kept to the end
static Catalog *volatile g_shown; // NULL: French
static char g_setting[16] = "AUTO";
static char g_code[16] = "fr";
static char g_codes[MAX_LANGUAGES][16], g_names[MAX_LANGUAGES][64];
static int g_count;
// The texts without a translation, told once each (their hash)
static SRWLOCK g_missingLock = SRWLOCK_INIT;
static unsigned g_missing[MAX_MISSING];
static int g_missingCount;

static void say(const char *fmt, ...)
{
    if (!g_log)
        return;
    char text[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    g_log(text);
}

static unsigned hash(const char *s)
{
    unsigned h = 2166136261u; // FNV-1a
    for (; *s; s++)
        h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

// ------------------------------------------------------------------------------
// A catalogue
// ------------------------------------------------------------------------------

// The %-conversions of a printf format in their order, their flags and sizes left out ("d s" for
// "%d of %-8s"), "%%" too
static void conversions(const char *s, char *out, size_t size)
{
    size_t n = 0;
    while (*s)
    {
        if (*s++ != '%')
            continue;
        if (*s == '%')
        {
            s++;
            continue;
        }
        while (*s && strchr("-+ #0'", *s))
            s++;
        for (; *s && strchr("0123456789.*", *s); s++)
            if (*s == '*' && n + 1 < size)
                out[n++] = '*';
        for (; *s && strchr("hlLzjtwI", *s); s++)
        {
            if (n + 1 < size)
                out[n++] = *s;
            if (*s == 'I') // I32, I64
                while (isdigit((unsigned char)s[1]))
                {
                    if (n + 1 < size)
                        out[n++] = s[1];
                    s++;
                }
        }
        if (*s)
        {
            if (n + 1 < size)
                out[n++] = *s;
            s++;
        }
        if (n + 1 < size)
            out[n++] = ' ';
    }
    out[n] = 0;
}

static int same_conversions(const char *a, const char *b)
{
    char ca[256], cb[256];
    conversions(a, ca, sizeof ca);
    conversions(b, cb, sizeof cb);
    return !strcmp(ca, cb);
}

static int hex_digit(char c)
{
    return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
}

// The quoted text at p ("...", C's escapes) written at *out, *out moved after it; 0 when it is not one
static int quoted(const char *p, char **out)
{
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p++ != '"')
        return 0;
    char *o = *out;
    for (; *p && *p != '"'; p++)
    {
        if (*p != '\\')
        {
            *o++ = *p;
            continue;
        }
        p++;
        switch (*p)
        {
        case 'n': *o++ = '\n'; break;
        case 't': *o++ = '\t'; break;
        case 'r': *o++ = '\r'; break;
        case 'a': *o++ = '\a'; break;
        case 'b': *o++ = '\b'; break;
        case 'f': *o++ = '\f'; break;
        case 'v': *o++ = '\v'; break;
        case 'x':
        {
            int v = 0;
            for (int k = 0; k < 2 && isxdigit((unsigned char)p[1]); k++)
                v = v * 16 + hex_digit(*++p);
            *o++ = (char)v;
            break;
        }
        case 0:
            return 0;
        default:
            if (*p >= '0' && *p <= '7')
            {
                int v = *p - '0';
                for (int k = 0; k < 2 && p[1] >= '0' && p[1] <= '7'; k++)
                    v = v * 8 + (*++p - '0');
                *o++ = (char)v;
            }
            else
                *o++ = *p; // \" \\ \' \?
        }
    }
    if (*p != '"')
        return 0;
    *out = o;
    return 1;
}

static void add_entry(Catalog *c, const char *id, const char *str)
{
    if (c->count == c->cap)
    {
        c->cap = c->cap ? c->cap * 2 : 256;
        c->ids = (const char **)realloc((void *)c->ids, c->cap * sizeof(char *));
        c->strs = (const char **)realloc((void *)c->strs, c->cap * sizeof(char *));
    }
    c->ids[c->count] = id;
    c->strs[c->count] = str;
    c->count++;
}

// Where an entry of the file is, as its lines come
struct Reader
{
    Catalog *c;
    const char *file;
    char *o;          // where the next text goes
    char *key, *str;  // the entry's French text (with its context) and its translation
    int open;         // a text being written: its end is still to come
    int skip;         // the text being read is not kept (msgid_plural, msgstr[1]...)
    int fuzzy, line;
};

static void close_text(Reader *r)
{
    if (r->open)
        *r->o++ = 0;
    r->open = 0;
}

// The header's X-Language-Name, the language's name in the lists
static void header(Catalog *c, const char *text)
{
    const char *name = strstr(text, "X-Language-Name:");
    if (!name)
        return;
    name += 16;
    while (*name == ' ')
        name++;
    size_t n = strcspn(name, "\r\n");
    while (n && name[n - 1] == ' ')
        n--;
    snprintf(c->name, sizeof c->name, "%.*s", (int)n, name);
}

static void finish(Reader *r)
{
    close_text(r);
    if (r->key && r->str && !r->key[0])
        header(r->c, r->str);
    else if (r->key && r->str && r->key[0] && r->str[0] && !r->fuzzy)
    {
        const char *id = strchr(r->key, '\x04');
        id = id ? id + 1 : r->key;
        if (same_conversions(id, r->str))
            add_entry(r->c, r->key, r->str);
        else
            say("%s, line %d: the translation of \"%.120s\" has other %%-formats than the French text: left out", r->file, r->line, id);
    }
    r->key = r->str = NULL;
    r->fuzzy = r->skip = 0;
}

// A keyword's text (msgid "...") or a continued line ("...")
static void text(Reader *r, const char *p)
{
    char *at = r->o;
    if (!quoted(p, &r->o))
    {
        r->o = at;
        say("%s, line %d: a quoted text was expected", r->file, r->line);
    }
    else if (r->skip || !r->open)
        r->o = at;
}

static Catalog *parse(const char *code, const char *data, size_t size, const char *file)
{
    Catalog *c = (Catalog *)calloc(1, sizeof *c);
    snprintf(c->code, sizeof c->code, "%s", code);
    c->text = (char *)malloc(size + 2); // the texts unescaped are never longer than the file
    Reader r = {};
    r.c = c;
    r.file = file;
    r.o = c->text;
    if (size >= 3 && !memcmp(data, "\xEF\xBB\xBF", 3)) // UTF-8's mark
        data += 3;
    for (const char *p = data; *p;)
    {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        char line[8192];
        if (n >= sizeof line)
            n = sizeof line - 1;
        memcpy(line, p, n);
        line[n] = 0;
        if (n && line[n - 1] == '\r')
            line[n - 1] = 0;
        p = end ? end + 1 : p + strlen(p);
        r.line++;
        const char *s = line;
        while (*s == ' ' || *s == '\t')
            s++;
        if (!*s)
            continue;
        if (*s == '#')
        {
            if (r.str) // the entry before ends: the comments are the next one's
                finish(&r);
            if (s[1] == ',' && strstr(s, "fuzzy"))
                r.fuzzy = 1;
            continue;
        }
        if (*s == '"')
        {
            text(&r, s);
            continue;
        }
        if (!strncmp(s, "msgctxt", 7) && (s[7] == ' ' || s[7] == '\t'))
        {
            finish(&r);
            r.key = r.o;
            r.open = 1;
            text(&r, s + 7);
        }
        else if (!strncmp(s, "msgid_plural", 12))
        {
            close_text(&r);
            r.skip = 1;
            text(&r, s + 12);
        }
        else if (!strncmp(s, "msgid", 5) && (s[5] == ' ' || s[5] == '\t'))
        {
            if (r.open && r.key && !r.str) // after its context
                *r.o++ = '\x04';
            else
            {
                finish(&r);
                r.key = r.o;
                r.open = 1;
            }
            text(&r, s + 5);
        }
        else if (!strncmp(s, "msgstr", 6))
        {
            const char *t = s + 6;
            int index = 0;
            if (*t == '[')
            {
                index = atoi(t + 1);
                t = strchr(t, ']');
                t = t ? t + 1 : s + 6;
            }
            close_text(&r);
            r.skip = index != 0 || r.str;
            if (!r.skip)
            {
                r.str = r.o;
                r.open = 1;
            }
            text(&r, t);
        }
    }
    finish(&r);
    // The table: twice as many places as entries, at least 16
    unsigned places = 16;
    while (places < (unsigned)c->count * 2)
        places *= 2;
    c->mask = places - 1;
    c->slots = (int *)calloc(places, sizeof(int));
    for (int i = 0; i < c->count; i++)
    {
        unsigned k = hash(c->ids[i]) & c->mask;
        while (c->slots[k])
            k = (k + 1) & c->mask;
        c->slots[k] = i + 1;
    }
    return c;
}

static const char *find(const Catalog *c, const char *text)
{
    if (!c->count)
        return NULL;
    for (unsigned k = hash(text) & c->mask;; k = (k + 1) & c->mask)
    {
        int i = c->slots[k];
        if (!i)
            return NULL;
        if (!strcmp(c->ids[i - 1], text))
            return c->strs[i - 1];
    }
}

static char *read_file(const wchar_t *path, size_t *size)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    LARGE_INTEGER n;
    char *data = NULL;
    DWORD got = 0;
    if (GetFileSizeEx(h, &n) && n.QuadPart < MAX_CATALOGUE && (data = (char *)malloc((size_t)n.QuadPart + 1)) != NULL &&
        ReadFile(h, data, (DWORD)n.QuadPart, &got, NULL))
    {
        data[got] = 0;
        *size = got;
    }
    else
    {
        free(data);
        data = NULL;
    }
    CloseHandle(h);
    return data;
}

// The language's catalogue, read the first time; NULL when its file is missing
static Catalog *catalog(const char *code)
{
    for (Catalog *c = g_catalogs; c; c = c->next)
        if (!strcmp(c->code, code))
            return c;
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s\\lang\\%S.po", g_dir, code);
    size_t size = 0;
    char *data = read_file(path, &size);
    if (!data)
        return NULL;
    char file[32];
    snprintf(file, sizeof file, "lang\\%s.po", code);
    Catalog *c = parse(code, data, size, file);
    free(data);
    c->next = g_catalogs;
    g_catalogs = c;
    say("%s: %d translations", file, c->count);
    return c;
}

// ------------------------------------------------------------------------------
// The language shown
// ------------------------------------------------------------------------------

static void lower(char *s)
{
    for (; *s; s++)
        *s = (char)tolower((unsigned char)*s);
}

static int listed(const char *code)
{
    for (int i = 0; i < g_count; i++)
        if (!strcmp(g_codes[i], code))
            return 1;
    return 0;
}

// The setting's language: its code, or for AUTO Windows' own when it is French or has a catalogue
// (pt_BR's, else pt's, as linuxloader looks for them), else English
static void resolve(const char *setting, char *code, size_t size)
{
    if (_stricmp(setting, "AUTO"))
    {
        snprintf(code, size, "%s", setting);
        lower(code);
        return;
    }
    wchar_t name[LOCALE_NAME_MAX_LENGTH];
    char windows[LOCALE_NAME_MAX_LENGTH] = "en";
    if (LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0))
    {
        WideCharToMultiByte(CP_UTF8, 0, name, -1, windows, sizeof windows, NULL, NULL);
        lower(windows);
        for (char *c = windows; *c; c++) // "pt-BR": pt_br
            if (*c == '-')
                *c = '_';
    }
    if (!listed(windows))
        windows[strcspn(windows, "_")] = 0; // pt
    snprintf(code, size, "%s", listed(windows) ? windows : "en");
}

// A language's name in its own language ("English", "Français"), else its code
static void native_name(const char *code, char *out, size_t size)
{
    wchar_t w[16], name[64];
    MultiByteToWideChar(CP_UTF8, 0, code, -1, w, 16);
    if (GetLocaleInfoEx(w, LOCALE_SNATIVELANGUAGENAME, name, 64) > 1)
    {
        LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, name, 1, name, 1, NULL, NULL, 0);
        WideCharToMultiByte(CP_UTF8, 0, name, -1, out, (int)size, NULL, NULL);
    }
    else
        snprintf(out, size, "%s", code);
}

void wl_lang_init(const wchar_t *dir, const char *setting, void (*log)(const char *message))
{
    wcscpy_s(g_dir, MAX_PATH, dir);
    g_log = log;
    // French, then each lang\<code>.po
    g_count = 0;
    snprintf(g_codes[g_count++], sizeof g_codes[0], "fr");
    wchar_t pattern[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%s\\lang\\*.po", g_dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            size_t n = wcslen(fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY || n < 4 || n > 3 + 15 || _wcsicmp(fd.cFileName + n - 3, L".po") || g_count == MAX_LANGUAGES)
                continue;
            char code[16];
            fd.cFileName[n - 3] = 0;
            WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, code, sizeof code, NULL, NULL);
            lower(code);
            if (!listed(code))
                snprintf(g_codes[g_count++], sizeof g_codes[0], "%s", code);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    // Their names: the catalogue's X-Language-Name, else Windows' name of the language
    for (int i = 0; i < g_count; i++)
    {
        const Catalog *c = i ? catalog(g_codes[i]) : NULL;
        if (c && c->name[0])
            snprintf(g_names[i], sizeof g_names[i], "%s", c->name);
        else
            native_name(g_codes[i], g_names[i], sizeof g_names[i]);
    }
    wl_lang_set(setting);
}

void wl_lang_set(const char *setting)
{
    snprintf(g_setting, sizeof g_setting, "%s", setting && setting[0] ? setting : "AUTO");
    if (!_stricmp(g_setting, "AUTO"))
        snprintf(g_setting, sizeof g_setting, "AUTO");
    else
        lower(g_setting);
    char code[16];
    resolve(g_setting, code, sizeof code);
    Catalog *c = NULL;
    if (strcmp(code, "fr") && (c = catalog(code)) == NULL)
    {
        say("lang\\%s.po missing: the texts stay in French", code);
        snprintf(code, sizeof code, "fr");
    }
    snprintf(g_code, sizeof g_code, "%s", code);
    AcquireSRWLockExclusive(&g_missingLock);
    g_missingCount = 0;
    ReleaseSRWLockExclusive(&g_missingLock);
    InterlockedExchangePointer((PVOID volatile *)&g_shown, c);
    say("language %s (LANGUAGE = %s)", g_code, g_setting);
}

const char *wl_lang_setting(void)
{
    return g_setting;
}

const char *wl_lang_code(void)
{
    return g_code;
}

static void missing(const char *text)
{
    if (!g_log)
        return;
    unsigned h = hash(text);
    int seen = 0;
    AcquireSRWLockExclusive(&g_missingLock);
    for (int i = 0; i < g_missingCount && !seen; i++)
        seen = g_missing[i] == h;
    if (!seen && g_missingCount < MAX_MISSING)
        g_missing[g_missingCount++] = h;
    ReleaseSRWLockExclusive(&g_missingLock);
    if (!seen)
        say("no %s translation for \"%.200s\"", g_code, text);
}

const char *wl_tr(const char *text)
{
    const Catalog *c = g_shown;
    if (!c || !text || !text[0])
        return text;
    const char *t = find(c, text);
    if (t)
        return t;
    missing(text);
    return text;
}

int wl_lang_count(void)
{
    return g_count;
}

const char *wl_lang_code_at(int i)
{
    return i >= 0 && i < g_count ? g_codes[i] : "";
}

const char *wl_lang_name_at(int i)
{
    return i >= 0 && i < g_count ? g_names[i] : "";
}

static const char *name_of(const char *code)
{
    for (int i = 0; i < g_count; i++)
        if (!strcmp(g_codes[i], code))
            return g_names[i];
    return code;
}

int wl_lang_combo(const char *label)
{
    int automatic = !strcmp(g_setting, "AUTO"), changed = 0;
    char shown[96];
    if (automatic)
        snprintf(shown, sizeof shown, wl_tr("Automatique (%s)"), name_of(g_code));
    else
        snprintf(shown, sizeof shown, "%s", name_of(g_code));
    if (ImGui::BeginCombo(label, shown))
    {
        if (ImGui::Selectable(wl_tr("Automatique (langue de Windows)"), automatic) && !automatic)
        {
            wl_lang_set("AUTO");
            changed = 1;
        }
        for (int i = 0; i < g_count; i++)
        {
            int current = !automatic && !strcmp(g_codes[i], g_code);
            ImGui::PushID(i);
            if (ImGui::Selectable(g_names[i], current) && !current)
            {
                wl_lang_set(g_codes[i]);
                changed = 1;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}
