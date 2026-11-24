# windowsloader

<img src="res/windowsloader.png" width="96" alt="windowsloader's logo">

Runs Windows arcade games (Sega Europa-R, RingEdge, RingWide, Nu, ALLS, Raw Thrills, Sega Racing
Classic...) on a PC, with the cabinet hardware emulated: I/O boards (JVS, USB HID), drive board and
force feedback, dongles and network. It is the Windows counterpart of
[linuxloader](https://github.com/njz3/linuxloader) and keeps its conventions.

The game runs unchanged, from its own folder: `windowsloader.exe` starts it suspended and injects
`windowsloader32.dll` (or `windowsloader64.dll`), which hooks the Windows APIs the game uses to reach
its hardware ([MinHook](https://github.com/TsudaKageyu/minhook)).

The games it plays, one line per version told apart (the picker's table, `src/launcher/gamelist.cpp`;
`windowsloader.exe --list-games` prints it with each program's CRC-32; game by game:
[BILAN.md](BILAN.md)). Version "—": the copies do not tell it, the one played is the Racecab
cabinets'. CRC32: that of the program the Racecab cabinets launch, the one known to play (named
when the game has several); "—": not on the cabinets. For information, except for the Raw Thrills
games (all of them `sdaemon.exe`), Crazy Speed Arcade and Road Fighters 3D, which windowsloader tells
apart by their program's CRC32. Status "plays": checked in agent mode; "with reservations": something is left
(BILAN.md); "in progress": does not play yet. Force feedback and link: "partly" when not all of it
is checked. Another version of a game whose versions are told apart is listed by the picker as
"version not supported".

| Game | Version | Platform | Program | CRC32 | Status | Force feedback | Link |
|---|---|---|---|---|---|---|---|
| Sega Rally 3 | — | Sega Europa-R, 2008 | `Rally.exe` | `9228B72E` | plays | yes | yes |
| Daytona Championship USA | 171221 | Sega (PC), 2017 | `Daytona.exe` | `08823BEF` | plays | yes | yes |
| Daytona Championship USA New Season Edition | 190306 | Sega (PC), 2019 | `Daytona.exe` | `9650CBB4` | plays | yes | yes |
| Ford Racing Full Blown | — | Sega (PC), 2006 | `fordracing.exe` | `EB788677` | plays | yes | yes |
| Initial D Arcade Stage 6 AA | — | Sega RingEdge, 2011 | `id6_dump_.exe`, `InitialD6_GLW_RE_SBUU.exe` | `0C422DA1` (`id6_dump_.exe`) | plays | partly | no |
| Initial D Arcade Stage 7 AAX | — | Sega RingEdge, 2012 | `InitialD7_GLW_RE_SBYD_dumped_.exe`, `InitialD7_GLW_RE_SBYD.exe` | `2B7DF1BC` (`InitialD7_GLW_RE_SBYD_dumped_.exe`) | plays | partly | no |
| Initial D Arcade Stage 8 Infinity | — | Sega RingEdge, 2014 | `InitialD8_GLW_RE_SBZZ_redumped_.exe`, `InitialD8_GLW_RE_SBZZ_redumped4k_.exe` | `56544E57` (`InitialD8_GLW_RE_SBZZ_redumped_.exe`) | plays | partly | no |
| GRID | 1.1 | Sega Europa-R, 2008 | `GRID.exe` | `264E2CCF` | plays | yes | yes |
| DiRT Showdown | 1.2 | Sega Amusements (PC), 2015 | `Showdown.exe` | `879BB2CD` | plays | partly | yes |
| K.O. Drive | — | Sega RingWide, 2013 | `M-DriveR_RingWide.exe`, `M-DriveR_RingWide 1080P.exe`, `M-DriveR_RingWide original.exe` | `0C2B0515` (`M-DriveR_RingWide.exe`) | plays | partly | yes |
| Sonic & Sega All-Stars Racing | — | Sega RingWide, 2011 | `game.exe` | `CEDBD852` | plays | partly | yes |
| Sega Racing Classic | — | Sega RingEdge, 2009 | `d1a.exe` | `FC87F309` | plays | partly | yes |
| Let's Go Safari | — | Sega RingWide, 2014 | `GameSampR_RingWide.exe` | `02E0DDB6` | plays | partly | no |
| Battle Gear 4 Tuned | 2.8 | Taito Type X+, 2007 | `game.exe`, `game4.exe` | `D379A14B` (`game.exe`); English translation BG4Tuned_Translation_v5: `74F40C60` (`game.exe`, km/h) | plays | partly | yes |
| Battle Gear 4 | — | Taito Type X+, 2006 | `game.exe`, `game4.exe` | `282C6BB7` (`game.exe`) | plays | partly | yes |
| Chase H.Q. 2 | — | Taito Type X2, 2007 | `game.exe` | `BE64D00F` | plays | partly | yes |
| D1GP Arcade | — | Taito Type X2, 2009 | `game.exe` | `55A0BD9D` | plays | partly | yes |
| Valve Limit R | — | Taito Type X, 2005 | `launcher.exe` | `919C4D69` | plays | partly | yes |
| Wacky Races | — | Taito Type X2 | `launcher.exe` | `52FD5DFD` | plays, with reservations | partly | yes |
| GTI Club: Supermini Festa! | — | Konami PC, 2008 | `gti3.exe`, `gti3japkey.exe` | `C0F782AF` (`gti3.exe`) | plays | partly | yes |
| Wasteland Racers 2071 | 1.6 | Trio Tech PC, 2006 | `Arcade.exe` | `A6CD1034` | plays | yes | yes |
| Mario Kart Arcade GP DX 1.18 | 1.18 | Namco System ES3 | `MK_AGP3_FINAL.exe` | `280295A6` | plays | partly | yes |
| Mario Kart Arcade GP DX 1.05 | 1.05 | Namco System ES3 | `MK_AGP3_FINAL.exe` | — | plays, with reservations | partly | partly |
| Mario Kart Arcade GP DX 1.06.35 | 1.06.35 | Namco System ES3 | `MK_AGP3_FINAL.exe` | `81D94584` | plays, with reservations | partly | partly |
| Mario Kart Arcade GP DX 1.06 (USA) | 1.06 | Namco System ES3, 2016 | `MK_AGP3_FINAL.exe` | — | plays, with reservations | partly | partly |
| Mario Kart Arcade GP DX 1.10 | 1.10 | Namco System ES3 | `MK_AGP3_FINAL.exe` | `4D386327` | plays | partly | yes |
| Crazy Ride | 1.9.0 | UNIS PC (64 bits) | `CrazyTaxiGame-Win64-Shipping.exe` | `0C97E693` | plays | partly | yes |
| Storm Racer G | — | Sega (PC) | `racing.exe` | `E5A27313` | plays, with reservations | partly | yes |
| Storm Rider | 1.2.6 | Wahlap (PC), 2013 | `motobike.exe` | `ED34CEB0` | plays | no | yes |
| Initial D Arcade Stage Zero Ver.1.31 | 1.31 | Sega Nu (64 bits), 2017 | `InitialD0_DX11_Nu.exe` | `CF32AFCE` | plays, with reservations | yes | no |
| Initial D Arcade Stage Zero Ver.2 | 2.30 | Sega Nu (64 bits), 2017 | `InitialD0_DX11_Nu.exe` | `C7F92CFF` | plays, with reservations | yes | no |
| Frenzy Express | — | Uniana (PC), 2001 | `xwin.exe` | `DBAF1475` | plays | yes | no |
| Sega World Drivers Championship | — | Sega ALLS (64 bits) | `Todoroki-Win64-Shipping.exe` | `3A971F3C` | plays, with reservations | partly | no |
| Wangan Midnight Maximum Tune 6 | — | Namco System ES3X (64 bits), 2018 | `wmn6r.exe` | `C4B9C773` | plays, with reservations | yes | yes |
| Wangan Midnight Maximum Tune 5 DX+ | — | Namco System ES3X (64 bits), 2016 | `wmn5r.exe` | `32E089BC` | plays, with reservations | yes | yes |
| Wangan Midnight Maximum Tune 5 | — | Namco System ES3X (64 bits), 2014 | `wmn5r.exe` | `51060B63` | plays, with reservations | yes | yes |
| Road Fighters 3D | — | Konami PC, 2010 | `jgt.dll` (run in `wl_avsboot.exe`), `jgt_link_cab2.dll` | `68306E6F` (`jgt.dll`), `94023423` (`jgt_link_cab2.dll`) | plays, with reservations | yes | yes |
| Crazy Speed Arcade | — | UNIS PC, 2010 | `Client_r.exe` | `42B6AC2D` | plays, with reservations | partly | yes |
| The Fast and the Furious | — | Raw Thrills PC, 2004 | `sdaemon.exe` | `9AEBF60F` | plays | partly | yes |
| The Fast and the Furious Drift | — | Raw Thrills PC, 2007 | `sdaemon.exe` | `8467C744` | plays | yes | yes |
| The Fast and the Furious Super Bikes | — | Raw Thrills PC, 2006 | `sdaemon.exe` | `25ED956B` | plays | no | yes |
| The Fast and the Furious Super Bikes 2 | — | Raw Thrills PC, 2010 | `sdaemon.exe` | `66E8AD40` | plays | no | yes |
| The Fast and the Furious SuperCars | — | Raw Thrills PC, 2010 | `sdaemon.exe` | `B01B9D7D` | plays | partly | yes |
| Nicktoons Nitro Racing | — | Raw Thrills PC, 2009 | `sdaemon.exe` | `EC2F7B39` | plays, with reservations | partly | yes |
| H2Overdrive | — | Raw Thrills PC, 2009 | `sdaemon.exe` | `30E58AB0` | plays, with reservations | partly | yes |
| Dirty Drivin' | 1.14 | Raw Thrills PC, 2011 | `sdaemon.exe` (`-WinPC -Windowed -r 1360x768`) | `A933D4E5` | plays, with reservations | partly | yes |
| Cars | — | Raw Thrills PC, 2012 | `sdaemon.exe` | `E0444E25` | plays, with reservations | partly | yes |
| Batman | — | Raw Thrills PC, 2013 | `sdaemon.exe` | `0F66E24E` | plays | yes | partly |
| Winter X Games SnoCross | 1.58 | Raw Thrills PC, 2022 | `sdaemon.exe` | `873E033B` | plays, with reservations | partly | yes |

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

The launchers' icon (the Explorer's, the picker window's in the taskbar) is `res/windowsloader.ico`,
built into them by `src/launcher/launcher.rc`; `python scripts/make_logo.py` (Pillow) draws it again,
each size from 16 to 256 pixels on its own, with `res/windowsloader.png`.

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

  --log <file>     log written by the game side (default: windowsloader.log beside the launcher, or at
                   the write folder's root; none with [Launcher] LOG = false;
                   the one before kept as <name>.old.log)
  --fullscreen     let the game switch the display to full screen (default: kept in a window)
  --timeout <s>    stop the game after <s> seconds (for tests)
  --iolimit <n>    max reads/writes logged per device handle (default: 500)
  --quiet          do not echo the log on the console
  --debug          start the game under the launcher as its debugger ([Launcher] DEBUG = true):
                   the DLLs it loads, its errors and a crash in <log>.debug.log, a minidump in
                   <log>.crash.dmp
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

A game that stops while it starts (an access violation, 0xC0000005) may do so before
windowsloader's DLL is in it, in one of its own DLLs, leaving nothing in the log. `[Launcher] DEBUG
= true` (the picker passes it on too) or `--debug` starts it under the launcher as its debugger, from
its first instruction: `<log>.debug.log` beside the log (`windowsloader.debug.log`) lists the DLLs
it loads and where, the threads it starts, its debug strings, its errors (the first 64, the C++
exceptions left out), and for a crash no handler took, where it happened (module and offset, read
or write address), the registers, the code addresses on the stack and the modules; the minidump
`<log>.crash.dmp` opens in WinDbg or Visual Studio. Nothing is written without a log (the write
folder's rule): the console only then. A game that looks for a debugger may run otherwise under it.
The log itself gives each error's calls too (`calls:`).

The short name is the game's in the settings (`[Game.<name>]`, `<name>.ini`) and in the write folder
(`WRITE_DIR\<name>`). Daytona Championship USA's two editions are `daytona3` and `daytona3nse`, the
cabinets' names. The CRC32 is that of the cabinets' copies, for information only: a program
with another CRC32 is recognized and played all the same (a game is told by its program's name, the
files beside it and, for a few, the program's time stamp).

`windowsloader.ini` and `controls.ini` explain each of their settings in a comment above it, with its
default. A missing file is written with the defaults; an existing one (or a game's own `<name>.ini`)
gets at each start the settings and comments it lacks, the values already there and their order
kept, and is left untouched when nothing is missing. A `-c` file of another name is left as it is.
Comments are lines starting with `#`, with no `=` in them (Windows would read one as a setting).

Started alone, without a game (a double click in the Explorer), `windowsloader.exe` opens the game
picker, as linuxloader's: the games found in the games folder as a list, the chosen one's
description beside it (platform, state, its version and the versions supported, program,
arguments, force feedback and link between cabinets: yes, partly or no, what is emulated). A copy
of another version of a game whose versions are told apart (Mario Kart DX: 1.05, 1.06, 1.10 and 1.18,
by their program's build date) is listed as "version not supported". Enter or a double
click to play. The game runs through `windowsloader.exe` again, its log in `windowsloader.log` next
to the launcher, and the picker comes back when it ends. The games folder is `[Launcher] GAMES_DIR` of
`windowsloader.ini` (relative to the launcher's folder, or a full path), else a `games` folder next
to the launcher or one or two levels up (`C:\Racecab\Games` for
`C:\Racecab\Emulators\windowsloader`); its field at the top of the picker (typed, Enter to take it) or its "Choisir..." button picks another and saves
it there. Each of its folders is a game when a program windowsloader knows is found in it, six
levels down at most (the table is `src/launcher/gamelist.cpp`). A picture shows when the folder has
`snap`, `title`, `art`, `cover` or its own name as `.png` or `.jpg`. Arrows or a gamepad to choose,
F5 to scan again, Escape or F9 to quit. O (the gamepad's Y, or the "Options du jeu" button) shows
the chosen game's own options without starting it, the same page as the game's menu has, and
"Enregistrer" writes them in its `[Game.<short name>]` section: of its own `<short name>.ini` when
there is one next to `windowsloader.ini` (the game then reads that file alone), else of
`windowsloader.ini`; taken at the game's next start. Their tables are `src/common/gameoptions.cpp`,
shared by the picker and the games' modules.

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
is on another graphics chip than the game's), `SHOW_FRAME` (`true`: the number of the frame
presented in the window's title, beside the frames per second, 4 times a second; a game that stops
presenting keeps its last number; also on the menu's display page), `KEEP_ACTIVE` (`true` by
default: the game is never told its window lost the activation, another program's window in front,
and runs on as on a cabinet; Sega Rally 3 and Battle Gear 4 Tuned paused then; `false`: told, as
Windows does; also on the menu's display page), `MONITOR` (`AUTO`, the screen the window opens on;
`1`, `2`…: Windows' display n, `\\.\DISPLAYn`), `WINDOW_VSYNC` (`true` by default; `false`: a
Direct3D 9 game kept in a window presents without waiting for the screen, Windows' compositor still
showing whole frames), `FRAME_STATS` (`true`: every 10 s the log tells where the time between two
frames goes: Present and its wait, the game's Sleep and waits and their callers, its CPU, the rest)
and `FRAME_PROFILE` (`true`, with `FRAME_STATS`: the presenting thread sampled every millisecond, the
modules and the game's code its CPU goes to, and the game's fiber switches), `DISPLAY_MODE` (`WINDOW`,
`WINDOW_STRETCH`; `FULLSCREEN`, `FULLSCREEN_STRETCH`: the screen switched to its smallest mode holding the game's picture;
`BORDERLESS`, `BORDERLESS_STRETCH`: a borderless window covering the screen at the desktop's
resolution) and `WIDTH` / `HEIGHT` (the game's render resolution, `AUTO` by default). F11 or
Alt+Enter switches between window and full screen. In full screen and borderless, a menu bar of
the game's window ("File") is taken off, and given back in a window; the window keeps the place,
size and borderless style windowsloader gave it whatever the game does to it after (a game that
resizes its window to its own resolution, to the work area above the taskbar, or puts its frame
back). A Direct3D 9 `Present` that Windows answers "window hidden" or "mode changed"
(`S_PRESENT_OCCLUDED`, `S_PRESENT_MODE_CHANGED`) is told `D3D_OK` to the game. A game's
own `<short name>.ini` next to `windowsloader.ini` (`gticlub3.ini`) is read instead when it exists.

A game's own options are in its `[Game.<short name>]` section, as linuxloader has them (keys in
capitals; a list's values in capitals, `true`/`false`, numbers), set from the menu's "Options du
jeu" page or by `--set Game.KEY=VALUE` for the game launched. Battle Gear 4 Tuned (`[Game.bg4]`):
`CABINET = PRO` for the Professional edition's cabinet (wide screen, clutch, H shifter: actions
`Clutch`, `Gear1` to `Gear6`; the game switches its own settings at the next start and closes once,
start it again; with a write folder, the Pro cabinet's settings and saves are kept apart in
`bg4pro`, so going from one cabinet to the other no longer closes the game each time),
`HOLD_GEAR = true` to keep the gear engaged once the lever is let go, `SPEAKERS = CABINET` (the
default) for the cabinet's 7.1 speakers, which Windows mixes down to the PC's (the game's music needs
them: told stereo, it plays its effects only), `STEREO` for the former setting,
`NESYS_SERVER = host[:port]` (port 80 by default) for NESYS, the game's online service: its
requests (HTTP, to `siru4cc.battlegear.net`, a name that still leads to a host where nothing
answers) all go to that server, past windowsloader's refusal of any host off the PC, and without
`LINK` its broadcast comes back to it, as its network check at the start wants; `124.146.156.106` by default,
`OFF` (or empty) for NESYS offline. The player's NESYS entry key is in the cabinet's reader from the start with
`ENTRY_KEY = true`, and the `EntryKey` action (`KEY_K` by default) puts it in or takes it out (out by default); its 7
characters are `KEY_ID` (letters and digits, set in the game's options, read at each reading of the key), else read from `OpenParrot\KeyId.txt` in the game's folder, as TeknoParrot does (the
file the NESYS Revival Project's KeyManager keeps), else from the one windowsloader makes in the
write folder (two letters, `T`, four digits; the I/O test's `KEY ID`). Sonic &
Sega All-Stars Racing (`[Game.ssasr]`): `FREE_PLAY = false` for one game per coin (free play by
default; the game's test menu has no coin settings, the cabinet's system menu has them).

Road Fighters 3D is a DLL, `jgt.dll`: `windowsloader "<game folder>\jgt.dll" [--avs-log]` runs it in
`wl_avsboot.exe`, beside the launcher (32-bit), which boots Konami's AVS and e-Amusement offline from
the game's `prop\` files (`--avs-log`: AVS's own log in windowsloader's; after the game's path, as the
game's arguments). Without e-Amusement the game stops on a network warning (5-2002-0000): SERVICE goes
on. `[Game.rf3d]`: `VIEW = 2D` (the default) starts it in 2D, the sharp picture of an ordinary
screen, `3D` in the cabinet's stereo (two views interlaced line by line, for its polarized screen and
glasses); the 2D/3D button (action `View1`) changes it during the game. Dirty Drivin'
(`[Game.dirtydrivin]`) is told a 1360x768 screen at least (`VIRTUAL_DISPLAY`), its loading stopping
on a lower one, and started with `-WinPC -Windowed -r 1360x768` by the picker, as windowsarcadeloader
does.

Mario Kart Arcade GP DX (`[Game.mkdx]`, 1.18 and 1.10), cabinets linked over the PC's network:
`LINK = 192.168.1.21,192.168.1.22` lists the cabinets' PCs (up to 4, cabinet 1 first), the same
line on each; `CABINET = 0` finds each cabinet's number from its PC's address in that list (or
`CABINET = 2`). The game sees the cabinets' network it knows (cabinet N at 192.168.112.(10+N)); its
UDP packets (ports 20199 and 20100) go between the listed PCs only, which the Windows firewall
must let in (`MK_AGP3_FINAL.exe`). As on the real cabinets, each one's PCB-ID must differ (1 to 4,
test menu, ゲーム設定 → 基板ID, 1.10: Game options → Board ID; Enter, selector, Enter): E24-03
« 基板ID重複エラー » otherwise.
Without `LINK` the cabinet plays alone.
Daytona Championship USA and its New Season Edition (`[Game.daytona3]`, `[Game.daytona3nse]`) link
the same way: `LINK` lists the cabinets' PCs (up to 8, cabinet 1 first, the main one), the same line
on each, and `CABINET` is found from it (or set). The game sees its cabinets at 192.168.1.N (N the
cabinet's number) on the network it knows, whatever the PCs' real addresses; its TCP and UDP traffic
(26000, 5888 and the others) goes between the listed PCs only, which the Windows firewall must let in
(`Daytona.exe`). `[Network] NumCabinets` and `CabinetID` of `ShellData.ini` are read from them,
the file is left as it is. The link sends the players' pictures to the other cabinets and does not
hold without a camera: the game's `CAMERA` option (below) gives it the PC's or an emulated one.
Without `LINK` the cabinet plays alone (cabinet 1 of 1).
Ford Racing Full Blown (`[Game.fordracing]`), GTI Club: Supermini Festa! (`[Game.gticlub3]`), DiRT
Showdown (`[Game.showdown]`), Sega Rally 3 (`[Game.segarally3]`, cabinets 1 to 4, its
`[Network] CabinetID` of ShellData.ini read from `LINK`; Winsock 1's functions taken too), GRID
(`[Game.grid]`, cabinets 1 to 6, 1 hosts; its discovery server's territory and track pack, none, made the searches' 0), Wasteland Racers 2071 (`[Game.wasteland]`, the lowest
address hosts, its race in DirectPlay) , Sonic & Sega All-Stars Racing (`[Game.ssasr]`: the game
gives itself 192.168.88.<its test menu's CABINET ID>, translated to `LINK`'s PCs, so that CABINET
ID must be the cabinet's place in `LINK`, set in gametest.exe's GAME ASSIGNMENTS; raced on the cabinets), Sega Racing Classic (`[Game.src]`: `PALETTE_FIX`, true by default, its shader reading its colour RAM in the middle of a texel, without the red specks of NVIDIA boards; its 192.168.75.k
are `LINK`'s PCs; its test menu's GAME ASSIGNMENTS: cabinet 1 MAIN, TOTAL NODES the count, cabinet
k SUB with CAR NUMBER k, set twice on a blank save; raced on the cabinets), K.O. Drive
(`[Game.kodrive]`, 2 to 6 cabinets: its 192.168.99.k of group A are `LINK`'s PCs, its LINK ID,
TOTAL MACHINE and GROUP ID taken from `LINK`, nothing to set in its test menu; cabinet 1 leads the
race; UDP 11882; raced on the cabinets), Storm Rider (`[Game.stormrider]`, up to 4 bikes: a broadcast
on UDP 9000, then TCP 5001 and UDP 5002; the first to start a race serves it; raced on the
cabinets), Storm Racer G
(`[Game.srg]`, up to 8: its multicast group 234.7.7.7 on UDP 5002, sent to each PC of `LINK`;
raced on the cabinets),
Crazy Ride (`[Game.crazyride]`: `Plugins\McastUDP.dll`'s multicast 234.2.2.1 on UDP 8888, sent to
each PC of `LINK`; linked, its `bEnableOnline` setting is read true, for its matching screen,
and its ping answered; raced on the cabinets), Wangan Midnight Maximum Tune 5, 5 DX+ and 6
(`[Game.wmmt5]`, `[Game.wmmt5dxp]`, `[Game.wmmt6]`: cabinet k is 192.168.92.(10+k) to the game, its
board ID k − 1 and its key serial its own, nothing to set in the test menu; its announcements to the
group 225.0.0.1 on UDP 50765 are sent to each PC of `LINK`, its own included, as the group's loopback
does; a player accepts VS in "Select VS acceptance", another one starting on a second cabinet is asked
"Challenge other racers?", YES with the wheel held right; raced on the cabinets) — and Battle Gear 4 and 4 Tuned (`[Game.bg4eng]`, `[Game.bg4]`: its
MACHINE NO from `LINK`) find their cabinets on their network by a broadcast, then talk to each
other's addresses: `LINK` lists the cabinets' PCs (up to 8, the same line on each), `CABINET` is
found from it (or set). Their traffic goes between those PCs only, at their real addresses: a
broadcast is sent to each of them, any other address of the network is refused, and the game sees
only the PC's card of `LINK` (a VPN's left out), and its own name resolves to its address of `LINK`
alone. A broadcast to a port where the game has several UDP sockets (GRID) goes as a broadcast on
`LINK`'s network, which reaches each of them. Wacky Races (`[Game.wacky]`), D1GP (`[Game.d1gp]`, 2
cabinets, LINK BATTLE in Mode Select), Valve Limit R (`[Game.vlimitr]`) and Chase H.Q. 2 (`[Game.chasehq2]`, 2
cabinets) link through Windows' DirectPlay: cabinet 1 of
`LINK` hosts, the others join it at its address (their test menu's NETWORK-ID or CONNECTION METHOD,
or Chase H.Q. 2's LINKHOST and HOSTADDR, taken from `LINK`); Windows' DirectPlay feature must be on and
the firewall let the game in.
Ford Racing's `CabinetPostion` and Showdown's
`[Network] CabinetID` are read from `LINK` and `CABINET`; GTI Club's CABINET ID is its test
menu's (NETWORK OPTIONS), different on each cabinet. The log traces the link (each bind, connect
and accept, the first packets with each PC on each port, the traffic every 10 s). The four raced
two linked cabinets (racecab1 and 2); in Sega Rally 3, once a cabinet picks Quick Race, the other
presses Start within its countdown (about 15 s) to join.
The Raw Thrills games (`[Game.<name>]`, up to 8 cabinets), Road Fighters 3D (`[Game.rf3d]`) and
Crazy Speed Arcade (`[Game.crazyspeed]`, its LAN mode) link the same way, `LINK` and `CABINET` on
each cabinet. The Fast and the Furious, its Drift, Super Bikes and Nicktoons take their machine ID
and their networking (DIP switches 1-3 and 8 of their board) from `CABINET`; the other Raw Thrills
games take theirs from their test menu (SuperCars and Super Bikes 2: "Enable networking and set
network ID here"), to be set to `CABINET`. A Raw Thrills game reboots its cabinet when those
settings change: it ends, and the launcher starts it again. H2Overdrive's player number and
Cars' network ID come from `CABINET` too. Road Fighters 3D's CABINET ID (its NETWORK OPTIONS) is
`CABINET`, over its test menu's; `jgt.dll` serves each cabinet; its in-store battle (店内対戦) is
chosen on each cabinet, as Crazy Speed's IN-STORE TOURNAMENT (its GAME MODE, at the wheel).
Raced linked on one PC (two games, 127.0.0.2 and .3): The Fast and the Furious, its Drift, Super
Bikes 1 and 2, SuperCars, Nicktoons, Dirty Drivin', Cars, Batman, Road Fighters 3D and Crazy
Speed, SnoCross (one cabinet starts, the other joins: "JOIN IN NOW!"); H2Overdrive's players
find each other. On the cabinets (racecab1 and 2): all of them raced linked (Batman linked, its
rival not seen yet). SuperCars, Super Bikes 2 and SnoCross read their machine ID (their prefs)
from `CABINET`.
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

A game that refuses a screen smaller than it needs (a cabinet's CRT at 512x384 or 1024x384: Crazy Ride
and Sega World Drivers Championship "The current resolution is too low to run this game", Sonic & Sega
All-Stars Racing "Résolution de jeu non supportée") is told a larger one: `VIRTUAL_DISPLAY` in its
`[Game.crazyride]`, `[Game.swdc]`, `[Game.ssasr]`, `[Game.srg]` section, `AUTO` by default (its minimum,
1280x720, 1920x1080 for Crazy Ride, 1360x768 for Storm Racer G (its race at 8 to 12 frames/s on a 1920x240
screen otherwise), only when the real screen is smaller; on a larger screen that lacks it, that
size only added to the modes' lists), a size (`1280x720`, always) or `OFF`. The game then sees that screen
wherever it asks (GetSystemMetrics, EnumDisplaySettings(Ex) and their modes' list, GetMonitorInfo,
GetDeviceCaps, Direct3D 9's adapter modes, DXGI's output and its modes) and draws at that size; its
picture is put on the real screen by `DISPLAY_MODE` and `ASPECT_RATIO` (on a super resolution CRT,
`BORDERLESS_STRETCH` with `ASPECT_RATIO = 4:3`). windowsloader's own calls and Windows' still get the
real screen. Crazy Ride and SWDC (Unreal Engine 4) refuse a screen under 640x480 and draw at the screen's
size: SWDC told 1280x720, its 16:9 picture; Crazy Ride told 1920x1080 (under that size it draws its 3D
view at 4:3 in the left three quarters of its picture). Sonic & Sega All-Stars Racing looks for its
`Settings.ini` size (1280x720) at 60 Hz among Direct3D 9's modes: the mode is added at 60 Hz and at the
screen's rate. Tried on the cabinets at 512x384, 640x480, 1024x384, 1280x240 and 1920x1080.
A size larger than the real screen is taken too (`VIRTUAL_DISPLAY = 2560x1440` on a 1920x1080 screen,
or 1920x1080 on a CRT): a game that draws at the screen's size then draws at that one, its picture
reduced on the real screen (a sharper picture, at the cost of the graphics card); written by hand in
the `[Game.<name>]` section of any other game, it is taken the same way (`AUTO` there: the real screen).
A game that fixes its own resolution (its settings file, Sega Rally 3's `[Display] WIDTH`/`HEIGHT`) draws
at that one whatever it is told; one with a 2D interface drawn for a size may misplace it.

The Taito Type X games' `D:\` and `E:\` are their own folder (nothing written on the PC's real D: or
E:). A folder there that the game writes in without making it (D1GP's `bk_info` and `card_backup`, which
a cabinet's drive has) is made in the game's folder when the game creates a file in it, or in the
write folder (`WRITE_DIR`) when there is one.

Sega Rally 3 pauses when its window loses the activation (another program's window in front: a
race's loading or the race itself frozen until its window was clicked): windowsloader does not pass
that loss on (`[Display] KEEP_ACTIVE`, for all the games), the game runs on as on a cabinet.

Sega Rally 3 can also race Sega Rally Revo's tracks (the PC game, same engine): `[Game.segarally3]
REVO_DIR` names the folder of a full Revo (with `Main_release\tracks`), relative to the game's `Rally`
folder or a full path. The Revo tracks found there (up to 24: tropical, canyon, alpine, lakeside,
safari, arctic) are added to Quick Race, read from that folder (nothing copied); its track select then
lists the environments first, then, the environment chosen, its tracks (Sega Rally 3's own first, then
Revo's). A Revo track's time limits are those of the Sega Rally 3 track of its environment (Desert for
safari, Alpine for arctic), its audio events too. For `Rally.exe` 9228B72E (the cabinets' editions)
only. Linked too (Quick Race): every cabinet votes on the tracks chosen, Revo's counted (the most
chosen, on a tie Tropical, Canyon, Alpine then the Revo tracks in their list's order), and the track
select's second page gets what is left of its 15 seconds (5 at least); the cabinets must have the same
Revo tracks: windowsloader gives the game a network id of its own made from them (the game info
screen's NETWORK ID; the game's own is 1, 1001 on a twin cabinet), so that a cabinet links only with
those of the same tracks, never with one without Revo. Revo's championships are not in a link. The
championship keeps its four stages, and Revo championships of four stages are added, one per
environment with four Revo tracks (two with five: A its first four, B its last four; none for Lakeside,
with one): after the gearbox, a page lists Sega Rally 3's own championship first, then the Revo ones.
Their races keep the championship's 22 places (5 cars on the track, the others met as the player
overtakes): a Revo track has none of the start points Sega Rally 3's tracks give those 5 cars, which
windowsloader then places 150 segments apart, as the game does when a track has fewer than five (its
own completion writes nothing when it has none). The Revo tracks are named after their environment and
their route's number (Alpine 6 for alpine6; Revo's own names are in its encoded language files), the
environment's name in the game's language, Arctic and Safari in English; in the race, the environment's
name. Their cards are Revo's (its frontend track cards, the same file in Sega Rally 3's folder, which
the game never loads: loaded while Quick Race's track select is shown only, the game's resources
holding 16 libraries at most). Sega Rally 3's own tracks stay: Tropical, Canyon and Alpine first in
their environment's list, Lakeside as the championship's last stage (never in Quick Race, as in the
game), Desert.

Sega Rally 3's cameras (from SR3CamLab's findings and profiles, by Ninja Dynamics, MIT license:
https://github.com/ninjadynamics/SR3CamLab), both in its "Options du jeu" page and changed there at
once: `[Game.segarally3] CHASE_CAM` gives the camera behind the car other settings (`DAYTONA`,
`CHASE`, `CHASE_FAR`, `DRONE`; `OFF`, the default: the game's own), the camera turning toward the
car's path rather than its nose, its distance, height and field of view (the far chase camera the same,
farther and higher); `HIDDEN_CAMERAS = true` adds the game's cameras absent from the arcade's View
Change cycle (Chase, Bumper, Bonnet): Far chase, Cockpit (from the passenger's seat, as the game
places it), Driver's seat (the same mirrored), Wheel; each camera's name is shown at each change, and
the camera chosen is kept from one stage to the next. The car's spin and free cameras (they stop the
race's clock or need a mouse) are left out. The game's code is only changed once one of them is
wanted.

A real board can replace an emulated one on a serial port (passthrough, linuxloader's keys):
`[Emulation] EMULATE_JVS = false` with `[Paths] JVS_PATH = COM5` for the JVS board,
`EMULATE_DRIVEBOARD = false` with `SERIAL_<n>_PATH` for the game's other emulated ports (its
`COM<n>`: a wheel's or a motor's board). The same choices are in the "Options du jeu" page, part
"Cartes".

A game that photographs its players has a camera option in its "Options du jeu" page, its own for
each game (Daytona Championship USA, both editions; Mario Kart Arcade GP DX, whose test menu's ゲーム設定 →
ナムカム（カメラ）の使用 must be ON for it to take photos; 1.05 and 1.06: its GAME OPTIONS' NAMCAM and
STEERING WHEEL REACTION, the wheel's force, are given ON as the game reads its settings, the file
left as it is, unless `CAMERA_WHEEL_ON = false`): `CAMERA = AUTO` (the default: the PC's
camera, else the emulated one), `PICTURE` (the emulated one) or the name of one of the PC's video
capture devices, which the list shows (that one, else the emulated one); the emulated camera shows
`CAMERA_PICTURE` (a JPG or PNG file, put at the game's size; black when empty).
`[Emulation] CAMERA`: `AUTO` (the default) leaves it to that option, other games get no camera;
`true` always gives the PC's cameras; `false` never (the emulated one then).

**Tab** pauses the game and opens the settings menu, Tab again closes it (F10 until October 2026;
Tab as in Flycast, linuxloader and Supermodel; Alt+Tab is left to Windows), its pages in hikaruloader's order: display, the game's own options,
controls, force feedback (its device, strength and direction), the game and its cabinet's boards (how the
controls are emulated), the network outputs the game can send.

The controls page is linuxloader's (itself laid out as Flycast's): the devices found (the keyboard,
then each controller with its `JOY<n>` / `GC<n>` prefix in `controls.ini`), each with a "Lier" button
that opens its bindings page over the whole picture: every action of `[Common]` and `[Driving]` (with the game's own actions the file lacks, unbound),
grouped by player, with that device's inputs, their state now (an axis around its middle with its dead
zone, a pedal's travel, a button's dot) and "Lier" (the device's next input replaces its own ones for
the action, the other devices' kept), "Délier", and windowsloader's "Ajouter" (the device's next input
joins its own ones: two keys for an action; a key and a joystick's button are each bound from their
device's page) and "Défaut" (the device's inputs of that action as a new `controls.ini` has them);
"Réinitialiser..." does the same for all the device's actions. Below the devices: what the game gets of each of its inputs (the value its board reads), and
the dead zones and other `[Config]` options. Every change is written to `controls.ini` and taken at once.

A real Sega 837-15257 "IO4" I/O board on USB (VID 0CA3, PID 0021) is one more controller: "SEGA
IO4 837-15257" in the devices, a `JOY<n>` bound and captured as the others (its GUID in
`[ControllerGUIDs]` to give it a player). Its 8 ADCs are axes 0 to 7 (0x0000 to 0xFFFF, a wheel at
0x8000 in the middle), its switches buttons 0 to 31 (player 1's bit n is button n, player 2's
button 16 + n), a coin counted in chute 1 or 2 presses
button 32 or 33 for 100 ms. SWDC's cabinet wires ADC 0 the wheel, 1 the accelerator, 2 the brake,
and button 9 Test, 7 Start, 6 Service, 5 to 2 up, down, left, right, 1 the view. The board is only
read (no command: it sends its reports by itself, BackForceFeeder's way), shared with the programs
that read it too; a switch pressed is at 1, and a board whose first report has most switches at 1
(the unwired ones) is read inverted, as the log tells. It may be plugged in or out while the game
runs; the log says `io4: board found` and what its first report holds.

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
Each kind of effect has its gain in the settings file's `[ForceFeedback]` (linuxloader's names),
in percent, 100 by default, 0 (off) to 200, read at the start, under `FFB_Strength` (0 to 200 %, the
menu's "Force", for every effect: the constant force, the spring, the damper and the vibration):
`FFB_GAIN_CONSTANT` (the torque the game computes, a ramp), `FFB_GAIN_SPRING` (a spring, also one
windowsloader makes from a stiffness and a target position: Mario Kart DX, Trio Tech, Storm Racer
G, the panel springs of Sega Racing Classic and Daytona), `FFB_GAIN_DAMPER` (a brake against the
wheel's movement: Sega Racing Classic, Daytona) and `FFB_GAIN_SINE` (a vibration, a shake, any
periodic wave). A game whose module gives only one torque has it under `FFB_GAIN_CONSTANT`. The
Sega servoboard (Initial D 6 to 8, Initial D Zero, SWDC, K.O. Drive, Sonic & Sega All-Stars
Racing) plays all its effects: the torque and its pages of torques as a force, its spring (`87`
the point, `88` on or off, `8B` the stiffness), its damper (`86`) and its sine (`85`) as effects
the player's wheel holds itself (a friction for a wheel without a damper), all under the gain `83`
and held within the maximum force `81`, as a real board (cold: `81 40`, `83 40`, half of its
strength). The log gives the effects as they change (`ffb: effects`). The scales of the damper,
of the spring's point and of the pages are readings, not measures. The other DirectInput dampers are not played; the Sega board's raw frames sent to BackForceFeeder by the
network outputs stay as the game sent them. The log says the gains read (`ffb: gains`).
The force's direction is SDL's own, as linuxloader's: a positive constant force pushes the wheel to
the left (SDL gives the direction a force comes from). Up to the 20261028-3 release, windowsloader
sent it the other way round: a `controls.ini` saved with `FFB_Invert = 1` to make up for it must go
back to `FFB_Invert = 0` ("Inverser le sens" unticked).

When the game ends (its own end, `ExitGame`, the menu's or the agent's quit), the wheel's force
feedback effects are stopped and the wheel closed before the process goes (SDL_StopHapticEffects,
by the controls thread); a crash or a game killed from outside leaves them to the wheel's driver.

`[Common] ExitGame` ends the game, as linuxloader's (and Escape under TeknoParrot): by default
`KEY_Escape, GC0_BUTTON_START + GC0_BUTTON_BACK` (also `JOY0_BUTTON_11 + JOY0_BUTTON_10` up to the
20261118-1 release), added to an existing
`controls.ini` that lacks it; `ExitGame =` (empty) turns it off. The keyboard counts only while the
game's window has the focus, nothing while the settings menu is open; the launcher then closes the
programs it started with the game.

The common actions' default keys are MAME's: `Test` F2 (T before), `P1_Coin` 5, `P2_Coin` 6,
`P1_Start` 1, `P2_Start` 2, `P1_Service` 9 (S before), `P2_Service` 0; also `P1_Gas_Digital` left Ctrl (MAME's pedal 1, the Up arrow kept) and `Pause` P
(the Codemasters games). An existing `controls.ini` gets the lines it lacks (`P2_Coin`, `P2_Start`,
`P2_Service`, `Pause`); its own lines (`Test = KEY_T`...) stay as they are.

A key or a button on an axis or a pedal (`P1_Steer_Left`, `P1_Steer_Right`, `P1_Gas_Digital`...)
moves it as MAME's keys move an analog control, hikaruloader's settings: `[Config] Axis_Key_Ramp`
milliseconds to take it all the way (500 by default), `Axis_Key_Return` back to the middle or
released when let go or turned the other way (250); 0 at once (windowsloader's way before). Also on
the menu's controls page, with the dead zones.

`controls.ini` numbers a joystick's buttons as SDL does, from 0 (`JOY0_BUTTON_0` is the first one),
where Windows' and vJoy's panels count from 1. The menu's controls page shows them from 1 by default
("Bouton 1" for `JOY0_BUTTON_0`): `[Input] BUTTONS_FROM_ONE = true` of `windowsloader.ini`, `false`
for SDL's own numbers; its box under the devices' list writes it at once. `controls.ini` keeps SDL's
numbers either way.

"Sauvegarder" writes the display settings to the file read, "Sauvegarder pour ce jeu" to the game's
own file (made from a copy of the file read the first time). `[Launcher] SETTINGS_MENU = false`, for
a public cabinet: Tab no longer opens the menu over a running game (the agent's `menu on` still
does).

### The log

`windowsloader.log`, a text file for debugging (what the game opens, its devices, the link, the
force feedback...), is written beside `windowsloader.exe`, or at the write folder's root when there
is one (`[Paths] WRITE_DIR` or `--write-dir`), made anew at each start, whatever the folder the
launcher is started from; the one before is kept as `windowsloader.old.log` (the same for a
companion's `-companion.log`). `[Launcher] LOG = false` writes none: the picker's "Journal" box and the
settings menu's (its last page) change it, from the next start. `--log <file>` names another file.
A write folder that cannot be written writes none either.

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
the first time: change those, not the ones beside the launcher; when one beside the launcher is newer
than its copy, its changes are not read and the launcher's console, the log and the top of the menu's
"Jeu" page say so, as linuxloader does) and `windowsloader.log`; while it is in use, the "Jeu" page
also says so with the folder the game's writes go to;
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
Mario Kart DX 1.05's program is limited to 2 GB, too little for its title's textures here (a black
screen after the attract, the item button not read). The limit comes from the program's header, when
the process is made: the launcher starts a copy with only that flag changed, in
`WRITE_DIR\mkdx\large-address` (beside the launcher without a write folder), made when missing or
different, in the game's folder and shown to the game as its own program. The game's file is not
changed. With a write folder that cannot be used, no copy: the game starts as it is, and says so.
A write folder that cannot be used (it cannot be made, a test file cannot be made, written and
removed in it, or the settings cannot be copied there; the DLL tests `WRITE_DIR\<game>` the same
way) is still the only place written to, so nothing is written at all: the settings and the game's
files are read in their usual places, never written there (two programs started by mistake on the
same write folder must not change the settings they share), the game's writes and windowsloader's
own fail, and there is no log (but `--log`'s). The reason is on the launcher's console and in the
debugger's output (DebugView); over the game, for 20 s from its first frame, without a message box
or a pause: "Cannot write to the write folder", the folder, "Nothing is saved: the settings and the
game saves are read from the usual folders and not written." (the same text in Supermodel's and
Flycast's njz3 forks), and in the Tab menu's game page. The files a game cannot run without, named by
a filter on their names (`[Paths] TEMP_WRITES`, wildcards with `;` between them, and the game's
module: Wangan Midnight's bookkeeping `*BK_*.bin`, which it stops without), are written instead to
working copies in Windows' temporary folder (`%TEMP%\windowsloader-nosave-<pid>`, made from their
first place), read back during the run, and removed by the launcher at the end, or at its next start
after a crash; nothing else is written there (as linuxloader's working copies).

Network outputs (`[Outputs] OUTPUTS_NETWORK = true`, linuxloader's keys and protocol, MAME's, as in the
njz3 forks of Supermodel and sm2-emu): a TCP server on `OUTPUTS_NETWORK_PORT` (8000) sends
`name = value\r` lines to BackForceFeeder or MAMEHooker, `mame_start = <game>` and the known values
to each client that connects, and `mame_stop = 1` when the game ends (after a servo board frame of no
torque when windowsloader made them from the force); a UDP datagram announces the
start to 127.0.0.1:`OUTPUTS_NETWORK_UDP_PORT` (8001, 0 for none). The values: `RawLamps`,
`RawLamps1`... and `lamp0`... (the JVS board's general purpose outputs), `RawDrive` (the bytes the game
sends to a Sega servoboard; for the other boards, the Sega servoboard's frames made from the force:
80, 81, 83 once, then 84 the torque; none for Sega Rally 3, whose motor is a parallel port's byte,
and whose I/O board gets no lamps from the game). A game's companion opens no server: the port is the game's.
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
The menu's "Sorties réseau" page lists the values the running game can send, with their type and
their last value (kept even when `OUTPUTS_NETWORK` is off), and the server's state. Its box "Envoyer
les sorties réseau" starts or stops the server at once, the game going on (no new start needed):
started, it announces the game by UDP and gives each client that connects every known value;
stopped, its clients get `mame_stop = 1` and the port is closed. The choice is written to the
settings file read (`OUTPUTS_NETWORK = true` or `false`) for the next starts; a `--set
Outputs.OUTPUTS_NETWORK=...` of the launcher still wins at those. A port another program holds is
told there (`OUTPUTS_NETWORK_PORT` to change in the file). The agent's `menu outputs on|off` does the
same.

Sound: on an output set above 48000 Hz in Windows, a game using XAudio2 2.0 to 2.7 that asks for the
output's rate gets a mastering voice at 48000 Hz (XAudio2's reverb stops there). The log's `xaudio2:`
lines give the output's rate and channels and what the game asks for.

## License

GPL v3 (see `LICENSE`), by Benjamin Maurin - njz3. Code taken from other projects (MAME,
linuxloader, sm2-emu, Flycast, segatools) stays under its own license and names its origin.
MinHook (`src/minhook`) is under the BSD 2-clause license of its author, Tsuda Kageyu. Dear ImGui
(`src/imgui`, version 1.92.0, taken unchanged from sm2-emu, with its Direct3D 9, Direct3D 10,
Direct3D 11 and Win32 backends) is under the MIT license of Omar Cornut. d3d8to9 (`src/d3d8to9`, from
https://github.com/crosire/d3d8to9 at commit 255338f, its entry point changed for windowsloader) is
under the BSD 2-clause license of Patrick Mours (`src/d3d8to9/LICENSE.md`): Direct3D 8 games are
played on Direct3D 9. Sega Rally 3's cameras are reimplemented from SR3CamLab's findings and profiles (MIT
license, Copyright (c) 2026 Ninja Dynamics).
