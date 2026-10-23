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

// The DirectInput joystick played by windowsloader (vdinput.h): an IDirectInputDevice8A of its
// own (an IDirectInputDevice8W over it at the end, and DirectInput 7's device) with 6 absolute axes and up to 32 buttons, no POV, force feedback when the module asks for
// it (its effects played as windowsloader's force, below). The game's data
// format is honoured as DirectInput does it: each object of the format takes the first free
// object of the device of its type (and GUID, and instance when it names one), GetDeviceState
// writes them at the format's offsets. Axes report in the range the game sets (DIPROP_RANGE),
// 0..65535 until then.

#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ffb.h"
#include "hooks.h"
#include "log.h"
#include "vdinput.h"

static const WlVirtualJoystick *g_joy;
static CRITICAL_SECTION g_effectsLock; // the force feedback effects (below)

// {57A4C0DE-D17A-4E10-9C3B-77696E6C6472}: the instance GUID, made up for windowsloader
static const GUID g_instance = {0x57A4C0DE, 0xD17A, 0x4E10, {0x9C, 0x3B, 0x77, 0x69, 0x6E, 0x6C, 0x64, 0x72}};

// ------------------------------------------------------------------------------
// The board also among raw input's HID devices (GetRawInputDeviceList, GetRawInputDeviceInfo; the
// game's own calls, its import table): DiRT Showdown keeps a DirectInput joystick only when a HID
// device with its VID/PID is there and that device's name has no "IG_" (not an XInput pad)
// ------------------------------------------------------------------------------

#define RAW_HANDLE ((HANDLE)(ULONG_PTR)0x57A4C0DE)

static UINT(WINAPI *oGetRawInputDeviceList)(PRAWINPUTDEVICELIST, PUINT, UINT);
static UINT WINAPI hGetRawInputDeviceList(PRAWINPUTDEVICELIST list, PUINT count, UINT size)
{
    if (!count || size != sizeof(RAWINPUTDEVICELIST))
        return oGetRawInputDeviceList(list, count, size);
    UINT real = 0;
    if (oGetRawInputDeviceList(NULL, &real, size) == (UINT)-1)
        return (UINT)-1;
    if (!list)
    {
        *count = real + 1;
        return 0;
    }
    if (*count < real + 1)
    {
        *count = real + 1;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return (UINT)-1;
    }
    UINT room = *count - 1;
    UINT got = oGetRawInputDeviceList(list, &room, size);
    if (got == (UINT)-1)
        return got;
    list[got].hDevice = RAW_HANDLE;
    list[got].dwType = RIM_TYPEHID;
    return got + 1;
}

static void raw_name(char *out, size_t size)
{
    snprintf(out, size, "\\\\?\\HID#VID_%04X&PID_%04X#7&57a4c0de&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}", g_joy->vid, g_joy->pid);
}

// RIDI_DEVICENAME in characters, RIDI_DEVICEINFO in bytes, as GetRawInputDeviceInfo counts them
static UINT raw_info(UINT command, void *data, PUINT size, int wide)
{
    if (command == RIDI_DEVICENAME)
    {
        char name[160];
        raw_name(name, sizeof name);
        UINT need = (UINT)strlen(name) + 1;
        if (!data || *size < need)
        {
            *size = need;
            if (!data)
                return 0;
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return (UINT)-1;
        }
        if (wide)
            MultiByteToWideChar(CP_ACP, 0, name, -1, (wchar_t *)data, (int)*size);
        else
            memcpy(data, name, need);
        return need;
    }
    if (command == RIDI_DEVICEINFO)
    {
        if (!data || *size < sizeof(RID_DEVICE_INFO))
        {
            *size = sizeof(RID_DEVICE_INFO);
            if (!data)
                return 0;
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return (UINT)-1;
        }
        RID_DEVICE_INFO *info = (RID_DEVICE_INFO *)data;
        memset(info, 0, sizeof *info);
        info->cbSize = sizeof *info;
        info->dwType = RIM_TYPEHID;
        info->hid.dwVendorId = g_joy->vid;
        info->hid.dwProductId = g_joy->pid;
        info->hid.dwVersionNumber = 0x0100;
        info->hid.usUsagePage = 0x01; // generic desktop
        info->hid.usUsage = 0x04;     // joystick
        return sizeof *info;
    }
    SetLastError(ERROR_INVALID_PARAMETER); // no preparsed data
    return (UINT)-1;
}

static UINT(WINAPI *oGetRawInputDeviceInfoA)(HANDLE, UINT, LPVOID, PUINT);
static UINT WINAPI hGetRawInputDeviceInfoA(HANDLE device, UINT command, LPVOID data, PUINT size)
{
    return device == RAW_HANDLE && size ? raw_info(command, data, size, 0) : oGetRawInputDeviceInfoA(device, command, data, size);
}

static UINT(WINAPI *oGetRawInputDeviceInfoW)(HANDLE, UINT, LPVOID, PUINT);
static UINT WINAPI hGetRawInputDeviceInfoW(HANDLE device, UINT command, LPVOID data, PUINT size)
{
    return device == RAW_HANDLE && size ? raw_info(command, data, size, 1) : oGetRawInputDeviceInfoW(device, command, data, size);
}

void wl_vdinput_register(const WlVirtualJoystick *joy)
{
    g_joy = joy;
    InitializeCriticalSection(&g_effectsLock);
    wl_log("dinput: \"%s\" played by windowsloader (%d buttons), the PC's game controllers hidden from the game", joy->name, joy->buttons);
    HMODULE game = GetModuleHandleW(NULL);
    int listed = wl_patch_iat(game, "user32.dll", "GetRawInputDeviceList", (void *)hGetRawInputDeviceList, (void **)&oGetRawInputDeviceList);
    if (wl_patch_iat(game, "user32.dll", "GetRawInputDeviceInfoA", (void *)hGetRawInputDeviceInfoA, (void **)&oGetRawInputDeviceInfoA) +
            wl_patch_iat(game, "user32.dll", "GetRawInputDeviceInfoW", (void *)hGetRawInputDeviceInfoW, (void **)&oGetRawInputDeviceInfoW) && listed)
        wl_log("dinput: \"%s\" also a raw input HID device (VID %04x PID %04x) for the game", joy->name, joy->vid, joy->pid);
}

int wl_vdinput_active(void)
{
    return g_joy != NULL;
}

int wl_vdinput_ffb(void)
{
    return g_joy && g_joy->ffb;
}

int wl_vdinput_is_ours(REFGUID instance)
{
    return g_joy && IsEqualGUID(instance, g_instance);
}

// The product GUID DirectInput gives a HID device: {PIDVID-0000-0000-0000-504944564944}
static GUID product_guid(void)
{
    GUID g = {(DWORD)g_joy->pid << 16 | g_joy->vid, 0, 0, {0, 0, 'P', 'I', 'D', 'V', 'I', 'D'}};
    return g;
}

// DirectInput 8's type, or DirectInput 7's (legacy: a joystick of the wheel kind; dinput.h keeps
// those constants for DIRECTINPUT_VERSION 0x700 and below)
#define LEGACY_JOYSTICK 4
#define LEGACY_JOYSTICK_WHEEL 6
static DWORD device_type(int legacy)
{
    if (legacy)
        return LEGACY_JOYSTICK | LEGACY_JOYSTICK_WHEEL << 8 | DIDEVTYPE_HID;
    return DI8DEVTYPE_DRIVING | DI8DEVTYPEDRIVING_DUALPEDALS << 8 | DIDEVTYPE_HID;
}

// {57A4C0DE-D17A-4E10-9C3B-77696E6C6666}: the force feedback driver's GUID, made up as well
static const GUID g_ffDriver = {0x57A4C0DE, 0xD17A, 0x4E10, {0x9C, 0x3B, 0x77, 0x69, 0x6E, 0x6C, 0x66, 0x66}};

void wl_vdinput_instance(DIDEVICEINSTANCEA *out)
{
    memset(out, 0, sizeof *out);
    out->dwSize = sizeof *out;
    out->guidInstance = g_instance;
    out->guidProduct = product_guid();
    if (g_joy->ffb)
        out->guidFFDriver = g_ffDriver;
    out->dwDevType = device_type(0);
    snprintf(out->tszInstanceName, sizeof out->tszInstanceName, "%s", g_joy->name);
    snprintf(out->tszProductName, sizeof out->tszProductName, "%s", g_joy->name);
    out->wUsagePage = 0x01; // generic desktop
    out->wUsage = 0x04;     // joystick
}

void wl_vdinput_instance_legacy(DIDEVICEINSTANCEA *out)
{
    wl_vdinput_instance(out);
    out->dwDevType = device_type(1);
}

// ------------------------------------------------------------------------------
// The device's objects: axes first, then buttons
// ------------------------------------------------------------------------------

static const GUID *const g_axisGuids[WL_VDINPUT_AXES] = {&GUID_XAxis, &GUID_YAxis, &GUID_ZAxis, &GUID_RxAxis, &GUID_RyAxis, &GUID_RzAxis};
static const char *const g_axisNames[WL_VDINPUT_AXES] = {"X Axis", "Y Axis", "Z Axis", "X Rotation", "Y Rotation", "Z Rotation"};

#define MAX_OBJECTS (WL_VDINPUT_AXES + 32)
#define MAX_FORMAT 256

static int object_count(void)
{
    return WL_VDINPUT_AXES + g_joy->buttons;
}

static int is_axis(int obj)
{
    return obj < WL_VDINPUT_AXES;
}

static int instance_of(int obj)
{
    return is_axis(obj) ? obj : obj - WL_VDINPUT_AXES;
}

static DWORD didft_of(int obj)
{
    return (is_axis(obj) ? DIDFT_ABSAXIS : DIDFT_PSHBUTTON) | DIDFT_MAKEINSTANCE(instance_of(obj));
}

static const GUID *guid_of(int obj)
{
    return is_axis(obj) ? g_axisGuids[obj] : &GUID_Button;
}

// ------------------------------------------------------------------------------
// Force feedback (WlVirtualJoystick.ffb): the effects the game creates are kept and played
// together as one force on the X axis. Springs pull towards their offset by the wheel's
// position, constant forces, ramps and periodic forces push; dampers, inertia and friction are
// left out (the wheel's speed is not known here). A force towards +X is towards the right.
// ------------------------------------------------------------------------------

#define MAX_EFFECTS 32

class VirtualEffect;
static VirtualEffect *g_effects[MAX_EFFECTS];
static LONG g_deviceGain = 10000;
static int g_actuators = 1; // DISFFC_SETACTUATORSOFF clears it

static int effect_kind(REFGUID g)
{
    if (IsEqualGUID(g, GUID_ConstantForce))
        return 1;
    if (IsEqualGUID(g, GUID_RampForce))
        return 2;
    if (IsEqualGUID(g, GUID_Square) || IsEqualGUID(g, GUID_Sine) || IsEqualGUID(g, GUID_Triangle) || IsEqualGUID(g, GUID_SawtoothUp) ||
        IsEqualGUID(g, GUID_SawtoothDown))
        return 3;
    if (IsEqualGUID(g, GUID_Spring))
        return 4;
    if (IsEqualGUID(g, GUID_Damper) || IsEqualGUID(g, GUID_Inertia) || IsEqualGUID(g, GUID_Friction))
        return 5;
    return 0;
}

static const char *effect_name(REFGUID g)
{
    static const char *names[] = {"unknown", "constant", "ramp", "periodic", "spring", "damper/inertia/friction"};
    return names[effect_kind(g)];
}

class VirtualEffect : public IDirectInputEffect
{
  public:
    VirtualEffect(REFGUID g) : m_refs(1), m_guid(g), m_gain(10000), m_duration(INFINITE), m_dir(1), m_playing(0), m_start(0), m_iterations(1)
    {
        memset(&m_constant, 0, sizeof m_constant);
        memset(&m_ramp, 0, sizeof m_ramp);
        memset(&m_periodic, 0, sizeof m_periodic);
        memset(&m_condition, 0, sizeof m_condition);
        EnterCriticalSection(&g_effectsLock);
        for (int i = 0; i < MAX_EFFECTS; i++)
            if (!g_effects[i])
            {
                g_effects[i] = this;
                break;
            }
        LeaveCriticalSection(&g_effectsLock);
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, LPVOID *out)
    {
        if (!out)
            return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDirectInputEffect))
        {
            *out = this;
            AddRef();
            return S_OK;
        }
        *out = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef()
    {
        return (ULONG)InterlockedIncrement(&m_refs);
    }
    STDMETHODIMP_(ULONG) Release()
    {
        LONG n = InterlockedDecrement(&m_refs);
        if (!n)
        {
            EnterCriticalSection(&g_effectsLock);
            for (int i = 0; i < MAX_EFFECTS; i++)
                if (g_effects[i] == this)
                    g_effects[i] = NULL;
            LeaveCriticalSection(&g_effectsLock);
            delete this;
        }
        return (ULONG)n;
    }

    // IDirectInputEffect
    STDMETHODIMP Initialize(HINSTANCE inst, DWORD version, REFGUID g)
    {
        return DI_OK;
    }
    STDMETHODIMP GetEffectGuid(LPGUID g)
    {
        if (!g)
            return E_POINTER;
        *g = m_guid;
        return DI_OK;
    }
    STDMETHODIMP GetParameters(LPDIEFFECT e, DWORD flags)
    {
        if (!e)
            return E_POINTER;
        if (flags & DIEP_GAIN)
            e->dwGain = m_gain;
        if (flags & DIEP_DURATION)
            e->dwDuration = m_duration;
        if ((flags & DIEP_TYPESPECIFICPARAMS) && e->lpvTypeSpecificParams)
        {
            const void *p = specific();
            DWORD size = specific_size();
            if (e->cbTypeSpecificParams < size)
                return DIERR_MOREDATA;
            memcpy(e->lpvTypeSpecificParams, p, size);
            e->cbTypeSpecificParams = size;
        }
        return DI_OK;
    }
    STDMETHODIMP SetParameters(LPCDIEFFECT e, DWORD flags)
    {
        if (!e)
            return E_POINTER;
        EnterCriticalSection(&g_effectsLock);
        if (flags & DIEP_GAIN)
            m_gain = e->dwGain;
        if (flags & DIEP_DURATION)
            m_duration = e->dwDuration;
        if ((flags & DIEP_DIRECTION) && e->rglDirection && e->cAxes)
        {
            // One axis: the sign along X (polar: 9000 is +X, 27000 is -X)
            LONG d = e->rglDirection[0];
            if (e->dwFlags & DIEFF_POLAR)
                m_dir = d > 0 && d < 18000 ? 1 : d > 18000 ? -1 : 0;
            else if (e->dwFlags & DIEFF_SPHERICAL)
                m_dir = d < 9000 || d > 27000 ? 1 : -1;
            else
                m_dir = d > 0 ? 1 : d < 0 ? -1 : 0;
        }
        if ((flags & DIEP_TYPESPECIFICPARAMS) && e->lpvTypeSpecificParams)
        {
            DWORD size = specific_size();
            memcpy(specific(), e->lpvTypeSpecificParams, e->cbTypeSpecificParams < size ? e->cbTypeSpecificParams : size);
        }
        LeaveCriticalSection(&g_effectsLock);
        static int logged;
        if (logged++ < 40)
            wl_log("dinput \"%s\": %s effect set (flags 0x%lx, gain %lu, direction %d)", g_joy->name, effect_name(m_guid), flags, m_gain, m_dir);
        if (flags & DIEP_START)
            Start(1, 0);
        return DI_OK;
    }
    STDMETHODIMP Start(DWORD iterations, DWORD flags)
    {
        EnterCriticalSection(&g_effectsLock);
        if (flags & DIES_SOLO)
            for (int i = 0; i < MAX_EFFECTS; i++)
                if (g_effects[i])
                    g_effects[i]->m_playing = 0;
        m_playing = 1;
        m_start = GetTickCount();
        m_iterations = iterations ? iterations : 1;
        LeaveCriticalSection(&g_effectsLock);
        return DI_OK;
    }
    STDMETHODIMP Stop()
    {
        m_playing = 0;
        return DI_OK;
    }
    STDMETHODIMP GetEffectStatus(LPDWORD status)
    {
        if (!status)
            return E_POINTER;
        *status = playing(GetTickCount()) ? DIEGES_PLAYING : 0;
        return DI_OK;
    }
    STDMETHODIMP Download()
    {
        return DI_OK;
    }
    STDMETHODIMP Unload()
    {
        m_playing = 0;
        return DI_OK;
    }
    STDMETHODIMP Escape(LPDIEFFESCAPE esc)
    {
        return DIERR_UNSUPPORTED;
    }

    // Its force now towards +X, -10000..10000, the wheel at x (-10000 left .. 10000 right)
    double force(LONG x, DWORD now)
    {
        if (!playing(now))
            return 0;
        double f = 0, t = (double)(now - m_start);
        switch (effect_kind(m_guid))
        {
        case 1:
            f = m_constant.lMagnitude * (double)m_dir;
            break;
        case 2:
        {
            double k = m_duration && m_duration != INFINITE ? t * 1000.0 / m_duration : 0;
            k -= (long)k;
            f = (m_ramp.lStart + (m_ramp.lEnd - m_ramp.lStart) * k) * m_dir;
            break;
        }
        case 3:
        {
            double period = m_periodic.dwPeriod ? m_periodic.dwPeriod / 1000.0 : 1, phase = t / period + m_periodic.dwPhase / 36000.0;
            phase -= (long)phase;
            double w;
            if (IsEqualGUID(m_guid, GUID_Square))
                w = phase < 0.5 ? 1 : -1;
            else if (IsEqualGUID(m_guid, GUID_Triangle))
                w = phase < 0.5 ? 4 * phase - 1 : 3 - 4 * phase;
            else if (IsEqualGUID(m_guid, GUID_SawtoothUp))
                w = 2 * phase - 1;
            else if (IsEqualGUID(m_guid, GUID_SawtoothDown))
                w = 1 - 2 * phase;
            else
                w = sin(phase * 6.283185307179586);
            f = ((double)m_periodic.dwMagnitude * w + m_periodic.lOffset) * m_dir;
            break;
        }
        case 4:
        {
            // A spring pulls back towards its offset, its push limited by its saturation
            LONG d = x - m_condition.lOffset;
            if (d <= (LONG)m_condition.lDeadBand && d >= -(LONG)m_condition.lDeadBand)
                return 0;
            LONG c = d > 0 ? m_condition.lPositiveCoefficient : m_condition.lNegativeCoefficient;
            DWORD sat = d > 0 ? m_condition.dwPositiveSaturation : m_condition.dwNegativeSaturation;
            f = -(double)c * d / 10000.0;
            if (sat && f > (double)sat)
                f = sat;
            if (sat && f < -(double)sat)
                f = -(double)sat;
            break;
        }
        default:
            return 0;
        }
        return f * m_gain / 10000.0;
    }

  private:
    int playing(DWORD now)
    {
        if (!m_playing)
            return 0;
        if (m_duration != INFINITE && m_iterations != INFINITE && (double)(now - m_start) * 1000.0 > (double)m_duration * m_iterations)
            m_playing = 0;
        return m_playing;
    }

    void *specific()
    {
        switch (effect_kind(m_guid))
        {
        case 1:
            return &m_constant;
        case 2:
            return &m_ramp;
        case 3:
            return &m_periodic;
        default:
            return &m_condition;
        }
    }
    DWORD specific_size()
    {
        switch (effect_kind(m_guid))
        {
        case 1:
            return sizeof m_constant;
        case 2:
            return sizeof m_ramp;
        case 3:
            return sizeof m_periodic;
        default:
            return sizeof m_condition;
        }
    }

    volatile LONG m_refs;
    GUID m_guid;
    DWORD m_gain, m_duration;
    int m_dir;
    volatile int m_playing;
    DWORD m_start, m_iterations;
    DICONSTANTFORCE m_constant;
    DIRAMPFORCE m_ramp;
    DIPERIODIC m_periodic;
    DICONDITION m_condition; // the first axis's
};

// The effects together, played on windowsloader's force (> 0 to the left), the wheel at x
static void ffb_update(unsigned short x)
{
    LONG pos = (LONG)x * 20000 / 65535 - 10000;
    DWORD now = GetTickCount();
    double total = 0;
    EnterCriticalSection(&g_effectsLock);
    for (int i = 0; i < MAX_EFFECTS; i++)
        if (g_effects[i])
            total += g_effects[i]->force(pos, now);
    LeaveCriticalSection(&g_effectsLock);
    total = g_actuators ? total * g_deviceGain / 10000.0 / 10000.0 : 0;
    wl_ffb_set(total > 1 ? -1 : total < -1 ? 1 : -total);
}

// DirectInput 7's devices (legacy) have the same methods at the same places as DirectInput 8's,
// up to WriteEffectToFile: the same device serves them, with their own device type
class VirtualDevice : public IDirectInputDevice8A
{
  public:
    VirtualDevice(int legacy) : m_refs(1), m_legacy(legacy), m_formatCount(0), m_dataSize(0), m_acquired(0), m_logged(0)
    {
        for (int i = 0; i < MAX_OBJECTS; i++)
        {
            m_offset[i] = -1;
            m_min[i] = 0;
            m_max[i] = 65535;
        }
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, LPVOID *out)
    {
        if (!out)
            return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || (!m_legacy && IsEqualIID(riid, IID_IDirectInputDevice8A)) ||
            (m_legacy && (IsEqualIID(riid, IID_IDirectInputDeviceA) || IsEqualIID(riid, IID_IDirectInputDevice2A) ||
                          IsEqualIID(riid, IID_IDirectInputDevice7A))))
        {
            *out = this;
            AddRef();
            return S_OK;
        }
        *out = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef()
    {
        return (ULONG)InterlockedIncrement(&m_refs);
    }
    STDMETHODIMP_(ULONG) Release()
    {
        LONG n = InterlockedDecrement(&m_refs);
        if (!n)
            delete this;
        return (ULONG)n;
    }

    // IDirectInputDevice8A
    STDMETHODIMP GetCapabilities(LPDIDEVCAPS caps)
    {
        if (!caps || caps->dwSize < sizeof(DIDEVCAPS_DX3))
            return DIERR_INVALIDPARAM;
        DWORD size = caps->dwSize;
        memset(caps, 0, size);
        caps->dwSize = size;
        caps->dwFlags = DIDC_ATTACHED | DIDC_EMULATED | (g_joy->ffb ? DIDC_FORCEFEEDBACK : 0);
        caps->dwDevType = device_type(m_legacy);
        caps->dwAxes = WL_VDINPUT_AXES;
        caps->dwButtons = g_joy->buttons;
        return DI_OK;
    }

    STDMETHODIMP EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA cb, LPVOID ref, DWORD flags)
    {
        if (!cb)
            return DIERR_INVALIDPARAM;
        for (int i = 0; i < object_count(); i++)
        {
            DWORD t = didft_of(i);
            if (flags != DIDFT_ALL && !(flags & DIDFT_GETTYPE(t)))
                continue;
            DIDEVICEOBJECTINSTANCEA o;
            object_info(i, &o);
            if (cb(&o, ref) == DIENUM_STOP)
                break;
        }
        return DI_OK;
    }

    STDMETHODIMP GetProperty(REFGUID prop, LPDIPROPHEADER h)
    {
        if (!h)
            return DIERR_INVALIDPARAM;
        if (&prop == &DIPROP_RANGE)
        {
            int obj = find_object(h);
            if (obj < 0 || !is_axis(obj))
                return DIERR_OBJECTNOTFOUND;
            ((LPDIPROPRANGE)h)->lMin = m_min[obj];
            ((LPDIPROPRANGE)h)->lMax = m_max[obj];
            return DI_OK;
        }
        if (&prop == &DIPROP_VIDPID)
        {
            ((LPDIPROPDWORD)h)->dwData = (DWORD)g_joy->pid << 16 | g_joy->vid;
            return DI_OK;
        }
        if (&prop == &DIPROP_PRODUCTNAME || &prop == &DIPROP_INSTANCENAME)
        {
            MultiByteToWideChar(CP_UTF8, 0, g_joy->name, -1, ((LPDIPROPSTRING)h)->wsz, MAX_PATH);
            return DI_OK;
        }
        if (&prop == &DIPROP_BUFFERSIZE || &prop == &DIPROP_DEADZONE || &prop == &DIPROP_SATURATION)
        {
            ((LPDIPROPDWORD)h)->dwData = &prop == &DIPROP_SATURATION ? 10000 : 0;
            return DI_OK;
        }
        if (&prop == &DIPROP_JOYSTICKID)
        {
            ((LPDIPROPDWORD)h)->dwData = 0;
            return DI_OK;
        }
        if (&prop == &DIPROP_FFGAIN && g_joy->ffb)
        {
            ((LPDIPROPDWORD)h)->dwData = (DWORD)g_deviceGain;
            return DI_OK;
        }
        return DIERR_UNSUPPORTED;
    }

    STDMETHODIMP SetProperty(REFGUID prop, LPCDIPROPHEADER h)
    {
        if (!h)
            return DIERR_INVALIDPARAM;
        if (&prop == &DIPROP_RANGE)
        {
            const DIPROPRANGE *r = (const DIPROPRANGE *)h;
            int set = 0;
            for (int i = 0; i < WL_VDINPUT_AXES; i++)
                if (h->dwHow == DIPH_DEVICE || find_object(h) == i)
                {
                    m_min[i] = r->lMin;
                    m_max[i] = r->lMax;
                    set++;
                }
            if (m_logged < 12)
            {
                m_logged++;
                wl_log("dinput \"%s\": range %ld..%ld for %s", g_joy->name, r->lMin, r->lMax,
                       h->dwHow == DIPH_DEVICE ? "every axis" : set ? g_axisNames[find_object(h)] : "no axis");
            }
            return set ? DI_OK : DIERR_OBJECTNOTFOUND;
        }
        if (&prop == &DIPROP_FFGAIN)
            g_deviceGain = (LONG)((LPCDIPROPDWORD)h)->dwData;
        // Buffer, dead zone, saturation, auto-centre: accepted, nothing to do
        return DI_OK;
    }

    STDMETHODIMP Acquire()
    {
        m_acquired = 1;
        return DI_OK;
    }
    STDMETHODIMP Unacquire()
    {
        m_acquired = 0;
        return DI_OK;
    }

    STDMETHODIMP GetDeviceState(DWORD size, LPVOID data)
    {
        if (!data || !m_dataSize || size != m_dataSize)
            return DIERR_INVALIDPARAM;
        unsigned short axes[WL_VDINPUT_AXES] = {32768, 32768, 32768, 32768, 32768, 32768};
        unsigned char buttons[32] = {0};
        g_joy->fill(axes, buttons);
        if (g_joy->ffb)
            ffb_update(axes[0]);
        unsigned char *p = (unsigned char *)data;
        memset(p, 0, size);
        for (int k = 0; k < m_formatCount; k++)
        {
            int obj = m_formatObj[k];
            DWORD ofs = m_formatOfs[k];
            if (obj < 0)
            {
                if (m_formatPov[k] && ofs + 4 <= size)
                    *(DWORD *)(p + ofs) = 0xFFFFFFFF; // POV centred
                continue;
            }
            if (is_axis(obj) && ofs + 4 <= size)
                *(LONG *)(p + ofs) = m_min[obj] + (LONG)(((LONGLONG)axes[obj] * (m_max[obj] - m_min[obj]) + 32767) / 65535);
            else if (!is_axis(obj) && ofs < size)
                p[ofs] = buttons[instance_of(obj)] ? 0x80 : 0;
        }
        return DI_OK;
    }

    STDMETHODIMP GetDeviceData(DWORD size, LPDIDEVICEOBJECTDATA data, LPDWORD count, DWORD flags)
    {
        // No buffered events: the game reads the state
        if (count)
            *count = 0;
        return DI_OK;
    }

    STDMETHODIMP SetDataFormat(LPCDIDATAFORMAT f)
    {
        if (!f || !f->rgodf || f->dwObjSize < sizeof(DIOBJECTDATAFORMAT))
            return DIERR_INVALIDPARAM;
        int used[MAX_OBJECTS] = {0};
        m_formatCount = 0;
        m_dataSize = f->dwDataSize;
        for (int i = 0; i < MAX_OBJECTS; i++)
            m_offset[i] = -1;
        int mapped = 0;
        for (DWORD k = 0; k < f->dwNumObjs && m_formatCount < MAX_FORMAT; k++)
        {
            const DIOBJECTDATAFORMAT *o = (const DIOBJECTDATAFORMAT *)((const BYTE *)f->rgodf + k * f->dwObjSize);
            int obj = -1;
            WORD inst = DIDFT_GETINSTANCE(o->dwType);
            for (int i = 0; i < object_count() && obj < 0; i++)
            {
                if (used[i] || !(DIDFT_GETTYPE(o->dwType) & DIDFT_GETTYPE(didft_of(i))))
                    continue;
                if (o->pguid && !IsEqualGUID(*o->pguid, *guid_of(i)))
                    continue;
                if (inst != 0xFFFF && inst != instance_of(i))
                    continue;
                obj = i;
            }
            m_formatObj[m_formatCount] = obj;
            m_formatOfs[m_formatCount] = o->dwOfs;
            m_formatPov[m_formatCount] = (DIDFT_GETTYPE(o->dwType) & DIDFT_POV) != 0;
            m_formatCount++;
            if (obj >= 0)
            {
                used[obj] = 1;
                m_offset[obj] = (int)o->dwOfs;
                mapped++;
            }
        }
        wl_log("dinput \"%s\": data format of %lu bytes, %lu objects, %d of them on the device", g_joy->name, f->dwDataSize, f->dwNumObjs, mapped);
        return DI_OK;
    }

    STDMETHODIMP SetEventNotification(HANDLE e)
    {
        return DI_OK;
    }
    STDMETHODIMP SetCooperativeLevel(HWND w, DWORD flags)
    {
        return DI_OK;
    }

    STDMETHODIMP GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA info, DWORD obj, DWORD how)
    {
        if (!info)
            return DIERR_INVALIDPARAM;
        DIPROPHEADER h = {sizeof h, sizeof h, obj, how};
        int i = find_object(&h);
        if (i < 0)
            return DIERR_OBJECTNOTFOUND;
        object_info(i, info);
        return DI_OK;
    }

    STDMETHODIMP GetDeviceInfo(LPDIDEVICEINSTANCEA info)
    {
        if (!info || info->dwSize < sizeof(DIDEVICEINSTANCE_DX3A))
            return DIERR_INVALIDPARAM;
        DIDEVICEINSTANCEA d;
        wl_vdinput_instance(&d);
        d.dwDevType = device_type(m_legacy);
        DWORD size = info->dwSize;
        memcpy(info, &d, size < sizeof d ? size : sizeof d);
        info->dwSize = size;
        return DI_OK;
    }

    STDMETHODIMP RunControlPanel(HWND w, DWORD flags)
    {
        return DI_OK;
    }
    STDMETHODIMP Initialize(HINSTANCE inst, DWORD version, REFGUID g)
    {
        return DI_OK;
    }

    // Force feedback only on a wheel that has it (WlVirtualJoystick.ffb): the others' motor is
    // driven another way
    STDMETHODIMP CreateEffect(REFGUID g, LPCDIEFFECT e, LPDIRECTINPUTEFFECT *out, LPUNKNOWN outer)
    {
        if (!out)
            return E_POINTER;
        *out = NULL;
        if (!g_joy->ffb || !effect_kind(g))
            return DIERR_UNSUPPORTED;
        VirtualEffect *v = new VirtualEffect(g);
        if (e)
            v->SetParameters(e, DIEP_ALLPARAMS);
        *out = v;
        wl_log("dinput \"%s\": %s effect created", g_joy->name, effect_name(g));
        return DI_OK;
    }
    STDMETHODIMP EnumEffects(LPDIENUMEFFECTSCALLBACKA cb, LPVOID ref, DWORD type)
    {
        if (!cb || !g_joy->ffb)
            return DI_OK;
        static const GUID *const guids[] = {&GUID_ConstantForce, &GUID_RampForce, &GUID_Square, &GUID_Sine, &GUID_Triangle, &GUID_SawtoothUp,
                                            &GUID_SawtoothDown, &GUID_Spring, &GUID_Damper, &GUID_Inertia, &GUID_Friction};
        for (int i = 0; i < (int)(sizeof guids / sizeof guids[0]); i++)
        {
            DIEFFECTINFOA info;
            effect_info(*guids[i], &info);
            if (type != DIEFT_ALL && DIEFT_GETTYPE(type) != DIEFT_GETTYPE(info.dwEffType))
                continue;
            if (cb(&info, ref) == DIENUM_STOP)
                break;
        }
        return DI_OK;
    }
    STDMETHODIMP GetEffectInfo(LPDIEFFECTINFOA info, REFGUID g)
    {
        if (!info)
            return E_POINTER;
        if (!g_joy->ffb || !effect_kind(g))
            return DIERR_DEVICENOTREG;
        effect_info(g, info);
        return DI_OK;
    }
    STDMETHODIMP GetForceFeedbackState(LPDWORD state)
    {
        if (!state)
            return E_POINTER;
        if (!g_joy->ffb)
            return DIERR_UNSUPPORTED;
        *state = DIGFFS_POWERON | (g_actuators ? DIGFFS_ACTUATORSON : DIGFFS_ACTUATORSOFF);
        return DI_OK;
    }
    STDMETHODIMP SendForceFeedbackCommand(DWORD cmd)
    {
        if (!g_joy->ffb)
            return DIERR_UNSUPPORTED;
        EnterCriticalSection(&g_effectsLock);
        if (cmd & (DISFFC_RESET | DISFFC_STOPALL))
            for (int i = 0; i < MAX_EFFECTS; i++)
                if (g_effects[i])
                    g_effects[i]->Stop();
        if (cmd & DISFFC_SETACTUATORSON)
            g_actuators = 1;
        if (cmd & DISFFC_SETACTUATORSOFF)
            g_actuators = 0;
        LeaveCriticalSection(&g_effectsLock);
        wl_log("dinput \"%s\": force feedback command 0x%lx", g_joy->name, cmd);
        return DI_OK;
    }
    STDMETHODIMP EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK cb, LPVOID ref, DWORD flags)
    {
        return DI_OK;
    }
    STDMETHODIMP Escape(LPDIEFFESCAPE esc)
    {
        return DIERR_UNSUPPORTED;
    }

    STDMETHODIMP Poll()
    {
        if (g_joy->ffb)
        {
            unsigned short axes[WL_VDINPUT_AXES] = {32768, 32768, 32768, 32768, 32768, 32768};
            unsigned char buttons[32] = {0};
            g_joy->fill(axes, buttons);
            ffb_update(axes[0]);
        }
        return DI_OK;
    }
    STDMETHODIMP SendDeviceData(DWORD size, LPCDIDEVICEOBJECTDATA data, LPDWORD count, DWORD flags)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP EnumEffectsInFile(LPCSTR file, LPDIENUMEFFECTSINFILECALLBACK cb, LPVOID ref, DWORD flags)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP WriteEffectToFile(LPCSTR file, DWORD count, LPDIFILEEFFECT effects, DWORD flags)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP BuildActionMap(LPDIACTIONFORMATA af, LPCSTR user, DWORD flags)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP SetActionMap(LPDIACTIONFORMATA af, LPCSTR user, DWORD flags)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA info)
    {
        return DIERR_UNSUPPORTED;
    }

  private:
    static void effect_info(REFGUID g, DIEFFECTINFOA *info)
    {
        memset(info, 0, sizeof *info);
        info->dwSize = sizeof *info;
        info->guid = g;
        int k = effect_kind(g);
        info->dwEffType = k == 1 ? DIEFT_CONSTANTFORCE : k == 2 ? DIEFT_RAMPFORCE : k == 3 ? DIEFT_PERIODIC : DIEFT_CONDITION;
        info->dwStaticParams = info->dwDynamicParams = DIEP_DURATION | DIEP_GAIN | DIEP_AXES | DIEP_DIRECTION | DIEP_TYPESPECIFICPARAMS;
        snprintf(info->tszName, sizeof info->tszName, "%s", effect_name(g));
    }

    // The object a property header names (DIPH_BYID: its type and instance, DIPH_BYOFFSET: its
    // offset in the data format), -1 when none
    int find_object(LPCDIPROPHEADER h)
    {
        for (int i = 0; i < object_count(); i++)
            if ((h->dwHow == DIPH_BYID && (h->dwObj & 0x00FFFFFF) == (didft_of(i) & 0x00FFFFFF)) ||
                (h->dwHow == DIPH_BYOFFSET && m_offset[i] >= 0 && h->dwObj == (DWORD)m_offset[i]))
                return i;
        return -1;
    }

    void object_info(int i, DIDEVICEOBJECTINSTANCEA *o)
    {
        memset(o, 0, sizeof *o);
        o->dwSize = sizeof *o;
        o->guidType = *guid_of(i);
        o->dwOfs = m_offset[i] >= 0 ? (DWORD)m_offset[i] : (DWORD)i;
        o->dwType = didft_of(i);
        o->dwFlags = is_axis(i) ? DIDOI_ASPECTPOSITION : 0;
        if (i == 0 && g_joy->ffb)
        {
            o->dwType |= DIDFT_FFACTUATOR;
            o->dwFlags |= DIDOI_FFACTUATOR;
        }
        if (is_axis(i))
            snprintf(o->tszName, sizeof o->tszName, "%s", g_axisNames[i]);
        else
            snprintf(o->tszName, sizeof o->tszName, "Button %d", instance_of(i));
        o->wUsagePage = is_axis(i) ? 0x01 : 0x09;
        o->wUsage = is_axis(i) ? (WORD)(0x30 + i) : (WORD)(instance_of(i) + 1);
    }

    volatile LONG m_refs;
    int m_legacy;
    int m_formatCount;
    int m_formatObj[MAX_FORMAT]; // the device object each object of the format reads, -1 for none
    DWORD m_formatOfs[MAX_FORMAT];
    unsigned char m_formatPov[MAX_FORMAT];
    DWORD m_dataSize;
    int m_offset[MAX_OBJECTS]; // each device object's offset in the format, -1 when not in it
    LONG m_min[MAX_OBJECTS], m_max[MAX_OBJECTS];
    int m_acquired;
    int m_logged;
};

IDirectInputDevice8A *wl_vdinput_create(void)
{
    return g_joy ? new VirtualDevice(0) : NULL;
}

IDirectInputDevice8A *wl_vdinput_create_legacy(void)
{
    return g_joy ? new VirtualDevice(1) : NULL;
}

// ------------------------------------------------------------------------------
// The Unicode interface (a game's IDirectInput8W, D1GP's): the ANSI device above, the structures
// that carry names converted
// ------------------------------------------------------------------------------

static void to_wide(const char *in, WCHAR *out, int size)
{
    MultiByteToWideChar(CP_UTF8, 0, in, -1, out, size);
}

void wl_vdinput_instance_w(DIDEVICEINSTANCEW *out)
{
    DIDEVICEINSTANCEA a;
    wl_vdinput_instance(&a);
    memset(out, 0, sizeof *out);
    out->dwSize = sizeof *out;
    out->guidInstance = a.guidInstance;
    out->guidProduct = a.guidProduct;
    out->dwDevType = a.dwDevType;
    to_wide(a.tszInstanceName, out->tszInstanceName, MAX_PATH);
    to_wide(a.tszProductName, out->tszProductName, MAX_PATH);
    out->guidFFDriver = a.guidFFDriver;
    out->wUsagePage = a.wUsagePage;
    out->wUsage = a.wUsage;
}

static void object_to_wide(const DIDEVICEOBJECTINSTANCEA *a, DIDEVICEOBJECTINSTANCEW *w)
{
    memset(w, 0, sizeof *w);
    w->dwSize = sizeof *w;
    w->guidType = a->guidType;
    w->dwOfs = a->dwOfs;
    w->dwType = a->dwType;
    w->dwFlags = a->dwFlags;
    to_wide(a->tszName, w->tszName, MAX_PATH);
    w->dwFFMaxForce = a->dwFFMaxForce;
    w->dwFFForceResolution = a->dwFFForceResolution;
    w->wCollectionNumber = a->wCollectionNumber;
    w->wDesignatorIndex = a->wDesignatorIndex;
    w->wUsagePage = a->wUsagePage;
    w->wUsage = a->wUsage;
    w->dwDimension = a->dwDimension;
    w->wExponent = a->wExponent;
    w->wReportId = a->wReportId;
}

static void effect_to_wide(const DIEFFECTINFOA *a, DIEFFECTINFOW *w)
{
    memset(w, 0, sizeof *w);
    w->dwSize = sizeof *w;
    w->guid = a->guid;
    w->dwEffType = a->dwEffType;
    w->dwStaticParams = a->dwStaticParams;
    w->dwDynamicParams = a->dwDynamicParams;
    to_wide(a->tszName, w->tszName, MAX_PATH);
}

// The game's callback and its argument, for the ANSI enumerations
struct WideObjects
{
    LPDIENUMDEVICEOBJECTSCALLBACKW cb;
    LPVOID ref;
};
static BOOL CALLBACK wide_object(LPCDIDEVICEOBJECTINSTANCEA a, LPVOID ref)
{
    WideObjects *w = (WideObjects *)ref;
    DIDEVICEOBJECTINSTANCEW o;
    object_to_wide(a, &o);
    return w->cb(&o, w->ref);
}

struct WideEffects
{
    LPDIENUMEFFECTSCALLBACKW cb;
    LPVOID ref;
};
static BOOL CALLBACK wide_effect(LPCDIEFFECTINFOA a, LPVOID ref)
{
    WideEffects *w = (WideEffects *)ref;
    DIEFFECTINFOW e;
    effect_to_wide(a, &e);
    return w->cb(&e, w->ref);
}

class VirtualDeviceW : public IDirectInputDevice8W
{
  public:
    VirtualDeviceW() : m_refs(1), m_a(new VirtualDevice(0))
    {
    }
    virtual ~VirtualDeviceW()
    {
        m_a->Release();
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, LPVOID *out)
    {
        if (!out)
            return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDirectInputDevice8W))
        {
            *out = this;
            AddRef();
            return S_OK;
        }
        return m_a->QueryInterface(riid, out);
    }
    STDMETHODIMP_(ULONG) AddRef()
    {
        return (ULONG)InterlockedIncrement(&m_refs);
    }
    STDMETHODIMP_(ULONG) Release()
    {
        LONG n = InterlockedDecrement(&m_refs);
        if (!n)
            delete this;
        return (ULONG)n;
    }

    // IDirectInputDevice8W: the ANSI device's, names in UTF-16
    STDMETHODIMP GetCapabilities(LPDIDEVCAPS caps)
    {
        return m_a->GetCapabilities(caps);
    }
    STDMETHODIMP EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKW cb, LPVOID ref, DWORD flags)
    {
        if (!cb)
            return DIERR_INVALIDPARAM;
        WideObjects w = {cb, ref};
        return m_a->EnumObjects(wide_object, &w, flags);
    }
    STDMETHODIMP GetProperty(REFGUID prop, LPDIPROPHEADER h)
    {
        return m_a->GetProperty(prop, h);
    }
    STDMETHODIMP SetProperty(REFGUID prop, LPCDIPROPHEADER h)
    {
        return m_a->SetProperty(prop, h);
    }
    STDMETHODIMP Acquire()
    {
        return m_a->Acquire();
    }
    STDMETHODIMP Unacquire()
    {
        return m_a->Unacquire();
    }
    STDMETHODIMP GetDeviceState(DWORD size, LPVOID data)
    {
        return m_a->GetDeviceState(size, data);
    }
    STDMETHODIMP GetDeviceData(DWORD size, LPDIDEVICEOBJECTDATA data, LPDWORD count, DWORD flags)
    {
        return m_a->GetDeviceData(size, data, count, flags);
    }
    STDMETHODIMP SetDataFormat(LPCDIDATAFORMAT f)
    {
        return m_a->SetDataFormat(f);
    }
    STDMETHODIMP SetEventNotification(HANDLE e)
    {
        return m_a->SetEventNotification(e);
    }
    STDMETHODIMP SetCooperativeLevel(HWND w, DWORD flags)
    {
        return m_a->SetCooperativeLevel(w, flags);
    }
    STDMETHODIMP GetObjectInfo(LPDIDEVICEOBJECTINSTANCEW info, DWORD obj, DWORD how)
    {
        if (!info || info->dwSize < sizeof(DIDEVICEOBJECTINSTANCE_DX3W))
            return DIERR_INVALIDPARAM;
        DIDEVICEOBJECTINSTANCEA a;
        a.dwSize = sizeof a;
        HRESULT hr = m_a->GetObjectInfo(&a, obj, how);
        if (FAILED(hr))
            return hr;
        DIDEVICEOBJECTINSTANCEW w;
        object_to_wide(&a, &w);
        DWORD size = info->dwSize;
        memcpy(info, &w, size < sizeof w ? size : sizeof w);
        info->dwSize = size;
        return DI_OK;
    }
    STDMETHODIMP GetDeviceInfo(LPDIDEVICEINSTANCEW info)
    {
        if (!info || info->dwSize < sizeof(DIDEVICEINSTANCE_DX3W))
            return DIERR_INVALIDPARAM;
        DIDEVICEINSTANCEW d;
        wl_vdinput_instance_w(&d);
        DWORD size = info->dwSize;
        memcpy(info, &d, size < sizeof d ? size : sizeof d);
        info->dwSize = size;
        return DI_OK;
    }
    STDMETHODIMP RunControlPanel(HWND w, DWORD flags)
    {
        return m_a->RunControlPanel(w, flags);
    }
    STDMETHODIMP Initialize(HINSTANCE inst, DWORD version, REFGUID g)
    {
        return m_a->Initialize(inst, version, g);
    }
    STDMETHODIMP CreateEffect(REFGUID g, LPCDIEFFECT e, LPDIRECTINPUTEFFECT *out, LPUNKNOWN outer)
    {
        return m_a->CreateEffect(g, e, out, outer);
    }
    STDMETHODIMP EnumEffects(LPDIENUMEFFECTSCALLBACKW cb, LPVOID ref, DWORD type)
    {
        if (!cb)
            return DI_OK;
        WideEffects w = {cb, ref};
        return m_a->EnumEffects(wide_effect, &w, type);
    }
    STDMETHODIMP GetEffectInfo(LPDIEFFECTINFOW info, REFGUID g)
    {
        if (!info)
            return E_POINTER;
        DIEFFECTINFOA a;
        HRESULT hr = m_a->GetEffectInfo(&a, g);
        if (SUCCEEDED(hr))
            effect_to_wide(&a, info);
        return hr;
    }
    STDMETHODIMP GetForceFeedbackState(LPDWORD state)
    {
        return m_a->GetForceFeedbackState(state);
    }
    STDMETHODIMP SendForceFeedbackCommand(DWORD cmd)
    {
        return m_a->SendForceFeedbackCommand(cmd);
    }
    STDMETHODIMP EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK cb, LPVOID ref, DWORD flags)
    {
        return m_a->EnumCreatedEffectObjects(cb, ref, flags);
    }
    STDMETHODIMP Escape(LPDIEFFESCAPE esc)
    {
        return m_a->Escape(esc);
    }
    STDMETHODIMP Poll()
    {
        return m_a->Poll();
    }
    STDMETHODIMP SendDeviceData(DWORD size, LPCDIDEVICEOBJECTDATA data, LPDWORD count, DWORD flags)
    {
        return m_a->SendDeviceData(size, data, count, flags);
    }
    STDMETHODIMP EnumEffectsInFile(LPCWSTR file, LPDIENUMEFFECTSINFILECALLBACK cb, LPVOID ref, DWORD flags)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP WriteEffectToFile(LPCWSTR file, DWORD count, LPDIFILEEFFECT effects, DWORD flags)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP BuildActionMap(LPDIACTIONFORMATW af, LPCWSTR user, DWORD flags)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP SetActionMap(LPDIACTIONFORMATW af, LPCWSTR user, DWORD flags)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP GetImageInfo(LPDIDEVICEIMAGEINFOHEADERW info)
    {
        return DIERR_UNSUPPORTED;
    }

  private:
    volatile LONG m_refs;
    VirtualDevice *m_a;
};

IDirectInputDevice8W *wl_vdinput_create_w(void)
{
    return g_joy ? new VirtualDeviceW() : NULL;
}
