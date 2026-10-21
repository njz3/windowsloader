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

#include <windows.h>
#include <intrin.h>

// ------------------------------------------------------------------------------
// Settings read from the environment set by the launcher (see wlshared.h)
// ------------------------------------------------------------------------------

typedef struct
{
    int windowed; // keep the game in a window and leave the desktop alone (cursor, display mode, global hooks)
    int ioLimit;  // max logged reads/writes per device handle
} WlSettings;

extern WlSettings g_wl;
extern HMODULE g_wlModule;

// ------------------------------------------------------------------------------
// API hooks (MinHook). Each hook file declares a table; a hook whose module is not loaded yet
// is installed by wl_hooks_install_pending(), which the LoadLibrary hooks call after each load.
// ------------------------------------------------------------------------------

typedef struct
{
    const wchar_t *module; // L"kernel32.dll"
    const char *name;      // "CreateFileA"
    void *detour;
    void **original;
    int installed;
} WlHook;

#define WL_HOOK(module, name) {module, #name, (void *)h##name, (void **)&o##name, 0}

void wl_hooks_init(void);
void wl_hooks_add(WlHook *hooks, int count);
void wl_hooks_install_pending(void);

// Hook any code address (COM vtable entries...), returns 1 on success
int wl_hook_code(void *target, void *detour, void **original, const char *what);

// Replaces one entry of a module's import table: only that module's own calls change, unlike the
// MinHook hooks (the clocks of the paused game, while Windows and windowsloader keep the real time).
// Returns 1 when the import was found.
int wl_patch_iat(HMODULE mod, const char *dll, const char *func, void *replacement, void **original);

// Re-entrancy guard, per thread. A hook calls the original function inside wl_busy_enter/leave:
// whatever that call does internally (CreateFileA -> CreateFileW, our logger's WriteFile...)
// then goes through the other hooks untouched, so nothing is logged twice.
int wl_busy(void);
void wl_busy_enter(void);
void wl_busy_leave(void);

// Address of the code that called the hook
#define WL_CALLER _ReturnAddress()

// "Rally.exe+0x1234"
const char *wl_where(const void *addr, char *buf, size_t size);

// True for a call made by a module outside the game's folder (Windows, drivers) or by
// windowsloader itself: hooks only report and change what the game and its own DLLs do.
int wl_is_system_caller(const void *addr);

// Windowed mode: gives the game's window a frame and a client area of the picture's aspect ratio
void wl_window_fit(HWND wnd, int width, int height);

// Per-module hook tables
void wl_files_hooks(void);
void wl_devices_hooks(void);
void wl_system_hooks(void);
void wl_window_hooks(void);
void wl_d3d9_hooks(void);
void wl_dinput_hooks(void);
void wl_crash_init(void);
