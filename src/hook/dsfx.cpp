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

// DirectSound's I3DL2 reverb where Windows no longer has it (dsfx.h). DirectSound makes a buffer's
// effects with CoCreateInstance (system.cpp's hook): for this one, an object of ours, a DMO
// (IMediaObject, IMediaObjectInPlace) whose sound is the Waves reverb's (everything passed on to
// one), with the I3DL2 settings interface (IDirectSoundFXI3DL2Reverb) the game gets from
// GetObjectInPath: its settings kept as given and put onto the Waves reverb's (the wet level, the
// decay time and its high frequency ratio; the rest has no counterpart there).

#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <mediaobj.h>

#include "dsfx.h"
#include "log.h"

static const GUID g_i3dl2 = {0xEF985E71, 0xD5C7, 0x42D4, {0xBA, 0x4D, 0x2D, 0x07, 0x3E, 0x2E, 0x96, 0xF4}};       // GUID_DSFX_STANDARD_I3DL2REVERB
static const GUID g_waves = {0x87FC0268, 0x9A55, 0x4360, {0x95, 0xAA, 0x00, 0x4A, 0x1D, 0x9D, 0xE2, 0x6C}};       // GUID_DSFX_WAVES_REVERB
static const GUID g_i3dl2Iid = {0x4B166A6A, 0x0D66, 0x43F3, {0x80, 0xE3, 0xEE, 0x62, 0x80, 0xDE, 0xE1, 0xA4}};    // IID_IDirectSoundFXI3DL2Reverb(8)
static const GUID g_wavesIid = {0x46858C3A, 0x0DC6, 0x45E3, {0xB7, 0x60, 0xD4, 0xEE, 0xF1, 0x6C, 0xB3, 0x25}};    // IID_IDirectSoundFXWavesReverb(8)

// dsound.h's presets, in DSFX_I3DL2_ENVIRONMENT_PRESET's order
static const DSFXI3DL2Reverb g_presets[] = {
    {I3DL2_ENVIRONMENT_PRESET_DEFAULT},     {I3DL2_ENVIRONMENT_PRESET_GENERIC},    {I3DL2_ENVIRONMENT_PRESET_PADDEDCELL},
    {I3DL2_ENVIRONMENT_PRESET_ROOM},        {I3DL2_ENVIRONMENT_PRESET_BATHROOM},   {I3DL2_ENVIRONMENT_PRESET_LIVINGROOM},
    {I3DL2_ENVIRONMENT_PRESET_STONEROOM},   {I3DL2_ENVIRONMENT_PRESET_AUDITORIUM}, {I3DL2_ENVIRONMENT_PRESET_CONCERTHALL},
    {I3DL2_ENVIRONMENT_PRESET_CAVE},        {I3DL2_ENVIRONMENT_PRESET_ARENA},      {I3DL2_ENVIRONMENT_PRESET_HANGAR},
    {I3DL2_ENVIRONMENT_PRESET_CARPETEDHALLWAY}, {I3DL2_ENVIRONMENT_PRESET_HALLWAY}, {I3DL2_ENVIRONMENT_PRESET_STONECORRIDOR},
    {I3DL2_ENVIRONMENT_PRESET_ALLEY},       {I3DL2_ENVIRONMENT_PRESET_FOREST},     {I3DL2_ENVIRONMENT_PRESET_CITY},
    {I3DL2_ENVIRONMENT_PRESET_MOUNTAINS},   {I3DL2_ENVIRONMENT_PRESET_QUARRY},     {I3DL2_ENVIRONMENT_PRESET_PLAIN},
    {I3DL2_ENVIRONMENT_PRESET_PARKINGLOT},  {I3DL2_ENVIRONMENT_PRESET_SEWERPIPE},  {I3DL2_ENVIRONMENT_PRESET_UNDERWATER},
    {I3DL2_ENVIRONMENT_PRESET_SMALLROOM},   {I3DL2_ENVIRONMENT_PRESET_MEDIUMROOM}, {I3DL2_ENVIRONMENT_PRESET_LARGEROOM},
    {I3DL2_ENVIRONMENT_PRESET_MEDIUMHALL},  {I3DL2_ENVIRONMENT_PRESET_LARGEHALL},  {I3DL2_ENVIRONMENT_PRESET_PLATE},
};

int wl_dsfx_is_i3dl2(REFCLSID cls)
{
    return IsEqualCLSID(cls, g_i3dl2);
}

static float clampf(float v, float low, float high)
{
    return v < low ? low : v > high ? high : v;
}

static long clampl(long v, long low, long high)
{
    return v < low ? low : v > high ? high : v;
}

class I3dl2Reverb : public IMediaObject, public IMediaObjectInPlace, public IDirectSoundFXI3DL2Reverb
{
    volatile LONG m_refs = 1;
    IMediaObject *m_inner;
    IMediaObjectInPlace *m_innerInPlace; // NULL when the Waves reverb has none
    IDirectSoundFXWavesReverb *m_waves;
    SRWLOCK m_lock = SRWLOCK_INIT;
    DSFXI3DL2Reverb m_params;
    DWORD m_preset = DSFX_I3DL2_ENVIRONMENT_PRESET_DEFAULT;
    LONG m_quality = DSFX_I3DL2REVERB_QUALITY_DEFAULT;

    ~I3dl2Reverb()
    {
        m_waves->Release();
        if (m_innerInPlace)
            m_innerInPlace->Release();
        m_inner->Release();
    }

    // The settings onto the Waves reverb: the reverb's level (the room's plus the late reverb's,
    // in hundredths of a dB) its mix, the decay time and its high frequency ratio
    void apply(void)
    {
        DSFXWavesReverb w;
        w.fInGain = 0;
        w.fReverbMix = clampf((m_params.lRoom + m_params.lReverb) / 100.0f, DSFX_WAVESREVERB_REVERBMIX_MIN, DSFX_WAVESREVERB_REVERBMIX_MAX);
        w.fReverbTime = clampf(m_params.flDecayTime * 1000.0f, DSFX_WAVESREVERB_REVERBTIME_MIN, DSFX_WAVESREVERB_REVERBTIME_MAX);
        w.fHighFreqRTRatio = clampf(m_params.flDecayHFRatio, DSFX_WAVESREVERB_HIGHFREQRTRATIO_MIN, DSFX_WAVESREVERB_HIGHFREQRTRATIO_MAX);
        m_waves->SetAllParameters(&w);
    }

  public:
    I3dl2Reverb(IMediaObject *inner, IDirectSoundFXWavesReverb *waves, const DSFXI3DL2Reverb *params)
        : m_inner(inner), m_innerInPlace(NULL), m_waves(waves), m_params(*params)
    {
        if (FAILED(inner->QueryInterface(__uuidof(IMediaObjectInPlace), (void **)&m_innerInPlace)))
            m_innerInPlace = NULL;
        apply();
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID iid, void **out) override
    {
        if (!out)
            return E_POINTER;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, __uuidof(IMediaObject)))
            *out = static_cast<IMediaObject *>(this);
        else if (IsEqualIID(iid, __uuidof(IMediaObjectInPlace)) && m_innerInPlace)
            *out = static_cast<IMediaObjectInPlace *>(this);
        else if (IsEqualIID(iid, g_i3dl2Iid))
            *out = static_cast<IDirectSoundFXI3DL2Reverb *>(this);
        else if (IsEqualIID(iid, g_wavesIid))
        {
            *out = NULL;
            return E_NOINTERFACE;
        }
        else
            return m_inner->QueryInterface(iid, out); // the DMO's other interfaces (its parameters' description...)
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef(void) override
    {
        return (ULONG)InterlockedIncrement(&m_refs);
    }
    STDMETHODIMP_(ULONG) Release(void) override
    {
        LONG n = InterlockedDecrement(&m_refs);
        if (!n)
            delete this;
        return (ULONG)n;
    }

    // IMediaObject: the Waves reverb's
    STDMETHODIMP GetStreamCount(DWORD *in, DWORD *out) override { return m_inner->GetStreamCount(in, out); }
    STDMETHODIMP GetInputStreamInfo(DWORD i, DWORD *flags) override { return m_inner->GetInputStreamInfo(i, flags); }
    STDMETHODIMP GetOutputStreamInfo(DWORD i, DWORD *flags) override { return m_inner->GetOutputStreamInfo(i, flags); }
    STDMETHODIMP GetInputType(DWORD i, DWORD n, DMO_MEDIA_TYPE *t) override { return m_inner->GetInputType(i, n, t); }
    STDMETHODIMP GetOutputType(DWORD i, DWORD n, DMO_MEDIA_TYPE *t) override { return m_inner->GetOutputType(i, n, t); }
    STDMETHODIMP SetInputType(DWORD i, const DMO_MEDIA_TYPE *t, DWORD flags) override
    {
        HRESULT hr = m_inner->SetInputType(i, t, flags);
        if (FAILED(hr) && t && !(flags & DMO_SET_TYPEF_CLEAR))
            wl_log("dsfx: the Waves reverb standing for the I3DL2 one refuses the sound's format -> 0x%08lx", hr);
        return hr;
    }
    STDMETHODIMP SetOutputType(DWORD i, const DMO_MEDIA_TYPE *t, DWORD flags) override { return m_inner->SetOutputType(i, t, flags); }
    STDMETHODIMP GetInputCurrentType(DWORD i, DMO_MEDIA_TYPE *t) override { return m_inner->GetInputCurrentType(i, t); }
    STDMETHODIMP GetOutputCurrentType(DWORD i, DMO_MEDIA_TYPE *t) override { return m_inner->GetOutputCurrentType(i, t); }
    STDMETHODIMP GetInputSizeInfo(DWORD i, DWORD *size, DWORD *lookahead, DWORD *alignment) override
    {
        return m_inner->GetInputSizeInfo(i, size, lookahead, alignment);
    }
    STDMETHODIMP GetOutputSizeInfo(DWORD i, DWORD *size, DWORD *alignment) override { return m_inner->GetOutputSizeInfo(i, size, alignment); }
    STDMETHODIMP GetInputMaxLatency(DWORD i, REFERENCE_TIME *t) override { return m_inner->GetInputMaxLatency(i, t); }
    STDMETHODIMP SetInputMaxLatency(DWORD i, REFERENCE_TIME t) override { return m_inner->SetInputMaxLatency(i, t); }
    STDMETHODIMP Flush(void) override { return m_inner->Flush(); }
    STDMETHODIMP Discontinuity(DWORD i) override { return m_inner->Discontinuity(i); }
    STDMETHODIMP AllocateStreamingResources(void) override { return m_inner->AllocateStreamingResources(); }
    STDMETHODIMP FreeStreamingResources(void) override { return m_inner->FreeStreamingResources(); }
    STDMETHODIMP GetInputStatus(DWORD i, DWORD *flags) override { return m_inner->GetInputStatus(i, flags); }
    STDMETHODIMP ProcessInput(DWORD i, IMediaBuffer *b, DWORD flags, REFERENCE_TIME start, REFERENCE_TIME length) override
    {
        return m_inner->ProcessInput(i, b, flags, start, length);
    }
    STDMETHODIMP ProcessOutput(DWORD flags, DWORD count, DMO_OUTPUT_DATA_BUFFER *buffers, DWORD *status) override
    {
        return m_inner->ProcessOutput(flags, count, buffers, status);
    }
    STDMETHODIMP Lock(LONG lock) override { return m_inner->Lock(lock); }

    // IMediaObjectInPlace
    STDMETHODIMP Process(ULONG size, BYTE *data, REFERENCE_TIME start, DWORD flags) override
    {
        return m_innerInPlace->Process(size, data, start, flags);
    }
    STDMETHODIMP Clone(IMediaObjectInPlace **out) override
    {
        if (!out)
            return E_POINTER;
        *out = NULL;
        IMediaObjectInPlace *copy = NULL;
        HRESULT hr = m_innerInPlace->Clone(&copy);
        if (FAILED(hr))
            return hr;
        IMediaObject *inner = NULL;
        IDirectSoundFXWavesReverb *waves = NULL;
        if (FAILED(copy->QueryInterface(__uuidof(IMediaObject), (void **)&inner)) || FAILED(copy->QueryInterface(g_wavesIid, (void **)&waves)))
        {
            if (inner)
                inner->Release();
            copy->Release();
            return E_FAIL;
        }
        copy->Release();
        AcquireSRWLockShared(&m_lock);
        DSFXI3DL2Reverb params = m_params;
        ReleaseSRWLockShared(&m_lock);
        I3dl2Reverb *clone = new I3dl2Reverb(inner, waves, &params);
        clone->m_preset = m_preset;
        clone->m_quality = m_quality;
        *out = static_cast<IMediaObjectInPlace *>(clone);
        return S_OK;
    }
    STDMETHODIMP GetLatency(REFERENCE_TIME *t) override { return m_innerInPlace->GetLatency(t); }

    // IDirectSoundFXI3DL2Reverb: the settings kept in their ranges
    STDMETHODIMP SetAllParameters(LPCDSFXI3DL2Reverb p) override
    {
        if (!p)
            return E_POINTER;
        DSFXI3DL2Reverb v = *p;
        v.lRoom = clampl(v.lRoom, DSFX_I3DL2REVERB_ROOM_MIN, DSFX_I3DL2REVERB_ROOM_MAX);
        v.lRoomHF = clampl(v.lRoomHF, DSFX_I3DL2REVERB_ROOMHF_MIN, DSFX_I3DL2REVERB_ROOMHF_MAX);
        v.flRoomRolloffFactor = clampf(v.flRoomRolloffFactor, DSFX_I3DL2REVERB_ROOMROLLOFFFACTOR_MIN, DSFX_I3DL2REVERB_ROOMROLLOFFFACTOR_MAX);
        v.flDecayTime = clampf(v.flDecayTime, DSFX_I3DL2REVERB_DECAYTIME_MIN, DSFX_I3DL2REVERB_DECAYTIME_MAX);
        v.flDecayHFRatio = clampf(v.flDecayHFRatio, DSFX_I3DL2REVERB_DECAYHFRATIO_MIN, DSFX_I3DL2REVERB_DECAYHFRATIO_MAX);
        v.lReflections = clampl(v.lReflections, DSFX_I3DL2REVERB_REFLECTIONS_MIN, DSFX_I3DL2REVERB_REFLECTIONS_MAX);
        v.flReflectionsDelay = clampf(v.flReflectionsDelay, DSFX_I3DL2REVERB_REFLECTIONSDELAY_MIN, DSFX_I3DL2REVERB_REFLECTIONSDELAY_MAX);
        v.lReverb = clampl(v.lReverb, DSFX_I3DL2REVERB_REVERB_MIN, DSFX_I3DL2REVERB_REVERB_MAX);
        v.flReverbDelay = clampf(v.flReverbDelay, DSFX_I3DL2REVERB_REVERBDELAY_MIN, DSFX_I3DL2REVERB_REVERBDELAY_MAX);
        v.flDiffusion = clampf(v.flDiffusion, DSFX_I3DL2REVERB_DIFFUSION_MIN, DSFX_I3DL2REVERB_DIFFUSION_MAX);
        v.flDensity = clampf(v.flDensity, DSFX_I3DL2REVERB_DENSITY_MIN, DSFX_I3DL2REVERB_DENSITY_MAX);
        v.flHFReference = clampf(v.flHFReference, DSFX_I3DL2REVERB_HFREFERENCE_MIN, DSFX_I3DL2REVERB_HFREFERENCE_MAX);
        AcquireSRWLockExclusive(&m_lock);
        m_params = v;
        apply();
        ReleaseSRWLockExclusive(&m_lock);
        return S_OK;
    }
    STDMETHODIMP GetAllParameters(LPDSFXI3DL2Reverb p) override
    {
        if (!p)
            return E_POINTER;
        AcquireSRWLockShared(&m_lock);
        *p = m_params;
        ReleaseSRWLockShared(&m_lock);
        return S_OK;
    }
    STDMETHODIMP SetPreset(DWORD preset) override
    {
        if (preset >= sizeof g_presets / sizeof g_presets[0])
            return E_INVALIDARG;
        HRESULT hr = SetAllParameters(&g_presets[preset]);
        if (SUCCEEDED(hr))
            m_preset = preset;
        return hr;
    }
    STDMETHODIMP GetPreset(LPDWORD preset) override
    {
        if (!preset)
            return E_POINTER;
        *preset = m_preset;
        return S_OK;
    }
    STDMETHODIMP SetQuality(LONG quality) override
    {
        if (quality < DSFX_I3DL2REVERB_QUALITY_MIN || quality > DSFX_I3DL2REVERB_QUALITY_MAX)
            return E_INVALIDARG;
        m_quality = quality;
        return S_OK;
    }
    STDMETHODIMP GetQuality(LONG *quality) override
    {
        if (!quality)
            return E_POINTER;
        *quality = m_quality;
        return S_OK;
    }
};

HRESULT wl_dsfx_i3dl2_create(REFIID iid, void **out, WlCoCreate create)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    IMediaObject *inner = NULL;
    HRESULT hr = create(g_waves, NULL, CLSCTX_INPROC_SERVER, __uuidof(IMediaObject), (void **)&inner);
    IDirectSoundFXWavesReverb *waves = NULL;
    if (SUCCEEDED(hr))
        hr = inner->QueryInterface(g_wavesIid, (void **)&waves);
    static volatile LONG logged;
    if (FAILED(hr))
    {
        if (inner)
            inner->Release();
        if (InterlockedIncrement(&logged) == 1)
            wl_log("dsfx: Windows has no I3DL2 reverb (DirectSound effect), and the Waves reverb cannot stand for it -> 0x%08lx", hr);
        return REGDB_E_CLASSNOTREG;
    }
    if (InterlockedIncrement(&logged) == 1)
        wl_log("dsfx: Windows has no I3DL2 reverb (DirectSound effect): the Waves reverb stands for it");
    I3dl2Reverb *r = new I3dl2Reverb(inner, waves, &g_presets[DSFX_I3DL2_ENVIRONMENT_PRESET_DEFAULT]);
    hr = static_cast<IMediaObject *>(r)->QueryInterface(iid, out);
    static_cast<IMediaObject *>(r)->Release();
    return hr;
}
