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
#include <stdarg.h>
#include <stdio.h>

#include "hooks.h"
#include "log.h"

static HANDLE g_logFile = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_logLock;
static LARGE_INTEGER g_logStart, g_logFreq;

void wl_log_open(const wchar_t *path)
{
    InitializeCriticalSection(&g_logLock);
    QueryPerformanceFrequency(&g_logFreq);
    QueryPerformanceCounter(&g_logStart);
    if (path && path[0])
        g_logFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

void wl_log_close(void)
{
    if (g_logFile == INVALID_HANDLE_VALUE)
        return;
    EnterCriticalSection(&g_logLock);
    HANDLE f = g_logFile;
    g_logFile = INVALID_HANDLE_VALUE;
    LeaveCriticalSection(&g_logLock);
    wl_busy_enter();
    CloseHandle(f);
    wl_busy_leave();
}

static void log_line(const char *text, int len)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double t = (double)(now.QuadPart - g_logStart.QuadPart) / (double)g_logFreq.QuadPart;

    char line[2048];
    int n = snprintf(line, sizeof line, "[%9.3f] [%5lu] ", t, GetCurrentThreadId());
    if (n < 0)
        return;
    if (len > (int)sizeof line - n - 2)
        len = (int)sizeof line - n - 2;
    memcpy(line + n, text, len);
    n += len;
    line[n++] = '\n';

    wl_busy_enter();
    EnterCriticalSection(&g_logLock);
    if (g_logFile != INVALID_HANDLE_VALUE)
    {
        DWORD written;
        WriteFile(g_logFile, line, (DWORD)n, &written, NULL);
    }
    LeaveCriticalSection(&g_logLock);
    wl_busy_leave();
}

void wl_log(const char *fmt, ...)
{
    if (g_logFile == INVALID_HANDLE_VALUE)
        return;
    DWORD err = GetLastError();
    char buf[1800];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n >= 0)
    {
        if (n >= (int)sizeof buf)
            n = sizeof buf - 1;
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
            n--;
        log_line(buf, n);
    }
    SetLastError(err);
}

void wl_log_hex(const char *prefix, const void *data, size_t len)
{
    static const char digits[] = "0123456789abcdef";
    const unsigned char *p = (const unsigned char *)data;
    char hex[64 * 3 + 8];
    size_t shown = len > 64 ? 64 : len, o = 0;
    for (size_t i = 0; i < shown && p; i++)
    {
        hex[o++] = digits[p[i] >> 4];
        hex[o++] = digits[p[i] & 15];
        hex[o++] = ' ';
    }
    if (shown < len)
    {
        memcpy(hex + o, "...", 3);
        o += 3;
    }
    else if (o)
        o--;
    hex[o] = 0;
    wl_log("%s %s", prefix, hex);
}

const char *wl_u8(const wchar_t *w, char *buf, size_t size)
{
    if (!w)
        return "(null)";
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, (int)size, NULL, NULL))
    {
        // Too long: keep what surely fits (4 UTF-8 bytes at most per UTF-16 unit)
        size_t units = wcslen(w);
        if (units > size / 4)
            units = size / 4;
        int n = WideCharToMultiByte(CP_UTF8, 0, w, (int)units, buf, (int)size - 1, NULL, NULL);
        buf[n > 0 ? n : 0] = 0;
    }
    return buf;
}
