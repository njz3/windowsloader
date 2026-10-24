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

// Sega RingEdge and RingWide, the platforms of Initial D Arcade Stage 6 AA (id6_dump_.exe, OpenGL),
// K.O. Drive (M-DriveR_RingWide.exe, Direct3D 9) and Sonic & Sega All-Stars Racing (game.exe,
// Direct3D 9), all x86. The game reaches the PC board
// through Sega's amLib, linked into it:
// - drivers: \\.\mxsram (battery-backed SRAM, read and written as a disk), \\.\mxsuperio (the
//   hardware monitor of the Super I/O chip), \\.\mxsmbus (SMBus: EEPROM, a PCA9535 port expander),
//   \\.\columba;
// - the JVS I/O board on COM4 (Initial D) or through the \\.\mxjvs driver (K.O. Drive), the drive
//   board (the wheel's motor) on COM1;
// - the platform's services by PCP (pcp.cpp) on 127.0.0.1:40100 and up: keychip, storage, network.
// The SRAM's geometry is that of segatools (common/amex/sram.c, Unlicense).

#include <winsock2.h>
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "driveboard.h"
#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "jvs.h"
#include "log.h"
#include "pcp.h"
#include "vdev.h"

// What differs from one game to the other
struct RingGame
{
    const wchar_t *exes[3];
    const wchar_t *testMode; // the cabinet's test mode program
    const wchar_t *marker;   // a file beside, for generic names (the test mode: exes[0] when NULL)
    const char *name, *title;
    const char *gameid;        // the keychip's
    const wchar_t *table;      // the keychip's answers, from the game's folder
    const char *platform;      // "AAL" RingEdge, "AAM" RingWide
    const char *maker, *board; // the PC's, as the BIOS gives them
    WORD driverVersion;        // of the drivers' interface: 0x100 RingEdge amLib, 1 RingWide's
    const char *jvsPort, *jvsPort2; // K.O. Drive: the game on \\.\mxjvs, its TestMode.exe on COM4
    WlInput *inputs;
    int inputCount;
    const int (*switches)[2]; // an input, its bit in player 1's two switch bytes
    int switchCount;
    int freeWheelTest; // the drive board's power-on test wants the wheel to turn (driveboard.h)
    int linkUp;        // the network card's link: Sonic wants it (query_nic_status 0) to ask its address
    const char *saveDrive; // a drive of the cabinet's, the game's folder here (Sonic's Y:\ssr_save.bin)
    int freePlayOption;    // the option FREE_PLAY (eeprom_system)
};

static const RingGame *g_game;
static wchar_t g_dir[MAX_PATH];

// ------------------------------------------------------------------------------
// SRAM: 512 KB kept in a file beside the game
// ------------------------------------------------------------------------------

#define SRAM_SIZE 0x80000

static HANDLE g_sramFile = INVALID_HANDLE_VALUE;

static HANDLE sram_file(void)
{
    if (g_sramFile == INVALID_HANDLE_VALUE)
    {
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%swl_sram.bin", g_dir);
        wl_busy_enter();
        g_sramFile = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
        if (g_sramFile != INVALID_HANDLE_VALUE)
        {
            LARGE_INTEGER size;
            if (GetFileSizeEx(g_sramFile, &size) && size.QuadPart < SRAM_SIZE)
            {
                LARGE_INTEGER end;
                end.QuadPart = SRAM_SIZE;
                SetFilePointerEx(g_sramFile, end, NULL, FILE_BEGIN);
                SetEndOfFile(g_sramFile);
            }
        }
        wl_busy_leave();
    }
    return g_sramFile;
}

static BOOL sram_io(void *buffer, DWORD size, DWORD *done, LONGLONG offset, int write)
{
    HANDLE f = sram_file();
    if (f == INVALID_HANDLE_VALUE || offset >= SRAM_SIZE)
    {
        *done = 0;
        return f != INVALID_HANDLE_VALUE;
    }
    if (offset + size > SRAM_SIZE)
        size = (DWORD)(SRAM_SIZE - offset);
    OVERLAPPED ov;
    memset(&ov, 0, sizeof ov);
    ov.Offset = (DWORD)offset;
    wl_busy_enter();
    BOOL ok = write ? WriteFile(f, buffer, size, done, &ov) : ReadFile(f, buffer, size, done, &ov);
    wl_busy_leave();
    return ok;
}

static BOOL sram_read(void *buffer, DWORD size, DWORD *read, LONGLONG offset)
{
    return sram_io(buffer, size, read, offset, 0);
}

static BOOL sram_write(const void *buffer, DWORD size, DWORD *written, LONGLONG offset)
{
    return sram_io((void *)buffer, size, written, offset, 1);
}

static BOOL zero_ioctl(DWORD code, const void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned);

// The sector size, asked after the geometry by K.O. Drive's amSramInit (+0x7bb3ea)
#define MXSRAM_IOCTL_GET_SECTOR_SIZE 0x9C406004

static BOOL sram_ioctl(DWORD code, const void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned)
{
    if (code == MXSRAM_IOCTL_GET_SECTOR_SIZE && outSize >= 4)
    {
        *(DWORD *)out = 4;
        *returned = 4;
        return TRUE;
    }
    if (code == IOCTL_DISK_GET_DRIVE_GEOMETRY && outSize >= sizeof(DISK_GEOMETRY))
    {
        DISK_GEOMETRY *g = (DISK_GEOMETRY *)out;
        memset(g, 0, sizeof *g);
        g->Cylinders.QuadPart = SRAM_SIZE / 4;
        g->TracksPerCylinder = 1;
        g->SectorsPerTrack = 1;
        g->BytesPerSector = 4;
        *returned = sizeof *g;
        return TRUE;
    }
    return zero_ioctl(code, in, inSize, out, outSize, returned);
}

static const WlVirtualDevice g_sram = {"\\\\.\\mxsram", sram_ioctl, sram_read, sram_write, NULL, NULL};

// ------------------------------------------------------------------------------
// The other drivers, still unknown: zeros
// ------------------------------------------------------------------------------

// The version of the driver's interface, asked by amLib of each one (K.O. Drive's amSramInit
// +0x7bb2f3 wants 1 in the low word)
#define MX_IOCTL_GET_ABI_VERSION 0x9C406000

static BOOL zero_ioctl(DWORD code, const void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned)
{
    if (code == MX_IOCTL_GET_ABI_VERSION && outSize >= 2)
    {
        memset(out, 0, outSize);
        *(WORD *)out = g_game->driverVersion;
        *returned = outSize;
        return TRUE;
    }
    if (out)
        memset(out, 0, outSize);
    *returned = outSize;
    return TRUE;
}

// The Super I/O chip, as amHmGetLPCChipId reads it (K.O. Drive +0x7a9b60), for chips 0 and 1:
// its LPC registers {chip, 0x0b, register, value}: the ID in 0x20 and 0x21, 0xA020 or 0xA23x (a
// Fintek); its hardware monitor {chip, register, value}: register 0x48, the monitor's SMBus
// address, 0x10 to 0x6f
#define MXSUPERIO_IOCTL_LPC_READ 0x9C402004
#define MXSUPERIO_IOCTL_HWMON_READ 0x9C40200C

static BOOL superio_ioctl(DWORD code, const void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned)
{
    const unsigned char *req = (const unsigned char *)in;
    unsigned char *r = (unsigned char *)out;
    if (code == MXSUPERIO_IOCTL_LPC_READ && inSize >= 4 && outSize >= 4)
    {
        unsigned char reg = req[2];
        memmove(r, req, 3);
        r[3] = reg == 0x20 ? 0xA0 : reg == 0x21 ? 0x20 : 0;
        *returned = 4;
        return TRUE;
    }
    if (code == MXSUPERIO_IOCTL_HWMON_READ && inSize >= 3 && outSize >= 3)
    {
        unsigned char reg = req[1];
        memmove(r, req, 2);
        r[2] = reg == 0x48 ? 0x2D : 0;
        *returned = 3;
        return TRUE;
    }
    return zero_ioctl(code, in, inSize, out, outSize, returned);
}

static const WlVirtualDevice g_superio = {"\\\\.\\mxsuperio", superio_ioctl, NULL, NULL, NULL, NULL};

// ------------------------------------------------------------------------------
// SMBus, as the RingWide's amLib uses it (K.O. Drive amEepromInit +0x7b1190, amDipsw +0x7a0a0d).
// A request: {status, protocol, address, command, length, 32 bytes}, 0x9C402004; with a 16-bit
// command, {status, protocol, address (16 bits), command (16 bits), length, 32 bytes}, 0x9C40200C.
// The driver sets the status, 0 when done. Protocols seen: 3 a probe, 4 write a byte, 5 read a
// byte (the DIP switches' PCA9535), 8 write and 9 read a block (the EEPROM, its 16-bit offset as
// the command). The EEPROM is kept in a file beside the game, the other devices' registers in
// memory.
// ------------------------------------------------------------------------------

#define MXSMBUS_IOCTL_GET_VERSION 0x9C406008
#define MXSMBUS_IOCTL_REQUEST 0x9C402004
#define MXSMBUS_IOCTL_REQUEST_EX 0x9C40200C
#define EEPROM_SIZE 0x10000

static HANDLE g_eepromFile = INVALID_HANDLE_VALUE;
static unsigned char g_smbusRegs[128][256];

// The PCA9535 port expander's inputs (amDipsw): port 0 the DIP switches, port 1 bits 2 and 3 the
// board's Test and Service push buttons (amDipswGetPushsw +0x7a1f54); all active low
#define PCA9535_ADDRESS 0x20

static unsigned char pca9535_input(int port); // with the inputs, below

// The system records (amSysData) start with a CRC32 of their other bytes and have a copy 0x200
// further. The first, AM_SYSDATA_STATIC, is written by the cabinet's system, not by the game: its
// byte 0x0C is the board's region (1 Japan, 2, 4 export, 8 China), its 16 bytes at 0x0F the board's
// serial. Sonic & Sega All-Stars Racing starts its credits (amCredit, +0x414ed0) only with a region
// among those (+0x414950), so a blank or broken one is written with the keychip's export region.
static unsigned crc32(const unsigned char *p, int n)
{
    unsigned c = 0xFFFFFFFF;
    for (int i = 0; i < n; i++)
    {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = c & 1 ? c >> 1 ^ 0xEDB88320 : c >> 1;
    }
    return ~c;
}

#define RECORD 0x20

static int record_read(HANDLE file, unsigned char *r, DWORD at)
{
    OVERLAPPED ov;
    memset(&ov, 0, sizeof ov);
    ov.Offset = at;
    DWORD done = 0;
    return ReadFile(file, r, RECORD, &done, &ov) && done == RECORD && *(const unsigned *)r == crc32(r + 4, RECORD - 4);
}

static void record_write(HANDLE file, unsigned char *r, DWORD at)
{
    *(unsigned *)r = crc32(r + 4, RECORD - 4);
    for (DWORD copy = at; copy <= at + 0x200; copy += 0x200)
    {
        OVERLAPPED ov;
        memset(&ov, 0, sizeof ov);
        ov.Offset = copy;
        DWORD done = 0;
        WriteFile(file, r, RECORD, &done, &ov);
    }
}

// AM_SYSDATA_CREDIT (0x20) as Sonic writes it in a blank EEPROM (coins); its byte 0x0A is the free
// play. Sonic's test menu has no coin settings (the cabinet's system menu has them): the option
// FREE_PLAY sets it.
static const unsigned char g_creditDefault[RECORD] = {0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01,
                                                      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00};

static void eeprom_system(HANDLE file)
{
    unsigned char r[RECORD];
    if (!record_read(file, r, 0))
    {
        memset(r, 0, sizeof r);
        r[0x0C] = 4; // export, as keychip.appboot.region
        memcpy(r + 0x0F, "AAMN-01A00000001", 16); // made up
        record_write(file, r, 0);
        wl_log("eeprom: AM_SYSDATA_STATIC written (region export, serial AAMN-01A00000001)");
    }
    if (!g_game->freePlayOption)
        return;
    int free = wl_game_option_bool("FREE_PLAY");
    if (!record_read(file, r, RECORD))
    {
        if (!free)
            return; // the game writes its own
        memcpy(r, g_creditDefault, sizeof r);
    }
    if (r[0x0A] != free)
    {
        r[0x0A] = (unsigned char)free;
        record_write(file, r, RECORD);
        wl_log("eeprom: AM_SYSDATA_CREDIT set to %s", free ? "free play" : "coins");
    }
}

static void eeprom_io(void *data, int length, int offset, int write)
{
    if (offset + length > EEPROM_SIZE)
        length = EEPROM_SIZE - offset;
    if (length <= 0)
        return;
    wl_busy_enter();
    if (g_eepromFile == INVALID_HANDLE_VALUE)
    {
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%swl_eeprom.bin", g_dir);
        g_eepromFile = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
        if (g_eepromFile != INVALID_HANDLE_VALUE)
            eeprom_system(g_eepromFile);
    }
    OVERLAPPED ov;
    memset(&ov, 0, sizeof ov);
    ov.Offset = (DWORD)offset;
    DWORD done = 0;
    if (write)
        WriteFile(g_eepromFile, data, (DWORD)length, &done, &ov);
    else if (!ReadFile(g_eepromFile, data, (DWORD)length, &done, &ov) || done < (DWORD)length)
        memset((unsigned char *)data + done, 0, length - done); // past the file's end: blank
    wl_busy_leave();
}

static BOOL smbus_ioctl(DWORD code, const void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned)
{
    if (code == MXSMBUS_IOCTL_GET_VERSION && outSize >= 4)
    {
        *(DWORD *)out = g_game->driverVersion;
        *returned = 4;
        return TRUE;
    }
    int ex = code == MXSMBUS_IOCTL_REQUEST_EX;
    DWORD size = ex ? 39 : 37;
    if ((code != MXSMBUS_IOCTL_REQUEST && !ex) || inSize < size || outSize < size)
        return zero_ioctl(code, in, inSize, out, outSize, returned);
    unsigned char *r = (unsigned char *)out;
    memmove(r, in, size);
    int protocol = r[1];
    int address = ex ? *(WORD *)(r + 2) : r[2];
    int command = ex ? *(WORD *)(r + 4) : r[3];
    int length = ex ? r[6] : r[4];
    unsigned char *data = r + (ex ? 7 : 5);
    if (length > 32)
        length = 32;
    unsigned char *regs = g_smbusRegs[address & 0x7F];
    switch (protocol)
    {
    case 4: // write a byte
        regs[command & 0xFF] = data[0];
        break;
    case 5: // read a byte
        data[0] = address == PCA9535_ADDRESS && command < 2 ? pca9535_input(command) : regs[command & 0xFF];
        break;
    case 8: // write a block
        eeprom_io(data, length, command, 1);
        break;
    case 9: // read a block
        eeprom_io(data, length, command, 0);
        break;
    }
    static int logged;
    if (protocol != 9 && logged++ < 40)
        wl_log("smbus: protocol %d address %02x command %04x length %d data %02x", protocol, address, command, length, data[0]);
    r[0] = 0; // done
    *returned = size;
    return TRUE;
}

static const WlVirtualDevice g_smbus = {"\\\\.\\mxsmbus", smbus_ioctl, NULL, NULL, NULL, NULL};

// The RingWide's amLib finds the SMBus driver by its interface (amEepromInit, K.O. Drive +0x7b1012)
static const GUID g_smbusInterface = {0x5C49E1FE, 0x3FEC, 0x4B8D, {0xA4, 0xB5, 0x76, 0xBE, 0x70, 0x25, 0xD8, 0x42}};

// ------------------------------------------------------------------------------
// columba: reads the PC's physical memory. amOemstring (+0x807740) reads the BIOS area
// 0xF0000-0xFFFFF, looks for the legacy DMI entry point "_DMI_" (table length at +6, address at
// +8), reads the table and keeps, by their order in each structure: type 0 strings 1 and 2 (BIOS
// version, date), type 1 string 0 (maker: "NEC" is a RingEdge, "Supermicro" or "Advantech" a
// RingWide), type 11 string 2 (the platform: "AAL" a RingEdge, "AAM" a RingWide; amLib takes it
// first, K.O. Drive +0x7bbcd6). Request: address (8 bytes), access width, byte count. It
// wants all 64 KB of the table's read back even past 1 MB: the table is put below, at 0xE0000.
// ------------------------------------------------------------------------------

#define COLUMBA_IOCTL_READ 0x9C406104
#define BIOS_AREA 0xE0000 // to 0xFFFFF: the DMI table, then the BIOS with its entry point

static unsigned char g_bios[0x20000];

static int dmi_structure(unsigned char *p, int type, WORD handle, const char *const *strings, int count)
{
    int n = 0;
    p[n++] = (unsigned char)type;
    p[n++] = 4; // formatted area: the header only
    p[n++] = (unsigned char)handle;
    p[n++] = (unsigned char)(handle >> 8);
    for (int i = 0; i < count; i++)
    {
        size_t len = strlen(strings[i]) + 1;
        memcpy(p + n, strings[i], len);
        n += (int)len;
    }
    if (!count)
        p[n++] = 0;
    p[n++] = 0;
    return n;
}

static void build_bios(void)
{
    static const char *const bios[] = {"American Megatrends Inc.", "080015", "05/21/2010"};
    const char *const system[] = {g_game->maker, g_game->board};
    const char *const oem[] = {"SEGA", g_game->board, g_game->platform};
    unsigned char *table = g_bios;
    int n = 0;
    n += dmi_structure(table + n, 0, 0, bios, 3);
    n += dmi_structure(table + n, 1, 1, system, 2);
    n += dmi_structure(table + n, 11, 2, oem, 3);
    n += dmi_structure(table + n, 127, 3, NULL, 0);

    unsigned char *ep = g_bios + 0x10000; // the 15-byte DMI entry point, at 0xF0000
    memcpy(ep, "_DMI_", 5);
    *(WORD *)(ep + 6) = (WORD)n;
    *(DWORD *)(ep + 8) = BIOS_AREA;
    *(WORD *)(ep + 12) = 4; // structures
    ep[14] = 0x24;          // DMI 2.4
    unsigned char sum = 0;
    for (int i = 0; i < 15; i++)
        sum += ep[i];
    ep[5] = (unsigned char)(0x100 - sum);
}

static BOOL columba_ioctl(DWORD code, const void *in, DWORD inSize, void *out, DWORD outSize, DWORD *returned)
{
    if (code != COLUMBA_IOCTL_READ || inSize < 16)
        return zero_ioctl(code, in, inSize, out, outSize, returned);
    const unsigned char *req = (const unsigned char *)in;
    ULONGLONG address = *(const ULONGLONG *)req;
    DWORD count = *(const DWORD *)(req + 12);
    if (count > outSize)
        count = outSize;
    for (DWORD i = 0; i < count; i++)
    {
        ULONGLONG a = address + i;
        ((unsigned char *)out)[i] = a >= BIOS_AREA && a < BIOS_AREA + sizeof g_bios ? g_bios[a - BIOS_AREA] : 0;
    }
    *returned = count;
    return TRUE;
}

static const WlVirtualDevice g_columba = {"\\\\.\\columba", columba_ioctl, NULL, NULL, NULL, NULL};

// ------------------------------------------------------------------------------
// The JVS I/O board: Sega's type 3 board (837-14572). Analog channels 0 steering, 1 accelerator,
// 2 brake; the switches of player 1 are the game's own.
// ------------------------------------------------------------------------------

// The inputs all the games have, first in each game's table
enum
{
    IN_STEERING,
    IN_GAS,
    IN_BRAKE,
    IN_START,
    IN_SERVICE,
    IN_TEST,
    IN_COIN,
    IN_COMMON
};

#define COMMON_INPUTS                                         \
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 255, 128},     \
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 255, 0},    \
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 255, 0},        \
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},          \
    {"Service", "P1_Service", WL_INPUT_SWITCH, 0, 1, 0},      \
    {"Test", "Test", WL_INPUT_SWITCH, 0, 1, 0},               \
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0}

// Initial D: byte 0 bit 7 Start, 6 Service, 5 to 2 the shifter's up, down, left, right, 1 the view
enum
{
    ID6_VIEW = IN_COMMON,
    ID6_SHIFT_UP,
    ID6_SHIFT_DOWN,
    ID6_SHIFT_LEFT,
    ID6_SHIFT_RIGHT,
    ID6_COUNT
};

static WlInput g_id6Inputs[ID6_COUNT] = {
    COMMON_INPUTS,
    {"ViewChange", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftUp", "GearUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftDown", "GearDown", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftLeft", "ShiftLeft", WL_INPUT_SWITCH, 0, 1, 0},
    {"ShiftRight", "ShiftRight", WL_INPUT_SWITCH, 0, 1, 0},
};

static const int g_id6Switches[][2] = {{IN_START, 15}, {IN_SERVICE, 14}, {ID6_SHIFT_UP, 13}, {ID6_SHIFT_DOWN, 12},
                                       {ID6_SHIFT_LEFT, 11}, {ID6_SHIFT_RIGHT, 10}, {ID6_VIEW, 9}};

// K.O. Drive: one button, Start and the weapon (byte 0 bit 1, as its test mode shows it), and
// Service (bit 6); Test and Service are also the board's push buttons (pca9535_input)
enum
{
    KOD_COUNT = IN_COMMON
};

static WlInput g_kodInputs[KOD_COUNT] = {COMMON_INPUTS};

static const int g_kodSwitches[][2] = {{IN_START, 9}, {IN_SERVICE, 14}};

// Let's Go Safari (GameSampR_RingWide.exe, 2014): a jeep, its test mode (GameTestR_RingWide.exe)
// shows the wheel (HANDLE), the four course buttons, a lever (up, down), firework, horn, wiper and
// view change. The game wants the I/O board 837-14895 (6402 "JVS I/O board does not fulfill the
// game spec" otherwise, +0x17100): one player, 14 switches, at least 16 outputs. Its test mode
// reads three switch bytes: the course buttons byte 1 bits 7 to 4, view, wiper, horn and firework
// bits 0 to 3; the lever byte 2 bits 2 (up) and 3 (down), the wheel's switches bits 6 (left) and
// 7 (right). The wheel on channel 0
enum
{
    LGS_COURSE1 = IN_COMMON,
    LGS_COURSE2,
    LGS_COURSE3,
    LGS_COURSE4,
    LGS_VIEW,
    LGS_WIPER,
    LGS_HORN,
    LGS_FIREWORK,
    LGS_LEVER_UP,
    LGS_LEVER_DOWN,
    LGS_COUNT
};

static WlInput g_lgsInputs[LGS_COUNT] = {
    COMMON_INPUTS,
    {"Course1", "Course1", WL_INPUT_SWITCH, 0, 1, 0},
    {"Course2", "Course2", WL_INPUT_SWITCH, 0, 1, 0},
    {"Course3", "Course3", WL_INPUT_SWITCH, 0, 1, 0},
    {"Course4", "Course4", WL_INPUT_SWITCH, 0, 1, 0},
    {"ViewChange", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"Wiper", "Wiper", WL_INPUT_SWITCH, 0, 1, 0},
    {"Horn", "Horn", WL_INPUT_SWITCH, 0, 1, 0},
    {"Firework", "Firework", WL_INPUT_SWITCH, 0, 1, 0},
    {"LeverUp", "GearUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"LeverDown", "GearDown", WL_INPUT_SWITCH, 0, 1, 0},
};

static const int g_lgsSwitches[][2] = {{IN_SERVICE, 14}, {LGS_COURSE1, 7}, {LGS_COURSE2, 6}, {LGS_COURSE3, 5}, {LGS_COURSE4, 4},
                                       {LGS_VIEW, 0},    {LGS_WIPER, 1},   {LGS_HORN, 2},    {LGS_FIREWORK, 3}};

static unsigned char pca9535_input(int port)
{
    if (port == 0)
        return 0xFF;
    unsigned char v = 0xFF;
    if (wl_input_get(&g_game->inputs[IN_TEST]))
        v &= ~0x04;
    if (wl_input_get(&g_game->inputs[IN_SERVICE]))
        v &= ~0x08;
    return v;
}

// Sonic & Sega All-Stars Racing, as its test mode (gametest.exe) shows them: byte 0 bit 7 Start,
// 6 Service, byte 1 bit 0 the item button; the third channel is its drift pedal
enum
{
    SSR_ITEM = IN_COMMON,
    SSR_COUNT
};

static WlInput g_ssrInputs[SSR_COUNT] = {
    COMMON_INPUTS,
    {"Item", "Item", WL_INPUT_SWITCH, 0, 1, 0},
};

static const int g_ssrSwitches[][2] = {{IN_START, 15}, {IN_SERVICE, 14}, {SSR_ITEM, 8}};

// Sega Racing Classic (d1a.exe, 2009), as its test mode reads them (+0xdb690): player 1's byte 0
// bit 7 Start, 6 Service, 5..2 the views VR SW1 to VR SW4; the 4-speed H shifter on player 2's
// byte 0, bit 5 up, 4 down, 3 left: SHIFT1 left and up, SHIFT2 left and down, SHIFT3 up, SHIFT4
// down. A real lever stays in its gear: the gear here is kept, chosen by Gear1..Gear4 or stepped
// by GearUp / GearDown
enum
{
    SRC_VIEW1 = IN_COMMON,
    SRC_VIEW2,
    SRC_VIEW3,
    SRC_VIEW4,
    SRC_GEAR1,
    SRC_GEAR2,
    SRC_GEAR3,
    SRC_GEAR4,
    SRC_GEAR_UP,
    SRC_GEAR_DOWN,
    SRC_COUNT
};

static WlInput g_srcInputs[SRC_COUNT] = {
    COMMON_INPUTS,
    {"View1", "View1", WL_INPUT_SWITCH, 0, 1, 0},
    {"View2", "View2", WL_INPUT_SWITCH, 0, 1, 0},
    {"View3", "View3", WL_INPUT_SWITCH, 0, 1, 0},
    {"View4", "View4", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear1", "Gear1", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear2", "Gear2", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear3", "Gear3", WL_INPUT_SWITCH, 0, 1, 0},
    {"Gear4", "Gear4", WL_INPUT_SWITCH, 0, 1, 0},
    {"GearUp", "GearUp", WL_INPUT_SWITCH, 0, 1, 0},
    {"GearDown", "GearDown", WL_INPUT_SWITCH, 0, 1, 0},
};

static const int g_srcSwitches[][2] = {{IN_START, 15}, {IN_SERVICE, 14}, {SRC_VIEW1, 13},
                                       {SRC_VIEW2, 12}, {SRC_VIEW3, 11}, {SRC_VIEW4, 10}};

// The H shifter's gear (1..4), on player 2's switches
static unsigned short src_shifter(void)
{
    static int gear = 1, was[6];
    const WlInput *in = g_game->inputs;
    for (int i = 0; i < 6; i++)
    {
        int now = wl_input_get(&in[SRC_GEAR1 + i]);
        if (now && !was[i])
        {
            if (i < 4)
                gear = i + 1;
            else if (i == 4 && gear < 4)
                gear++;
            else if (i == 5 && gear > 1)
                gear--;
        }
        was[i] = now;
    }
    // byte 0 in bits 15..8: up 0x2000, down 0x1000, left 0x0800
    static const unsigned short bits[4] = {0x2800, 0x1800, 0x2000, 0x1000};
    return bits[gear - 1];
}

// Sega Racing Classic's drive board: the game keeps its one-byte command, the Model 2 and 3
// boards', in its data (VA 0x834C19, as FFBArcadePlugin's SegaRacingClassic.cpp reads it; the
// executable has a fixed base); nothing sends it out here. The high nibble the effect, the low its
// step (0 the strongest, 7 the weakest): 9x roll right, Ax roll left, Bx uncentering (a shake),
// Cx centering (a spring), Dx clutch (a friction). Played as the constant force ffb.cpp takes,
// from the wheel's position: > 0 pushes left.
#define SRC_DRIVE_COMMAND 0x834C19

static void src_drive_board(int steering)
{
    static int last = 128, logged;
    static unsigned char was;
    static DWORD start;
    unsigned char c = *(volatile unsigned char *)SRC_DRIVE_COMMAND;
    if (c != was && logged < 300)
    {
        logged++;
        wl_log("driveboard: command %02x", c);
    }
    was = c;
    double at = (steering - 128) / 128.0, speed = (steering - last) / 16.0; // > 0 turned / turning right
    last = steering;
    double step = (8 - (c & 0x0F)) / 8.0, force = 0;
    if ((c & 0x0F) > 7)
        step = 0;
    switch (c >> 4)
    {
    case 0x9:
        force = -step; // roll right
        break;
    case 0xA:
        force = step; // roll left
        break;
    case 0xB:
    {
        if (!start)
            start = GetTickCount();
        double t = (GetTickCount() - start) / 1000.0;
        force = step * 0.5 * (((int)(t * 20) & 1) ? 1 : -1); // a 10 Hz shake
        break;
    }
    case 0xC:
        force = step * at; // back to the centre
        break;
    case 0xD:
        force = step * (speed > 1 ? 1 : speed < -1 ? -1 : speed); // against the wheel's motion
        break;
    }
    wl_ffb_set(force);
}

// Let's Go Safari's outputs (its output test): byte 0 bit 7 the coin blocker (set: off), 2 the
// vibration, 1 and 0 the course 1 and 2 lamps; byte 1 bits 7 to 2 the course 3 and 4, firework,
// horn, wiper and view change lamps. The vibration stays on while the jeep drives.
static volatile int g_lgsVibration;

static void lgs_output(const unsigned char *data, int length)
{
    int on = length > 0 && (data[0] & 0x04);
    if (on != g_lgsVibration)
        wl_log("outputs: vibration %s", on ? "on" : "off");
    g_lgsVibration = on;
}

static void jvs_state(WlJvsState *s)
{
    WlInput *in = g_game->inputs;
    static int coinWas;
    static unsigned short coins;
    int coin = wl_input_get(&in[IN_COIN]);
    if (coin && !coinWas)
        coins++;
    coinWas = coin;

    s->system = wl_input_get(&in[IN_TEST]) ? 0x80 : 0;
    for (int i = 0; i < g_game->switchCount; i++)
        if (wl_input_get(&in[g_game->switches[i][0]]))
            s->player[0] |= (unsigned short)(1u << g_game->switches[i][1]);
    if (g_game->inputs == g_srcInputs)
        s->player[1] |= src_shifter();
    s->coins[0] = coins;
    int steering;
    if (!wl_driveboard_free_wheel(&steering))
        steering = wl_input_get(&in[IN_STEERING]);
    if (g_game->inputs == g_lgsInputs)
    {
        if (wl_input_get(&in[LGS_LEVER_UP]))
            s->player3[0] |= 0x04;
        if (wl_input_get(&in[LGS_LEVER_DOWN]))
            s->player3[0] |= 0x08;
        // The wheel's switches, HANDLE L and R in its test mode: the game reads no analog channel,
        // the wheel turned a quarter of the way closes one
        if (steering <= 96)
            s->player3[0] |= 0x40;
        if (steering >= 160)
            s->player3[0] |= 0x80;
        // The vibration, played as a light shake at 10 Hz
        wl_ffb_set(g_lgsVibration ? (((GetTickCount() / 50) & 1) ? 0.25 : -0.25) : 0);
    }
    if (g_game->inputs == g_srcInputs)
        src_drive_board(steering);
    s->analog[0] = (unsigned short)(steering << 8);
    s->analog[1] = (unsigned short)(wl_input_get(&in[IN_GAS]) << 8);
    s->analog[2] = (unsigned short)(wl_input_get(&in[IN_BRAKE]) << 8);
}

static WlJvsBoard g_jvs = {NULL, "SEGA ENTERPRISES,LTD.;I/O BD JVS;837-14572;Ver1.00;2005/10", 2, 2, 2, 8, 10, 20, jvs_state, NULL};

// ------------------------------------------------------------------------------
// PCP services
// ------------------------------------------------------------------------------

// An answer starts with the request's command and its value (amMasterCheckResponse +0x800620 wants
// them back), a "?" replaced by the value asked for; then the fields of that command. A field
// "code" other than 0 would be an error.
static const struct
{
    const char *command, *value, *fields;
} g_answers[] = {
    {"mxmaster.foreground.getcount", NULL, "count=1"}, // the game programs installed: 0 is error 0949
    {"keychip.appboot.region", "4", ""}, // export
    {"keychip.appboot.modeltype", "2", ""},
    {"keychip.appboot.systemflag", "0", ""},
    {"keychip.appboot.networkaddr", "192.168.1.0", ""},
    {"keychip.billing.keyid", "A72E-01A12345678", ""},
    {"keychip.version", "0104", ""},
};

// keychip.ds.compute=<challenge, 7 bytes in hex>&page=<0-3>: the keychip's DS28CN01 signs the
// challenge with the secret of that page. The game checks the answer against its own table
// (Initial D: SBUU_Table.dat, +0x7f01a7; K.O. Drive's is missing from its copies, TeknoParrot's
// included): 87-byte records, the challenge then its four 20-byte answers, one per
// page, each answer's bytes shuffled in the file (the order below, found by comparing the record
// in the game's memory with the file's). The answer is taken from there. One time in ten the game
// sends a random challenge it does not check: any answer does.
static unsigned char *g_dsTable;
static DWORD g_dsSize;

#define DS_RECORD 87

static const unsigned char g_dsOrder[20] = {15, 5, 13, 14, 10, 1, 2, 11, 16, 7, 4, 18, 12, 6, 3, 0, 17, 8, 19, 9};

static int ds_compute(const char *challenge, int page, char *answer, size_t size)
{
    unsigned char c[7];
    if (strlen(challenge) != 14)
        return 0;
    for (int i = 0; i < 7; i++)
    {
        unsigned v;
        if (sscanf(challenge + 2 * i, "%2x", &v) != 1)
            return 0;
        c[i] = (unsigned char)v;
    }
    if (!g_dsTable)
    {
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%s%s", g_dir, g_game->table);
        wl_busy_enter();
        HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (f != INVALID_HANDLE_VALUE)
        {
            g_dsSize = GetFileSize(f, NULL);
            g_dsTable = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, g_dsSize);
            DWORD rd = 0;
            if (!g_dsTable || !ReadFile(f, g_dsTable, g_dsSize, &rd, NULL) || rd != g_dsSize)
                g_dsSize = 0;
            CloseHandle(f);
        }
        wl_busy_leave();
    }
    if (page < 0 || page > 3)
        page = 0;
    for (DWORD i = 0; g_dsTable && i + DS_RECORD <= g_dsSize && i < 10000 * DS_RECORD; i += DS_RECORD)
        if (!memcmp(g_dsTable + i, c, 7))
        {
            size_t n = 0;
            for (int k = 0; k < 20 && n + 3 < size; k++)
                n += snprintf(answer + n, size - n, "%02X", g_dsTable[i + 7 + page * 20 + g_dsOrder[k]]);
            return 1;
        }
    return 0;
}

// keychip.ssd.proof=<16 bytes in hex>: the second part of the same file, after the 10000 records
// above: 32-byte records, the challenge then its 16-byte answer, shuffled too
#define SSD_START (10000 * DS_RECORD)
#define SSD_RECORD 32

static const unsigned char g_ssdOrder[16] = {6, 8, 4, 12, 7, 13, 1, 10, 2, 3, 11, 14, 15, 0, 5, 9};

static int ssd_proof(const char *challenge, char *answer, size_t size)
{
    unsigned char c[16];
    if (strlen(challenge) != 32 || !g_dsTable)
        return 0;
    for (int i = 0; i < 16; i++)
    {
        unsigned v;
        if (sscanf(challenge + 2 * i, "%2x", &v) != 1)
            return 0;
        c[i] = (unsigned char)v;
    }
    for (DWORD i = SSD_START; i + SSD_RECORD <= g_dsSize; i += SSD_RECORD)
        if (!memcmp(g_dsTable + i, c, 16))
        {
            size_t n = 0;
            for (int k = 0; k < 16 && n + 3 < size; k++)
                n += snprintf(answer + n, size - n, "%02X", g_dsTable[i + 16 + g_ssdOrder[k]]);
            return 1;
        }
    return 0;
}

// An answer to a challenge the table has not: bytes made from it, never all zero (Initial D 8 said
// "Keychip Not Found", error 0949, once after a random challenge answered with zeros)
static void made_up_answer(const char *challenge, int bytes, char *answer, size_t size)
{
    unsigned x = 0x9E3779B9u;
    for (const char *c = challenge; *c; c++)
        x = (x ^ (unsigned char)*c) * 0x01000193u;
    size_t n = 0;
    for (int k = 0; k < bytes && n + 3 < size; k++)
    {
        x ^= x << 13, x ^= x >> 17, x ^= x << 5;
        n += snprintf(answer + n, size - n, "%02X", (x >> 8) & 0xFF);
    }
}

static int pcp_answer(const char *request, char *response, size_t size)
{
    char command[128];
    size_t n = strcspn(request, "=&");
    if (n >= sizeof command)
        n = sizeof command - 1;
    memcpy(command, request, n);
    command[n] = 0;
    char value[256];
    wl_pcp_value(request, command, value, sizeof value);
    if (!strcmp(command, "keychip.ds.compute"))
    {
        char answer[64], page[8];
        wl_pcp_value(request, "page", page, sizeof page);
        if (!ds_compute(value, atoi(page), answer, sizeof answer))
        {
            wl_log("keychip: challenge %s not in %S (a random one, not checked)", value, g_game->table);
            made_up_answer(value, 20, answer, sizeof answer);
        }
        snprintf(response, size, "%s=%s", command, answer);
        return 1;
    }
    if (!strcmp(command, "keychip.ssd.proof"))
    {
        char answer[64];
        if (!ssd_proof(value, answer, sizeof answer))
        {
            wl_log("keychip: proof %s not in %S (a random one, not checked)", value, g_game->table);
            made_up_answer(value, 16, answer, sizeof answer);
        }
        snprintf(response, size, "%s=%s", command, answer);
        return 1;
    }
    // The network (amNetwork) and gcatcher (amGcatcher) services: "request=<operation>&..." is
    // answered "response=<operation>&result=success&<fields>"
    if (!strcmp(command, "request"))
    {
        static const struct
        {
            const char *operation, *fields;
        } ops[] = {
            {"query_nic_status", "status=1"}, // no link (Initial D: "Network error 8301", then offline)
            {"query_ip_address", "ip_address=192.168.1.4"},
            {"query_subnetmask", "subnetmask=255.255.255.0"},
            {"query_gateway", "gateway=192.168.1.254"},
            {"query_primary_dns", "primary_dns=192.168.1.254"},
            {"query_secondary_dns", "secondary_dns=0.0.0.0"},
            {"query_mac_address", "mac_address=00:D0:F1:01:02:03"},
            // gcatcher, the update server's client: nothing to fetch (+0x7eb060)
            {"isupdate", "incompatible=0&server=127.0.0.1&originalf=notavailable&originalb=notavailable&patchf=notavailable&patchb=notavailable"},
        };
        const char *fields = "";
        for (int i = 0; i < (int)(sizeof ops / sizeof ops[0]); i++)
            if (!strcmp(value, ops[i].operation))
                fields = ops[i].fields;
        // The DHCP client: Sonic & Sega All-Stars Racing (+0x4169f0) starts it (status=1) and waits
        // for its status 3, an address obtained, before it asks for the address
        static int dhcp;
        char dhcpFields[32];
        if (!strcmp(value, "update_dhcp_status"))
        {
            char status[8];
            wl_pcp_value(request, "status", status, sizeof status);
            dhcp = atoi(status) ? 3 : 0;
        }
        if (!strcmp(value, "query_nic_status") && g_game->linkUp)
            fields = "status=0";
        if (!strcmp(value, "query_dhcp_status"))
        {
            snprintf(dhcpFields, sizeof dhcpFields, "dhcp_status=%d", dhcp);
            fields = dhcpFields;
        }
        snprintf(response, size, "response=%s&result=success%s%s", value, fields[0] ? "&" : "", fields);
        return 1;
    }
    if (!strcmp(command, "keychip.appboot.gameid") || !strcmp(command, "keychip.appboot.platformid"))
    {
        snprintf(response, size, "%s=%s", command, command[16] == 'g' ? g_game->gameid : g_game->platform);
        return 1;
    }
    for (int i = 0; i < (int)(sizeof g_answers / sizeof g_answers[0]); i++)
        if (!strcmp(command, g_answers[i].command))
        {
            const char *v = !strcmp(value, "?") && g_answers[i].value ? g_answers[i].value : value;
            snprintf(response, size, "%s=%s%s%s", command, v, g_answers[i].fields[0] ? "&" : "", g_answers[i].fields);
            return 1;
        }
    // Not known yet: its own words back, the log shows it
    snprintf(response, size, "%s=%s", command, value);
    return 1;
}

// ------------------------------------------------------------------------------

// ------------------------------------------------------------------------------
// K.O. Drive's copies have no keychip table (fs\table is empty, TeknoParrot's included). Its
// amDongleSetAuthConfig (+0x7a4da0) takes the path "toolmode", Sega's own way to run without one:
// the game's pointer to the table's path is moved to that word, both found in its image.
// ------------------------------------------------------------------------------

static unsigned char *image_find(unsigned char *from, unsigned char *end, const void *pattern, size_t length)
{
    for (unsigned char *p = from; p + length <= end; p++)
        if (*p == *(const unsigned char *)pattern && !memcmp(p, pattern, length))
            return p;
    return NULL;
}

static void use_toolmode(void)
{
    unsigned char *base = (unsigned char *)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    unsigned char *end = base + nt->OptionalHeader.SizeOfImage;
    char table[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, g_game->table, -1, table, sizeof table, NULL, NULL);
    unsigned char *path = image_find(base, end, table, strlen(table) + 1);
    unsigned char *word = image_find(base, end, "toolmode", 9);
    // Let's Go Safari builds the path, sprintf(path, "%s\\%s_Table.dat", folder, gameid): the
    // format pushed is replaced by that word, which the call then copies as the path
    static const char format[] = "%s\\%s_Table.dat";
    unsigned char *fmt = path ? NULL : image_find(base, end, format, sizeof format);
    if (fmt && word)
    {
        unsigned char push[5] = {0x68};
        DWORD fmtVa = (DWORD)(ULONG_PTR)fmt, wordVa = (DWORD)(ULONG_PTR)word;
        memcpy(push + 1, &fmtVa, 4);
        unsigned char *at = image_find(base, end, push, 5);
        if (at)
        {
            wl_patch_code(at + 1, &fmtVa, &wordVa, 4, "the keychip's table replaced by its toolmode");
            return;
        }
    }
    if (!path || !word)
    {
        wl_log("keychip: no table, and the game has not the toolmode way: it will stop on error 0949");
        return;
    }
    // mov dword ptr [variable], path
    DWORD pathVa = (DWORD)(ULONG_PTR)path, wordVa = (DWORD)(ULONG_PTR)word;
    unsigned char *at = base;
    while ((at = image_find(at, end, &pathVa, 4)) != NULL)
    {
        if (at - 6 >= base && at[-6] == 0xC7 && at[-5] == 0x05)
        {
            wl_patch_code(at, &pathVa, &wordVa, 4, "the keychip's table replaced by its toolmode");
            return;
        }
        at++;
    }
    wl_log("keychip: no table, and the game's reference to it not found: it will stop on error 0949");
}

#define COUNT(a) (int)(sizeof a / sizeof a[0])

static const WlGameOption g_creditOptions[] = {
    {"FREE_PLAY", "Jeu gratuit",
     "Décoché : une partie par pièce. Le menu de test du jeu n'a pas ce réglage (c'est celui du système de la borne qui l'a).",
     WL_OPTION_BOOL, NULL, 0, 0, "true", 1, NULL},
};

static const RingGame g_games[] = {
    {{L"id6_dump_.exe", L"InitialD6_GLW_RE_SBUU.exe"}, NULL, NULL, "initiald6", "Initial D Arcade Stage 6 AA", "SBUU",
     L"SBUU_Table.dat", "AAL", "NEC", "RingEdge", 0x100, "COM4", NULL, g_id6Inputs, ID6_COUNT, g_id6Switches, COUNT(g_id6Switches), 0, 0, NULL, 0},
    // Initial D 7 and 8: the same cabinet as 6, the JVS board through the \\.\mxjvs driver (error
    // 6401 otherwise), the card unit through picodaemon.exe (cardrw.cpp); the decrypted executables
    // first (Initial D 8's _dumped_ lacks the manifest of the VC 2008 runtime: the redumped first)
    {{L"InitialD7_GLW_RE_SBYD_dumped_.exe", L"InitialD7_GLW_RE_SBYD.exe"}, NULL, L"SBYD_Table.dat", "initiald7", "Initial D Arcade Stage 7 AAX",
     "SBYD", L"SBYD_Table.dat", "AAL", "NEC", "RingEdge", 0x100, "\\\\.\\mxjvs", "COM4", g_id6Inputs, ID6_COUNT, g_id6Switches, COUNT(g_id6Switches), 0,
     0, NULL, 0},
    {{L"InitialD8_GLW_RE_SBZZ_redumped_.exe", L"InitialD8_GLW_RE_SBZZ_redumped4k_.exe", L"InitialD8_GLW_RE_SBZZ_dumped_.exe"}, NULL,
     L"SBZZ_Table.dat", "initiald8", "Initial D Arcade Stage 8 Infinity", "SBZZ", L"SBZZ_Table.dat", "AAL", "NEC", "RingEdge", 0x100, "\\\\.\\mxjvs",
     "COM4", g_id6Inputs, ID6_COUNT, g_id6Switches, COUNT(g_id6Switches), 0, 0, NULL, 0},
    {{L"M-DriveR_RingWide.exe", L"M-DriveR_RingWide 1080P.exe", L"M-DriveR_RingWide original.exe"}, L"TestMode.exe", NULL, "kodrive", "K.O. Drive",
     "SBXA", L"..\\fs\\table\\SBXA_Table.dat", "AAM", "Supermicro", "RingWide", 1, "\\\\.\\mxjvs", "COM4", g_kodInputs, KOD_COUNT,
     g_kodSwitches, COUNT(g_kodSwitches), 1, 0, NULL, 0},
    {{L"game.exe"}, L"gametest.exe", L"SBVH_Table.dat", "ssasr", "Sonic & Sega All-Stars Racing", "SBVH", L"SBVH_Table.dat", "AAM",
     "Supermicro", "RingWide", 1, "\\\\.\\mxjvs", "COM4", g_ssrInputs, SSR_COUNT, g_ssrSwitches, COUNT(g_ssrSwitches), 1, 1, "Y:", 1},
    // Its test mode is the same program with -t
    {{L"d1a.exe"}, NULL, L"rom\\SBTN_Table.dat", "src", "Sega Racing Classic", "SBTN", L"rom\\SBTN_Table.dat", "AAL", "NEC",
     "RingEdge", 0x100, "COM4", NULL, g_srcInputs, SRC_COUNT, g_srcSwitches, COUNT(g_srcSwitches), 0, 0, NULL, 0},
    // No keychip table in its copies: the toolmode (use_toolmode)
    {{L"GameSampR_RingWide.exe"}, L"GameTestR_RingWide.exe", NULL, "lgs", "Let's Go Safari", "SDAX", L"SDAX_Table.dat", "AAM",
     "Supermicro", "RingWide", 1, "\\\\.\\mxjvs", "COM4", g_lgsInputs, LGS_COUNT, g_lgsSwitches, COUNT(g_lgsSwitches), 0, 0, NULL, 0},
};

int wl_game_ringedge(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    wcsncpy_s(g_dir, MAX_PATH, exePath, base - exePath);
    for (int i = 0; i < COUNT(g_games) && !g_game; i++)
    {
        const RingGame *g = &g_games[i];
        int known = 0;
        for (int k = 0; k < 3; k++)
            if (g->exes[k] && !_wcsicmp(base, g->exes[k]))
                known = 1;
        int test = g->testMode && !_wcsicmp(base, g->testMode);
        wchar_t marker[MAX_PATH];
        swprintf(marker, MAX_PATH, L"%s%s", g_dir, g->marker ? g->marker : g->exes[0]);
        if ((known && !g->marker) || ((known || test) && GetFileAttributesW(marker) != INVALID_FILE_ATTRIBUTES))
            g_game = g;
    }
    if (!g_game)
        return 0;
    *name = g_game->name;
    *title = g_game->title;
    wl_log("game: %s (Sega %s)", *title, g_game->board);
    static char controls[96];
    int lgs = g_game->inputs == g_lgsInputs;
    snprintf(controls, sizeof controls, "Controls on JVS board, Sega %s, JVS 3.0 (%s)", lgs ? "837-14895" : "837-14572",
             !strcmp(g_game->jvsPort, "\\\\.\\mxjvs") ? "mxjvs driver" : g_game->jvsPort);
    wl_game_set_boards(controls, g_game->inputs == g_srcInputs ? "Motor board: Sega drive board, Model 2/3 commands (read in the game's data)"
                                 : lgs                         ? "Motor board: none, the JVS board's vibration output played as a shake"
                                                               : "Motor board: Sega-like servoboard (COM1)");
    if (g_game->freePlayOption)
        wl_game_set_options(g_creditOptions, COUNT(g_creditOptions));

    wchar_t table[MAX_PATH];
    swprintf(table, MAX_PATH, L"%s%s", g_dir, g_game->table);
    if (GetFileAttributesW(table) == INVALID_FILE_ATTRIBUTES)
        use_toolmode();

    if (g_game->saveDrive)
    {
        char folder[MAX_PATH];
        WideCharToMultiByte(CP_ACP, 0, g_dir, -1, folder, sizeof folder, NULL, NULL);
        size_t n = strlen(folder);
        if (n && folder[n - 1] == '\\')
            folder[n - 1] = 0;
        wl_files_redirect(g_game->saveDrive, folder);
    }

    build_bios();
    wl_inputs_set_table(g_game->inputs, g_game->inputCount);
    g_jvs.port = g_game->jvsPort;
    if (lgs)
    {
        // Its board, 837-14895 in its "FE MODE" (the identity TeknoParrot gives it)
        g_jvs.id = "SEGA CORPORATION;I/O BD JVS;837-14895;1;1;FE MODE;2005/10";
        g_jvs.players = 1;
        g_jvs.switches = 14;
        g_jvs.outputs = 16;
        g_jvs.output = lgs_output;
    }
    wl_jvs_register(&g_jvs);
    if (g_game->jvsPort2)
        wl_jvs_add_port(g_game->jvsPort2);
    wl_driveboard_register("COM1");
    if (g_game->freeWheelTest)
        wl_driveboard_free_wheel_test();
    wl_vdev_register(&g_sram);
    wl_vdev_register(&g_superio);
    wl_vdev_register(&g_smbus);
    wl_vdev_interface(&g_smbusInterface, g_smbus.name);
    wl_vdev_register(&g_columba);
    // 40114: Let's Go Safari asks it six times, 12 s each, before its test mode starts
    static const unsigned short ports[] = {40100, 40102, 40104, 40106, 40108, 40110, 40112, 40114};
    static const char *names[] = {"40100", "40102", "40104", "40106", "40108", "40110", "40112", "40114"};
    for (int i = 0; i < 8; i++)
        wl_pcp_serve(ports[i], names[i], pcp_answer);
    return 1;
}
