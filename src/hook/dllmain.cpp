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

// windowsloader32.dll / windowsloader64.dll, loaded by the launcher into the suspended game.
// DllMain runs before the game's own code: the hooks are in place when its main thread starts.

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "agent.h"
#include "controls.h"
#include "display.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "pause.h"
#include "settings.h"
#include "vdev.h"
#include "wlshared.h"

static int env_int(const wchar_t *name, int def)
{
    wchar_t v[32];
    if (!GetEnvironmentVariableW(name, v, 32))
        return def;
    return _wtoi(v);
}

static void wl_init(void)
{
    wchar_t logPath[MAX_PATH] = L"";
    if (!GetEnvironmentVariableW(WL_ENV_LOG, logPath, MAX_PATH))
    {
        // Loaded without the launcher: log next to the DLL
        GetModuleFileNameW(g_wlModule, logPath, MAX_PATH);
        wchar_t *slash = wcsrchr(logPath, L'\\');
        if (slash)
            wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - logPath), L"windowsloader.log");
    }
    wl_log_open(logPath);

    g_wl.windowed = env_int(WL_ENV_WINDOWED, 1) != 0;
    g_wl.ioLimit = env_int(WL_ENV_IOLIMIT, 500);

    wchar_t exe[MAX_PATH], dir[MAX_PATH];
    char u1[MAX_PATH * 2], u2[MAX_PATH * 2];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    GetCurrentDirectoryW(MAX_PATH, dir);
    wl_log("windowsloader " WL_VERSION " (%d-bit) in %s (pid %lu)", (int)sizeof(void *) * 8, wl_u8(exe, u1, sizeof u1), GetCurrentProcessId());
    wl_log("command line: %s", GetCommandLineA());
    wl_log("folder: %s", wl_u8(dir, u2, sizeof u2));
    wl_log("windowed: %s, device I/O logged: %d per handle", g_wl.windowed ? "yes" : "no", g_wl.ioLimit);

    wl_crash_init();
    wl_hooks_init();
    wl_files_hooks();
    wl_devices_hooks();
    wl_vdev_hooks();
    wl_system_hooks();
    wl_window_hooks();
    wl_d3d8_hooks();
    wl_d3d9_hooks();
    wl_dinput_hooks();
    wl_d3d11_hooks();
    wl_gl_hooks();
    wl_game_init();
    // The settings once the game is known: its own <short name>.ini is read instead when there is one
    wl_settings_init();
    wl_game_settings();
    wl_vdev_settings();
    wl_display_load();
    wl_pause_init();
    wl_hooks_install_pending();
    wl_agent_init();

    // The controllers, unless the agent drives the game alone (with --controllers it holds the inputs
    // it sets, the controllers the others, and the force feedback reaches the wheel)
    wchar_t controls[MAX_PATH];
    if (GetEnvironmentVariableW(WL_ENV_CONTROLS, controls, MAX_PATH))
    {
        if (env_int(WL_ENV_AGENT, 0) == 1 && env_int(WL_ENV_CONTROLLERS, 0) != 1)
            wl_controls_load(controls);
        else
            wl_controls_start(controls);
    }
    wl_log("ready, starting the game");
}

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_wlModule = inst;
        wl_init();
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        wl_log("unloading%s", reserved ? " (process exit)" : "");
        wl_log_close();
    }
    return TRUE;
}
