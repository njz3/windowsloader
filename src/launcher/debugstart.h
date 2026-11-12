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

// [Launcher] DEBUG = true (or --debug): the game started with the launcher as its debugger, from
// its very first instruction, before windowsloader's DLL is in it (a game that stops while it
// starts, in a DLL of its own, leaves no other trace). A thread of the launcher creates the
// process and follows it: the DLLs loaded (where, how big), the threads started, the errors
// raised (the first 64 of 0xC0000000 and above but the C++ exceptions: an access violation, its
// address read or written), the debug strings, and a fatal error, one no handler took: where, the registers, the
// code addresses found on the stack, the modules, and a minidump. All in <log>.debug.log beside
// the log (and on the console), the minidump in <log>.crash.dmp; nothing written without a log
// (the write folder's rule), the console only then. A game that checks for a debugger may behave
// otherwise under it.

// CreateProcessW's arguments and its result; FALSE with GetLastError as CreateProcessW's. out
// gets each line for the console. logPath may be empty
BOOL wl_debug_start(const wchar_t *path, wchar_t *cmd, BOOL inherit, DWORD flags, const wchar_t *dir, STARTUPINFOW *si,
                    PROCESS_INFORMATION *pi, const wchar_t *logPath, void (*out)(const char *line));

// Once the game has ended: the debugger's last lines written (waits for its thread)
void wl_debug_end(void);
