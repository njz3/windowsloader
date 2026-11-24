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

// [Display] FRAME_STATS: where the presenting thread's time goes (framestats.h). Written for Road
// Fighters 3D, at 48 fps on the cabinets with neither the GPU nor a CPU core full (24/11/2026).

#include <windows.h>
#include <mmsystem.h>
#include <intrin.h>
#include <stdio.h>
#include <string.h>

#include "framestats.h"
#include "hooks.h"
#include "log.h"
#include "settings.h"

#pragma intrinsic(_ReturnAddress)
#pragma comment(lib, "winmm.lib")

static int g_state;              // 0 not read yet, 1 on, -1 off
static DWORD g_thread;           // the presenting thread
static int g_depth;              // the presenting thread inside one of our detours, or logging
static int g_inPresent;          // the presenting thread inside the game's Present
static double g_msPerTick;
static LONGLONG g_windowStart, g_frameStart, g_presentStart;
static ULONGLONG g_presentCpu;

#define CALLERS 16
typedef struct
{
    const void *where;
    char kind;      // 'S' Sleep, 'W' a wait
    DWORD timeout;  // the last one asked
    unsigned count;
    double ms;
} Caller;

typedef struct
{
    unsigned frames;
    double frameMs, frameMax;
    double presentMs, presentMax, presentWaitMs;
    double sleepMs, waitMs; // outside Present
    unsigned sleeps, waits;
    unsigned sleepAsked[4];  // Sleep(0), Sleep(1), 2..5, more
    double sleepTook[4];
    unsigned buckets[6];     // frames under 17 ms, 17-21, 21-25, 25-34, 34-50, longer
    Caller callers[CALLERS];
    int callerCount;
    ULONGLONG cpuStart;      // the thread's user + kernel time, 100 ns
    ULONGLONG cpuPresent;    // of it in Present
} Window;

static Window g_w;

static LONGLONG now(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

static ULONGLONG thread_cpu(void)
{
    FILETIME c, e, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u))
        return 0;
    return ((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime) + ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime);
}

static int counted(void)
{
    return g_state == 1 && GetCurrentThreadId() == g_thread && !g_depth;
}

static void add(const void *where, char kind, DWORD timeout, double ms)
{
    if (g_inPresent)
    {
        g_w.presentWaitMs += ms;
        return;
    }
    if (kind == 'S')
    {
        g_w.sleepMs += ms;
        g_w.sleeps++;
        int b = timeout == 0 ? 0 : timeout == 1 ? 1 : timeout <= 5 ? 2 : 3;
        g_w.sleepAsked[b]++;
        g_w.sleepTook[b] += ms;
    }
    else
    {
        g_w.waitMs += ms;
        g_w.waits++;
    }
    int i;
    for (i = 0; i < g_w.callerCount; i++)
        if (g_w.callers[i].where == where && g_w.callers[i].kind == kind)
            break;
    if (i == g_w.callerCount)
    {
        if (i == CALLERS)
            return;
        g_w.callerCount++;
        memset(&g_w.callers[i], 0, sizeof g_w.callers[i]);
        g_w.callers[i].where = where;
        g_w.callers[i].kind = kind;
    }
    g_w.callers[i].count++;
    g_w.callers[i].ms += ms;
    g_w.callers[i].timeout = timeout;
}

// Each detour times its original on the presenting thread; the inner calls (Sleep -> SleepEx)
// pass through, the outer one counted with the game's return address
#define TIMED(kind, timeout, call)                                                                                                     \
    if (!counted())                                                                                                                    \
        return call;                                                                                                                   \
    const void *where = _ReturnAddress();                                                                                              \
    g_depth++;                                                                                                                         \
    LONGLONG t0 = now();                                                                                                               \
    auto r = call;                                                                                                                     \
    double ms = (now() - t0) * g_msPerTick;                                                                                            \
    g_depth--;                                                                                                                         \
    add(where, kind, timeout, ms);                                                                                                     \
    return r;

static VOID(WINAPI *oSleep)(DWORD);
static DWORD(WINAPI *oSleepEx)(DWORD, BOOL);
static DWORD(WINAPI *oWaitForSingleObject)(HANDLE, DWORD);
static DWORD(WINAPI *oWaitForSingleObjectEx)(HANDLE, DWORD, BOOL);
static DWORD(WINAPI *oWaitForMultipleObjects)(DWORD, const HANDLE *, BOOL, DWORD);
static DWORD(WINAPI *oWaitForMultipleObjectsEx)(DWORD, const HANDLE *, BOOL, DWORD, BOOL);
static DWORD(WINAPI *oMsgWaitForMultipleObjects)(DWORD, const HANDLE *, BOOL, DWORD, DWORD);
static DWORD(WINAPI *oMsgWaitForMultipleObjectsEx)(DWORD, const HANDLE *, DWORD, DWORD, DWORD);

static VOID WINAPI fsSleep(DWORD ms)
{
    if (!counted())
    {
        oSleep(ms);
        return;
    }
    const void *where = _ReturnAddress();
    g_depth++;
    LONGLONG t0 = now();
    oSleep(ms);
    double took = (now() - t0) * g_msPerTick;
    g_depth--;
    add(where, 'S', ms, took);
}

static DWORD WINAPI fsSleepEx(DWORD timeout_, BOOL alertable)
{
    TIMED('S', timeout_, oSleepEx(timeout_, alertable))
}

static DWORD WINAPI fsWaitForSingleObject(HANDLE h, DWORD timeout_)
{
    TIMED('W', timeout_, oWaitForSingleObject(h, timeout_))
}

static DWORD WINAPI fsWaitForSingleObjectEx(HANDLE h, DWORD timeout_, BOOL alertable)
{
    TIMED('W', timeout_, oWaitForSingleObjectEx(h, timeout_, alertable))
}

static DWORD WINAPI fsWaitForMultipleObjects(DWORD n, const HANDLE *h, BOOL all, DWORD timeout_)
{
    TIMED('W', timeout_, oWaitForMultipleObjects(n, h, all, timeout_))
}

static DWORD WINAPI fsWaitForMultipleObjectsEx(DWORD n, const HANDLE *h, BOOL all, DWORD timeout_, BOOL alertable)
{
    TIMED('W', timeout_, oWaitForMultipleObjectsEx(n, h, all, timeout_, alertable))
}

static DWORD WINAPI fsMsgWaitForMultipleObjects(DWORD n, const HANDLE *h, BOOL all, DWORD timeout_, DWORD mask)
{
    TIMED('W', timeout_, oMsgWaitForMultipleObjects(n, h, all, timeout_, mask))
}

static DWORD WINAPI fsMsgWaitForMultipleObjectsEx(DWORD n, const HANDLE *h, DWORD timeout_, DWORD mask, DWORD flags)
{
    TIMED('W', timeout_, oMsgWaitForMultipleObjectsEx(n, h, timeout_, mask, flags))
}

static void hook(const char *dll, const char *name, void *detour, void **original)
{
    HMODULE m = GetModuleHandleA(dll);
    if (!m)
        m = LoadLibraryA(dll);
    void *target = m ? (void *)GetProcAddress(m, name) : NULL;
    if (!target || !wl_hook_code(target, detour, original, name))
        wl_log("frame stats: %s!%s not hooked", dll, name);
}

// [Display] FRAME_PROFILE = true: where the presenting thread's CPU goes, sampled every ms while it
// runs (its cycles counted since the last sample): the code it is in (module + 16-byte bucket), and
// the first return address on its stack in the game's executable and in windowsloader (scanned,
// each preceded by a call). The 8 most frequent of each, logged with the frame stats
#define PROFILE_SLOTS 1024
typedef struct
{
    size_t key;
    unsigned count;
} Slot;

typedef struct
{
    Slot leaf[PROFILE_SLOTS], game[PROFILE_SLOTS], wl[PROFILE_SLOTS], module[PROFILE_SLOTS];
    unsigned samples, idle, blocked;
} Profile;

// The system calls a thread waits in: a sample there is the thread blocked, not its CPU
static size_t g_waitCalls[24];
static int g_waitCallCount;

static void wait_call(const char *dll, const char *name)
{
    HMODULE m = GetModuleHandleA(dll);
    void *f = m ? (void *)GetProcAddress(m, name) : NULL;
    if (f && g_waitCallCount < 24)
        g_waitCalls[g_waitCallCount++] = (size_t)f & ~(size_t)15;
}

static int in_wait_call(size_t pc)
{
    pc &= ~(size_t)15;
    for (int i = 0; i < g_waitCallCount; i++)
        if (pc == g_waitCalls[i] || pc == g_waitCalls[i] + 16)
            return 1;
    return 0;
}

// The module holding an address (its base), cached
static size_t g_modLo[64], g_modHi[64];
static int g_modCount;

static size_t module_of(size_t a)
{
    for (int i = 0; i < g_modCount; i++)
        if (a >= g_modLo[i] && a < g_modHi[i])
            return g_modLo[i];
    HMODULE m = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)a, &m) || !m)
        return 0;
    if (g_modCount < 64)
    {
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)m + ((IMAGE_DOS_HEADER *)m)->e_lfanew);
        g_modLo[g_modCount] = (size_t)m;
        g_modHi[g_modCount++] = (size_t)m + nt->OptionalHeader.SizeOfImage;
    }
    return (size_t)m;
}

static Profile g_profile;
static volatile LONG g_fiberSwitches; // the game's SwitchToFiber calls
static SRWLOCK g_profileLock = SRWLOCK_INIT;
static size_t g_gameLo, g_gameHi, g_wlLo, g_wlHi;

static void module_range(HMODULE m, size_t *lo, size_t *hi)
{
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)m + ((IMAGE_DOS_HEADER *)m)->e_lfanew);
    *lo = (size_t)m;
    *hi = (size_t)m + nt->OptionalHeader.SizeOfImage;
}

static void count(Slot *t, size_t key)
{
    size_t h = (key >> 4) * 2654435761u % PROFILE_SLOTS;
    for (int i = 0; i < PROFILE_SLOTS; i++, h = (h + 1) % PROFILE_SLOTS)
        if (t[h].key == key || !t[h].count)
        {
            t[h].key = key;
            t[h].count++;
            return;
        }
}

// A return address: the bytes before it a call (E8 rel32, FF /2 of 2, 3, 6 or 7 bytes)
static int after_call(size_t a)
{
    const unsigned char *p = (const unsigned char *)a;
    __try
    {
        if (p[-5] == 0xE8)
            return 1;
        for (int n = 2; n <= 7; n++)
            if (p[-n] == 0xFF && ((p[-n + 1] >> 3) & 7) == 2)
                return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return 0;
}

static DWORD WINAPI profile_thread(LPVOID)
{
    wl_busy_enter();
    HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_thread);
    if (!t)
    {
        wl_log("frame stats: the profile cannot open thread %lu (%lu)", g_thread, GetLastError());
        return 0;
    }
    timeBeginPeriod(1);
    static size_t stack[1024];
    ULONG64 lastCycles = 0;
    for (;;)
    {
        Sleep(1);
        ULONG64 cycles = 0;
        QueryThreadCycleTime(t, &cycles);
        int ran = cycles - lastCycles > 100000; // 0.03 ms at 3.3 GHz: it ran since the last sample
        lastCycles = cycles;
        if (!ran)
        {
            g_profile.idle++;
            continue;
        }
        if (SuspendThread(t) == (DWORD)-1)
            break;
        CONTEXT c;
        c.ContextFlags = CONTEXT_CONTROL;
        size_t pc = 0, sp = 0, words = 0;
        if (GetThreadContext(t, &c))
        {
#ifdef _WIN64
            pc = (size_t)c.Rip;
            sp = (size_t)c.Rsp;
#else
            pc = (size_t)c.Eip;
            sp = (size_t)c.Esp;
#endif
            __try
            {
                for (; words < 1024; words++)
                    stack[words] = ((volatile size_t *)sp)[words];
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
        ResumeThread(t);
        if (!pc)
            continue;
        size_t game = 0, wl = 0;
        for (size_t i = 0; i < words && (!game || !wl); i++)
        {
            size_t a = stack[i];
            if (!game && a > g_gameLo && a < g_gameHi && after_call(a))
                game = a;
            else if (!wl && a > g_wlLo && a < g_wlHi && after_call(a))
                wl = a;
        }
        int blocked = in_wait_call(pc);
        size_t mod = blocked ? 0 : module_of(pc);
        AcquireSRWLockExclusive(&g_profileLock);
        if (blocked)
            g_profile.blocked++;
        else
        {
            g_profile.samples++;
            count(g_profile.leaf, pc & ~(size_t)15);
            count(g_profile.module, mod);
            count(g_profile.game, game);
            count(g_profile.wl, wl);
        }
        ReleaseSRWLockExclusive(&g_profileLock);
    }
    return 0;
}

static void profile_log_table(const char *what, Slot *t, unsigned samples)
{
    for (int k = 0; k < 8; k++)
    {
        int best = -1;
        for (int i = 0; i < PROFILE_SLOTS; i++)
            if (t[i].count && (best < 0 || t[i].count > t[best].count))
                best = i;
        if (best < 0 || t[best].count * 100 < samples) // under 1 %
            break;
        char where[160];
        if (t[best].key)
            wl_where((const void *)t[best].key, where, sizeof where);
        else
            snprintf(where, sizeof where, "(none on the stack)");
        wl_log("frame stats:   %s %s: %.1f%%", what, where, t[best].count * 100.0 / samples);
        t[best].count = 0;
    }
}

static void profile_log(void)
{
    static Profile p; // copied out of the lock: wl_where and the log may take the loader's lock
    AcquireSRWLockExclusive(&g_profileLock);
    p = g_profile;
    memset(&g_profile, 0, sizeof g_profile);
    ReleaseSRWLockExclusive(&g_profileLock);
    if (!p.samples)
        return;
    wl_log("frame stats: profile, %u samples running (%u blocked in a wait, %u idle), %ld fiber switches", p.samples, p.blocked, p.idle,
           InterlockedExchange(&g_fiberSwitches, 0));
    profile_log_table("module", p.module, p.samples);
    profile_log_table("in", p.leaf, p.samples);
    profile_log_table("game's caller", p.game, p.samples);
    profile_log_table("windowsloader's caller", p.wl, p.samples);
}

// The game's fiber switches (Super Bikes 2 and Drift run their tasks on fibers): counted
static VOID(WINAPI *oSwitchToFiber)(LPVOID);

static VOID WINAPI fsSwitchToFiber(LPVOID fiber)
{
    InterlockedIncrement(&g_fiberSwitches);
    oSwitchToFiber(fiber);
}

static void profile_start(void)
{
    static const char *waits[] = {"ZwWaitForSingleObject", "ZwWaitForMultipleObjects", "ZwWaitForMultipleObjects32", "ZwDelayExecution",
                                  "ZwAlpcSendWaitReceivePort", "ZwWaitForAlertByThreadId", "ZwRemoveIoCompletion", "ZwRemoveIoCompletionEx",
                                  "ZwSignalAndWaitForSingleObject", "ZwYieldExecution", "ZwWaitForKeyedEvent", "ZwReplyWaitReceivePort",
                                  "ZwWaitForWorkViaWorkerFactory", "ZwDeviceIoControlFile"};
    for (int i = 0; i < (int)(sizeof waits / sizeof waits[0]); i++)
        wait_call("ntdll.dll", waits[i]);
    LoadLibraryA("win32u.dll");
    wait_call("win32u.dll", "NtUserMsgWaitForMultipleObjectsEx");
    wait_call("win32u.dll", "NtUserWaitMessage");
    wait_call("win32u.dll", "NtGdiDdDDIWaitForSynchronizationObject");
    wait_call("gdi32.dll", "D3DKMTWaitForSynchronizationObject");
    module_range(GetModuleHandleW(NULL), &g_gameLo, &g_gameHi);
    HMODULE self = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)profile_start, &self);
    if (self)
        module_range(self, &g_wlLo, &g_wlHi);
    if (wl_patch_iat(GetModuleHandleW(NULL), "KERNEL32.dll", "SwitchToFiber", (void *)fsSwitchToFiber, (void **)&oSwitchToFiber))
        wl_log("frame stats: the game's SwitchToFiber counted");
    HANDLE h = CreateThread(NULL, 0, profile_thread, NULL, 0, NULL);
    if (h)
    {
        SetThreadPriority(h, THREAD_PRIORITY_HIGHEST);
        CloseHandle(h);
        wl_log("frame stats: profile of thread %lu every ms (FRAME_PROFILE)", g_thread);
    }
}

static void start(void)
{
    g_state = wl_settings_get_bool("Display", "FRAME_STATS", 0) ? 1 : -1;
    if (g_state < 0)
        return;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_msPerTick = 1000.0 / (double)f.QuadPart;
    g_thread = GetCurrentThreadId();
    g_depth++;
    hook("kernelbase.dll", "Sleep", (void *)fsSleep, (void **)&oSleep);
    hook("kernelbase.dll", "SleepEx", (void *)fsSleepEx, (void **)&oSleepEx);
    hook("kernelbase.dll", "WaitForSingleObject", (void *)fsWaitForSingleObject, (void **)&oWaitForSingleObject);
    hook("kernelbase.dll", "WaitForSingleObjectEx", (void *)fsWaitForSingleObjectEx, (void **)&oWaitForSingleObjectEx);
    hook("kernelbase.dll", "WaitForMultipleObjects", (void *)fsWaitForMultipleObjects, (void **)&oWaitForMultipleObjects);
    hook("kernelbase.dll", "WaitForMultipleObjectsEx", (void *)fsWaitForMultipleObjectsEx, (void **)&oWaitForMultipleObjectsEx);
    hook("user32.dll", "MsgWaitForMultipleObjects", (void *)fsMsgWaitForMultipleObjects, (void **)&oMsgWaitForMultipleObjects);
    hook("user32.dll", "MsgWaitForMultipleObjectsEx", (void *)fsMsgWaitForMultipleObjectsEx, (void **)&oMsgWaitForMultipleObjectsEx);
    TIMECAPS tc;
    typedef MMRESULT(WINAPI * GetDevCaps)(LPTIMECAPS, UINT);
    HMODULE winmm = LoadLibraryA("winmm.dll");
    GetDevCaps caps = winmm ? (GetDevCaps)GetProcAddress(winmm, "timeGetDevCaps") : NULL;
    if (caps && caps(&tc, sizeof tc) == 0)
        wl_log("frame stats: on, thread %lu (the timer's period %u to %u ms)", g_thread, tc.wPeriodMin, tc.wPeriodMax);
    else
        wl_log("frame stats: on, thread %lu", g_thread);
    if (wl_settings_get_bool("Display", "FRAME_PROFILE", 0))
        profile_start();
    g_depth--;
    memset(&g_w, 0, sizeof g_w);
    g_windowStart = now();
    g_w.cpuStart = thread_cpu();
}

static void log_window(LONGLONG t)
{
    g_depth++;
    double span = (t - g_windowStart) * g_msPerTick;
    double cpu = (thread_cpu() - g_w.cpuStart - g_w.cpuPresent) / 10000.0; // outside Present
    unsigned n = g_w.frames ? g_w.frames : 1;
    double rest = g_w.frameMs - g_w.presentMs - g_w.sleepMs - g_w.waitMs - cpu;
    wl_log("frame stats: %u frames in %.0f ms, %.1f fps; per frame %.2f ms (longest %.1f): Present %.2f (longest %.1f, of it waiting %.2f), "
           "Sleep %.2f (%u calls), waits %.2f (%u calls), CPU outside Present %.2f, the rest %.2f (blocked elsewhere)",
           g_w.frames, span, g_w.frames * 1000.0 / span, g_w.frameMs / n, g_w.frameMax, g_w.presentMs / n, g_w.presentMax, g_w.presentWaitMs / n,
           g_w.sleepMs / n, g_w.sleeps, g_w.waitMs / n, g_w.waits, cpu / n, rest / n);
    wl_log("frame stats: frames < 17 ms %u, 17-21 %u, 21-25 %u, 25-34 %u, 34-50 %u, longer %u; Sleep(0) %u calls %.1f ms, Sleep(1) %u %.1f ms, "
           "Sleep(2-5) %u %.1f ms, Sleep(>5) %u %.1f ms",
           g_w.buckets[0], g_w.buckets[1], g_w.buckets[2], g_w.buckets[3], g_w.buckets[4], g_w.buckets[5], g_w.sleepAsked[0], g_w.sleepTook[0],
           g_w.sleepAsked[1], g_w.sleepTook[1], g_w.sleepAsked[2], g_w.sleepTook[2], g_w.sleepAsked[3], g_w.sleepTook[3]);
    // The callers that waited most
    for (int k = 0; k < 4; k++)
    {
        int best = -1;
        for (int i = 0; i < g_w.callerCount; i++)
            if (g_w.callers[i].count && (best < 0 || g_w.callers[i].ms > g_w.callers[best].ms))
                best = i;
        if (best < 0 || g_w.callers[best].ms < 1)
            break;
        char where[160];
        wl_where(g_w.callers[best].where, where, sizeof where);
        wl_log("frame stats:   %s from %s: %u calls, %.2f ms per frame (timeout %ld)", g_w.callers[best].kind == 'S' ? "Sleep" : "wait", where,
               g_w.callers[best].count, g_w.callers[best].ms / n, (long)g_w.callers[best].timeout);
        g_w.callers[best].count = 0;
    }
    profile_log();
    memset(&g_w, 0, sizeof g_w);
    g_windowStart = t;
    g_w.cpuStart = thread_cpu();
    g_depth--;
}

void wl_framestats_present_begin(void)
{
    if (!g_state)
        start();
    if (g_state < 0 || GetCurrentThreadId() != g_thread)
        return;
    LONGLONG t = now();
    if (g_frameStart)
    {
        double ms = (t - g_frameStart) * g_msPerTick;
        g_w.frames++;
        g_w.frameMs += ms;
        if (ms > g_w.frameMax)
            g_w.frameMax = ms;
        g_w.buckets[ms < 17 ? 0 : ms < 21 ? 1 : ms < 25 ? 2 : ms < 34 ? 3 : ms < 50 ? 4 : 5]++;
    }
    g_frameStart = t;
    if ((t - g_windowStart) * g_msPerTick >= 10000)
        log_window(t);
    g_presentCpu = thread_cpu();
    g_presentStart = now();
    g_inPresent = 1;
}

void wl_framestats_present_end(void)
{
    if (g_state < 0 || GetCurrentThreadId() != g_thread || !g_inPresent)
        return;
    g_inPresent = 0;
    double ms = (now() - g_presentStart) * g_msPerTick;
    g_w.cpuPresent += thread_cpu() - g_presentCpu;
    g_w.presentMs += ms;
    if (ms > g_w.presentMax)
        g_w.presentMax = ms;
}
