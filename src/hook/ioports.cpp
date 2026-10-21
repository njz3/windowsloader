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
#include <string.h>

#include "hooks.h"
#include "ioports.h"
#include "log.h"

static WlPortWriter g_writer;

void wl_ioports_set_writer(WlPortWriter writer)
{
    g_writer = writer;
}

static void port_write(unsigned short port, unsigned short value)
{
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 40)
        wl_log("io: out 0x%04x <- 0x%02x", port, value);
    if (g_writer)
        g_writer(port, value);
}

// inpout32's exports, with its calling convention (__stdcall)
static void WINAPI wl_Out32(short port, short value)
{
    port_write((unsigned short)port, (unsigned short)value);
}

static short WINAPI wl_Inp32(short port)
{
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 20)
        wl_log("io: in 0x%04x -> 0", (unsigned short)port);
    return 0;
}

static BOOL WINAPI wl_IsInpOutDriverOpen(void)
{
    return TRUE;
}

static UCHAR WINAPI wl_DlPortReadPortUchar(USHORT port)
{
    return (UCHAR)wl_Inp32((short)port);
}

static void WINAPI wl_DlPortWritePortUchar(USHORT port, UCHAR value)
{
    port_write(port, value);
}

static USHORT WINAPI wl_DlPortReadPortUshort(USHORT port)
{
    return (USHORT)wl_Inp32((short)port);
}

static void WINAPI wl_DlPortWritePortUshort(USHORT port, USHORT value)
{
    port_write(port, value);
}

static ULONG WINAPI wl_DlPortReadPortUlong(ULONG port)
{
    return (USHORT)wl_Inp32((short)port);
}

static void WINAPI wl_DlPortWritePortUlong(ULONG port, ULONG value)
{
    port_write((unsigned short)port, (unsigned short)value);
}

int wl_ioports_is_driver_dll(const char *name)
{
    if (!name)
        return 0;
    const char *base = strrchr(name, '\\');
    const char *slash = strrchr(name, '/');
    if (slash && (!base || slash > base))
        base = slash;
    base = base ? base + 1 : name;
    return !_stricmp(base, "inpout32.dll") || !_stricmp(base, "inpout32") || !_stricmp(base, "inpout32_d.dll") || !_stricmp(base, "inpoutx64.dll");
}

// windowsloader's own module, one more reference taken so that the game's FreeLibrary is balanced
HMODULE wl_ioports_module(void)
{
    HMODULE mod = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)(void *)wl_Out32, &mod);
    return mod;
}

FARPROC wl_ioports_proc(HMODULE mod, LPCSTR name)
{
    static const struct
    {
        const char *name;
        FARPROC proc;
    } procs[] = {
        {"Out32", (FARPROC)wl_Out32},
        {"Inp32", (FARPROC)wl_Inp32},
        {"IsInpOutDriverOpen", (FARPROC)wl_IsInpOutDriverOpen},
        {"DlPortReadPortUchar", (FARPROC)wl_DlPortReadPortUchar},
        {"DlPortWritePortUchar", (FARPROC)wl_DlPortWritePortUchar},
        {"DlPortReadPortUshort", (FARPROC)wl_DlPortReadPortUshort},
        {"DlPortWritePortUshort", (FARPROC)wl_DlPortWritePortUshort},
        {"DlPortReadPortUlong", (FARPROC)wl_DlPortReadPortUlong},
        {"DlPortWritePortUlong", (FARPROC)wl_DlPortWritePortUlong},
    };
    if (mod != g_wlModule || !name || IS_INTRESOURCE(name))
        return NULL;
    for (int i = 0; i < (int)(sizeof procs / sizeof procs[0]); i++)
        if (!strcmp(name, procs[i].name))
            return procs[i].proc;
    return NULL;
}
