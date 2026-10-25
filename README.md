# windowsloader

Runs Windows arcade games (Sega Europa-R, RingEdge, RingWide, Nu, ALLS, Raw Thrills, Sega Racing
Classic...) on a PC, with the cabinet hardware emulated: I/O boards (JVS, USB HID), drive board and
force feedback, dongles and network. It is the Windows counterpart of
[linuxloader](https://github.com/njz3/linuxloader) and keeps its conventions.

The game runs unchanged, from its own folder: `windowsloader.exe` starts it suspended and injects
`windowsloader32.dll` (or `windowsloader64.dll`), which hooks the Windows APIs the game uses to reach
its hardware ([MinHook](https://github.com/TsudaKageyu/minhook)).

Status: early work. Games played so far:

| Game | Executable | What windowsloader plays |
|---|---|---|
| Sega Rally 3 (Europa-R) | `Rally\Rally.exe` (x86, Direct3D 9) | the USB HID I/O board, the wheel motor's I/O port |
| Daytona Championship USA, and its New Season Edition | `Sega\Daytona\Daytona.exe` (x86, Direct3D 11) | the "SEGA IO Controller" DirectInput board, the cabinet shell (credits, security, TCP 127.0.0.1:18888), InpWrapper.exe (the wheel motor on the parallel port) |

On a PC with two GPUs (a laptop), a Direct3D 11 game asking for the default adapter gets the one
with the most video memory (`WINDOWSLOADER_GPU=default` keeps Windows' choice): a 32-bit game runs
out of address space on an integrated GPU, whose textures are mapped into the process.

## Build

Visual Studio 2022 or 2026 with the C++ workload, CMake 3.21+, vcpkg (the one bundled with Visual
Studio works). From a VS Developer Prompt, or with `VCPKG_ROOT` pointing to vcpkg:

```
cmake --preset msvc-x86
cmake --build --preset msvc-x86-release
cmake --preset msvc-x64
cmake --build --preset msvc-x64-release
```

The DLL must have the bitness of the game, so both presets are needed: everything lands in
`bin/Release`, and each launcher hands a game of the other bitness to its sibling.

The build's version is named as the release archives are, `<version>-<the build's day>-njz3`
(`0.1.0-20261025-njz3`), followed by the branch's edition, "FFB Edition": in the game's window
title (`<title> [wmmt5] - 60 fps - windowsloader 0.1.0-20261025-njz3 FFB Edition`), the picker's,
the menu's "À propos" page and the log. `cmake/build_version.cmake` writes it at each build. A
release adds its archive's number of the day with `-DWL_BUILD_NUMBER=4` (`0.1.0-20261024-4-njz3`);
`WL_FORK` and `WL_EDITION` are the branch's name and edition.

| File | Built by | For |
|---|---|---|
| `windowsloader.exe`, `windowsloader32.dll` | `msvc-x86` | 32-bit games |
| `windowsloader64.exe`, `windowsloader64.dll` | `msvc-x64` | 64-bit games |

## Use

```
windowsloader [options] <game.exe> [game arguments...]
windowsloader               (alone: the game picker)

  --log <file>     log written by the game side (default: windowsloader.log)
  --fullscreen     let the game switch the display to full screen (default: kept in a window)
  --timeout <s>    stop the game after <s> seconds (for tests)
  --iolimit <n>    max reads/writes logged per device handle (default: 500)
  --quiet          do not echo the log on the console
  --agent          drive the game over stdin/stdout (linuxloader's agent protocol)
  --controllers    with --agent: the controllers are read and get the force feedback too
  -c, --config <file>      settings file (default: windowsloader.ini next to the launcher, or the
                           game's <short name>.ini beside it when there is one)
  --set SECTION.KEY=VALUE  overrides one setting of the file (repeatable), e.g.
                           --set Display.ASPECT_RATIO=4:3_CROP
  --write-dir <folder>     everything written goes there, the launcher's and the game's folders
                           only read (default: [Paths] WRITE_DIR of the settings file)
```

Started alone, without a game (a double click in the Explorer), `windowsloader.exe` opens the game
picker, as linuxloader's: the games found in the games folder as a list, the chosen one's
description beside it (platform, state, program, arguments, what is emulated), Enter or a double
click to play. The game runs through `windowsloader.exe` again, its log in `windowsloader.log` next
to the launcher, and the picker comes back when it ends. The games folder is `[Launcher] GAMES_DIR` of
`windowsloader.ini` (relative to the launcher's folder, or a full path), else a `games` folder next
to the launcher or one or two levels up (`C:\Racecab\Games` for
`C:\Racecab\Emulators\windowsloader`); Tab or the "Dossier des jeux" button picks another and saves
it there. Each of its folders is a game when a program windowsloader knows is found in it, six
levels down at most (the table is `src/launcher/gamelist.cpp`). A picture shows when the folder has
`snap`, `title`, `art`, `cover` or its own name as `.png` or `.jpg`. Arrows or a gamepad to choose,
F5 to scan again, Escape or F9 to quit.

In windowed mode the game also leaves the desktop alone: no display mode change, no cursor
clipping, no global keyboard hook, no shutdown or program started on its behalf.

Display settings are in `windowsloader.ini` next to the launcher, with linuxloader's `[Display]`
keys and meanings: `ASPECT_RATIO` (`ORIGINAL`; `4:3`, `16:9`, `4:3_LETTERBOX`: the picture whole in
that frame, black bars; `4:3_STRETCH`, `16:9_STRETCH`: the frame filled, deformed; `4:3_CROP`,
`16:9_CROP`: the frame filled, the picture cut), `CROP_POSITION` (-100 left or top to 100),
`CROP_BARREL` (the cut parts bent onto the edges, as on a cylinder; with `PRESENT = GDI`, the
default), `BARREL_CENTER` and `BARREL_RADIUS` (in % of the frame), `DISPLAY_MODE` (`WINDOW`,
`WINDOW_STRETCH`; `FULLSCREEN`, `FULLSCREEN_STRETCH`: the screen switched to its smallest mode holding the game's picture;
`BORDERLESS`, `BORDERLESS_STRETCH`: a borderless window covering the screen at the desktop's
resolution) and `WIDTH` / `HEIGHT` (the game's render resolution, `AUTO` by default). F11 or
Alt+Enter switches between window and full screen. A game's
own `<short name>.ini` next to `windowsloader.ini` (`gticlub3.ini`) is read instead when it exists.

A game's own options are in its `[Game.<short name>]` section, as linuxloader has them (keys in
capitals; a list's values in capitals, `true`/`false`, numbers), set from the menu's "Options du
jeu" page or by `--set Game.KEY=VALUE` for the game launched. Battle Gear 4 Tuned (`[Game.bg4]`):
`CABINET = PRO` for the Professional edition's cabinet (wide screen, clutch, H shifter: actions
`Clutch`, `Gear1` to `Gear6`; the game switches its own settings at the next start and closes once,
start it again), `HOLD_GEAR = true` to keep the gear engaged once the lever is let go. Sonic &
Sega All-Stars Racing (`[Game.ssasr]`): `FREE_PLAY = false` for one game per coin (free play by
default; the game's test menu has no coin settings, the cabinet's system menu has them).

A real board can replace an emulated one on a serial port (passthrough, linuxloader's keys):
`[Emulation] EMULATE_JVS = false` with `[Paths] JVS_PATH = COM5` for the JVS board,
`EMULATE_DRIVEBOARD = false` with `SERIAL_<n>_PATH` for the game's other emulated ports (its
`COM<n>`: a wheel's or a motor's board). The same choices are in the "Options du jeu" page, part
"Cartes".

**Tab** pauses the game and opens the settings menu, Tab again closes it (F10 until October 2026;
Tab as in Flycast, linuxloader and Supermodel; Alt+Tab is left to Windows): the game and its cabinet's boards (how the
controls reach it, which motor board drives its wheel), controls (bindings captured from the
keyboard, a gamepad or a wheel, saved to `controls.ini`), the game's own options, display, force
feedback strength and direction.
"Sauvegarder" writes the display settings to the file read, "Sauvegarder pour ce jeu" to the game's
own file (made from a copy of the file read the first time). `[Launcher] SETTINGS_MENU = false`, for
a public cabinet: Tab no longer opens the menu over a running game (the agent's `menu on` still
does).

The write folder (`--write-dir`, or `[Paths] WRITE_DIR`, relative to the launcher's folder or a full
path; linuxloader's key): the launcher's folder and the games' are then only read, for a write
filter (UWF) or games on a share. Its root holds `windowsloader.ini` and `controls.ini` (copied there
the first time: change those, not the ones beside the launcher) and `windowsloader.log`;
`WRITE_DIR\<game>` what the game writes, under its full path (`C:\Racecab\Games\X\save.bin` is
`WRITE_DIR\wmmt5\C\Racecab\Games\X\save.bin`), so a game writing above its program's folder or on a
drive redirected into it is taken too. The system's folders have their own: the user's AppData in
`WRITE_DIR\<game>\appdata` (Roaming, Local, LocalLow), the temporary folder in `temp`, ProgramData in
`programdata`, Windows' in `windows`, so a disk under a write filter (UWF) on everything but the write
folder keeps all the game writes, the graphics driver's and the libraries' too. A file opened to be
modified is copied there first; a file read comes from there when it is there, else from its place; a
folder's listing joins both; a DLL the game wrote there is loaded from there. A file removed that is
only in its first place stays there. A game's companion (Initial D 7 and 8's `picodaemon.exe`, SWDC's
`amdaemon.exe`, which the launcher starts beside it) writes in its game's `WRITE_DIR\<game>`, so the
files they share stay shared. The picker reads and writes the write folder's `windowsloader.ini`. Without a write folder (`WRITE_DIR` empty), everything is written in its usual
place.

Network outputs (`[Outputs] OUTPUTS_NETWORK = true`, linuxloader's keys and protocol, MAME's, as in the
njz3 forks of Supermodel and sm2-emu): a TCP server on `OUTPUTS_NETWORK_PORT` (8000) sends
`name = value\r` lines to BackForceFeeder or MAMEHooker, `mame_start = <game>` and the known values
to each client that connects, and `mame_stop = 1` when the game ends; a UDP datagram announces the
start to 127.0.0.1:`OUTPUTS_NETWORK_UDP_PORT` (8001, 0 for none). The values: `RawLamps`,
`RawLamps1`... and `lamp0`... (the JVS board's general purpose outputs), `RawDrive` (the bytes the game
sends to a Sega drive board; for the other boards, the Sega servo board's frames made from the force:
80, 81, 83 once, then 84 the torque). A game's companion opens no server: the port is the game's.

## License

GPL v3 (see `LICENSE`), by Benjamin Maurin - njz3. Code taken from other projects (MAME,
linuxloader, sm2-emu, Flycast, segatools) stays under its own license and names its origin.
MinHook (`src/minhook`) is under the BSD 2-clause license of its author, Tsuda Kageyu. Dear ImGui
(`src/imgui`, version 1.92.0, taken unchanged from sm2-emu, with its Direct3D 9, Direct3D 10,
Direct3D 11 and Win32 backends) is under the MIT license of Omar Cornut. d3d8to9 (`src/d3d8to9`, from
https://github.com/crosire/d3d8to9 at commit 255338f, its entry point changed for windowsloader) is
under the BSD 2-clause license of Patrick Mours (`src/d3d8to9/LICENSE.md`): Direct3D 8 games are
played on Direct3D 9.
