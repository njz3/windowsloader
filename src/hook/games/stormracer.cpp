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

// Storm Racer G (bin\racing.exe, x86, Direct3D 9, OpenAL). Its cabinet's board, on USB
// (EasyUSB214x), is reached through usb_io.dll, imported by the game (C++ names): windowsloader's
// functions replace the import table's entries, the board is never opened. Read in usb_io.dll:
//   UsbIO_InitSystem(n)        > 0 when the board answers (the game tries n = 0 and 1)
//   UsbIO_GetInputdata(data)   12 bytes: two words of buttons, then four analog bytes, the wheel
//                              (centre 0x80), the accelerator, the brake and the boost lever (a
//                              button when below SetBoostThreshold's value: second word bit 2).
//                              The game packs the words in one (+0x9f790): first word's bits
//                              1 -> 0x200, 4 -> 0x100, 8 -> 0x20, 0x10 -> 0x40; the second's as they are
//   UsbIO_ReadSysCoins(slot)   the board's coin counter (the same for every slot);
//   UsbIO_DecCoin(1, n)        takes n coins off it
//   UsbIO_SetSteeringStatu(s)  the wheel's motor, a position servo: s[2] its strength, s[3]..s[4]
//                              the window (0..255, the wheel's units) it holds the wheel in, s[5]
//                              the mode (3 at start: 100, 0x7f..0x80, the centre)
//   UsbIO_SetMoterVolt(v)      the motor's power, 0 (off) or 100
// The lamps, the seat's shakes, the road and the collisions are logged only. usb_cam.dll (a camera
// taking the players' pictures) is answered "not connected": the PC's own camera is never opened.
// Which button is which is still to be seen in the game's test menu ("Button Testing").

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "log.h"

enum
{
    IN_STEERING,
    IN_GAS,
    IN_BRAKE,
    IN_BOOST,
    IN_COIN,
    IN_BUTTONS, // the game's word bits, in g_bits' order
    IN_COUNT = IN_BUTTONS + 8
};

static WlInput g_inputs[IN_COUNT] = {
    {"Steering", "P1_Steer", WL_INPUT_AXIS, 0, 255, 128},
    {"Accelerator", "P1_Gas", WL_INPUT_ANALOG, 0, 255, 0},
    {"Brake", "P1_Brake", WL_INPUT_ANALOG, 0, 255, 0},
    {"Boost", "Nitro", WL_INPUT_SWITCH, 0, 1, 0},
    {"Coin", "P1_Coin", WL_INPUT_SWITCH, 0, 1, 0},
    {"Start", "P1_Start", WL_INPUT_SWITCH, 0, 1, 0},
    {"Button1", "ViewChange", WL_INPUT_SWITCH, 0, 1, 0},
    {"Button8", "P1_Up", WL_INPUT_SWITCH, 0, 1, 0},
    {"Button10", "P1_Down", WL_INPUT_SWITCH, 0, 1, 0},
    {"Button20", "P1_Left", WL_INPUT_SWITCH, 0, 1, 0},
    {"Button40", "P1_Right", WL_INPUT_SWITCH, 0, 1, 0},
    {"Button100", "Test", WL_INPUT_SWITCH, 0, 1, 0},
    {"Button200", "P1_Service", WL_INPUT_SWITCH, 0, 1, 0},
};

// For each button: the word (0 the first, 1 the second) and its bit in usb_io.dll's data. The game's
// own bit (its packed word, +0x9f790) is in the agent's name; Start is the second word's 2.
static const struct
{
    int word;
    unsigned bit;
} g_bits[IN_COUNT - IN_BUTTONS] = {
    {1, 0x02}, // Start, the game's 0x002
    {1, 0x01}, // the game's 0x001
    {1, 0x08}, // 0x008
    {1, 0x10}, // 0x010
    {0, 0x08}, // 0x020
    {0, 0x10}, // 0x040
    {0, 0x04}, // 0x100
    {0, 0x01}, // 0x200
};

// The first call of each function the game makes, logged
static void first_call(const char *name)
{
    static const char *seen[48];
    static int count;
    for (int i = 0; i < count; i++)
        if (seen[i] == name)
            return;
    if (count < 48)
        seen[count++] = name;
    wl_log("storm racer: the game calls %s", name);
}

// ------------------------------------------------------------------------------
// usb_io.dll's functions, as the board answers them (cdecl)
// ------------------------------------------------------------------------------

struct InputData
{
    unsigned first, second;
    unsigned char wheel, gas, brake, boost;
};

struct SteeringStatus
{
    int unknown, unknown2, strength, low, high, mode;
};

static volatile LONG g_coins, g_boostThreshold = 0x80, g_motorPower = 100;
static volatile LONG g_strength, g_low = 0x7f, g_high = 0x80;

// The motor holds the wheel in its window with its strength: a spring towards the window's middle
static void motor_out(int wheel)
{
    double target = ((g_low + g_high) / 2.0 - 127.5) / 127.5, at = (wheel - 127.5) / 127.5;
    double k = g_motorPower ? g_strength / 1000.0 : 0;
    if (k > 1)
        k = 1;
    // the game's wheel units grow to the right; > 0 pushes left
    wl_ffb_set(k * (at - target));
}

static int __cdecl io_InitSystem(int n)
{
    wl_log("storm racer: UsbIO_InitSystem(%d): the board is windowsloader's", n);
    return 1;
}

static void __cdecl io_ExitSystem(void)
{
}

static bool __cdecl io_CheckConnection(void)
{
    return true;
}

static int __cdecl io_GetInputdata(InputData *d)
{
    first_call("UsbIO_GetInputdata");
    memset(d, 0, sizeof *d);
    for (int i = 0; i < IN_COUNT - IN_BUTTONS; i++)
        if (wl_input_get(&g_inputs[IN_BUTTONS + i]))
            (g_bits[i].word ? d->second : d->first) |= g_bits[i].bit;
    d->wheel = (unsigned char)wl_input_get(&g_inputs[IN_STEERING]);
    d->gas = (unsigned char)wl_input_get(&g_inputs[IN_GAS]);
    d->brake = (unsigned char)wl_input_get(&g_inputs[IN_BRAKE]);
    // The boost lever: pressed below the game's threshold
    int boost = wl_input_get(&g_inputs[IN_BOOST]);
    d->boost = boost ? 0 : 0xFF;
    if (boost)
        d->second |= 0x04;
    static int coinWas;
    int coin = wl_input_get(&g_inputs[IN_COIN]);
    if (coin && !coinWas)
        InterlockedIncrement(&g_coins);
    coinWas = coin;
    static unsigned was[2];
    if (d->first != was[0] || d->second != was[1])
    {
        static int logged;
        if (logged++ < 60)
            wl_log("storm racer: buttons %02x %02x", d->first, d->second);
        was[0] = d->first;
        was[1] = d->second;
    }
    motor_out(d->wheel);
    return 1;
}

static int __cdecl io_ReadSysCoins(unsigned slot)
{
    first_call("UsbIO_ReadSysCoins");
    return (int)(g_coins & 0xFF);
}

static int __cdecl io_DecCoin(unsigned short unit, unsigned short count)
{
    first_call("UsbIO_DecCoin");
    LONG now = g_coins - count;
    InterlockedExchange(&g_coins, now < 0 ? 0 : now);
    return 0;
}

static int __cdecl io_CleanCoin(unsigned short unit)
{
    first_call("UsbIO_CleanCoin");
    InterlockedExchange(&g_coins, 0);
    return 0;
}

static int __cdecl io_ReadFailDecCoins(unsigned slot)
{
    return 0;
}

static int __cdecl io_SetSteeringStatu(const SteeringStatus *s)
{
    if (s->strength != -1)
        InterlockedExchange(&g_strength, s->strength);
    if (s->low != -1)
        InterlockedExchange(&g_low, s->low);
    if (s->high != -1)
        InterlockedExchange(&g_high, s->high);
    static int logged;
    if (logged++ < 40)
        wl_log("storm racer: steering mode %d, strength %d, window %d..%d", s->mode, s->strength, s->low, s->high);
    return 1;
}

static void __cdecl io_SetSteerCenter(int centre)
{
    first_call("UsbIO_SetSteerCenter");
    InterlockedExchange(&g_low, centre - 1);
    InterlockedExchange(&g_high, centre + 1);
}

static void __cdecl io_SetMoterVolt(int volts)
{
    if (volts == -1)
        return;
    if (InterlockedExchange(&g_motorPower, volts) != volts)
        wl_log("storm racer: motor power %d", volts);
}

static void __cdecl io_SetBoostThreshold(int threshold)
{
    InterlockedExchange(&g_boostThreshold, threshold);
}

// The outputs the board drives beyond the wheel: logged the first time
static void __cdecl io_SetDrifting(float drift)
{
    first_call("UsbIO_SetDrifting");
}

static void __cdecl io_SetMenuGear(bool on)
{
    first_call("UsbIO_SetMenuGear");
}

static void __cdecl io_SetCollisionData(void *data)
{
    first_call("UsbIO_SetCollisionData");
}

static void __cdecl io_SetBoostEffect(bool on)
{
    first_call("UsbIO_SetBoostEffect");
}

static void __cdecl io_OutputCarStatuData(void *data)
{
    first_call("UsbIO_OutputCarStatuData");
}

static int __cdecl io_SetOutputdata(void *data)
{
    first_call("UsbIO_SetOutputdata");
    return 1;
}

static void __cdecl io_SetRoadBasicStatu(void *data)
{
    first_call("UsbIO_SetRoadBasicStatu");
}

static void __cdecl io_SetSeatShakeImpluse(int a, int b, float c, bool d)
{
    first_call("UsbIO_SetSeatShakeImpluse");
}

static void __cdecl io_SetGamePlayStatu(bool playing)
{
    first_call("UsbIO_SetGamePlayStatu");
}

static void __cdecl io_SetPowerPause(void)
{
    first_call("UsbIO_SetPowerPause");
}

static void __cdecl io_SetRoadType(int type)
{
    first_call("UsbIO_SetRoadType");
}

static void __cdecl io_SetLedLightStatu(void *data)
{
    first_call("UsbIO_SetLedLightStatu");
}

static void __cdecl io_SetPWMLightStatu(void *data)
{
    first_call("UsbIO_SetPWMLightStatu");
}

static int __cdecl io_SetLightState(void *data)
{
    return 0;
}

static int __cdecl io_GetVersion(void)
{
    return 1;
}

static int __cdecl io_GetIOBroadID(void)
{
    return 1;
}

// The board's user memory: the real usb_io.dll answers 0 without reading or writing it
static int __cdecl io_UserControlMemory(unsigned char *buffer, unsigned short address, unsigned short length)
{
    return 0;
}

// usb_cam.dll: no camera
static int __cdecl cam_Init(int width, int height)
{
    wl_log("storm racer: the cabinet's camera (usb_cam.dll) answered not connected");
    return 0;
}

static int __cdecl cam_IsConnected(void)
{
    return 0;
}

static int __cdecl cam_Nothing(void)
{
    return 0;
}

static int __cdecl cam_QueryData(void *a, void *b, void *c)
{
    return -1;
}

// NetDbSync.dll: the game's records (track records, incomes, selections) kept on Wahlap's servers
// (netdbsync.xml: srg-s1.wahlap.hk:7001); trying them, the game stood still before its first frame.
// Offline here: each task fails at once (BeginTask other than 0, NetDbGetError 2), as the game
// already takes it ("ReadTrackRecord Error: 2"). All cdecl: the caller pops the arguments.
static int __cdecl db_Init(int flag)
{
    wl_log("storm racer: the network database (NetDbSync.dll) answered offline");
    return 0;
}

static int __cdecl db_Zero(void)
{
    return 0;
}

static int __cdecl db_Failed(void)
{
    return 2;
}

static const char *__cdecl db_Empty(void)
{
    return "";
}

static double __cdecl db_ZeroDouble(void)
{
    return 0;
}

static int __cdecl game_printf(const char *format, ...);
static int __cdecl game_fprintf(FILE *f, const char *format, ...);
static int __cdecl game_getch(void);

static const struct
{
    const char *dll, *name;
    void *fn;
} g_io[] = {
    {"NetDbSync.dll", "NetDbInit", (void *)db_Init},
    {"NetDbSync.dll", "NetDbEnd", (void *)db_Zero},
    {"NetDbSync.dll", "NetDbCurrentVer", (void *)db_Empty},
    {"NetDbSync.dll", "NetDbPatchVer", (void *)db_Empty},
    {"NetDbSync.dll", "NetDbSetPatch", (void *)db_Zero},
    {"NetDbSync.dll", "NetDbGetError", (void *)db_Failed},
    {"NetDbSync.dll", "BeginTask", (void *)db_Failed},
    {"NetDbSync.dll", "ExecuteTask", (void *)db_Failed},
    {"NetDbSync.dll", "?EndTask@@YAHH_N@Z", (void *)db_Zero},
    {"NetDbSync.dll", "ParamSetCount", (void *)db_Zero},
    {"NetDbSync.dll", "ParamSetInt", (void *)db_Zero},
    {"NetDbSync.dll", "?ParamSetUInt@@YAHHHI@Z", (void *)db_Zero},
    {"NetDbSync.dll", "ParamSetDouble", (void *)db_Zero},
    {"NetDbSync.dll", "ParamSetString", (void *)db_Zero},
    {"NetDbSync.dll", "ParamNext", (void *)db_Zero},
    {"NetDbSync.dll", "ResultNext", (void *)db_Zero},
    {"NetDbSync.dll", "ResultGetTotal", (void *)db_Zero},
    {"NetDbSync.dll", "?ResultGetInt@@YAHHPBD@Z", (void *)db_Zero},
    {"NetDbSync.dll", "?ResultGetString@@YAPBDHPBD@Z", (void *)db_Empty},
    {"NetDbSync.dll", "?ResultGetDouble@@YANHPBD@Z", (void *)db_ZeroDouble},
    {"usb_io.dll", "?UsbIO_InitSystem@@YAHH@Z", (void *)io_InitSystem},
    {"usb_io.dll", "?UsbIO_ExitSystem@@YAXXZ", (void *)io_ExitSystem},
    {"usb_io.dll", "?UsbIO_CheckConnection@@YA_NXZ", (void *)io_CheckConnection},
    {"usb_io.dll", "?UsbIO_GetInputdata@@YAHAAUUsbIOBoardInputData_s@@@Z", (void *)io_GetInputdata},
    {"usb_io.dll", "?UsbIO_ReadSysCoins@@YAHI@Z", (void *)io_ReadSysCoins},
    {"usb_io.dll", "?UsbIO_DecCoin@@YAHGG@Z", (void *)io_DecCoin},
    {"usb_io.dll", "?UsbIO_CleanCoin@@YAHG@Z", (void *)io_CleanCoin},
    {"usb_io.dll", "?UsbIO_ReadFailDecCoins@@YAHI@Z", (void *)io_ReadFailDecCoins},
    {"usb_io.dll", "?UsbIO_SetSteeringStatu@@YAHAAUUsbIOBoardSteeringStatu@@@Z", (void *)io_SetSteeringStatu},
    {"usb_io.dll", "?UsbIO_SetSteerCenter@@YAXH@Z", (void *)io_SetSteerCenter},
    {"usb_io.dll", "?UsbIO_SetMoterVolt@@YAXH@Z", (void *)io_SetMoterVolt},
    {"usb_io.dll", "?UsbIO_SetBoostThreshold@@YAXH@Z", (void *)io_SetBoostThreshold},
    {"usb_io.dll", "?UsbIO_SetDrifting@@YAXM@Z", (void *)io_SetDrifting},
    {"usb_io.dll", "?UsbIO_SetMenuGear@@YAX_N@Z", (void *)io_SetMenuGear},
    {"usb_io.dll", "?UsbIO_SetCollisionData@@YAXAAUUsbIOBoardCollisionData_s@@@Z", (void *)io_SetCollisionData},
    {"usb_io.dll", "?UsbIO_SetBoostEffect@@YAX_N@Z", (void *)io_SetBoostEffect},
    {"usb_io.dll", "?UsbIO_OutputCarStatuData@@YAXAAUUsbIOBoardCarStatuData_s@@@Z", (void *)io_OutputCarStatuData},
    {"usb_io.dll", "?UsbIO_SetOutputdata@@YAHAAUUsbIOBoardOutputData_s@@@Z", (void *)io_SetOutputdata},
    {"usb_io.dll", "?UsbIO_SetRoadBasicStatu@@YAXAAUUsbIOBoardSetRoadData_s@@@Z", (void *)io_SetRoadBasicStatu},
    {"usb_io.dll", "?UsbIO_SetSeatShakeImpluse@@YAXHHM_N@Z", (void *)io_SetSeatShakeImpluse},
    {"usb_io.dll", "?UsbIO_SetGamePlayStatu@@YAX_N@Z", (void *)io_SetGamePlayStatu},
    {"usb_io.dll", "?UsbIO_SetPowerPause@@YAXXZ", (void *)io_SetPowerPause},
    {"usb_io.dll", "?UsbIO_SetRoadType@@YAXH@Z", (void *)io_SetRoadType},
    {"usb_io.dll", "?UsbIO_SetLedLightStatu@@YAXAAUUsbIOBoardLEDLightStatu_s@@@Z", (void *)io_SetLedLightStatu},
    {"usb_io.dll", "?UsbIO_SetPWMLightStatu@@YAXAAUUsbIOBoardPWMLightStatu_s@@@Z", (void *)io_SetPWMLightStatu},
    {"usb_io.dll", "?UsbIO_SetLightState@@YAHAAUUsbIOBoardLightState_s@@@Z", (void *)io_SetLightState},
    {"usb_io.dll", "?UsbIO_GetVersionH@@YAHXZ", (void *)io_GetVersion},
    {"usb_io.dll", "?UsbIO_GetVersionL@@YAHXZ", (void *)io_GetVersion},
    {"usb_io.dll", "?UsbIO_GetIOBroadID@@YAHXZ", (void *)io_GetIOBroadID},
    {"usb_io.dll", "?UsbIO_UserControlMemoryRead@@YAHPAEGG@Z", (void *)io_UserControlMemory},
    {"usb_io.dll", "?UsbIO_UserControlMemoryWrite@@YAHPAEGG@Z", (void *)io_UserControlMemory},
    {"usb_cam.dll", "UsbIO_CamInit", (void *)cam_Init},
    {"usb_cam.dll", "UsbIO_CamIsConnected", (void *)cam_IsConnected},
    {"usb_cam.dll", "UsbIO_CamStart", (void *)cam_Nothing},
    {"usb_cam.dll", "UsbIO_CamStop", (void *)cam_Nothing},
    {"usb_cam.dll", "UsbIO_CamClean", (void *)cam_Nothing},
    {"usb_cam.dll", "UsbIO_CamGetCamImagePixelSizeInByte", (void *)cam_Nothing},
    {"usb_cam.dll", "UsbIO_QueryCamData", (void *)cam_QueryData},
    {"MSVCR90.dll", "printf", (void *)game_printf},
    {"MSVCR90.dll", "fprintf", (void *)game_fprintf},
    {"MSVCR90.dll", "_getch", (void *)game_getch},
};

// The game's console (racing.exe is a console program; windowsloader gives it NUL): what it prints
// in the log; its failed checks (+0x4c0000) print a message on stderr and ask "c: continue s: stack
// a: abort i: ignore" with _getch, the main thread waiting: answered c
// Some of its lines are given an argument that is not what the format says (a %s of 2, after a
// video): formatted under a guard, the format itself kept when that fails
static void format_line(char *line, size_t size, const char *format, va_list args)
{
    __try
    {
        _vsnprintf_s(line, size, _TRUNCATE, format, args);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        strncpy_s(line, size, format, _TRUNCATE);
    }
}

static void console_line(const char *format, va_list args)
{
    static int logged;
    char line[512];
    format_line(line, sizeof line, format, args);
    size_t len = strlen(line);
    while (len && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' '))
        line[--len] = 0;
    // Not the files and shaders it loads, a line each
    const char *text = line + strspn(line, "\n\r ");
    if (!strncmp(text, "FileLoad:", 9) || !strncmp(text, "Serialize file", 14) || strstr(text, " bytes = ") || !strncmp(text, "num ", 4))
        return;
    if (*text && logged < 300)
    {
        logged++;
        wl_log("storm racer: console: %s", text);
    }
}

static int __cdecl game_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    console_line(format, args);
    va_end(args);
    return 0;
}

static int __cdecl game_fprintf(FILE *f, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    console_line(format, args);
    va_end(args);
    return 0;
}

static int __cdecl game_getch(void)
{
    static int logged;
    if (logged++ < 20)
        wl_log("storm racer: a check failed in the game (its message above): continued (c)");
    return 'c';
}

// Its videos are WMV3 in AVI files, decoded through Video for Windows (AVIFIL32): without the
// Windows Media Video 9 decoder (wmv9vcm), AVIStreamGetFrameOpen fails, then the game reads a frame
// it did not get and stops (+0x112f2f). Without a decoder, its videos play black frames: a 32-bit
// picture of the stream's size, as the game asks it (AVIGETFRAMEF_BESTDISPLAYFMT).
struct BlackFrames
{
    BITMAPINFOHEADER header;
    // the pixels follow
};

static PVOID(WINAPI *oGetFrameOpen)(PVOID stream, LPBITMAPINFOHEADER wanted);
static LPVOID(WINAPI *oGetFrame)(PVOID frames, LONG position);
static HRESULT(WINAPI *oGetFrameClose)(PVOID frames);
static HRESULT(WINAPI *oReadFormat)(PVOID stream, LONG position, LPVOID format, LONG *size);

static PVOID WINAPI video_open(PVOID stream, LPBITMAPINFOHEADER wanted)
{
    PVOID frames = oGetFrameOpen(stream, wanted);
    if (frames || !oReadFormat)
        return frames;
    // The stream's format: a BITMAPINFOHEADER, the codec's data after it (its size asked first)
    LONG size = 0;
    if (FAILED(oReadFormat(stream, 0, NULL, &size)) || size < (LONG)sizeof(BITMAPINFOHEADER) || size > 65536)
        return NULL;
    BITMAPINFOHEADER *read = (BITMAPINFOHEADER *)calloc(1, size);
    if (!read)
        return NULL;
    BITMAPINFOHEADER format = {};
    if (SUCCEEDED(oReadFormat(stream, 0, read, &size)))
        format = *read;
    free(read);
    if (format.biWidth <= 0 || format.biWidth > 4096 || format.biHeight == 0 || abs(format.biHeight) > 4096)
        return NULL;
    LONG width = format.biWidth, height = abs(format.biHeight);
    BlackFrames *black = (BlackFrames *)calloc(1, sizeof(BlackFrames) + (size_t)width * height * 4);
    if (!black)
        return NULL;
    black->header.biSize = sizeof black->header;
    black->header.biWidth = width;
    black->header.biHeight = height;
    black->header.biPlanes = 1;
    black->header.biBitCount = 32;
    black->header.biSizeImage = (DWORD)(width * height * 4);
    static int told;
    if (!told++)
        wl_log("storm racer: no decoder for its videos (WMV3, wmv9vcm): black frames, %ldx%ld", width, height);
    return black;
}

static int black_frames(PVOID frames)
{
    // Ours are calloc'd: a BITMAPINFOHEADER at their start (Windows' are a COM object)
    return frames && ((BlackFrames *)frames)->header.biSize == sizeof(BITMAPINFOHEADER) && ((BlackFrames *)frames)->header.biBitCount == 32 &&
           ((BlackFrames *)frames)->header.biPlanes == 1;
}

static LPVOID WINAPI video_frame(PVOID frames, LONG position)
{
    return black_frames(frames) ? &((BlackFrames *)frames)->header : oGetFrame(frames, position);
}

static HRESULT WINAPI video_close(PVOID frames)
{
    if (!black_frames(frames))
        return oGetFrameClose(frames);
    free(frames);
    return S_OK;
}

// The race's checkpoint signs on the HUD (+0x1bb20, stdcall, the HUD first): this executable looks
// for "checkPoint", "finishPoint" and "checkPole" in the "hud" menu (+0x416a94), which this copy's
// ui/hud_en.urc and hud2_en.urc do not have (older than the executable); then the race's start
// read a null element (+0x1bf77). Without them, no signs
static void(__stdcall *oCheckpointHud)(unsigned char *hud, int a, int b, int c, int d, int e);
static void __stdcall checkpoint_hud(unsigned char *hud, int a, int b, int c, int d, int e)
{
    if (!*(void **)(hud + 0xD50) || !*(void **)(hud + 0xD54) || !*(void **)(hud + 0xD58))
    {
        static int told;
        if (!told++)
            wl_log("storm racer: the HUD has no checkpoint signs (checkPoint, finishPoint, checkPole): none shown");
        return;
    }
    oCheckpointHud(hud, a, b, c, d, e);
}

static int exists(const wchar_t *dir, const wchar_t *name)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s%s", dir, name);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

int wl_game_stormracer(const wchar_t *exePath, const char **name, const char **title)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    wchar_t dir[MAX_PATH];
    wcsncpy_s(dir, MAX_PATH, exePath, base - exePath);
    if (_wcsicmp(base, L"racing.exe") || !exists(dir, L"usb_io.dll") || !exists(dir, L"racing.cfg"))
        return 0;
    *name = "srg";
    *title = "Storm Racer G";
    wl_log("game: %s", *title);
    wl_game_set_boards("Controls on USB I/O board (through usb_io.dll)", "Motor board: the same board (position servo: strength and window)");
    wl_inputs_set_table(g_inputs, IN_COUNT);
    int n = 0;
    HMODULE exe = GetModuleHandleW(NULL);
    for (size_t i = 0; i < sizeof g_io / sizeof g_io[0]; i++)
    {
        void *was;
        n += wl_patch_iat(exe, g_io[i].dll, g_io[i].name, g_io[i].fn, &was) != 0;
    }
    HMODULE avi = GetModuleHandleW(L"AVIFIL32.dll");
    if (avi)
        oReadFormat = (HRESULT(WINAPI *)(PVOID, LONG, LPVOID, LONG *))GetProcAddress(avi, "AVIStreamReadFormat");
    wl_patch_iat(exe, "AVIFIL32.dll", "AVIStreamGetFrameOpen", (void *)video_open, (void **)&oGetFrameOpen);
    wl_patch_iat(exe, "AVIFIL32.dll", "AVIStreamGetFrame", (void *)video_frame, (void **)&oGetFrame);
    wl_patch_iat(exe, "AVIFIL32.dll", "AVIStreamGetFrameClose", (void *)video_close, (void **)&oGetFrameClose);
    static const unsigned char hudWas[8] = {0x83, 0xEC, 0x2C, 0x80, 0x7C, 0x24, 0x44, 0x00};
    if (!memcmp((unsigned char *)exe + 0x1BB20, hudWas, sizeof hudWas))
        wl_hook_code((unsigned char *)exe + 0x1BB20, (void *)checkpoint_hud, (void **)&oCheckpointHud, "the HUD's checkpoint signs (racing.exe+0x1bb20)");
    // Its scene's render target and depth buffer are asked multisampled, 4 samples at quality 2
    // (CreateRenderTarget +0xa7412, CreateDepthStencilSurface +0xa7448): refused here
    // (D3DERR_INVALIDCALL, "dx9SurfaceCreate: Error"), then a null pointer. Without multisampling,
    // as TeknoParrot's "Disable MSAA"
    static const unsigned char msaa[4] = {0x6A, 0x02, 0x6A, 0x04}, none[4] = {0x6A, 0x00, 0x6A, 0x00};
    unsigned char *code = (unsigned char *)exe; // relocatable: the offsets from its base
    wl_patch_code(code + 0xA7412, msaa, none, 4, "no multisampling (render target)");
    wl_patch_code(code + 0xA7448, msaa, none, 4, "no multisampling (depth buffer)");
    wl_log("storm racer: %d of %d functions of usb_io.dll, usb_cam.dll, NetDbSync.dll and the console given by windowsloader", n, (int)(sizeof g_io / sizeof g_io[0]));
    return 1;
}
