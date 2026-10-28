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

#include <stddef.h>

// The player's controllers (keyboard, joysticks, gamepads, vJoy through SDL3) feeding the
// game's inputs (inputs.h), bound in controls.ini with linuxloader's syntax:
//   [Common] / [Driving]   ACTION = BINDING, BINDING + BINDING, ...
//   KEY_<name>             a key, read while the game's window has the focus
//   JOY<n>_BUTTON_<b>, JOY<n>_HAT<h>_<UP|DOWN|LEFT|RIGHT>, JOY<n>_AXIS_<a>[suffix]
//   GC<n>_BUTTON_<name>, GC<n>_AXIS_<name>[suffix]   (SDL gamepads: A, B, START, LEFTX, RIGHTTRIGGER...)
//   suffixes: _POSITIVE, _NEGATIVE (half an axis as a button), _POSITIVE_HALF, _NEGATIVE_HALF,
//             _INVERTED
//   [Config] <name>_DeadZone = 0..32767, FFB_Strength = percent, FFB_Invert = 0/1,
//            FFB_Device = AUTO or a force feedback device's name (--set ForceFeedback.FFB_DEVICE=)
//   [ControllerGUIDs] P<n>_GUID = SDL joystick GUID
// <n> is the player: P1_GUID's device is JOY0 / GC0, the other devices follow in order.

// Reads controls.ini (written with the defaults when missing) and starts the thread that polls
// the controllers. Not called in agent mode: the agent alone drives the game.
void wl_controls_start(const wchar_t *iniPath);

// Reads controls.ini only, without the controllers: the agent mode, for the settings menu's pages
void wl_controls_load(const wchar_t *iniPath);
int wl_controls_has_file(void);

// For the settings menu's page (linuxloader's: the devices, each binding the actions to its own inputs)
int wl_controls_running(void);
const char *wl_controls_player_name(int player);

struct WlControlDevice
{
    int player;  // -1 the keyboard (KEY_), else JOY<player>_ (and GC<player>_ for a gamepad)
    int gamepad; // SDL sees a gamepad
    int ffb;     // the force feedback is played on it
    char name[128];
};
int wl_controls_devices(WlControlDevice *out, int max); // the keyboard, then the controllers connected

struct WlControlRow
{
    char key[64];    // P1_Steer, GearUp...
    char value[512]; // KEY_Left, JOY0_AXIS_0, GC0_BUTTON_START + GC0_BUTTON_BACK...
};
int wl_controls_rows(const char *section, WlControlRow *rows, int max);              // in the file's order
void wl_controls_set_row(const char *section, const char *key, const char *value);   // saved, then reloaded
int wl_controls_default_row(const char *section, const char *key, char *out, size_t size); // a new file's; 0 for none
// A binding's input now: 0 not there, 1 pressed or not in *on (a key, a button, a hat, half an
// axis, a combination), 2 an axis, its raw value -32768..32767 in *raw
int wl_controls_input_state(const char *token, int *on, int *raw);

enum
{
    WL_CAPTURE_BUTTON,   // a key, a button, a hat, half an axis
    WL_CAPTURE_STEERING, // an axis turned to the left
    WL_CAPTURE_PEDAL     // an axis pressed from its rest
};
void wl_controls_capture(int player, int kind);         // waits for an input of one device (-1 the keyboard)
int wl_controls_capture_result(char *out, size_t size); // 1 once captured
void wl_controls_capture_cancel(void);
void wl_controls_ffb_settings(double *strength, int *invert);
void wl_controls_set_ffb(double strength, int invert);                 // applied and saved
int wl_controls_ffb_device(char *out, size_t size);                    // FFB_Device, "" for AUTO; 0 when not connected
int wl_controls_ffb_devices(char (*names)[128], int max);              // the connected force feedback devices
void wl_controls_ffb_playing(char *out, size_t size);                  // the device the effects go to, "" for none
void wl_controls_set_ffb_device(const char *name);                     // "" or AUTO: player 1's; applied at once and saved
