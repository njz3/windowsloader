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

// Namco System ES3: Mario Kart Arcade GP DX, MK_AGP3_FINAL.exe (x86, Direct3D 9Ex, relocated: the
// offsets below are from its base), v1.18 and v1.10 (2015; told by v1.18's bngrw.dll, absent).
// v1.10 has the same key, authentication and offline functions at other places (found by the
// key's interface GUID, the "last_auth.bin" they read, the same bytes) and its card reader on COM4
// instead of bngrw.dll. Being worked on:
// - ShareK, the cabinet's content distribution, is reached through a .NET COM class of ipccl.dll
//   that the cabinet registers (RegAsm, project.ps1): windowsloader leaves the registry alone, the
//   class is missing and the game's handler of that failure reads a null pointer: the ShareK
//   client is not started (+0x7e80d, the test of its flag made a jump);
// - the game takes the first DirectInput game controller as its wheel (vJoy on the test PC): a
//   wheel of windowsloader's is the only one listed.

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "jvs.h"
#include "log.h"
#include "vdev.h"
#include "vdinput.h"

enum
{
    IN_STEERING,
    IN_GAS,
    IN_BRAKE,
    IN_BUTTONS,
    IN_ITEM = IN_BUTTONS,
    IN_MARIO,
    IN_START,
    IN_COIN,
    IN_TEST,
    IN_SERVICE,
    IN_UP,
    IN_DOWN,
    IN_ENTER,
    IN_COUNT
};

static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 65535, 32768},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 65535, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 65535, 0},
    {"Item", "Action", WL_INPUT_SWITCH, 0, 1, 0},
    {"Mario", "Nitro", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"Service", "P1_Service", WL_INPUT_SWITCH, 0, 1, 0},
    {"MenuUp", "SelectUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"MenuDown", "SelectDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"Enter", "SelectEnter", WL_INPUT_SWITCH, 0, 1, 0},
};

static void fill_wheel(unsigned short axes[WL_VDINPUT_AXES], unsigned char *buttons)
{
    axes[0] = (unsigned short)wl_input_get(&g_inputs[IN_STEERING]);
    axes[1] = (unsigned short)wl_input_get(&g_inputs[IN_GAS]);
    axes[2] = (unsigned short)wl_input_get(&g_inputs[IN_BRAKE]);
    for (int i = 0; i < 6; i++)
        buttons[i] = (unsigned char)(wl_input_get(&g_inputs[IN_BUTTONS + i]) != 0);
}

static const WlVirtualJoystick g_wheel = {"Namco Steering Wheel", 0x0B9A, 0x0001, 8, fill_wheel};

// The cabinet's JVS board on COM3 at 115200 bauds (the game's JVS library, +0x4ac1b0, its host
// "NBGI.;WinArc;Ver2.01;JPN"; the sense line as Taito's: CTS off until every board has its
// address, +0x4ac636), Namco's NA-JV, 10-bit analog channels. Its layout, seen in the game's switch
// test: the test switch in the system byte (a switch: the test menu lasts while it is on), Service
// 0x4000, the select switch up 0x2000 and down 0x1000, Enter 0x0200, the item button 0x0020 (0x0010
// too), the Mario button 0x0002; Start 0x8000 has no use. Steering (left low), accelerator, brake
// on channels 0, 1, 2, against the calibration of the test menu's "analog volume" page.
static void jvs_state(WlJvsState *s)
{
    static int coinWas;
    static unsigned short coins;
    int coin = wl_input_get(&g_inputs[IN_COIN]);
    if (coin && !coinWas)
        coins++;
    coinWas = coin;
    s->system = wl_input_get(&g_inputs[IN_TEST]) ? 0x80 : 0;
    static const struct
    {
        int input;
        unsigned short bit;
    } bits[] = {{IN_START, 0x8000}, {IN_SERVICE, 0x4000}, {IN_UP, 0x2000}, {IN_DOWN, 0x1000},
                {IN_ENTER, 0x0200}, {IN_ITEM, 0x0020}, {IN_MARIO, 0x0002}};
    for (int i = 0; i < (int)(sizeof bits / sizeof bits[0]); i++)
        if (wl_input_get(&g_inputs[bits[i].input]))
            s->player[0] |= bits[i].bit;
    s->coins[0] = coins;
    s->analog[0] = (unsigned short)wl_input_get(&g_inputs[IN_STEERING]);
    s->analog[1] = (unsigned short)wl_input_get(&g_inputs[IN_GAS]);
    s->analog[2] = (unsigned short)wl_input_get(&g_inputs[IN_BRAKE]);
}

static void jvs_output(const unsigned char *data, int length)
{
    static unsigned char was[8];
    if (length > 8)
        length = 8;
    if (memcmp(was, data, length))
    {
        static int logged;
        if (logged++ < 40)
            wl_log("namco: JVS outputs %02x %02x %02x %02x", length > 0 ? data[0] : 0, length > 1 ? data[1] : 0, length > 2 ? data[2] : 0, length > 3 ? data[3] : 0);
        memcpy(was, data, length);
    }
}

// Namco's own JVS commands, after the board's identity, and the replies the game's JVS library
// reads (its parser of a poll's reply, +0x4adf90, and of the start's, +0x4aefa0):
// - 0x70 0x18 "PL" then 0x02 and 2 bytes, 0x14 and 1 byte, or 0x80 and 1 byte (built at +0x4af6b1
//   and +0x4af7c1): report 01 and one byte (kept at +0x36b of the board's state, not read again;
//   01 as TeknoParrot gives). The start's "70 18 PL 14 xx" is followed by "70 18 PL 80 yy" when
//   yy is set, else by 6 bytes left in the game's buffer (its length is fixed): skipped, the game
//   checks only the first reply;
// - a serial link through the board: 0x70 0x16 and 2 bytes sets it up (report only), 0x70 0x05, a
//   count and the bytes sends (report 01, 01, then 00: sent), 0x70 0x15 and 2 bytes reads (report
//   01, a count, the bytes; "70 15 00 FF", the first after 0x16, wants a count of 0). Nothing is on
//   it here: count 0;
// - 0x78, 0x79, 0x7A, 14 bytes each (12-bit values in threes: the lamps, it seems), 0x7F alone:
//   report only.
static int namco_vendor(const unsigned char *p, int n, unsigned char *r, int *k)
{
    int used = 0, data = -1, data2 = -1;
    if (p[0] == 0x70 && n >= 2)
    {
        if (p[1] == 0x18 && n >= 5 && p[2] == 'P' && p[3] == 'L')
        {
            used = p[4] == 0x02 ? 7 : p[4] == 0x14 || p[4] == 0x80 ? 6 : 0;
            data = 0x01;
            if (p[4] == 0x14 && used < n && p[used] != 0x70) // the start's, with the buffer's leftovers
                used = n;
        }
        else if (p[1] == 0x16)
            used = 4;
        else if (p[1] == 0x15)
            used = 4, data = 0x00;
        else if (p[1] == 0x05 && n >= 3)
            used = 3 + p[2], data = 0x01, data2 = 0x00;
    }
    else if (p[0] >= 0x78 && p[0] <= 0x7A)
        used = 15;
    else if (p[0] == 0x7F)
        used = 1;
    if (!used || used > n)
    {
        static int logged;
        if (logged++ < 20)
            wl_log_hex("namco: JVS command not known", p, n);
        return 0;
    }
    static int seen[256];
    unsigned char id = p[0] == 0x70 ? p[1] : p[0];
    if (!seen[id]++)
        wl_log_hex("namco: JVS command", p, used);
    static int sends;
    if (p[0] == 0x70 && p[1] == 0x05 && sends++ < 40)
        wl_log_hex("namco: JVS serial link, sent", p + 3, p[2]);
    r[(*k)++] = 0x01;
    if (data >= 0)
        r[(*k)++] = (unsigned char)data;
    if (data2 >= 0)
        r[(*k)++] = (unsigned char)data2;
    return used;
}

static WlJvsBoard g_jvs = {"COM3", "namco ltd.;NA-JV;Ver6.01;JPN,Multipurpose.", 1, 2, 2, 8, 10, 16, jvs_state, jvs_output, 1, NULL, namco_vendor};

// The steering motor's board on COM1 at 19200 bauds (class clWaJvIo's Pimpl, its thread +0x1f57d0).
// The board speaks in 3 characters, read 3 at a time (+0x1f5d00): "C01" its motor ready (the game
// builds its commands only then, +0x1f64e5), "C06" stopped, "E00".."E20" its errors. Once the game
// is up (+0x202ec4) it sends every 8 ms 10 bytes (+0x1f5c80, built at +0x1f6630): FF FF, a target
// position (low byte, then 6 bits) in the steering's own unit, 0x200 the middle (0x1FF seen with
// the wheel at rest), a spring's stiffness 0..127, 0,
// a damper 0..127, 0, a vibration -128..127, 0. Played here as a spring towards the target plus
// the vibration (the sign to check at the wheel).
static CRITICAL_SECTION g_motorLock;
static int g_motorReady; // "C01" given

static BOOL com1_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    const unsigned char *b = (const unsigned char *)buffer;
    *written = size;
    if (size < 10 || b[0] != 0xFF || b[1] != 0xFF)
    {
        static int logged;
        if (logged++ < 20)
            wl_log_hex("namco: motor board, unknown command", buffer, size);
        return TRUE;
    }
    int target = b[2] | (b[3] & 0x3F) << 8;
    int stiffness = b[4], damper = b[6], vibration = (signed char)b[8];
    double wheel = (wl_input_get(&g_inputs[IN_STEERING]) - 32768) / 32768.0;
    double force = stiffness / 127.0 * (wheel - (target - 0x200) / 512.0) + vibration / 128.0;
    wl_ffb_set(force < -1 ? -1 : force > 1 ? 1 : force);
    static int last[4] = {-1, -1, -1, -1}, logged;
    if ((target != last[0] || stiffness != last[1] || damper != last[2] || vibration != last[3]) && logged++ < 200)
        wl_log("namco: motor target %04x stiffness %d damper %d vibration %d (wheel %.2f)", target, stiffness, damper, vibration, wheel);
    last[0] = target, last[1] = stiffness, last[2] = damper, last[3] = vibration;
    return TRUE;
}

static BOOL com1_read(void *buffer, DWORD size, DWORD *read, LONGLONG offset)
{
    *read = 0;
    EnterCriticalSection(&g_motorLock);
    if (!g_motorReady && size >= 3)
    {
        memcpy(buffer, "C01", 3);
        *read = 3;
        g_motorReady = 1;
        wl_log("namco: motor board ready (C01)");
    }
    LeaveCriticalSection(&g_motorLock);
    return TRUE;
}

static DWORD com1_modem(void)
{
    return MS_CTS_ON | MS_DSR_ON;
}

static DWORD com1_waiting(void)
{
    return g_motorReady ? 0 : 3;
}

static WlVirtualDevice g_com1 = {"COM1", NULL, com1_read, com1_write, com1_modem, com1_waiting};

// v1.10's card reader on COM4, 38400 bauds (v1.18's goes through bngrw.dll): an NFC reader
// speaking NXP's PN532 frames (Sony's RC-S956, Banapass's readers): 00 00 FF, the length (TFI,
// command, data) and its complement, D4 then the command and its data, their sum's complement, 00.
// Each command is acknowledged (00 00 FF 00 FF 00), then answered D5 and the command + 1. No card
// is on it: a poll (4A) finds no target.
static unsigned char g_nfcIn[300], g_nfcOut[1024];
static int g_nfcInLength, g_nfcOutLength;
static CRITICAL_SECTION g_nfcLock;

static void nfc_put(const unsigned char *b, int n)
{
    if (g_nfcOutLength + n <= (int)sizeof g_nfcOut)
    {
        memcpy(g_nfcOut + g_nfcOutLength, b, n);
        g_nfcOutLength += n;
    }
}

static void nfc_answer(int cmd, const unsigned char *data, int n)
{
    static const unsigned char ack[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    nfc_put(ack, 6);
    unsigned char f[64] = {0x00, 0x00, 0xFF, (unsigned char)(2 + n), (unsigned char)(0x100 - (2 + n)), 0xD5, (unsigned char)(cmd + 1)};
    memcpy(f + 7, data, n);
    unsigned char sum = 0;
    for (int i = 5; i < 7 + n; i++)
        sum += f[i];
    f[7 + n] = (unsigned char)(0x100 - sum);
    f[8 + n] = 0x00;
    nfc_put(f, 9 + n);
}

static void nfc_command(const unsigned char *d, int n) // D4, the command, its data
{
    int cmd = d[1];
    static unsigned logged[256];
    if (logged[cmd]++ < 3)
        wl_log_hex("namco: card reader, command", d + 1, n - 1 < 16 ? n - 1 : 16);
    static const unsigned char firmware[4] = {0x32, 0x01, 0x06, 0x07}; // PN532 1.6
    static const unsigned char zero[8] = {0};
    switch (cmd)
    {
    case 0x02: // firmware version
        nfc_answer(cmd, firmware, 4);
        break;
    case 0x4A: // poll for targets: none
    case 0x52: // release
    case 0x16: // power down
        nfc_answer(cmd, zero, 1);
        break;
    case 0x06: // registers read: their values (0s)
        nfc_answer(cmd, zero, n - 2 > 0 ? (n - 2) / 2 : 1);
        break;
    case 0x08: // registers written: a status
        nfc_answer(cmd, zero, 1);
        break;
    case 0x40: // data exchange, communicate through: no target (error 01)
    case 0x42:
    {
        static const unsigned char error = 0x01;
        nfc_answer(cmd, &error, 1);
        break;
    }
    default: // 18 serial speed, 12 parameters, 14 SAM, 32 RF settings, 08 registers written...
        nfc_answer(cmd, NULL, 0);
        break;
    }
}

static BOOL com4_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    const unsigned char *b = (const unsigned char *)buffer;
    *written = size;
    EnterCriticalSection(&g_nfcLock);
    for (DWORD i = 0; i < size; i++)
    {
        if (g_nfcInLength < (int)sizeof g_nfcIn)
            g_nfcIn[g_nfcInLength++] = b[i];
        // A frame: 00 00 FF LEN LCS data[LEN] DCS 00 (an ACK has LEN 0: the host's abort)
        unsigned char *f = g_nfcIn;
        int n = g_nfcInLength;
        while (n >= 3 && !(f[0] == 0x00 && f[1] == 0x00 && f[2] == 0xFF))
            f++, n--; // the wake-up's 55s and anything before a frame
        if (f != g_nfcIn)
        {
            memmove(g_nfcIn, f, n);
            g_nfcInLength = n;
        }
        if (n >= 5 && g_nfcIn[3] == 0 && g_nfcIn[4] == 0xFF)
        {
            g_nfcInLength = n > 6 ? n - 6 : 0; // an ACK
            memmove(g_nfcIn, g_nfcIn + 6, g_nfcInLength);
        }
        else if (n >= 5 && n >= 5 + g_nfcIn[3] + 2)
        {
            if (g_nfcIn[3] >= 2 && g_nfcIn[5] == 0xD4)
                nfc_command(g_nfcIn + 5, g_nfcIn[3]);
            int used = 5 + g_nfcIn[3] + 2;
            g_nfcInLength = n - used;
            memmove(g_nfcIn, g_nfcIn + used, g_nfcInLength);
        }
    }
    LeaveCriticalSection(&g_nfcLock);
    return TRUE;
}

static BOOL com4_read(void *buffer, DWORD size, DWORD *read, LONGLONG offset)
{
    EnterCriticalSection(&g_nfcLock);
    DWORD n = (DWORD)g_nfcOutLength < size ? (DWORD)g_nfcOutLength : size;
    memcpy(buffer, g_nfcOut, n);
    memmove(g_nfcOut, g_nfcOut + n, g_nfcOutLength - n);
    g_nfcOutLength -= n;
    LeaveCriticalSection(&g_nfcLock);
    *read = n;
    return TRUE;
}

static DWORD com4_waiting(void)
{
    return (DWORD)g_nfcOutLength;
}

static WlVirtualDevice g_com4 = {"COM4", NULL, com4_read, com4_write, com1_modem, com4_waiting};

// The Banapass card reader, through bngrw.dll (Namco's library for it, imported by the game): no
// reader on this PC. The game's boot check attaches it (+0x202d5b, +0x201426) and waits for it to
// be ready (+0x201450): the requests (attach, reset, LED, beep, URL) take the reader's number, a
// callback and its context, and the library calls back callback(number, result, context) once
// done; the game's callbacks (+0x2012c0, +0x201330) keep the result, 0 for done, -301 an error.
// BngRwIsCmdExec is 1 while a request runs. Here every request is done at once; no card is ever
// touched (BngRwReqWaitTouch -1, as OpenParrot's OpenBanapass answers it).
typedef void(__cdecl *BngCallback)(int number, int result, void *context);

static int WINAPI bng_init(void)
{
    wl_log("namco: BngRwInit (the card reader, emulated: no card)");
    return 0;
}

static int WINAPI bng_attach(UINT number, char *port, int a, int b, BngCallback callback, void *context)
{
    wl_log("namco: BngRwAttach %u \"%s\": attached", number, port ? port : "");
    if (callback)
        callback((int)number, 0, context);
    return 1;
}

static int WINAPI bng_is_cmd_exec(UINT number)
{
    return 0;
}

static int WINAPI bng_wait_touch(UINT number, int a, UINT b, void *callback, void *context)
{
    return -1; // no card
}

static int WINAPI bng_cancel(UINT number)
{
    return number > 7 ? -100 : 1;
}

static int WINAPI bng_set_led_power(UINT a, UINT b, UINT c)
{
    return 0;
}

static int WINAPI bng_dev_reset(UINT number, BngCallback callback, void *context)
{
    if (callback)
        callback((int)number, 0, context);
    return 1;
}

static int WINAPI bng_request(UINT number, UINT value, BngCallback callback, void *context) // LED, beep
{
    if (callback)
        callback((int)number, 0, context);
    return 1;
}

static int WINAPI bng_send_url(UINT number, int a, UINT b, int *c, char *d, char *e, BngCallback callback, void *context)
{
    if (number > 7 || !d)
        return -100;
    if (callback)
        callback((int)number, 0, context);
    return 1;
}

static void WINAPI bng_fin(void)
{
}

static void bngrw_patch(void)
{
    static const struct
    {
        const char *name;
        void *fn;
    } fns[] = {{"_BngRwInit@0", (void *)bng_init},
               {"_BngRwAttach@24", (void *)bng_attach},
               {"_BngRwIsCmdExec@4", (void *)bng_is_cmd_exec},
               {"_BngRwReqWaitTouch@20", (void *)bng_wait_touch},
               {"_BngRwReqCancel@4", (void *)bng_cancel},
               {"_BngRwSetLedPower@12", (void *)bng_set_led_power},
               {"_BngRwDevReset@12", (void *)bng_dev_reset},
               {"_BngRwReqLed@16", (void *)bng_request},
               {"_BngRwReqBeep@16", (void *)bng_request},
               {"_BngRwReqSendUrlTo@32", (void *)bng_send_url},
               {"_BngRwFin@0", (void *)bng_fin}};
    int n = 0;
    for (int i = 0; i < (int)(sizeof fns / sizeof fns[0]); i++)
    {
        void *was;
        n += wl_patch_iat(GetModuleHandleW(NULL), "bngrw.dll", fns[i].name, fns[i].fn, &was) != 0;
    }
    wl_log("namco: %d of bngrw.dll's functions (the card reader) given by windowsloader", n);
}

// The cabinet's security key, a USB device of its own (interface {3ABF6F2D-71C4-462a-8A92-
// 1E6861E6AF27}, its driver's DeviceIoControl 0x220408 and 0x220410, signed data): +0x4b17b0 finds
// it, reads it and checks it, then fills the cabinet's information (0x628 bytes) the boot check's
// "drive cabinet" step reads (+0x207c50): at +0x428 the key's 12 digits, "2710" the game, a digit,
// a digit of 0..3, 4 or 9, "02", 4 digits. Answered as a present and verified key, as linuxloader
// answers its games' keys: the information filled with such a number, nothing else.
static int __cdecl cabinet_key(void *info)
{
    memset(info, 0, 0x628);
    wcscpy_s((wchar_t *)((char *)info + 0x428), 13, L"271011020001");
    static int logged;
    if (!logged++)
        wl_log("namco: the cabinet's security key, emulated: present, verified (271011020001)");
    return 1;
}

// The cabinets' network: the boot check's link step (+0x31cb20) wants the cabinet's address in
// 192.168.112.11..20 (errors 0x11, 0x13 otherwise) and a gateway (error 0x10); the address is the
// last interface in 192.168. that WSAIoctl(SIO_GET_INTERFACE_LIST) gives (+0x31c3c0), the gateway
// GetAdaptersInfo's. The game's own calls (its imports) see one interface, the first cabinet's
// 192.168.112.11 behind 192.168.112.1; the PC's network is left as it is (the game's attempts to
// change it, ipconfig and routes, are refused by system.cpp).
#define CABINET_ADDRESS 0x0B70A8C0u // 192.168.112.11, in network order
#define CABINET_MASK 0x00FFFFFFu    // 255.255.255.0

struct WlInterfaceInfo // INTERFACE_INFO, 76 bytes
{
    ULONG flags; // IFF_UP 1, IFF_BROADCAST 2, IFF_LOOPBACK 4
    struct
    {
        USHORT family, port;
        ULONG address;
        unsigned char rest[16];
    } address, broadcast, mask;
};

struct WlIpAddrString // IP_ADDR_STRING
{
    WlIpAddrString *next;
    char address[16], mask[16];
    DWORD context;
};

struct WlAdapterInfo // IP_ADAPTER_INFO
{
    WlAdapterInfo *next;
    DWORD comboIndex;
    char name[260], description[132];
    UINT addressLength;
    BYTE mac[8];
    DWORD index;
    UINT type, dhcpEnabled;
    WlIpAddrString *current;
    WlIpAddrString ipAddressList, gatewayList, dhcpServer;
    BOOL haveWins;
    WlIpAddrString primaryWins, secondaryWins;
    __int64 leaseObtained, leaseExpires;
};

static int(WINAPI *oWSAIoctl)(UINT_PTR, DWORD, void *, DWORD, void *, DWORD, DWORD *, void *, void *);
static int WINAPI cabinet_WSAIoctl(UINT_PTR s, DWORD code, void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned, void *ov, void *cr)
{
    if (code != 0x4004747F || !out || outSize < sizeof(WlInterfaceInfo)) // SIO_GET_INTERFACE_LIST
        return oWSAIoctl(s, code, in, inSize, out, outSize, returned, ov, cr);
    memset(out, 0, outSize); // the game counts its entries wrongly (bytes / 24): the others read as down
    WlInterfaceInfo *i = (WlInterfaceInfo *)out;
    i->flags = 1 | 2;
    i->address.family = i->broadcast.family = i->mask.family = 2; // AF_INET
    i->address.address = CABINET_ADDRESS;
    i->broadcast.address = 0xFFFFFFFFu;
    i->mask.address = CABINET_MASK;
    if (returned)
        *returned = sizeof(WlInterfaceInfo);
    static int logged;
    if (!logged++)
        wl_log("namco: the cabinet's network interface given to the game: 192.168.112.11/24");
    return 0;
}

static ULONG WINAPI cabinet_GetAdaptersInfo(void *info, ULONG *size)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    if (!info || *size < sizeof(WlAdapterInfo))
    {
        *size = sizeof(WlAdapterInfo);
        return ERROR_BUFFER_OVERFLOW;
    }
    WlAdapterInfo *a = (WlAdapterInfo *)info;
    memset(a, 0, sizeof *a);
    strcpy_s(a->name, sizeof a->name, "{00000000-0000-0000-0000-000000000011}");
    strcpy_s(a->description, sizeof a->description, "Cabinet network (windowsloader)");
    static const BYTE mac[6] = {0x00, 0xD0, 0xF1, 0x01, 0x02, 0x11};
    a->addressLength = 6;
    memcpy(a->mac, mac, 6);
    a->index = 11;
    a->type = 6; // MIB_IF_TYPE_ETHERNET
    strcpy_s(a->ipAddressList.address, 16, "192.168.112.11");
    strcpy_s(a->ipAddressList.mask, 16, "255.255.255.0");
    strcpy_s(a->gatewayList.address, 16, "192.168.112.1");
    strcpy_s(a->gatewayList.mask, 16, "255.255.255.255");
    *size = sizeof *a;
    return 0;
}

// The game then makes the cabinets' gateway the PC's default route (+0x31c180: each default route
// deleted, one set again) and stops on error 0x10 when that fails: its two calls are taken as
// done, nothing is changed
static DWORD WINAPI cabinet_route(void *row)
{
    static int logged;
    if (!logged++)
        wl_log("namco: the game's change of the default route (the cabinets' gateway) taken as done, the PC's routes left alone");
    return 0;
}

// And pings the cabinets' router, 192.168.112.254 (+0x31c9c0, 4 tries of 100 ms), wanting its data
// back: the router and the cabinet itself answer; any other address is a time-out, nothing is sent
#define CABINET_ROUTER 0xFE70A8C0u // 192.168.112.254, in network order

struct WlEchoReply // ICMP_ECHO_REPLY
{
    ULONG address, status, roundTrip;
    USHORT dataSize, reserved;
    void *data;
    struct
    {
        UCHAR ttl, tos, flags, optionsSize;
        UCHAR *options;
    } options;
};

static DWORD WINAPI cabinet_IcmpSendEcho(HANDLE icmp, ULONG to, void *data, WORD size, void *options, void *reply, DWORD replySize, DWORD timeout)
{
    static int logged;
    if ((to != CABINET_ROUTER && to != CABINET_ADDRESS) || !reply || replySize < sizeof(WlEchoReply) + size) // the game gives no more
    {
        if (logged++ < 10)
            wl_log("namco: ping %lu.%lu.%lu.%lu: no answer (the cabinets' network is the router and this cabinet)", to & 0xFF, to >> 8 & 0xFF,
                   to >> 16 & 0xFF, to >> 24);
        Sleep(timeout < 1000 ? timeout : 1000);
        SetLastError(11010); // IP_REQ_TIMED_OUT
        return 0;
    }
    WlEchoReply *r = (WlEchoReply *)reply;
    memset(r, 0, sizeof *r);
    r->address = to;
    r->roundTrip = 1;
    r->dataSize = size;
    r->data = r + 1;
    r->options.ttl = 128;
    if (size)
        memcpy(r + 1, data, size);
    if (logged++ < 10)
        wl_log("namco: ping %lu.%lu.%lu.%lu answered", to & 0xFF, to >> 8 & 0xFF, to >> 16 & 0xFF, to >> 24);
    return 1;
}

// The network authentication: without its server, the game plays offline only while its last
// authentication (F:/system/last_auth.bin, 56 bytes, "ABGN") is recent enough, +0x62e60, asked at
// 13 places; error 0x39 at boot otherwise. The servers of these cabinets are gone: answered as a
// recent authentication, as the key above
static int WINAPI cabinet_auth_recent(void *auth)
{
    static int logged;
    if (!logged++)
        wl_log("namco: the network authentication taken as recent (offline play allowed)");
    return 1;
}

// Offline, the game also stops 36 hours (129600 s) after it went offline, a date kept in
// F:/system/boot_limit.bin (+0x68430, "オフライン状態開始から規定時間経過したため、起動制限が発生", error
// 0x37): taken as not reached. Its object comes in eax and nothing on the stack: a function
// without arguments answers.
static int __cdecl cabinet_offline_limit(void)
{
    static int logged;
    if (!logged++)
        wl_log("namco: the offline time limit taken as not reached");
    return 0;
}

// ------------------------------------------------------------------------------
// System ES3X: Wangan Midnight Maximum Tune 5 (wmn5r.exe, x64, Direct3D 11, 2014). Being worked on.
// The cabinet's Sentinel HASP key is reached through hasp_windows_x64_109906.dll, imported by
// number: login, logout, get_size, read and decrypt are windowsloader's (the import table's
// entries). The key's memory: 0xD40 bytes, the cabinet's ID at 0xD00 with its sum after it, two
// records at 0x10 and 0x20 (their values, the size and the places from OpenParrot's WMMT5.cpp;
// decrypt leaves the data as it is, as there)
// ------------------------------------------------------------------------------

#define HASP_STATUS_OK 0
#define DONGLE_SIZE 0xD40

static unsigned char g_dongle[DONGLE_SIZE];

static unsigned int __stdcall hasp_login(unsigned int feature, void *vendorCode, unsigned int *handle)
{
    wl_log("hasp: login, feature %u", feature);
    if (handle)
        *handle = 1;
    return HASP_STATUS_OK;
}

static unsigned int __stdcall hasp_logout(unsigned int handle)
{
    wl_log("hasp: logout");
    return HASP_STATUS_OK;
}

static unsigned int __stdcall hasp_get_size(unsigned int handle, unsigned int file, unsigned int *size)
{
    wl_log("hasp: get_size, file %u", file);
    if (size)
        *size = DONGLE_SIZE;
    return HASP_STATUS_OK;
}

static unsigned int __stdcall hasp_read(unsigned int handle, unsigned int file, unsigned int offset, unsigned int length, void *buffer)
{
    static int logged;
    if (logged++ < 50)
        wl_log("hasp: read, file %u, offset 0x%x, %u bytes", file, offset, length);
    if (!buffer)
        return HASP_STATUS_OK;
    memset(buffer, 0, length);
    if (offset < DONGLE_SIZE)
        memcpy(buffer, g_dongle + offset, offset + length <= DONGLE_SIZE ? length : DONGLE_SIZE - offset);
    return HASP_STATUS_OK;
}

static unsigned int __stdcall hasp_decrypt(unsigned int handle, void *buffer, unsigned int length)
{
    static int logged;
    if (logged++ < 20)
        wl_log("hasp: decrypt, %u bytes (left as they are)", length);
    return HASP_STATUS_OK;
}

static void wangan_dongle(void)
{
    static const unsigned char record[16] = {0, 0, 0, 0x01, 0, 0, 0, 0x0A, 0, 0, 0, 0x04, 0x3B, 0x6B, 0x40, 0x87};
    g_dongle[0] = 0x01;
    memcpy(g_dongle + 0x10, record, sizeof record);
    memcpy(g_dongle + 0x20, record, sizeof record);
    // The cabinet's ID (not a terminal: 13 at its 5th place, 11 for a terminal), its bytes' sum
    // and that sum's complement at the memory's end
    static const char id[] = "272213990002";
    unsigned sum = 0;
    for (int i = 0; i < 12; i++)
        sum += (unsigned char)id[i];
    memcpy(g_dongle + 0xD00, id, 12);
    g_dongle[0xD3E] = (unsigned char)sum;
    g_dongle[0xD3F] = (unsigned char)~sum;
    static const struct
    {
        int ordinal;
        void *function;
        const char *name;
    } hasp[] = {
        {13, (void *)hasp_login, "hasp_login"}, {14, (void *)hasp_logout, "hasp_logout"}, {7, (void *)hasp_get_size, "hasp_get_size"},
        {15, (void *)hasp_read, "hasp_read"},   {2, (void *)hasp_decrypt, "hasp_decrypt"},
    };
    HMODULE exe = GetModuleHandleW(NULL);
    for (int i = 0; i < (int)(sizeof hasp / sizeof hasp[0]); i++)
        if (!wl_patch_iat(exe, "hasp_windows_x64_109906.dll", MAKEINTRESOURCEA(hasp[i].ordinal), hasp[i].function, NULL))
            wl_log("hasp: %s (#%d) not in the import table", hasp[i].name, hasp[i].ordinal);
}

static int exists(const wchar_t *dir, const wchar_t *name)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s%s", dir, name);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

int wl_game_namco(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    wchar_t dir[MAX_PATH];
    wcsncpy_s(dir, MAX_PATH, exePath, base - exePath);
    int wmmt5 = !_wcsicmp(base, L"wmn5r.exe");
    if (!wmmt5 && _wcsicmp(base, L"MK_AGP3_FINAL.exe"))
        return 0;

    // Its drives F:\ and G:\ (system, bookkeeping, dongle data, test mode settings) are the copy's
    // F* and G* folders and files: F:\system\x is Fsystem\x, G:\boot_limit_N.bin Gboot_limit_N.bin
    char folder[MAX_PATH], to[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, dir, -1, folder, sizeof folder, NULL, NULL);
    snprintf(to, sizeof to, "%sF", folder);
    wl_files_redirect_prefix("F:\\", to);
    snprintf(to, sizeof to, "%sG", folder);
    wl_files_redirect_prefix("G:\\", to);

    if (wmmt5)
    {
        *name = "wmmt5";
        *title = "Wangan Midnight Maximum Tune 5";
        wl_log("game: %s (Namco System ES3X)", *title);
        wl_game_set_boards("Controls on JVS board, Namco NA-JV Ver6.01, JVS 3.0 (COM3)", "Motor board: Namco motor board (COM1)");
        wl_inputs_set_table(g_inputs, IN_COUNT);
        wl_jvs_register(&g_jvs);
        InitializeCriticalSection(&g_motorLock);
        wl_vdev_register(&g_com1);
        wl_vdev_label("COM1", "Carte du moteur du volant");
        wangan_dongle();
        // E0505 "network: interface not found": the game looks for its card in the cabinets'
        // network, 192.168.92.0/24 (its addresses table, +0x18a7260: .254, .11, .20, .253)
        static const unsigned char mac[6] = {0x00, 0x18, 0xD8, 0x00, 0x00, 0x0B};
        wl_netenv_fake(0xC0A85C0B, 0xC0A85CFE, mac);
        // E1911 "USB DONGLE error2": the key's ID is compared with the cabinet's, given by its
        // "auth" COM class (not registered here: "auth: can't get instance(80040154)"); that one
        // empty is the error (+0x58c63a). The error's flag stays clear (mov r12b, 1 made 0, as
        // OpenParrot's WMMT5.cpp does)
        unsigned char *exe = (unsigned char *)GetModuleHandleW(NULL);
        static const unsigned char set[3] = {0x41, 0xB4, 0x01}, clear[3] = {0x41, 0xB4, 0x00};
        wl_patch_code(exe + 0x58C6CA, set, clear, 3, "the key's ID left unchecked (wmn5r.exe+0x58c6ca)");
        // "Connecting to Wangan terminal": a cabinet alone has no terminal (the card machine of the
        // arcade). The scanner's three jumps made NOPs (+0x87bd68, OpenParrot's "disregard
        // terminal scanner stuff" for a cabinet that is not a terminal)
        static const unsigned char je6[6] = {0x0F, 0x84, 0x88, 0x00, 0x00, 0x00}, nop6[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        static const unsigned char je2[2] = {0x74, 0x60}, jne2[2] = {0x75, 0x4F}, nop2[2] = {0x90, 0x90};
        wl_patch_code(exe + 0x87BD6E, je6, nop6, 6, "no terminal (wmn5r.exe+0x87bd6e)");
        wl_patch_code(exe + 0x87BD77, je2, nop2, 2, "no terminal (wmn5r.exe+0x87bd77)");
        wl_patch_code(exe + 0x87BD7D, jne2, nop2, 2, "no terminal (wmn5r.exe+0x87bd7d)");
        return 1;
    }
    // v1.18 has the card reader's bngrw.dll beside it; v1.10 (2015) has not, nor its patches' places
    int v118 = exists(dir, L"bngrw.dll");
    *name = "mkdx";
    *title = "Mario Kart Arcade GP DX";
    wl_log("game: %s %s (Namco System ES3)", *title, v118 ? "v1.18" : "v1.10");
    wl_game_set_boards("Controls on JVS board, Namco NA-JV Ver6.01, JVS 3.0 (COM3)",
                       "Motor board: Namco motor board (COM1, 19200 bauds: position servo with spring, damper, vibration)");
    wl_inputs_set_table(g_inputs, IN_COUNT);
    wl_vdinput_register(&g_wheel);
    wl_jvs_register(&g_jvs);
    InitializeCriticalSection(&g_motorLock);
    wl_vdev_register(&g_com1);
    wl_vdev_label("COM1", "Carte du moteur du volant");

    // ShareK's client not started: je to the function's end made a jmp (nop; jmp rel32). The exe
    // moves (ASLR): addresses from its base
    unsigned char *exe = (unsigned char *)GetModuleHandleW(NULL);
    static const unsigned char je[2] = {0x0F, 0x84}, jmp[2] = {0x90, 0xE9};
    if (v118)
    {
        wl_patch_code(exe + 0x7E80D, je, jmp, 2, "ShareK's client not started (its COM class is not registered)");
        bngrw_patch();
    }
    // The boot check's link step looks for the other cabinets by broadcast on UDP 20199: alone, the
    // cabinet hears itself (nothing leaves the PC)
    wl_system_loopback_broadcast(1);
    void *was;
    if (wl_patch_iat((HMODULE)exe, "WS2_32.dll", "WSAIoctl", (void *)cabinet_WSAIoctl, (void **)&oWSAIoctl) &&
        wl_patch_iat((HMODULE)exe, "IPHLPAPI.DLL", "GetAdaptersInfo", (void *)cabinet_GetAdaptersInfo, &was))
        wl_log("game: the cabinets' network shown to the game (192.168.112.11, gateway 192.168.112.1)");
    wl_patch_iat((HMODULE)exe, "IPHLPAPI.DLL", "DeleteIpForwardEntry", (void *)cabinet_route, &was);
    wl_patch_iat((HMODULE)exe, "IPHLPAPI.DLL", "SetIpForwardEntry", (void *)cabinet_route, &was);
    wl_patch_iat((HMODULE)exe, "IPHLPAPI.DLL", "IcmpSendEcho", (void *)cabinet_IcmpSendEcho, &was);
    if (v118)
    {
        wl_hook_code(exe + 0x4B17B0, (void *)cabinet_key, &was, "the cabinet's security key (MK_AGP3_FINAL.exe+0x4b17b0)");
        wl_hook_code(exe + 0x62E60, (void *)cabinet_auth_recent, &was, "the network authentication's age (MK_AGP3_FINAL.exe+0x62e60)");
        wl_hook_code(exe + 0x68430, (void *)cabinet_offline_limit, &was, "the offline time limit (MK_AGP3_FINAL.exe+0x68430)");
    }
    else // v1.10: the same key's function (found by the key's interface GUID it looks for)
    {
        wl_hook_code(exe + 0x43AEF0, (void *)cabinet_key, &was, "the cabinet's security key (MK_AGP3_FINAL.exe+0x43aef0, v1.10)");
        // The same two as v1.18's: the first use of "last_auth.bin" (v1.18's +0x62e60, the same
        // start), the _time64 compared with the date it went offline (the same bytes)
        wl_hook_code(exe + 0x5B060, (void *)cabinet_auth_recent, &was, "the network authentication's age (MK_AGP3_FINAL.exe+0x5b060, v1.10)");
        wl_hook_code(exe + 0x5F630, (void *)cabinet_offline_limit, &was, "the offline time limit (MK_AGP3_FINAL.exe+0x5f630, v1.10)");
        InitializeCriticalSection(&g_nfcLock);
        wl_vdev_register(&g_com4);
        wl_vdev_label("COM4", "Lecteur de cartes");
    }
    return 1;
}
