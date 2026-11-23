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

// wl_avsboot.exe <game.dll> [--avs-log]: the program of Konami's AVS games, whose code is a DLL
// (Road Fighters 3D: jgt.dll, which exports dll_entry_init and dll_entry_main) that the cabinet's
// launcher boots. Named "wl_" as windowsloader's own and "avsboot" as the boot of AVS, Konami's
// system library (libavs-win32.dll); the launcher starts it in place of a game's program, with the
// DLL's folder as its working folder, and injects windowsloader32.dll in it as in any game, which
// shows it to the game as <that folder>\wl_avsboot.exe. Built for 32-bit games only.
//
// Reimplemented from windowsarcadeloader's wal-avs-boot (aderumier, GPL v3,
// https://github.com/aderumier/windowsarcadeloader, crates/loader/src/avs_boot.rs), which follows
// the cabinet's launcher and spice2x's (avs/core.cpp, avs/ea3.cpp). The steps:
// 0. Windows' own copies of its components loaded first when the game's folder has another
//    loader's proxy of one (FFBArcadePlugin's dinput8.dll), so that the game's DLL, which imports
//    them, gets Windows' (the launcher does the same for a program's imports); then kbt.dll and
//    kld.dll, the cabinet's boot check libraries, when the folder has them (the game finds them
//    loaded, else windowsloader's module answers for them).
// 1. AVS (libavs-win32.dll, AVS 2 with its functions by name: avs_boot with two heaps) booted with
//    prop/avs-config.xml, its e-Amusement time server off (/config/sntp/ea_on: no network, the boot
//    would wait for it) and the PC's time zone (/config/time/gmt_offset).
// 2. libavs-win32-ea3.dll and the game's DLL loaded.
// 3. The e-Amusement identity of prop/ea3-config.xml (or ea3-cfg.xml, eamuse-config.xml) given to
//    AVS's environment (/env/profile/...: security code, PCB id, soft id code), with default ids
//    where the file has none.
// 4. dll_entry_init("<model><dest><spec><rev><ext>", prop/app-config.xml's /param).
// 5. ea3_boot(/ea3): offline, without Konami's servers, the game boots on in local mode.
// 6. dll_entry_main(): the game, until it ends; then ea3_shutdown, avs_shutdown.
// A game whose DLLs are in modules\ with prop\ in the folder above runs from that folder.
//
// Its messages go to the debugger's output, which windowsloader's log takes ("debug: wl_avsboot:
// ..."); with --avs-log, AVS's own log too.

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------------------------
// Messages
// ------------------------------------------------------------------------------

static void say(const char *fmt, ...)
{
    char text[1024] = "wl_avsboot: ";
    size_t n = strlen(text);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text + n, sizeof text - n, fmt, ap);
    va_end(ap);
    OutputDebugStringA(text);
}

static __declspec(noreturn) void fail(const char *fmt, ...)
{
    char text[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    say("%s", text);
    ExitProcess(1);
}

// AVS's log (--avs-log): its lines to the debugger's output
static int g_avsLog;

static unsigned __cdecl log_writer(void *ctx, const char *chars, unsigned n)
{
    if (!g_avsLog || !chars || !n)
        return n;
    char line[1024];
    unsigned len = 0;
    while (len < n && chars[len])
        len++;
    if (len >= sizeof line)
        len = sizeof line - 1;
    memcpy(line, chars, len);
    line[len] = 0;
    char *next = NULL;
    for (char *p = strtok_s(line, "\r\n", &next); p; p = strtok_s(NULL, "\r\n", &next))
        if (*p)
            say("avs: %s", p);
    return n;
}

// ------------------------------------------------------------------------------
// Files and DLLs
// ------------------------------------------------------------------------------

static int exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// A DLL of the game's folder (the working folder), by its full path: the DLLs it imports are
// looked for beside it first, as when the cabinet's launcher is in that folder
static HMODULE load(const char *name, int needed)
{
    char path[MAX_PATH];
    if (!GetFullPathNameA(name, MAX_PATH, path, NULL))
        fail("%s: no such path", name);
    if (!needed && !exists(path))
        return NULL;
    HMODULE m = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m && needed)
        fail("%s: cannot be loaded (error %lu)", path, GetLastError());
    if (m)
        say("%s loaded", path);
    return m;
}

static void *proc(HMODULE m, const char *dll, const char *name)
{
    void *p = (void *)GetProcAddress(m, name);
    if (!p)
        fail("%s: no %s (not the AVS this program knows: AVS 2 with its functions by name)", dll, name);
    return p;
}

// The Windows components another loader leaves a proxy of in a game's folder (the launcher's
// list): Windows' own loaded first, the game's DLL then gets it
static void system_components(void)
{
    static const char *names[] = {"dinput8.dll", "dinput.dll", "d3d8.dll", "d3d9.dll", "d3d11.dll", "dxgi.dll", "ddraw.dll",
                                  "opengl32.dll", "winmm.dll", "dsound.dll", "version.dll", "xinput1_3.dll", "xinput1_4.dll"};
    char system[MAX_PATH], path[MAX_PATH];
    UINT n = GetSystemDirectoryA(system, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
    {
        if (!exists(names[i]))
            continue;
        snprintf(path, MAX_PATH, "%s\\%s", system, names[i]);
        if (exists(path) && LoadLibraryA(path))
            say("%s in the game's folder (another loader's): Windows' own loaded, %s", names[i], path);
    }
}

// ------------------------------------------------------------------------------
// AVS
// ------------------------------------------------------------------------------

enum
{
    NODE_S32 = 6,
    NODE_STR = 11,
    NODE_BOOL = 52,
};

typedef int(__cdecl *Reader)(unsigned ctx, void *buf, size_t count);
typedef unsigned(__cdecl *LogWriter)(void *ctx, const char *chars, unsigned n);

static struct
{
    void(__cdecl *boot)(void *config, void *heap, size_t heapSize, void *heap2, size_t heap2Size, LogWriter writer, void *ctx);
    void(__cdecl *shutdown)(void);
    int(__cdecl *read_query_memsize)(Reader reader, unsigned ctx, void *a, void *b);
    void *(__cdecl *create)(unsigned flags, void *buf, unsigned size);
    int(__cdecl *insert_read)(void *prop, void *node, Reader reader, unsigned ctx);
    void *(__cdecl *search)(void *prop, void *node, const char *path);
    int(__cdecl *node_refer)(void *prop, void *node, const char *path, unsigned type, void *out, unsigned size);
    int(__cdecl *node_remove)(void *node);
    void *(__cdecl *node_create)(void *prop, void *node, unsigned type, const char *path, ...);
    int(__cdecl *setenv)(const char *name, const char *value);
} g_avs;

// The XML file being read
static char *g_data;
static size_t g_size, g_pos;

static int __cdecl reader(unsigned ctx, void *buf, size_t count)
{
    size_t n = count < g_size - g_pos ? count : g_size - g_pos;
    memcpy(buf, g_data + g_pos, n);
    g_pos += n;
    return (int)n;
}

// An XML file as a property, with extra bytes free for the nodes added to it
static void *prop_read(const char *path, size_t extra)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        fail("%s: cannot be read", path);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    free(g_data);
    g_data = (char *)malloc(size > 0 ? (size_t)size : 1);
    g_size = size > 0 && g_data ? fread(g_data, 1, (size_t)size, f) : 0;
    fclose(f);
    g_pos = 0;
    int need = g_avs.read_query_memsize(reader, 0, NULL, NULL);
    if (need <= 0)
        fail("%s: not a property file (0x%x)", path, need);
    size_t total = (size_t)need + extra;
    void *buf = calloc(1, total); // kept for the run, as AVS keeps the property
    void *prop = buf ? g_avs.create(23, buf, (unsigned)total) : NULL;
    if (!prop)
        fail("%s: property_create failed", path);
    g_pos = 0;
    if (!g_avs.insert_read(prop, NULL, reader, 0))
        fail("%s: property_insert_read failed", path);
    return prop;
}

static void *node(void *prop, const char *path)
{
    return g_avs.search(prop, NULL, path);
}

static void text(void *prop, const char *path, char *out, size_t size)
{
    out[0] = 0;
    if (g_avs.node_refer(prop, NULL, path, NODE_STR, out, (unsigned)size) <= 0)
        out[0] = 0;
    out[size - 1] = 0;
}

static void remove_node(void *prop, const char *path)
{
    void *n = node(prop, path);
    if (n)
        g_avs.node_remove(n);
}

static void set_text(void *prop, const char *path, const char *value)
{
    remove_node(prop, path);
    g_avs.node_create(prop, NULL, NODE_STR, path, value);
}

static void set_int(void *prop, const char *path, unsigned type, int value)
{
    remove_node(prop, path);
    g_avs.node_create(prop, NULL, type, path, value);
}

// ------------------------------------------------------------------------------
// The boot
// ------------------------------------------------------------------------------

#define HEAP_SIZE (16 * 1024 * 1024)

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdLine, int show)
{
    if (__argc < 2)
        fail("usage: wl_avsboot.exe <game.dll> [--avs-log]");
    char game[MAX_PATH];
    if (!WideCharToMultiByte(CP_ACP, 0, __wargv[1], -1, game, MAX_PATH, NULL, NULL))
        fail("the game's DLL's path cannot be read");
    for (int i = 2; i < __argc; i++)
        if (!_wcsicmp(__wargv[i], L"--avs-log"))
            g_avsLog = 1;

    // The game's DLLs in modules\, its files in the folder above
    if (!exists("prop\\avs-config.xml") && exists("..\\prop\\avs-config.xml"))
        SetCurrentDirectoryA("..");
    char dir[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, dir);
    say("%s from %s%s", game, dir, g_avsLog ? ", AVS's log on" : "");

    // 0. The components' proxies, the cabinet's boot check libraries
    system_components();
    load("kbt.dll", 0);
    load("kld.dll", 0);

    // 1. AVS
    HMODULE avs = load("libavs-win32.dll", 1);
    const char *a = "libavs-win32.dll";
    *(void **)&g_avs.boot = proc(avs, a, "avs_boot");
    *(void **)&g_avs.shutdown = proc(avs, a, "avs_shutdown");
    *(void **)&g_avs.read_query_memsize = proc(avs, a, "property_read_query_memsize");
    *(void **)&g_avs.create = proc(avs, a, "property_create");
    *(void **)&g_avs.insert_read = proc(avs, a, "property_insert_read");
    *(void **)&g_avs.search = proc(avs, a, "property_search");
    *(void **)&g_avs.node_refer = proc(avs, a, "property_node_refer");
    *(void **)&g_avs.node_remove = proc(avs, a, "property_node_remove");
    *(void **)&g_avs.node_create = proc(avs, a, "property_node_create");
    *(void **)&g_avs.setenv = proc(avs, a, "std_setenv");

    void *config = prop_read("prop\\avs-config.xml", 0x1000);
    void *configNode = node(config, "/config");
    if (!configNode)
        fail("prop\\avs-config.xml: no /config");
    set_int(config, "/config/sntp/ea_on", NODE_BOOL, 0);
    TIME_ZONE_INFORMATION tz;
    GetTimeZoneInformation(&tz);
    set_int(config, "/config/time/gmt_offset", NODE_S32, -tz.Bias * 60);
    void *heap = calloc(1, HEAP_SIZE), *heap2 = calloc(1, HEAP_SIZE);
    if (!heap || !heap2)
        fail("no memory for AVS's heaps");
    g_avs.boot(configNode, heap, HEAP_SIZE, heap2, HEAP_SIZE, log_writer, NULL);
    say("AVS booted");

    // 2. e-Amusement, the game
    HMODULE ea3 = load("libavs-win32-ea3.dll", 1);
    void(__cdecl * ea3_boot)(void *);
    void(__cdecl * ea3_shutdown)(void);
    *(void **)&ea3_boot = proc(ea3, "libavs-win32-ea3.dll", "ea3_boot");
    *(void **)&ea3_shutdown = proc(ea3, "libavs-win32-ea3.dll", "ea3_shutdown");
    HMODULE dll = load(game, 1);
    bool(__cdecl * entry_init)(char *, void *);
    bool(__cdecl * entry_main)(void);
    *(void **)&entry_init = proc(dll, game, "dll_entry_init");
    *(void **)&entry_main = proc(dll, game, "dll_entry_main");

    // 3. The identity
    static const char *ea3Files[] = {"prop\\ea3-config.xml", "prop\\ea3-cfg.xml", "prop\\eamuse-config.xml"};
    const char *ea3File = NULL;
    for (int i = 0; i < 3 && !ea3File; i++)
        if (exists(ea3Files[i]))
            ea3File = ea3Files[i];
    if (!ea3File)
        fail("no prop\\ea3-config.xml or prop\\eamuse-config.xml");
    void *ea = prop_read(ea3File, 0x40000);
    char soft[5][64];
    static const char *softKeys[5] = {"model", "dest", "spec", "rev", "ext"};
    for (int i = 0; i < 5; i++)
    {
        char path[64];
        snprintf(path, sizeof path, "/ea3/soft/%s", softKeys[i]);
        text(ea, path, soft[i], sizeof soft[i]);
    }
    if (!soft[0][0])
        fail("%s: no /ea3/soft/model", ea3File);
    // The ids: the file's, else defaults (windowsarcadeloader's)
    static const struct
    {
        const char *path, *def;
    } ids[4] = {{"/ea3/id/pcbid", "01201000000000010101"},
                {"/ea3/id/hardid", "0100DEADBEEF"},
                {"/ea3/id/softid", "012199999999"},
                {"/ea3/id/accountid", "012018008135"}};
    char id[4][64];
    for (int i = 0; i < 4; i++)
    {
        text(ea, ids[i].path, id[i], sizeof id[i]);
        if (!id[i][0])
        {
            snprintf(id[i], sizeof id[i], "%s", ids[i].def);
            set_text(ea, ids[i].path, id[i]);
        }
    }
    char security[128], softId[128];
    snprintf(security, sizeof security, "G*%s%s%s%s", soft[0], soft[1], soft[2], soft[3]);
    snprintf(softId, sizeof softId, "%s:%s:%s:%s:%s", soft[0], soft[1], soft[2], soft[3], soft[4]);
    const char *env[][2] = {
        {"/env/boot/build", "windowsloader"},
        {"/env/boot/version", "windowsloader"},
        {"/env/boot/tag", "windowsloader"},
        {"/env/profile/security_code", security},
        {"/env/profile/secplug_b_security_code", security},
        {"/env/profile/system_id", id[0]},
        {"/env/profile/hardware_id", id[1]},
        {"/env/profile/license_id", id[2]},
        {"/env/profile/software_id", id[2]},
        {"/env/profile/account_id", id[3]},
        {"/env/profile/soft_id_code", softId},
    };
    for (size_t i = 0; i < sizeof env / sizeof env[0]; i++)
        g_avs.setenv(env[i][0], env[i][1]);
    say("%s: soft id %s, PCB id %s", ea3File, softId, id[0]);

    // 4. The game's init
    void *app = exists("prop\\app-config.xml") ? prop_read("prop\\app-config.xml", 0x1000) : NULL;
    void *param = app ? node(app, "/param") : NULL;
    char code[128];
    snprintf(code, sizeof code, "%s%s%s%s%s", soft[0], soft[1], soft[2], soft[3], soft[4]);
    say("dll_entry_init(\"%s\")", code);
    if (!entry_init(code, param))
        say("dll_entry_init failed");

    // 5. e-Amusement, offline
    ea3_boot(node(ea, "/ea3"));
    say("e-Amusement booted, the game starts");

    // 6. The game
    bool ok = entry_main();
    say("dll_entry_main ended (%s)", ok ? "ok" : "failed");
    ea3_shutdown();
    g_avs.shutdown();
    return ok ? 0 : 1;
}
