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

// Sega Rally 3 (Sega Europa-R, 2008), Rally\Rally.exe.
//
// Its I/O board is a USB HID device, VID 0x1043 PID 0x0000, found through SetupAPI
// (Rally.exe+0x17b2f0). A thread ("Input HID Thread", +0x17ba30) reads 15-byte input reports
// with ReadFile (+0x17b640):
//   byte 0      report ID
//   byte 1      steering, against [CalibrationData] SteeringLeft..SteeringRight of ShellData.ini
//   byte 3, 4   the pedals, against BrakeUp..BrakeDown and AccelUp..AccelDown
//   bytes 6-8   24 switches, bit 0 of byte 6 first (the game's buttons 18 to 41): bit 0 Start,
//               1 view change, 2 handbrake, 3 gear up, 4 gear down (seen in the game); 5 to 7
//               do nothing in a race, the others nothing on the title screen (not tried in a race)
// Nothing is written back to the board. The serial ports COM1 and COM2 (38400 8E1, +0x22ded0)
// are only opened with the motion base ([gamesettings] MotionBaseOK=1 of ShellData.ini).
//
// The wheel motor is an I/O port: the game loads ..\Shell\inpout32.dll (+0x2582a8) and writes
// one byte to port 0x1020 with Out32 (+0x1c866f): 0 for no force (|f| <= 1/15), else the low
// nibble (int)(15 - |f| * 14), 1 the strongest to 14 the weakest, and bit 4 set when f >= 0.
// windowsloader never loads inpout32 (ioports.cpp): the writes come here.

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "display.h"
#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "ioports.h"
#include "lang.h"
#include "link.h"
#include "log.h"
#include "menu.h"
#include "outputs.h"
#include "vhid.h"

enum
{
    IN_STEERING,
    IN_ACCEL,
    IN_BRAKE,
    IN_BIT0,
    IN_COUNT = IN_BIT0 + 24
};

static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 10, 245, 128},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 40, 210, 40},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 40, 210, 40},
};

// The switches whose role is known, by bit; the others keep the name BitN and no binding
static const struct
{
    int bit;
    const char *name, *action;
} g_switches[] = {
    {0, "Start", "P1_Start"},
    {1, "ViewChange", "ViewChange"},
    {2, "Handbrake", "Handbrake"},
    {3, "GearUp", "GearUp"},
    {4, "GearDown", "GearDown"},
};
static char g_bitNames[24][8];

static void fill_input(unsigned char *r, int length)
{
    r[0] = 0;
    r[1] = (unsigned char)wl_input_get(&g_inputs[IN_STEERING]);
    r[3] = (unsigned char)wl_input_get(&g_inputs[IN_BRAKE]);
    r[4] = (unsigned char)wl_input_get(&g_inputs[IN_ACCEL]);
    unsigned bits = 0;
    for (int i = 0; i < 24; i++)
        if (wl_input_get(&g_inputs[IN_BIT0 + i]))
            bits |= 1u << i;
    r[6] = (unsigned char)bits;
    r[7] = (unsigned char)(bits >> 8);
    r[8] = (unsigned char)(bits >> 16);
}

static const WlVirtualHid g_board = {
    "\\\\?\\hid#vid_1043&pid_0000#7&57a4c0de&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}",
    0x1043,
    0x0000,
    0x0100,
    0x0001, // generic desktop
    0x0004, // joystick
    15,
    4,
    fill_input,
};

// Port 0x1020 back to the force -1..1, > 0 pushing left. Bit 4 pushes the wheel to the right:
// measured with scripts/agent/sr3_ffb.py, the wheel held right gets bit 4 clear, the centring
// force pushing it back to the left.
#define FFB_PORT 0x1020
#define FFB_SIGN -1.0

static void port_write(unsigned short port, unsigned short value)
{
    if (port != FFB_PORT)
        return;
    int nibble = value & 0x0F;
    double f = nibble ? (15 - nibble) / 14.0 : 0;
    if (!(value & 0x10))
        f = -f;
    wl_ffb_set(FFB_SIGN * f);
}

// The render resolution of windowsloader.ini ([Display] WIDTH / HEIGHT) given to the game when
// it reads its own: [Graphics] HorizontalResolution / VerticalResolution of Rally\config.ini.
// Linked, the cabinet's number: [Network] CabinetID of ShellData.ini ("For Link play: must be on
// same LAN and change CabinetID to 1..4"), Enabled
static int ini_override(const char *file, const char *section, const char *key, char *value, size_t size)
{
    size_t n = strlen(file);
    if (wl_link_count() && n >= 13 && !_stricmp(file + n - 13, "ShellData.ini") && !_stricmp(section, "Network"))
    {
        if (!_stricmp(key, "CabinetID"))
            snprintf(value, size, "%d", wl_link_cabinet());
        else if (!_stricmp(key, "Enabled"))
            snprintf(value, size, "1");
        else
            return 0;
        return 1;
    }
    int w, h;
    wl_display_internal(&w, &h);
    if (!w || !h || n < 10 || _stricmp(file + n - 10, "config.ini") || _stricmp(section, "Graphics"))
        return 0;
    if (!_stricmp(key, "HorizontalResolution"))
        snprintf(value, size, "%d", w);
    else if (!_stricmp(key, "VerticalResolution"))
        snprintf(value, size, "%d", h);
    else
        return 0;
    return 1;
}

// The analogue ranges follow the cabinet's calibration, so the full travel of a controller
// covers what the game expects
static void read_calibration(const wchar_t *gameDir)
{
    wchar_t ini[MAX_PATH];
    swprintf(ini, MAX_PATH, L"%s..\\ShellData\\ShellData.ini", gameDir);
    struct
    {
        int input;
        const wchar_t *low, *high;
    } cal[] = {
        {IN_STEERING, L"SteeringLeft", L"SteeringRight"},
        {IN_ACCEL, L"AccelUp", L"AccelDown"},
        {IN_BRAKE, L"BrakeUp", L"BrakeDown"},
    };
    for (int i = 0; i < 3; i++)
    {
        WlInput *in = &g_inputs[cal[i].input];
        in->rawLow = GetPrivateProfileIntW(L"CalibrationData", cal[i].low, in->rawLow, ini);
        in->rawHigh = GetPrivateProfileIntW(L"CalibrationData", cal[i].high, in->rawHigh, ini);
        in->rawRest = in->type == WL_INPUT_AXIS ? (in->rawLow + in->rawHigh + 1) / 2 : in->rawLow;
        wl_log("  %s: %d..%d, rest %d", in->name, in->rawLow, in->rawHigh, in->rawRest);
    }
}

// ------------------------------------------------------------------------------
// Sega Rally Revo's tracks ([Game.segarally3] REVO_DIR)
// ------------------------------------------------------------------------------
// Sega Rally 3 is Revo's engine: a track is a folder of Main_release\tracks, its files named from
// the track's base name (\main_release\tracks\<base>\<base>_master_gfx_xdata, <first 3 letters>
// _track_route<last character>_..., +0x21b3b0), so Revo's folders load as they are. Its tracks are
// the nodes of ArcadeDatabase\arcadedatabase_xdata.sbf, loaded whole into one block (its root at
// 0xb2a850: +8 the tracks' list, +0xc the time limits): a node is next, id, the cars' texture pack
// (0 dust, 1 mud, 2 snow), hidden, 50, -> its name's text id, -> base name, -> its audio events
// (Audio\<id>_Events.bin); the time limits are 9 groups of 0x160 bytes per node (3 modes x 3
// difficulties) in the list's order, read by position (+0x261aa0): a track past them gets 0 s.
// Quick race's track select (ID_PAGE_TRACKSELECT) lists the nodes not hidden (+0x200660, the
// selector in edi), and its choice is the selector's position, given as the track's id (+0x266a1d).
// The menu is a state machine (handlers at 0x7246c8): state 9 opens the page, state 10 waits for
// the choice (15 s at most) and returns 11. A vote among the cabinets then keeps Tropical4, Canyon4
// or Alpine4 (+0x24ed00), Tropical4 by default.
// With REVO_DIR: Revo's tracks found there are added as hidden nodes (their folders redirected
// there, their time limits those of the Sega Rally 3 track of their environment); the page lists
// the environments first, then, the page opened again, that environment's tracks; the position
// chosen is turned into the track's id, and the vote counts every track (link_vote). Linked: each
// cabinet sends its player's choice by its id (a Revo track's from its place in the list of the 24,
// the same on every cabinet with the same tracks), every cabinet votes on the same data, the host
// gives the race's track by its name (+0x1b9190); the network id made from the Revo tracks found
// keeps a cabinet from linking with one of other tracks or without Revo. Revo's championships stay
// off in a link.
#if defined(_M_IX86)
enum
{
    ENV_TROPICAL,
    ENV_CANYON,
    ENV_ALPINE,
    ENV_LAKESIDE,
    ENV_SAFARI,
    ENV_ARCTIC,
    ENV_COUNT
};

static const struct
{
    const char *lang;   // its name on the page (Sega Rally 3's texts)
    int sr3;            // the Sega Rally 3 track standing for it: its card, its time limits
    unsigned surface;   // the cars' texture pack
    const char *events; // its audio events
    int own;            // Sega Rally 3's own track of that environment on the list (-1 none)
} g_envs[ENV_COUNT] = {
    {"LANG_ID_TROPICAL", 0, 1, "ID_TRACK_TROPICAL_1", 0},
    {"LANG_ID_CANYON", 1, 0, "ID_TRACK_CANYON_1", 1},
    {"LANG_ID_ALPINE", 2, 2, "ID_TRACK_ALPINE_1", 2},
    {"LANG_ID_LAKESIDE", 3, 1, "ID_TRACK_LAKESIDE_1", -1},
    {"LANG_ID_DESERT", 4, 0, "ID_TRACK_DESERT_1", -1}, // Safari: Sega Rally 3 has none, Desert4 the nearest
    {"LANG_ID_ALPINE", 2, 2, "ID_TRACK_ALPINE_1", -1}, // Arctic: Alpine4's snow
};

static const struct
{
    const char *base;
    int env;
} g_revoTracks[] = {
    {"tropical1", ENV_TROPICAL}, {"tropical2", ENV_TROPICAL}, {"tropical3", ENV_TROPICAL}, {"tropical7", ENV_TROPICAL},
    {"tropical8", ENV_TROPICAL}, {"canyon1", ENV_CANYON},     {"canyon2", ENV_CANYON},     {"canyon3", ENV_CANYON},
    {"Canyon7", ENV_CANYON},     {"alpine1", ENV_ALPINE},     {"alpine2", ENV_ALPINE},     {"alpine3", ENV_ALPINE},
    {"alpine6", ENV_ALPINE},     {"alpine7", ENV_ALPINE},     {"lakeside1", ENV_LAKESIDE}, {"safari1", ENV_SAFARI},
    {"safari2", ENV_SAFARI},     {"safari3", ENV_SAFARI},     {"Safari6", ENV_SAFARI},     {"arctic1", ENV_ARCTIC},
    {"arctic2", ENV_ARCTIC},     {"arctic3", ENV_ARCTIC},     {"arctic6", ENV_ARCTIC},     {"Arctic7", ENV_ARCTIC},
};
#define REVO_MAX (int)(sizeof g_revoTracks / sizeof g_revoTracks[0])

static unsigned char *g_image; // Rally.exe as loaded; the addresses below are its own (base 0x400000)
#define VA(a) ((void *)(g_image + ((a) - 0x400000)))

static struct
{
    const char *base;
    int env;
    int slot;      // its place in g_revoTracks: its id is the database's last + 1 + slot
    int id;
    char text[24]; // its name's text id: WL_REVO_<base>
} g_revo[REVO_MAX];
static int g_revoCount;
static unsigned g_revoMask; // the Revo tracks found, bit n for g_revoTracks[n]
static unsigned g_firstRevoId = 0xFFFFFFFF; // the vote's: an id from there on is a Revo track
static void *g_curTrackAddr, *g_dbAddr;     // 0x9dbdb0 (the track chosen), 0xb2a850 (the database)
static volatile LONG g_dbDone;

// The page's entries: their name, the id whose card they show, the environment or track they are
static struct
{
    const char *lang; // its name's text id (the game's, or one of ours, WL_REVO_...)
    int card, value;
    const char *help; // the game's text id whose help it shows
} g_items[REVO_MAX + ENV_COUNT];
static int g_itemCount;
static volatile LONG g_pass; // 0: the environments, 1: an environment's tracks, 2: the championships
static int g_env;
static volatile LONG g_menuActive; // the page's list is ours
static volatile LONG g_champPage; // the championships' list shown (state 3 to our page, then 4)

static void revo_champs(void);

// The Revo tracks added to the database once, at the first listing (it is loaded at the start)
static int revo_database(void)
{
    if (g_dbDone)
        return g_dbDone > 0;
    unsigned char *db = *(unsigned char **)g_dbAddr;
    if (!db)
        return 0;
    unsigned *node = *(unsigned **)(db + 8) + 2, *last = node;
    int count = 0;
    unsigned maxId = 0;
    for (; node; node = (unsigned *)node[0])
    {
        count++;
        if (node[1] > maxId)
            maxId = node[1];
        last = node;
    }
    const size_t group = 0x160 * 9;
    unsigned char *times = *(unsigned char **)(db + 0xc);
    unsigned char *more = (unsigned char *)VirtualAlloc(NULL, (count + g_revoCount) * group + 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!more || count < 5)
    {
        InterlockedExchange(&g_dbDone, -1);
        wl_log("segarally3: Revo: the database not as expected (%d tracks), Revo's tracks left out", count);
        return 0;
    }
    // A group is read up to 4 bytes past its end, the next one's first dword (never read for itself)
    memcpy(more, times, count * group + 4);
    unsigned fifty = (*(unsigned **)(db + 8) + 2)[4];
    for (int i = 0; i < g_revoCount; i++)
    {
        const int e = g_revo[i].env, model = g_envs[e].sr3;
        memcpy(more + (count + i) * group + 4, times + model * group + 4, group);
        unsigned *n = (unsigned *)VirtualAlloc(NULL, 0x40, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!n)
            break;
        // Linked, the cabinets send each other their player's track by its id (+0x1afd80): an id of
        // the list's place, the same on every cabinet with the same Revo tracks (the network id,
        // revo_setup, keeps the others apart), whichever folders another has
        g_revo[i].id = (int)(maxId + 1 + g_revo[i].slot);
        n[0] = 0;
        n[1] = (unsigned)g_revo[i].id;
        n[2] = g_envs[e].surface;
        n[3] = 1; // hidden: the game's own lists leave it out
        n[4] = fifty;
        n[5] = (unsigned)g_envs[e].lang; // the environment's: a text id of ours here stops the race's loading (read as a hash)
        n[6] = (unsigned)g_revo[i].base;
        n[7] = (unsigned)g_envs[e].events;
        strncpy_s((char *)(n + 8), 20, g_envs[e].lang, _TRUNCATE);
        last[0] = (unsigned)n;
        last = n;
    }
    g_firstRevoId = maxId + 1;
    *(unsigned char **)(db + 0xc) = more;
    revo_champs();
    InterlockedExchange(&g_dbDone, 1);
    wl_log("segarally3: Revo: %d tracks added to the database (ids %u to %u by their place in the list of %d, after %d)", g_revoCount,
           maxId + 1 + g_revo[0].slot, maxId + 1 + g_revo[g_revoCount - 1].slot, REVO_MAX, count);
    return 1;
}

// The names: Revo's own are in its language files (encoded, not read); the environment's name in the
// game's language (its text id's) and the route's number instead (Alpine 6 for alpine6), Arctic and
// Safari in English (the game has no text for them). Our text ids (WL_REVO_...) are answered by the
// language's text lookup (its method 4, +0x1bb2d0: the text id -> its text, NULL when unknown)
static const char *const g_envTextIds[ENV_COUNT] = {NULL, NULL, NULL, NULL, "WL_REVO_ENV_SAFARI", "WL_REVO_ENV_ARCTIC"};
static const wchar_t *const g_envEnglish[ENV_COUNT] = {L"Tropical", L"Canyon", L"Alpine", L"Lakeside", L"Safari", L"Arctic"};
static wchar_t g_trackNames[REVO_MAX][40];
typedef const wchar_t *(__fastcall *TextFn)(void *language, void *, const char *id);
static TextFn g_textOriginal;

// Its method 5 (+0x1b9570: a text by its id's hash, from method 3, +0x1bb2a0; NULL for a hash it does
// not know) is the one the menu and the race's loading use: our ids' hashes answered there too (an
// unknown one leaves the race's loading waiting)
static const wchar_t *__fastcall revo_text(void *language, void *edx, const char *id);
typedef unsigned(__fastcall *HashFn)(void *language, void *, const char *id);
typedef const wchar_t *(__fastcall *ByHashFn)(void *language, void *, unsigned hash);
static ByHashFn g_byHashOriginal;
static unsigned g_envHashes[ENV_COUNT], g_trackHashes[REVO_MAX];
static int g_hashesDone;

// Revo's championships (chosen after the gearbox, on the track select's page): the game's own (its 4
// stages), then per environment with 4 Revo tracks or more, A its first 4, B its last 4 (5 tracks);
// 4 stages each, as the game's (its 21 opponents are read at fixed places after the 4 stages' ids in
// the database's championship, 0x43593ffb: count, then the ids), the last its final
#define CHAMP_MAX 12
static struct
{
    char text[32]; // its name's text id: WL_REVO_CHAMP_...
    int env;       // -1 for the game's own
    char part;     // 'A', 'B', or 0 for an environment's only one
    int stages[4];
} g_champs[CHAMP_MAX];
static int g_champCount;
static int g_champsOn; // the championships' page installed (revo_setup)
static wchar_t g_champNames[CHAMP_MAX][40];
static unsigned g_champHashes[CHAMP_MAX];
static int g_champHashesDone;

static const wchar_t *__fastcall revo_text_by_hash(void *language, void *edx, unsigned hash)
{
    const wchar_t *text = g_byHashOriginal(language, edx, hash);
    if (text)
        return text;
    HashFn hashOf = (HashFn)(*(void ***)language)[3];
    if (!g_hashesDone)
    {
        for (int e = 0; e < ENV_COUNT; e++)
            g_envHashes[e] = g_envTextIds[e] ? hashOf(language, NULL, g_envTextIds[e]) : 0;
        for (int i = 0; i < g_revoCount; i++)
            g_trackHashes[i] = hashOf(language, NULL, g_revo[i].text);
        g_hashesDone = 1;
    }
    // The championships' hashes once they are made (revo_database, at the first listing): a lookup
    // before then leaves them to a later one
    if (!g_champHashesDone && g_dbDone > 0)
    {
        for (int k = 0; k < g_champCount; k++)
            g_champHashes[k] = hashOf(language, NULL, g_champs[k].text);
        g_champHashesDone = 1;
    }
    for (int e = 0; e < ENV_COUNT; e++)
        if (g_envTextIds[e] && hash == g_envHashes[e])
            return revo_text(language, edx, g_envTextIds[e]);
    for (int i = 0; i < g_revoCount; i++)
        if (hash == g_trackHashes[i])
            return revo_text(language, edx, g_revo[i].text);
    for (int k = 0; g_champHashesDone && k < g_champCount; k++)
        if (hash == g_champHashes[k])
            return revo_text(language, edx, g_champs[k].text);
    return NULL;
}

static const char *env_text(int e)
{
    return g_envTextIds[e] ? g_envTextIds[e] : g_envs[e].lang;
}

static const wchar_t *__fastcall revo_text(void *language, void *edx, const char *id)
{
    if (!id || strncmp(id, "WL_REVO_", 8))
        return g_textOriginal(language, edx, id);
    for (int e = 0; e < ENV_COUNT; e++)
        if (g_envTextIds[e] && !strcmp(id, g_envTextIds[e]))
            return g_envEnglish[e];
    for (int i = 0; i < g_revoCount; i++)
        if (!strcmp(id, g_revo[i].text))
        {
            if (!g_trackNames[i][0])
            {
                const int e = g_revo[i].env;
                const wchar_t *env = g_envTextIds[e] ? NULL : g_textOriginal(language, edx, g_envs[e].lang);
                size_t n = strlen(g_revo[i].base);
                swprintf(g_trackNames[i], 40, L"%s %c", env ? env : g_envEnglish[e], g_revo[i].base[n - 1]);
            }
            return g_trackNames[i];
        }
    for (int k = 0; k < g_champCount; k++)
        if (!strcmp(id, g_champs[k].text))
        {
            if (!g_champNames[k][0])
            {
                const int e = g_champs[k].env;
                const wchar_t *env = e < 0 || g_envTextIds[e] ? NULL : g_textOriginal(language, edx, g_envs[e].lang);
                if (e < 0)
                    swprintf(g_champNames[k], 40, L"Sega Rally 3");
                else if (g_champs[k].part)
                    swprintf(g_champNames[k], 40, L"%s %c", env ? env : g_envEnglish[e], g_champs[k].part);
                else
                    swprintf(g_champNames[k], 40, L"%s", env ? env : g_envEnglish[e]);
            }
            return g_champNames[k];
        }
    return g_textOriginal(language, edx, id);
}

// One card per node: two entries of the same node stop the menu when it makes its cards (seen with
// Alpine4 for Alpine and Arctic): an environment of Sega Rally 3's own shows its track's card, the
// others the card of their first Revo track (none drawn) or of the Sega Rally 3 track standing for them
static void revo_env_items(void)
{
    g_itemCount = 0;
    int used[64] = {0};
    for (int e = 0; e < ENV_COUNT; e++)
    {
        int first = -1;
        for (int i = 0; i < g_revoCount && first < 0; i++)
            if (g_revo[i].env == e)
                first = g_revo[i].id;
        if (g_envs[e].own < 0 && first < 0)
            continue;
        int card = g_envs[e].own >= 0 ? g_envs[e].own : first;
        if (card < 0 || card >= 64 || used[card])
            card = first;
        if (card >= 0 && card < 64)
            used[card] = 1;
        g_items[g_itemCount++] = {env_text(e), card, e, g_envs[e].lang};
    }
}

static void revo_track_items(int e)
{
    g_itemCount = 0;
    if (g_envs[e].own >= 0)
        g_items[g_itemCount++] = {g_envs[e].lang, g_envs[e].own, g_envs[e].own, g_envs[e].lang};
    for (int i = 0; i < g_revoCount; i++)
        if (g_revo[i].env == e)
            g_items[g_itemCount++] = {g_revo[i].text, g_revo[i].id, g_revo[i].id, g_envs[e].lang};
}

// The championships, once the Revo tracks have their ids (revo_database)
static void revo_champs(void)
{
    g_champCount = 0;
    snprintf(g_champs[0].text, sizeof g_champs[0].text, "WL_REVO_CHAMP_SR3");
    g_champs[0].env = -1;
    g_champs[0].part = 0;
    g_champCount = 1;
    static const char *names[ENV_COUNT] = {"TROPICAL", "CANYON", "ALPINE", "LAKESIDE", "SAFARI", "ARCTIC"};
    for (int e = 0; e < ENV_COUNT && g_champCount + 2 <= CHAMP_MAX; e++)
    {
        int ids[REVO_MAX], n = 0;
        for (int i = 0; i < g_revoCount; i++)
            if (g_revo[i].env == e)
                ids[n++] = g_revo[i].id;
        if (n < 4)
            continue;
        for (int part = 0; part < (n > 4 ? 2 : 1); part++)
        {
            int k = g_champCount++;
            g_champs[k].env = e;
            g_champs[k].part = n > 4 ? (char)('A' + part) : 0;
            snprintf(g_champs[k].text, sizeof g_champs[k].text, "WL_REVO_CHAMP_%s%c", names[e], n > 4 ? 'A' + part : '_');
            for (int st = 0; st < 4; st++)
                g_champs[k].stages[st] = ids[part ? n - 4 + st : st];
        }
    }
}

// The championships' list: card the first stage's (Sega Rally 3's own: Tropical4's)
static void revo_champ_items(void)
{
    g_itemCount = 0;
    for (int k = 0; k < g_champCount; k++)
    {
        int e = g_champs[k].env;
        g_items[g_itemCount++] = {g_champs[k].text, e < 0 ? 0 : g_champs[k].stages[0], k, e < 0 ? g_envs[0].lang : g_envs[e].lang};
    }
}

// "<text id>_HELP" in the game's language, as +0x200660 asks it
static const void *lang_help(const char *lang)
{
    char text[64];
    snprintf(text, sizeof text, "%s_HELP", lang);
    void **holder = ((void **(__cdecl *)(void))VA(0x546910))();
    void *obj = holder ? *holder : NULL;
    if (!obj)
        return NULL;
    return ((const void *(__fastcall *)(void *, void *, const char *))(*(void ***)obj)[3])(obj, NULL, text);
}

// The cards: their pictures are textures named TRACKSLIDE_<base name in capitals> (+0x228f60);
// Sega Rally 3's tracks' in arcade pack1 resources, Revo's (all 24) in frontend track cards, the
// same file as Revo's, which the game never loads. The game's resource manager holds 16 libraries
// (+0x14b310 refuses more): that pack loaded with the menu's (+0x247189) left the championship's race
// loading forever. So it is loaded only while Quick Race's track select is shown (+0x245840, the
// menu's packs' loader, with a flag of its own, 7) and released when the choice is made (+0x231c20
// releases a flag's packs), the cards' pictures taken off it first. The cards are made once, with
// the menu (+0x22d1c0, one per entry, +0x22aa30 giving each its picture); ours noted there, in
// order, and given the pictures of the list shown when it changes (+0x2271c0)
#define CARDS_FLAG 7
static void *g_cardOriginal;
static const char g_cardsPack[] = "\\frontend\\frontend track cards_data";
static void *g_cards[REVO_MAX + ENV_COUNT];
static int g_cardCount, g_packLoaded;

static void cards_pack_load(void)
{
    if (g_packLoaded)
        return;
    void *loader = VA(0x645840), *manager = VA(0xade800);
    unsigned a = *(unsigned *)VA(0x7ece5c), b = *(unsigned *)VA(0x7ece3c);
    const char *pack = g_cardsPack;
    __asm {
        push esi
        push CARDS_FLAG
        push pack
        mov ecx, a
        mov edx, b
        mov esi, manager
        call loader
        pop esi
    }
    g_packLoaded = 1;
}

static void cards_pack_release(void)
{
    if (!g_packLoaded)
        return;
    for (int i = 0; i < g_cardCount; i++) // no picture from the pack left on a card
    {
        void *sub = *(void **)((char *)g_cards[i] + 0xd4);
        if (sub)
            ((void(__cdecl *)(void *, const char *))VA(0x6271c0))(sub, "TRACKSLIDE_NONE");
    }
    void *release = VA(0x631c20), *manager = VA(0xade800);
    __asm {
        push edi
        mov edi, manager
        push CARDS_FLAG
        call release
        pop edi
    }
    g_packLoaded = 0;
}

static void __cdecl card_made(void *card, unsigned *node)
{
    ((void(__cdecl *)(void *, unsigned *))g_cardOriginal)(card, node);
    if (g_cardCount < (int)(sizeof g_cards / sizeof g_cards[0]))
        g_cards[g_cardCount++] = card;
}

// The list shown's pictures on the cards
static void cards_pictures(void)
{
    unsigned char *db = *(unsigned char **)g_dbAddr;
    if (!db)
        return;
    for (int i = 0; i < g_cardCount && i < g_itemCount; i++)
    {
        unsigned *node = *(unsigned **)(db + 8) + 2;
        while (node && (int)node[1] != g_items[i].card)
            node = (unsigned *)node[0];
        void *sub = *(void **)((char *)g_cards[i] + 0xd4);
        if (!node || !sub)
            continue;
        char name[64];
        snprintf(name, sizeof name, "TRACKSLIDE_%s", (const char *)node[6]);
        _strupr_s(name, sizeof name);
        ((void(__cdecl *)(void *, const char *))VA(0x6271c0))(sub, name);
    }
}

static void *g_fillOriginal;
static void *g_selector; // the page's selector: its position at +0x94 (as +0x206490 reads it)

static void call_fill(void *selector)
{
    void *f = g_fillOriginal;
    __asm {
        push edi
        mov edi, selector
        call f
        pop edi
    }
}

static void __cdecl fill_list(void *selector, void *caller)
{
    // The menu's cards are made once, when the menu is (+0x22d710), one per entry of this list: the
    // environments' then, at least as many as an environment's tracks
    // A selection left half way (linked: back to the lobby from a track select, the other cabinet
    // gone) left an environment's tracks listed, 1 for Lakeside: the menu made again then had 1 card,
    // and the next environments' page (6) went past it (+0x228ea9). The menu's new
    // cards always from the longest lists, the cards pack of the last page released (its cards gone)
    int making = caller == VA(0x62d22a);
    if (making)
    {
        g_cardCount = 0; // the menu's cards about to be made
        cards_pack_release();
        InterlockedExchange(&g_champPage, 0);
    }
    if (g_revoCount && (!g_itemCount || making) && revo_database())
    {
        // the longest of the lists shown on that page: as many cards
        InterlockedExchange(&g_pass, 0);
        revo_env_items();
        if (g_champsOn && g_champCount > g_itemCount)
            revo_champ_items();
    }
    if (!g_revoCount || !revo_database() || !g_itemCount)
    {
        InterlockedExchange(&g_menuActive, 0);
        call_fill(selector);
        return;
    }
    g_selector = selector;
    void **vt = *(void ***)selector;
    ((void(__fastcall *)(void *, void *))vt[0x58 / 4])(selector, NULL); // emptied
    for (int i = 0; i < g_itemCount; i++)
        ((void(__fastcall *)(void *, void *, const char *, int, int, int, const void *))vt[0x54 / 4])(selector, NULL, g_items[i].lang, g_items[i].card,
                                                                                                    -1, 0, lang_help(g_items[i].help));
    InterlockedExchange(&g_menuActive, 1);
    char where[64];
    wl_log("segarally3: Revo: %s listed (%d), selector %p, for %s", g_pass == 2 ? "championships" : g_pass ? "tracks" : "environments", g_itemCount, selector,
           wl_where(caller, where, sizeof where));
}

__declspec(naked) static void fill_hook(void)
{
    __asm {
        push dword ptr [esp]
        push edi
        call fill_list
        add esp, 8
        ret
    }
}

// The track select's choice (+0x266a1d, the selector's position in ecx) to the track's id: only
// the tracks' list gets there (the environments' is handled in state 10, the game's choice skipped)
static int __fastcall track_choice(int position, void *, void *dst)
{
    int id = position;
    if (g_menuActive && g_pass && position >= 0 && position < g_itemCount)
        id = g_items[position].value;
    return ((int(__fastcall *)(int, void *, void *))VA(0x5af700))(id, NULL, dst);
}

// Milliseconds as the menu's states count them (+0x24827a)
static unsigned __int64 menu_now(void)
{
    ((void(__cdecl *)(void))VA(0x58cdf0))();
    return *(unsigned __int64 *)VA(0x9c4a08) / 1000;
}

// The track select's page opened, as state 9 opens it (+0x248473): for the championships' list
static void open_track_page(void)
{
    int page = -1;
    if (*(int *)VA(0x9c0f48))
    {
        const char *name = (const char *)VA(0x6da070); // "ID_PAGE_TRACKSELECT"
        int chars = (int)strlen(name);
        void *f = VA(0x5470d0);
        __asm {
            push chars
            mov ecx, name
            call f
            add esp, 4
            mov page, eax
        }
    }
    EnterCriticalSection((LPCRITICAL_SECTION)VA(0xa3b2a0));
    int current = *(int *)VA(0x70ac0c);
    void *shown = NULL;
    if (current >= 0 && current < *(int *)VA(0xa3b658))
    {
        void *a = *(void **)((char *)VA(0xa3b2fc) + current * 0x74), *b = *(void **)((char *)VA(0xa3b300) + current * 0x74);
        shown = a ? a : b;
    }
    ((void(__cdecl *)(int))VA(0x54c0c0))(page);
    if (shown)
    {
        void *object = *(void **)((char *)shown + 4), *f = VA(0x5491a0);
        __asm {
            push edi
            mov edi, object
            call f
            pop edi
        }
    }
    LeaveCriticalSection((LPCRITICAL_SECTION)VA(0xa3b2a0));
}

// A championship's 4 stages written over the database's (the game's own kept aside first), the
// player's track its first stage's
static int g_stagesGame[4], g_stagesKept;

static void champ_apply(int k)
{
    unsigned char *db = *(unsigned char **)g_dbAddr;
    int *champ = db ? *(int **)(db + 0x10) : NULL;
    if (!champ || champ[1] != 4)
        return;
    if (!g_stagesKept)
    {
        memcpy(g_stagesGame, champ + 2, sizeof g_stagesGame);
        g_stagesKept = 1;
    }
    int revo = k > 0 && k < g_champCount;
    memcpy(champ + 2, revo ? g_champs[k].stages : g_stagesGame, sizeof g_stagesGame);
    ((int(__fastcall *)(int, void *, void *))VA(0x5af700))(champ[2], NULL, VA(0x9dbda0));
}

// The championship's race starts the 5 cars of its rolling grid (its 22 places) from the track's
// own start points (+0x1d33f0: the track's points of kind 4, [[+0x9db48c]+0x18], their place on
// the route in segments): the 5 ahead of the player (ahead, sorted up) and the 5 behind (behind,
// sorted down), each list completed 150 segments apart when the track has fewer than 5. A Revo
// track has none, and that completion starts at the count less one, unsigned: from 0 nothing is
// written, the 10 places left as the stack had them (the cars' laps counted from them, +0x1a9f00,
// then read past their tables, +0x1ac4a5). Ours: the lists the game left empty filled the same
// way, 150, 300... segments ahead and behind (a track with points of its own unchanged: the
// game writes all 5 places then)
typedef void(__stdcall *StartsFn)(int *ahead, int *behind);
static StartsFn g_startsOriginal;

static void __stdcall champ_starts(int *ahead, int *behind)
{
    const int unset = (int)0x80000000;
    for (int i = 0; i < 5; i++)
        ahead[i] = behind[i] = unset;
    g_startsOriginal(ahead, behind);
    int filled = 0;
    for (int i = 0; i < 5; i++)
    {
        if (ahead[i] == unset)
            ahead[i] = (i ? ahead[i - 1] : 0) + 150, filled = 1;
        if (behind[i] == unset)
            behind[i] = (i ? behind[i - 1] : 0) - 150, filled = 1;
    }
    static int logged;
    if (filled && logged++ < 4)
        wl_log("segarally3: Revo: the championship's grid placed 150 segments apart (the track has no start points of its own)");
}

typedef int(__cdecl *StateFn)(int *machine);
static StateFn g_state3, g_state9, g_state10;

// State 3 waits for the championship's gearbox, then (4) the race: our page first, the championships'
// list, when there are Revo ones (else the game's stages back)
static int __cdecl state3(int *machine)
{
    int next = g_state3(machine);
    if (next != 4 || !g_revoCount)
        return next;
    if (wl_link_count() || !revo_database() || g_champCount < 2)
    {
        if (g_stagesKept)
            champ_apply(0);
        return next;
    }
    InterlockedExchange(&g_champPage, 1);
    InterlockedExchange(&g_pass, 2);
    revo_champ_items();
    cards_pack_load();
    open_track_page();
    wl_log("segarally3: Revo: the championships' list (%d)", g_champCount);
    return 10;
}

static int __cdecl state9(int *machine)
{
    if (g_pass || !machine[2])
    {
        InterlockedExchange(&g_pass, 0);
        revo_env_items();
        if (g_revoCount)
        {
            cards_pack_load();
            cards_pictures();
        }
    }
    return g_state9(machine);
}

// State 10 waits for the page's choice (+0x9c4338 set by its callback) or its 15 s (the time it
// started at +0xae31c8), then makes the choice (+0x2669c0). The environments' list: its choice (or
// the time over) handled here, the game's choice skipped: the page's own start again (its callback
// +0x266be0 with 0x17: the list made again, here the environment's tracks, its cards, its 15 s, the
// selector enabled), the state kept
static int __cdecl state10(int *machine)
{
    if (g_champPage)
    {
        // Its 15 s from its first frame, the cards' pictures once the page's start made the list
        if (!machine[2])
        {
            *(unsigned __int64 *)VA(0xae31c8) = menu_now();
            return 10;
        }
        if (machine[2] == 3 || machine[2] == 20)
            cards_pictures();
        int chosen = *(int *)VA(0x9c4338) != 0, late = menu_now() - *(unsigned __int64 *)VA(0xae31c8) >= 15000;
        if (!chosen && !late)
            return 10;
        int position = g_selector ? *(int *)((char *)g_selector + 0x94) : 0;
        int k = position >= 0 && position < g_itemCount ? g_items[position].value : 0;
        champ_apply(k);
        const int *stages = k ? g_champs[k].stages : g_stagesGame;
        wl_log("segarally3: Revo: championship %s chosen (%s), stages %d %d %d %d", g_champs[k].text, chosen ? "confirmed" : "time over",
               stages[0], stages[1], stages[2], stages[3]);
        cards_pack_release();
        InterlockedExchange(&g_champPage, 0);
        InterlockedExchange(&g_pass, 0);
        return 4;
    }
    if (g_menuActive && !g_pass && machine[2] && g_selector)
    {
        unsigned __int64 started = *(unsigned __int64 *)VA(0xae31c8);
        int chosen = *(int *)VA(0x9c4338) != 0, late = menu_now() - started >= 15000;
        if (chosen || late)
        {
            int position = *(int *)((char *)g_selector + 0x94);
            g_env = position >= 0 && position < g_itemCount ? g_items[position].value : g_items[0].value;
            InterlockedExchange(&g_pass, 1);
            revo_track_items(g_env);
            static const char *names[ENV_COUNT] = {"tropical", "canyon", "alpine", "lakeside", "safari", "arctic"};
            wl_log("segarally3: Revo: environment %s chosen (%s), %d tracks", names[g_env], chosen ? "confirmed" : "time over", g_itemCount);
            ((int(__cdecl *)(int, int, int, int))VA(0x666be0))(0, 0x17, 0, 0);
            cards_pictures();
            // Its own 15 s; linked, what is left of the environments' (the other cabinets wait for
            // this one's choice, +0x24f030), 5 s at least
            unsigned __int64 now = menu_now();
            *(unsigned __int64 *)VA(0xae31c8) = !wl_link_count() ? now : now - started < 10000 ? started : now - 10000;
            return 10;
        }
    }
    int next = g_state10(machine);
    if (next != 10)
        cards_pack_release(); // the track chosen: the page left
    return next;
}

// The vote (+0x24ed00), linked or not, every cabinet on the same data: the players' cars (their ids
// on its stack, +0x28 + 8 n, n < [+0x10], kept when their +0x2d8/+0x2dc are [+0x20]/[+0x24]),
// each car's track at +0x2e8 (sent by its cabinet as an id, +0x1afd80); the game's counts only
// Tropical4, Canyon4 and Alpine4 (by name), the most voted, Tropical4 first on a tie, then Canyon4,
// Tropical4 without a vote. Ours at its end (+0x24efca, its result in eax), when a Revo track is
// among the choices: every track counted by its id, the most voted, on a tie the game's order then
// the lowest id (the same on every cabinet, as their data); Sega Rally 3's tracks only: the game's
// result, unchanged; no vote: the track chosen here when it is a Revo one (alone), else the game's
typedef void *(__stdcall *CarFn)(void *cars, unsigned lo, unsigned hi, int create);

static const char *track_base(unsigned id)
{
    unsigned char *db = *(unsigned char **)g_dbAddr;
    for (unsigned *node = db ? *(unsigned **)(db + 8) + 2 : NULL; node; node = (unsigned *)node[0])
        if (node[1] == id)
            return (const char *)node[6];
    return "?";
}

static int vote_rank(unsigned id)
{
    static const char *const order[3] = {"Tropical4", "Canyon4", "Alpine4"};
    const char *base = track_base(id);
    for (int i = 0; i < 3; i++)
        if (!strcmp(base, order[i]))
            return i;
    return 3;
}

static unsigned __cdecl link_vote(const unsigned char *frame, unsigned gameChoice)
{
    unsigned count = *(const unsigned *)(frame + 0x10), s0 = *(const unsigned *)(frame + 0x20), s1 = *(const unsigned *)(frame + 0x24);
    unsigned ids[6];
    int votes[6], kinds = 0, players = 0;
    for (unsigned i = 0; i < count && i < 6; i++)
    {
        unsigned lo = *(const unsigned *)(frame + 0x28 + i * 8), hi = *(const unsigned *)(frame + 0x2c + i * 8);
        unsigned char *car = (unsigned char *)((CarFn)VA(0x5af900))(VA(0x9db478), lo, hi, 0);
        if (!car || *(unsigned *)(car + 0x2d8) != s0 || *(unsigned *)(car + 0x2dc) != s1)
            continue;
        unsigned *node = *(unsigned **)(car + 0x2e8);
        if (!node)
            continue;
        players++;
        int k = 0;
        while (k < kinds && ids[k] != node[1])
            k++;
        if (k == kinds)
        {
            ids[kinds] = node[1];
            votes[kinds++] = 0;
        }
        votes[k]++;
    }
    unsigned result = gameChoice;
    int revo = 0;
    for (int k = 0; k < kinds; k++)
        revo |= ids[k] >= g_firstRevoId;
    if (kinds && !revo)
        ; // Sega Rally 3's tracks only: the game's own result (Classic Race's Desert4 as the game has it)
    else if (kinds)
    {
        int best = 0;
        for (int k = 1; k < kinds; k++)
        {
            int rk = vote_rank(ids[k]), rb = vote_rank(ids[best]);
            if (votes[k] > votes[best] || (votes[k] == votes[best] && (rk < rb || (rk == rb && ids[k] < ids[best]))))
                best = k;
        }
        result = ids[best];
    }
    else
    {
        unsigned *cur = *(unsigned **)g_curTrackAddr;
        if (cur && cur[1] >= g_firstRevoId)
            result = cur[1];
    }
    char text[256];
    int n = snprintf(text, sizeof text, "segarally3: Revo: the vote, %d player(s) of %u:", players, count);
    for (int k = 0; k < kinds && n < (int)sizeof text; k++)
        n += snprintf(text + n, sizeof text - n, " %s (%u) %d", track_base(ids[k]), ids[k], votes[k]);
    wl_log("%s; %s (%u)%s", text, track_base(result), result, result == gameChoice ? ", as the game's" : "");
    return result;
}

__declspec(naked) static void vote_hook(void)
{
    __asm {
        push eax          // the game's result
        lea ecx, [esp + 8] // the vote's stack, as at +0x24efca
        push ecx
        call link_vote
        add esp, 8
        mov edx, g_dbAddr // the instruction replaced: mov edx, [0xb2a850]
        mov edx, [edx]
        ret
    }
}

// The tracks' unlock flags: 6 words, one per track of the database's list (+0x28 of the player's
// state), set by +0x195130 (the track's id on the stack, the state in edi) for every node of the
// list (+0x1952a0, +0x195720) and read by +0x1950b0 (the id in edx): for the Revo tracks, past the 6,
// they wrote over what follows (the number of cars, 22 in the championship: its race's loading then
// waited forever). A Revo track's: nothing written, always available
static void *g_unlockSetOriginal, *g_unlockGetOriginal;

__declspec(naked) static void unlock_set_hook(void)
{
    __asm {
        mov eax, [esp + 4]
        cmp eax, g_firstRevoId
        jae revo
        jmp dword ptr [g_unlockSetOriginal]
    revo:
        xor eax, eax
        ret 4
    }
}

__declspec(naked) static void unlock_get_hook(void)
{
    __asm {
        cmp edx, g_firstRevoId
        jae revo
        jmp dword ptr [g_unlockGetOriginal]
    revo:
        mov eax, 1
        ret 4
    }
}

static int call_patch(unsigned at, unsigned from, const void *to, int nops, const char *what)
{
    unsigned char expected[6], bytes[6];
    expected[0] = bytes[0] = 0xE8;
    *(int *)(expected + 1) = (int)(from - (at + 5));
    *(int *)(bytes + 1) = (int)((unsigned)to - ((unsigned)VA(at) + 5));
    bytes[5] = 0x90;
    return wl_patch_code(VA(at), expected, bytes, 5 + nops, what);
}

static void revo_setup(const wchar_t *gameDir)
{
    static int done; // once: the settings may be read again (the menu's saving)
    char option[MAX_PATH];
    wl_game_option("REVO_DIR", option, sizeof option);
    if (!option[0] || done++)
        return;
    wchar_t dir[MAX_PATH], full[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, option, -1, dir, MAX_PATH);
    if (!(dir[0] && dir[1] == L':') && !(dir[0] == L'\\' && dir[1] == L'\\'))
    {
        wchar_t joined[MAX_PATH];
        swprintf(joined, MAX_PATH, L"%s%s", gameDir, dir);
        wcscpy_s(dir, MAX_PATH, joined);
    }
    if (!GetFullPathNameW(dir, MAX_PATH, full, NULL))
        wcscpy_s(full, MAX_PATH, dir);
    size_t n = wcslen(full);
    while (n > 3 && full[n - 1] == L'\\')
        full[--n] = 0;
    g_image = (unsigned char *)GetModuleHandleW(NULL);
    g_curTrackAddr = VA(0x9dbdb0);
    g_dbAddr = VA(0xb2a850);
    // This build's code only (all the known editions have it): what is changed is checked first
    static const unsigned char fill[] = {0x83, 0xEC, 0x40, 0x6A, 0x3F};
    static const unsigned char vote[] = {0x8B, 0x15, 0x50, 0xA8, 0xB2, 0x00};
    if (memcmp(VA(0x600660), fill, sizeof fill) || memcmp(VA(0x64efca), vote, sizeof vote) || *(unsigned *)VA(0x7246ec) != 0x648370 ||
        *(unsigned *)VA(0x7246f0) != 0x648270 || *(unsigned *)VA(0x6edaf0) != 0x5bb2d0 ||
        *(unsigned *)VA(0x6edaf4) != 0x5b9570)
    {
        wl_log("segarally3: Revo: REVO_DIR given, but not the Rally.exe this was made for (9228B72E): Revo's tracks left out");
        return;
    }
    char folder[MAX_PATH * 2];
    WideCharToMultiByte(CP_UTF8, 0, full, -1, folder, sizeof folder, NULL, NULL);
    int perEnv[ENV_COUNT] = {0};
    for (int i = 0; i < REVO_MAX; i++)
    {
        wchar_t probe[MAX_PATH];
        swprintf(probe, MAX_PATH, L"%s\\Main_release\\tracks\\%S\\%S_master_xdata.sbf", full, g_revoTracks[i].base, g_revoTracks[i].base);
        if (GetFileAttributesW(probe) == INVALID_FILE_ATTRIBUTES)
            continue;
        char from[MAX_PATH], to[MAX_PATH * 2];
        snprintf(from, sizeof from, ".\\main_release\\tracks\\%s\\", g_revoTracks[i].base);
        snprintf(to, sizeof to, "%s\\Main_release\\tracks\\%s\\", folder, g_revoTracks[i].base);
        wl_files_redirect_prefix(from, to);
        g_revo[g_revoCount].base = g_revoTracks[i].base;
        snprintf(g_revo[g_revoCount].text, sizeof g_revo[0].text, "WL_REVO_%s", g_revoTracks[i].base);
        g_revo[g_revoCount].env = g_revoTracks[i].env;
        g_revo[g_revoCount].slot = i;
        g_revoMask |= 1u << i;
        g_revoCount++;
        perEnv[g_revoTracks[i].env]++;
    }
    if (!g_revoCount)
    {
        wl_log("segarally3: Revo: no track of Sega Rally Revo in %s (REVO_DIR): Main_release\\tracks\\<track>\\ expected", folder);
        return;
    }
    wl_log("segarally3: Revo: %d tracks in %s (tropical %d, canyon %d, alpine %d, lakeside %d, safari %d, arctic %d), list 0x%06X", g_revoCount, folder,
           perEnv[ENV_TROPICAL], perEnv[ENV_CANYON], perEnv[ENV_ALPINE], perEnv[ENV_LAKESIDE], perEnv[ENV_SAFARI], perEnv[ENV_ARCTIC], g_revoMask);
    // The network id (Demonware's title id, +0x197de0: 1 at +0x30caa4, else NetworkTitleID.txt, + 1000
    // for a twin cabinet; the game info screen's "NETWORK ID"): the local network's sessions of
    // another id are not listed (+0x2655xx, "discovery/gameinfo"). Ours from the Revo tracks found:
    // a cabinet links only with those that have the same ones (the tracks sent by their ids), never
    // with Sega Rally 3 alone or another list
    {
        unsigned h = 2166136261u;
        for (int b = 0; b < 4; b++)
            h = (h ^ ((g_revoMask >> (8 * b)) & 0xFF)) * 16777619u;
        unsigned one = 1, id = 1 + ((0x4000u | ((h ^ (h >> 14)) & 0x3FFF)) << 16);
        wchar_t file[MAX_PATH];
        swprintf(file, MAX_PATH, L"%sNetworkTitleID.txt", gameDir);
        if (GetFileAttributesW(file) != INVALID_FILE_ATTRIBUTES)
            wl_log("segarally3: Revo: NetworkTitleID.txt in the game's folder: its id is the game's, not Revo's (cabinets without Revo may join)");
        if (wl_patch_code(VA(0x70caa4), &one, &id, 4, "Sega Rally 3's network id (Revo's tracks)"))
            wl_log("segarally3: Revo: network id %u (NETWORK ID, + 1000 on a twin cabinet): linked with the cabinets of the same Revo tracks only",
                   id);
    }
    wl_hook_code(VA(0x600660), (void *)fill_hook, &g_fillOriginal, "Sega Rally 3's track list (Revo's tracks)");
    wl_hook_code(VA(0x62aa30), (void *)card_made, &g_cardOriginal, "Sega Rally 3's track cards (Revo's tracks)");
    wl_hook_code(VA(0x595130), (void *)unlock_set_hook, &g_unlockSetOriginal, "Sega Rally 3's track unlocks (Revo's tracks)");
    wl_hook_code(VA(0x5950b0), (void *)unlock_get_hook, &g_unlockGetOriginal, "Sega Rally 3's track unlock check (Revo's tracks)");
    call_patch(0x666a1d, 0x5af700, (void *)track_choice, 0, "Sega Rally 3's track choice (Revo's tracks)");
    unsigned char vbytes[6] = {0xE8};
    *(int *)(vbytes + 1) = (int)((unsigned)vote_hook - ((unsigned)VA(0x64efca) + 5));
    vbytes[5] = 0x90;
    wl_patch_code(VA(0x64efca), vote, vbytes, 6, "Sega Rally 3's track vote (Revo's tracks)");
    g_byHashOriginal = (ByHashFn)VA(0x5b9570);
    unsigned byHash = (unsigned)revo_text_by_hash, byHashWas = 0x5b9570;
    wl_patch_code(VA(0x6edaf4), &byHashWas, &byHash, 4, "Sega Rally 3's texts by hash (Revo's tracks' names)");
    g_textOriginal = (TextFn)VA(0x5bb2d0);
    unsigned text = (unsigned)revo_text, textWas = 0x5bb2d0;
    wl_patch_code(VA(0x6edaf0), &textWas, &text, 4, "Sega Rally 3's texts (Revo's tracks' names)");
    g_state9 = (StateFn)VA(0x648370);
    g_state10 = (StateFn)VA(0x648270);
    g_state3 = (StateFn)VA(0x648960);
    unsigned s3 = (unsigned)state3, o3 = 0x648960;
    // The championships' page, with the championship's grid placed on a Revo track (champ_starts)
    static const unsigned char starts[9] = {0x83, 0xEC, 0x08, 0x8B, 0x15, 0x98, 0x5C, 0xA9, 0x00};
    if (memcmp(VA(0x5d33f0), starts, sizeof starts))
        wl_log("segarally3: Revo: the championship's grid not the one known (Rally.exe+0x1d33f0): Revo's championships left out");
    else if (wl_hook_code(VA(0x5d33f0), (void *)champ_starts, (void **)&g_startsOriginal, "Sega Rally 3's championship grid (Revo's championships)"))
    {
        g_champsOn = 1;
        wl_patch_code(VA(0x7246d4), &o3, &s3, 4, "Sega Rally 3's menu state 3 (Revo's championships)");
    }
    unsigned s9 = (unsigned)state9, s10 = (unsigned)state10, o9 = 0x648370, o10 = 0x648270;
    wl_patch_code(VA(0x7246ec), &o9, &s9, 4, "Sega Rally 3's menu state 9 (Revo's environments)");
    wl_patch_code(VA(0x7246f0), &o10, &s10, 4, "Sega Rally 3's menu state 10 (Revo's tracks)");
}

// ------------------------------------------------------------------------------
// The cameras: [Game.segarally3] CHASE_CAM (the chase camera's settings) and HIDDEN_CAMERAS (the
// game's unused cameras in the View Change button's cycle). Reimplemented from SR3CamLab (MIT,
// Copyright (c) 2026 Ninja Dynamics, https://github.com/ninjadynamics/SR3CamLab): its findings
// (the addresses, the camera manager, the chase camera's aim and eye), its profiles and their
// formulas; their code here is windowsloader's own. Both are changed from the menu at once: the
// game's code is changed the first time one is wanted, never before.
// ------------------------------------------------------------------------------

// The chase camera (Rally.exe's update +0x1f7xxx, its two kinds: vtable 0x6eb808 the chase,
// 0x6eb8b8 the far chase): its spring's stiffness from two getters (vtable +0xa0 the largest,
// +0xa4 the smallest, the game's from its settings) times 7.5 (0x6effe4), its damping -15
// (0x6f0550); its aim the car's heading made 2D (normalized by +0x1e00 at +0x1f72dd, the car's
// velocity at the caller's esp+0x2c); its eye placed by +0x1f2600 (angle, distance, height on the
// stack, the camera in ecx; its field of view at +0x3b0, degrees; its base distance at +0x30)
struct ChaseProfile
{
    const char *value, *name;
    float strength;            // 0..1: how much the camera turns to the car's path rather than its nose
    float fadeStart, fadeFull; // km/h: from no turn to all of it
    float kmin, kmax;          // the spring's stiffness
    float damping;
    float maxAngle; // degrees: how far the camera may swing from behind the car (180: no limit)
    float cap;      // 0..1: where the soft limit starts, of maxAngle
    float distance, height, fov;
};

// profiles.yaml's (SR3CamLab)
static const ChaseProfile g_chaseProfiles[] = {
    {"DAYTONA", "Daytona", 0.7f, 14, 43, 65, 92, 11.5f, 35, 0.25f, 3.8f, 1.9f, 72},
    {"CHASE", "Chase", 0.7f, 14, 43, 30, 50, 11.5f, 35, 0.25f, 5.1f, 1.6f, 60},
    {"CHASE_FAR", "Chase Far", 0.7f, 14, 43, 30, 50, 11.5f, 35, 0.25f, 6.4f, 1.8f, 60},
    {"DRONE", "Drone", 0.7f, 14, 43, 30, 50, 11.5f, 35, 0.25f, 6.0f, 4.6f, 87},
};

static struct
{
    volatile LONG on; // a profile (else the game's camera: everything handed on)
    const char *name;
    float strength, vmin, vinv, slip, knee, range, invRange, distance, height, fov;
} g_chase;
static __declspec(align(16)) float g_chaseMult[4] = {7.5f, 11.0f, 14.25f, 9.0f}; // read as 16 bytes (movups), the first one used
static float g_chaseDamp = -15.0f, g_chaseKmin, g_chaseKmax, g_chaseDelta;
static void *g_normalize, *g_eyeOriginal, *g_kmaxOriginal, *g_kminOriginal;
static struct
{
    unsigned char *cam;
    float fov;
} g_fovSaved[2]; // the game's field of view of the cameras changed, back with the game's camera

static float clamp01(float x)
{
    return x < 0 ? 0 : x > 1 ? 1 : x;
}

// After the aim's normalization: turned toward the car's path, the turn kept for the eye
static void __cdecl chase_aim(unsigned char *sp)
{
    if (!g_chase.on || g_chase.strength <= 0)
    {
        g_chaseDelta = 0;
        return;
    }
    float *v = *(float **)(sp + 4);
    float tax = v[0], taz = v[2];
    float vx = *(float *)(sp + 0x30), vz = *(float *)(sp + 0x38);
    float speed = sqrtf(vx * vx + vz * vz);
    if (speed > 0.5f)
    {
        float fade = clamp01((speed - g_chase.vmin) * g_chase.vinv);
        float nx = vx / speed, nz = vz / speed;
        float t = fade * clamp01((v[0] * nx + v[2] * nz) * g_chase.slip) * g_chase.strength;
        float x = v[0] + (nx - v[0]) * t, z = v[2] + (nz - v[2]) * t;
        float len = sqrtf(x * x + z * z);
        if (len > 1e-4f)
        {
            v[0] = x / len;
            v[2] = z / len;
        }
    }
    g_chaseDelta = atan2f(v[0] * taz - v[2] * tax, v[0] * tax + v[2] * taz);
}

__declspec(naked) static void chase_aim_hook(void)
{
    __asm {
        push dword ptr [esp + 8] // the game's call: normalize(dst, src)
        push dword ptr [esp + 8]
        call dword ptr [g_normalize]
        add esp, 8
        push eax
        lea ecx, [esp + 4] // the stack as at the call
        push ecx
        call chase_aim
        add esp, 4
        pop eax
        ret
    }
}

static void fov_restore(void)
{
    for (int i = 0; i < 2; i++)
        if (g_fovSaved[i].cam)
        {
            if (*(unsigned *)g_fovSaved[i].cam == 0x6eb808 || *(unsigned *)g_fovSaved[i].cam == 0x6eb8b8)
                *(float *)(g_fovSaved[i].cam + 0x3b0) = g_fovSaved[i].fov;
            g_fovSaved[i].cam = NULL;
        }
}

// The eye's angle, distance and height (the far chase farther and higher), its field of view
static void __cdecl chase_eye(unsigned char *sp, unsigned char *cam)
{
    if (!g_chase.on)
    {
        if (g_fovSaved[0].cam)
            fov_restore();
        return;
    }
    float *angle = (float *)(sp + 4), *distance = (float *)(sp + 8), *height = (float *)(sp + 12);
    int isFar = *(unsigned *)cam == 0x6eb8b8;
    *distance = g_chase.distance * (isFar ? 1.25f : 1.0f) + *(float *)(cam + 0x30);
    *height = g_chase.height * (isFar ? 1.15f : 1.0f);
    int k = 0;
    while (k < 2 && g_fovSaved[k].cam && g_fovSaved[k].cam != cam)
        k++;
    if (k < 2 && !g_fovSaved[k].cam)
    {
        g_fovSaved[k].cam = cam;
        g_fovSaved[k].fov = *(float *)(cam + 0x3b0);
    }
    *(float *)(cam + 0x3b0) = g_chase.fov;
    float delta = g_chaseDelta == g_chaseDelta ? g_chaseDelta : 0; // NaN: none
    float a = *angle + delta, m = fabsf(a);
    if (m > g_chase.knee)
    {
        float t = (m - g_chase.knee) * g_chase.invRange;
        m = g_chase.knee + g_chase.range * t / sqrtf(1 + t * t); // a soft limit
        a = a < 0 ? -m : m;
    }
    *angle = a;
}

__declspec(naked) static void chase_eye_hook(void)
{
    __asm {
        pushad
        lea eax, [esp + 32] // the stack as at the call
        push ecx            // the camera
        push eax
        call chase_eye
        add esp, 8
        popad
        jmp dword ptr [g_eyeOriginal]
    }
}

__declspec(naked) static void chase_kmax_hook(void)
{
    __asm {
        cmp g_chase.on, 0
        je game
        fld g_chaseKmax
        ret
    game:
        jmp dword ptr [g_kmaxOriginal]
    }
}

__declspec(naked) static void chase_kmin_hook(void)
{
    __asm {
        cmp g_chase.on, 0
        je game
        fld g_chaseKmin
        ret
    game:
        jmp dword ptr [g_kminOriginal]
    }
}

static int g_chaseHooked; // 0 not yet, 1 done, -1 not this Rally.exe

static int chase_hooks(void)
{
    if (g_chaseHooked)
        return g_chaseHooked > 0;
    g_chaseHooked = -1;
    static const unsigned char aim[] = {0xE8, 0x1E, 0xAB, 0xE0, 0xFF}, eye[] = {0xE8, 0xD2, 0xA7, 0xFF, 0xFF};
    if (memcmp(VA(0x5f72dd), aim, 5) || memcmp(VA(0x5f7e29), eye, 5) || *(unsigned *)VA(0x5f724d) != 0x6effe4 ||
        *(unsigned *)VA(0x5f76b0) != 0x6f0550 || *(unsigned *)VA(0x6eb8a8) != 0x5ec4c0 || *(unsigned *)VA(0x6eb8ac) != 0x5ec4b0 ||
        *(unsigned *)VA(0x6eb958) != 0x5ec4c0 || *(unsigned *)VA(0x6eb95c) != 0x5ec4b0)
    {
        wl_log("segarally3: cameras: not the Rally.exe this was made for (9228B72E): CHASE_CAM left out");
        return 0;
    }
    g_normalize = VA(0x401e00);
    g_eyeOriginal = VA(0x5f2600);
    g_kmaxOriginal = VA(0x5ec4c0);
    g_kminOriginal = VA(0x5ec4b0);
    unsigned mult = (unsigned)g_chaseMult, damp = (unsigned)&g_chaseDamp, kmax = (unsigned)chase_kmax_hook, kmin = (unsigned)chase_kmin_hook;
    unsigned multWas = 0x6effe4, dampWas = 0x6f0550, kmaxWas = 0x5ec4c0, kminWas = 0x5ec4b0;
    int ok = wl_patch_code(VA(0x5f724d), &multWas, &mult, 4, "Sega Rally 3's chase camera stiffness (CHASE_CAM)") &&
             wl_patch_code(VA(0x5f76b0), &dampWas, &damp, 4, "Sega Rally 3's chase camera damping (CHASE_CAM)") &&
             wl_patch_code(VA(0x6eb8a8), &kmaxWas, &kmax, 4, "Sega Rally 3's chase camera spring (CHASE_CAM)") &&
             wl_patch_code(VA(0x6eb8ac), &kminWas, &kmin, 4, "Sega Rally 3's chase camera spring (CHASE_CAM)") &&
             wl_patch_code(VA(0x6eb958), &kmaxWas, &kmax, 4, "Sega Rally 3's far chase camera spring (CHASE_CAM)") &&
             wl_patch_code(VA(0x6eb95c), &kminWas, &kmin, 4, "Sega Rally 3's far chase camera spring (CHASE_CAM)") &&
             call_patch(0x5f72dd, 0x401e00, (void *)chase_aim_hook, 0, "Sega Rally 3's chase camera aim (CHASE_CAM)") &&
             call_patch(0x5f7e29, 0x5f2600, (void *)chase_eye_hook, 0, "Sega Rally 3's chase camera eye (CHASE_CAM)");
    g_chaseHooked = ok ? 1 : -1;
    return ok;
}

// The option's value: a profile's constants, or the game's camera
static void chase_apply(const char *value)
{
    const ChaseProfile *p = NULL;
    for (int i = 0; i < (int)(sizeof g_chaseProfiles / sizeof g_chaseProfiles[0]); i++)
        if (value && !_stricmp(value, g_chaseProfiles[i].value))
            p = &g_chaseProfiles[i];
    if (!p)
    {
        if (g_chase.on)
            wl_log("segarally3: cameras: the game's chase camera");
        InterlockedExchange(&g_chase.on, 0);
        g_chaseMult[0] = 7.5f;
        g_chaseDamp = -15.0f;
        return;
    }
    if (!chase_hooks())
        return;
    InterlockedExchange(&g_chase.on, 0); // the constants changed while the hooks pass
    g_chase.name = p->name;
    g_chase.strength = p->strength;
    g_chase.vmin = p->fadeStart / 3.6f;
    g_chase.vinv = 3.6f / (p->fadeFull - p->fadeStart);
    g_chase.slip = 2.0f;
    if (p->maxAngle >= 180)
    {
        g_chase.knee = 3.14159265f;
        g_chase.range = 1e-4f;
    }
    else
    {
        float m = p->maxAngle * 3.14159265f / 180;
        g_chase.knee = p->cap * m;
        g_chase.range = m - g_chase.knee > 1e-4f ? m - g_chase.knee : 1e-4f;
    }
    g_chase.invRange = 1 / g_chase.range;
    g_chase.distance = p->distance;
    g_chase.height = p->height;
    g_chase.fov = p->fov;
    g_chaseKmin = p->kmin;
    g_chaseKmax = p->kmax;
    g_chaseMult[0] = 1.0f;
    g_chaseDamp = -p->damping;
    InterlockedExchange(&g_chase.on, 1);
    wl_log("segarally3: cameras: chase camera %s (stiffness %.0f..%.0f, damping %.1f, path %.2f, swing %.0f deg, %.1f m, %.1f m high, fov %.0f)",
           p->name, p->kmin, p->kmax, p->damping, p->strength, p->maxAngle, p->distance, p->height, p->fov);
}

// The cameras of the race's list, the View Change button's cycle: the camera manager
// [0x9eb4ec]+0x18 (its frame's time at +0), its cameras inside it (the chase +0x510), the one
// in use at +0x190, the list's count at +0x22c, the index chosen at +0x230 (View Change: + 1,
// modulo the count), its cameras from +0x234 (21 at most). The game's other cameras, made but
// never in the arcade's list, appended to it; the car's spin and the free cameras left out (the
// spin stops the race's clock, the free ones need a mouse). [0xa65794]: the scenery all drawn
// (the PVS built for eyes near the road leaves holes around the higher or farther ones)
#define CAM_MAX 21
#define CAM_SEAT 0x80000000u // the cockpit's camera from the driver's seat (its eye mirrored)

static const struct
{
    unsigned off;
    const char *name;
} g_camNames[] = {
    {0x510, "Chase"},          {0x8c4, "Far chase"}, {0x1700, "Bumper"}, {0x189c, "Bonnet"},
    {0x1a38, "Cockpit"},       {CAM_SEAT | 0x1a38, "Driver's seat"},     {0x1bd0, "Wheel"},
};
static const unsigned g_camExtras[] = {0x8c4, 0x1700, 0x189c, 0x1a38, CAM_SEAT | 0x1a38, 0x1bd0};

static volatile LONG g_hiddenOn;
static int g_framesHooked; // 0 not yet, 1 done, -1 not this Rally.exe
static struct
{
    unsigned char *mgr;
    int base, total, last, keep, quiet;
    unsigned char *cams[CAM_MAX];
    unsigned offs[CAM_MAX];
} g_list;
static float g_seatEye[6];
static float *g_seatSaved;

static const char *cam_name(unsigned off)
{
    for (int i = 0; i < (int)(sizeof g_camNames / sizeof g_camNames[0]); i++)
        if (g_camNames[i].off == off)
            return g_camNames[i].name;
    return "Camera";
}

// The cockpit's camera: from the driver's seat (its eye and look points, car space, x sideways,
// [cam+0x194], a mirrored copy) while that entry is chosen, its own otherwise
static void cam_seat(unsigned char *mgr, int seat)
{
    unsigned char *cockpit = mgr + 0x1a38;
    if (*(unsigned *)cockpit != 0x6ebdf0)
        return;
    float **eye = (float **)(cockpit + 0x194);
    if (seat)
    {
        if (*eye != g_seatEye && *eye)
        {
            g_seatSaved = *eye;
            for (int i = 0; i < 6; i++)
                g_seatEye[i] = g_seatSaved[i];
            g_seatEye[0] = -g_seatEye[0];
            g_seatEye[3] = -g_seatEye[3];
            *eye = g_seatEye;
        }
    }
    else if (*eye == g_seatEye)
        *eye = g_seatSaved;
}

static void cam_shown(int i)
{
    unsigned off = g_list.offs[i];
    const char *name = cam_name(off);
    char text[64];
    if (off == 0x510 && g_chase.on)
        snprintf(text, sizeof text, "%s (%s)", name, g_chase.name);
    else
        snprintf(text, sizeof text, "%s", name);
    wl_menu_flash(text, i >= g_list.base ? 0xFFD040 : 0xFFFFFF, 2000);
}

// Each frame, before the picture is presented
static void __cdecl cams_frame(void)
{
    unsigned char *m = *(unsigned char **)VA(0x9eb4ec);
    if (!m)
    {
        g_list.mgr = NULL;
        return;
    }
    m += 0x18;
    unsigned char *chase = m + 0x510;
    int *count = (int *)(m + 0x22c), *index = (int *)(m + 0x230);
    unsigned char **list = (unsigned char **)(m + 0x234), **inUse = (unsigned char **)(m + 0x190);
    if (*(unsigned *)chase != 0x6eb808 || *count < 1 || *count > CAM_MAX || list[0] != chase)
        return; // no race's list
    int ours = g_list.mgr == m && *count == g_list.total;
    for (int i = g_list.base; ours && i < g_list.total; i++)
        ours = list[i] == g_list.cams[i];
    if (!g_hiddenOn)
    {
        if (ours && g_list.total > g_list.base) // turned off from the menu: the game's list back
        {
            cam_seat(m, 0);
            if (*index >= g_list.base)
            {
                if (*inUse == list[*index])
                    *inUse = list[0];
                *index = 0;
            }
            *count = g_list.base;
            *(int *)VA(0xa65794) = 0;
            wl_log("segarally3: cameras: the game's %d cameras", g_list.base);
        }
        g_list.mgr = NULL;
        return;
    }
    if (!ours)
    {
        // The game's list (a race, a stage): its cameras' names, the others appended
        int n = *count;
        for (int i = 0; i < n; i++)
        {
            g_list.cams[i] = list[i];
            g_list.offs[i] = (unsigned)(list[i] - m);
        }
        g_list.base = n;
        char text[256];
        int t = snprintf(text, sizeof text, "segarally3: cameras: the game's");
        for (int i = 0; i < n && t < (int)sizeof text; i++)
            t += snprintf(text + t, sizeof text - t, " %s (+0x%X)", cam_name(g_list.offs[i]), g_list.offs[i]);
        for (int e = 0; e < (int)(sizeof g_camExtras / sizeof g_camExtras[0]) && n < CAM_MAX; e++)
        {
            unsigned off = g_camExtras[e];
            unsigned char *cam = m + (off & ~CAM_SEAT);
            if (!*(unsigned *)cam)
                continue; // not made
            int there = 0;
            for (int i = 0; i < n && !(off & CAM_SEAT); i++)
                there |= g_list.cams[i] == cam;
            if (there)
                continue;
            g_list.cams[n] = list[n] = cam;
            g_list.offs[n] = off;
            n++;
        }
        *count = n;
        g_list.mgr = m;
        g_list.total = n;
        g_list.last = -1;
        g_list.quiet = 1;
        // The camera chosen before (the previous stage's): the game chose its own
        if (g_list.keep > 0 && g_list.keep - 1 < n)
            *index = g_list.keep - 1;
        if (t < (int)sizeof text)
            snprintf(text + t, sizeof text - t, "; %d added", n - g_list.base);
        wl_log("%s", text);
    }
    int i = *index;
    cam_seat(m, i >= 0 && i < g_list.total && g_list.offs[i] == (CAM_SEAT | 0x1a38));
    if (i == g_list.last)
        return;
    if (g_list.last >= 0 && i != (g_list.last + 1) % g_list.total)
    {
        // Not View Change (a step forward): the game's reset of its camera (a stage's start, a
        // continue), the camera chosen kept
        if (*inUse == list[i])
            *inUse = list[g_list.last];
        *index = g_list.last;
        return;
    }
    if (i < 0 || i >= g_list.total)
        return;
    g_list.last = i;
    g_list.keep = i + 1;
    *(int *)VA(0xa65794) = i >= g_list.base;
    if (g_list.quiet)
        g_list.quiet = 0;
    else
        cam_shown(i);
}

static void *g_devAddr; // 0x7ed10c: the game's IDirect3DDevice9

__declspec(naked) static void cams_frame_hook(void)
{
    __asm {
        pushfd
        pushad
        call cams_frame
        popad
        popfd
        mov eax, g_devAddr // the instruction replaced: mov eax, [0x7ed10c]
        mov eax, [eax]
        ret
    }
}

static void hidden_apply(const char *value)
{
    int on = value && (!_stricmp(value, "true") || !strcmp(value, "1"));
    if (on && !g_framesHooked)
    {
        g_framesHooked = -1;
        static const unsigned char was[] = {0xA1, 0x0C, 0xD1, 0x7E, 0x00};
        unsigned char bytes[5] = {0xE8};
        *(int *)(bytes + 1) = (int)((unsigned)cams_frame_hook - ((unsigned)VA(0x591c4d) + 5));
        g_devAddr = VA(0x7ed10c);
        if (wl_patch_code(VA(0x591c4d), was, bytes, 5, "Sega Rally 3's frame (HIDDEN_CAMERAS)"))
            g_framesHooked = 1;
        else
            wl_log("segarally3: cameras: not the Rally.exe this was made for (9228B72E): HIDDEN_CAMERAS left out");
    }
    if (g_framesHooked > 0 && on != (int)g_hiddenOn)
        wl_log("segarally3: cameras: hidden cameras %s", on ? "in the View Change button's cycle" : "left out");
    InterlockedExchange(&g_hiddenOn, on && g_framesHooked > 0);
}

static void cams_setup(void)
{
    g_image = (unsigned char *)GetModuleHandleW(NULL);
    char value[WL_OPTION_VALUE_SIZE];
    wl_game_option("CHASE_CAM", value, sizeof value);
    chase_apply(value);
    hidden_apply(wl_game_option_bool("HIDDEN_CAMERAS") ? "true" : "false");
}
#else
static void revo_setup(const wchar_t *) // the game is 32 bits
{
}
static void cams_setup(void)
{
}
static void chase_apply(const char *)
{
}
static void hidden_apply(const char *)
{
}
#endif

// The link between cabinets (link.h): 1 to 4 (Demonware's LAN play: its cabinets found by a
// broadcast, a session joined; GetAdaptersInfo for its address, Winsock 1 by number)

static wchar_t g_gameDir[MAX_PATH];

static void sr3_settings(void)
{
    wl_link_settings(GetModuleHandleW(NULL));
    revo_setup(g_gameDir);
    cams_setup();
}

int wl_game_segarally3(const wchar_t *exePath)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    if (_wcsicmp(base, L"Rally.exe"))
        return 0;

    wchar_t dir[MAX_PATH];
    wcsncpy_s(dir, MAX_PATH, exePath, base - exePath);
    wcscpy_s(g_gameDir, MAX_PATH, dir);
    wl_log("game: Sega Rally 3 (Europa-R)");
    wl_game_set_boards("Controls on USB HID board, Sega Europa-R I/O (VID 1043, PID 0000)",
                       "Motor board: Sega PC parallel-port driver (I/O port 0x1020: strength nibble, direction bit)");

    for (int i = 0; i < 24; i++)
    {
        snprintf(g_bitNames[i], sizeof g_bitNames[i], "Bit%d", i);
        WlInput *in = &g_inputs[IN_BIT0 + i];
        in->name = g_bitNames[i];
        in->action = NULL;
        for (int k = 0; k < (int)(sizeof g_switches / sizeof g_switches[0]); k++)
            if (g_switches[k].bit == i)
            {
                in->name = g_switches[k].name;
                in->action = g_switches[k].action;
            }
        in->type = WL_INPUT_SWITCH;
        in->rawLow = 0;
        in->rawHigh = 1;
        in->rawRest = 0;
    }
    read_calibration(dir);
    wl_inputs_set_table(g_inputs, IN_COUNT);
    wl_vhid_register(&g_board);
    wl_ioports_set_writer(port_write);
    // Its motor is that port's byte, no Sega servo board: no RawDrive frames made from its force
    // (network outputs); its I/O board gets nothing from the game (no lamps)
    wl_outputs_no_servo();
    wl_files_set_ini_override(ini_override);
    wl_display_set_internal_supported(1);
    // Its window procedure (+0x158770) pauses the game on WM_ACTIVATEAPP (inactive): another program's
    // window in front (another emulator's, the desktop's) froze a race or its loading until the game's
    // was clicked; [Display] KEEP_ACTIVE (true by default, display.cpp) keeps that message from it
    wl_game_use_options("segarally3");
    wl_game_option_hooks("CHASE_CAM", chase_apply, NULL);
    wl_game_option_hooks("HIDDEN_CAMERAS", hidden_apply, NULL);
    wl_game_on_settings(sr3_settings);
    return 1;
}
