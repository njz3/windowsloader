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
// own with 6 absolute axes and up to 32 buttons, no POV, no force feedback. The game's data
// format is honoured as DirectInput does it: each object of the format takes the first free
// object of the device of its type (and GUID, and instance when it names one), GetDeviceState
// writes them at the format's offsets. Axes report in the range the game sets (DIPROP_RANGE),
// 0..65535 until then.

#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#include <string.h>

#include "hooks.h"
#include "log.h"
#include "vdinput.h"

static const WlVirtualJoystick *g_joy;

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

static DWORD device_type(void)
{
    return DI8DEVTYPE_DRIVING | DI8DEVTYPEDRIVING_DUALPEDALS << 8 | DIDEVTYPE_HID;
}

void wl_vdinput_instance(DIDEVICEINSTANCEA *out)
{
    memset(out, 0, sizeof *out);
    out->dwSize = sizeof *out;
    out->guidInstance = g_instance;
    out->guidProduct = product_guid();
    out->dwDevType = device_type();
    snprintf(out->tszInstanceName, sizeof out->tszInstanceName, "%s", g_joy->name);
    snprintf(out->tszProductName, sizeof out->tszProductName, "%s", g_joy->name);
    out->wUsagePage = 0x01; // generic desktop
    out->wUsage = 0x04;     // joystick
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

class VirtualDevice : public IDirectInputDevice8A
{
  public:
    VirtualDevice() : m_refs(1), m_formatCount(0), m_dataSize(0), m_acquired(0), m_logged(0)
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
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDirectInputDevice8A))
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
        caps->dwFlags = DIDC_ATTACHED | DIDC_EMULATED;
        caps->dwDevType = device_type();
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

    // No force feedback: the wheel motor of these cabinets is driven another way
    STDMETHODIMP CreateEffect(REFGUID g, LPCDIEFFECT e, LPDIRECTINPUTEFFECT *out, LPUNKNOWN outer)
    {
        if (out)
            *out = NULL;
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP EnumEffects(LPDIENUMEFFECTSCALLBACKA cb, LPVOID ref, DWORD type)
    {
        return DI_OK;
    }
    STDMETHODIMP GetEffectInfo(LPDIEFFECTINFOA info, REFGUID g)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP GetForceFeedbackState(LPDWORD state)
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP SendForceFeedbackCommand(DWORD cmd)
    {
        return DIERR_UNSUPPORTED;
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
        if (is_axis(i))
            snprintf(o->tszName, sizeof o->tszName, "%s", g_axisNames[i]);
        else
            snprintf(o->tszName, sizeof o->tszName, "Button %d", instance_of(i));
        o->wUsagePage = is_axis(i) ? 0x01 : 0x09;
        o->wUsage = is_axis(i) ? (WORD)(0x30 + i) : (WORD)(instance_of(i) + 1);
    }

    volatile LONG m_refs;
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
    return g_joy ? new VirtualDevice() : NULL;
}
