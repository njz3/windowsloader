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
// func may be an ordinal, MAKEINTRESOURCEA(n), for a function imported by its number (Wangan
// Midnight's HASP library). Returns 1 when the import was found.
int wl_patch_iat(HMODULE mod, const char *dll, const char *func, void *replacement, void **original);

// Bytes of the game's code replaced, only when they are the ones expected (the game's version the
// change was made for); logged with what. Returns 1 when done.
int wl_patch_code(void *address, const void *expected, const void *bytes, size_t length, const char *what);

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

// The game's own .ini reads changed by its module (Sega Rally 3's render resolution, Daytona's
// camera and network): returns 1 with the value to give the game instead of its file's, as text
// (GetPrivateProfileInt gives its number)
typedef int (*WlIniOverride)(const char *file, const char *section, const char *key, char *value, size_t size);
void wl_files_set_ini_override(WlIniOverride fn);

// A log file of the game written up to maxBytes per opening, the rest dropped (the file whose
// path ends with nameEnd)
void wl_files_cap(const char *nameEnd, unsigned long long maxBytes);

// A folder the game uses at a fixed place ("C:\Sega", GRID's) taken from another one (the copy
// beside the game): the game's paths under from go to the same paths under to, so nothing is
// written at the fixed place on this PC. Paths are compared without regard to case or / and \.
void wl_files_redirect(const char *from, const char *to);
// The same, the path's beginning replaced as text: "F:\" to "<folder>\F" makes F:\system\x
// <folder>\Fsystem\x (Mario Kart Arcade GP DX's drives, as its cabinets' copies keep them)
void wl_files_redirect_prefix(const char *from, const char *to);
// The path under its redirected folder (written in buf), or the path itself: for the files Windows
// opens for the game from its own DLLs, which the file hooks do not see (DirectShow's movies)
const wchar_t *wl_files_redirectedW(const wchar_t *path, wchar_t *buf, size_t size);
// The write folder (WINDOWSLOADER_WRITE_DIR, the launcher's --write-dir or [Paths] WRITE_DIR):
// WRITE_DIR\<game> made, the game's writes sent there from now on. Once the game and the settings
// are known
void wl_files_write_dir(void);
// The write folder for the settings menu: 1 in use (*folder WRITE_DIR\<game>), 0 none (*folder L""),
// -1 asked but it cannot be written (*folder the one that cannot: nothing is written, anywhere)
int wl_files_write_state(const wchar_t **folder);
// windowsloader's own file the agent asked for (a picture), written even then: on around it, this
// thread only
void wl_files_allow_writes(int on);
// A write folder that cannot be written: 1 when a write of path is refused (outside it), for
// windowsloader's own writes made before the file hooks are on (the settings' defaults)
int wl_files_write_refused(const wchar_t *path);
// Then the files a game cannot run without, by their names' wildcards (";" between them,
// "*BK_*.bin"): written to working copies in Windows' temporary folder, removed at the end
void wl_files_temp_writes(const char *names);
// A path with a folder as the write folder has it for reading: its copy there (in buf) when there
// is one, else the path itself (LoadLibrary: a DLL the game wrote, in the temporary folder say)
const wchar_t *wl_files_read_path(const wchar_t *path, wchar_t *buf, size_t size);
// The same for a file windowsloader keeps for the game and modifies (a board's memory): its copy in
// the write folder, made from the first one if needed, or the path itself without a write folder
// (buf of MAX_PATH * 2: past MAX_PATH, the \\?\ form)
const wchar_t *wl_files_write_path(const wchar_t *path, wchar_t *buf, size_t size);

// A program the game starts that windowsloader plays itself (Daytona's InpWrapper.exe, its
// parallel port helper): returns 1 when the module took it over. The game is still told the
// program did not start; nothing reaches Windows.
typedef int (*WlProcessHandler)(const char *app, const char *cmd);
void wl_system_set_process_handler(WlProcessHandler fn);

// A DLL the game loads that windowsloader refuses (by its file name, any folder): LoadLibrary
// fails as for a missing file (DiRT Showdown's Rapture3D audio driver, which wants settings in the
// PC's registry: the game's OpenAL then takes its generic DirectSound driver)
void wl_system_refuse_dll(const char *name);

// A DLL windowsloader plays itself, by its file name (an ALLS's display driver, "$amvideo" in
// amdaemon's registry): LoadLibrary gives a handle of ours, GetProcAddress these functions, by
// name or by ordinal
struct WlVirtualDllFunction
{
    const char *name;
    unsigned ordinal;
    void *fn;
};
void wl_system_virtual_dll(const char *name, const WlVirtualDllFunction *functions, int count);

// The cabinet's network card in place of the PC's (netenv.cpp): one Ethernet card at ip, 24-bit
// subnet, its gateway its DNS server, pings answered (an ALLS's amdaemon checks the network so)
void wl_netenv_fake(unsigned ip, unsigned gateway, const unsigned char mac[6]);
int wl_netenv_fake_ip(unsigned *ip); // that card's address (network order), 0 when it is the PC's

// The game's broadcasts (x.x.x.255) sent to 127.0.0.1 instead, the same port: a game whose cabinets
// elect a host by broadcast hears itself and becomes the host, nothing leaves the PC
void wl_system_loopback_broadcast(int on);

// The PC's cameras given to the game, as [Emulation] CAMERA = true does (a module whose link needs
// them: Daytona Championship USA's)
void wl_system_allow_camera(const char *why);
// 1 when the game may have the PC's cameras (for windowsloader's own calls, which system.cpp lets by)
int wl_system_camera_allowed(void);

// The game's debug messages (OutputDebugString) holding one of these strings are not logged (a
// game's noise): a list ended by NULL, kept by the module
void wl_system_quiet_debug(const char *const *strings);

// A registry key of the cabinet's made up under HKEY_LOCAL_MACHINE (an ALLS's
// "SYSTEM\SEGA\SystemProperty\static"): opened, read and written by the game in memory only.
// REG_SZ data is an 8-bit string (size ignored); the others are size bytes.
struct WlRegValue
{
    const char *name;
    DWORD type;
    const void *data;
    DWORD size;
};
void wl_registry_add_key(const char *path, const WlRegValue *values, int count);

// Per-module hook tables
void wl_files_hooks(void);
void wl_devices_hooks(void);
void wl_system_hooks(void);
void wl_netenv_hooks(void);
void wl_window_hooks(void);
void wl_d3d8_hooks(void);
void wl_d3d9_hooks(void);
void wl_dinput_hooks(void);
void wl_dinput_hide_controllers(void); // the PC's game controllers kept from the game's DirectInput
void wl_dinput_legacy(void *directInputA); // DirectInput 7 (ANSI) made through COM: its devices handled the same way
void wl_d3d11_hooks(void);
void wl_gl_hooks(void);
void wl_gl_picture(int width, int height); // from a game's module: its OpenGL picture's size, in the lower left corner of its window
// The settings menu (menu.cpp) over an OpenGL game drawing in windowsloader's framebuffer: 1 when
// the current context does; wl_gl_present then draws the game's last picture in the window
int wl_gl_redirected(void);
int wl_gl_present(void);
void wl_crash_init(void);
