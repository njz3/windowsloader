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

#include "inputs.h"

// The Sega Europe shell (Sega\shell\Shell.exe) as the games of those cabinets talk to it: TCP on
// 127.0.0.1:18888, frames of 11 bytes, three letters naming the game ("DYT" Daytona Championship
// USA, "DSW" DiRT Showdown), the command and the payload length (both big endian), 4 zero bytes,
// then the payload. The game reports what it does, the shell sets the credits and the cabinet's
// SKU and answers a security challenge sent every 10 s. windowsloader is that shell.

struct WlSegaShell
{
    char tag[4];                    // "DYT"
    const char *const *names;       // the game's names for its commands, by number
    int nameCount;
    int msgChallenge, msgAuthenticate, msgSetCredit, msgSetSku;
    int msgCreditTaken;             // the game took a credit (Daytona: start pressed)
    int msgLaunched, msgAck;        // the launch the shell acknowledges (-1: none)
    int freeplay;
    WlInput *coin, *service;        // a press adds a credit (NULL: none)
    int freeplayCredits;            // credits announced in free play (DiRT Showdown starts a game only with one)
};

void wl_segashell_start(const WlSegaShell *shell);
