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
title (`<title> [wmmt5] - 59.9 fps - windowsloader 0.1.0-20261025-njz3 FFB Edition`, the frames per
second with a tenth), the picker's,
the menu's "À propos" page and the log. `cmake/build_version.cmake` writes it at each build. A
release adds its archive's number of the day with `-DWL_BUILD_NUMBER=4` (`0.1.0-20261024-4-njz3`);
`WL_FORK` and `WL_EDITION` are the branch's name and edition.

| File | Built by | For |
|---|---|---|
| `windowsloader.exe`, `windowsloader32.dll` | `msvc-x86` | 32-bit games |
| `windowsloader64.exe`, `windowsloader64.dll` | `msvc-x64` | 64-bit games |
| `lang\en.po`, `lang\windowsloader.pot` | both (copied from `lang/`) | the interface's languages |

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
  --list-games     the games windowsloader knows, one line per program it takes: the game, its
                   short name, its version, its force feedback and its link between cabinets
                   (yes, partly, no), the program, the program's CRC32 in the cabinets' copies
                   ("--------" when not known)
```

The short name is the game's in the settings (`[Game.<name>]`, `<name>.ini`) and in the write folder
(`WRITE_DIR\<name>`). The CRC32 is that of the cabinets' copies, for information only: a program
with another CRC32 is recognized and played all the same (a game is told by its program's name, the
files beside it and, for a few, the program's time stamp).

Started alone, without a game (a double click in the Explorer), `windowsloader.exe` opens the game
picker, as linuxloader's: the games found in the games folder as a list, the chosen one's
description beside it (platform, state, program, arguments, force feedback and link between
cabinets: yes, partly or no, what is emulated), Enter or a double
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

The keyboard is read only while a window of the game is in front (the controllers, wheels and vJoy
always are). So that a game whose window would stay behind (GTI Club 3, after a long start) gets the
keys, its window is put in front during its first 10 seconds, again whenever another program takes
the front in that time; after them, a window the player put behind stays there. The picker and the
launcher pass the game their right to put a window in front (`AllowSetForegroundWindow`); when it is
not enough, the window's thread joins the input of the window in front for the time it takes its
place. Not in agent mode. The log tells it: `display: the game's window ... put in front`, or
`... is not in front (another program kept it)`.

Display settings are in `windowsloader.ini` next to the launcher, with linuxloader's `[Display]`
keys and meanings: `ASPECT_RATIO` (`ORIGINAL`; `4:3`, `16:9`, `4:3_LETTERBOX`: the picture whole in
that frame, black bars; `4:3_STRETCH`, `16:9_STRETCH`: the frame filled, deformed; `4:3_CROP`,
`16:9_CROP`: the frame filled, the picture cut), `CROP_POSITION` (-100 left or top to 100),
`CROP_BARREL` (the cut parts bent onto the edges, as on a cylinder; with `PRESENT = GDI` only),
`BARREL_CENTER` and `BARREL_RADIUS` (in % of the frame), `PRESENT` (`DIRECT`, the default since
28/10/2026: the game presents its own picture; `GDI`: windowsloader reads each frame back and draws it
into the window, much slower, for a PC that shows nothing of `DIRECT`, such as a laptop whose screen
is on another graphics chip than the game's), `DISPLAY_MODE` (`WINDOW`,
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

Mario Kart Arcade GP DX (`[Game.mkdx]`, 1.18 and 1.10), cabinets linked over the PC's network:
`LINK = 192.168.1.21,192.168.1.22` lists the cabinets' PCs (up to 4, cabinet 1 first), the same
line on each; `CABINET = 0` finds each cabinet's number from its PC's address in that list (or
`CABINET = 2`). The game sees the cabinets' network it knows (cabinet N at 192.168.112.(10+N)); its
UDP packets (ports 20199 and 20100) go between the listed PCs only, which the Windows firewall
must let in (`MK_AGP3_FINAL.exe`). As on the real cabinets, each one's PCB-ID must differ (1 to 4,
test menu, ゲーム設定 → 基板ID, 1.10: Game options → Board ID; Enter, selector, Enter): E24-03
« 基板ID重複エラー » otherwise.
Without `LINK` the cabinet plays alone.
Sega World Drivers Championship (`[Game.swdc]`) plays as a cabinet alone: the launcher starts
amdaemon (the ALLS platform's service) 5 s before the game, with the game's `config.json` and
windowsloader's settings for a cabinet alone (written in the temporary folder: ALL.Net's
accounting, the network delivery, the electronic money, ABaaS's logs and the LAN install off).
`CABINET`, taken as the game starts (the launcher gives it `-launch=`, unless its command line has
one): `CABINET` (the default) the full cabinet, `-launch=Cabinet`, with its dashboard (838-15415 on
COM20: the wheel's four buttons and two paddles, the speed, gear and rev lamps it shows) and its
LED board (837-15070 on COM2: 9 RGB LEDs, 3 power outputs); `MINI` the mini cabinet,
`-launch=MiniCabinet`, as TeknoParrot runs it, without them (the wheel's buttons do nothing
there). In a full cabinet the network outputs have `speed`, `gear`, `rpm`, `dash_lamp0` to
`dash_lamp7` (the dashboard's) and `led0_r` to `led8_b` (0-255), `fet0` to `fet2` (the LED
board's), besides the I/O board's lamps. The
PC's CD/DVD drives and removable disks are hidden from both (amdaemon stops on "Error 0919 DVD
Drive Remain Error" when it finds a DVD drive, 0917 for a USB key); `D:`, `E:` and `Y:` are the
copy's folders, fixed disks, whatever the PC has under these letters. The
network services the cabinet reaches are played on the PC: ALL.Net (in amdaemon, port 40180), the
Aime server (40182) and the game's title server (in the game, 40181: its season and event
settings); the game shows ALL.Net, Aime and the user database online and plays as a guest (at the
login screen: Start, Left for はい, Start). `LANGUAGE = ENGLISH` (the default), `JAPANESE` or
`CHINESE`: the language of amdaemon's own messages (its errors and warnings); the game's texts are
Japanese. Not yet: the matching server ("GAME SERVER BAD" at the start, no network battles).

A real board can replace an emulated one on a serial port (passthrough, linuxloader's keys):
`[Emulation] EMULATE_JVS = false` with `[Paths] JVS_PATH = COM5` for the JVS board,
`EMULATE_DRIVEBOARD = false` with `SERIAL_<n>_PATH` for the game's other emulated ports (its
`COM<n>`: a wheel's or a motor's board). The same choices are in the "Options du jeu" page, part
"Cartes".

**Tab** pauses the game and opens the settings menu, Tab again closes it (F10 until October 2026;
Tab as in Flycast, linuxloader and Supermodel; Alt+Tab is left to Windows): the game and its cabinet's boards (how the
controls, the game's own options, display, force feedback (its device, strength and direction),
the network outputs the game can send.

The controls page is linuxloader's (itself laid out as Flycast's): the devices found (the keyboard,
then each controller with its `JOY<n>` / `GC<n>` prefix in `controls.ini`), each with a "Lier" button
that opens its bindings page over the whole picture: every action of `[Common]` and `[Driving]`,
grouped by player, with that device's inputs, their state now (an axis around its middle with its dead
zone, a pedal's travel, a button's dot) and "Lier" (the device's next input replaces its own ones for
the action, the other devices' kept), "Délier", and windowsloader's "Ajouter" (the device's next input
joins its own ones: two keys for an action; a key and a joystick's button are each bound from their
device's page) and "Défaut" (the device's inputs of that action as a new `controls.ini` has them);
"Réinitialiser..." does the same for all the device's actions. Below the devices: what the game gets of each of its inputs (the value its board reads), and
the dead zones and other `[Config]` options. Every change is written to `controls.ini` and taken at once.

The force feedback's device, as Supermodel, Flycast, linuxloader and sm2-emu choose it:
`controls.ini` `[Config] FFB_Device` (beside `FFB_Strength` and `FFB_Invert`; `--set
ForceFeedback.FFB_DEVICE=...` overrides it) is `AUTO` (or empty: player 1's wheel, as before) or a
force feedback device's name as the system gives it ("vJoy Device"), compared whole without case
or the spaces around it. The chosen device alone plays the effects, whichever device the wheel is
read from (the steering read on vJoy, the effects on a real wheel, or the reverse); of two devices
of that name, player 1's, else the first. While it is not connected, at the start or unplugged
during the game, player 1's wheel plays them; plugged in again, it takes them back with the current
force. The menu's force feedback page lists "Automatique" and the connected devices ("<name> (non
connecté)" for a chosen one that is absent); a choice there takes effect at once and is saved.
The force's direction is SDL's own, as linuxloader's: a positive constant force pushes the wheel to
the left (SDL gives the direction a force comes from). Up to the 20261028-3 release, windowsloader
sent it the other way round: a `controls.ini` saved with `FFB_Invert = 1` to make up for it must go
back to `FFB_Invert = 0` ("Inverser le sens" unticked).

`[Common] ExitGame` ends the game, as linuxloader's (and Escape under TeknoParrot): by default
`KEY_Escape, GC0_BUTTON_START + GC0_BUTTON_BACK, JOY0_BUTTON_11 + JOY0_BUTTON_10`, added to an existing
`controls.ini` that lacks it; `ExitGame =` (empty) turns it off. The keyboard counts only while the
game's window has the focus, nothing while the settings menu is open; the launcher then closes the
programs it started with the game.

"Sauvegarder" writes the display settings to the file read, "Sauvegarder pour ce jeu" to the game's
own file (made from a copy of the file read the first time). `[Launcher] SETTINGS_MENU = false`, for
a public cabinet: Tab no longer opens the menu over a running game (the agent's `menu on` still
does).

### Languages

The picker and the settings menu speak French or English, and any other language given a catalogue.
`[Launcher] LANGUAGE` of `windowsloader.ini` (or `--set Launcher.LANGUAGE=en`): `AUTO` (the default:
Windows' display language, French as it is, another one through its catalogue when there is one,
`pt_BR` then `pt`, else English), `fr`, `en`, or the code of another catalogue. The picker's list
at the bottom and the menu's "About" page ("À propos") change it at once and write it to
`windowsloader.ini`; only the texts the games' modules give at their start (the network outputs'
descriptions) wait for the next start.

The texts are written in French in the code; each other language is a gettext catalogue,
`lang\<code>.po` beside the launcher (`lang\en.po`), read as it is: no `.mo` to build, no gettext
library. Its `msgid` is the French text, its `msgstr` the translation; an entry left empty or marked
fuzzy keeps the French text, one whose `%` formats differ from the French text's is left out (a
mistaken catalogue cannot crash a game), and the header's `X-Language-Name` is the name shown in the
lists (else Windows' name of the language). A text the language has no translation for is told once
in the game's log (`lang: no en translation for "..."`).

Another language: `python scripts/lang_update.py --new de "Deutsch"` makes `lang/de.po` from the
template, to translate with [Poedit](https://poedit.net) or a text editor, then put in the `lang`
folder beside the launcher. When texts change in the code, `python scripts/lang_update.py` gathers the
marked ones again in `lang/windowsloader.pot` and brings each `.po` up to date (as `msgmerge`: the
translations kept, the new texts empty, the ones gone kept at the end as obsolete), and
`python scripts/lang_update.py --check` tells what is left to translate. In the code, `wl_tr("...")`
translates a text where it is shown and `WL_N("...")` marks one in a table translated later
(`src/common/lang.h`); `xgettext --keyword=wl_tr --keyword=WL_N --from-code=UTF-8` finds the same
texts. linuxloader's catalogues are made the same way, English being its source language.

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
sends to a Sega servoboard; for the other boards, the Sega servoboard's frames made from the force:
80, 81, 83 once, then 84 the torque). A game's companion opens no server: the port is the game's.
The lamps of boards that are not JVS ones go out the same way (`RawLamps`..., `lamp<n>`): Wasteland
Racers 2071's SDC2 board (`SetDigitalOut`, `lamp0` to `lamp15`, named after its `Inputs.xml`),
SWDC's IO4 board (14 outputs, `lamp0` Start, passed on by amdaemon), Storm Racer G's USB board
(`lamp0` to `lamp5`), GTI Club 3's HBHI board (`lamp0` to `lamp7`). Their other lights are values
of their own: Storm Racer G's PWM lights `pwm0` to `pwm5` (0 to 255), GTI Club 3's brightness
`bright0` to `bright7` (0 to 31), Crazy Ride's lights (`BoxLight_r`, `_g`, `_b`, `EdgeLight_...`,
`SideLight_...`, `MachineSideLight_...`, 0 or 1; `SpeedLight`, `StopLight`, `BrakeLight`, 0 to 3;
`GameStateLight`; `StartLight`), Mario Kart Arcade GP DX's cabinet LEDs (Namco commands 0x78 to
0x7A, 9 RGB LEDs each): `led0_r`, `led0_g`, `led0_b` ... `led26_b`, 0 to 15 (the colours' order
supposed, as Crazy Ride's), SWDC's in a full cabinet (its LED board and dashboard, see above); the
other games give their I/O board no lamps.
The menu's "Sorties et réseau" page lists the values the running game can send, with their type and
their last value (kept even when `OUTPUTS_NETWORK` is off), and the server's state.

## License

GPL v3 (see `LICENSE`), by Benjamin Maurin - njz3. Code taken from other projects (MAME,
linuxloader, sm2-emu, Flycast, segatools) stays under its own license and names its origin.
MinHook (`src/minhook`) is under the BSD 2-clause license of its author, Tsuda Kageyu. Dear ImGui
(`src/imgui`, version 1.92.0, taken unchanged from sm2-emu, with its Direct3D 9, Direct3D 10,
Direct3D 11 and Win32 backends) is under the MIT license of Omar Cornut. d3d8to9 (`src/d3d8to9`, from
https://github.com/crosire/d3d8to9 at commit 255338f, its entry point changed for windowsloader) is
under the BSD 2-clause license of Patrick Mours (`src/d3d8to9/LICENSE.md`): Direct3D 8 games are
played on Direct3D 9.
