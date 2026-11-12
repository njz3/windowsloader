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

// Sega's servoboard (the wheel's motor board of Lindbergh, RingEdge, RingWide, ALLS and Nu driving
// cabinets) on an emulated serial port. The game writes 4-byte frames: opcode, two 7-bit values, a
// checksum; each frame gets an answer, one status byte (0x00 ready, 0x44 busy) or, after FA 00 xx,
// four (below).
// The protocol is BackForceFeeder's DRIVEBOARD.md ("Sega servoboard"):
// - FF reset, 80 effects on/off, 81 maximum force, 83 gain, 84 constant torque, 85 sine, 86 damper,
//   87/88/8B spring, 9D/9E parameters, FA the answers' mode, FB play a page, FC the wheel's bounds
//   test and the encoder's index (busy once);
// - 84: torque = ((v1 << 7) | v2) - 0x80, -128..127, > 0 to the left;
// - 85: a sine, v1 its frequency in half hertz, v2 its amplitude.
// Every effect is played (below): the torque, the sine and a page as forces, the spring and the
// damper as effects the player's wheel holds itself (ffb.cpp). K.O. Drive (RingWide) reads 4
// bytes as its answer.
// The same emulation for every Sega game with this board: Initial D 6, 7 and 8, K.O. Drive, Sonic
// (ringedge.cpp), SWDC and Initial D Zero (alls.cpp).
// Sonic & Sega All-Stars Racing checks the status byte: 3 bits, repeated in the high nibble; it
// wants 1 (0x11) after the 81 and 82 settings, 0 after a status asked (FD).

#include <windows.h>
#include <math.h>
#include <string.h>

#include "ffb.h"
#include "log.h"
#include "outputs.h"
#include "servoboard.h"
#include "vdev.h"

#define BOARD_READY 0x00
#define BOARD_BUSY 0x44
#define BOARD_SET 0x11

static unsigned char g_frame[4];
static int g_used;
static volatile LONG g_status = BOARD_READY;
// One answer per frame received, the bytes waiting for ClearCommError: Sonic & Sega All-Stars
// Racing (+0x41a7b0) reads while there are some. A read with none still gets the status.
static unsigned char g_answers[256];
static int g_answerFirst, g_answerCount;
static CRITICAL_SECTION g_lock;
static int g_boundsTested;
static unsigned g_logged;

// The answers' mode, chosen by FA 00 xx (BackForceFeeder's DRIVEBOARD.md, and the captures of real
// cabinets' boards that linuxloader replays):
// - 1F, and after a reset: short, the one status byte above;
// - 10 (Initial D's, Initial D Zero's test menu waits for it): 4 bytes, 0x80 | 10 once set up or
//   0xA0 | 10 before, the state in two 7-bit halves (01 reset and waiting, 04 looking for the
//   encoder's index for 4.05 s after FC, 00 set up), and their sum (b0 ^ b1 ^ b2) & 0x7F;
// - 14: the same with the encoder's position (the centre, 0x2000) in place of the state;
// - 00 to 0F: the values of the last command 80 to 8F read back, 0x80 | xx, the two values, their
//   sum (Harley-Davidson's board: FA 00 00, then 80 01 01 answered 80 01 01 00);
// - 1D: the last 9D, its index and page; 1E: the value of the parameter it selects (14 bits).
// Every command gets its mode's answer; the reset (FF) the one of the mode it ends (Race TV's in
// mode 14: 94 ...; Harley-Davidson's 94, then 11 for the next command).
#define REPLY_SHORT 0x1F
#define REPLY_SELECTED 0x1D
#define REPLY_PARAMETER 0x1E
static int g_reply = REPLY_SHORT;
static unsigned char g_params[16][2]; // the values of the last commands 80 to 8F

// The board's parameters, 16 pages of 16 (9D picks one, 9E writes it, FB plays a page): index 0
// a page's step in milliseconds [guess], 1 to 15 torques around 0x80, its zero. The values a MIDI
// board (838-14174) gave cold and after an initialisation, the same (DRIVEBOARD.md, "Reading the
// parameters back"); indices 4 to 15 are 0x80 on every page
static const unsigned short g_coldPages[16][4] = {
    {0x20, 0xC0, 0x80, 0x80}, {0x20, 0xB0, 0x80, 0x80}, {0x20, 0xA0, 0x80, 0x80}, {0x20, 0x90, 0x80, 0x80},
    {0x30, 0xAB, 0x80, 0x80}, {0x40, 0xA0, 0x80, 0x80}, {0x40, 0x90, 0x80, 0x80}, {0x60, 0x95, 0x80, 0x80},
    {0x10, 0xC0, 0x40, 0x80}, {0x10, 0xA0, 0x60, 0x80}, {0x10, 0xC0, 0xC0, 0x40}, {0x10, 0xA0, 0xA0, 0x60},
    {0x10, 0xC0, 0x40, 0x40}, {0x10, 0xA0, 0x60, 0x60}, {0x10, 0xC0, 0x40, 0xC0}, {0x10, 0xA0, 0x60, 0xA0},
};
static unsigned short g_pages[16][16]; // [page][index]
static int g_selIndex, g_selPage;      // the last 9D within the table
static int g_selected = 1;             // the last 9D was within it

static void cold_pages(void)
{
    for (int p = 0; p < 16; p++)
        for (int i = 0; i < 16; i++)
            g_pages[p][i] = i < 4 ? g_coldPages[p][i] : 0x80;
    g_selIndex = g_selPage = 0;
    g_selected = 1;
}
static int g_setUp;                   // the index found since the last reset
static ULONGLONG g_indexSince;        // FC's search under way: its start
#define INDEX_SEARCH_MS 4050          // 4.03 to 4.06 s in five cabinets' captures (OutRun 2 SP, Harley, Hummer, R-Tuned, Race TV)

static int four_bytes(void)
{
    return g_reply != REPLY_SHORT;
}

static void long_answer(unsigned char *a)
{
    if (g_indexSince && GetTickCount64() - g_indexSince >= INDEX_SEARCH_MS)
    {
        g_indexSince = 0;
        g_setUp = 1;
    }
    if (g_reply < 0x10)
    {
        a[0] = (unsigned char)(0x80 | g_reply);
        a[1] = g_params[g_reply][0];
        a[2] = g_params[g_reply][1];
    }
    else if (g_reply == REPLY_SELECTED || g_reply == REPLY_PARAMETER)
    {
        unsigned short v = g_pages[g_selPage][g_selIndex];
        a[0] = (unsigned char)((g_setUp ? 0x80 : 0xA0) | g_reply);
        a[1] = (unsigned char)(g_reply == REPLY_SELECTED ? g_selIndex : (v >> 7) & 0x7F);
        a[2] = (unsigned char)(g_reply == REPLY_SELECTED ? g_selPage : v & 0x7F);
    }
    else
    {
        a[0] = (unsigned char)((g_setUp ? 0x80 : 0xA0) | g_reply);
        a[1] = g_reply == 0x14 ? 0x40 : 0x00;
        a[2] = g_reply == 0x14 ? 0x00 : g_indexSince ? 0x04 : g_setUp ? 0x00 : 0x01;
    }
    a[3] = (unsigned char)((a[0] ^ a[1] ^ a[2]) & 0x7F);
}

// The answer to a command, in the mode it finds: its bytes in a, their count
static int answer(unsigned char *a, unsigned char opcode)
{
    if (four_bytes())
    {
        long_answer(a);
        return 4;
    }
    a[0] = (unsigned char)(opcode == 0x81 || opcode == 0x82 ? BOARD_SET : g_status);
    return 1;
}

// The power-on test's free wheel: K.O. Drive (+0x4a50d0) gives a torque of +8 then -8 and wants
// the wheel's position on each side in turn; Sonic (+0x41b280) waits for it to rest against each
// stop, 0x80 apart at least. A positive torque turns it to the left.
static int g_freeTest;
static volatile LONG g_freeRunning, g_freeSteering = 128;

void wl_servoboard_free_wheel_test(void)
{
    g_freeTest = 1;
}

int wl_servoboard_free_wheel(int *steering)
{
    if (!g_freeRunning)
        return 0;
    *steering = g_freeSteering;
    return 1;
}

// The effects, every one the board takes (DRIVEBOARD.md's operation codes and measures on a real
// board; the readings not measured, marked [guess], as njz3's linuxloader forceFeedback.c makes
// them: its servoboard is the work of windowsloader's author, taken over here under GPL v3):
// - the board's output: a level x (on 127) times the gain 83 (00..7F) / 127, held within the
//   maximum force 81 (its first value, both ways: measured), on 127 of the board's strength. Cold
//   and after a reset (FF): 81 40, 83 40, half of it [measured]; 81's second value (the force
//   against the wheel's movement [guess]) is not used;
// - 80 a b: b 0 every effect off, else on (on after a reset too, for a game that never sends 80);
//   a 01 the short mode: the torque dropped about 240 ms after the last frame, any frame [measured];
// - 84 the torque, centred, > 0 to the left; FB 00 pp adds page pp's torques to it (pp & 0x0F, its
//   bit 0x10 the other way [guess]), indices 1 to 15 one after the other, around 0x80, each for
//   index 0's milliseconds (0: 16, at most 1000) [guess]. 9D index page picks a parameter, 9E
//   writes it (14 bits);
// - 85 a sine: v1 its frequency in half hertz, v2 its amplitude; either at 0 stops it; played by
//   the player's wheel itself (a period of 2000 / v1 ms);
// - 86 the damper: v1 0 kept as 1 [measured], its coefficient v2 times 16 / v1 above 0x10 [guess]
//   (86 10 7F strong, 86 40 40 light, 86 xx 00 none);
// - the spring: 87 its point, centred, > 0 to the left, 128 counts about 90 degrees, taken here as
//   two thirds of a 270-degree wheel's half travel [guess]; 88 v1 00 on, 01 off; 8B v1 its
//   stiffness, 0 after a reset (no spring until a game sends one); the most it pulls, the board's
//   output of 127.
// The torque and a page are a force (wl_ffb_set_parts); the spring, the damper and the sine
// effects the player's wheel holds itself (wl_ffb_set_conditions, wl_ffb_set_vibration), at the
// wheel's own rate. A thread plays a page and the short mode's drop between the frames.
#define SHORT_MODE_MS 240
#define COLD_LEVEL 0x40              // 81's and 83's, cold
#define SPRING_CENTRE_PER_COUNT (90.0 / 128 / 135) // of a 270-degree wheel's half travel

static CRITICAL_SECTION g_fxLock;
static int g_torque, g_sineFrequency, g_sineAmplitude;
static int g_gain = COLD_LEVEL, g_cap = COLD_LEVEL, g_enabled = 1, g_shortMode;
static int g_damperSpeed = 1, g_damperParam;
static int g_springOn = 1, g_springStiffness, g_springCentre;
static int g_page = -1, g_pageInvert; // FB's page playing, -1 none
static ULONGLONG g_pageStart;
static ULONGLONG g_lastFrame;
static double g_qpcPerSecond;

// The board's output for a level on 127: a fraction of its strength
static double board_out(double level)
{
    double x = level * g_gain / 127.0;
    x = x > g_cap ? g_cap : x < -g_cap ? -g_cap : x;
    return x / 127.0;
}

// The page's torque now, around 0; the page ended: -1 in g_page
static int page_torque(void)
{
    if (g_page < 0)
        return 0;
    int step = g_pages[g_page][0];
    step = step == 0 ? 16 : step > 1000 ? 1000 : step;
    int index = 1 + (int)((GetTickCount64() - g_pageStart) / (ULONGLONG)step);
    if (index > 15)
    {
        g_page = -1;
        return 0;
    }
    int t = (int)g_pages[g_page][index] - 0x80;
    return g_pageInvert ? -t : t;
}

static void play(void) // under g_fxLock
{
    double torque = 0, sine = 0, spring = 0, saturation = 0, damper = 0;
    if (g_enabled)
    {
        int t = g_torque + page_torque();
        t = t > 127 ? 127 : t < -127 ? -127 : t;
        if (!(g_shortMode && GetTickCount64() - g_lastFrame > SHORT_MODE_MS))
            torque = board_out(t);
        if (g_sineFrequency && g_sineAmplitude)
            sine = board_out(g_sineAmplitude);
        if (g_springOn && g_springStiffness)
        {
            spring = board_out(g_springStiffness);
            saturation = board_out(127);
        }
        damper = board_out(g_damperParam * (g_damperSpeed > 0x10 ? 16.0 / g_damperSpeed : 1.0));
    }
    wl_ffb_set_parts(torque, 0, 0, 0);
    wl_ffb_set_conditions(spring, g_springCentre * SPRING_CENTRE_PER_COUNT, saturation, damper);
    wl_ffb_set_vibration(sine, sine ? 2000 / g_sineFrequency : 0);
}

static DWORD WINAPI effects_thread(LPVOID unused)
{
    for (;;)
    {
        Sleep(4);
        EnterCriticalSection(&g_fxLock);
        if (g_shortMode || g_page >= 0)
            play();
        LeaveCriticalSection(&g_fxLock);
    }
}

static void effects(const unsigned char *f, int value)
{
    EnterCriticalSection(&g_fxLock);
    g_lastFrame = GetTickCount64();
    int v1 = f[1] & 0x7F, v2 = f[2] & 0x7F;
    switch (f[0])
    {
    case 0x80:
        g_shortMode = v1 == 0x01;
        g_enabled = v2 != 0;
        break;
    case 0x81:
        g_cap = v1;
        break;
    case 0x83:
        g_gain = v1;
        break;
    case 0x84:
        g_torque = value;
        break;
    case 0x85:
        g_sineFrequency = v1;
        g_sineAmplitude = v2;
        break;
    case 0x86:
        g_damperSpeed = v1 ? v1 : 1;
        g_damperParam = v2;
        break;
    case 0x87:
        g_springCentre = value;
        break;
    case 0x88:
        g_springOn = v1 == 0;
        break;
    case 0x8B:
        g_springStiffness = v1;
        break;
    case 0x9D: // out of the table: nothing selected, the next 9E written nowhere
        g_selected = v1 < 16 && v2 < 16;
        if (g_selected)
        {
            g_selIndex = v1;
            g_selPage = v2;
        }
        break;
    case 0x9E:
        if (g_selected)
            g_pages[g_selPage][g_selIndex] = (unsigned short)(v1 << 7 | v2);
        break;
    case 0xFB:
        g_page = v2 & 0x0F;
        g_pageInvert = (v2 & 0x10) != 0;
        g_pageStart = GetTickCount64();
        break;
    case 0xFF: // the board cold again
        g_torque = g_sineFrequency = g_sineAmplitude = g_shortMode = 0;
        g_gain = g_cap = COLD_LEVEL;
        g_enabled = 1;
        g_damperSpeed = 1;
        g_damperParam = 0;
        g_springOn = 1;
        g_springStiffness = g_springCentre = 0;
        g_page = -1;
        cold_pages();
        break;
    default:
        break;
    }
    play();
    LeaveCriticalSection(&g_fxLock);
}

static void frame(const unsigned char *f)
{
    int value = ((f[1] & 0x7F) << 7 | (f[2] & 0x7F)) - 0x80;
    if (g_freeTest && (f[0] == 0xFF || f[0] == 0x83) && g_freeRunning != (f[0] == 0xFF))
    {
        g_freeRunning = f[0] == 0xFF; // the reset starts the test, the gain ends it
        g_freeSteering = 128;
        wl_log("servoboard: power-on test %s, the wheel %s", g_freeRunning ? "started" : "done", g_freeRunning ? "left free" : "back to the player");
    }
    // Each command logged when its values change (the status asks, FD, are not)
    static unsigned short last[256];
    unsigned short values = (unsigned short)(f[1] << 8 | f[2] | 0x8000);
    if (f[0] != 0xFD && last[f[0]] != values && g_logged++ < 1000)
        wl_log("servoboard: %02x %02x %02x %02x", f[0], f[1], f[2], f[3]);
    last[f[0]] = values;
    if (f[0] >= 0x80 && f[0] <= 0x8F) // read back by FA 00 00 to 0F
    {
        g_params[f[0] & 0x0F][0] = f[1] & 0x7F;
        g_params[f[0] & 0x0F][1] = f[2] & 0x7F;
    }
    effects(f, value);
    switch (f[0])
    {
    case 0x84:
        if (g_freeRunning && value)
            g_freeSteering = value > 0 ? 0x20 : 0xE0;
        break;
    case 0xFF: // reset: the short answers, the board to set up again
        g_reply = REPLY_SHORT;
        g_setUp = 0;
        g_indexSince = 0;
        memset(g_params, 0, sizeof g_params);
        break;
    case 0xFA: // the answers' mode
        if (f[2] <= 0x10 || f[2] == 0x14 || f[2] == REPLY_SELECTED || f[2] == REPLY_PARAMETER || f[2] == REPLY_SHORT)
            g_reply = f[2];
        break;
    case 0xFC: // bounds test: busy the first time it is asked (short), the index looked for
        g_indexSince = GetTickCount64();
        g_setUp = 0;
        g_status = g_boundsTested++ ? BOARD_READY : BOARD_BUSY;
        return;
    default:
        break;
    }
    g_status = BOARD_READY;
}

static BOOL board_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    const unsigned char *b = (const unsigned char *)buffer;
    wl_outputs_drive(b, size); // as they come, for BackForceFeeder ([Outputs] OUTPUTS_NETWORK)
    for (DWORD i = 0; i < size; i++)
    {
        g_frame[g_used++] = b[i];
        if (g_used == 4)
        {
            unsigned char a[4];
            int reset = g_frame[0] == 0xFF, n = 0;
            if (reset)
                n = answer(a, g_frame[0]); // in the mode the reset ends
            frame(g_frame);
            if (!reset)
                n = answer(a, g_frame[0]);
            EnterCriticalSection(&g_lock);
            // The reset's answer alone, those of before gone (a test reopening the port); a 4-byte
            // mode's first answer alone (the reset's short one would put them off by a byte)
            if (reset || (g_frame[0] == 0xFA && four_bytes()))
                g_answerCount = 0;
            for (int k = 0; k < n && g_answerCount < (int)sizeof g_answers; k++)
                g_answers[(g_answerFirst + g_answerCount++) % sizeof g_answers] = a[k];
            LeaveCriticalSection(&g_lock);
            g_used = 0;
        }
    }
    *written = size;
    return TRUE;
}

static BOOL board_read(void *buffer, DWORD size, DWORD *read, LONGLONG offset)
{
    if (!size)
    {
        *read = 0;
        return TRUE;
    }
    // The answers waiting, as many as asked (a reset's 4-byte one read in the short mode it starts);
    // none: the mode's status
    unsigned char *b = (unsigned char *)buffer;
    EnterCriticalSection(&g_lock);
    DWORD n = 0;
    while (n < size && g_answerCount)
    {
        b[n++] = g_answers[g_answerFirst];
        g_answerFirst = (g_answerFirst + 1) % sizeof g_answers;
        g_answerCount--;
    }
    if (!n)
    {
        unsigned char a[4];
        int k = 1;
        if (four_bytes())
        {
            long_answer(a);
            k = 4;
        }
        else
            a[0] = (unsigned char)g_status;
        for (int i = 0; i < k && n < size; i++)
            b[n++] = a[i];
    }
    LeaveCriticalSection(&g_lock);
    *read = n;
    return TRUE;
}

static DWORD board_waiting(void)
{
    return (DWORD)g_answerCount;
}

static DWORD board_modem(void)
{
    return MS_CTS_ON | MS_DSR_ON;
}

static WlVirtualDevice g_port = {NULL, NULL, board_read, board_write, board_modem, board_waiting};

void wl_servoboard_register(const char *port)
{
    InitializeCriticalSection(&g_lock);
    InitializeCriticalSection(&g_fxLock);
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_qpcPerSecond = (double)f.QuadPart;
    cold_pages();
    CloseHandle(CreateThread(NULL, 0, effects_thread, NULL, 0, NULL));
    g_port.name = port;
    wl_vdev_register(&g_port);
    wl_outputs_declare_drive(); // for the menu's "Sorties et réseau" page
    wl_log("emulated servoboard on %s", port);
}
