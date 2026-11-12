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

// The game under the launcher as its debugger (debugstart.h). The launcher has the game's bitness
// (a 32-bit game gets windowsloader.exe, a 64-bit one windowsloader64.exe), so the game's thread
// contexts are the launcher's own CONTEXT.

#include <windows.h>
#include <dbghelp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "debugstart.h"

#define MAX_MODULES 1024
#define FIRST_CHANCE_LOGGED 64
#define THREADS_LOGGED 200
#define STRINGS_LOGGED 200
#define STACK_BYTES 2048
#define STACK_FOUND 32

struct Module
{
    ULONG_PTR base, size;
    char name[MAX_PATH];
};

static struct
{
    const wchar_t *path;
    wchar_t *cmd;
    BOOL inherit;
    DWORD flags;
    const wchar_t *dir;
    STARTUPINFOW *si;
    PROCESS_INFORMATION pi;
    BOOL ok;
    DWORD error;
    HANDLE ready, thread, file;
    wchar_t dump[MAX_PATH];
    void (*out)(const char *line);
    ULONGLONG start;
    Module modules[MAX_MODULES];
    int moduleCount;
} g;

static void dlog(const char *fmt, ...)
{
    char line[2048];
    int n = snprintf(line, sizeof line, "[debug %8.3f] ", (GetTickCount64() - g.start) / 1000.0);
    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(line + n, sizeof line - n - 2, fmt, ap);
    va_end(ap);
    n = m < 0 ? n : n + m < (int)sizeof line - 2 ? n + m : (int)sizeof line - 3;
    line[n++] = '\n';
    line[n] = 0;
    DWORD written;
    if (g.file != INVALID_HANDLE_VALUE)
        WriteFile(g.file, line, (DWORD)n, &written, NULL);
    if (g.out)
        g.out(line);
}

static void to_u8(const wchar_t *w, char *out, size_t size)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)size, NULL, NULL))
        out[0] = 0;
}

// The module's size from its headers in the game
static ULONG_PTR image_size(ULONG_PTR base)
{
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS nt;
    if (!ReadProcessMemory(g.pi.hProcess, (LPCVOID)base, &dos, sizeof dos, NULL) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        !ReadProcessMemory(g.pi.hProcess, (LPCVOID)(base + dos.e_lfanew), &nt, sizeof nt, NULL) || nt.Signature != IMAGE_NT_SIGNATURE)
        return 0;
    return nt.OptionalHeader.SizeOfImage;
}

static void module_name(HANDLE file, ULONG_PTR base, char *out, size_t size)
{
    wchar_t w[MAX_PATH];
    out[0] = 0;
    if (file && GetFinalPathNameByHandleW(file, w, MAX_PATH, FILE_NAME_NORMALIZED))
    {
        const wchar_t *p = wcsncmp(w, L"\\\\?\\", 4) ? w : w + 4;
        to_u8(p, out, size);
    }
    if (!out[0])
        snprintf(out, size, "(module at %p)", (void *)base);
}

static void add_module(HANDLE file, ULONG_PTR base)
{
    if (g.moduleCount >= MAX_MODULES)
        return;
    Module *m = &g.modules[g.moduleCount++];
    m->base = base;
    m->size = image_size(base);
    module_name(file, base, m->name, sizeof m->name);
    dlog("module %s at %p, %lu bytes", m->name, (void *)base, (unsigned long)m->size);
}

static void remove_module(ULONG_PTR base)
{
    for (int i = 0; i < g.moduleCount; i++)
        if (g.modules[i].base == base)
        {
            dlog("module %s unloaded", g.modules[i].name);
            g.modules[i] = g.modules[--g.moduleCount];
            return;
        }
}

// "module.dll+0x1234" (its file name), or the address alone
static const char *where(ULONG_PTR a, char *buf, size_t size)
{
    for (int i = 0; i < g.moduleCount; i++)
    {
        const Module *m = &g.modules[i];
        if (m->size && a >= m->base && a < m->base + m->size)
        {
            const char *slash = strrchr(m->name, '\\');
            snprintf(buf, size, "%s+0x%llx", slash ? slash + 1 : m->name, (unsigned long long)(a - m->base));
            return buf;
        }
    }
    snprintf(buf, size, "%p", (void *)a);
    return buf;
}

static int in_module(ULONG_PTR a)
{
    for (int i = 0; i < g.moduleCount; i++)
        if (g.modules[i].size && a >= g.modules[i].base && a < g.modules[i].base + g.modules[i].size)
            return 1;
    return 0;
}

static const char *code_name(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
        return "access violation";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "illegal instruction";
    case EXCEPTION_STACK_OVERFLOW:
        return "stack overflow";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "division by zero";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "privileged instruction";
    case EXCEPTION_IN_PAGE_ERROR:
        return "in-page error";
    case 0xC0000409:
        return "stack buffer overrun or fast fail";
    case 0xC0000374:
        return "heap corruption";
    case 0xC0000135:
        return "DLL not found";
    case 0xC0000139:
        return "entry point not found";
    case 0xC0000142:
        return "DLL initialization failed";
    case 0xE06D7363:
        return "C++ exception";
    case EXCEPTION_BREAKPOINT:
        return "breakpoint";
    default:
        return "";
    }
}

static void describe(const EXCEPTION_RECORD *r, char *out, size_t size)
{
    char at[MAX_PATH + 32];
    where((ULONG_PTR)r->ExceptionAddress, at, sizeof at);
    const char *name = code_name(r->ExceptionCode);
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
    {
        ULONG_PTR op = r->ExceptionInformation[0];
        snprintf(out, size, "0x%08lX (access violation, %s %p) at %s", r->ExceptionCode, op == 0 ? "read" : op == 1 ? "write" : "execute",
                 (void *)r->ExceptionInformation[1], at);
    }
    else
        snprintf(out, size, "0x%08lX%s%s%s at %s", r->ExceptionCode, name[0] ? " (" : "", name, name[0] ? ")" : "", at);
}

static void write_dump(DWORD threadId, EXCEPTION_RECORD *r, CONTEXT *c)
{
    if (!g.dump[0])
    {
        dlog("no minidump: no log file (the write folder's rule)");
        return;
    }
    HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    typedef BOOL(WINAPI * WriteFn)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
                                   PMINIDUMP_CALLBACK_INFORMATION);
    WriteFn write = dbghelp ? (WriteFn)GetProcAddress(dbghelp, "MiniDumpWriteDump") : NULL;
    if (!write)
    {
        dlog("no minidump: dbghelp.dll not found");
        return;
    }
    HANDLE f = CreateFileW(g.dump, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
    {
        dlog("no minidump: the file cannot be written (error %lu)", GetLastError());
        return;
    }
    EXCEPTION_POINTERS ep = {r, c};
    MINIDUMP_EXCEPTION_INFORMATION mei = {threadId, &ep, FALSE};
    MINIDUMP_TYPE type =
        (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpWithHandleData);
    BOOL ok = write(g.pi.hProcess, g.pi.dwProcessId, f, type, c ? &mei : NULL, NULL, NULL);
    DWORD err = GetLastError();
    LARGE_INTEGER size = {0};
    GetFileSizeEx(f, &size);
    CloseHandle(f);
    char u[MAX_PATH * 3];
    to_u8(g.dump, u, sizeof u);
    if (ok)
        dlog("minidump written: %s (%lld bytes)", u, size.QuadPart);
    else
        dlog("minidump not written (error 0x%08lx): %s", err, u);
}

// An error no handler of the game took: the process ends after it
static void fatal(const DEBUG_EVENT *e)
{
    EXCEPTION_RECORD r = e->u.Exception.ExceptionRecord;
    char text[MAX_PATH + 128], at[MAX_PATH + 32];
    describe(&r, text, sizeof text);
    dlog("FATAL: exception %s, thread %lu, not handled by the game", text, e->dwThreadId);

    CONTEXT c;
    memset(&c, 0, sizeof c);
    c.ContextFlags = CONTEXT_FULL;
    HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, e->dwThreadId);
    BOOL got = t && GetThreadContext(t, &c);
    if (t)
        CloseHandle(t);
    ULONG_PTR sp = 0;
    if (got)
    {
#ifdef _WIN64
        dlog("  rip=%016llx rsp=%016llx rbp=%016llx rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx", c.Rip, c.Rsp, c.Rbp, c.Rax, c.Rbx, c.Rcx, c.Rdx);
        dlog("  rsi=%016llx rdi=%016llx r8=%016llx r9=%016llx r10=%016llx r11=%016llx", c.Rsi, c.Rdi, c.R8, c.R9, c.R10, c.R11);
        sp = (ULONG_PTR)c.Rsp;
#else
        dlog("  eip=%08lx esp=%08lx ebp=%08lx eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx", c.Eip, c.Esp, c.Ebp, c.Eax, c.Ebx, c.Ecx, c.Edx,
             c.Esi, c.Edi);
        sp = (ULONG_PTR)c.Esp;
#endif
    }
    else
        dlog("  registers not read (error %lu)", GetLastError());

    // The code addresses on the stack, nearest first: the calls that led there, and some leftovers
    if (sp)
    {
        static ULONG_PTR stack[STACK_BYTES / sizeof(ULONG_PTR)];
        SIZE_T read = 0;
        ReadProcessMemory(g.pi.hProcess, (LPCVOID)sp, stack, sizeof stack, &read);
        int found = 0;
        for (SIZE_T i = 0; i < read / sizeof(ULONG_PTR) && found < STACK_FOUND; i++)
            if (in_module(stack[i]))
            {
                dlog("  stack +0x%03llx: %s", (unsigned long long)(i * sizeof(ULONG_PTR)), where(stack[i], at, sizeof at));
                found++;
            }
        if (!found)
            dlog("  stack: no code address in its first %lu bytes", (unsigned long)read);
    }
    dlog("  modules loaded (%d):", g.moduleCount);
    for (int i = 0; i < g.moduleCount; i++)
        dlog("    %p-%p %s", (void *)g.modules[i].base, (void *)(g.modules[i].base + g.modules[i].size), g.modules[i].name);
    write_dump(e->dwThreadId, &r, got ? &c : NULL);
}

static void debug_string(const OUTPUT_DEBUG_STRING_INFO *s)
{
    static int logged;
    if (logged >= STRINGS_LOGGED)
        return;
    if (++logged == STRINGS_LOGGED)
        dlog("(the next debug strings are not shown here)");
    char text[1024];
    text[0] = 0;
    if (s->fUnicode)
    {
        wchar_t w[512];
        SIZE_T n = s->nDebugStringLength < 511 ? s->nDebugStringLength : 511;
        if (ReadProcessMemory(g.pi.hProcess, s->lpDebugStringData, w, n * sizeof(wchar_t), NULL))
        {
            w[n] = 0;
            to_u8(w, text, sizeof text);
        }
    }
    else
    {
        SIZE_T n = s->nDebugStringLength < sizeof text - 1 ? s->nDebugStringLength : sizeof text - 1;
        if (ReadProcessMemory(g.pi.hProcess, s->lpDebugStringData, text, n, NULL))
            text[n] = 0;
    }
    size_t len = strlen(text);
    while (len && (text[len - 1] == '\n' || text[len - 1] == '\r'))
        text[--len] = 0;
    if (len)
        dlog("debug string: %s", text);
}

static DWORD WINAPI debug_thread(LPVOID)
{
    // The thread that creates the process is its debugger: it alone may wait for its events
    g.ok = CreateProcessW(g.path, g.cmd, NULL, NULL, g.inherit, g.flags | DEBUG_ONLY_THIS_PROCESS, NULL, g.dir, g.si, &g.pi);
    g.error = GetLastError();
    SetEvent(g.ready);
    if (!g.ok)
        return 0;
    dlog("the game started under the launcher as its debugger (pid %lu)", g.pi.dwProcessId);
    int loaderBreak = 1, firstChance = 0, threads = 0;
    for (;;)
    {
        DEBUG_EVENT e;
        if (!WaitForDebugEvent(&e, INFINITE))
        {
            dlog("WaitForDebugEvent failed (error %lu): the game no longer followed", GetLastError());
            return 0;
        }
        DWORD cont = DBG_CONTINUE;
        char at[MAX_PATH + 32], text[MAX_PATH + 128];
        int done = 0;
        switch (e.dwDebugEventCode)
        {
        case CREATE_PROCESS_DEBUG_EVENT:
            add_module(e.u.CreateProcessInfo.hFile, (ULONG_PTR)e.u.CreateProcessInfo.lpBaseOfImage);
            if (e.u.CreateProcessInfo.hFile)
                CloseHandle(e.u.CreateProcessInfo.hFile);
            break;
        case LOAD_DLL_DEBUG_EVENT:
            add_module(e.u.LoadDll.hFile, (ULONG_PTR)e.u.LoadDll.lpBaseOfDll);
            if (e.u.LoadDll.hFile)
                CloseHandle(e.u.LoadDll.hFile);
            break;
        case UNLOAD_DLL_DEBUG_EVENT:
            remove_module((ULONG_PTR)e.u.UnloadDll.lpBaseOfDll);
            break;
        case CREATE_THREAD_DEBUG_EVENT:
            if (threads++ < THREADS_LOGGED)
                dlog("thread %lu started at %s", e.dwThreadId, where((ULONG_PTR)e.u.CreateThread.lpStartAddress, at, sizeof at));
            break;
        case EXIT_THREAD_DEBUG_EVENT:
            if (e.u.ExitThread.dwExitCode >= 0xC0000000)
                dlog("thread %lu ended with 0x%08lX", e.dwThreadId, e.u.ExitThread.dwExitCode);
            break;
        case OUTPUT_DEBUG_STRING_EVENT:
            debug_string(&e.u.DebugString);
            break;
        case RIP_EVENT:
            dlog("RIP event: error %lu, type %lu", e.u.RipInfo.dwError, e.u.RipInfo.dwType);
            break;
        case EXCEPTION_DEBUG_EVENT:
        {
            const EXCEPTION_RECORD *r = &e.u.Exception.ExceptionRecord;
            DWORD code = r->ExceptionCode;
            // The loader's breakpoint, the one a debugger gets when the process starts
            if (loaderBreak && (code == EXCEPTION_BREAKPOINT || code == 0x4000001F))
            {
                loaderBreak = 0;
                dlog("the loader's breakpoint (the process initialised)");
                break;
            }
            cont = DBG_EXCEPTION_NOT_HANDLED; // the game's handlers get it, as without a debugger
            if (!e.u.Exception.dwFirstChance)
                fatal(&e);
            else if (code >= 0xC0000000 && code != 0xE06D7363 && firstChance < FIRST_CHANCE_LOGGED) // C++ exceptions are the game's own
            {
                describe(r, text, sizeof text);
                dlog("exception %s, thread %lu (first chance: the game may handle it)%s", text, e.dwThreadId,
                     ++firstChance == FIRST_CHANCE_LOGGED ? "; the next ones not shown" : "");
            }
            break;
        }
        case EXIT_PROCESS_DEBUG_EVENT:
            dlog("the game ended, exit code 0x%08lX%s%s%s", e.u.ExitProcess.dwExitCode, code_name(e.u.ExitProcess.dwExitCode)[0] ? " (" : "",
                 code_name(e.u.ExitProcess.dwExitCode), code_name(e.u.ExitProcess.dwExitCode)[0] ? ")" : "");
            done = 1;
            break;
        default:
            break;
        }
        ContinueDebugEvent(e.dwProcessId, e.dwThreadId, cont);
        if (done)
            return 0;
    }
}

BOOL wl_debug_start(const wchar_t *path, wchar_t *cmd, BOOL inherit, DWORD flags, const wchar_t *dir, STARTUPINFOW *si,
                    PROCESS_INFORMATION *pi, const wchar_t *logPath, void (*out)(const char *line))
{
    g.path = path;
    g.cmd = cmd;
    g.inherit = inherit;
    g.flags = flags;
    g.dir = dir;
    g.si = si;
    g.out = out;
    g.start = GetTickCount64();
    g.file = INVALID_HANDLE_VALUE;
    g.dump[0] = 0;
    if (logPath && logPath[0])
    {
        // <log>.debug.log and <log>.crash.dmp, <log> the log's path without its .log
        wchar_t base[MAX_PATH], file[MAX_PATH];
        wcsncpy_s(base, MAX_PATH, logPath, _TRUNCATE);
        size_t n = wcslen(base);
        if (n > 4 && !_wcsicmp(base + n - 4, L".log"))
            base[n - 4] = 0;
        swprintf(file, MAX_PATH, L"%s.debug.log", base);
        swprintf(g.dump, MAX_PATH, L"%s.crash.dmp", base);
        DeleteFileW(g.dump); // the last crash's, not this start's
        g.file = CreateFileW(file, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        char u[MAX_PATH * 3];
        to_u8(file, u, sizeof u);
        if (g.file == INVALID_HANDLE_VALUE)
            dlog("the debug log %s cannot be written (error %lu): this console only", u, GetLastError());
        else
            dlog("debug log: %s", u);
    }
    g.ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    g.thread = g.ready ? CreateThread(NULL, 0, debug_thread, NULL, 0, NULL) : NULL;
    if (!g.thread)
    {
        DWORD err = GetLastError();
        if (g.file != INVALID_HANDLE_VALUE)
            CloseHandle(g.file);
        g.file = INVALID_HANDLE_VALUE;
        SetLastError(err);
        return FALSE;
    }
    WaitForSingleObject(g.ready, INFINITE);
    if (!g.ok)
    {
        WaitForSingleObject(g.thread, INFINITE);
        CloseHandle(g.thread);
        g.thread = NULL;
        SetLastError(g.error);
        return FALSE;
    }
    *pi = g.pi;
    return TRUE;
}

void wl_debug_end(void)
{
    if (g.thread)
    {
        WaitForSingleObject(g.thread, 10000);
        CloseHandle(g.thread);
        g.thread = NULL;
    }
    if (g.file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g.file);
        g.file = INVALID_HANDLE_VALUE;
    }
}
