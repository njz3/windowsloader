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

// A list chosen from with the cabinet's controls (picklist.h): a window of its own, drawn with GDI
// (no Direct3D before the game's), the controls read every 16 ms from their inputs (controls.cpp
// keeps them up to date from its own thread).

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "hooks.h"
#include "log.h"
#include "picklist.h"

#define REPEAT_FIRST 400 // ms a direction held before it moves again
#define REPEAT_NEXT 160  // then between moves

struct Pick
{
    const char *const *labels;
    int count;
    int selected;
    int chosen; // -1 while none
    int closed;
    int top;    // the first line shown
    int rows;   // lines shown
    int rowHeight, listTop, listLeft, listWidth;
    wchar_t title[256], hint[256], status[128];
};

static Pick *g_pick;

static void widen(const char *s, wchar_t *out, int size)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, s ? s : "", -1, out, size))
        out[0] = 0;
}

static void layout(Pick *p, int width, int height)
{
    p->rowHeight = height / 16 > 20 ? height / 16 : 20;
    p->listTop = height / 5;
    p->rows = (height * 7 / 10 - p->listTop) / p->rowHeight;
    if (p->rows < 1)
        p->rows = 1;
    p->listLeft = width / 5;
    p->listWidth = width * 3 / 5;
}

static void keep_visible(Pick *p)
{
    if (p->selected < p->top)
        p->top = p->selected;
    if (p->selected >= p->top + p->rows)
        p->top = p->selected - p->rows + 1;
}

static void move(Pick *p, int by)
{
    p->selected = (p->selected + by + p->count) % p->count;
    keep_visible(p);
}

static void paint(HWND wnd, Pick *p)
{
    PAINTSTRUCT ps;
    HDC screen = BeginPaint(wnd, &ps);
    RECT client;
    GetClientRect(wnd, &client);
    int w = client.right, h = client.bottom;
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    FillRect(dc, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetBkMode(dc, TRANSPARENT);

    HFONT big = CreateFontW(-h / 12, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Arial");
    HFONT row = CreateFontW(-p->rowHeight * 7 / 10, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Arial");
    HFONT small = CreateFontW(-h / 28, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Arial");

    HGDIOBJ oldFont = SelectObject(dc, big);
    SetTextColor(dc, RGB(255, 210, 0));
    RECT r = {0, h / 20, w, p->listTop};
    DrawTextW(dc, p->title, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(dc, row);
    for (int i = 0; i < p->rows && p->top + i < p->count; i++)
    {
        int n = p->top + i;
        RECT line = {p->listLeft, p->listTop + i * p->rowHeight, p->listLeft + p->listWidth, p->listTop + (i + 1) * p->rowHeight};
        if (n == p->selected)
        {
            HBRUSH b = CreateSolidBrush(RGB(255, 210, 0));
            FillRect(dc, &line, b);
            DeleteObject(b);
            SetTextColor(dc, RGB(0, 0, 0));
        }
        else
            SetTextColor(dc, RGB(230, 230, 230));
        wchar_t label[256];
        widen(p->labels[n], label, 256);
        DrawTextW(dc, label, -1, &line, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    // more above or below: arrows
    SetTextColor(dc, RGB(160, 160, 160));
    if (p->top > 0)
    {
        RECT a = {p->listLeft, p->listTop - p->rowHeight, p->listLeft + p->listWidth, p->listTop};
        DrawTextW(dc, L"\x25B2", -1, &a, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if (p->top + p->rows < p->count)
    {
        RECT a = {p->listLeft, p->listTop + p->rows * p->rowHeight, p->listLeft + p->listWidth, p->listTop + (p->rows + 1) * p->rowHeight};
        DrawTextW(dc, L"\x25BC", -1, &a, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(dc, small);
    SetTextColor(dc, RGB(200, 200, 200));
    RECT hint = {0, h * 82 / 100, w, h * 90 / 100};
    DrawTextW(dc, p->hint, -1, &hint, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT status = {0, h * 90 / 100, w, h * 96 / 100};
    DrawTextW(dc, p->status, -1, &status, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    BitBlt(screen, 0, 0, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldFont);
    DeleteObject(big);
    DeleteObject(row);
    DeleteObject(small);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(wnd, &ps);
}

static LRESULT CALLBACK pick_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Pick *p = g_pick;
    switch (msg)
    {
    case WM_PAINT:
        if (p)
        {
            paint(wnd, p);
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (p)
        {
            layout(p, LOWORD(lp), HIWORD(lp));
            keep_visible(p);
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (!p)
            break;
        if (wp == VK_UP || wp == VK_LEFT)
            move(p, -1);
        else if (wp == VK_DOWN || wp == VK_RIGHT)
            move(p, 1);
        else if (wp == VK_PRIOR)
            move(p, -p->rows);
        else if (wp == VK_NEXT)
            move(p, p->rows);
        else if (wp == VK_RETURN)
            p->chosen = p->selected;
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case WM_MOUSEWHEEL:
        if (p)
        {
            move(p, GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1);
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (p)
        {
            int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
            int i = (y - p->listTop) / p->rowHeight;
            if (y >= p->listTop && i < p->rows && p->top + i < p->count && x >= p->listLeft && x < p->listLeft + p->listWidth)
            {
                if (p->top + i == p->selected)
                    p->chosen = p->selected; // a click on the one selected chooses it
                p->selected = p->top + i;
                InvalidateRect(wnd, NULL, FALSE);
            }
        }
        return 0;
    case WM_CLOSE:
        if (p)
            p->closed = 1;
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

static int in_front(HWND wnd)
{
    return GetForegroundWindow() == wnd;
}

// In front, as display.cpp puts the game's window: allowed when the launcher passed its right on,
// else attached to the input of the window in front. 1 when done
static int to_front(HWND wnd)
{
    if (in_front(wnd))
        return 1;
    BringWindowToTop(wnd);
    if (!SetForegroundWindow(wnd) || !in_front(wnd))
    {
        HWND fg = GetForegroundWindow();
        DWORD fgThread = fg ? GetWindowThreadProcessId(fg, NULL) : 0, me = GetCurrentThreadId();
        if (fgThread && fgThread != me && AttachThreadInput(me, fgThread, TRUE))
        {
            BringWindowToTop(wnd);
            SetForegroundWindow(wnd);
            AttachThreadInput(me, fgThread, FALSE);
        }
    }
    SetFocus(wnd);
    return in_front(wnd);
}

// -1, 0 or 1: the direction the controls hold
static int direction(const WlPickControls *c)
{
    int d = 0;
    if (c->axis)
    {
        double centre = (c->axis->rawLow + c->axis->rawHigh) / 2.0, half = (c->axis->rawHigh - c->axis->rawLow) / 2.0;
        double v = half ? (wl_input_get(c->axis) - centre) / half : 0;
        if (v < -0.5)
            d = -1;
        else if (v > 0.5)
            d = 1;
    }
    if (c->up && wl_input_get(c->up) != c->up->rawLow)
        d = -1;
    if (c->down && wl_input_get(c->down) != c->down->rawLow)
        d = 1;
    return d;
}

int wl_pick_list(const char *title, const char *hint, const char *const *labels, int count, int selected, const WlPickControls *controls,
                 int timeout)
{
    if (count <= 0)
        return -1;
    Pick p = {};
    p.labels = labels;
    p.count = count;
    p.selected = selected >= 0 && selected < count ? selected : 0;
    p.chosen = -1;
    widen(title, p.title, 256);
    widen(hint, p.hint, 256);

    wl_busy_enter(); // windowsloader's own window, not the game's
    WNDCLASSW wc = {};
    wc.lpfnWndProc = pick_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"windowsloader-pick";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);
    int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
    g_pick = &p;
    layout(&p, w, h);
    keep_visible(&p);
    HWND wnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, p.title, WS_POPUP | WS_VISIBLE, 0, 0, w, h, NULL, NULL, wc.hInstance, NULL);
    if (!wnd)
    {
        wl_log("pick: its window not made (error %lu)", GetLastError());
        g_pick = NULL;
        wl_busy_leave();
        return -1;
    }
    int front = to_front(wnd);

    ULONGLONG start = GetTickCount64(), nextMove = 0, nextFront = start + 250;
    int held = 0, armed = 0, lastShown = -1;
    while (p.chosen < 0 && !p.closed)
    {
        // in front for the keyboard (the controls bound to keys too, controls.cpp): tried again for a
        // few seconds when Windows refused it
        if (!front && GetTickCount64() >= nextFront && GetTickCount64() - start < 5000)
        {
            nextFront = GetTickCount64() + 250;
            front = to_front(wnd);
        }
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        ULONGLONG now = GetTickCount64();
        int d = direction(controls);
        if (d && (d != held || now >= nextMove))
        {
            nextMove = now + (d != held ? REPEAT_FIRST : REPEAT_NEXT);
            move(&p, d);
            InvalidateRect(wnd, NULL, FALSE);
        }
        held = d;
        int start_ = controls->choose && wl_input_get(controls->choose) != controls->choose->rawLow;
        if (!start_)
            armed = 1; // Start seen released: a press from now chooses
        else if (armed)
            p.chosen = p.selected;
        if (timeout > 0)
        {
            int left = timeout - (int)((now - start) / 1000);
            if (left <= 0)
            {
                p.chosen = p.selected;
                wl_log("pick: no choice after %d s: the one selected taken", timeout);
            }
            else if (left != lastShown)
            {
                lastShown = left;
                swprintf(p.status, 128, L"%d s", left);
                InvalidateRect(wnd, NULL, FALSE);
            }
        }
        Sleep(16);
    }
    wl_log("pick: %s (the window %s in front)", p.closed ? "closed" : p.labels[p.chosen], in_front(wnd) ? "was" : "was not");
    DestroyWindow(wnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    g_pick = NULL;
    wl_busy_leave();
    return p.closed ? -1 : p.chosen;
}
