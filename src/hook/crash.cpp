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
// and keeps our own CRT from killing the game on a bad argument.

#include <windows.h>
#include <stdlib.h>

#include "hooks.h"
#include "log.h"

static volatile LONG g_reported;

static LONG CALLBACK on_exception(EXCEPTION_POINTERS *ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    // Only errors; C++ exceptions (0xE06D7363), debug strings and thread names are normal
    if (code < 0xC0000000 || InterlockedIncrement(&g_reported) > 32)
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

void wl_crash_init(void)
{
    AddVectoredExceptionHandler(1, on_exception);
    _set_invalid_parameter_handler(on_invalid_parameter);
}
