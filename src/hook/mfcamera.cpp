// windowsloader - runs Windows arcade games with emulated cabinet hardware
// Copyright (C) 2026 Benjamin Maurin - njz3
// License: GNU General Public License version 3 (GPL v3)
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE
// file for the full text. The author reserves the right to change the license of this code.

// A game's camera through Media Foundation (Mario Kart Arcade GP DX's player photos), as camera.h
// chooses it. The game lists the cameras (MFEnumDeviceSources), activates the first, makes a
// source reader on it (MFCreateSourceReaderFromMediaSource, asynchronous: its callback's
// OnReadSample gets each frame, then it asks for the next with ReadSample). Its import table's
// two functions are windowsloader's:
//   MFEnumDeviceSources  the PC's cameras (the one named first, tried once: it must give a frame),
//                        or one emulated camera
//   MFCreateSourceReaderFromMediaSource  on the emulated camera: a reader of windowsloader's,
//                        RGB32 640x480 top-down, 30 frames a second, each the still picture
// RGB32's bytes are B, G, R, X; a game that reads them as R, G, B gets them so (Mario Kart DX: its
// photo screen showed red as blue with Media Foundation's order).
// The emulated objects are plain COM objects; Media Foundation's own makes the media types, the
// samples and their buffers (mfplat.dll, which the game loads).

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdio.h>
#include <string.h>

#include "camera.h"
#include "hooks.h"
#include "log.h"

#pragma comment(lib, "mfuuid.lib")

#define FRAME_W 640
#define FRAME_H 480

static int g_rgbOrder; // the frame's bytes R, G, B, X

// mfplat.dll's functions, looked up (windowsloader does not load it for the other games)
static HRESULT(WINAPI *pMFCreateAttributes)(IMFAttributes **, UINT32);
static HRESULT(WINAPI *pMFCreateMediaType)(IMFMediaType **);
static HRESULT(WINAPI *pMFCreateMemoryBuffer)(DWORD, IMFMediaBuffer **);
static HRESULT(WINAPI *pMFCreateSample)(IMFSample **);
static HRESULT(WINAPI *pMFStartup)(ULONG, DWORD);
static HRESULT(WINAPI *pMFShutdown)(void);

static int mfplat(void)
{
    HMODULE m = GetModuleHandleW(L"mfplat.dll");
    if (!m)
        m = LoadLibraryW(L"mfplat.dll");
    if (!m)
        return 0;
    *(FARPROC *)&pMFCreateAttributes = GetProcAddress(m, "MFCreateAttributes");
    *(FARPROC *)&pMFCreateMediaType = GetProcAddress(m, "MFCreateMediaType");
    *(FARPROC *)&pMFCreateMemoryBuffer = GetProcAddress(m, "MFCreateMemoryBuffer");
    *(FARPROC *)&pMFCreateSample = GetProcAddress(m, "MFCreateSample");
    *(FARPROC *)&pMFStartup = GetProcAddress(m, "MFStartup");
    *(FARPROC *)&pMFShutdown = GetProcAddress(m, "MFShutdown");
    return pMFCreateAttributes && pMFCreateMediaType && pMFCreateMemoryBuffer && pMFCreateSample && pMFStartup && pMFShutdown;
}

// ------------------------------------------------------------------------------
// The emulated camera's source: only a token for the reader below
// ------------------------------------------------------------------------------

// Asked of a source, only the emulated one answers it
static const GUID IID_WlEmulatedSource = {0x6c1f3a52, 0x8d2e, 0x4b7a, {0x9e, 0x41, 0x57, 0x4c, 0x6f, 0x61, 0x64, 0x72}};

class EmulatedSource : public IMFMediaSource
{
    volatile LONG m_refs = 1;

public:
    STDMETHODIMP QueryInterface(REFIID iid, void **out) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFMediaEventGenerator) || iid == __uuidof(IMFMediaSource) || iid == IID_WlEmulatedSource)
        {
            *out = static_cast<IMFMediaSource *>(this);
            AddRef();
            return S_OK;
        }
        *out = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG r = InterlockedDecrement(&m_refs);
        if (!r)
            delete this;
        return r;
    }
    STDMETHODIMP GetEvent(DWORD, IMFMediaEvent **) override { return MF_E_NO_EVENTS_AVAILABLE; }
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback *, IUnknown *) override { return E_NOTIMPL; }
    STDMETHODIMP EndGetEvent(IMFAsyncResult *, IMFMediaEvent **) override { return E_NOTIMPL; }
    STDMETHODIMP QueueEvent(MediaEventType, REFGUID, HRESULT, const PROPVARIANT *) override { return S_OK; }
    STDMETHODIMP GetCharacteristics(DWORD *c) override
    {
        *c = MFMEDIASOURCE_IS_LIVE;
        return S_OK;
    }
    STDMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor **) override { return E_NOTIMPL; }
    STDMETHODIMP Start(IMFPresentationDescriptor *, const GUID *, const PROPVARIANT *) override { return S_OK; }
    STDMETHODIMP Stop() override { return S_OK; }
    STDMETHODIMP Pause() override { return S_OK; }
    STDMETHODIMP Shutdown() override { return S_OK; }
};

// ------------------------------------------------------------------------------
// Its activation object, as MFEnumDeviceSources lists a camera: attributes (its name, its link)
// kept by a store of Media Foundation's
// ------------------------------------------------------------------------------

class EmulatedActivate : public IMFActivate
{
    volatile LONG m_refs = 1;
    IMFAttributes *m_a;

public:
    explicit EmulatedActivate(IMFAttributes *a) : m_a(a) {}
    ~EmulatedActivate() { m_a->Release(); }
    STDMETHODIMP QueryInterface(REFIID iid, void **out) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFAttributes) || iid == __uuidof(IMFActivate))
        {
            *out = static_cast<IMFActivate *>(this);
            AddRef();
            return S_OK;
        }
        *out = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG r = InterlockedDecrement(&m_refs);
        if (!r)
            delete this;
        return r;
    }
    // IMFAttributes: the store's
    STDMETHODIMP GetItem(REFGUID k, PROPVARIANT *v) override { return m_a->GetItem(k, v); }
    STDMETHODIMP GetItemType(REFGUID k, MF_ATTRIBUTE_TYPE *t) override { return m_a->GetItemType(k, t); }
    STDMETHODIMP CompareItem(REFGUID k, REFPROPVARIANT v, BOOL *r) override { return m_a->CompareItem(k, v, r); }
    STDMETHODIMP Compare(IMFAttributes *o, MF_ATTRIBUTES_MATCH_TYPE t, BOOL *r) override { return m_a->Compare(o, t, r); }
    STDMETHODIMP GetUINT32(REFGUID k, UINT32 *v) override { return m_a->GetUINT32(k, v); }
    STDMETHODIMP GetUINT64(REFGUID k, UINT64 *v) override { return m_a->GetUINT64(k, v); }
    STDMETHODIMP GetDouble(REFGUID k, double *v) override { return m_a->GetDouble(k, v); }
    STDMETHODIMP GetGUID(REFGUID k, GUID *v) override { return m_a->GetGUID(k, v); }
    STDMETHODIMP GetStringLength(REFGUID k, UINT32 *n) override { return m_a->GetStringLength(k, n); }
    STDMETHODIMP GetString(REFGUID k, LPWSTR s, UINT32 n, UINT32 *l) override { return m_a->GetString(k, s, n, l); }
    STDMETHODIMP GetAllocatedString(REFGUID k, LPWSTR *s, UINT32 *l) override { return m_a->GetAllocatedString(k, s, l); }
    STDMETHODIMP GetBlobSize(REFGUID k, UINT32 *n) override { return m_a->GetBlobSize(k, n); }
    STDMETHODIMP GetBlob(REFGUID k, UINT8 *b, UINT32 n, UINT32 *l) override { return m_a->GetBlob(k, b, n, l); }
    STDMETHODIMP GetAllocatedBlob(REFGUID k, UINT8 **b, UINT32 *n) override { return m_a->GetAllocatedBlob(k, b, n); }
    STDMETHODIMP GetUnknown(REFGUID k, REFIID i, LPVOID *p) override { return m_a->GetUnknown(k, i, p); }
    STDMETHODIMP SetItem(REFGUID k, REFPROPVARIANT v) override { return m_a->SetItem(k, v); }
    STDMETHODIMP DeleteItem(REFGUID k) override { return m_a->DeleteItem(k); }
    STDMETHODIMP DeleteAllItems() override { return m_a->DeleteAllItems(); }
    STDMETHODIMP SetUINT32(REFGUID k, UINT32 v) override { return m_a->SetUINT32(k, v); }
    STDMETHODIMP SetUINT64(REFGUID k, UINT64 v) override { return m_a->SetUINT64(k, v); }
    STDMETHODIMP SetDouble(REFGUID k, double v) override { return m_a->SetDouble(k, v); }
    STDMETHODIMP SetGUID(REFGUID k, REFGUID v) override { return m_a->SetGUID(k, v); }
    STDMETHODIMP SetString(REFGUID k, LPCWSTR v) override { return m_a->SetString(k, v); }
    STDMETHODIMP SetBlob(REFGUID k, const UINT8 *b, UINT32 n) override { return m_a->SetBlob(k, b, n); }
    STDMETHODIMP SetUnknown(REFGUID k, IUnknown *u) override { return m_a->SetUnknown(k, u); }
    STDMETHODIMP LockStore() override { return m_a->LockStore(); }
    STDMETHODIMP UnlockStore() override { return m_a->UnlockStore(); }
    STDMETHODIMP GetCount(UINT32 *n) override { return m_a->GetCount(n); }
    STDMETHODIMP GetItemByIndex(UINT32 i, GUID *k, PROPVARIANT *v) override { return m_a->GetItemByIndex(i, k, v); }
    STDMETHODIMP CopyAllItems(IMFAttributes *d) override { return m_a->CopyAllItems(d); }
    // IMFActivate
    STDMETHODIMP ActivateObject(REFIID iid, void **out) override
    {
        EmulatedSource *s = new EmulatedSource;
        HRESULT hr = s->QueryInterface(iid, out);
        s->Release();
        return hr;
    }
    STDMETHODIMP ShutdownObject() override { return S_OK; }
    STDMETHODIMP DetachObject() override { return S_OK; }
};

// ------------------------------------------------------------------------------
// The source reader on the emulated camera: one stream, RGB32 640x480; asynchronous, as the game
// asks (each ReadSample answered by OnReadSample from its thread a frame later)
// ------------------------------------------------------------------------------

class EmulatedReader : public IMFSourceReader
{
    volatile LONG m_refs = 1, m_pending = 0, m_stop = 0;
    IMFSourceReaderCallback *m_callback;
    HANDLE m_wake, m_thread = NULL;
    BYTE *m_frame; // BGRA, top-down
    LONGLONG m_time = 0;

    static DWORD WINAPI thread(void *self)
    {
        ((EmulatedReader *)self)->run();
        delete (EmulatedReader *)self;
        return 0;
    }

    void run()
    {
        while (!m_stop)
        {
            WaitForSingleObject(m_wake, 100);
            while (!m_stop && m_pending > 0)
            {
                Sleep(33);
                InterlockedDecrement(&m_pending);
                IMFSample *sample = make_sample();
                if (m_stop)
                {
                    if (sample)
                        sample->Release();
                    break;
                }
                m_callback->OnReadSample(sample ? S_OK : E_OUTOFMEMORY, 0, 0, m_time, sample);
                m_time += 333333;
                static volatile LONG frames;
                LONG f = InterlockedIncrement(&frames);
                if (f == 1 || f == 100)
                    wl_log("camera: the emulated camera's frame %ld taken by the game", f);
                if (sample)
                    sample->Release();
            }
        }
    }

    IMFSample *make_sample()
    {
        IMFMediaBuffer *buffer = NULL;
        IMFSample *sample = NULL;
        BYTE *p;
        if (FAILED(pMFCreateMemoryBuffer(FRAME_W * FRAME_H * 4, &buffer)))
            return NULL;
        if (SUCCEEDED(buffer->Lock(&p, NULL, NULL)))
        {
            memcpy(p, m_frame, FRAME_W * FRAME_H * 4);
            buffer->Unlock();
            buffer->SetCurrentLength(FRAME_W * FRAME_H * 4);
        }
        if (SUCCEEDED(pMFCreateSample(&sample)))
        {
            sample->AddBuffer(buffer);
            sample->SetSampleTime(m_time);
            sample->SetSampleDuration(333333);
        }
        buffer->Release();
        return sample;
    }

    static HRESULT media_type(IMFMediaType **out)
    {
        IMFMediaType *t;
        HRESULT hr = pMFCreateMediaType(&t);
        if (FAILED(hr))
            return hr;
        t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        t->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        t->SetUINT64(MF_MT_FRAME_SIZE, (UINT64)FRAME_W << 32 | FRAME_H);
        t->SetUINT64(MF_MT_FRAME_RATE, (UINT64)30 << 32 | 1);
        t->SetUINT64(MF_MT_PIXEL_ASPECT_RATIO, (UINT64)1 << 32 | 1);
        t->SetUINT32(MF_MT_DEFAULT_STRIDE, FRAME_W * 4);
        t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        t->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        *out = t;
        return S_OK;
    }

    static int first_video(DWORD stream)
    {
        return stream == 0 || stream == (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM || stream == (DWORD)MF_SOURCE_READER_ANY_STREAM;
    }

public:
    EmulatedReader(IMFSourceReaderCallback *callback, const unsigned char *bgr) : m_callback(callback)
    {
        m_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
        m_frame = new BYTE[FRAME_W * FRAME_H * 4];
        for (int i = 0; i < FRAME_W * FRAME_H; i++)
        {
            m_frame[4 * i] = bgr ? bgr[3 * i + (g_rgbOrder ? 2 : 0)] : 0;
            m_frame[4 * i + 1] = bgr ? bgr[3 * i + 1] : 0;
            m_frame[4 * i + 2] = bgr ? bgr[3 * i + (g_rgbOrder ? 0 : 2)] : 0;
            m_frame[4 * i + 3] = 0xFF;
        }
    }
    ~EmulatedReader()
    {
        if (m_callback)
            m_callback->Release();
        CloseHandle(m_wake);
        delete[] m_frame;
    }

    STDMETHODIMP QueryInterface(REFIID iid, void **out) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFSourceReader))
        {
            *out = static_cast<IMFSourceReader *>(this);
            AddRef();
            return S_OK;
        }
        *out = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG r = InterlockedDecrement(&m_refs);
        if (!r)
        {
            // Its thread ends and deletes it (it may be the one calling: OnReadSample)
            InterlockedExchange(&m_stop, 1);
            if (m_thread)
            {
                SetEvent(m_wake);
                CloseHandle(m_thread);
            }
            else
                delete this;
        }
        return r;
    }
    STDMETHODIMP GetStreamSelection(DWORD stream, BOOL *on) override
    {
        *on = first_video(stream);
        return first_video(stream) || stream < 8 ? S_OK : MF_E_INVALIDSTREAMNUMBER;
    }
    STDMETHODIMP SetStreamSelection(DWORD, BOOL) override { return S_OK; }
    STDMETHODIMP GetNativeMediaType(DWORD stream, DWORD index, IMFMediaType **out) override
    {
        if (!first_video(stream))
            return MF_E_INVALIDSTREAMNUMBER;
        if (index)
            return MF_E_NO_MORE_TYPES;
        return media_type(out);
    }
    STDMETHODIMP GetCurrentMediaType(DWORD stream, IMFMediaType **out) override
    {
        return first_video(stream) ? media_type(out) : MF_E_INVALIDSTREAMNUMBER;
    }
    STDMETHODIMP SetCurrentMediaType(DWORD stream, DWORD *, IMFMediaType *) override
    {
        return first_video(stream) ? S_OK : MF_E_INVALIDSTREAMNUMBER;
    }
    STDMETHODIMP SetCurrentPosition(REFGUID, REFPROPVARIANT) override { return S_OK; }
    STDMETHODIMP ReadSample(DWORD stream, DWORD, DWORD *index, DWORD *flags, LONGLONG *time, IMFSample **sample) override
    {
        if (!first_video(stream))
            return MF_E_INVALIDSTREAMNUMBER;
        if (index || flags || time || sample || !m_callback)
            return E_INVALIDARG; // asynchronous: the callback gets it
        if (!m_thread)
        {
            m_thread = CreateThread(NULL, 0, thread, this, 0, NULL);
            if (!m_thread)
                return E_OUTOFMEMORY;
        }
        InterlockedIncrement(&m_pending);
        SetEvent(m_wake);
        return S_OK;
    }
    STDMETHODIMP Flush(DWORD stream) override
    {
        InterlockedExchange(&m_pending, 0);
        if (m_callback)
            m_callback->OnFlush(stream);
        return S_OK;
    }
    STDMETHODIMP GetServiceForStream(DWORD, REFGUID, REFIID, LPVOID *out) override
    {
        *out = NULL;
        return MF_E_UNSUPPORTED_SERVICE;
    }
    STDMETHODIMP GetPresentationAttribute(DWORD, REFGUID, PROPVARIANT *) override { return MF_E_ATTRIBUTENOTFOUND; }
};

// ------------------------------------------------------------------------------
// A camera of the PC: its reader as the game reads it. Mario Kart DX turns Media Foundation's
// converters off (MF_READWRITE_DISABLE_CONVERTERS) and takes the camera's first native format only
// (GetNativeMediaType 0), RGB32, RGB24, YUY2 or NV12: a webcam whose first is MJPG was "not found"
// (racecab2's DV20, E08-01). The converters are allowed again, the camera set to a format the game
// knows (one of its own, YUY2 at 640x480 first; else decoded to YUY2, NV12 or RGB32), and that
// format given as its first native one; the rest is the camera's reader
// ------------------------------------------------------------------------------

static const GUID *const g_known[] = {&MFVideoFormat_YUY2, &MFVideoFormat_NV12, &MFVideoFormat_RGB32, &MFVideoFormat_RGB24};

static void type_text(IMFMediaType *t, char *out, size_t size)
{
    GUID sub = {};
    UINT64 frame = 0;
    t->GetGUID(MF_MT_SUBTYPE, &sub);
    t->GetUINT64(MF_MT_FRAME_SIZE, &frame);
    char cc[5] = {(char)(sub.Data1 & 0xFF), (char)(sub.Data1 >> 8 & 0xFF), (char)(sub.Data1 >> 16 & 0xFF), (char)(sub.Data1 >> 24), 0};
    for (int i = 0; i < 4; i++)
        if (cc[i] < 32 || cc[i] > 126)
            cc[i] = '?';
    if (sub == MFVideoFormat_RGB32)
        strcpy_s(cc, "RGB32"); // a D3DFORMAT number, not a FourCC
    else if (sub == MFVideoFormat_RGB24)
        strcpy_s(cc, "RGB24");
    snprintf(out, size, "%s %ux%u", cc, (unsigned)(frame >> 32), (unsigned)(frame & 0xFFFFFFFF));
}

static int known(const GUID &sub)
{
    for (size_t i = 0; i < sizeof g_known / sizeof g_known[0]; i++)
        if (sub == *g_known[i])
            return (int)i;
    return -1;
}

// The camera's format for the game, set on its reader (NULL: none the game knows); its native
// formats to the log the first time
static void choose_format(IMFSourceReader *r, IMFMediaType **first)
{
    static int listed;
    IMFMediaType *f = NULL;
    const DWORD video = (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM;
    // The camera's formats, the best the game knows: its own kind first, then the nearest to 640x480
    IMFMediaType *best = NULL, *t;
    int bestKind = 99;
    UINT64 bestFrame = 0, firstFrame = 0;
    char text[48];
    for (DWORD i = 0; i < 64 && SUCCEEDED(r->GetNativeMediaType(video, i, &t)); i++)
    {
        GUID sub = {};
        UINT64 frame = 0;
        t->GetGUID(MF_MT_SUBTYPE, &sub);
        t->GetUINT64(MF_MT_FRAME_SIZE, &frame);
        if (!i)
            firstFrame = frame;
        if (i < 24 && !listed)
        {
            type_text(t, text, sizeof text);
            wl_log("camera: the PC's camera, native format %lu: %s", i, text);
        }
        int kind = known(sub);
        auto distance = [](UINT64 size) { return _abs64((LONGLONG)(size >> 32) - 640) + _abs64((LONGLONG)(size & 0xFFFFFFFF) - 480); };
        if (kind >= 0 && (kind < bestKind || (kind == bestKind && distance(frame) < distance(bestFrame))))
        {
            if (best)
                best->Release();
            best = t;
            bestKind = kind;
            bestFrame = frame;
            continue;
        }
        t->Release();
    }
    listed = 1;
    if (best && SUCCEEDED(r->SetCurrentMediaType(video, NULL, best)))
        f = best;
    else
    {
        if (best)
            best->Release();
        // None the game knows: decoded (the converters allowed), the camera's first size
        if (!firstFrame)
            firstFrame = (UINT64)640 << 32 | 480;
        for (size_t k = 0; k < 3 && !f; k++)
        {
            IMFMediaType *want;
            if (FAILED(pMFCreateMediaType(&want)))
                break;
            want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            want->SetGUID(MF_MT_SUBTYPE, *g_known[k]);
            want->SetUINT64(MF_MT_FRAME_SIZE, firstFrame);
            if (SUCCEEDED(r->SetCurrentMediaType(video, NULL, want)))
                r->GetCurrentMediaType(video, &f);
            want->Release();
        }
    }
    // The game reads the format's stride (MF_MT_DEFAULT_STRIDE) and gives up without it: a copy
    // with one when the camera's has none
    UINT32 stride;
    IMFMediaType *copy;
    if (f && FAILED(f->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) && SUCCEEDED(pMFCreateMediaType(&copy)))
    {
        GUID sub = {};
        UINT64 frame = 0;
        f->CopyAllItems(copy);
        f->GetGUID(MF_MT_SUBTYPE, &sub);
        f->GetUINT64(MF_MT_FRAME_SIZE, &frame);
        UINT32 w = (UINT32)(frame >> 32);
        copy->SetUINT32(MF_MT_DEFAULT_STRIDE, sub == MFVideoFormat_RGB32 ? w * 4 : sub == MFVideoFormat_RGB24 ? (w * 3 + 3) & ~3u
                                                                          : sub == MFVideoFormat_YUY2  ? w * 2
                                                                                                        : w);
        f->Release();
        f = copy;
    }
    if (f)
    {
        type_text(f, text, sizeof text);
        wl_log("camera: the PC's camera read as %s (given to the game as its first format)", text);
    }
    else
        wl_log("camera: no format of the PC's camera the game knows: the game's own choice");
    *first = f;
}

class PcReader : public IMFSourceReader
{
    volatile LONG m_refs = 1;
    IMFSourceReader *m_r;
    IMFMediaType *m_first = NULL; // the format given as the first native one

    static int first_video(DWORD stream)
    {
        return stream == 0 || stream == (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM;
    }

public:
    explicit PcReader(IMFSourceReader *r) : m_r(r)
    {
        choose_format(m_r, &m_first);
    }
    ~PcReader()
    {
        if (m_first)
            m_first->Release();
        m_r->Release();
    }
    STDMETHODIMP QueryInterface(REFIID iid, void **out) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFSourceReader))
        {
            *out = static_cast<IMFSourceReader *>(this);
            AddRef();
            return S_OK;
        }
        return m_r->QueryInterface(iid, out);
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG r = InterlockedDecrement(&m_refs);
        if (!r)
            delete this;
        return r;
    }
    STDMETHODIMP GetStreamSelection(DWORD s, BOOL *on) override { return m_r->GetStreamSelection(s, on); }
    STDMETHODIMP SetStreamSelection(DWORD s, BOOL on) override { return m_r->SetStreamSelection(s, on); }
    STDMETHODIMP GetNativeMediaType(DWORD s, DWORD index, IMFMediaType **out) override
    {
        if (!m_first || !first_video(s))
            return m_r->GetNativeMediaType(s, index, out);
        if (index)
            return MF_E_NO_MORE_TYPES;
        m_first->AddRef();
        *out = m_first;
        return S_OK;
    }
    STDMETHODIMP GetCurrentMediaType(DWORD s, IMFMediaType **out) override { return m_r->GetCurrentMediaType(s, out); }
    STDMETHODIMP SetCurrentMediaType(DWORD s, DWORD *reserved, IMFMediaType *t) override { return m_r->SetCurrentMediaType(s, reserved, t); }
    STDMETHODIMP SetCurrentPosition(REFGUID f, REFPROPVARIANT p) override { return m_r->SetCurrentPosition(f, p); }
    STDMETHODIMP ReadSample(DWORD s, DWORD f, DWORD *i, DWORD *fl, LONGLONG *t, IMFSample **sample) override
    {
        return m_r->ReadSample(s, f, i, fl, t, sample);
    }
    STDMETHODIMP Flush(DWORD s) override { return m_r->Flush(s); }
    STDMETHODIMP GetServiceForStream(DWORD s, REFGUID g, REFIID i, LPVOID *o) override { return m_r->GetServiceForStream(s, g, i, o); }
    STDMETHODIMP GetPresentationAttribute(DWORD s, REFGUID g, PROPVARIANT *v) override { return m_r->GetPresentationAttribute(s, g, v); }
};

// ------------------------------------------------------------------------------
// The game's two functions
// ------------------------------------------------------------------------------

static HRESULT(STDAPICALLTYPE *o_MFEnumDeviceSources)(IMFAttributes *, IMFActivate ***, UINT32 *);
static HRESULT(STDAPICALLTYPE *o_MFCreateSourceReaderFromMediaSource)(IMFMediaSource *, IMFAttributes *, IMFSourceReader **);

static void free_list(IMFActivate **list, UINT32 count)
{
    for (UINT32 i = 0; i < count; i++)
        if (list[i])
            list[i]->Release();
    CoTaskMemFree(list);
}

// The PC's camera tried once before the game has it, in the format chosen for the game: one that
// does not open or gives no frame in 5 s is left for the emulated one (without a camera Mario Kart
// DX stops on E08-01 at boot). On a camera of its own list (a second one of the game's), shut
// down after
struct Probe
{
    volatile LONG refs; // the waiting thread's and the probe's
    IMFActivate *camera;
    HRESULT hr;
};

static void probe_release(Probe *p)
{
    if (!InterlockedDecrement(&p->refs))
    {
        p->camera->Release();
        delete p;
    }
}

static DWORD WINAPI probe_thread(void *arg)
{
    Probe *p = (Probe *)arg;
    HRESULT init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    pMFStartup(MF_VERSION, MFSTARTUP_FULL);
    IMFMediaSource *source = NULL;
    IMFAttributes *a = NULL;
    IMFSourceReader *r = NULL;
    IMFMediaType *type = NULL;
    HRESULT hr = p->camera->ActivateObject(IID_PPV_ARGS(&source));
    if (FAILED(hr))
        wl_log("camera: the PC's camera did not open (0x%08lx)", hr);
    if (SUCCEEDED(hr))
        hr = pMFCreateAttributes(&a, 1);
    if (SUCCEEDED(hr))
    {
        a->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        hr = o_MFCreateSourceReaderFromMediaSource(source, a, &r);
        if (FAILED(hr))
            wl_log("camera: the PC's camera's reader: 0x%08lx", hr);
    }
    if (SUCCEEDED(hr))
    {
        choose_format(r, &type);
        if (!type)
            hr = MF_E_INVALIDMEDIATYPE;
    }
    for (int i = 0; SUCCEEDED(hr) && i < 300; i++)
    {
        DWORD stream, flags = 0;
        LONGLONG time;
        IMFSample *sample = NULL;
        hr = r->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &stream, &flags, &time, &sample);
        if (SUCCEEDED(hr) && (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM)))
            hr = E_FAIL;
        if (FAILED(hr))
            wl_log("camera: the PC's camera's frame not read (0x%08lx, flags 0x%lx)", hr, flags);
        if (FAILED(hr) || !sample)
            continue;
        // Its brightness: the average of its luma (YUY2: one byte in 2; NV12: its first plane), else of its bytes
        IMFMediaBuffer *buffer = NULL;
        BYTE *data;
        DWORD size = 0;
        double sum = 0;
        DWORD count = 0;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)) && SUCCEEDED(buffer->Lock(&data, NULL, &size)))
        {
            GUID sub = {};
            type->GetGUID(MF_MT_SUBTYPE, &sub);
            DWORD n = sub == MFVideoFormat_NV12 ? size * 2 / 3 : size, step = sub == MFVideoFormat_YUY2 ? 2 : 1;
            for (DWORD k = 0; k < n; k += step)
                sum += data[k], count++;
            buffer->Unlock();
        }
        if (buffer)
            buffer->Release();
        sample->Release();
        wl_log("camera: the PC's camera's first frame: %lu bytes, average luma %.1f", size, count ? sum / count : 0.0);
        break;
    }
    if (type)
        type->Release();
    if (r)
        r->Release();
    if (a)
        a->Release();
    if (source)
    {
        source->Shutdown();
        source->Release();
    }
    p->camera->ShutdownObject();
    pMFShutdown();
    if (SUCCEEDED(init))
        CoUninitialize();
    p->hr = hr;
    probe_release(p);
    return 0;
}

static int probe(IMFAttributes *attributes, UINT32 at)
{
    static int result; // 1 tried and good, -1 given up
    if (result)
        return result > 0;
    result = -1;
    IMFActivate **found = NULL;
    UINT32 n = 0;
    if (FAILED(o_MFEnumDeviceSources(attributes, &found, &n)) || at >= n)
    {
        if (found)
            free_list(found, n);
        wl_log("camera: the PC's camera not found again to try it: the emulated one");
        return 0;
    }
    Probe *p = new Probe;
    p->refs = 2;
    p->camera = found[at];
    p->camera->AddRef();
    p->hr = E_PENDING;
    free_list(found, n);
    HANDLE thread = CreateThread(NULL, 0, probe_thread, p, 0, NULL);
    if (!thread)
        probe_release(p);
    DWORD waited = thread ? WaitForSingleObject(thread, 5000) : WAIT_FAILED;
    if (thread)
        CloseHandle(thread);
    HRESULT hr = waited == WAIT_OBJECT_0 ? p->hr : E_PENDING;
    probe_release(p);
    if (SUCCEEDED(hr))
    {
        result = 1;
        wl_log("camera: the PC's camera tried: it gives frames, given to the game");
        return 1;
    }
    wl_log(hr == E_PENDING ? "camera: the PC's camera gave no frame in 5 s: the emulated one"
                           : "camera: the PC's camera tried, no frame (0x%08lx): the emulated one",
           hr);
    return 0;
}

static HRESULT STDAPICALLTYPE cam_MFEnumDeviceSources(IMFAttributes *attributes, IMFActivate ***list, UINT32 *count)
{
    if (!list || !count)
        return E_POINTER;
    *list = NULL;
    *count = 0;
    int device = wl_camera_device();
    if (device != WL_CAMERA_EMULATED && wl_system_camera_allowed() && mfplat())
    {
        IMFActivate **found = NULL;
        UINT32 n = 0;
        HRESULT hr = o_MFEnumDeviceSources(attributes, &found, &n);
        if (SUCCEEDED(hr) && n)
        {
            // A camera named: first in the list (the game takes the first)
            const char *name = wl_camera_name();
            int at = device == WL_CAMERA_ASKED ? 0 : -1;
            for (UINT32 i = 0; i < n && at < 0; i++)
            {
                WCHAR *w = NULL;
                UINT32 len;
                char u[256] = "";
                if (SUCCEEDED(found[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &w, &len)))
                {
                    WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, NULL, NULL);
                    CoTaskMemFree(w);
                }
                if (!_stricmp(u, name))
                    at = (int)i;
            }
            if (at >= 0 && !probe(attributes, (UINT32)at))
                at = -2; // tried, no frame
            if (at >= 0)
            {
                IMFActivate *t = found[0];
                found[0] = found[at];
                found[at] = t;
                *list = found;
                *count = n;
                // The game asks again each frame while it has no camera: the first times only
                static volatile LONG logged;
                if (InterlockedIncrement(&logged) <= 3)
                    wl_log("camera: the PC's %u camera(s) listed for the game, camera %d first", n, at);
                return hr;
            }
            if (at == -1)
                wl_log("camera: \"%s\" not among the PC's %u cameras: the emulated one", name, n);
        }
        else
            wl_log("camera: no camera of the PC (0x%08lx): the emulated one", hr);
        if (SUCCEEDED(hr) && found)
            free_list(found, n);
    }
    // The emulated camera, alone in the list
    IMFAttributes *a = NULL;
    if (!mfplat() || FAILED(pMFCreateAttributes(&a, 4)))
        return E_FAIL;
    a->SetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, L"windowsloader camera");
    a->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, L"\\\\?\\windowsloader#camera");
    a->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate **one = (IMFActivate **)CoTaskMemAlloc(sizeof(IMFActivate *));
    if (!one)
    {
        a->Release();
        return E_OUTOFMEMORY;
    }
    one[0] = new EmulatedActivate(a);
    *list = one;
    *count = 1;
    wl_log("camera: the emulated camera listed for the game");
    return S_OK;
}

static HRESULT STDAPICALLTYPE cam_MFCreateSourceReaderFromMediaSource(IMFMediaSource *source, IMFAttributes *attributes, IMFSourceReader **reader)
{
    IUnknown *mine = NULL;
    if (!source || FAILED(source->QueryInterface(IID_WlEmulatedSource, (void **)&mine)))
    {
        // A camera of the PC: the converters allowed (MJPG decoded), its reader as the game reads it
        if (attributes)
        {
            attributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);
            attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        }
        HRESULT hr = o_MFCreateSourceReaderFromMediaSource(source, attributes, reader);
        if (FAILED(hr) || !reader || !*reader || !mfplat())
        {
            wl_log("camera: the PC's camera's reader: 0x%08lx", hr);
            return hr;
        }
        *reader = new PcReader(*reader);
        return hr;
    }
    mine->Release();
    IMFSourceReaderCallback *callback = NULL;
    if (attributes)
        attributes->GetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, __uuidof(IMFSourceReaderCallback), (void **)&callback);
    if (!reader || !mfplat())
    {
        if (callback)
            callback->Release();
        return E_POINTER;
    }
    *reader = new EmulatedReader(callback, wl_camera_picture(FRAME_W, FRAME_H));
    wl_log("camera: the emulated camera read %s, RGB32 %dx%d", callback ? "asynchronously" : "without a callback (no frame)", FRAME_W, FRAME_H);
    return S_OK;
}

int wl_mfcamera_patch(HMODULE exe, int rgbOrder)
{
    g_rgbOrder = rgbOrder;
    int ok = wl_patch_iat(exe, "MF.dll", "MFEnumDeviceSources", (void *)cam_MFEnumDeviceSources, (void **)&o_MFEnumDeviceSources) &&
             wl_patch_iat(exe, "MFReadWrite.dll", "MFCreateSourceReaderFromMediaSource", (void *)cam_MFCreateSourceReaderFromMediaSource,
                          (void **)&o_MFCreateSourceReaderFromMediaSource);
    wl_log(ok ? "camera: Media Foundation's cameras chosen by windowsloader (camera.h)" : "camera: Media Foundation not imported by name: the game's own cameras");
    return ok;
}
