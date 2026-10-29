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

// The agent mode's commands, a subset of linuxloader's (same names and answers). Once the game
// draws its first frame: "ready GAME W H".
//
//   frame                  "frame N", the frames presented
//   step [N]               wait for N frames (1 without N): "ok FRAME". The game is not held
//                          between steps: it runs at its own pace.
//   shot PATH              the next frame as a BMP: "ok" or "err ..."
//   display [ASPECT] [MODE] [KEY=VALUE...] set the aspect ratio, the display mode (windowsloader.ini
//                          names, ORIGINAL, 4:3_CROP, FULLSCREEN_STRETCH...) and the crop's
//                          CROP_POSITION, CROP_BARREL, BARREL_CENTER, BARREL_RADIUS, not saved:
//                          "display ASPECT MODE"
//   screen PATH            what the window shows (the picture placed by the aspect ratio and the
//                          display mode, black bars), as a BMP: "ok" or "err ..."
//   inputs                 "inputs N", then N lines "NAME TYPE" (axis, analog or switch)
//   set INPUT VALUE        hold an input: axis -1..1 (+1 fully left), analog 0..1, switch 0/1
//   setraw INPUT RAW       hold the raw value the game reads
//   release INPUT|all      give it back to the controllers
//   get INPUT              "value RAW"
//   ffb                    the force feedback since the last ffb: "ffb N", then N lines
//                          "FRAME Steering constant FORCE", FORCE -1..1, > 0 pushing left
//   menu [on|off]          open or close the settings menu; "menu open" or "menu closed"
//   menu tab N             show its page N (0 game, 1 controls, 2 display, 3 force feedback, 4 about,
//                          5 game options, 6 outputs and network)
//   menu map [N]           on the controls page, open device N's bindings (0 the keyboard, then the
//                          controllers in the list's order); without N, close them
//   menu save [game]       its button "Sauvegarder" (or "Sauvegarder pour ce jeu"): "ok MESSAGE" or
//                          "err MESSAGE"
//   menu outputs on|off    the "Sorties et réseau" page's box: the network outputs' server started or
//                          stopped at once, OUTPUTS_NETWORK written: "ok MESSAGE" or "err MESSAGE"
//   quit                   "bye", and the game ends

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "display.h"
#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "log.h"
#include "menu.h"
#include "wlshared.h"

static HANDLE g_pipe = INVALID_HANDLE_VALUE;

static void answer(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n > (int)sizeof buf - 2)
        n = sizeof buf - 2;
    buf[n++] = '\n';
    DWORD written;
    WriteFile(g_pipe, buf, (DWORD)n, &written, NULL);
}

static const char *type_name(WlInputType t)
{
    return t == WL_INPUT_AXIS ? "axis" : t == WL_INPUT_ANALOG ? "analog" : "switch";
}

static void command(char *line)
{
    char *args = line;
    while (*args && *args != ' ')
        args++;
    if (*args)
        *args++ = 0;
    char a1[256] = "", a2[64] = "";
    sscanf(args, "%255s %63s", a1, a2);

    if (!strcmp(line, "frame"))
        answer("frame %u", wl_d3d9_frames());
    else if (!strcmp(line, "step"))
    {
        unsigned n = a1[0] ? (unsigned)atoi(a1) : 1;
        unsigned target = wl_d3d9_frames() + n;
        ULONGLONG start = GetTickCount64();
        while (wl_d3d9_frames() < target && GetTickCount64() - start < 60000)
            Sleep(1);
        answer("ok %u", wl_d3d9_frames());
    }
    else if (!strcmp(line, "shot"))
    {
        if (!a1[0])
            answer("err shot needs a path");
        else if (wl_d3d9_shot(args, 5000))
            answer("ok");
        else
            answer("err no frame or unsupported back buffer");
    }
    else if (!strcmp(line, "display"))
    {
        int aspect = wl_display_aspect(), mode = wl_display_mode(), bad = 0;
        int position, barrel, center, radius;
        wl_display_crop(&position, &barrel, &center, &radius);
        char words[6][64] = {};
        int count = sscanf(args, "%63s %63s %63s %63s %63s %63s", words[0], words[1], words[2], words[3], words[4], words[5]);
        for (int i = 0; i < count; i++)
        {
            int a = wl_display_aspect_by_name(words[i]), m = wl_display_mode_by_name(words[i]);
            char *equals = strchr(words[i], '=');
            if (a >= 0)
                aspect = a;
            else if (m >= 0)
                mode = m;
            else if (equals && !_strnicmp(words[i], "CROP_POSITION=", 14))
                position = atoi(equals + 1);
            else if (equals && !_strnicmp(words[i], "CROP_BARREL=", 12))
                barrel = !_stricmp(equals + 1, "true") || atoi(equals + 1);
            else if (equals && !_strnicmp(words[i], "BARREL_CENTER=", 14))
                center = atoi(equals + 1);
            else if (equals && !_strnicmp(words[i], "BARREL_RADIUS=", 14))
                radius = atoi(equals + 1);
            else
                bad = 1;
        }
        if (bad)
            answer("err unknown aspect ratio, display mode or crop setting");
        else
        {
            if (count > 0)
            {
                wl_display_set(aspect, mode);
                wl_display_set_crop(position, barrel, center, radius);
                unsigned target = wl_d3d9_frames() + 3; // applied on the next frames
                ULONGLONG start = GetTickCount64();
                while (wl_d3d9_frames() < target && GetTickCount64() - start < 5000)
                    Sleep(5);
            }
            answer("display %s %s", wl_display_aspect_name(wl_display_aspect()), wl_display_mode_name(wl_display_mode()));
        }
    }
    else if (!strcmp(line, "screen"))
    {
        if (!a1[0])
            answer("err screen needs a path");
        else if (wl_display_screenshot(args))
            answer("ok");
        else
            answer("err no window");
    }
    else if (!strcmp(line, "inputs"))
    {
        answer("inputs %d", wl_inputs_count());
        for (int i = 0; i < wl_inputs_count(); i++)
            answer("%s %s", wl_input_at(i)->name, type_name(wl_input_at(i)->type));
    }
    else if (!strcmp(line, "set") || !strcmp(line, "setraw"))
    {
        WlInput *in = wl_input_find(a1);
        if (!in || !a2[0])
            answer("err unknown input or missing value");
        else
        {
            wl_input_hold(in, !strcmp(line, "set") ? wl_input_raw_of(in, atof(a2)) : atoi(a2));
            answer("ok");
        }
    }
    else if (!strcmp(line, "release"))
    {
        WlInput *in = wl_input_find(a1);
        if (!strcmp(a1, "all"))
            wl_inputs_release_all();
        else if (in)
            wl_input_release(in);
        if (in || !strcmp(a1, "all"))
            answer("ok");
        else
            answer("err unknown input");
    }
    else if (!strcmp(line, "get"))
    {
        WlInput *in = wl_input_find(a1);
        if (in)
            answer("value %d", wl_input_get(in));
        else
            answer("err unknown input");
    }
    else if (!strcmp(line, "ffb"))
    {
        WlFfbEvent events[512];
        int n = wl_ffb_drain(events, 512);
        answer("ffb %d", n);
        for (int i = 0; i < n; i++)
            answer("%u Steering constant %.4f", events[i].frame, events[i].force);
    }
    else if (!strcmp(line, "menu"))
    {
        if (!strcmp(a1, "tab"))
            wl_menu_request_tab(atoi(a2));
        else if (!strcmp(a1, "map"))
            wl_menu_request_map(a2[0] ? atoi(a2) : -1);
        else if (!strcmp(a1, "save"))
        {
            char status[MAX_PATH * 2 + 64];
            int ok = wl_menu_save(!strcmp(a2, "game"), status, sizeof status);
            answer("%s %s", ok ? "ok" : "err", status);
            return;
        }
        else if (!strcmp(a1, "outputs"))
        {
            char status[MAX_PATH * 2 + 96];
            int ok = (!strcmp(a2, "on") || !strcmp(a2, "off")) && wl_menu_outputs(!strcmp(a2, "on"), status, sizeof status);
            answer("%s %s", ok ? "ok" : "err", ok || a2[0] ? status : "on or off");
            return;
        }
        else if (a1[0])
        {
            int open = !strcmp(a1, "on");
            wl_menu_request(open);
            ULONGLONG start = GetTickCount64();
            while (wl_menu_is_open() != open && GetTickCount64() - start < 5000)
                Sleep(10);
        }
        answer("menu %s", wl_menu_is_open() ? "open" : "closed");
    }
    else if (!strcmp(line, "quit"))
    {
        answer("bye");
        wl_log("agent: quit");
        TerminateProcess(GetCurrentProcess(), 0);
    }
    else
        answer("err unknown command %s", line);
}

static DWORD WINAPI agent_thread(LPVOID)
{
    wl_busy_enter(); // the agent's own pipe I/O goes through the hooks untouched

    char name[64];
    snprintf(name, sizeof name, "\\\\.\\pipe\\windowsloader-%lu", GetCurrentProcessId());
    g_pipe = CreateNamedPipeA(name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 1 << 16, 1 << 16, 0, NULL);
    if (g_pipe == INVALID_HANDLE_VALUE)
    {
        wl_log("agent: cannot create %s (error %lu)", name, GetLastError());
        return 0;
    }
    if (!ConnectNamedPipe(g_pipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED)
    {
        wl_log("agent: no client on %s (error %lu)", name, GetLastError());
        return 0;
    }
    wl_log("agent: connected on %s", name);

    // As the other agent modes: "ready GAME W H" once the game draws
    while (!wl_d3d9_frames())
        Sleep(10);
    int w, h;
    wl_d3d9_size(&w, &h);
    answer("ready %s %d %d", wl_game_name(), w, h);

    char line[4096], buf[1024];
    int len = 0;
    DWORD rd;
    while (ReadFile(g_pipe, buf, sizeof buf, &rd, NULL) && rd)
    {
        for (DWORD i = 0; i < rd; i++)
        {
            char c = buf[i];
            if (c == '\r')
                continue;
            if (c != '\n')
            {
                if (len < (int)sizeof line - 1)
                    line[len++] = c;
                continue;
            }
            line[len] = 0;
            len = 0;
            if (line[0])
                command(line);
        }
    }
    wl_log("agent: client gone");
    return 0;
}

void wl_agent_init(void)
{
    wchar_t v[8];
    if (!GetEnvironmentVariableW(WL_ENV_AGENT, v, 8) || v[0] != L'1')
        return;
    HANDLE t = CreateThread(NULL, 0, agent_thread, NULL, 0, NULL);
    if (t)
        CloseHandle(t);
}
