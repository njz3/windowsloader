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

// Network outputs, MAME style ([Outputs] OUTPUTS_NETWORK, linuxloader's keys), so tools such as
// BackForceFeeder or MAMEHooker get the lamps and the drive board without a plugin in the game
// (OutputBlaster, FFB Arcade Plugin). The protocol of linuxloader's hardware/lindbergh/netOutputs.c
// (GPL v3, the same author), as the njz3 sm2-emu and Supermodel forks have it:
//  - a TCP server (OUTPUTS_NETWORK_PORT, 8000): every client gets the same "name = value\r" lines,
//    and "mame_start = <game>\r" with every known value when it connects;
//  - one UDP datagram "mame_start = <game>\rtcp = <port>\r" to 127.0.0.1:OUTPUTS_NETWORK_UDP_PORT
//    (8001) at start, 0 for none;
//  - "mame_stop = 1\r" when the game ends.
// The values: RawLamps (RawLamps1...) and lamp<n>, the JVS general purpose outputs; RawDrive, the
// bytes the game sends to a Sega drive board, in order. The other boards' force (wl_ffb_set) is
// sent as the Sega servo board's frames, as linuxloader does for Dead Heat's and Gaelco's boards:
// 80 00 01 (effects on), 81 7F 7F (no cap), 83 7F 00 (full gain) once, then 84 the torque, centred
// on 0x80, on 14 bits.
// The values are kept even when they are not sent: the menu's "Sorties et réseau" page lists those
// the game can send (the boards declare them as they are registered) with their last value.

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "hooks.h"
#include "log.h"
#include "outputs.h"
#include "settings.h"

#define OUT_CLIENTS 8
#define OUT_CLIENT_BUFFER 65536 // a client further behind than this is dropped
#define OUT_GPO_BYTES 4
#define OUT_PENDING_MAX (1 << 20) // lines kept for the server at most (it takes them every 10 ms)

struct Client
{
    SOCKET socket;
    char out[OUT_CLIENT_BUFFER];
    size_t used;
};

static volatile LONG g_on;        // asked for: the values are sent
static volatile LONG g_lockReady; // the values are kept from the start, for the menu, sent or not
static volatile LONG g_listening; // the server listens
static volatile LONG g_clientCount;
static HANDLE g_thread;
static CRITICAL_SECTION g_lock;
static int g_lampOutputs;  // declared: the JVS board's general purpose outputs
static int g_driveDeclared; // declared: a Sega drive board's own bytes
static const char *g_lampBoard = "la carte JVS";
static const char *const *g_lampNames;

// The game's own values (wl_outputs_declare_value)
#define OUT_VALUES 160
struct Value
{
    char name[32];
    const char *type;
    char about[128];
    int value, known;
};
static Value g_values[OUT_VALUES];
static volatile LONG g_valueCount;
static char g_game[64];
static int g_tcpPort, g_udpPort;
static Client g_clients[OUT_CLIENTS]; // the server thread's

// Under g_lock: the known values, for the clients that connect later; the lines made since the
// server last ran; the end asked for
static unsigned char g_gpo[OUT_GPO_BYTES];
static int g_gpoKnown[OUT_GPO_BYTES];
static int g_lastDrive = -1;
static char *g_pending;
static size_t g_pendingUsed, g_pendingSize;
static int g_stopping;
static int g_rawDrive;     // a Sega drive board's own bytes seen: the force is not made into frames
static int g_servoStarted; // the servo board's first frames sent
static int g_sentTorque = 1000;

static void pending_append(const char *text, size_t length)
{
    if (!g_on || g_pendingUsed + length > OUT_PENDING_MAX)
        return;
    if (g_pendingUsed + length > g_pendingSize)
    {
        size_t size = g_pendingSize ? g_pendingSize * 2 : 4096;
        while (size < g_pendingUsed + length)
            size *= 2;
        char *grown = (char *)realloc(g_pending, size);
        if (!grown)
            return;
        g_pending = grown;
        g_pendingSize = size;
    }
    memcpy(g_pending + g_pendingUsed, text, length);
    g_pendingUsed += length;
}

static void pending_line(const char *name, int value)
{
    char line[96];
    int length = snprintf(line, sizeof line, "%s = %d\r", name, value);
    if (length > 0)
        pending_append(line, (size_t)length);
}

static void gpo_name(int byteIndex, char *name, size_t size)
{
    if (byteIndex)
        snprintf(name, size, "RawLamps%d", byteIndex);
    else
        snprintf(name, size, "RawLamps");
}

// JVS output n (1-based) is bit 7 - (n - 1) % 8 of byte (n - 1) / 8; the lamps' names count from 0,
// as MAME's
static void lamp_name(int byteIndex, int bit, char *name, size_t size)
{
    snprintf(name, size, "lamp%d", byteIndex * 8 + (7 - bit));
}

// Every known value, for a client that just connected (under g_lock)
static size_t build_state(char *out, size_t size)
{
    size_t used = 0;
    int n = snprintf(out, size, "mame_start = %s\r", g_game);
    if (n > 0)
        used += (size_t)n;
    char name[32];
    for (int i = 0; i < OUT_GPO_BYTES && used < size; i++)
    {
        if (!g_gpoKnown[i])
            continue;
        gpo_name(i, name, sizeof name);
        n = snprintf(out + used, size - used, "%s = %d\r", name, g_gpo[i]);
        if (n > 0)
            used += (size_t)n;
        for (int bit = 7; bit >= 0 && used < size; bit--)
        {
            lamp_name(i, bit, name, sizeof name);
            n = snprintf(out + used, size - used, "%s = %d\r", name, (g_gpo[i] >> bit) & 1);
            if (n > 0)
                used += (size_t)n;
        }
    }
    if (g_lastDrive >= 0 && used < size)
    {
        n = snprintf(out + used, size - used, "RawDrive = %d\r", g_lastDrive);
        if (n > 0)
            used += (size_t)n;
    }
    for (int i = 0; i < g_valueCount && used < size; i++)
        if (g_values[i].known)
        {
            n = snprintf(out + used, size - used, "%s = %d\r", g_values[i].name, g_values[i].value);
            if (n > 0)
                used += (size_t)n;
        }
    return used < size ? used : size;
}

static void close_client(Client *c)
{
    if (c->socket != INVALID_SOCKET)
    {
        closesocket(c->socket);
        InterlockedDecrement(&g_clientCount);
    }
    c->socket = INVALID_SOCKET;
    c->used = 0;
}

static void client_queue(Client *c, const char *data, size_t length)
{
    if (c->socket == INVALID_SOCKET)
        return;
    if (c->used + length > OUT_CLIENT_BUFFER)
    {
        wl_log("outputs: a client too slow, dropped");
        close_client(c);
        return;
    }
    memcpy(c->out + c->used, data, length);
    c->used += length;
}

static void client_flush(Client *c)
{
    while (c->socket != INVALID_SOCKET && c->used)
    {
        int sent = send(c->socket, c->out, (int)c->used, 0);
        if (sent > 0)
        {
            memmove(c->out, c->out + sent, c->used - (size_t)sent);
            c->used -= (size_t)sent;
        }
        else
        {
            if (sent < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
                return;
            close_client(c);
        }
    }
}

// Nothing is read from the clients: what they send is drained, their leaving noticed
static void client_drain(Client *c)
{
    char buffer[256];
    while (c->socket != INVALID_SOCKET)
    {
        int got = recv(c->socket, buffer, sizeof buffer, 0);
        if (got > 0)
            continue;
        if (got < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
            return;
        close_client(c);
    }
}

static void accept_clients(SOCKET listener)
{
    for (;;)
    {
        SOCKET s = accept(listener, NULL, NULL);
        if (s == INVALID_SOCKET)
            return;
        Client *c = NULL;
        for (int i = 0; i < OUT_CLIENTS && !c; i++)
            if (g_clients[i].socket == INVALID_SOCKET)
                c = &g_clients[i];
        if (!c)
        {
            closesocket(s);
            continue;
        }
        u_long nonBlocking = 1;
        ioctlsocket(s, FIONBIO, &nonBlocking);
        BOOL noDelay = TRUE;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&noDelay, sizeof noDelay);
        c->socket = s;
        c->used = 0;
        InterlockedIncrement(&g_clientCount);
        char state[8192];
        EnterCriticalSection(&g_lock);
        size_t length = build_state(state, sizeof state);
        LeaveCriticalSection(&g_lock);
        client_queue(c, state, length);
        wl_log("outputs: a client connected");
    }
}

static void announce_start(void)
{
    if (g_udpPort <= 0)
        return;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return;
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons((u_short)g_udpPort);
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    char message[128];
    int length = snprintf(message, sizeof message, "mame_start = %s\rtcp = %d\r", g_game, g_tcpPort);
    if (length > 0)
        sendto(s, message, length, 0, (const sockaddr *)&to, sizeof to);
    closesocket(s);
}

// Off: nothing kept any more (the server could not start)
static void outputs_off(void)
{
    EnterCriticalSection(&g_lock);
    InterlockedExchange(&g_on, 0);
    free(g_pending);
    g_pending = NULL;
    g_pendingUsed = g_pendingSize = 0;
    LeaveCriticalSection(&g_lock);
}

static DWORD WINAPI server_thread(LPVOID)
{
    wl_busy_enter(); // the server's sockets are not the game's
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa))
    {
        wl_log("outputs: WSAStartup failed, no network outputs");
        outputs_off();
        return 0;
    }
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in at = {};
    at.sin_family = AF_INET;
    at.sin_port = htons((u_short)g_tcpPort);
    at.sin_addr.s_addr = htonl(INADDR_ANY);
    if (listener == INVALID_SOCKET || bind(listener, (const sockaddr *)&at, sizeof at) || listen(listener, OUT_CLIENTS))
    {
        wl_log("outputs: cannot listen on TCP port %d (error %d), no network outputs", g_tcpPort, WSAGetLastError());
        if (listener != INVALID_SOCKET)
            closesocket(listener);
        outputs_off();
        return 0;
    }
    u_long nonBlocking = 1;
    ioctlsocket(listener, FIONBIO, &nonBlocking);
    for (int i = 0; i < OUT_CLIENTS; i++)
        g_clients[i].socket = INVALID_SOCKET;
    announce_start();
    InterlockedExchange(&g_listening, 1);
    wl_log("outputs: listening on TCP port %d (game \"%s\"), start announced to 127.0.0.1:%d", g_tcpPort, g_game, g_udpPort);

    char *batch = NULL;
    size_t batchSize = 0;
    for (;;)
    {
        accept_clients(listener);
        // The lines made since the last time
        EnterCriticalSection(&g_lock);
        int stop = g_stopping;
        size_t length = g_pendingUsed;
        if (length > batchSize)
        {
            char *grown = (char *)realloc(batch, length);
            if (grown)
            {
                batch = grown;
                batchSize = length;
            }
            else
                length = 0;
        }
        if (length)
            memcpy(batch, g_pending, length);
        g_pendingUsed = 0;
        LeaveCriticalSection(&g_lock);
        for (int i = 0; i < OUT_CLIENTS; i++)
        {
            if (length)
                client_queue(&g_clients[i], batch, length);
            client_flush(&g_clients[i]);
            client_drain(&g_clients[i]);
        }
        if (stop)
            break;
        Sleep(10);
    }
    InterlockedExchange(&g_listening, 0);
    for (int i = 0; i < OUT_CLIENTS; i++)
        close_client(&g_clients[i]);
    closesocket(listener);
    free(batch);
    return 0;
}

void wl_outputs_init(void)
{
    // The values are kept from now on, for the menu's page, whether they are sent or not
    InitializeCriticalSection(&g_lock);
    snprintf(g_game, sizeof g_game, "%s", wl_game_name());
    InterlockedExchange(&g_lockReady, 1);
    if (!wl_settings_get_bool("Outputs", "OUTPUTS_NETWORK", 0))
        return;
    // A companion (picodaemon, amdaemon) starts before its game: its server would take the port,
    // and BackForceFeeder would hear it, not the game
    if (wl_game_companion_of())
    {
        wl_log("outputs: a companion, the network outputs left to its game");
        return;
    }
    g_tcpPort = wl_settings_get_int("Outputs", "OUTPUTS_NETWORK_PORT", 8000);
    g_udpPort = wl_settings_get_int("Outputs", "OUTPUTS_NETWORK_UDP_PORT", 8001);
    InterlockedExchange(&g_on, 1);
    // The sockets are made by the thread: here is DllMain, under the loader's lock
    g_thread = CreateThread(NULL, 0, server_thread, NULL, 0, NULL);
    if (!g_thread)
    {
        wl_log("outputs: cannot start the server's thread, no network outputs");
        InterlockedExchange(&g_on, 0);
    }
}

void wl_outputs_stop(void)
{
    if (!g_on)
        return;
    EnterCriticalSection(&g_lock);
    int already = g_stopping;
    if (!already)
    {
        pending_append("mame_stop = 1\r", strlen("mame_stop = 1\r"));
        g_stopping = 1;
    }
    LeaveCriticalSection(&g_lock);
    if (!already && g_thread && GetCurrentThreadId() != GetThreadId(g_thread))
        WaitForSingleObject(g_thread, 500); // it sends mame_stop before it ends
}

void wl_outputs_gpo(int byteIndex, unsigned char value)
{
    if (!g_lockReady || byteIndex < 0 || byteIndex >= OUT_GPO_BYTES)
        return;
    EnterCriticalSection(&g_lock);
    if (!g_gpoKnown[byteIndex] || g_gpo[byteIndex] != value)
    {
        unsigned char changed = g_gpoKnown[byteIndex] ? (unsigned char)(g_gpo[byteIndex] ^ value) : 0xFF;
        g_gpo[byteIndex] = value;
        g_gpoKnown[byteIndex] = 1;
        char name[32];
        gpo_name(byteIndex, name, sizeof name);
        pending_line(name, value);
        for (int bit = 7; bit >= 0; bit--)
            if (changed & (1 << bit))
            {
                lamp_name(byteIndex, bit, name, sizeof name);
                pending_line(name, (value >> bit) & 1);
            }
    }
    LeaveCriticalSection(&g_lock);
}

void wl_outputs_drive(const unsigned char *bytes, size_t count)
{
    if (!g_lockReady || !bytes)
        return;
    EnterCriticalSection(&g_lock);
    g_rawDrive = 1;
    for (size_t i = 0; i < count; i++)
    {
        pending_line("RawDrive", bytes[i]);
        g_lastDrive = bytes[i];
    }
    LeaveCriticalSection(&g_lock);
}

// A frame of the Sega servo board: command, two 7-bit bytes, checksum (under g_lock)
static void servo_frame(unsigned char command, int first, int second)
{
    unsigned char f[4] = {command, (unsigned char)(first & 0x7F), (unsigned char)(second & 0x7F), 0};
    f[3] = (unsigned char)((f[0] + f[1] + f[2]) & 0x7F);
    for (int i = 0; i < 4; i++)
    {
        pending_line("RawDrive", f[i]);
        g_lastDrive = f[i];
    }
}

void wl_outputs_force(double force)
{
    if (!g_lockReady)
        return;
    // The servoboard's torque as servoboard.cpp reads it: 84's value - 0x80, > 0 pushing left
    int torque = (int)(force * 127 + (force < 0 ? -0.5 : 0.5));
    EnterCriticalSection(&g_lock);
    if (!g_rawDrive && torque != g_sentTorque)
    {
        if (!g_servoStarted)
        {
            servo_frame(0x80, 0x00, 0x01); // effects on, permanent
            servo_frame(0x81, 0x7F, 0x7F); // no cap
            servo_frame(0x83, 0x7F, 0x00); // full gain
            g_servoStarted = 1;
        }
        int raw = torque + 0x80;
        servo_frame(0x84, raw >> 7, raw);
        g_sentTorque = torque;
    }
    LeaveCriticalSection(&g_lock);
}

void wl_outputs_declare_lamps(int outputs)
{
    if (outputs > g_lampOutputs)
        g_lampOutputs = outputs < OUT_GPO_BYTES * 8 ? outputs : OUT_GPO_BYTES * 8;
}

void wl_outputs_declare_drive(void)
{
    g_driveDeclared = 1;
}

void wl_outputs_name_lamps(const char *board, const char *const *names, int count)
{
    if (board)
        g_lampBoard = board;
    g_lampNames = names;
    wl_outputs_declare_lamps(count);
}

void wl_outputs_lamps(unsigned bits, int count)
{
    // Lamp n is bit 7 - n % 8 of byte n / 8, as a JVS board's outputs
    for (int i = 0; i * 8 < count && i < OUT_GPO_BYTES; i++)
    {
        unsigned char b = 0;
        for (int k = 0; k < 8 && i * 8 + k < count; k++)
            if (bits >> (i * 8 + k) & 1)
                b |= (unsigned char)(0x80 >> k);
        wl_outputs_gpo(i, b);
    }
}

int wl_outputs_declare_value(const char *name, const char *type, const char *about)
{
    LONG i = InterlockedIncrement(&g_valueCount) - 1;
    if (i >= OUT_VALUES)
    {
        InterlockedDecrement(&g_valueCount);
        return -1;
    }
    Value *v = &g_values[i];
    snprintf(v->name, sizeof v->name, "%s", name);
    v->type = type;
    snprintf(v->about, sizeof v->about, "%s", about);
    return i;
}

void wl_outputs_value(int index, int value)
{
    if (!g_lockReady || index < 0 || index >= g_valueCount)
        return;
    EnterCriticalSection(&g_lock);
    Value *v = &g_values[index];
    if (!v->known || v->value != value)
    {
        v->value = value;
        v->known = 1;
        pending_line(v->name, value);
    }
    LeaveCriticalSection(&g_lock);
}

static void list_add(WlOutputInfo *out, int max, int *n, const char *name, const char *type, const char *about, int known, int value)
{
    if (*n >= max)
        return;
    WlOutputInfo *o = &out[(*n)++];
    snprintf(o->name, sizeof o->name, "%s", name);
    o->type = type;
    snprintf(o->about, sizeof o->about, "%s", about);
    o->known = known;
    o->value = value;
    o->text = NULL;
}

int wl_outputs_list(WlOutputInfo *out, int max)
{
    if (!g_lockReady || !out)
        return 0;
    int n = 0;
    char name[32], about[128];
    EnterCriticalSection(&g_lock);
    list_add(out, max, &n, "mame_start", "texte", "Au début du jeu, et à chaque client qui se connecte : le nom court du jeu", 1, 0);
    out[n - 1].text = g_game;
    // The lamps: each byte, then each output, as wl_outputs_gpo sends them
    int bytes = (g_lampOutputs + 7) / 8;
    for (int i = 0; i < bytes; i++)
    {
        gpo_name(i, name, sizeof name);
        snprintf(about, sizeof about, "Lampes %d à %d de %s, la première sur le bit 7", i * 8, i * 8 + 7, g_lampBoard);
        list_add(out, max, &n, name, "octet (0-255)", about, g_gpoKnown[i], g_gpo[i]);
    }
    for (int k = 0; k < g_lampOutputs; k++)
    {
        int i = k / 8, bit = 7 - k % 8;
        lamp_name(i, bit, name, sizeof name);
        if (g_lampNames && g_lampNames[k])
            snprintf(about, sizeof about, "%s : sortie %d de %s", g_lampNames[k], k + 1, g_lampBoard);
        else
            snprintf(about, sizeof about, "Sortie %d de %s (une lampe)", k + 1, g_lampBoard);
        list_add(out, max, &n, name, "bit (0/1)", about, g_gpoKnown[i], (g_gpo[i] >> bit) & 1);
    }
    // The drive board: the game's own bytes, or the servo board's frames made from the force
    if (g_driveDeclared)
        list_add(out, max, &n, "RawDrive", "octets (0-255), un par ligne",
                 "Ce que le jeu envoie à la carte moteur Sega, tel quel (mode Raw, pour une servoboard)", g_lastDrive >= 0, g_lastDrive);
    else if (wl_game_motor())
        list_add(out, max, &n, "RawDrive", "octets (0-255), un par ligne",
                 "Trames de servoboard Sega faites de la force du jeu : 80, 81, 83 une fois, puis 84 et le couple", g_lastDrive >= 0, g_lastDrive);
    for (int i = 0; i < g_valueCount; i++)
        list_add(out, max, &n, g_values[i].name, g_values[i].type, g_values[i].about, g_values[i].known, g_values[i].value);
    list_add(out, max, &n, "mame_stop", "1", "Quand le jeu se termine", 0, 1);
    LeaveCriticalSection(&g_lock);
    return n;
}

int wl_outputs_server(int *tcpPort, int *udpPort, int *clients)
{
    if (tcpPort)
        *tcpPort = g_tcpPort;
    if (udpPort)
        *udpPort = g_udpPort;
    if (clients)
        *clients = g_clientCount;
    return g_listening != 0;
}
