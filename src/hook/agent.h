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

#pragma once

#include <windows.h>

// Agent mode (launcher --agent): the game driven by another program, one command a line, in the
// protocol of linuxloader's, sm2-emu's and Supermodel's agent modes. The DLL serves the commands
// on the pipe \\.\pipe\windowsloader-<pid>; the launcher relays its stdin and stdout to it.
void wl_agent_init(void);

// Frames presented so far, the size of the picture, and a picture of the next frame (Direct3D 9 games)
unsigned wl_d3d9_frames(void);
void wl_d3d9_size(int *w, int *h);

// From the other renderers' Present hooks (Direct3D 11): the same frame counter and picture size
void wl_frame_presented(void);
void wl_frame_size(int w, int h);
int wl_d3d9_shot(const char *path, DWORD timeoutMs);

// The picture the agent asked for, taken by the other renderers: its path while one is pending,
// then wl_shot_done with 1 when the file is written. Pixels are 32-bit, BGRX or RGBX (rgba set).
const char *wl_shot_pending(void);
void wl_shot_done(int ok);
int wl_write_bmp(const char *path, const unsigned char *pixels, int pitch, int w, int h, int rgba);

// Takes the picture the agent asked for, from the back buffer about to be presented
struct IDirect3DDevice9;
void wl_d3d9_take_pending_shot(IDirect3DDevice9 *dev);
struct IDXGISwapChain;
void wl_d3d11_take_pending_shot(IDXGISwapChain *sc);
