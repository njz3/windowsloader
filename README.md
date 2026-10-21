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

| File | Built by | For |
|---|---|---|
| `windowsloader.exe`, `windowsloader32.dll` | `msvc-x86` | 32-bit games |
| `windowsloader64.exe`, `windowsloader64.dll` | `msvc-x64` | 64-bit games |

## Use

```
windowsloader [options] <game.exe> [game arguments...]

  --log <file>     log written by the game side (default: windowsloader.log)
  --fullscreen     let the game switch the display to full screen (default: kept in a window)
  --timeout <s>    stop the game after <s> seconds (for tests)
  --iolimit <n>    max reads/writes logged per device handle (default: 500)
  --quiet          do not echo the log on the console
```

In windowed mode the game also leaves the desktop alone: no display mode change, no cursor
clipping, no global keyboard hook, no shutdown or program started on its behalf.

Display settings are in `windowsloader.ini` next to the launcher, with linuxloader's `[Display]`
keys: `ASPECT_RATIO` (`ORIGINAL`, `4:3`, `16:9`, `4:3_LETTERBOX`), `DISPLAY_MODE` (`WINDOW`,
`WINDOW_STRETCH`, `FULLSCREEN`, `FULLSCREEN_STRETCH`) and `WIDTH` / `HEIGHT` (the game's render
resolution, `AUTO` by default). F11 or Alt+Enter switches between window and full screen.

**F10** pauses the game and opens the settings menu: controls (bindings captured from the keyboard,
a gamepad or a wheel, saved to `controls.ini`), display, force feedback strength and direction.

## License

GPL v3 (see `LICENSE`), by Benjamin Maurin - njz3. Code taken from other projects (MAME,
linuxloader, sm2-emu, Flycast, segatools) stays under its own license and names its origin.
MinHook (`src/minhook`) is under the BSD 2-clause license of its author, Tsuda Kageyu. Dear ImGui
(`src/imgui`, version 1.92.0, taken unchanged from sm2-emu, with its Direct3D 9, Direct3D 11 and
Win32 backends) is under the MIT license of Omar Cornut.
