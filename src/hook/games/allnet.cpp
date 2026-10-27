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

// ALL.Net, Sega's network service, and Aime's server, played on this PC for an ALLS's amdaemon.exe
// (Sega World Drivers Championship, alls.cpp), in amdaemon's process, and the game's title server
// ("ABaaS"), in the game's:
// - the names amdaemon looks up (DnsQuery_A, DnsQueryEx and getaddrinfo, its import table) are
//   answered as segatools' hooks/dns.c (Unlicense) answers them: the name is replaced by an address
//   and the query made with it. naominet.jp, ib.naominet.jp and aime.naominet.jp (or in ROT13) are 192.0.2.1
//   (TEST-NET-1, RFC 5737: never routed; ALL.Net's library refuses 0.0.0.0/8, 127.0.0.0/8 and
//   240.0.0.0/4), the shop's router and its broadband line (tenporouter.loc, bbrouter.loc) the
//   cabinet's gateway (netenv.cpp); the mobile and DSL lines' routers are not found;
// - its connections to 192.0.2.1:80 (ALL.Net's HTTP) go to the server below, on 127.0.0.1:40180,
//   those to 192.0.2.1:22345 to the Aime server, on 127.0.0.1:40182;
// - that server answers POST /sys/servlet/PowerOn, ALL.Net's authentication, with the fields
//   ARTEMiS's core/allnet.py gives (its fields, not its code); with "Pragma: DFI" the body is zlib
//   then base64, both ways. The title server it gives the game (uri, a host: the game's library
//   makes "http://" uri ":80" of it, +0x2626f28) is 192.0.2.1 too; the game's connections to
//   192.0.2.1:80 go to its own server, on 127.0.0.1:40181 (wl_allnet_game);
// - the title server, in the game's process (below), answers its startup's requests: the
//   season's, events' and settings' data (/api/data/load), empty rankings, the rest acknowledged;
// - the Aime server (below) answers amdaemon as a server that knows no card.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "hooks.h"
#include "log.h"

#define SERVER_PORT 40180 // ALL.Net, in amdaemon
#define TITLE_PORT 40181  // the title server, in the game
#define AIME_PORT 40182   // the Aime server, in amdaemon

static int g_serverPort = SERVER_PORT; // this process's server's
#define ALLNET_ADDRESS "192.0.2.1"     // ALL.Net's names' answer
#define ALLNET_ADDRESS_N 0x010200C0u // the same, in network order

static char g_router[16]; // the cabinet's gateway, "192.168.172.1"

// ------------------------------------------------------------------------------
// Base64 and zlib (RFC 1950 and 1951): what ALL.Net's "DFI" bodies are
// ------------------------------------------------------------------------------

static const char g_b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64_decode(const char *in, int n, unsigned char *out, int room)
{
    unsigned v = 0;
    int bits = 0, len = 0;
    for (int i = 0; i < n && in[i] != '='; i++)
    {
        const char *p = strchr(g_b64, in[i]);
        if (!p || !in[i])
            continue; // line breaks
        v = v << 6 | (unsigned)(p - g_b64);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            if (len < room)
                out[len++] = (unsigned char)(v >> bits);
        }
    }
    return len;
}

static int base64_encode(const unsigned char *in, int n, char *out, int room)
{
    int len = 0;
    for (int i = 0; i < n && len + 4 < room; i += 3)
    {
        unsigned v = in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[len++] = g_b64[v >> 18 & 63];
        out[len++] = g_b64[v >> 12 & 63];
        out[len++] = i + 1 < n ? g_b64[v >> 6 & 63] : '=';
        out[len++] = i + 2 < n ? g_b64[v & 63] : '=';
    }
    out[len] = 0;
    return len;
}

// Inflate: Huffman codes canonical, decoded bit by bit (the code's first value and index per length)
struct Bits
{
    const unsigned char *p, *end;
    int bit, bad;
};

static unsigned get_bits(Bits *b, int n)
{
    unsigned v = 0;
    for (int i = 0; i < n; i++)
    {
        if (b->p >= b->end)
        {
            b->bad = 1;
            return 0;
        }
        v |= (unsigned)(*b->p >> b->bit & 1) << i;
        if (++b->bit == 8)
            b->bit = 0, b->p++;
    }
    return v;
}

struct Huffman
{
    unsigned short count[16], symbol[288];
};

static void huffman(Huffman *h, const unsigned char *lengths, int n)
{
    unsigned short offset[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++)
        h->count[lengths[i]]++;
    h->count[0] = 0;
    offset[1] = 0;
    for (int len = 1; len < 15; len++)
        offset[len + 1] = (unsigned short)(offset[len] + h->count[len]);
    for (int i = 0; i < n; i++)
        if (lengths[i])
            h->symbol[offset[lengths[i]]++] = (unsigned short)i;
}

static int decode(Bits *b, const Huffman *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++)
    {
        code |= (int)get_bits(b, 1);
        int count = h->count[len];
        if (code - first < count)
            return h->symbol[index + code - first];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    b->bad = 1;
    return -1;
}

// A zlib stream into out; the length written, -1 when it is not one
static int inflate_zlib(const unsigned char *in, int n, unsigned char *out, int room)
{
    static const unsigned short lengthBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const unsigned char lengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const unsigned short distBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                                6145, 8193, 12289, 16385, 24577};
    static const unsigned char distExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    static const unsigned char order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    if (n < 6 || (in[0] & 0x0F) != 8 || (in[0] << 8 | in[1]) % 31)
        return -1;
    Bits b = {in + 2, in + n, 0, 0};
    int len = 0, last;
    do
    {
        last = (int)get_bits(&b, 1);
        int type = (int)get_bits(&b, 2);
        if (type == 0)
        {
            if (b.bit)
                b.bit = 0, b.p++;
            if (b.end - b.p < 4)
                return -1;
            int stored = b.p[0] | b.p[1] << 8;
            b.p += 4;
            if (b.end - b.p < stored || len + stored > room)
                return -1;
            memcpy(out + len, b.p, stored);
            b.p += stored;
            len += stored;
            continue;
        }
        if (type == 3)
            return -1;
        static Huffman lit, dist; // under g_inflateLock
        unsigned char lengths[320];
        if (type == 1)
        {
            for (int i = 0; i < 288; i++)
                lengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
            huffman(&lit, lengths, 288);
            memset(lengths, 5, 30);
            huffman(&dist, lengths, 30);
        }
        else
        {
            int nlit = (int)get_bits(&b, 5) + 257, ndist = (int)get_bits(&b, 5) + 1, ncode = (int)get_bits(&b, 4) + 4;
            memset(lengths, 0, sizeof lengths);
            for (int i = 0; i < ncode; i++)
                lengths[order[i]] = (unsigned char)get_bits(&b, 3);
            Huffman codes;
            huffman(&codes, lengths, 19);
            memset(lengths, 0, sizeof lengths);
            for (int i = 0; i < nlit + ndist && !b.bad;)
            {
                int sym = decode(&b, &codes), repeat = 0, value = 0;
                if (sym < 16)
                {
                    lengths[i++] = (unsigned char)sym;
                    continue;
                }
                if (sym == 16 && i)
                    value = lengths[i - 1], repeat = 3 + (int)get_bits(&b, 2);
                else if (sym == 17)
                    repeat = 3 + (int)get_bits(&b, 3);
                else if (sym == 18)
                    repeat = 11 + (int)get_bits(&b, 7);
                else
                    return -1;
                if (i + repeat > nlit + ndist)
                    return -1;
                while (repeat--)
                    lengths[i++] = (unsigned char)value;
            }
            huffman(&lit, lengths, nlit);
            huffman(&dist, lengths + nlit, ndist);
        }
        for (;;)
        {
            int sym = decode(&b, &lit);
            if (b.bad || sym < 0)
                return -1;
            if (sym < 256)
            {
                if (len >= room)
                    return -1;
                out[len++] = (unsigned char)sym;
                continue;
            }
            if (sym == 256)
                break;
            sym -= 257;
            if (sym >= 29)
                return -1;
            int length = lengthBase[sym] + (int)get_bits(&b, lengthExtra[sym]);
            int d = decode(&b, &dist);
            if (d < 0 || d >= 30)
                return -1;
            int distance = distBase[d] + (int)get_bits(&b, distExtra[d]);
            if (distance > len || len + length > room)
                return -1;
            for (int i = 0; i < length; i++, len++)
                out[len] = out[len - distance];
        }
    } while (!last && !b.bad);
    return b.bad ? -1 : len;
}

// Deflate without compression (stored blocks), in a zlib stream
static int deflate_zlib(const unsigned char *in, int n, unsigned char *out, int room)
{
    if (room < n + 6 + 5 * (n / 65535 + 1))
        return -1;
    int len = 0;
    out[len++] = 0x78, out[len++] = 0x01;
    int at = 0;
    do
    {
        int block = n - at > 65535 ? 65535 : n - at;
        out[len++] = at + block == n;
        out[len++] = (unsigned char)block, out[len++] = (unsigned char)(block >> 8);
        out[len++] = (unsigned char)~block, out[len++] = (unsigned char)(~block >> 8);
        memcpy(out + len, in + at, block);
        len += block, at += block;
    } while (at < n);
    unsigned a = 1, b = 0;
    for (int i = 0; i < n; i++)
        a = (a + in[i]) % 65521, b = (b + a) % 65521;
    unsigned adler = b << 16 | a;
    out[len++] = (unsigned char)(adler >> 24), out[len++] = (unsigned char)(adler >> 16);
    out[len++] = (unsigned char)(adler >> 8), out[len++] = (unsigned char)adler;
    return len;
}

static CRITICAL_SECTION g_inflateLock;

// ------------------------------------------------------------------------------
// The names amdaemon looks up
// ------------------------------------------------------------------------------

// The address a name is answered with, g_absent for a name not found, NULL for the others. The
// cabinet is on a broadband line behind the shop's router (amdaemon's config: support_line
// broadband): tenporouter.loc and bbrouter.loc are found, mobirouter.loc and dslrouter.loc not
// (tenporouter.loc and mobirouter.loc both found: error 8005, "Network type error (WAN)")
static const char g_absent[] = "(not found)";

// A name in ROT13: the one older amdaemons look up (Initial D Zero v1.31's: anbzvarg.wc for
// naominet.jp, nvzr.anbzvarg.wc for aime.naominet.jp)
static void rot13(const char *name, char *out, size_t size)
{
    size_t i = 0;
    for (; name[i] && i + 1 < size; i++)
    {
        char c = name[i];
        if (c >= 'a' && c <= 'z')
            c = (char)('a' + (c - 'a' + 13) % 26);
        else if (c >= 'A' && c <= 'Z')
            c = (char)('A' + (c - 'A' + 13) % 26);
        out[i] = c;
    }
    out[i] = 0;
}

static const char *address_of(const char *name)
{
    static const char *local[] = {"naominet.jp", "ib.naominet.jp", "aime.naominet.jp"};
    static const char *routers[] = {"tenporouter.loc", "bbrouter.loc"};
    static const char *absent[] = {"mobirouter.loc", "dslrouter.loc"};
    char decoded[256] = "";
    if (name)
        rot13(name, decoded, sizeof decoded);
    for (int i = 0; name && i < (int)(sizeof local / sizeof local[0]); i++)
        if (!_stricmp(name, local[i]) || !_stricmp(decoded, local[i]))
            return ALLNET_ADDRESS;
    for (int i = 0; name && i < (int)(sizeof routers / sizeof routers[0]); i++)
        if (!_stricmp(name, routers[i]))
            return g_router;
    for (int i = 0; name && i < (int)(sizeof absent / sizeof absent[0]); i++)
        if (!_stricmp(name, absent[i]))
            return g_absent;
    return NULL;
}

#define WL_DNS_NAME_ERROR 9003 // DNS_ERROR_RCODE_NAME_ERROR

struct WlDnsQueryResult // DNS_QUERY_RESULT
{
    ULONG version;
    LONG status;
    ULONG64 options;
    void *records;
    void *reserved;
};

static const wchar_t *address_ofW(const wchar_t *name)
{
    if (!name)
        return NULL;
    char n[256];
    WideCharToMultiByte(CP_ACP, 0, name, -1, n, sizeof n, NULL, NULL);
    const char *a = address_of(n);
    if (!a)
        return NULL;
    if (a == g_absent)
        return L"";
    static wchar_t addresses[2][16]; // ALL.Net's and the router's, kept for the queries' life
    wchar_t *w = addresses[a == g_router];
    MultiByteToWideChar(CP_ACP, 0, a, -1, w, 16);
    return w;
}

static void log_name(const char *how, const char *name, const char *address)
{
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 40)
        wl_log("allnet: %s \"%s\" -> %s", how, name ? name : "", address ? address : "(the PC's answer)");
}

static LONG(WINAPI *oDnsQuery_A)(PCSTR, WORD, DWORD, PVOID, void **, PVOID *);
static LONG WINAPI allnet_DnsQuery_A(PCSTR name, WORD type, DWORD options, PVOID extra, void **results, PVOID *reserved)
{
    const char *a = address_of(name);
    log_name("DnsQuery_A", name, a);
    if (a == g_absent)
    {
        if (results)
            *results = NULL;
        return WL_DNS_NAME_ERROR;
    }
    return oDnsQuery_A(a ? a : name, type, options, extra, results, reserved);
}

struct WlDnsQueryRequest // DNS_QUERY_REQUEST, version 1
{
    ULONG version;
    PCWSTR name;
    WORD type;
    ULONG64 options;
    void *servers;
    ULONG interfaceIndex;
    void *callback;
    void *context;
};

static LONG(WINAPI *oDnsQueryEx)(WlDnsQueryRequest *, void *, void *);
static LONG WINAPI allnet_DnsQueryEx(WlDnsQueryRequest *request, void *results, void *cancel)
{
    const wchar_t *a = request && request->version == 1 ? address_ofW(request->name) : NULL;
    char n[256] = "", u[32] = "";
    if (request && request->name)
        WideCharToMultiByte(CP_ACP, 0, request->name, -1, n, sizeof n, NULL, NULL);
    if (a)
        WideCharToMultiByte(CP_ACP, 0, a, -1, u, sizeof u, NULL, NULL);
    log_name("DnsQueryEx", n, a ? (a[0] ? u : g_absent) : NULL);
    if (!a)
        return oDnsQueryEx(request, results, cancel);
    if (!a[0]) // not found, at once: the query done synchronously
    {
        WlDnsQueryResult *r = (WlDnsQueryResult *)results;
        if (r)
            r->status = WL_DNS_NAME_ERROR, r->records = NULL;
        return WL_DNS_NAME_ERROR;
    }
    WlDnsQueryRequest copy = *request;
    copy.name = a;
    return oDnsQueryEx(&copy, results, cancel);
}

static int(WSAAPI *ogetaddrinfo)(PCSTR, PCSTR, const ADDRINFOA *, PADDRINFOA *);
static int WSAAPI allnet_getaddrinfo(PCSTR node, PCSTR service, const ADDRINFOA *hints, PADDRINFOA *result)
{
    const char *a = address_of(node);
    log_name("getaddrinfo", node, a);
    if (a == g_absent)
    {
        if (result)
            *result = NULL;
        return WSAHOST_NOT_FOUND;
    }
    return ogetaddrinfo(a ? a : node, service, hints, result);
}

// ALL.Net's HTTP (192.0.2.1:80 once its name is answered) to the server, Aime's (192.0.2.1:22345) to
// the Aime server; to is filled then
static int to_server(const sockaddr *a, int len, sockaddr_in *to, const char *how)
{
    const sockaddr_in *in = (const sockaddr_in *)a;
    if (a && len >= (int)sizeof(sockaddr_in) && a->sa_family == AF_INET)
    {
        static volatile LONG logged;
        int port = in->sin_addr.s_addr != ALLNET_ADDRESS_N ? 0 : in->sin_port == htons(80) ? g_serverPort : in->sin_port == htons(22345) ? AIME_PORT : 0;
        if (InterlockedIncrement(&logged) <= 20)
        {
            const unsigned char *b = (const unsigned char *)&in->sin_addr;
            wl_log("allnet: %s %u.%u.%u.%u:%u%s", how, b[0], b[1], b[2], b[3], ntohs(in->sin_port), port ? " -> this PC's server" : "");
        }
        if (port)
        {
            *to = *in;
            to->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            to->sin_port = htons((u_short)port);
            return 1;
        }
    }
    return 0;
}

static int(WSAAPI *oconnect)(SOCKET, const sockaddr *, int);
static int WSAAPI allnet_connect(SOCKET s, const sockaddr *a, int len)
{
    sockaddr_in to;
    if (to_server(a, len, &to, "connect"))
        return oconnect(s, (const sockaddr *)&to, sizeof to);
    return oconnect(s, a, len);
}

static int(WSAAPI *oWSAConnect)(SOCKET, const sockaddr *, int, LPWSABUF, LPWSABUF, LPQOS, LPQOS);
static int WSAAPI allnet_WSAConnect(SOCKET s, const sockaddr *a, int len, LPWSABUF callerData, LPWSABUF calleeData, LPQOS sqos, LPQOS gqos)
{
    sockaddr_in to;
    if (to_server(a, len, &to, "WSAConnect"))
        return oWSAConnect(s, (const sockaddr *)&to, sizeof to, callerData, calleeData, sqos, gqos);
    return oWSAConnect(s, a, len, callerData, calleeData, sqos, gqos);
}

// ------------------------------------------------------------------------------
// The server
// ------------------------------------------------------------------------------

// A field of a form ("game_id=SDDS&ver=..."): its value, "" when absent
static void form_field(const char *form, const char *key, char *out, size_t size)
{
    size_t k = strlen(key);
    out[0] = 0;
    for (const char *p = form; p && *p; p = strchr(p, '&') ? strchr(p, '&') + 1 : NULL)
        if (!strncmp(p, key, k) && p[k] == '=')
        {
            size_t n = strcspn(p + k + 1, "&\r\n");
            snprintf(out, size, "%.*s", (int)n, p + k + 1);
            return;
        }
}

static void send_all(SOCKET c, const char *data, int n)
{
    while (n > 0)
    {
        int sent = send(c, data, n, 0);
        if (sent <= 0)
            return;
        data += sent, n -= sent;
    }
}

static void respond(SOCKET c, int status, const char *headers, const char *body, int n)
{
    char head[512];
    int h = snprintf(head, sizeof head, "HTTP/1.1 %d %s\r\nContent-Length: %d\r\nConnection: close\r\n%s\r\n", status,
                     status == 200 ? "OK" : status == 404 ? "Not Found" : "Bad Request", n, headers ? headers : "");
    send_all(c, head, h);
    send_all(c, body, n);
}

// ALL.Net's servlets: PowerOn, the authentication, and DownloadOrder, the network delivery's
// question (nothing to download: uri=null)
static void allnet_servlet(SOCKET c, const char *what, const char *body, int n, int dfi)
{
    static unsigned char packed[8192], form[8192];
    int len;
    EnterCriticalSection(&g_inflateLock);
    if (dfi)
    {
        int z = base64_decode(body, n, packed, sizeof packed);
        len = inflate_zlib(packed, z, form, sizeof form - 1);
    }
    else
    {
        len = n < (int)sizeof form - 1 ? n : (int)sizeof form - 1;
        memcpy(form, body, len);
    }
    if (len < 0)
    {
        LeaveCriticalSection(&g_inflateLock);
        wl_log("allnet: %s: the request's body is not zlib (%d bytes)", what, n);
        respond(c, 400, NULL, "", 0);
        return;
    }
    form[len] = 0;
    char serial[32], token[32];
    form_field((char *)form, "serial", serial, sizeof serial);
    form_field((char *)form, "token", token, sizeof token);
    wl_log("allnet: %s: %s", what, (char *)form);
    LeaveCriticalSection(&g_inflateLock);

    char text[1024];
    int tn;
    if (!strcmp(what, "PowerOn"))
    {
        // The PC's own time zone and time
        TIME_ZONE_INFORMATION tz;
        DWORD dst = GetTimeZoneInformation(&tz);
        long bias = -(tz.Bias + (dst == TIME_ZONE_ID_DAYLIGHT ? tz.DaylightBias : 0));
        SYSTEMTIME t;
        GetSystemTime(&t);
        tn = snprintf(text, sizeof text,
                      "stat=1&uri=%s&host=%s&place_id=123&name=windowsloader&nickname=windowsloader&region0=1&region_name0=W&"
                      "region_name1=&region_name2=&region_name3=&country=JPN&allnet_id=456&client_timezone=%c%02ld%02ld&"
                      "utc_time=%04u-%02u-%02uT%02u:%02u:%02uZ&setting=1&res_ver=3&token=%s\n",
                      ALLNET_ADDRESS, ALLNET_ADDRESS, bias < 0 ? '-' : '+', labs(bias) / 60, labs(bias) % 60, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                      t.wSecond, token);
    }
    else
        tn = snprintf(text, sizeof text, "stat=1&serial=%s&uri=null\n", serial);
    wl_log("allnet: %s answered: %.*s", what, tn - 1, text);
    if (!dfi)
    {
        respond(c, 200, "Content-Type: text/plain\r\n", text, tn);
        return;
    }
    // The base64 in lines, each ended by CR LF (the library decodes it line by line and wants one,
    // +0x36c7cf: "Auth Response Format Error" otherwise)
    static char encoded[4096];
    EnterCriticalSection(&g_inflateLock);
    int z = deflate_zlib((const unsigned char *)text, tn, packed, sizeof packed);
    int en = base64_encode(packed, z, encoded, sizeof encoded - 2);
    encoded[en++] = '\r', encoded[en++] = '\n';
    respond(c, 200, "Content-Type: text/plain\r\nPragma: DFI\r\n", encoded, en);
    LeaveCriticalSection(&g_inflateLock);
}

// ------------------------------------------------------------------------------
// The title server, in the game's process: its library (abaasgs) posts to "http://" uri ":80" path
// a body of 16 bytes of header (01 01, a status, big-endian, the game's ID, 8 random bytes: also the
// IV) and AES-256-CBC(zlib(JSON)), PKCS#7, with a key it makes of the title's key; the answers are
// the same (+0x26443e0: the header checked, the rest decrypted then uncompressed). The key is the
// one the game gives BCrypt (BCryptGenerateSymmetricKey), taken there
// ------------------------------------------------------------------------------

static unsigned char g_aesKey[32];
static volatile LONG g_aesKeyKnown;

typedef LONG(WINAPI *BCryptOpen)(void **, LPCWSTR, LPCWSTR, ULONG);
typedef LONG(WINAPI *BCryptSetProp)(void *, LPCWSTR, unsigned char *, ULONG, ULONG);
typedef LONG(WINAPI *BCryptGenKey)(void *, void **, unsigned char *, ULONG, unsigned char *, ULONG, ULONG);
typedef LONG(WINAPI *BCryptCrypt)(void *, unsigned char *, ULONG, void *, unsigned char *, ULONG, unsigned char *, ULONG, ULONG *, ULONG);
typedef LONG(WINAPI *BCryptDestroy)(void *);

static BCryptGenKey oBCryptGenerateSymmetricKey;
static LONG WINAPI title_BCryptGenerateSymmetricKey(void *alg, void **key, unsigned char *object, ULONG objectSize, unsigned char *secret, ULONG n,
                                                    ULONG flags)
{
    if (secret && n == sizeof g_aesKey && !InterlockedCompareExchange(&g_aesKeyKnown, 1, 0))
    {
        memcpy(g_aesKey, secret, sizeof g_aesKey);
        wl_log("allnet: the title server's AES key taken from the game");
    }
    return oBCryptGenerateSymmetricKey(alg, key, object, objectSize, secret, n, flags);
}

// AES-256-CBC with that key, n a multiple of 16, no padding added; 0 when done
static int aes_cbc(int encrypt, const unsigned char iv[16], const unsigned char *in, ULONG n, unsigned char *out)
{
    static BCryptOpen open_;
    static BCryptSetProp setProperty;
    static BCryptGenKey generateKey;
    static BCryptCrypt encrypt_, decrypt_;
    static BCryptDestroy destroyKey;
    static void *alg;
    if (!alg)
    {
        HMODULE bcrypt = LoadLibraryW(L"bcrypt.dll");
        open_ = (BCryptOpen)GetProcAddress(bcrypt, "BCryptOpenAlgorithmProvider");
        setProperty = (BCryptSetProp)GetProcAddress(bcrypt, "BCryptSetProperty");
        generateKey = (BCryptGenKey)GetProcAddress(bcrypt, "BCryptGenerateSymmetricKey");
        encrypt_ = (BCryptCrypt)GetProcAddress(bcrypt, "BCryptEncrypt");
        decrypt_ = (BCryptCrypt)GetProcAddress(bcrypt, "BCryptDecrypt");
        destroyKey = (BCryptDestroy)GetProcAddress(bcrypt, "BCryptDestroyKey");
        static wchar_t cbc[] = L"ChainingModeCBC";
        if (!open_ || open_(&alg, L"AES", NULL, 0) || setProperty(alg, L"ChainingMode", (unsigned char *)cbc, sizeof cbc, 0))
        {
            alg = NULL;
            return -1;
        }
    }
    void *key = NULL;
    if (!g_aesKeyKnown || generateKey(alg, &key, NULL, 0, g_aesKey, sizeof g_aesKey, 0))
        return -1;
    unsigned char ivCopy[16];
    memcpy(ivCopy, iv, 16);
    ULONG done = 0;
    LONG r = (encrypt ? encrypt_ : decrypt_)(key, (unsigned char *)in, n, NULL, ivCopy, 16, out, n, &done, 0);
    destroyKey(key);
    return r || done != n ? -1 : 0;
}

// A request's JSON, its length; -1 when it is not one
static int title_decode(const unsigned char *body, int n, char *json, int room)
{
    if (n < 32 || (n - 16) % 16)
        return -1;
    unsigned char *plain = (unsigned char *)malloc(n);
    int len = -1;
    if (plain && !aes_cbc(0, body, body + 16, n - 16, plain))
    {
        int pad = plain[n - 17];
        if (pad >= 1 && pad <= 16)
        {
            EnterCriticalSection(&g_inflateLock);
            len = inflate_zlib(plain, n - 16 - pad, (unsigned char *)json, room - 1);
            LeaveCriticalSection(&g_inflateLock);
        }
    }
    free(plain);
    if (len >= 0)
        json[len] = 0;
    return len;
}

static unsigned crc32(const unsigned char *p, int n)
{
    unsigned c = 0xFFFFFFFF;
    while (n-- > 0)
    {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = c >> 1 ^ (0xEDB88320 & (0 - (c & 1)));
    }
    return ~c;
}

// An answer: its header, 01 01, the status (0), the JSON's CRC-32 and its length, big-endian (the
// library checks both, +0x264478a: -8 otherwise), 4 bytes more, then the JSON, if any, packed
static int title_encode(const unsigned char *request, const char *json, unsigned char *out, int room)
{
    unsigned char *head = out;
    int n = json ? (int)strlen(json) : 0;
    unsigned crc = crc32((const unsigned char *)json, n);
    head[0] = 1, head[1] = 1, head[2] = 0, head[3] = 0;
    for (int i = 0; i < 4; i++)
    {
        head[4 + i] = (unsigned char)(crc >> (24 - 8 * i));
        head[8 + i] = (unsigned char)((unsigned)n >> (24 - 8 * i));
        head[12 + i] = (unsigned char)(GetTickCount() * 7 + rand() + i);
    }
    if (!json)
        return 16;
    unsigned char *packed = (unsigned char *)malloc(n + 64 + 5 * (n / 65535 + 1));
    int z = packed ? deflate_zlib((const unsigned char *)json, n, packed, n + 64 + 5 * (n / 65535 + 1)) : -1;
    int pad = z >= 0 ? 16 - z % 16 : 0;
    int total = -1;
    if (z >= 0 && 16 + z + pad <= room)
    {
        memset(packed + z, pad, pad);
        if (!aes_cbc(1, head, packed, z + pad, out + 16))
            total = 16 + z + pad;
    }
    free(packed);
    return total;
}

// The keys of a request's "free_buckets": {"keys": [...]}, each in turn
static const char *next_key(const char *p, char *key, size_t size)
{
    p = strchr(p, '"');
    if (!p || *p == ']')
        return NULL;
    const char *end = strchr(p + 1, '"');
    if (!end)
        return NULL;
    snprintf(key, size, "%.*s", (int)(end - p - 1), p + 1);
    return end + 1;
}

// The title data the server has, by key: a JSON string each. A simple value as Unreal writes it (the
// game gives it to the property's ImportText, +0x4370f0); a structure's, a JSON object written in
// that string (ParseJsonDataResponseImpl), each of its values a string too, given to ImportText
// ("1", not 1, which it reads "1.0"), its arrays [1,2]: the game turns the answer's [ ] into ( ),
// then "rounds":(1) into "rounds":"(1)". The times: Unix time, seconds (UTC, +0x32afdb). With
// these the game plays a guest's race; the others are left out (the game takes them as unset)
static const struct
{
    const char *key, *value;
} g_titleData[] = {
    // NetSeasonSettingData: id, year, no, primary_year, primary_no, time_start and time_end (2018-01-01
    // to 2037-12-31), rounds and primary_rounds (int arrays, at least one round: the game reads the
    // first, +0x30f7ee)
    {"season_setting", "\"{\\\"id\\\":\\\"1\\\",\\\"year\\\":\\\"2018\\\",\\\"no\\\":\\\"1\\\",\\\"primary_year\\\":\\\"2018\\\","
                       "\\\"primary_no\\\":\\\"1\\\",\\\"time_start\\\":\\\"1514764800\\\",\\\"time_end\\\":\\\"2145916799\\\","
                       "\\\"rounds\\\":[1],\\\"primary_rounds\\\":[1]}\""},
    // NetSeasonTitleData: the season's names
    {"season_title", "\"{\\\"title\\\":\\\"windowsloader\\\",\\\"primary_title\\\":\\\"windowsloader\\\"}\""},
    // NetSeasonRecommendSettingData: scale_percent, update_time (the time sale's period, 1 to 99: the
    // game divides by it, +0x3949f2)
    {"season_recommend_setting", "\"{\\\"scale_percent\\\":\\\"100\\\",\\\"update_time\\\":\\\"24\\\"}\""},
    // NetEventSettingBaseData: id, year, rounds, primary_rounds, grid_sort_rule, car_class,
    // team_category, teams_num, is_close (at least one round as well: the game reads the first,
    // +0x30f7f5)
    {"event_setting1", "\"{\\\"id\\\":\\\"1\\\",\\\"year\\\":\\\"2018\\\",\\\"rounds\\\":[1],\\\"primary_rounds\\\":[1],"
                       "\\\"grid_sort_rule\\\":\\\"0\\\",\\\"car_class\\\":\\\"0\\\",\\\"team_category\\\":\\\"0\\\","
                       "\\\"teams_num\\\":\\\"0\\\",\\\"is_close\\\":\\\"1\\\"}\""},
};

static void title_server(SOCKET c, const char *method, const char *path, const char *headers, const char *body, int n)
{
    static char json[1 << 16];
    static CRITICAL_SECTION lock;
    static volatile LONG once;
    if (!InterlockedExchange(&once, 1))
        InitializeCriticalSection(&lock);
    EnterCriticalSection(&lock);
    int len = title_decode((const unsigned char *)body, n, json, sizeof json);
    static volatile LONG logged;
    int log = InterlockedIncrement(&logged) <= 60;
    if (log)
        wl_log("allnet: title server: %s %s, %d bytes: %.*s", method, path, n, len > 0 ? (len < 300 ? len : 300) : 0, len > 0 ? json : "");
    // The answer's JSON, NULL for none
    static char answer[1 << 16];
    const char *reply = NULL;
    if (!strcmp(path, "/api/data/load") && strstr(json, "\"free_buckets\""))
    {
        int a = snprintf(answer, sizeof answer, "{\"free_buckets\":{");
        const char *p = strstr(json, "\"keys\"");
        p = p ? strchr(p, '[') : NULL;
        char key[128];
        int first = 1;
        while (p && (p = next_key(p, key, sizeof key)) != NULL)
            for (int i = 0; i < (int)(sizeof g_titleData / sizeof g_titleData[0]); i++)
                if (!strcmp(key, g_titleData[i].key) && a < (int)sizeof answer - 2)
                {
                    a += snprintf(answer + a, sizeof answer - a, "%s\"%s\":%s", first ? "" : ",", key, g_titleData[i].value);
                    first = 0;
                }
        snprintf(answer + a, sizeof answer - a, "}}");
        reply = answer;
    }
    else if (!strcmp(path, "/api/ranking/list"))
        reply = "{\"rankings\":[]}";
    static unsigned char out[1 << 17];
    int on = n >= 16 ? title_encode((const unsigned char *)body, reply, out, sizeof out) : -1;
    LeaveCriticalSection(&lock);
    if (on < 0)
    {
        wl_log("allnet: title server: %s not answered (no key yet, or not its format)", path);
        respond(c, 400, NULL, "", 0);
        return;
    }
    if (log)
        wl_log("allnet: title server answered %s: %.300s", path, reply ? reply : "(the header only)");
    respond(c, 200, "Content-Type: application/octet-stream\r\n", (const char *)out, on);
}

static const char *header_value(const char *headers, const char *name, char *out, size_t size)
{
    out[0] = 0;
    size_t k = strlen(name);
    for (const char *p = headers; p && *p; p = strstr(p, "\r\n") ? strstr(p, "\r\n") + 2 : NULL)
        if (!_strnicmp(p, name, k) && p[k] == ':')
        {
            const char *v = p + k + 1;
            while (*v == ' ')
                v++;
            snprintf(out, size, "%.*s", (int)strcspn(v, "\r\n"), v);
            return out;
        }
    return out;
}

// ------------------------------------------------------------------------------
// The Aime server ("AimeDB", aime.naominet.jp:22345, amdaemon's connections to 192.0.2.1:22345 sent to
// 127.0.0.1:40182): packets AES-128-ECB with the key "Copyright(C)SEGA", a header of 32 bytes (A13E,
// the protocol's version, the command, the packet's length, a status, the game, the shop, the
// keychip), as ARTEMiS's core/aimedb.py has them (its fields, not its code). Each command is answered
// by the next one (64 hello: 65), status 1; no card is known: a lookup gives user -1
// ------------------------------------------------------------------------------


// AES-128-ECB with that key, n a multiple of 16; 0 when done
static int aes_ecb(int encrypt, const unsigned char *in, ULONG n, unsigned char *out)
{
    static BCryptCrypt encrypt_, decrypt_;
    static void *key;
    if (!key)
    {
        HMODULE bcrypt = LoadLibraryW(L"bcrypt.dll");
        BCryptOpen open_ = (BCryptOpen)GetProcAddress(bcrypt, "BCryptOpenAlgorithmProvider");
        BCryptSetProp setProperty = (BCryptSetProp)GetProcAddress(bcrypt, "BCryptSetProperty");
        BCryptGenKey generateKey = (BCryptGenKey)GetProcAddress(bcrypt, "BCryptGenerateSymmetricKey");
        encrypt_ = (BCryptCrypt)GetProcAddress(bcrypt, "BCryptEncrypt");
        decrypt_ = (BCryptCrypt)GetProcAddress(bcrypt, "BCryptDecrypt");
        static wchar_t ecb[] = L"ChainingModeECB";
        static unsigned char secret[] = "Copyright(C)SEGA";
        void *alg = NULL, *k = NULL;
        if (!open_ || open_(&alg, L"AES", NULL, 0) || setProperty(alg, L"ChainingMode", (unsigned char *)ecb, sizeof ecb, 0) ||
            generateKey(alg, &k, NULL, 0, secret, 16, 0))
            return -1;
        key = k;
    }
    ULONG done = 0;
    LONG r = (encrypt ? encrypt_ : decrypt_)(key, (unsigned char *)in, n, NULL, NULL, 0, out, n, &done, 0);
    return r || done != n ? -1 : 0;
}

static int recv_all(SOCKET c, unsigned char *p, int n)
{
    for (int got = 0; got < n;)
    {
        int r = recv(c, (char *)p + got, n - got, 0);
        if (r <= 0)
            return -1;
        got += r;
    }
    return 0;
}

// A request's answer, its length; 0 for none (goodbye)
static int aime_answer(const unsigned char *q, unsigned char *a)
{
    int cmd = q[4] | q[5] << 8, code = cmd + 1, length = 0x20;
    switch (cmd)
    {
    case 0x01: // FeliCa lookup: index 0, no access code
        code = 0x03, length = 0x30;
        break;
    case 0x02: // FeliCa registration
        code = 0x03, length = 0x30;
        break;
    case 0x04: // lookup by access code
    case 0x05: // registration
        code = 0x06, length = 0x130;
        break;
    case 0x0B: // campaigns: none
        length = 0x200;
        break;
    case 0x0D: // campaign cleared
        length = 0x50;
        break;
    case 0x0F: // lookup (extended)
        length = 0x130;
        break;
    case 0x11: // FeliCa lookup (extended)
        length = 0x140;
        break;
    case 0x66: // goodbye
        return 0;
    default: // 07 and 09 logs, 13 the extended one, 64 hello: the header only
        break;
    }
    memset(a, 0, length);
    a[0] = 0x3E, a[1] = 0xA1;
    a[2] = q[2], a[3] = q[3]; // the protocol's version, the request's
    a[4] = (unsigned char)code, a[5] = (unsigned char)(code >> 8);
    a[6] = (unsigned char)length, a[7] = (unsigned char)(length >> 8);
    a[8] = 1;                       // status: done
    memcpy(a + 10, q + 10, 6 + 4 + 12); // the game, the shop, the keychip
    if (cmd == 0x04 || cmd == 0x0F || cmd == 0x11)
        memset(a + 0x20, 0xFF, 4); // the user: -1, not known
    if (cmd == 0x05)
        a[0x20] = 1; // registered: user 1
    if (cmd == 0x11)
        memset(a + 0x24, 0xFF, 8); // its relations: -1
    return length;
}

static DWORD WINAPI aime_connection(void *arg)
{
    SOCKET c = (SOCKET)arg;
    wl_busy_enter();
    unsigned char q[0x1000], a[0x200], packet[0x200];
    for (;;)
    {
        if (recv_all(c, packet, 16) || aes_ecb(0, packet, 16, q) || q[0] != 0x3E || q[1] != 0xA1)
            break;
        int length = q[6] | q[7] << 8;
        if (length < 0x20 || length > (int)sizeof q || length % 16 || recv_all(c, q + 16, length - 16) ||
            aes_ecb(0, q + 16, length - 16, q + 16))
            break;
        static volatile LONG logged;
        if (InterlockedIncrement(&logged) <= 40)
            wl_log("allnet: aime: command %02x (%d bytes), game %.6s, keychip %.12s", q[4] | q[5] << 8, length, q + 10, q + 20);
        int n = aime_answer(q, a);
        if (!n || aes_ecb(1, a, n, packet))
            break;
        send_all(c, (const char *)packet, n);
    }
    shutdown(c, SD_SEND);
    closesocket(c);
    wl_busy_leave();
    return 0;
}

static DWORD WINAPI aime_server(void *unused)
{
    wl_busy_enter();
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(AIME_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (s == INVALID_SOCKET || bind(s, (const sockaddr *)&a, sizeof a) || listen(s, 16))
    {
        wl_log("allnet: the Aime server could not listen on 127.0.0.1:%d (error %d)", AIME_PORT, WSAGetLastError());
        return 0;
    }
    wl_log("allnet: the Aime server on 127.0.0.1:%d", AIME_PORT);
    for (;;)
    {
        SOCKET c = accept(s, NULL, NULL);
        if (c == INVALID_SOCKET)
            break;
        HANDLE t = CreateThread(NULL, 0, aime_connection, (void *)c, 0, NULL);
        if (t)
            CloseHandle(t);
        else
            closesocket(c);
    }
    return 0;
}

static DWORD WINAPI connection_thread(void *arg)
{
    SOCKET c = (SOCKET)arg;
    wl_busy_enter(); // the server's sockets are not amdaemon's
    enum
    {
        MAX_REQUEST = 1 << 20
    };
    char *buf = (char *)malloc(MAX_REQUEST + 1);
    int len = 0, headerEnd = -1;
    while (buf && headerEnd < 0 && len < MAX_REQUEST)
    {
        int r = recv(c, buf + len, MAX_REQUEST - len, 0);
        if (r <= 0)
            break;
        len += r;
        buf[len] = 0;
        char *e = strstr(buf, "\r\n\r\n");
        if (e)
            headerEnd = (int)(e - buf) + 4;
    }
    if (headerEnd > 0)
    {
        char value[64];
        buf[headerEnd - 2] = 0; // the headers' text, the blank line cut
        int bodyLength = atoi(header_value(buf, "Content-Length", value, sizeof value));
        if (!_stricmp(header_value(buf, "Expect", value, sizeof value), "100-continue"))
            send_all(c, "HTTP/1.1 100 Continue\r\n\r\n", 25);
        if (bodyLength < 0 || bodyLength > MAX_REQUEST - headerEnd)
            bodyLength = MAX_REQUEST - headerEnd;
        while (len < headerEnd + bodyLength)
        {
            int r = recv(c, buf + len, headerEnd + bodyLength - len, 0);
            if (r <= 0)
                break;
            len += r;
        }
        char method[16] = "", path[256] = "";
        sscanf_s(buf, "%15s %255s", method, (unsigned)sizeof method, path, (unsigned)sizeof path);
        const char *headers = strstr(buf, "\r\n") ? strstr(buf, "\r\n") + 2 : "";
        const char *body = buf + headerEnd;
        int n = len - headerEnd;
        int dfi = !_stricmp(header_value(headers, "Pragma", value, sizeof value), "DFI");
        if (!_stricmp(path, "/sys/servlet/PowerOn"))
            allnet_servlet(c, "PowerOn", body, n, dfi);
        else if (!_stricmp(path, "/sys/servlet/DownloadOrder"))
            allnet_servlet(c, "DownloadOrder", body, n, dfi);
        else if (!strncmp(path, "/api/", 5) || strstr(path, "/api/"))
            title_server(c, method, path, headers, body, n);
        else
        {
            wl_log("allnet: %s %s: not known, 404", method, path);
            respond(c, 404, NULL, "", 0);
        }
    }
    free(buf);
    shutdown(c, SD_SEND);
    closesocket(c);
    wl_busy_leave();
    return 0;
}

static DWORD WINAPI server_thread(void *unused)
{
    wl_busy_enter();
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)g_serverPort);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (s == INVALID_SOCKET || bind(s, (const sockaddr *)&a, sizeof a) || listen(s, 16))
    {
        wl_log("allnet: the server could not listen on 127.0.0.1:%d (error %d)", g_serverPort, WSAGetLastError());
        return 0;
    }
    wl_log("allnet: the server on 127.0.0.1:%d", g_serverPort);
    for (;;)
    {
        SOCKET c = accept(s, NULL, NULL);
        if (c == INVALID_SOCKET)
            break;
        HANDLE t = CreateThread(NULL, 0, connection_thread, (void *)c, 0, NULL);
        if (t)
            CloseHandle(t);
        else
            closesocket(c);
    }
    return 0;
}

// In the game: its title server here, its connections to it (192.0.2.1:80) to this server, its AES key
void wl_allnet_game(void)
{
    g_serverPort = TITLE_PORT;
    InitializeCriticalSection(&g_inflateLock);
    HMODULE exe = GetModuleHandleW(NULL);
    if (!wl_patch_iat(exe, "bcrypt.dll", "BCryptGenerateSymmetricKey", (void *)title_BCryptGenerateSymmetricKey, (void **)&oBCryptGenerateSymmetricKey))
        wl_log("allnet: the game's BCryptGenerateSymmetricKey not in its import table: no title server");
    int con = wl_patch_iat(exe, "WS2_32.dll", (const char *)(ULONG_PTR)4, (void *)allnet_connect, (void **)&oconnect) || // by its number
              wl_patch_iat(exe, "WS2_32.dll", "connect", (void *)allnet_connect, (void **)&oconnect);
    int wsaCon = wl_patch_iat(exe, "WS2_32.dll", "WSAConnect", (void *)allnet_WSAConnect, (void **)&oWSAConnect);
    wl_log("allnet: the game's connections to the title server (%s:80) to 127.0.0.1:%d: connect %d, WSAConnect %d", ALLNET_ADDRESS, TITLE_PORT, con,
           wsaCon);
    HANDLE t = CreateThread(NULL, 0, server_thread, NULL, 0, NULL);
    if (t)
        CloseHandle(t);
}

void wl_allnet_start(unsigned gateway)
{
    snprintf(g_router, sizeof g_router, "%u.%u.%u.%u", gateway >> 24, gateway >> 16 & 0xFF, gateway >> 8 & 0xFF, gateway & 0xFF);
    InitializeCriticalSection(&g_inflateLock);
    HMODULE exe = GetModuleHandleW(NULL);
    int dnsA = wl_patch_iat(exe, "DNSAPI.dll", "DnsQuery_A", (void *)allnet_DnsQuery_A, (void **)&oDnsQuery_A);
    int dnsEx = wl_patch_iat(exe, "DNSAPI.dll", "DnsQueryEx", (void *)allnet_DnsQueryEx, (void **)&oDnsQueryEx);
    int gai = wl_patch_iat(exe, "WS2_32.dll", "getaddrinfo", (void *)allnet_getaddrinfo, (void **)&ogetaddrinfo);
    int con = wl_patch_iat(exe, "WS2_32.dll", (const char *)(ULONG_PTR)4, (void *)allnet_connect, (void **)&oconnect) || // by its number
              wl_patch_iat(exe, "WS2_32.dll", "connect", (void *)allnet_connect, (void **)&oconnect);
    int wsaCon = wl_patch_iat(exe, "WS2_32.dll", "WSAConnect", (void *)allnet_WSAConnect, (void **)&oWSAConnect);
    wl_log("allnet: amdaemon's imports taken: DnsQuery_A %d, DnsQueryEx %d, getaddrinfo %d, connect %d, WSAConnect %d; the shop's router at %s", dnsA,
           dnsEx, gai, con, wsaCon, g_router);
    HANDLE t = CreateThread(NULL, 0, server_thread, NULL, 0, NULL);
    if (t)
        CloseHandle(t);
    t = CreateThread(NULL, 0, aime_server, NULL, 0, NULL);
    if (t)
        CloseHandle(t);
}
