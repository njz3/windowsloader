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
#include <string.h>

#include "jvs.h"
#include "log.h"
#include "outputs.h"
#include "vdev.h"

#define JVS_SYNC 0xE0
#define JVS_ESCAPE 0xD0

#define JVS_NODES 2

// The boards on the bus, the first one registered first
static const WlJvsBoard *g_boards[JVS_NODES];
static int g_nodes;
static unsigned char g_address[JVS_NODES]; // 0 until the host gives one
static unsigned short g_used[JVS_NODES][2]; // coins taken by the host (command 0x30)

// Bytes from the host, until a whole packet is there; bytes to the host
static unsigned char g_in[512];
static int g_inUsed;
static unsigned char g_out[1024];
static int g_outUsed;
static CRITICAL_SECTION g_lock;

static void out_byte(unsigned char b, int escape)
{
    if (g_outUsed >= (int)sizeof g_out - 2)
        return;
    if (escape && (b == JVS_SYNC || b == JVS_ESCAPE))
    {
        g_out[g_outUsed++] = JVS_ESCAPE;
        b--;
    }
    g_out[g_outUsed++] = b;
}

static void send_packet(const unsigned char *data, int length)
{
    unsigned char sum = 0;
    out_byte(JVS_SYNC, 0);
    out_byte(0x00, 1); // to the host
    out_byte((unsigned char)(length + 1), 1);
    sum = (unsigned char)(length + 1);
    for (int i = 0; i < length; i++)
    {
        out_byte(data[i], 1);
        sum += data[i];
    }
    out_byte(sum, 1);
}

// One request (its commands) to board b -> the reply's status and reports in r; returns its length,
// 0 for none
static int answer(int b, const unsigned char *p, int n, unsigned char *r)
{
    const WlJvsBoard *board = g_boards[b];
    int k = 0;
    r[k++] = 0x01; // status: fine
    WlJvsState st;
    memset(&st, 0, sizeof st);
    if (board->read)
        board->read(&st);
    int i = 0;
    while (i < n)
    {
        unsigned char c = p[i++];
        if (board->vendor && c >= 0x60 && c <= 0x7F)
        {
            int used = board->vendor(p + i - 1, n - (i - 1), r, &k);
            if (used > 0)
            {
                i += used - 1;
                continue;
            }
        }
        switch (c)
        {
        case 0xF0: // reset (broadcast, no reply): every board
            i++;
            memset(g_address, 0, sizeof g_address);
            return 0;
        case 0xF1: // address
            g_address[b] = p[i++];
            r[k++] = 0x01;
            break;
        case 0x10: // identity
        {
            size_t len = strlen(board->id) + 1;
            r[k++] = 0x01;
            memcpy(r + k, board->id, len);
            k += (int)len;
            break;
        }
        case 0x11: // command format version
            r[k++] = 0x01;
            r[k++] = 0x13;
            break;
        case 0x12: // JVS version
            r[k++] = 0x01;
            r[k++] = 0x30;
            break;
        case 0x13: // communication version
            r[k++] = 0x01;
            r[k++] = 0x10;
            break;
        case 0x14: // features
            r[k++] = 0x01;
            r[k++] = 0x01, r[k++] = (unsigned char)board->players, r[k++] = (unsigned char)(board->switches ? board->switches : board->switchBytes * 8), r[k++] = 0;
            if (board->coinSlots)
                r[k++] = 0x02, r[k++] = (unsigned char)board->coinSlots, r[k++] = 0, r[k++] = 0;
            if (board->analogChannels)
                r[k++] = 0x03, r[k++] = (unsigned char)board->analogChannels, r[k++] = (unsigned char)board->analogBits, r[k++] = 0;
            if (board->outputs)
                r[k++] = 0x12, r[k++] = (unsigned char)board->outputs, r[k++] = 0, r[k++] = 0;
            r[k++] = 0x00;
            break;
        case 0x15: // the host's identity
            while (i < n && p[i])
                i++;
            i++;
            r[k++] = 0x01;
            break;
        case 0x20: // switches: players, bytes per player
        {
            int players = p[i++], bytes = p[i++];
            static unsigned last = 0;
            static int changes;
            unsigned now = (unsigned)st.system << 24 | (unsigned)st.player3[0] << 16 | st.player[0];
            if (now != last && changes++ < 100)
                wl_log("jvs: switches %02x %04x %02x", st.system, st.player[0], st.player3[0]);
            last = now;
            r[k++] = 0x01;
            r[k++] = st.system;
            for (int pl = 0; pl < players; pl++)
                for (int b = 0; b < bytes; b++)
                    r[k++] = pl >= 2 ? 0 : b < 2 ? (unsigned char)(st.player[pl] >> (8 - 8 * b)) : b == 2 ? st.player3[pl] : 0;
            break;
        }
        case 0x21: // coins
        {
            int slots = p[i++];
            r[k++] = 0x01;
            for (int s = 0; s < slots; s++)
            {
                unsigned short count = s < 2 ? (unsigned short)(st.coins[s] - g_used[b][s]) & 0x3FFF : 0;
                r[k++] = (unsigned char)(count >> 8);
                r[k++] = (unsigned char)count;
            }
            break;
        }
        case 0x22: // analog channels
        {
            int channels = p[i++];
            r[k++] = 0x01;
            for (int ch = 0; ch < channels; ch++)
            {
                unsigned short v = ch < 10 ? st.analog[ch] : 0;
                r[k++] = (unsigned char)(v >> 8);
                r[k++] = (unsigned char)v;
            }
            break;
        }
        case 0x26: // general purpose inputs: byte count
        {
            int bytes = p[i++];
            r[k++] = 0x01;
            for (int m = 0; m < bytes; m++)
                r[k++] = m < 4 ? st.misc[m] : 0;
            break;
        }
        case 0x30: // take coins: slot, count
        {
            int slot = p[i] - 1;
            unsigned short count = (unsigned short)(p[i + 1] << 8 | p[i + 2]);
            i += 3;
            if (slot >= 0 && slot < 2)
                g_used[b][slot] += count;
            r[k++] = 0x01;
            break;
        }
        case 0x31: // add coins
        {
            int slot = p[i] - 1;
            unsigned short count = (unsigned short)(p[i + 1] << 8 | p[i + 2]);
            i += 3;
            if (slot >= 0 && slot < 2)
                g_used[b][slot] -= count;
            r[k++] = 0x01;
            break;
        }
        case 0x32: // general purpose outputs: byte count, bytes
        {
            int bytes = p[i++];
            if (board->output)
                board->output(p + i, bytes);
            for (int o = 0; o < bytes; o++)
                wl_outputs_gpo(o, p[i + o]); // the lamps, for BackForceFeeder ([Outputs] OUTPUTS_NETWORK)
            i += bytes;
            r[k++] = 0x01;
            break;
        }
        case 0x33: // analog outputs: channel count, 2 bytes each (Battle Gear 4, Chase H.Q. 2's motor)
        {
            int channels = p[i++];
            unsigned short values[16];
            for (int ch = 0; ch < channels && ch < 16; ch++)
                values[ch] = (unsigned short)(p[i + 2 * ch] << 8 | p[i + 2 * ch + 1]);
            if (board->analogOutput)
                board->analogOutput(values, channels < 16 ? channels : 16);
            i += 2 * channels;
            r[k++] = 0x01;
            break;
        }
        case 0x34: // characters for the cabinet's display: byte count, bytes (Battle Gear 4)
        {
            int bytes = p[i++];
            static char last[256];
            static int logged;
            char text[256];
            int t = 0;
            for (int c = 0; c < bytes && t < (int)sizeof text - 1; c++)
                if (p[i + c] >= 0x20 && p[i + c] < 0x7F)
                    text[t++] = (char)p[i + c];
            text[t] = 0;
            if (strcmp(text, last) && logged++ < 20) // blinking messages change each few frames
                wl_log("jvs: display \"%s\"", text);
            strcpy_s(last, sizeof last, text);
            i += bytes;
            r[k++] = 0x01;
            break;
        }
        case 0x6D: // Taito's, no argument (Battle Gear 4's I/O test and its entry key reader, KEY ID:
        case 0x6E: // 6d, 6f, 70 last in its requests): taken, nothing answered; an unknown command
        case 0x6F: // made the game close the port and start the bus again
        case 0x70:
        {
            static unsigned char logged[0x80];
            if (!logged[c & 0x7F]++)
                wl_log("jvs: command %02x (Taito) answered, no data", c);
            r[k++] = 0x01;
            break;
        }
        case 0x65: // Taito's, one byte (Battle Gear 4, last of its requests): taken
            i += 1;
            r[k++] = 0x01;
            break;
        case 0x64: // Taito's, one byte (Battle Gear 4, now and then): taken
            i += 1;
            r[k++] = 0x01;
            break;
        case 0x66: // Taito's, two bytes (Battle Gear 4 Pro, its last request): taken, logged on change
        {
            static int last = -1, logged;
            int v = p[i] << 8 | p[i + 1];
            if (v != last && logged++ < 40)
                wl_log("jvs: command 66 (Taito) %02x %02x", p[i], p[i + 1]);
            last = v;
            i += 2;
            r[k++] = 0x01;
            break;
        }
        case 0x2F: // send the last reply again: not kept, an empty one
            r[k++] = 0x01;
            break;
        default:
        {
            static int logged;
            if (logged++ < 20)
                wl_log_hex("jvs: unknown command, request:", p, n);
            r[0] = 0x02; // unsupported command
            return k;
        }
        }
    }
    return k;
}

static void take_packets(void)
{
    for (;;)
    {
        int start = 0;
        while (start < g_inUsed && g_in[start] != JVS_SYNC)
            start++;
        if (start)
        {
            memmove(g_in, g_in + start, g_inUsed - start);
            g_inUsed -= start;
        }
        // Unescape: node, length, then length bytes (data and sum)
        unsigned char pkt[512];
        int got = 0, i = 1, need = -1;
        while (i < g_inUsed && (need < 0 || got < need))
        {
            unsigned char b = g_in[i++];
            if (b == JVS_ESCAPE)
            {
                if (i >= g_inUsed)
                    break;
                b = (unsigned char)(g_in[i++] + 1);
            }
            pkt[got++] = b;
            if (got == 2)
                need = 2 + pkt[1];
        }
        if (need < 0 || got < need)
            return; // not all there yet
        memmove(g_in, g_in + i, g_inUsed - i);
        g_inUsed -= i;

        unsigned char node = pkt[0], length = pkt[1];
        unsigned char sum = (unsigned char)(node + length);
        for (int k = 2; k < 2 + length - 1; k++)
            sum += pkt[k];
        if (sum != pkt[2 + length - 1])
        {
            unsigned char r = 0x03; // checksum error
            send_packet(&r, 1);
            continue;
        }
        // The board at that address; the broadcast (reset, addressing) to the first one without an
        // address, or the first one when all have one
        int b = -1;
        for (int k = 0; k < g_nodes && b < 0; k++)
            if (node == 0xFF ? !g_address[k] : node == g_address[k])
                b = k;
        if (b < 0 && node == 0xFF)
            b = 0;
        if (b < 0)
            continue;
        static unsigned packets;
        if (++packets % 1000 == 0)
            wl_log("jvs: %u requests answered", packets);
        unsigned char reply[512];
        int n = answer(b, pkt + 2, length - 1, reply);
        if (n)
            send_packet(reply, n);
    }
}

static BOOL jvs_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    EnterCriticalSection(&g_lock);
    const unsigned char *b = (const unsigned char *)buffer;
    for (DWORD i = 0; i < size; i++)
        if (g_inUsed < (int)sizeof g_in)
            g_in[g_inUsed++] = b[i];
    take_packets();
    LeaveCriticalSection(&g_lock);
    *written = size;
    return TRUE;
}

static BOOL jvs_read(void *buffer, DWORD size, DWORD *read, LONGLONG offset)
{
    EnterCriticalSection(&g_lock);
    DWORD n = (DWORD)g_outUsed < size ? (DWORD)g_outUsed : size;
    memcpy(buffer, g_out, n);
    memmove(g_out, g_out + n, g_outUsed - n);
    g_outUsed -= n;
    LeaveCriticalSection(&g_lock);
    if (!n)
        Sleep(1); // a real port waits for its timeout
    *read = n;
    return TRUE;
}

static DWORD jvs_waiting(void)
{
    return (DWORD)g_outUsed;
}

// The sense line, as the Sega libraries read it (OpenParrot, Ring_amLib/amJvs.cpp): CTS, and DSR
// once the boards have an address. Taito's Type X (Battle Gear 4 +0x60cb0a): CTS once they have one,
// the host giving addresses until then (+0x60cb37).
static DWORD jvs_modem(void)
{
    int addressed = 1;
    for (int k = 0; k < g_nodes; k++)
        if (!g_address[k])
            addressed = 0;
    if (g_boards[0]->ctsWhenAddressed)
        return addressed ? MS_CTS_ON : 0;
    return addressed ? MS_CTS_ON | MS_DSR_ON : MS_CTS_ON;
}

// The \\.\mxjvs driver of the RingWide (K.O. Drive's amJvst +0x7c0d3e): one call takes a request
// packet and gives back the reply
#define MXJVS_IOCTL_EXCHANGE 0x9C402000

static BOOL jvs_ioctl(DWORD code, const void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned)
{
    if (code != MXJVS_IOCTL_EXCHANGE)
    {
        static int logged;
        if (logged++ < 10)
            wl_log("jvs: unknown control code %08lx", code);
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }
    DWORD n;
    jvs_write(in, inSize, &n, 0);
    EnterCriticalSection(&g_lock);
    n = (DWORD)g_outUsed < outSize ? (DWORD)g_outUsed : outSize;
    memcpy(out, g_out, n);
    g_outUsed = 0;
    LeaveCriticalSection(&g_lock);
    *returned = n;
    return TRUE;
}

static WlVirtualDevice g_ports[2] = {{NULL, jvs_ioctl, jvs_read, jvs_write, jvs_modem, jvs_waiting},
                                     {NULL, jvs_ioctl, jvs_read, jvs_write, jvs_modem, jvs_waiting}};

void wl_jvs_register(const WlJvsBoard *board)
{
    g_boards[0] = board;
    g_nodes = 1;
    InitializeCriticalSection(&g_lock);
    g_ports[0].name = board->port;
    wl_vdev_register(&g_ports[0]);
    wl_log("emulated JVS board on %s: %s, %d players, %d analog channels", board->port, board->id, board->players, board->analogChannels);
}

void wl_jvs_add_port(const char *port)
{
    g_ports[1].name = port;
    wl_vdev_register(&g_ports[1]);
    wl_log("emulated JVS board also on %s", port);
}

int wl_jvs_is_port(const char *name)
{
    for (int i = 0; i < 2; i++)
        if (g_ports[i].name && name && !_stricmp(g_ports[i].name, name))
            return 1;
    return 0;
}

void wl_jvs_add_node(const WlJvsBoard *board)
{
    if (g_nodes < 1 || g_nodes >= JVS_NODES)
        return;
    g_boards[g_nodes++] = board;
    wl_log("emulated JVS board, node %d on the same bus: %s", g_nodes, board->id);
}
