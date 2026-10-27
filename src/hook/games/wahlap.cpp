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

// Storm Rider (Wahlap PC, 2013: Ntfs\all_test\game\Gold\motobike.exe, x86, Direct3D 9, OpenAL), a
// motorbike. Its cabinet's board is reached through motobikeio.dll, which the game, moto_coins.dll
// (the coins and the bookkeeping) and FirstMotoLightDLL.dll (the lights) import: its functions are
// windowsloader's (hooked in the library itself), the board is never opened. Read in motobikeio.dll
// (cdecl; a thread copies the board's reports into a buffer the functions read):
//   InitIoSys()               1 when the board is opened and its thread started
//   IsConnection()            1 once the board answers
//   GetIoKeyData()            a byte of buttons; GetIoKeyState(n) its bit n
//   GetControlButton()        the control panel's byte (the game keeps & 0x7F)
//   GetADData(n)              a 10-bit analog value, 0..0x3FF; channel 0 reversed by the library
//                             (0x3FF - the board's): the handlebar, 1 the throttle, 2 the brake
//   GetCoin(n)                coin slot n's counter; DecCoin(n, count) takes count off it,
//                             ClearCoin(n) empties it: 1 when done
//   ControlLightState(packet) the lights, a 16-byte packet sent as it is: 1 when done
//   MemoryRead(address, length, buffer) 5 when done, MemoryWrite(...) 1: the board's own memory
//                             (moto_coins.dll's bookkeeping), kept here in wl_motobikeio.bin
// The game packs its buttons in one word (+0x433720), as Storm Racer G (Wahlap's too): the
// control panel's byte as it is, and GetIoKeyData's bit 0 -> 0x200, 2 -> 0x100, 3 -> 0x20,
// 4 -> 0x40. Which button is which: Storm Racer G's, to be seen in the game's test menu.
//
// The game also checks a copy protection (a dongle bound to the PC's disk) that windowsloader does
// not handle: without it the game quits after loading.

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "log.h"
#include "outputs.h"

static int exists(const wchar_t *dir, const wchar_t *name)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s%s", dir, name);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

// ------------------------------------------------------------------------------
// The board (motobikeio.dll)
// ------------------------------------------------------------------------------

enum
{
    IN_STEERING,
    IN_GAS,
    IN_BRAKE,
    IN_COIN,
    IN_TEST,
    IN_SERVICE,
    IN_START,
    IN_BOOST,
    IN_VIEW1,
    IN_VIEW2,
    IN_UP,
    IN_DOWN,
    IN_COUNT
};

static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 0x3FF, 0x200},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 0x3FF, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 0x3FF, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "P1_Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"Boost", "Nitro", WL_INPUT_SWITCH, 0, 1, 0},
    {"View1", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"View2", "P1_Right", WL_INPUT_SWITCH, 0, 1, 0},
    {"Up", "P1_Up", WL_INPUT_SWITCH, 0, 1, 0},
    {"Down", "P1_Down", WL_INPUT_SWITCH, 0, 1, 0},
};

// Each button: 1 in the control panel's byte, 0 in GetIoKeyData's; its bit there (the game's own
// bit in the comment)
static const struct
{
    int input, panel;
    unsigned bit;
} g_bits[] = {
    {IN_START, 1, 0x02},   // 0x002
    {IN_BOOST, 1, 0x04},   // 0x004
    {IN_VIEW1, 1, 0x20},   // 0x020
    {IN_UP, 1, 0x08},      // 0x008
    {IN_DOWN, 1, 0x10},    // 0x010
    {IN_VIEW2, 1, 0x40},   // 0x040
    {IN_TEST, 0, 0x04},    // 0x100
    {IN_SERVICE, 0, 0x01}, // 0x200
};

#define COIN_SLOTS 4
static volatile LONG g_coins[COIN_SLOTS];
static int g_coinWas;

static unsigned buttons(int panel)
{
    unsigned b = 0;
    for (size_t i = 0; i < sizeof g_bits / sizeof g_bits[0]; i++)
        if (g_bits[i].panel == panel && wl_input_get(&g_inputs[g_bits[i].input]))
            b |= g_bits[i].bit;
    return b;
}

// A coin each time the button goes down, on slot 0
static void coins_poll(void)
{
    int coin = wl_input_get(&g_inputs[IN_COIN]) != 0;
    if (coin && !g_coinWas)
        InterlockedIncrement(&g_coins[0]);
    g_coinWas = coin;
}

static char(__cdecl *oInitIoSys)(void);
static char __cdecl hInitIoSys(void)
{
    wl_log("motobikeio: InitIoSys (the board played by windowsloader)");
    return 1;
}

static void(__cdecl *oExitIoSys)(void);
static void __cdecl hExitIoSys(void)
{
}

static void(__cdecl *oResetDevice)(void);
static void __cdecl hResetDevice(void)
{
}

static char(__cdecl *oIsConnection)(void);
static char __cdecl hIsConnection(void)
{
    return 1;
}

static int(__cdecl *oGetIoKeyData)(void);
static int __cdecl hGetIoKeyData(void)
{
    coins_poll();
    return (int)(signed char)buttons(0);
}

static int(__cdecl *oGetIoKeyState)(int);
static int __cdecl hGetIoKeyState(int bit)
{
    return bit >= 0 && bit < 8 && (buttons(0) >> bit & 1);
}

static char(__cdecl *oGetControlButton)(void);
static char __cdecl hGetControlButton(void)
{
    coins_poll();
    return (char)buttons(1);
}

static char(__cdecl *oGetPasswordButton)(void);
static char __cdecl hGetPasswordButton(void)
{
    return 0;
}

// The value the library returns: channel 0 is already the reversed one (the game's handlebar)
static int(__cdecl *oGetADData)(int);
static int __cdecl hGetADData(int channel)
{
    static const int inputs[3] = {IN_STEERING, IN_GAS, IN_BRAKE};
    if (channel < 0 || channel > 2)
        return 0;
    return wl_input_get(&g_inputs[inputs[channel]]) & 0x3FF;
}

static int(__cdecl *oGetCoin)(int);
static int __cdecl hGetCoin(int slot)
{
    coins_poll();
    return slot >= 0 && slot < COIN_SLOTS ? (int)(signed char)g_coins[slot] : 0;
}

static int(__cdecl *oDecCoin)(int, int);
static int __cdecl hDecCoin(int slot, int count)
{
    if (slot >= 0 && slot < COIN_SLOTS)
    {
        LONG left = g_coins[slot] - count;
        InterlockedExchange(&g_coins[slot], left > 0 ? left : 0);
    }
    return 1;
}

static int(__cdecl *oClearCoin)(int);
static int __cdecl hClearCoin(int slot)
{
    if (slot >= 0 && slot < COIN_SLOTS)
        InterlockedExchange(&g_coins[slot], 0);
    return 1;
}

// The lights' packets: logged as they change, to be decoded
static int(__cdecl *oControlLightState)(const unsigned char *);
static int __cdecl hControlLightState(const unsigned char *packet)
{
    static unsigned char last[16];
    static int logged;
    if (packet && memcmp(last, packet, 16))
    {
        memcpy(last, packet, 16);
        if (logged++ < 200)
            wl_log_hex("motobikeio: lights", packet, 16);
    }
    return 1;
}

// The board's memory, 64 KB, kept in wl_motobikeio.bin beside the game (or in its write folder)
#define BOARD_MEMORY 0x10000
static unsigned char g_memory[BOARD_MEMORY];
static wchar_t g_memoryFile[MAX_PATH];
static CRITICAL_SECTION g_memoryLock;

static void memory_load(const wchar_t *dir)
{
    InitializeCriticalSection(&g_memoryLock);
    wchar_t path[MAX_PATH], buf[MAX_PATH];
    swprintf(path, MAX_PATH, L"%swl_motobikeio.bin", dir);
    wcsncpy_s(g_memoryFile, MAX_PATH, wl_files_redirectedW(path, buf, MAX_PATH), _TRUNCATE);
    wl_busy_enter();
    HANDLE f = CreateFileW(g_memoryFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE)
    {
        DWORD n = 0;
        ReadFile(f, g_memory, BOARD_MEMORY, &n, NULL);
        CloseHandle(f);
    }
    wl_busy_leave();
}

static void memory_save(void)
{
    wl_busy_enter();
    HANDLE f = CreateFileW(g_memoryFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE)
    {
        DWORD n = 0;
        WriteFile(f, g_memory, BOARD_MEMORY, &n, NULL);
        CloseHandle(f);
    }
    wl_busy_leave();
}

static int(__cdecl *oMemoryRead)(unsigned short, unsigned short, unsigned char *);
static int __cdecl hMemoryRead(unsigned short address, unsigned short length, unsigned char *buffer)
{
    static int logged;
    if (logged++ < 20)
        wl_log("motobikeio: MemoryRead 0x%04x, %u bytes", address, length);
    if (!buffer)
        return 0;
    EnterCriticalSection(&g_memoryLock);
    for (unsigned i = 0; i < length; i++)
        buffer[i] = g_memory[(address + i) & (BOARD_MEMORY - 1)];
    LeaveCriticalSection(&g_memoryLock);
    return 5;
}

static int(__cdecl *oMemoryWrite)(unsigned short, unsigned short, const unsigned char *);
static int __cdecl hMemoryWrite(unsigned short address, unsigned short length, const unsigned char *buffer)
{
    static int logged;
    if (logged++ < 20)
        wl_log("motobikeio: MemoryWrite 0x%04x, %u bytes", address, length);
    if (!buffer)
        return 0;
    EnterCriticalSection(&g_memoryLock);
    for (unsigned i = 0; i < length; i++)
        g_memory[(address + i) & (BOARD_MEMORY - 1)] = buffer[i];
    memory_save();
    LeaveCriticalSection(&g_memoryLock);
    return 1;
}

static WlHook g_ioHooks[] = {
    WL_HOOK(L"motobikeio.dll", InitIoSys),        WL_HOOK(L"motobikeio.dll", ExitIoSys),        WL_HOOK(L"motobikeio.dll", ResetDevice),
    WL_HOOK(L"motobikeio.dll", IsConnection),     WL_HOOK(L"motobikeio.dll", GetIoKeyData),     WL_HOOK(L"motobikeio.dll", GetIoKeyState),
    WL_HOOK(L"motobikeio.dll", GetControlButton), WL_HOOK(L"motobikeio.dll", GetPasswordButton), WL_HOOK(L"motobikeio.dll", GetADData),
    WL_HOOK(L"motobikeio.dll", GetCoin),          WL_HOOK(L"motobikeio.dll", DecCoin),          WL_HOOK(L"motobikeio.dll", ClearCoin),
    WL_HOOK(L"motobikeio.dll", ControlLightState), WL_HOOK(L"motobikeio.dll", MemoryRead),      WL_HOOK(L"motobikeio.dll", MemoryWrite),
};

// ------------------------------------------------------------------------------

int wl_game_wahlap(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    wchar_t dir[MAX_PATH];
    wcsncpy_s(dir, MAX_PATH, exePath, base - exePath);
    if (_wcsicmp(base, L"motobike.exe") || !exists(dir, L"motobikeio.dll"))
        return 0;
    *name = "stormrider";
    *title = "Storm Rider";
    wl_log("game: %s (Wahlap PC)", *title);
    wl_game_set_boards("Controls on Wahlap's board (through motobikeio.dll)", "Motor board: none");
    wl_inputs_set_table(g_inputs, IN_COUNT);
    memory_load(dir);
    wl_hooks_add(g_ioHooks, sizeof g_ioHooks / sizeof g_ioHooks[0]);
    return 1;
}
