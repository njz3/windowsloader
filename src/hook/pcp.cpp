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

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "hooks.h"
#include "log.h"
#include "pcp.h"

struct Server
{
    unsigned short port;
    const char *name;
    WlPcpHandler handler;
    char last[256];
};

static Server g_servers[8];
static int g_serverCount;

const char *wl_pcp_value(const char *request, const char *keyword, char *value, size_t size)
{
    size_t n = strlen(keyword);
    value[0] = 0;
    for (const char *p = request; p && *p;)
    {
        if (!strncmp(p, keyword, n) && (p[n] == '=' || p[n] == '&' || !p[n]))
        {
            const char *v = p[n] == '=' ? p + n + 1 : p + n;
            size_t len = strcspn(v, "&");
            if (len >= size)
                len = size - 1;
            memcpy(value, v, len);
            value[len] = 0;
            return value;
        }
        p = strchr(p, '&');
        if (p)
            p++;
    }
    return value;
}

static void session(Server *srv, SOCKET s)
{
    char line[4096], response[4096];
    int used = 0;
    for (;;)
    {
        send(s, ">", 1, 0);
        // One request line
        int end = -1;
        while (end < 0)
        {
            for (int i = 0; i < used; i++)
                if (line[i] == '\n')
                {
                    end = i;
                    break;
                }
            if (end >= 0)
                break;
            if (used >= (int)sizeof line - 1)
                used = 0; // too long: dropped
            int got = recv(s, line + used, (int)sizeof line - 1 - used, 0);
            if (got <= 0)
            {
                if (used)
                    wl_log_hex("pcp: closed by the game, left unanswered:", line, used);
                return;
            }
            used += got;
        }
        int len = end;
        if (len > 0 && line[len - 1] == '\r')
            len--;
        char request[4096];
        memcpy(request, line, len);
        request[len] = 0;
        memmove(line, line + end + 1, used - end - 1);
        used -= end + 1;

        response[0] = 0;
        int keep = srv->handler(request, response, sizeof response - 2);
        // A request asked again and again (the game polls some) is logged once
        if (strcmp(request, srv->last))
            wl_log("pcp %s: \"%s\" -> \"%s\"", srv->name, request, response);
        snprintf(srv->last, sizeof srv->last, "%s", request);
        strcat(response, "\r\n");
        send(s, response, (int)strlen(response), 0);
        if (!keep)
            return;
    }
}

static DWORD WINAPI server_thread(LPVOID arg)
{
    Server *srv = (Server *)arg;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa))
        return 0;
    wl_busy_enter(); // the server's own sockets are not the game's
    SOCKET l = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL exclusive = TRUE;
    setsockopt(l, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive, sizeof exclusive);
    sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(srv->port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (l == INVALID_SOCKET || bind(l, (const sockaddr *)&a, sizeof a) || listen(l, 4))
    {
        wl_log("pcp %s: cannot listen on 127.0.0.1:%u (error %d)", srv->name, srv->port, WSAGetLastError());
        return 0;
    }
    wl_log("pcp %s: listening on 127.0.0.1:%u", srv->name, srv->port);
    for (;;)
    {
        SOCKET s = accept(l, NULL, NULL);
        if (s == INVALID_SOCKET)
        {
            Sleep(100);
            continue;
        }
        wl_log("pcp %s: the game is connected", srv->name);
        session(srv, s);
        closesocket(s);
        wl_log("pcp %s: closed", srv->name);
    }
}

void wl_pcp_serve(unsigned short port, const char *name, WlPcpHandler handler)
{
    if (g_serverCount >= (int)(sizeof g_servers / sizeof g_servers[0]))
        return;
    Server *srv = &g_servers[g_serverCount++];
    srv->port = port;
    srv->name = name;
    srv->handler = handler;
    CloseHandle(CreateThread(NULL, 0, server_thread, srv, 0, NULL));
}
