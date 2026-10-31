// windowsloader - runs Windows arcade games with emulated cabinet hardware
// Copyright (C) 2026 Benjamin Maurin - njz3
// License: GNU General Public License version 3 (GPL v3)
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE
// file for the full text. The author reserves the right to change the license of this code.

// The link (link.h): [Game.<name>] LINK lists the cabinets' PCs (their real addresses), the same
// line on each; CABINET is this one's number (0: its PC's place in LINK). The game's own sockets
// (its import table) then talk to those PCs only, at their real addresses:
//   sendto a broadcast (255.255.255.255, x.x.x.255)  -> each PC of LINK (its own too), the same port
//   sendto, connect a PC of LINK, or 127.x           -> as asked
//   sendto, connect any other address                -> refused, as system.cpp does without LINK
//   recvfrom another address                         -> not heard (the next datagram read)
//   the PC's cards (WSAIoctl SIO_GET_INTERFACE_LIST, GetAdaptersInfo, GetAdaptersAddresses)
//                                                    -> the card holding this PC's address of LINK,
//                                                       and the loopback (a cabinet's VPN left out)
// The log notes each bind, listen, connect and accept, the first packets with each PC on each
// local port (ports, size, first bytes), counts the traffic with each PC every 10 s, and the TCP connections that end.
// Its calls go through system.cpp's hooks as windowsloader's own, which let them by.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "hooks.h"
#include "link.h"
#include "log.h"

#define LINK_MAX 8

static int g_count, g_cabinet = 1;
static ULONG g_address[LINK_MAX]; // network order, cabinet 1's first

int wl_link_count(void)
{
    return g_count;
}

int wl_link_cabinet(void)
{
    return g_cabinet;
}

static const char *ip_text(ULONG ip, char *buf, size_t size)
{
    snprintf(buf, size, "%lu.%lu.%lu.%lu", ip & 0xFF, ip >> 8 & 0xFF, ip >> 16 & 0xFF, ip >> 24);
    return buf;
}

static void link_patch(HMODULE exe);

int wl_link_settings(HMODULE exe)
{
    char list[256];
    wl_game_option("LINK", list, sizeof list);
    int cabinet = wl_game_option_int("CABINET");
    g_count = 0;
    char *next = NULL;
    for (char *p = strtok_s(list, ",; ", &next); p && g_count < LINK_MAX; p = strtok_s(NULL, ",; ", &next))
    {
        unsigned b[4];
        char end;
        if (sscanf_s(p, "%u.%u.%u.%u%c", &b[0], &b[1], &b[2], &b[3], &end, 1) != 4 || b[0] > 255 || b[1] > 255 || b[2] > 255 || b[3] > 255 ||
            !b[0])
            wl_log("link: \"%s\" is not an address, left out", p);
        else
            g_address[g_count++] = b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24;
    }
    if (g_count && cabinet < 1)
    {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        wl_busy_enter();
        for (int i = 0; i < g_count && cabinet < 1; i++)
        {
            SOCKET t = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            sockaddr_in a = {};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = g_address[i];
            if (t != INVALID_SOCKET && !bind(t, (const sockaddr *)&a, sizeof a))
                cabinet = i + 1;
            if (t != INVALID_SOCKET)
                closesocket(t);
        }
        wl_busy_leave();
        WSACleanup();
        if (cabinet < 1)
            wl_log("link: none of LINK's addresses is this PC's (CABINET not set): the cabinet stays alone");
    }
    if (g_count && (cabinet < 1 || cabinet > g_count))
    {
        if (cabinet >= 1)
            wl_log("link: CABINET %d is not one of LINK's %d addresses: the cabinet stays alone", cabinet, g_count);
        g_count = 0;
    }
    g_cabinet = g_count ? cabinet : 1;
    char t[20];
    if (g_count)
    {
        wl_log("link: cabinet %d of %d, its PC %s; the game talks to LINK's PCs only", g_cabinet, g_count, ip_text(g_address[g_cabinet - 1], t, sizeof t));
        link_patch(exe);
    }
    return g_count;
}

// ------------------------------------------------------------------------------
// Addresses and the log
// ------------------------------------------------------------------------------

static int is_inet(const sockaddr *a, int len)
{
    return a && len >= (int)sizeof(sockaddr_in) && a->sa_family == AF_INET;
}

// The PC of LINK an address is, 1.., 0 for none
static int pc_of(ULONG ip)
{
    for (int i = 0; i < g_count; i++)
        if (g_address[i] == ip)
            return i + 1;
    return 0;
}

static int local(ULONG ip)
{
    return (ip & 0xFF) == 127 || !ip;
}

static int broadcast(ULONG ip)
{
    return ip == INADDR_BROADCAST || (ip >> 24) == 0xFF;
}

static volatile LONG g_sent[LINK_MAX], g_sentBytes[LINK_MAX], g_got[LINK_MAX], g_gotBytes[LINK_MAX];

static void count(int pc, int sent, int bytes)
{
    if (pc < 1 || pc > LINK_MAX || bytes < 0)
        return;
    InterlockedIncrement(sent ? &g_sent[pc - 1] : &g_got[pc - 1]);
    InterlockedExchangeAdd(sent ? &g_sentBytes[pc - 1] : &g_gotBytes[pc - 1], bytes);
    static volatile LONG last;
    LONG now = (LONG)GetTickCount(), was = last;
    if (now - was < 10000 || InterlockedCompareExchange(&last, now, was) != was)
        return;
    char line[600];
    int at = snprintf(line, sizeof line, "link: traffic since the last count:");
    for (int i = 0; i < g_count && at < (int)sizeof line; i++)
        at += snprintf(line + at, sizeof line - at, " cabinet %d: sent %ld (%ld bytes), received %ld (%ld bytes);", i + 1,
                       InterlockedExchange(&g_sent[i], 0), InterlockedExchange(&g_sentBytes[i], 0), InterlockedExchange(&g_got[i], 0),
                       InterlockedExchange(&g_gotBytes[i], 0));
    wl_log("%s", line);
}

// The first packets with each PC, each way and on each local port (a game's later sessions open
// ports of their own: DiRT Showdown's race on 1000 after its cabinets' 1001): ports, size, first bytes
#define LINK_PORTS 16
static void packet(const char *what, int pc, const sockaddr_in *peer, SOCKET s, const char *data, int n)
{
    static volatile LONG ports[LINK_MAX][2][LINK_PORTS], logged[LINK_MAX][2][LINK_PORTS];
    if (pc < 1 || pc > LINK_MAX)
        return;
    sockaddr_in me = {};
    int len = sizeof me;
    wl_busy_enter();
    getsockname(s, (sockaddr *)&me, &len);
    wl_busy_leave();
    LONG port = ntohs(me.sin_port) + 1; // 0: a free slot
    int way = what[0] == 's', i = 0;
    for (; i < LINK_PORTS; i++)
    {
        LONG was = InterlockedCompareExchange(&ports[pc - 1][way][i], port, 0);
        if (!was || was == port)
            break;
    }
    if (i == LINK_PORTS || InterlockedIncrement(&logged[pc - 1][way][i]) > 25)
        return;
    char hex[3 * 16 + 1] = "", t[20];
    for (int i = 0; data && i < n && i < 16; i++)
        snprintf(hex + 3 * i, sizeof hex - 3 * i, "%02x ", (unsigned char)data[i]);
    wl_log("link: %s cabinet %d (%s:%u), local port %u: %d bytes %s", what, pc, ip_text(peer->sin_addr.s_addr, t, sizeof t), ntohs(peer->sin_port),
           ntohs(me.sin_port), n, hex);
}

static void refused(const char *what, const sockaddr_in *a)
{
    static volatile LONG logged;
    char t[20];
    if (InterlockedIncrement(&logged) <= 30)
        wl_log("link: %s %s:%u refused: not a PC of LINK", what, ip_text(a->sin_addr.s_addr, t, sizeof t), ntohs(a->sin_port));
}

// TCP sockets with a PC of LINK, for the counts and the ends
#define SOCKETS 64
static struct
{
    volatile SOCKET s;
    volatile LONG pc;
} g_sockets[SOCKETS];

static void socket_pc(SOCKET s, int pc)
{
    for (int i = 0; i < SOCKETS; i++)
        if (!g_sockets[i].pc || g_sockets[i].s == s)
        {
            g_sockets[i].s = s;
            g_sockets[i].pc = pc;
            return;
        }
}

static int pc_socket(SOCKET s)
{
    for (int i = 0; i < SOCKETS; i++)
        if (g_sockets[i].pc && g_sockets[i].s == s)
            return g_sockets[i].pc;
    return 0;
}

static void tcp_end(SOCKET s, int pc, const char *what, DWORD err)
{
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 100)
        wl_log("link: TCP with cabinet %d (socket %Iu): %s (%lu)", pc, (ULONG_PTR)s, what, err);
}

// ------------------------------------------------------------------------------
// The game's sockets
// ------------------------------------------------------------------------------

static int(WSAAPI *o_bind)(SOCKET, const sockaddr *, int);
static int WSAAPI l_bind(SOCKET s, const sockaddr *a, int len)
{
    int r = o_bind(s, a, len);
    DWORD err = WSAGetLastError();
    if (g_count && is_inet(a, len))
    {
        char t[20];
        wl_log("link: bind %s:%u -> %d (%lu)", ip_text(((const sockaddr_in *)a)->sin_addr.s_addr, t, sizeof t), ntohs(((const sockaddr_in *)a)->sin_port), r,
               r ? err : 0);
    }
    WSASetLastError(err);
    return r;
}

static int(WSAAPI *o_listen)(SOCKET, int);
static int WSAAPI l_listen(SOCKET s, int backlog)
{
    int r = o_listen(s, backlog);
    DWORD err = WSAGetLastError();
    if (g_count)
    {
        sockaddr_in me = {};
        int len = sizeof me;
        wl_busy_enter();
        getsockname(s, (sockaddr *)&me, &len);
        wl_busy_leave();
        wl_log("link: listen on port %u -> %d (%lu)", ntohs(me.sin_port), r, r ? err : 0);
    }
    WSASetLastError(err);
    return r;
}

static int(WSAAPI *o_connect)(SOCKET, const sockaddr *, int);
static int WSAAPI l_connect(SOCKET s, const sockaddr *a, int len)
{
    if (!g_count || !is_inet(a, len))
        return o_connect(s, a, len);
    const sockaddr_in *to = (const sockaddr_in *)a;
    int pc = pc_of(to->sin_addr.s_addr);
    if (!pc && !local(to->sin_addr.s_addr))
    {
        refused("connect", to);
        WSASetLastError(WSAENETUNREACH);
        return SOCKET_ERROR;
    }
    int r = o_connect(s, a, len);
    DWORD err = WSAGetLastError();
    if (pc)
    {
        if (r == 0 || err == WSAEWOULDBLOCK)
            socket_pc(s, pc);
        static volatile LONG logged[LINK_MAX];
        char t[20];
        if (r == 0 || err == WSAEWOULDBLOCK || InterlockedIncrement(&logged[pc - 1]) <= 10)
            wl_log("link: connect cabinet %d (%s:%u) -> %d (%lu)", pc, ip_text(to->sin_addr.s_addr, t, sizeof t), ntohs(to->sin_port), r, r ? err : 0);
    }
    WSASetLastError(err);
    return r;
}

static SOCKET(WSAAPI *o_accept)(SOCKET, sockaddr *, int *);
static SOCKET WSAAPI l_accept(SOCKET s, sockaddr *a, int *len)
{
    sockaddr_in from = {};
    int fromLen = sizeof from;
    SOCKET r = o_accept(s, a ? a : (sockaddr *)&from, a ? len : &fromLen);
    if (r == INVALID_SOCKET || !g_count)
        return r;
    const sockaddr_in *peer = a ? (const sockaddr_in *)a : &from;
    if ((a ? len && is_inet(a, *len) : 1) && peer->sin_family == AF_INET)
    {
        int pc = pc_of(peer->sin_addr.s_addr);
        char t[20];
        wl_log("link: accept %s:%u (%s)", ip_text(peer->sin_addr.s_addr, t, sizeof t), ntohs(peer->sin_port), pc ? "a PC of LINK" : "not of LINK");
        if (pc)
            socket_pc(r, pc);
    }
    return r;
}

static int(WSAAPI *o_sendto)(SOCKET, const char *, int, int, const sockaddr *, int);
static int WSAAPI l_sendto(SOCKET s, const char *buf, int n, int flags, const sockaddr *a, int len)
{
    if (!g_count || !is_inet(a, len))
        return o_sendto(s, buf, n, flags, a, len);
    sockaddr_in to;
    memcpy(&to, a, sizeof to);
    ULONG ip = to.sin_addr.s_addr;
    if (broadcast(ip))
    {
        // Each PC of LINK, its own too (a broadcast comes back to its sender)
        int r = n;
        DWORD err = 0;
        for (int i = 0; i < g_count; i++)
        {
            to.sin_addr.s_addr = g_address[i];
            int sent = o_sendto(s, buf, n, flags, (const sockaddr *)&to, sizeof to);
            if (sent < 0)
                err = WSAGetLastError();
            count(i + 1, 1, sent);
            packet("sent (broadcast) to", i + 1, &to, s, buf, sent);
        }
        WSASetLastError(err);
        return r;
    }
    int pc = pc_of(ip);
    if (!pc && !local(ip))
    {
        refused("sendto", &to);
        return n; // lost, as on a network
    }
    int r = o_sendto(s, buf, n, flags, a, len);
    DWORD err = WSAGetLastError();
    if (pc)
    {
        count(pc, 1, r);
        packet("sent to", pc, &to, s, buf, r);
    }
    WSASetLastError(err);
    return r;
}

static int(WSAAPI *o_recvfrom)(SOCKET, char *, int, int, sockaddr *, int *);
static int WSAAPI l_recvfrom(SOCKET s, char *buf, int n, int flags, sockaddr *from, int *fromLen)
{
    for (int tries = 0;; tries++)
    {
        sockaddr_in own = {};
        int ownLen = sizeof own;
        sockaddr *f = from ? from : (sockaddr *)&own;
        int *fl = from ? fromLen : &ownLen;
        int r = o_recvfrom(s, buf, n, flags, f, fl);
        if (r < 0 || !g_count || !fl || !is_inet(f, *fl))
            return r;
        ULONG ip = ((const sockaddr_in *)f)->sin_addr.s_addr;
        int pc = pc_of(ip);
        if (pc || local(ip) || tries >= 16)
        {
            DWORD err = WSAGetLastError();
            if (pc)
            {
                count(pc, 0, r);
                packet("received from", pc, (const sockaddr_in *)f, s, buf, r);
            }
            WSASetLastError(err);
            return r;
        }
        // Another host of the local network: not heard
        static volatile LONG logged;
        char t[20];
        if (InterlockedIncrement(&logged) <= 20)
            wl_log("link: a datagram from %s (not a PC of LINK) not heard", ip_text(ip, t, sizeof t));
    }
}

static int(WSAAPI *o_send)(SOCKET, const char *, int, int);
static int WSAAPI l_send(SOCKET s, const char *buf, int n, int flags)
{
    int r = o_send(s, buf, n, flags);
    int pc = g_count ? pc_socket(s) : 0;
    if (pc)
    {
        DWORD err = WSAGetLastError();
        if (r >= 0)
            count(pc, 1, r);
        else if (err != WSAEWOULDBLOCK)
            tcp_end(s, pc, "send failed", err);
        WSASetLastError(err);
    }
    return r;
}

static int(WSAAPI *o_recv)(SOCKET, char *, int, int);
static int WSAAPI l_recv(SOCKET s, char *buf, int n, int flags)
{
    int r = o_recv(s, buf, n, flags);
    int pc = g_count ? pc_socket(s) : 0;
    if (pc)
    {
        DWORD err = WSAGetLastError();
        if (r > 0)
            count(pc, 0, r);
        else if (!r)
            tcp_end(s, pc, "closed by the other cabinet", 0);
        else if (err != WSAEWOULDBLOCK && err != WSAETIMEDOUT)
            tcp_end(s, pc, "recv failed", err);
        WSASetLastError(err);
    }
    return r;
}

static int(WSAAPI *o_closesocket)(SOCKET);
static int WSAAPI l_closesocket(SOCKET s)
{
    if (int pc = g_count ? pc_socket(s) : 0)
    {
        tcp_end(s, pc, "closed by this cabinet", 0);
        for (int i = 0; i < SOCKETS; i++)
            if (g_sockets[i].pc && g_sockets[i].s == s)
                g_sockets[i].pc = 0;
    }
    return o_closesocket(s);
}

// ------------------------------------------------------------------------------
// The PC's cards: the one of LINK
// ------------------------------------------------------------------------------

static ULONG own(void)
{
    return g_count ? g_address[g_cabinet - 1] : 0;
}

static int(WSAAPI *o_WSAIoctl)(SOCKET, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
static int WSAAPI l_WSAIoctl(SOCKET s, DWORD code, LPVOID in, DWORD inSize, LPVOID out, DWORD outSize, LPDWORD returned, LPWSAOVERLAPPED ov,
                             LPWSAOVERLAPPED_COMPLETION_ROUTINE done)
{
    int r = o_WSAIoctl(s, code, in, inSize, out, outSize, returned, ov, done);
    if (r || code != SIO_GET_INTERFACE_LIST || !g_count || !out || !returned)
        return r;
    INTERFACE_INFO *list = (INTERFACE_INFO *)out;
    int n = (int)(*returned / sizeof(INTERFACE_INFO)), kept = 0, ours = 0;
    for (int i = 0; i < n; i++)
    {
        ULONG ip = list[i].iiAddress.AddressIn.sin_addr.s_addr;
        ours |= ip == own();
        if (ip == own() || local(ip))
            list[kept++] = list[i];
    }
    if (!ours)
        return r; // its card not found: left as Windows gives it
    *returned = kept * sizeof(INTERFACE_INFO);
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 3)
        wl_log("link: the PC's cards (SIO_GET_INTERFACE_LIST): %d of %d shown, this PC's of LINK and the loopback", kept, n);
    return r;
}

static DWORD(WINAPI *o_GetAdaptersInfo)(PIP_ADAPTER_INFO, PULONG);
static DWORD WINAPI l_GetAdaptersInfo(PIP_ADAPTER_INFO info, PULONG size)
{
    DWORD r = o_GetAdaptersInfo(info, size);
    if (r != NO_ERROR || !g_count || !info)
        return r;
    char t[20];
    ip_text(own(), t, sizeof t);
    PIP_ADAPTER_INFO mine = NULL;
    int n = 0;
    for (PIP_ADAPTER_INFO a = info; a; a = a->Next, n++)
        for (PIP_ADDR_STRING ip = &a->IpAddressList; ip && !mine; ip = ip->Next)
            if (!strcmp(ip->IpAddress.String, t))
                mine = a;
    if (!mine)
        return r;
    if (mine != info)
        memcpy(info, mine, sizeof *info);
    info->Next = NULL;
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 3)
        wl_log("link: the PC's cards (GetAdaptersInfo): the one of %s shown, %d left out", t, n - 1);
    return r;
}

static ULONG(WINAPI *o_GetAdaptersAddresses)(ULONG, ULONG, PVOID, PIP_ADAPTER_ADDRESSES, PULONG);
static ULONG WINAPI l_GetAdaptersAddresses(ULONG family, ULONG flags, PVOID reserved, PIP_ADAPTER_ADDRESSES list, PULONG size)
{
    ULONG r = o_GetAdaptersAddresses(family, flags, reserved, list, size);
    if (r != NO_ERROR || !g_count || !list)
        return r;
    PIP_ADAPTER_ADDRESSES mine = NULL, loop = NULL;
    int n = 0;
    for (PIP_ADAPTER_ADDRESSES a = list; a; a = a->Next, n++)
    {
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK && !loop)
            loop = a;
        for (PIP_ADAPTER_UNICAST_ADDRESS u = a->FirstUnicastAddress; u && !mine; u = u->Next)
            if (u->Address.lpSockaddr && u->Address.lpSockaddr->sa_family == AF_INET &&
                ((const sockaddr_in *)u->Address.lpSockaddr)->sin_addr.s_addr == own())
                mine = a;
    }
    if (!mine)
        return r;
    if (mine != list)
    {
        // The first entry made this PC's card (the list's start is the caller's)
        IP_ADAPTER_ADDRESSES copy = *mine;
        *list = copy;
    }
    list->Next = loop && loop != mine && loop != list ? loop : NULL;
    if (list->Next)
        list->Next->Next = NULL;
    static volatile LONG logged;
    char t[20];
    if (InterlockedIncrement(&logged) <= 3)
        wl_log("link: the PC's cards (GetAdaptersAddresses): the one of %s shown%s, of %d", ip_text(own(), t, sizeof t), list->Next ? " and the loopback" : "", n);
    return r;
}

// ------------------------------------------------------------------------------

static int patch(HMODULE exe, const char *dll, int ordinal, const char *name, void *fn, void **original)
{
    return (ordinal && wl_patch_iat(exe, dll, (const char *)(ULONG_PTR)ordinal, fn, original)) || wl_patch_iat(exe, dll, name, fn, original);
}

static void link_patch(HMODULE exe)
{
    static const struct
    {
        const char *dll;
        int ordinal;
        const char *name;
        void *fn, **original;
    } table[] = {
        {"WS2_32.dll", 2, "bind", (void *)l_bind, (void **)&o_bind},
        {"WS2_32.dll", 13, "listen", (void *)l_listen, (void **)&o_listen},
        {"WS2_32.dll", 4, "connect", (void *)l_connect, (void **)&o_connect},
        {"WS2_32.dll", 1, "accept", (void *)l_accept, (void **)&o_accept},
        {"WS2_32.dll", 20, "sendto", (void *)l_sendto, (void **)&o_sendto},
        {"WS2_32.dll", 17, "recvfrom", (void *)l_recvfrom, (void **)&o_recvfrom},
        {"WS2_32.dll", 19, "send", (void *)l_send, (void **)&o_send},
        {"WS2_32.dll", 16, "recv", (void *)l_recv, (void **)&o_recv},
        {"WS2_32.dll", 3, "closesocket", (void *)l_closesocket, (void **)&o_closesocket},
        {"WS2_32.dll", 0, "WSAIoctl", (void *)l_WSAIoctl, (void **)&o_WSAIoctl},
        {"IPHLPAPI.DLL", 0, "GetAdaptersInfo", (void *)l_GetAdaptersInfo, (void **)&o_GetAdaptersInfo},
        {"IPHLPAPI.DLL", 0, "GetAdaptersAddresses", (void *)l_GetAdaptersAddresses, (void **)&o_GetAdaptersAddresses},
    };
    char found[256] = "";
    int at = 0;
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (patch(exe, table[i].dll, table[i].ordinal, table[i].name, table[i].fn, table[i].original))
            at += snprintf(found + at, sizeof found - at, "%s%s", at ? " " : "", table[i].name);
    wl_log("link: the game's network functions taken: %s", found[0] ? found : "none");
}
