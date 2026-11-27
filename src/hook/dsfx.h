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

typedef HRESULT(WINAPI *WlCoCreate)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID *);

// DirectSound's I3DL2 reverb (GUID_DSFX_STANDARD_I3DL2REVERB), gone from recent Windows (dsdmo.dll
// of Windows 11 build 26300 has no such class: SetFX failed, Thrill Drive 4 showed an error box):
// 1 for that class
int wl_dsfx_is_i3dl2(REFCLSID cls);
// An object standing in for it, made with create (CoCreateInstance unhooked): the Waves reverb
// (GUID_DSFX_WAVES_REVERB, still there) doing the sound, the I3DL2 settings the game gives put
// onto it. Fails when the Waves reverb cannot be made either.
HRESULT wl_dsfx_i3dl2_create(REFIID iid, void **out, WlCoCreate create);
