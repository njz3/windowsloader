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
#include <mmdeviceapi.h>
#include <audiopolicy.h>

#include "hooks.h"
#include "log.h"
#include "pause.h"

// ------------------------------------------------------------------------------
// Clocks. While paused the game reads the time the pause began; afterwards the real time minus
// all the time spent paused, so its clocks go on from where they stopped.
// ------------------------------------------------------------------------------

static volatile LONG g_paused;
static volatile LONGLONG g_qpcPaused, g_qpcFrozen, g_qpcStart; // counter ticks
static volatile LONGLONG g_msPaused;                           // milliseconds
static volatile LONG g_tickFrozen, g_tgtFrozen;
static LARGE_INTEGER g_freq;

static BOOL(WINAPI *oQueryPerformanceCounter)(LARGE_INTEGER *);
static DWORD(WINAPI *oGetTickCount)(void);
static DWORD(WINAPI *oTimeGetTime)(void);

static LONGLONG read64(volatile LONGLONG *v)
{
    return InterlockedCompareExchange64(v, 0, 0);
}

static BOOL WINAPI game_QueryPerformanceCounter(LARGE_INTEGER *c)
{
    BOOL ok = oQueryPerformanceCounter(c);
    if (ok && c)
        c->QuadPart = g_paused ? read64(&g_qpcFrozen) : c->QuadPart - read64(&g_qpcPaused);
    return ok;
}

static DWORD WINAPI game_GetTickCount(void)
{
    return g_paused ? (DWORD)g_tickFrozen : oGetTickCount() - (DWORD)read64(&g_msPaused);
}

static DWORD WINAPI game_timeGetTime(void)
{
    return g_paused ? (DWORD)g_tgtFrozen : oTimeGetTime() - (DWORD)read64(&g_msPaused);
}

void wl_pause_init(void)
{
    QueryPerformanceFrequency(&g_freq);
    HMODULE game = GetModuleHandleW(NULL);
    int qpc = wl_patch_iat(game, "KERNEL32.dll", "QueryPerformanceCounter", (void *)game_QueryPerformanceCounter, (void **)&oQueryPerformanceCounter);
    int tick = wl_patch_iat(game, "KERNEL32.dll", "GetTickCount", (void *)game_GetTickCount, (void **)&oGetTickCount);
    int tgt = wl_patch_iat(game, "WINMM.dll", "timeGetTime", (void *)game_timeGetTime, (void **)&oTimeGetTime);
    // The pause needs the real functions even where the game does not import them
    if (!oQueryPerformanceCounter)
        oQueryPerformanceCounter = QueryPerformanceCounter;
    if (!oGetTickCount)
        oGetTickCount = GetTickCount;
    if (!oTimeGetTime)
    {
        HMODULE winmm = LoadLibraryW(L"winmm.dll");
        oTimeGetTime = winmm ? (DWORD(WINAPI *)(void))GetProcAddress(winmm, "timeGetTime") : NULL;
        if (!oTimeGetTime)
            oTimeGetTime = GetTickCount;
    }
    wl_log("pause: game clocks replaced: QueryPerformanceCounter %s, GetTickCount %s, timeGetTime %s", qpc ? "yes" : "not imported",
           tick ? "yes" : "not imported", tgt ? "yes" : "not imported");
}

// ------------------------------------------------------------------------------
// Sound: the game's audio sessions on the default output, muted. Done in a thread of its own,
// which can enter COM's multithreaded apartment whatever the game's thread did.
// ------------------------------------------------------------------------------

static DWORD WINAPI mute_thread(LPVOID param)
{
    BOOL mute = param != NULL;
    HRESULT co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;
    IAudioSessionManager2 *mgr = NULL;
    IAudioSessionEnumerator *sessions = NULL;
    int count = 0, muted = 0;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void **)&en)) &&
        SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) &&
        SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void **)&mgr)) && SUCCEEDED(mgr->GetSessionEnumerator(&sessions)) &&
        SUCCEEDED(sessions->GetCount(&count)))
    {
        for (int i = 0; i < count; i++)
        {
            IAudioSessionControl *ctl = NULL;
            IAudioSessionControl2 *ctl2 = NULL;
            ISimpleAudioVolume *vol = NULL;
            DWORD pid = 0;
            if (SUCCEEDED(sessions->GetSession(i, &ctl)) && SUCCEEDED(ctl->QueryInterface(__uuidof(IAudioSessionControl2), (void **)&ctl2)) &&
                SUCCEEDED(ctl2->GetProcessId(&pid)) && pid == GetCurrentProcessId() &&
                SUCCEEDED(ctl->QueryInterface(__uuidof(ISimpleAudioVolume), (void **)&vol)) && SUCCEEDED(vol->SetMute(mute, NULL)))
                muted++;
            if (vol)
                vol->Release();
            if (ctl2)
                ctl2->Release();
            if (ctl)
                ctl->Release();
        }
    }
    if (sessions)
        sessions->Release();
    if (mgr)
        mgr->Release();
    if (dev)
        dev->Release();
    if (en)
        en->Release();
    if (SUCCEEDED(co))
        CoUninitialize();
    wl_log("pause: sound %s (%d audio session%s)", mute ? "muted" : "back", muted, muted == 1 ? "" : "s");
    return 0;
}

static void set_mute(int mute)
{
    HANDLE t = CreateThread(NULL, 0, mute_thread, mute ? (LPVOID)1 : NULL, 0, NULL);
    if (t)
    {
        WaitForSingleObject(t, 2000);
        CloseHandle(t);
    }
}

// ------------------------------------------------------------------------------

void wl_pause_begin(void)
{
    LARGE_INTEGER now;
    oQueryPerformanceCounter(&now);
    InterlockedExchange64(&g_qpcStart, now.QuadPart);
    InterlockedExchange64(&g_qpcFrozen, now.QuadPart - read64(&g_qpcPaused));
    InterlockedExchange(&g_tickFrozen, (LONG)(oGetTickCount() - (DWORD)read64(&g_msPaused)));
    InterlockedExchange(&g_tgtFrozen, (LONG)(oTimeGetTime() - (DWORD)read64(&g_msPaused)));
    InterlockedExchange(&g_paused, 1);
    set_mute(1);
}

void wl_pause_end(void)
{
    set_mute(0);
    LARGE_INTEGER now;
    oQueryPerformanceCounter(&now);
    LONGLONG ticks = now.QuadPart - read64(&g_qpcStart);
    InterlockedExchange64(&g_qpcPaused, read64(&g_qpcPaused) + ticks);
    InterlockedExchange64(&g_msPaused, read64(&g_msPaused) + ticks * 1000 / (g_freq.QuadPart ? g_freq.QuadPart : 1));
    InterlockedExchange(&g_paused, 0);
    wl_log("pause: %.1f s", (double)ticks / (double)(g_freq.QuadPart ? g_freq.QuadPart : 1));
}
