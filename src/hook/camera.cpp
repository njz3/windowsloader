// windowsloader - runs Windows arcade games with emulated cabinet hardware
// Copyright (C) 2026 Benjamin Maurin - njz3
// License: GNU General Public License version 3 (GPL v3)
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE
// file for the full text. The author reserves the right to change the license of this code.

// The camera's choice (camera.h): the PC's video capture devices from DirectShow's enumerator,
// the still picture decoded and sized by WIC. COM is used from the game's threads (the menu, the
// game opening its camera), never from DllMain.

#include <windows.h>
#include <dshow.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camera.h"
#include "hooks.h"
#include "lang.h"
#include "log.h"

#pragma comment(lib, "windowscodecs.lib")

#define CAMERA_MAX 16

const char g_wlCameraLabel[] = WL_N("Caméra");
const char g_wlCameraHelp[] =
    WL_N("Les photos des joueurs (le link de Daytona ne tient pas sans caméra). Auto : la caméra du PC, sinon l'image fixe. Image fixe : une "
         "caméra émulée qui montre l'image ci-dessous. Ou une caméra du PC par son nom (l'image fixe si elle ne s'ouvre pas).");
const char g_wlCameraPictureLabel[] = WL_N("Image fixe (JPG ou PNG)");
const char g_wlCameraPictureHelp[] = WL_N("Le chemin de l'image de la caméra émulée ; vide : une image noire.");

// Auto, the still picture, then each camera of the PC by its name, made when the menu shows it
WlOptionChoice g_wlCameraChoices[2 + CAMERA_MAX + 1] = {
    {"AUTO", WL_N("Auto (la caméra du PC, sinon l'image fixe)")},
    {"PICTURE", WL_N("Image fixe")},
};
static char g_names[CAMERA_MAX][128];
static char g_value[WL_OPTION_VALUE_SIZE], g_picture[WL_OPTION_VALUE_SIZE];

int wl_camera_devices(char names[][128], int max)
{
    static const GUID systemDeviceEnum = {0x62BE5D10, 0x60EB, 0x11D0, {0xBD, 0x3B, 0x00, 0xA0, 0xC9, 0x11, 0xCE, 0x86}};
    static const GUID videoInputCategory = {0x860BB310, 0x5D01, 0x11D0, {0xBD, 0x3B, 0x00, 0xA0, 0xC9, 0x11, 0xCE, 0x86}};
    HRESULT init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    int n = 0;
    wl_busy_enter(); // windowsloader's own: not the game's, refused by system.cpp
    ICreateDevEnum *devices = NULL;
    if (SUCCEEDED(CoCreateInstance(systemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, __uuidof(ICreateDevEnum), (void **)&devices)))
    {
        IEnumMoniker *list = NULL;
        if (devices->CreateClassEnumerator(videoInputCategory, &list, 0) == S_OK)
        {
            IMoniker *device;
            while (n < max && list->Next(1, &device, NULL) == S_OK)
            {
                snprintf(names[n], 128, "Camera %d", n + 1);
                IPropertyBag *bag;
                if (SUCCEEDED(device->BindToStorage(NULL, NULL, __uuidof(IPropertyBag), (void **)&bag)))
                {
                    VARIANT v;
                    VariantInit(&v);
                    if (SUCCEEDED(bag->Read(L"FriendlyName", &v, NULL)) && v.vt == VT_BSTR && v.bstrVal)
                        WideCharToMultiByte(CP_UTF8, 0, v.bstrVal, -1, names[n], 128, NULL, NULL);
                    VariantClear(&v);
                    bag->Release();
                }
                device->Release();
                n++;
            }
            list->Release();
        }
        devices->Release();
    }
    wl_busy_leave();
    if (SUCCEEDED(init))
        CoUninitialize();
    return n;
}

const WlOptionChoice *wl_camera_choices(void)
{
    static int made;
    if (!made)
    {
        made = 1;
        int n = wl_camera_devices(g_names, CAMERA_MAX);
        for (int i = 0; i < n; i++)
            g_wlCameraChoices[2 + i].value = g_wlCameraChoices[2 + i].label = g_names[i];
    }
    return g_wlCameraChoices;
}

void wl_camera_settings(void)
{
    wl_game_option("CAMERA", g_value, sizeof g_value);
    wl_game_option("CAMERA_PICTURE", g_picture, sizeof g_picture);
    if (_stricmp(g_value, "PICTURE"))
        wl_system_allow_camera("[Game] CAMERA, a camera of the PC");
}

const char *wl_camera_name(void)
{
    return g_value;
}

int wl_camera_device(void)
{
    if (!_stricmp(g_value, "PICTURE"))
        return WL_CAMERA_EMULATED;
    if (!g_value[0] || !_stricmp(g_value, "AUTO"))
        return WL_CAMERA_ASKED;
    char names[CAMERA_MAX][128];
    int n = wl_camera_devices(names, CAMERA_MAX);
    for (int i = 0; i < n; i++)
        if (!_stricmp(names[i], g_value))
            return i;
    wl_log("camera: \"%s\" not among the PC's %d cameras: the emulated one", g_value, n);
    return WL_CAMERA_EMULATED;
}

const unsigned char *wl_camera_picture(int width, int height)
{
    static unsigned char *frame;
    static int frameW, frameH;
    if (frame && frameW == width && frameH == height)
        return frame;
    unsigned char *f = (unsigned char *)calloc((size_t)width * height, 3);
    if (!f)
        return NULL;
    if (!g_picture[0])
    {
        wl_log("camera: emulated, a black picture");
        frame = f, frameW = width, frameH = height;
        return frame;
    }
    wchar_t path[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, g_picture, -1, path, MAX_PATH);
    HRESULT init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *source = NULL;
    IWICBitmapScaler *scaler = NULL;
    IWICFormatConverter *bgr = NULL;
    wl_busy_enter();
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr))
        hr = factory->CreateDecoderFromFilename(path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(hr))
        hr = decoder->GetFrame(0, &source);
    if (SUCCEEDED(hr))
        hr = factory->CreateBitmapScaler(&scaler);
    if (SUCCEEDED(hr))
        hr = scaler->Initialize(source, width, height, WICBitmapInterpolationModeFant);
    if (SUCCEEDED(hr))
        hr = factory->CreateFormatConverter(&bgr);
    if (SUCCEEDED(hr))
        hr = bgr->Initialize(scaler, GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr))
        hr = bgr->CopyPixels(NULL, width * 3, (UINT)width * height * 3, f);
    wl_busy_leave();
    if (bgr)
        bgr->Release();
    if (scaler)
        scaler->Release();
    if (source)
        source->Release();
    if (decoder)
        decoder->Release();
    if (factory)
        factory->Release();
    if (SUCCEEDED(init))
        CoUninitialize();
    if (FAILED(hr))
    {
        memset(f, 0, (size_t)width * height * 3);
        wl_log("camera: emulated, \"%s\" not read (0x%08lx): a black picture", g_picture, hr);
    }
    else
        wl_log("camera: emulated, the picture \"%s\" at %dx%d", g_picture, width, height);
    frame = f, frameW = width, frameH = height;
    return frame;
}
