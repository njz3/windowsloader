// windowsloader - runs Windows arcade games with emulated cabinet hardware
// Copyright (C) 2026 Benjamin Maurin - njz3
// License: GNU General Public License version 3 (GPL v3)
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
// the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE
// file for the full text. The author reserves the right to change the license of this code.

// The camera of a game that photographs its player, the same for every such game: its options
// in [Game.<name>] and its "Options du jeu" page,
//   CAMERA          AUTO (the default): the PC's camera the game asks for, else the emulated one
//                   PICTURE: the emulated one
//                   a camera's name: that camera of the PC, else the emulated one
//   CAMERA_PICTURE  what the emulated camera shows: a JPG or PNG file, black without one
// The module puts WL_CAMERA_OPTIONS in its options, calls wl_camera_settings() once they are read,
// and emulates the camera through the game's own API (Daytona: OpenCV's cv::VideoCapture).
// [Emulation] CAMERA = false keeps the PC's cameras from the game: the emulated one then.

#pragma once

#include <windows.h>

#include "game.h"

const WlOptionChoice *wl_camera_choices(void);
extern WlOptionChoice g_wlCameraChoices[];
// The two options' texts, in camera.cpp (where the catalogues find them)
extern const char g_wlCameraLabel[], g_wlCameraHelp[], g_wlCameraPictureLabel[], g_wlCameraPictureHelp[];

#define WL_CAMERA_OPTIONS                                                                                                                     \
    {"CAMERA", g_wlCameraLabel, g_wlCameraHelp, WL_OPTION_CHOICE, g_wlCameraChoices, 0, 0, "AUTO", 1, NULL, wl_camera_choices},                \
    {                                                                                                                                         \
        "CAMERA_PICTURE", g_wlCameraPictureLabel, g_wlCameraPictureHelp, WL_OPTION_TEXT, NULL, 0, 0, "", 1, NULL                              \
    }

// The options read (from the module's settings callback); the PC's cameras allowed unless PICTURE
void wl_camera_settings(void);

// What the game gets: WL_CAMERA_EMULATED, WL_CAMERA_ASKED (AUTO: the camera the game asks for), or
// the rank of the camera named, in DirectShow's list of video capture devices
#define WL_CAMERA_EMULATED -2
#define WL_CAMERA_ASKED -1
int wl_camera_device(void);
// The CAMERA value (a camera's name)
const char *wl_camera_name(void);

// The PC's video capture devices by their names (UTF-8), DirectShow's order
int wl_camera_devices(char names[][128], int max);

// The emulated camera's picture, BGR 24 bits, rows of width * 3 bytes: CAMERA_PICTURE put at that
// size, black without one; made once, kept
const unsigned char *wl_camera_picture(int width, int height);

// A game that takes its camera through Media Foundation (MFEnumDeviceSources, then an
// asynchronous MFCreateSourceReaderFromMediaSource): its two imports made windowsloader's, the
// camera chosen or an emulated one (mfcamera.cpp). rgbOrder: the game reads RGB32 frames as R, G,
// B rather than Media Foundation's B, G, R. Returns 1 when both were found
int wl_mfcamera_patch(HMODULE exe, int rgbOrder);
