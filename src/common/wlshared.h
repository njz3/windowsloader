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

// Settings handed from the launcher to the injected DLL. The game inherits the launcher's
// environment, so the launcher sets these before CreateProcess and the DLL reads them in DllMain.

#define WL_ENV_LOG      L"WINDOWSLOADER_LOG"      // log file path (absolute); the launcher echoes it on its console
#define WL_ENV_WINDOWED L"WINDOWSLOADER_WINDOWED" // "1": keep the game in a window, leave the desktop alone
#define WL_ENV_IOLIMIT  L"WINDOWSLOADER_IOLIMIT"  // max logged reads/writes per device handle
#define WL_ENV_AGENT    L"WINDOWSLOADER_AGENT"    // "1": serve the agent commands on \\.\pipe\windowsloader-<pid>
#define WL_ENV_CONTROLS L"WINDOWSLOADER_CONTROLS" // controls.ini path (absolute), next to the launcher by default

#define WL_AGENT_PIPE   L"\\\\.\\pipe\\windowsloader-%lu" // with the game's process id

#define WL_VERSION "0.1.0"
