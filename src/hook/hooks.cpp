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
#include <stdio.h>

#include "MinHook.h"
#include "hooks.h"
#include "log.h"

WlSettings g_wl = {1, 500};
HMODULE g_wlModule;

// ------------------------------------------------------------------------------
// Re-entrancy guard
// ------------------------------------------------------------------------------

static __declspec(thread) int t_busy;

int wl_busy(void)
{
    return t_busy;
}

void wl_busy_enter(void)
{
    t_busy++;
}

void wl_busy_leave(void)
{
    t_busy--;
}

// ------------------------------------------------------------------------------
// Hook tables
// ------------------------------------------------------------------------------

typedef struct
{
    WlHook *hooks;
    int count;
} HookTable;

static HookTable g_tables[32];
static int g_tableCount;
static CRITICAL_SECTION g_hookLock;
static int g_pending; // hooks not installed yet

void wl_hooks_init(void)
{
    InitializeCriticalSection(&g_hookLock);
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK)
        wl_log("MinHook: MH_Initialize failed (%s)", MH_StatusToString(st));
}

void wl_hooks_add(WlHook *hooks, int count)
{
    EnterCriticalSection(&g_hookLock);
    if (g_tableCount < (int)(sizeof g_tables / sizeof g_tables[0]))
    {
        g_tables[g_tableCount].hooks = hooks;
        g_tables[g_tableCount].count = count;
        g_tableCount++;
        g_pending += count;
    }
    LeaveCriticalSection(&g_hookLock);
}

// Creates the hook and queues its activation: MH_ApplyQueued then freezes the other threads
// once for all of them (about 100 ms each time).
static int queue_one(void *target, void *detour, void **original, const char *what)
{
    char where[128];
    MH_STATUS st = MH_CreateHook(target, detour, original);
    if (st == MH_OK)
        st = MH_QueueEnableHook(target);
    if (st != MH_OK)
    {
        wl_log("hook %s at %s: %s", what, wl_where(target, where, sizeof where), MH_StatusToString(st));
        return 0;
    }
    wl_log("hook %s at %s", what, wl_where(target, where, sizeof where));
    return 1;
}

static void apply_queued(int queued)
{
    if (!queued)
        return;
    MH_STATUS st = MH_ApplyQueued();
    if (st != MH_OK)
        wl_log("MinHook: enabling %d hooks failed (%s)", queued, MH_StatusToString(st));
}

void wl_hooks_install_pending(void)
{
    if (!g_pending)
        return;
    wl_busy_enter();
    EnterCriticalSection(&g_hookLock);
    int queued = 0;
    for (int t = 0; t < g_tableCount; t++)
    {
        for (int i = 0; i < g_tables[t].count; i++)
        {
            WlHook *h = &g_tables[t].hooks[i];
            if (h->installed)
                continue;
            HMODULE mod = GetModuleHandleW(h->module);
            if (!mod)
                continue;
            h->installed = 1;
            g_pending--;
            void *target = (void *)GetProcAddress(mod, h->name);
            char what[160], name[64];
            snprintf(what, sizeof what, "%s!%s", wl_u8(h->module, name, sizeof name), h->name);
            if (!target)
            {
                wl_log("hook %s: not exported", what);
                continue;
            }
            queued += queue_one(target, h->detour, h->original, what);
        }
    }
    apply_queued(queued);
    LeaveCriticalSection(&g_hookLock);
    wl_busy_leave();
}

int wl_hook_code(void *target, void *detour, void **original, const char *what)
{
    wl_busy_enter();
    EnterCriticalSection(&g_hookLock);
    int ok = queue_one(target, detour, original, what);
    apply_queued(ok);
    LeaveCriticalSection(&g_hookLock);
    wl_busy_leave();
    return ok;
}

int wl_patch_iat(HMODULE mod, const char *dll, const char *func, void *replacement, void **original)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress)
        return 0;
    // Without the table of names (an executable rebuilt from a memory dump), the loader has already
    // written the functions' addresses over the only table: the entry is the one holding the function
    HMODULE dllMod = GetModuleHandleA(dll);
    ULONG_PTR real = dllMod ? (ULONG_PTR)GetProcAddress(dllMod, func) : 0;
    int byOrdinal = IS_INTRESOURCE(func);
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++)
    {
        if (_stricmp((const char *)(base + d->Name), dll))
            continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++)
        {
            if (!d->OriginalFirstThunk && names->u1.AddressOfData >= nt->OptionalHeader.SizeOfImage)
            {
                if (!real || iat->u1.Function != real)
                    continue;
            }
            else if (byOrdinal ? !IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal) || IMAGE_ORDINAL(names->u1.Ordinal) != (ULONG_PTR)func
                               : IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal) ||
                                     strcmp((const char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name, func))
                continue;
            DWORD old;
            VirtualProtect(&iat->u1.Function, sizeof iat->u1.Function, PAGE_READWRITE, &old);
            if (original)
                *original = (void *)iat->u1.Function;
            iat->u1.Function = (ULONG_PTR)replacement;
            VirtualProtect(&iat->u1.Function, sizeof iat->u1.Function, old, &old);
            return 1;
        }
    }
    return 0;
}

int wl_patch_code(void *address, const void *expected, const void *bytes, size_t length, const char *what)
{
    char where[128];
    if (memcmp(address, expected, length))
    {
        wl_log("patch %s at %s: not the bytes expected (another version of the game?), left as it is", what, wl_where(address, where, sizeof where));
        return 0;
    }
    DWORD old;
    if (!VirtualProtect(address, length, PAGE_EXECUTE_READWRITE, &old))
        return 0;
    memcpy(address, bytes, length);
    VirtualProtect(address, length, old, &old);
    FlushInstructionCache(GetCurrentProcess(), address, length);
    wl_log("patch %s at %s", what, wl_where(address, where, sizeof where));
    return 1;
}

// ------------------------------------------------------------------------------
// Callers
// ------------------------------------------------------------------------------

const char *wl_where(const void *addr, char *buf, size_t size)
{
    HMODULE mod;
    wchar_t path[MAX_PATH];
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &mod) ||
        !GetModuleFileNameW(mod, path, MAX_PATH))
    {
        snprintf(buf, size, "%p", addr);
        return buf;
    }
    const wchar_t *base = wcsrchr(path, L'\\');
    char name[MAX_PATH];
    wl_u8(base ? base + 1 : path, name, sizeof name);
    snprintf(buf, size, "%s+0x%Ix", name, (size_t)((const char *)addr - (const char *)mod));
    return buf;
}

typedef struct
{
    HMODULE mod;
    int system;
} ModuleKind;

static ModuleKind g_modules[256];
static int g_moduleCount;
static SRWLOCK g_moduleLock = SRWLOCK_INIT;

int wl_is_system_caller(const void *addr)
{
    HMODULE mod;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &mod))
        return 0; // code outside any module: unpacked or generated by the game
    if (mod == g_wlModule)
        return 1;

    int kind = -1;
    AcquireSRWLockShared(&g_moduleLock);
    for (int i = 0; i < g_moduleCount; i++)
        if (g_modules[i].mod == mod)
        {
            kind = g_modules[i].system;
            break;
        }
    ReleaseSRWLockShared(&g_moduleLock);
    if (kind >= 0)
        return kind;

    // The game's modules are the exe and the DLLs of its folder; graphics drivers, antivirus
    // and the rest of Windows are elsewhere
    static wchar_t gameDir[MAX_PATH];
    static size_t gameDirLen;
    wchar_t path[MAX_PATH];
    if (!gameDirLen && GetModuleFileNameW(NULL, gameDir, MAX_PATH))
    {
        wchar_t *slash = wcsrchr(gameDir, L'\\');
        if (slash)
            slash[1] = 0;
        gameDirLen = wcslen(gameDir);
    }
    kind = !(GetModuleFileNameW(mod, path, MAX_PATH) && gameDirLen && !_wcsnicmp(path, gameDir, gameDirLen));

    // Microsoft redistributables shipped next to the game (XInput polls SetupAPI all the time)
    static const wchar_t *redist[] = {L"xinput", L"d3dx", L"d3dcompiler", L"dbghelp", L"msvc", L"vcruntime", L"xaudio", L"x3daudio", L"xapofx"};
    if (!kind)
    {
        const wchar_t *base = wcsrchr(path, L'\\');
        base = base ? base + 1 : path;
        for (int i = 0; i < (int)(sizeof redist / sizeof redist[0]) && !kind; i++)
            kind = !_wcsnicmp(base, redist[i], wcslen(redist[i]));
    }

    AcquireSRWLockExclusive(&g_moduleLock);
    if (g_moduleCount < (int)(sizeof g_modules / sizeof g_modules[0]))
    {
        g_modules[g_moduleCount].mod = mod;
        g_modules[g_moduleCount].system = kind;
        g_moduleCount++;
    }
    ReleaseSRWLockExclusive(&g_moduleLock);
    return kind;
}
