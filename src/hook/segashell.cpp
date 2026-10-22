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
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "segashell.h"

#define SHELL_PORT 18888
#define FRAME_HEADER 11

static const WlSegaShell *g_shell;
static volatile LONG g_credits;

static const char *msg_name(int cmd)
{
    return cmd >= 0 && cmd < g_shell->nameCount ? g_shell->names[cmd] : "UNKNOWN COMMAND";
}

// The game's check of the shell's answer (Daytona Championship USA +0x182500) turns the answer
// into the challenge. Its steps, each undone below in the reverse order:
//   1. every byte xor 0xB1
//   2. 12 rounds, k = 13 down to 2: the 32 bits (byte 0 the highest) shift left by one; the bit
//      coming in is the top bit xor one flip per byte having a bit in common with 40 08 10 04;
//      the bit leaving each byte, going into the next one up, is flipped when a walking mask
//      (bit k & 7 for byte 3, then one lower per byte, 0x80 after bit 0) meets 0xA5
//   3. byte 0 bit-reversed, every byte xor 0xC0
//   4. every byte xor 0x77
static const unsigned char g_secMask[4] = {0x40, 0x08, 0x10, 0x04};

static unsigned char bit_reverse(unsigned char b)
{
    unsigned char r = 0;
    for (int i = 0; i < 8; i++)
        if (b & 1 << i)
            r |= (unsigned char)(0x80 >> i);
    return r;
}

static int carry_in(const unsigned char o[4])
{
    int c = o[0] >> 7;
    for (int j = 0; j < 4; j++)
        if (g_secMask[j] & o[j])
            c ^= 1;
    return c;
}

// The flips of one round, for bytes 0 to 3
static void round_flips(int k, int flip[4])
{
    unsigned bl = 1u << (k & 7);
    for (int idx = 3; idx >= 0; idx--)
    {
        if (!bl)
            bl = 0x80;
        flip[idx] = (bl & 0xA5) != 0;
        bl >>= 1;
    }
}

// Undoes rounds k down to 2 (k counts down as the game's rounds do); returns 1 with the
// state before them. A round has 0, 1 or 2 states before it: the top bit of byte 0 is unknown.
static int undo_rounds(const unsigned char n[4], int round, unsigned char out[4])
{
    if (round > 13)
    {
        memcpy(out, n, 4);
        return 1;
    }
    int flip[4];
    round_flips(round, flip);
    for (int top = 0; top < 2; top++)
    {
        unsigned char o[4];
        o[0] = (unsigned char)(n[0] >> 1 | top << 7);
        o[1] = (unsigned char)(n[1] >> 1 | ((n[0] & 1) ^ flip[1]) << 7);
        o[2] = (unsigned char)(n[2] >> 1 | ((n[1] & 1) ^ flip[2]) << 7);
        o[3] = (unsigned char)(n[3] >> 1 | ((n[2] & 1) ^ flip[3]) << 7);
        if (carry_in(o) == (n[3] & 1) && undo_rounds(o, round + 1, out))
            return 1;
    }
    return 0;
}

static int security_answer(const unsigned char challenge[4], unsigned char answer[4])
{
    unsigned char r[4];
    for (int i = 0; i < 4; i++)
        r[i] = challenge[i] ^ 0x77 ^ 0xC0;
    r[0] = bit_reverse(r[0]);
    // The last round the game makes is k = 2: undone first
    if (!undo_rounds(r, 2, answer))
        return 0;
    for (int i = 0; i < 4; i++)
        answer[i] ^= 0xB1;
    return 1;
}

static int shell_send(SOCKET s, int cmd, const unsigned char *data, int len)
{
    unsigned char f[FRAME_HEADER + 16];
    if (len > 16)
        return 0;
    memset(f, 0, FRAME_HEADER);
    memcpy(f, g_shell->tag, 3);
    f[3] = (unsigned char)(cmd >> 8);
    f[4] = (unsigned char)cmd;
    f[5] = (unsigned char)(len >> 8);
    f[6] = (unsigned char)len;
    if (len)
        memcpy(f + FRAME_HEADER, data, len);
    return send(s, (const char *)f, FRAME_HEADER + len, 0) == FRAME_HEADER + len;
}

static void send_credits(SOCKET s)
{
    unsigned char c = (unsigned char)(g_shell->freeplay ? g_shell->freeplayCredits : g_credits);
    shell_send(s, g_shell->msgSetCredit, &c, 1);
    wl_log("shell: %s %u", msg_name(g_shell->msgSetCredit), c);
}

static void on_message(SOCKET s, int cmd, const unsigned char *p, int len)
{
    static int counts[257];
    int n = ++counts[cmd >= 0 && cmd < 256 ? cmd : 256];
    if (cmd == g_shell->msgChallenge && len >= 4)
    {
        unsigned char a[4];
        if (!security_answer(p, a))
        {
            wl_log("shell: challenge %02x %02x %02x %02x has no answer", p[0], p[1], p[2], p[3]);
            return;
        }
        shell_send(s, g_shell->msgAuthenticate, a, 4);
        if (n <= 3)
            wl_log("shell: challenge %02x %02x %02x %02x answered %02x %02x %02x %02x", p[0], p[1], p[2], p[3], a[0], a[1], a[2], a[3]);
        return;
    }
    if (cmd == g_shell->msgLaunched && g_shell->msgAck >= 0)
        shell_send(s, g_shell->msgAck, NULL, 0);
    if (cmd == g_shell->msgCreditTaken && !g_shell->freeplay && g_credits > 0)
    {
        InterlockedDecrement(&g_credits); // the game took a credit
        send_credits(s);
    }
    else if (cmd == g_shell->msgCreditTaken && g_shell->freeplay && g_shell->freeplayCredits)
        send_credits(s); // free play: the credit is there again
    if (cmd == 0 ? n == 1 : n <= 20 || n % 100 == 0)
    {
        char hex[3 * 16 + 1] = "";
        for (int i = 0; i < len && i < 16; i++)
            snprintf(hex + 3 * i, 4, " %02x", p[i]);
        wl_log("shell: %s%s (#%d)", msg_name(cmd), hex, n);
    }
}

static int pressed(WlInput *in)
{
    return in && wl_input_get(in) != 0;
}

// One connection of the game: its frames, and the coins dropped meanwhile
static void shell_session(SOCKET s)
{
    unsigned char buf[4096];
    int have = 0, coin = 0, service = 0;
    // The cabinet's SKU: Daytona's New Season Edition only notes it came, and sets one feature
    // when the second byte is 1 (+0x184ae1)
    static const unsigned char sku[2] = {0, 0};
    shell_send(s, g_shell->msgSetSku, sku, sizeof sku);
    wl_log("shell: %s %02x %02x", msg_name(g_shell->msgSetSku), sku[0], sku[1]);
    send_credits(s);
    for (;;)
    {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(s, &rd);
        timeval tv = {0, 20000};
        int r = select(0, &rd, NULL, NULL, &tv);
        if (r < 0)
            return;
        if (r > 0)
        {
            int got = recv(s, (char *)buf + have, (int)sizeof buf - have, 0);
            if (got <= 0)
                return;
            have += got;
            int at = 0;
            while (have - at >= FRAME_HEADER)
            {
                const unsigned char *f = buf + at;
                if (memcmp(f, g_shell->tag, 3))
                {
                    wl_log("shell: the game's data is out of step, dropped");
                    at = have;
                    break;
                }
                int len = f[5] << 8 | f[6];
                if (have - at < FRAME_HEADER + len)
                    break;
                on_message(s, f[3] << 8 | f[4], f + FRAME_HEADER, len);
                at += FRAME_HEADER + len;
            }
            memmove(buf, buf + at, have - at);
            have -= at;
            if (have == (int)sizeof buf)
                have = 0;
        }
        // A coin or a service credit, on the press
        int c = pressed(g_shell->coin), sv = pressed(g_shell->service);
        if ((c && !coin) || (sv && !service))
        {
            if (g_credits < 99)
                InterlockedIncrement(&g_credits);
            send_credits(s);
        }
        coin = c;
        service = sv;
    }
}

static DWORD WINAPI shell_thread(LPVOID)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa))
    {
        wl_log("shell: no Winsock, the game will quit after its security challenges");
        return 0;
    }
    SOCKET l = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL exclusive = TRUE;
    setsockopt(l, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive, sizeof exclusive);
    sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(SHELL_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (l == INVALID_SOCKET || bind(l, (const sockaddr *)&a, sizeof a) || listen(l, 1))
    {
        wl_log("shell: cannot listen on 127.0.0.1:%d (error %d; another shell running?), the game will quit after its security challenges",
               SHELL_PORT, WSAGetLastError());
        return 0;
    }
    wl_log("shell: listening on 127.0.0.1:%d, %s", SHELL_PORT, g_shell->freeplay ? "free play" : "credits from the coin input");
    for (;;)
    {
        SOCKET s = accept(l, NULL, NULL);
        if (s == INVALID_SOCKET)
        {
            Sleep(100);
            continue;
        }
        wl_log("shell: the game is connected");
        shell_session(s);
        closesocket(s);
        wl_log("shell: the game has disconnected");
    }
}

void wl_segashell_start(const WlSegaShell *shell)
{
    g_shell = shell;
    CloseHandle(CreateThread(NULL, 0, shell_thread, NULL, 0, NULL));
}
