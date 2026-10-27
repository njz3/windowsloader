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

// Logs the errors the game raises (access violations...) with the faulting module and offset,
// keeps our own CRT from killing the game on a bad argument, and logs where the game's threads
// stand when it stops presenting frames.

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>

#include "agent.h"
#include "hooks.h"
#include "log.h"
#include "menu.h"

static volatile LONG g_reported;
static DWORD g_watchThread; // the freeze watch's: its stack walks' faults are not the game's

static LONG CALLBACK on_exception(EXCEPTION_POINTERS *ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    // Only errors; C++ exceptions (0xE06D7363), debug strings and thread names are normal
    if (code < 0xC0000000 || GetCurrentThreadId() == g_watchThread || InterlockedIncrement(&g_reported) > 32)
        return EXCEPTION_CONTINUE_SEARCH;

    char where[128];
    void *addr = ep->ExceptionRecord->ExceptionAddress;
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
    {
        ULONG_PTR op = ep->ExceptionRecord->ExceptionInformation[0];
        wl_log("exception 0x%08lX (access violation, %s %p) at %s", code, op == 0 ? "read" : op == 1 ? "write" : "execute",
               (void *)ep->ExceptionRecord->ExceptionInformation[1], wl_where(addr, where, sizeof where));
    }
    else
        wl_log("exception 0x%08lX at %s", code, wl_where(addr, where, sizeof where));

#ifdef _WIN64
    CONTEXT *c = ep->ContextRecord;
    wl_log("  rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx rsi=%016llx rdi=%016llx rsp=%016llx rbp=%016llx", c->Rax, c->Rbx, c->Rcx,
           c->Rdx, c->Rsi, c->Rdi, c->Rsp, c->Rbp);
#else
    CONTEXT *c = ep->ContextRecord;
    wl_log("  eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx esp=%08lx ebp=%08lx", c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi,
           c->Edi, c->Esp, c->Ebp);
#endif
    return EXCEPTION_CONTINUE_SEARCH; // the game's own handlers still run
}

static void on_invalid_parameter(const wchar_t *expr, const wchar_t *func, const wchar_t *file, unsigned int line, uintptr_t reserved)
{
    wl_log("windowsloader: CRT invalid parameter ignored");
}

// ------------------------------------------------------------------------------
// Freezes. Once the game presents frames, FREEZE_MS without one is logged with each of its
// threads: its name or where it started, where it stands and the calls that led there, so a game
// stuck waiting (a device's answer, a lock...) shows on what. Logged again FREEZE_AGAIN_MS later
// if still frozen: the threads that did not move are the stuck ones. The menu's pause is not one:
// it holds the game in its Present.
// ------------------------------------------------------------------------------

#define FREEZE_MS 5000
#define FREEZE_AGAIN_MS 10000
#define FREEZE_REPORTS 4 // in all, the log kept short
#define FREEZE_CALLS 16  // shown per thread

typedef LONG(NTAPI *NtQueryInformationThreadFn)(HANDLE, int, void *, ULONG, ULONG *);
typedef HRESULT(WINAPI *GetThreadDescriptionFn)(HANDLE, PWSTR *);

// The thread's calls, innermost first. It is held only while its registers are read: the walk,
// done after, may read a running thread's stack moving under it, its faults caught.
static int thread_calls(HANDLE t, void **calls, int max)
{
    CONTEXT c;
    memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_FULL;
    if (SuspendThread(t) == (DWORD)-1)
        return 0;
    BOOL ok = GetThreadContext(t, &c);
    ResumeThread(t);
    if (!ok)
        return 0;
    int n = 0;
    __try
    {
#ifdef _WIN64
        while (n < max && c.Rip)
        {
            calls[n++] = (void *)c.Rip;
            DWORD64 sp = c.Rsp, base;
            PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(c.Rip, &base, NULL);
            if (f)
            {
                void *data;
                DWORD64 frame;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, c.Rip, f, &c, &data, &frame, NULL);
            }
            else
            {
                // A leaf: its caller's address is on top of the stack
                c.Rip = *(DWORD64 *)c.Rsp;
                c.Rsp += 8;
            }
            if (c.Rsp <= sp)
                break;
        }
#else
        // The frame pointers' chain: the frames without one are skipped
        calls[n++] = (void *)c.Eip;
        DWORD ebp = c.Ebp;
        while (n < max && ebp)
        {
            DWORD *frame = (DWORD *)ebp;
            if (!frame[1])
                break;
            calls[n++] = (void *)frame[1];
            if (frame[0] <= ebp)
                break;
            ebp = frame[0];
        }
#endif
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return n;
}

static NtQueryInformationThreadFn g_query;
static GetThreadDescriptionFn g_describe; // Windows 10 1607 and later

static void log_threads(DWORD frozenMs)
{
    wl_log("freeze: no frame for %lu ms (%u presented), the threads:", frozenMs, wl_d3d9_frames());
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return;
    THREADENTRY32 te;
    te.dwSize = sizeof te;
    for (BOOL more = Thread32First(snap, &te); more; more = Thread32Next(snap, &te))
    {
        if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId())
            continue;
        HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!t)
            continue;

        char name[160] = "", where[128];
        PWSTR desc = NULL;
        if (g_describe && SUCCEEDED(g_describe(t, &desc)) && desc && desc[0])
        {
            char u[128];
            snprintf(name, sizeof name, "\"%s\"", wl_u8(desc, u, sizeof u));
        }
        if (desc)
            LocalFree(desc);
        void *start = NULL;
        if (!name[0] && g_query && g_query(t, 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof start, NULL) == 0 && start)
            snprintf(name, sizeof name, "from %s", wl_where(start, where, sizeof where));

        void *calls[FREEZE_CALLS];
        int n = thread_calls(t, calls, FREEZE_CALLS);
        CloseHandle(t);
        char line[1600];
        int len = 0;
        for (int i = 0; i < n && len < (int)sizeof line - 130; i++)
            len += snprintf(line + len, sizeof line - len, "%s%s", i ? " < " : "", wl_where(calls[i], where, sizeof where));
        line[len] = 0;
        wl_log("  thread %lu %s: %s", te.th32ThreadID, name, n ? line : "(not read)");
    }
    CloseHandle(snap);
}

static DWORD WINAPI freeze_watch(LPVOID param)
{
    g_watchThread = GetCurrentThreadId();
    g_query = (NtQueryInformationThreadFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread");
    g_describe = (GetThreadDescriptionFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription");
    unsigned last = 0;
    DWORD since = GetTickCount(), next = FREEZE_MS;
    int reports = 0;
    for (;;)
    {
        Sleep(500);
        unsigned frames = wl_d3d9_frames();
        DWORD now = GetTickCount();
        if (frames != last || !frames || wl_menu_is_open())
        {
            if (next > FREEZE_MS)
                wl_log("freeze: frames presented again after %lu ms", now - since);
            last = frames;
            since = now;
            next = FREEZE_MS;
            continue;
        }
        if (now - since >= next)
        {
            if (reports++ < FREEZE_REPORTS)
                log_threads(now - since);
            next += FREEZE_AGAIN_MS;
        }
    }
}

void wl_crash_init(void)
{
    AddVectoredExceptionHandler(1, on_exception);
    _set_invalid_parameter_handler(on_invalid_parameter);
    CloseHandle(CreateThread(NULL, 0, freeze_watch, NULL, 0, NULL));
}
