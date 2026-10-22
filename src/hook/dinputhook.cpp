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

// Observer for DirectInput 8: the devices the game enumerates and opens, how (exclusive access),
// and the force feedback effects it creates and plays. Arcade games built on PC code often drive
// the wheel motor this way rather than through the I/O board. The COM methods are hooked through
// the vtables, once per vtable (the ANSI and Unicode interfaces have their own).
//
// With an I/O board played as a DirectInput joystick (vdinput.h), the enumeration lists that
// board first and hides the PC's game controllers, and CreateDevice on it gives windowsloader's
// own device. Only the ANSI interface has it so far (the Unicode one just hides the controllers).

#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#include <string.h>

#include "hooks.h"
#include "log.h"
#include "vdinput.h"

enum
{
    SLOT_DI_CREATEDEVICE = 3,
    SLOT_DI_ENUMDEVICES = 4,
    SLOT_DI_GETDEVICESTATUS = 5,
    SLOT_DEV_ACQUIRE = 7,
    SLOT_DEV_SETCOOPERATIVELEVEL = 13,
    SLOT_DEV_CREATEEFFECT = 18,
    SLOT_DEV_SENDFFCOMMAND = 22,
    SLOT_EFF_SETPARAMETERS = 6,
    SLOT_EFF_START = 7,
    SLOT_EFF_STOP = 8,
};

static void **vtable_of(void *com)
{
    return *(void ***)com;
}

static const char *guid_text(REFGUID g, char *buf, size_t size)
{
    snprintf(buf, size, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2],
             g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return buf;
}

static const char *effect_name(REFGUID g)
{
    static const struct
    {
        const GUID *guid;
        const char *name;
    } names[] = {
        {&GUID_ConstantForce, "constant"}, {&GUID_RampForce, "ramp"},     {&GUID_Square, "square"},
        {&GUID_Sine, "sine"},              {&GUID_Triangle, "triangle"},  {&GUID_SawtoothUp, "sawtooth-up"},
        {&GUID_SawtoothDown, "sawtooth-down"}, {&GUID_Spring, "spring"}, {&GUID_Damper, "damper"},
        {&GUID_Inertia, "inertia"},        {&GUID_Friction, "friction"}, {&GUID_CustomForce, "custom"},
    };
    for (int i = 0; i < (int)(sizeof names / sizeof names[0]); i++)
        if (IsEqualGUID(g, *names[i].guid))
            return names[i].name;
    return "unknown";
}

// ------------------------------------------------------------------------------
// What the log calls things: device names by instance GUID, then by device, effects by effect
// ------------------------------------------------------------------------------

struct NamedGuid
{
    GUID guid;
    char name[80];
};
static NamedGuid g_instances[32];
static int g_instanceCount;

struct NamedDevice
{
    void *dev;
    char name[80];
};
static NamedDevice g_devices[16];

struct NamedEffect
{
    void *eff;
    const char *type;
    char device[80];
    unsigned params; // SetParameters calls so far
};
static NamedEffect g_effects[64];
static SRWLOCK g_lock = SRWLOCK_INIT;

static void remember_instance(REFGUID g, const char *name)
{
    AcquireSRWLockExclusive(&g_lock);
    int i = 0;
    while (i < g_instanceCount && !IsEqualGUID(g_instances[i].guid, g))
        i++;
    if (i == g_instanceCount && g_instanceCount < (int)(sizeof g_instances / sizeof g_instances[0]))
        g_instanceCount++;
    if (i < g_instanceCount)
    {
        g_instances[i].guid = g;
        strncpy(g_instances[i].name, name, sizeof g_instances[i].name - 1);
    }
    ReleaseSRWLockExclusive(&g_lock);
}

static void instance_name(REFGUID g, char *out, size_t size)
{
    char gs[48];
    snprintf(out, size, "%s", guid_text(g, gs, sizeof gs));
    AcquireSRWLockShared(&g_lock);
    for (int i = 0; i < g_instanceCount; i++)
        if (IsEqualGUID(g_instances[i].guid, g))
            snprintf(out, size, "%s", g_instances[i].name);
    ReleaseSRWLockShared(&g_lock);
}

static void remember_device(void *dev, const char *name)
{
    AcquireSRWLockExclusive(&g_lock);
    for (int i = 0; i < (int)(sizeof g_devices / sizeof g_devices[0]); i++)
        if (!g_devices[i].dev || g_devices[i].dev == dev)
        {
            g_devices[i].dev = dev;
            strncpy(g_devices[i].name, name, sizeof g_devices[i].name - 1);
            break;
        }
    ReleaseSRWLockExclusive(&g_lock);
}

static void device_name(void *dev, char *out, size_t size)
{
    snprintf(out, size, "%p", dev);
    AcquireSRWLockShared(&g_lock);
    for (int i = 0; i < (int)(sizeof g_devices / sizeof g_devices[0]); i++)
        if (g_devices[i].dev == dev)
            snprintf(out, size, "%s", g_devices[i].name);
    ReleaseSRWLockShared(&g_lock);
}

static void remember_effect(void *eff, const char *type, const char *device)
{
    AcquireSRWLockExclusive(&g_lock);
    for (int i = 0; i < (int)(sizeof g_effects / sizeof g_effects[0]); i++)
        if (!g_effects[i].eff || g_effects[i].eff == eff)
        {
            g_effects[i].eff = eff;
            g_effects[i].type = type;
            g_effects[i].params = 0;
            strncpy(g_effects[i].device, device, sizeof g_effects[i].device - 1);
            break;
        }
    ReleaseSRWLockExclusive(&g_lock);
}

// The effect's entry, its SetParameters counter incremented when count is set
static int find_effect(void *eff, NamedEffect *out, int count)
{
    int found = 0;
    AcquireSRWLockExclusive(&g_lock);
    for (int i = 0; i < (int)(sizeof g_effects / sizeof g_effects[0]) && !found; i++)
        if (g_effects[i].eff == eff)
        {
            if (count)
                g_effects[i].params++;
            *out = g_effects[i];
            found = 1;
        }
    ReleaseSRWLockExclusive(&g_lock);
    return found;
}

// "constant 2500", "spring offset 0 coef 8000/8000 sat 10000/10000", "sine mag 3000 period 50000"
static void describe_params(const char *type, const DIEFFECT *e, char *out, size_t size)
{
    out[0] = 0;
    if (!e || !e->lpvTypeSpecificParams)
    {
        snprintf(out, size, "%s", type);
        return;
    }
    if (!strcmp(type, "constant") && e->cbTypeSpecificParams >= sizeof(DICONSTANTFORCE))
        snprintf(out, size, "constant %ld", ((const DICONSTANTFORCE *)e->lpvTypeSpecificParams)->lMagnitude);
    else if (e->cbTypeSpecificParams >= sizeof(DICONDITION) &&
             (!strcmp(type, "spring") || !strcmp(type, "damper") || !strcmp(type, "friction") || !strcmp(type, "inertia")))
    {
        const DICONDITION *c = (const DICONDITION *)e->lpvTypeSpecificParams;
        snprintf(out, size, "%s offset %ld coef %ld/%ld sat %lu/%lu dead %ld", type, c->lOffset, c->lPositiveCoefficient, c->lNegativeCoefficient,
                 c->dwPositiveSaturation, c->dwNegativeSaturation, c->lDeadBand);
    }
    else if (e->cbTypeSpecificParams >= sizeof(DIPERIODIC))
    {
        const DIPERIODIC *p = (const DIPERIODIC *)e->lpvTypeSpecificParams;
        snprintf(out, size, "%s mag %lu offset %ld period %lu", type, p->dwMagnitude, p->lOffset, p->dwPeriod);
    }
    else
        snprintf(out, size, "%s (%lu bytes of parameters)", type, e->cbTypeSpecificParams);
    size_t n = strlen(out);
    snprintf(out + n, size - n, " gain %lu duration %lu", e->dwGain, e->dwDuration);
}

// ------------------------------------------------------------------------------
// IDirectInputEffect
// ------------------------------------------------------------------------------

static HRESULT(STDMETHODCALLTYPE *oSetParameters)(IDirectInputEffect *, LPCDIEFFECT, DWORD);
static HRESULT STDMETHODCALLTYPE hSetParameters(IDirectInputEffect *eff, LPCDIEFFECT e, DWORD flags)
{
    HRESULT hr = oSetParameters(eff, e, flags);
    NamedEffect ne;
    // Games update their forces every frame: the first ones, then one in 100
    if (find_effect(eff, &ne, 1) && (ne.params <= 50 || ne.params % 100 == 0))
    {
        char d[200];
        describe_params(ne.type, e, d, sizeof d);
        wl_log("ffb %s: SetParameters #%u %s flags=0x%lx -> 0x%08lx", ne.device, ne.params, d, flags, hr);
    }
    return hr;
}

static HRESULT(STDMETHODCALLTYPE *oStart)(IDirectInputEffect *, DWORD, DWORD);
static HRESULT STDMETHODCALLTYPE hStart(IDirectInputEffect *eff, DWORD iterations, DWORD flags)
{
    HRESULT hr = oStart(eff, iterations, flags);
    NamedEffect ne;
    if (find_effect(eff, &ne, 0))
        wl_log("ffb %s: Start %s x%lu flags=0x%lx -> 0x%08lx", ne.device, ne.type, iterations, flags, hr);
    return hr;
}

static HRESULT(STDMETHODCALLTYPE *oStop)(IDirectInputEffect *);
static HRESULT STDMETHODCALLTYPE hStop(IDirectInputEffect *eff)
{
    HRESULT hr = oStop(eff);
    NamedEffect ne;
    if (find_effect(eff, &ne, 0))
        wl_log("ffb %s: Stop %s -> 0x%08lx", ne.device, ne.type, hr);
    return hr;
}

static void hook_effect(IDirectInputEffect *eff)
{
    static int done;
    if (done || !eff)
        return;
    done = 1;
    void **vt = vtable_of(eff);
    wl_hook_code(vt[SLOT_EFF_SETPARAMETERS], (void *)hSetParameters, (void **)&oSetParameters, "IDirectInputEffect::SetParameters");
    wl_hook_code(vt[SLOT_EFF_START], (void *)hStart, (void **)&oStart, "IDirectInputEffect::Start");
    wl_hook_code(vt[SLOT_EFF_STOP], (void *)hStop, (void **)&oStop, "IDirectInputEffect::Stop");
}

// ------------------------------------------------------------------------------
// IDirectInputDevice8 (A and W share these signatures; each vtable has its own originals)
// ------------------------------------------------------------------------------

typedef HRESULT(STDMETHODCALLTYPE *SetCoopFn)(void *, HWND, DWORD);
typedef HRESULT(STDMETHODCALLTYPE *AcquireFn)(void *);
typedef HRESULT(STDMETHODCALLTYPE *CreateEffectFn)(void *, REFGUID, LPCDIEFFECT, LPDIRECTINPUTEFFECT *, LPUNKNOWN);
typedef HRESULT(STDMETHODCALLTYPE *SendFFCommandFn)(void *, DWORD);

static SetCoopFn oSetCoopA, oSetCoopW;
static AcquireFn oAcquireA, oAcquireW;
static CreateEffectFn oCreateEffectA, oCreateEffectW;
static SendFFCommandFn oSendFFA, oSendFFW;

static HRESULT set_coop(SetCoopFn orig, void *dev, HWND wnd, DWORD flags)
{
    HRESULT hr = orig(dev, wnd, flags);
    char n[80];
    device_name(dev, n, sizeof n);
    wl_log("dinput %s: SetCooperativeLevel %s%s%s%s -> 0x%08lx", n, flags & DISCL_EXCLUSIVE ? "exclusive" : "", flags & DISCL_NONEXCLUSIVE ? "non-exclusive" : "",
           flags & DISCL_FOREGROUND ? " foreground" : "", flags & DISCL_BACKGROUND ? " background" : "", hr);
    return hr;
}
static HRESULT STDMETHODCALLTYPE hSetCoopA(void *dev, HWND wnd, DWORD flags)
{
    return set_coop(oSetCoopA, dev, wnd, flags);
}
static HRESULT STDMETHODCALLTYPE hSetCoopW(void *dev, HWND wnd, DWORD flags)
{
    return set_coop(oSetCoopW, dev, wnd, flags);
}

static HRESULT acquire(AcquireFn orig, void *dev)
{
    static volatile LONG logged;
    HRESULT hr = orig(dev);
    if (InterlockedIncrement(&logged) <= 20)
    {
        char n[80];
        device_name(dev, n, sizeof n);
        wl_log("dinput %s: Acquire -> 0x%08lx", n, hr);
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE hAcquireA(void *dev)
{
    return acquire(oAcquireA, dev);
}
static HRESULT STDMETHODCALLTYPE hAcquireW(void *dev)
{
    return acquire(oAcquireW, dev);
}

static HRESULT create_effect(CreateEffectFn orig, void *dev, REFGUID g, LPCDIEFFECT e, LPDIRECTINPUTEFFECT *out, LPUNKNOWN outer)
{
    HRESULT hr = orig(dev, g, e, out, outer);
    char n[80], d[200];
    device_name(dev, n, sizeof n);
    const char *type = effect_name(g);
    describe_params(type, e, d, sizeof d);
    wl_log("ffb %s: CreateEffect %s -> 0x%08lx", n, d, hr);
    if (SUCCEEDED(hr) && out && *out)
    {
        remember_effect(*out, type, n);
        hook_effect(*out);
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE hCreateEffectA(void *dev, REFGUID g, LPCDIEFFECT e, LPDIRECTINPUTEFFECT *out, LPUNKNOWN outer)
{
    return create_effect(oCreateEffectA, dev, g, e, out, outer);
}
static HRESULT STDMETHODCALLTYPE hCreateEffectW(void *dev, REFGUID g, LPCDIEFFECT e, LPDIRECTINPUTEFFECT *out, LPUNKNOWN outer)
{
    return create_effect(oCreateEffectW, dev, g, e, out, outer);
}

static HRESULT send_ff(SendFFCommandFn orig, void *dev, DWORD cmd)
{
    HRESULT hr = orig(dev, cmd);
    char n[80];
    device_name(dev, n, sizeof n);
    wl_log("ffb %s: SendForceFeedbackCommand 0x%lx -> 0x%08lx", n, cmd, hr);
    return hr;
}
static HRESULT STDMETHODCALLTYPE hSendFFA(void *dev, DWORD cmd)
{
    return send_ff(oSendFFA, dev, cmd);
}
static HRESULT STDMETHODCALLTYPE hSendFFW(void *dev, DWORD cmd)
{
    return send_ff(oSendFFW, dev, cmd);
}

static void hook_device(void *dev, int unicode)
{
    static int doneA, doneW;
    int *done = unicode ? &doneW : &doneA;
    if (*done || !dev)
        return;
    *done = 1;
    void **vt = vtable_of(dev);
    if (unicode)
    {
        wl_hook_code(vt[SLOT_DEV_SETCOOPERATIVELEVEL], (void *)hSetCoopW, (void **)&oSetCoopW, "IDirectInputDevice8W::SetCooperativeLevel");
        wl_hook_code(vt[SLOT_DEV_ACQUIRE], (void *)hAcquireW, (void **)&oAcquireW, "IDirectInputDevice8W::Acquire");
        wl_hook_code(vt[SLOT_DEV_CREATEEFFECT], (void *)hCreateEffectW, (void **)&oCreateEffectW, "IDirectInputDevice8W::CreateEffect");
        wl_hook_code(vt[SLOT_DEV_SENDFFCOMMAND], (void *)hSendFFW, (void **)&oSendFFW, "IDirectInputDevice8W::SendForceFeedbackCommand");
    }
    else
    {
        wl_hook_code(vt[SLOT_DEV_SETCOOPERATIVELEVEL], (void *)hSetCoopA, (void **)&oSetCoopA, "IDirectInputDevice8A::SetCooperativeLevel");
        wl_hook_code(vt[SLOT_DEV_ACQUIRE], (void *)hAcquireA, (void **)&oAcquireA, "IDirectInputDevice8A::Acquire");
        wl_hook_code(vt[SLOT_DEV_CREATEEFFECT], (void *)hCreateEffectA, (void **)&oCreateEffectA, "IDirectInputDevice8A::CreateEffect");
        wl_hook_code(vt[SLOT_DEV_SENDFFCOMMAND], (void *)hSendFFA, (void **)&oSendFFA, "IDirectInputDevice8A::SendForceFeedbackCommand");
    }
}

// ------------------------------------------------------------------------------
// IDirectInput8: EnumDevices (the callback is wrapped to see each device) and CreateDevice
// ------------------------------------------------------------------------------

struct EnumWrapA
{
    LPDIENUMDEVICESCALLBACKA cb;
    LPVOID ref;
    int hide; // the PC's game controllers kept from the game
};
struct EnumWrapW
{
    LPDIENUMDEVICESCALLBACKW cb;
    LPVOID ref;
    int hide;
};

// Joysticks, gamepads, wheels, flight sticks, first-person controllers
static int is_game_controller(DWORD devType)
{
    BYTE t = GET_DIDEVICE_TYPE(devType);
    return t >= DI8DEVTYPE_JOYSTICK && t <= DI8DEVTYPE_1STPERSON;
}

// An enumeration that asks for game controllers (all devices, the class, or one of its types)
static int wants_game_controllers(DWORD type)
{
    return type == DI8DEVCLASS_ALL || type == DI8DEVCLASS_GAMECTRL || is_game_controller(type);
}

static BOOL CALLBACK enum_cb_a(LPCDIDEVICEINSTANCEA di, LPVOID ref)
{
    EnumWrapA *w = (EnumWrapA *)ref;
    char g[48];
    if (w->hide && is_game_controller(di->dwDevType))
    {
        wl_log("dinput: device \"%s\" hidden from the game", di->tszProductName);
        return DIENUM_CONTINUE;
    }
    BOOL r = w->cb(di, w->ref);
    remember_instance(di->guidInstance, di->tszProductName);
    wl_log("dinput: device \"%s\" type 0x%lx product %s%s -> %s", di->tszProductName, di->dwDevType, guid_text(di->guidProduct, g, sizeof g),
           di->guidFFDriver.Data1 ? " (force feedback)" : "", r == DIENUM_CONTINUE ? "next" : "chosen, enumeration stopped");
    return r;
}

static BOOL CALLBACK enum_cb_w(LPCDIDEVICEINSTANCEW di, LPVOID ref)
{
    EnumWrapW *w = (EnumWrapW *)ref;
    char name[160], g[48];
    wl_u8(di->tszProductName, name, sizeof name);
    if (w->hide && is_game_controller(di->dwDevType))
    {
        wl_log("dinput: device \"%s\" hidden from the game", name);
        return DIENUM_CONTINUE;
    }
    BOOL r = w->cb(di, w->ref);
    remember_instance(di->guidInstance, name);
    wl_log("dinput: device \"%s\" type 0x%lx product %s%s -> %s", name, di->dwDevType, guid_text(di->guidProduct, g, sizeof g),
           di->guidFFDriver.Data1 ? " (force feedback)" : "", r == DIENUM_CONTINUE ? "next" : "chosen, enumeration stopped");
    return r;
}

typedef HRESULT(STDMETHODCALLTYPE *EnumDevicesAFn)(IDirectInput8A *, DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID, DWORD);
typedef HRESULT(STDMETHODCALLTYPE *EnumDevicesWFn)(IDirectInput8W *, DWORD, LPDIENUMDEVICESCALLBACKW, LPVOID, DWORD);
typedef HRESULT(STDMETHODCALLTYPE *CreateDeviceFn)(void *, REFGUID, void **, LPUNKNOWN);
static EnumDevicesAFn oEnumDevicesA;
static EnumDevicesWFn oEnumDevicesW;
static CreateDeviceFn oCreateDeviceA, oCreateDeviceW;

static HRESULT STDMETHODCALLTYPE hEnumDevicesA(IDirectInput8A *di, DWORD type, LPDIENUMDEVICESCALLBACKA cb, LPVOID ref, DWORD flags)
{
    wl_log("dinput: EnumDevices type 0x%lx flags 0x%lx%s", type, flags, flags & DIEDFL_FORCEFEEDBACK ? " (force feedback only)" : "");
    int ours = wl_vdinput_active();
    if (ours && cb && wants_game_controllers(type) && !(flags & DIEDFL_FORCEFEEDBACK))
    {
        DIDEVICEINSTANCEA d;
        wl_vdinput_instance(&d);
        BOOL r = cb(&d, ref);
        char where[128];
        wl_log("dinput: device \"%s\" (windowsloader) -> %s [the game's callback %s]", d.tszProductName,
               r == DIENUM_CONTINUE ? "next" : "chosen, enumeration stopped", wl_where((const void *)cb, where, sizeof where));
        if (r != DIENUM_CONTINUE)
            return DI_OK;
    }
    // Only game controllers asked for: the PC has none for the game
    if (ours && type != DI8DEVCLASS_ALL && wants_game_controllers(type))
        return DI_OK;
    EnumWrapA w = {cb, ref, ours};
    return oEnumDevicesA(di, type, cb ? enum_cb_a : NULL, cb ? &w : ref, flags);
}

static HRESULT STDMETHODCALLTYPE hEnumDevicesW(IDirectInput8W *di, DWORD type, LPDIENUMDEVICESCALLBACKW cb, LPVOID ref, DWORD flags)
{
    wl_log("dinput: EnumDevices type 0x%lx flags 0x%lx%s", type, flags, flags & DIEDFL_FORCEFEEDBACK ? " (force feedback only)" : "");
    int ours = wl_vdinput_active();
    if (ours && type != DI8DEVCLASS_ALL && wants_game_controllers(type))
        return DI_OK;
    EnumWrapW w = {cb, ref, ours};
    return oEnumDevicesW(di, type, cb ? enum_cb_w : NULL, cb ? &w : ref, flags);
}

static HRESULT create_device(CreateDeviceFn orig, void *di, REFGUID g, void **out, LPUNKNOWN outer, int unicode)
{
    if (!unicode && wl_vdinput_is_ours(g))
    {
        if (!out)
            return DIERR_INVALIDPARAM;
        *out = wl_vdinput_create();
        wl_log("dinput: CreateDevice on the board played by windowsloader -> %p", *out);
        return *out ? DI_OK : DIERR_OUTOFMEMORY;
    }
    HRESULT hr = orig(di, g, out, outer);
    char n[80];
    if (IsEqualGUID(g, GUID_SysKeyboard))
        snprintf(n, sizeof n, "keyboard");
    else if (IsEqualGUID(g, GUID_SysMouse))
        snprintf(n, sizeof n, "mouse");
    else
        instance_name(g, n, sizeof n);
    wl_log("dinput: CreateDevice \"%s\" -> 0x%08lx", n, hr);
    if (SUCCEEDED(hr) && out && *out)
    {
        remember_device(*out, n);
        hook_device(*out, unicode);
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE hCreateDeviceA(void *di, REFGUID g, void **out, LPUNKNOWN outer)
{
    return create_device(oCreateDeviceA, di, g, out, outer, 0);
}
static HRESULT STDMETHODCALLTYPE hCreateDeviceW(void *di, REFGUID g, void **out, LPUNKNOWN outer)
{
    return create_device(oCreateDeviceW, di, g, out, outer, 1);
}

// GetDeviceStatus: the board played by windowsloader is attached (DirectInput itself does not know it)
typedef HRESULT(STDMETHODCALLTYPE *GetDeviceStatusFn)(void *, REFGUID);
static GetDeviceStatusFn oGetDeviceStatusA, oGetDeviceStatusW;
static HRESULT device_status(GetDeviceStatusFn orig, void *di, REFGUID g)
{
    static int logged;
    if (wl_vdinput_is_ours(g))
    {
        if (!logged++)
            wl_log("dinput: GetDeviceStatus on the board played by windowsloader -> attached");
        return DI_OK;
    }
    return orig(di, g);
}
static HRESULT STDMETHODCALLTYPE hGetDeviceStatusA(void *di, REFGUID g)
{
    return device_status(oGetDeviceStatusA, di, g);
}
static HRESULT STDMETHODCALLTYPE hGetDeviceStatusW(void *di, REFGUID g)
{
    return device_status(oGetDeviceStatusW, di, g);
}

static HRESULT(WINAPI *oDirectInput8Create)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
static HRESULT WINAPI hDirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID *out, LPUNKNOWN outer)
{
    static int doneA, doneW;
    int nested = wl_busy();
    wl_busy_enter();
    HRESULT hr = oDirectInput8Create(inst, version, riid, out, outer);
    wl_busy_leave();
    if (nested || wl_is_system_caller(WL_CALLER))
        return hr;
    int unicode = IsEqualIID(riid, IID_IDirectInput8W);
    char where[128];
    wl_log("DirectInput8Create(0x%lx, %s) -> 0x%08lx [%s]", version, unicode ? "W" : "A", hr, wl_where(WL_CALLER, where, sizeof where));
    if (FAILED(hr) || !out || !*out)
        return hr;
    void **vt = vtable_of(*out);
    if (unicode && !doneW)
    {
        doneW = 1;
        wl_hook_code(vt[SLOT_DI_ENUMDEVICES], (void *)hEnumDevicesW, (void **)&oEnumDevicesW, "IDirectInput8W::EnumDevices");
        wl_hook_code(vt[SLOT_DI_CREATEDEVICE], (void *)hCreateDeviceW, (void **)&oCreateDeviceW, "IDirectInput8W::CreateDevice");
        wl_hook_code(vt[SLOT_DI_GETDEVICESTATUS], (void *)hGetDeviceStatusW, (void **)&oGetDeviceStatusW, "IDirectInput8W::GetDeviceStatus");
    }
    else if (!unicode && !doneA)
    {
        doneA = 1;
        wl_hook_code(vt[SLOT_DI_ENUMDEVICES], (void *)hEnumDevicesA, (void **)&oEnumDevicesA, "IDirectInput8A::EnumDevices");
        wl_hook_code(vt[SLOT_DI_CREATEDEVICE], (void *)hCreateDeviceA, (void **)&oCreateDeviceA, "IDirectInput8A::CreateDevice");
        wl_hook_code(vt[SLOT_DI_GETDEVICESTATUS], (void *)hGetDeviceStatusA, (void **)&oGetDeviceStatusA, "IDirectInput8A::GetDeviceStatus");
    }
    return hr;
}

// XInput, the other way a game reads a pad: with a board played as a DirectInput joystick the
// game sees no pad there either (Daytona Championship USA reads its second player through it)
static DWORD(WINAPI *oXInputGetState)(DWORD, void *);
static DWORD WINAPI hXInputGetState(DWORD user, void *state)
{
    if (wl_vdinput_active() && !wl_busy() && !wl_is_system_caller(WL_CALLER))
    {
        static volatile LONG logged;
        if (!InterlockedExchange(&logged, 1))
            wl_log("XInputGetState: no pad for the game (its controls come through windowsloader)");
        return ERROR_DEVICE_NOT_CONNECTED;
    }
    return oXInputGetState(user, state);
}

static WlHook g_hooks[] = {
    WL_HOOK(L"dinput8.dll", DirectInput8Create),
    WL_HOOK(L"xinput9_1_0.dll", XInputGetState),
};

void wl_dinput_hooks(void)
{
    wl_hooks_add(g_hooks, sizeof g_hooks / sizeof g_hooks[0]);
}
