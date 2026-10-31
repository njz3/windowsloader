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

// windowsloader launcher: starts a Windows arcade game suspended, injects windowsloader32.dll or
// windowsloader64.dll into it (LoadLibraryW through CreateRemoteThread), resumes it and waits.
// The game stays in its own folder and runs from there: some games (Raw Thrills) need fixed paths.

#include <windows.h>
#include <tlhelp32.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "gamelist.h"
#include "picker.h"
#include "wlshared.h"
#include "writedir.h"

#ifdef _WIN64
#define WL_SELF_BITS   64
#define WL_SELF_DLL    L"windowsloader64.dll"
#define WL_SIBLING_EXE L"windowsloader.exe"
#else
#define WL_SELF_BITS   32
#define WL_SELF_DLL    L"windowsloader32.dll"
#define WL_SIBLING_EXE L"windowsloader64.exe"
#endif

#define CMDLINE_MAX 32768

// ------------------------------------------------------------------------------
// Console output (UTF-8, also works when stdout is a pipe)
// ------------------------------------------------------------------------------

static int g_quiet;
static HANDLE g_msgOut; // our messages and the game's log: stdout, or stderr in agent mode (stdout is the protocol's)

static void out_raw(const char *s, DWORD len)
{
    DWORD written;
    if (!g_msgOut)
        g_msgOut = GetStdHandle(STD_OUTPUT_HANDLE);
    WriteFile(g_msgOut, s, len, &written, NULL);
}

static void say(const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n >= (int)sizeof buf)
        n = sizeof buf - 1;
    out_raw(buf, (DWORD)n);
}

// UTF-16 to UTF-8 for messages; a few rotating buffers so one say() can use several
static const char *u8(const wchar_t *w)
{
    static char bufs[4][MAX_PATH * 4];
    static int next;
    char *b = bufs[next++ & 3];
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, b, (int)sizeof bufs[0], NULL, NULL))
        b[0] = 0;
    return b;
}

static void say_error(const char *what)
{
    DWORD err = GetLastError();
    char msg[512] = "";
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, msg, sizeof msg, NULL);
    size_t len = strlen(msg);
    while (len && (msg[len - 1] == '\n' || msg[len - 1] == '\r'))
        msg[--len] = 0;
    say("windowsloader: %s failed (error %lu: %s)\n", what, err, msg);
}

static void usage(void)
{
    say("windowsloader " WL_BUILD_VERSION " " WL_EDITION " - runs Windows arcade games with emulated cabinet hardware\n"
        "\n"
        "Usage: %s [options] <game.exe> [game arguments...]\n"
        "       %s alone opens the game picker (the games of the games folder, see README.md)\n"
        "\n"
        "Options:\n"
        "  --log <file>     log written by the game side (default: windowsloader.log in the current folder)\n"
        "  --fullscreen     let the game switch the display to full screen (default: kept in a window)\n"
        "  --timeout <s>    stop the game after <s> seconds (for tests)\n"
        "  --iolimit <n>    max reads/writes logged per device handle (default: 500)\n"
        "  --quiet          do not echo the log on this console\n"
        "  --agent          drive the game over stdin/stdout (linuxloader's agent protocol); messages go to stderr\n"
        "  --controllers    with --agent: the controllers are read and get the force feedback too (opens vJoy)\n"
        "  -c, --config <file>  settings file (default: windowsloader.ini next to this launcher, or the game's\n"
        "                   <short name>.ini beside it when there is one)\n"
        "  --set SECTION.KEY=VALUE  overrides one setting of the file, e.g. --set Display.ASPECT_RATIO=4:3_CROP\n"
        "                   (repeatable)\n"
        "  --write-dir <folder>  everything written goes there, the launcher's and the game's folders only\n"
        "                   read (default: [Paths] WRITE_DIR of the settings file, none when empty)\n"
        "  --list-games     the games windowsloader knows, one line per program: the game, its short name,\n"
        "                   its version, its force feedback and link between cabinets (yes, partly, no),\n"
        "                   the program, the program's CRC32 in the cabinets' copies (for\n"
        "                   information: a program with another CRC32 is played all the same)\n"
        "  -h, --help       this help\n"
        "\n"
        "A 32-bit game needs windowsloader.exe + windowsloader32.dll, a 64-bit game windowsloader64.exe +\n"
        "windowsloader64.dll; each launcher hands the other kind of game to its sibling in the same folder.\n",
        WL_SELF_BITS == 64 ? "windowsloader64" : "windowsloader", WL_SELF_BITS == 64 ? "windowsloader64" : "windowsloader");
}

// --list-games: the games of the table (gamelist.cpp), one line per program it takes: the game, its
// short name (its settings' [Game.<name>], its WRITE_DIR\<name>), its version ("-" when the copies
// do not tell it), how far windowsloader plays its force feedback and its link between cabinets
// (yes, partly, no: BILAN.md), the program, its CRC-32 ("--------" when not known). The CRC-32 is that
// of the cabinets' copies, for information: a program with another one is recognized and played the same
static const char *feature_text(int feature)
{
    return feature == WL_FEATURE_YES ? "yes" : feature == WL_FEATURE_PARTLY ? "partly" : "no";
}

static void list_games(void)
{
    // The columns as wide as their longest text
    int wTitle = 4, wName = 4, wVersion = 7, wProgram = 7;
    for (int i = 0; i < g_wlKnownGameCount; i++)
    {
        const WlKnownGame *g = &g_wlKnownGames[i];
        int n;
        if ((n = (int)strlen(g->title)) > wTitle)
            wTitle = n;
        if ((n = (int)strlen(g->name)) > wName)
            wName = n;
        if ((n = (int)strlen(g->version)) > wVersion)
            wVersion = n;
        for (int k = 0; k < 3 && g->exes[k]; k++)
            if ((n = (int)wcslen(g->exes[k])) > wProgram)
                wProgram = n;
    }
    say("%-*s  %-*s  %-*s  %-6s  %-6s  %-*s  %s\n", wTitle, "Game", wName, "Name", wVersion, "Version", "FFB", "Link", wProgram, "Program",
        "CRC32");
    for (int i = 0; i < g_wlKnownGameCount; i++)
    {
        const WlKnownGame *g = &g_wlKnownGames[i];
        for (int k = 0; k < 3 && g->exes[k]; k++)
        {
            char crc[16] = "--------";
            if (g->crc32[k])
                snprintf(crc, sizeof crc, "%08lX", (unsigned long)g->crc32[k]);
            say("%-*s  %-*s  %-*s  %-6s  %-6s  %-*s  %s\n", wTitle, g->title, wName, g->name, wVersion, g->version[0] ? g->version : "-",
                feature_text(g->ffb), feature_text(g->link), wProgram, u8(g->exes[k]), crc);
        }
    }
}

// ------------------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------------------

// 32 or 64 for an x86 / x64 PE file, 0 for anything else, -1 when it cannot be read
static int pe_bits(const wchar_t *path)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return -1;

    int bits = 0;
    IMAGE_DOS_HEADER dos;
    IMAGE_FILE_HEADER fh;
    DWORD sig, rd;
    if (ReadFile(f, &dos, sizeof dos, &rd, NULL) && rd == sizeof dos && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        SetFilePointer(f, dos.e_lfanew, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
        ReadFile(f, &sig, sizeof sig, &rd, NULL) && rd == sizeof sig && sig == IMAGE_NT_SIGNATURE &&
        ReadFile(f, &fh, sizeof fh, &rd, NULL) && rd == sizeof fh)
    {
        if (fh.Machine == IMAGE_FILE_MACHINE_I386)
            bits = 32;
        else if (fh.Machine == IMAGE_FILE_MACHINE_AMD64)
            bits = 64;
    }
    CloseHandle(f);
    return bits;
}

// A console program (the PE's subsystem 3, IMAGE_SUBSYSTEM_WINDOWS_CUI): Storm Racer G's racing.exe
static int pe_console(const wchar_t *path)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    IMAGE_DOS_HEADER dos;
    // the signature (4), the file header (20), then the optional header's subsystem at 68
    unsigned char nt[4 + sizeof(IMAGE_FILE_HEADER) + 70];
    DWORD rd;
    int console = 0;
    if (ReadFile(f, &dos, sizeof dos, &rd, NULL) && rd == sizeof dos && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        SetFilePointer(f, dos.e_lfanew, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER && ReadFile(f, nt, sizeof nt, &rd, NULL) &&
        rd == sizeof nt)
        console = *(WORD *)(nt + 4 + sizeof(IMAGE_FILE_HEADER) + 68) == IMAGE_SUBSYSTEM_WINDOWS_CUI;
    CloseHandle(f);
    return console;
}

// Strips the file name: "C:\a\b.exe" -> "C:\a"
static void parent_dir(wchar_t *path)
{
    wchar_t *slash = wcsrchr(path, L'\\');
    if (slash)
        *slash = 0;
}

static void append_chars(wchar_t *cmd, size_t cap, wchar_t c, size_t count)
{
    size_t len = wcslen(cmd);
    while (count-- && len + 1 < cap)
        cmd[len++] = c;
    cmd[len] = 0;
}

// Appends one argument, quoted so that CommandLineToArgvW and the CRT read it back unchanged
static void append_arg(wchar_t *cmd, size_t cap, const wchar_t *arg)
{
    if (cmd[0])
        append_chars(cmd, cap, L' ', 1);
    if (arg[0] && !wcspbrk(arg, L" \t\n\v\""))
    {
        wcscat_s(cmd, cap, arg);
        return;
    }
    append_chars(cmd, cap, L'"', 1);
    for (const wchar_t *p = arg;; p++)
    {
        size_t backslashes = 0;
        while (*p == L'\\')
        {
            p++;
            backslashes++;
        }
        if (*p == 0)
        {
            append_chars(cmd, cap, L'\\', backslashes * 2);
            break;
        }
        if (*p == L'"')
        {
            append_chars(cmd, cap, L'\\', backslashes * 2 + 1);
            append_chars(cmd, cap, L'"', 1);
        }
        else
        {
            append_chars(cmd, cap, L'\\', backslashes);
            append_chars(cmd, cap, *p, 1);
        }
    }
    append_chars(cmd, cap, L'"', 1);
}

// The raw command line without the program name, to forward it untouched
static const wchar_t *skip_argv0(const wchar_t *cl)
{
    if (*cl == L'"')
    {
        cl++;
        while (*cl && *cl != L'"')
            cl++;
        if (*cl)
            cl++;
    }
    else
    {
        while (*cl && *cl != L' ' && *cl != L'\t')
            cl++;
    }
    while (*cl == L' ' || *cl == L'\t')
        cl++;
    return cl;
}

static int wait_child(HANDLE process)
{
    DWORD code = 1;
    WaitForSingleObject(process, INFINITE);
    GetExitCodeProcess(process, &code);
    return (int)code;
}

// Hands a game of the other bitness to the sibling launcher, with the same arguments
static int run_sibling(const wchar_t *selfDir, int gameBits)
{
    wchar_t exe[MAX_PATH];
    swprintf(exe, MAX_PATH, L"%s\\%s", selfDir, WL_SIBLING_EXE);
    if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES)
    {
        say("windowsloader: this is a %d-bit game and %s is missing next to this launcher.\n", gameBits, u8(WL_SIBLING_EXE));
        return 1;
    }

    static wchar_t cmd[CMDLINE_MAX];
    swprintf(cmd, CMDLINE_MAX, L"\"%s\" %s", exe, skip_argv0(GetCommandLineW()));

    STARTUPINFOW si = {sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        say_error("starting the sibling launcher");
        return 1;
    }
    CloseHandle(pi.hThread);
    int code = wait_child(pi.hProcess);
    CloseHandle(pi.hProcess);
    return code;
}

// True when a module with this file name is loaded in the process
static int remote_has_module(DWORD pid, const wchar_t *dllPath)
{
    const wchar_t *name = wcsrchr(dllPath, L'\\');
    name = name ? name + 1 : dllPath;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    int found = 0;
    MODULEENTRY32W me = {sizeof me};
    for (BOOL ok = Module32FirstW(snap, &me); ok && !found; ok = Module32NextW(snap, &me))
        found = _wcsicmp(me.szModule, name) == 0;
    CloseHandle(snap);
    return found;
}

// ------------------------------------------------------------------------------
// A port driver DLL the game imports statically (Ford Racing's INPOUT32.dll) would be loaded by
// Windows with the game's other imports, before windowsloader, and it reaches the hardware
// through a kernel driver. Before the suspended game initializes, its import is pointed at
// windowsloader's DLL (by its full path, as Detours does it), which exports the same functions
// at the same ordinals (wlhook.def). The same way, a Windows component's proxy left in the game's
// folder by another loader is replaced by Windows' own (is_system_component).
// ------------------------------------------------------------------------------

typedef LONG(NTAPI *NtQueryInformationProcessFn)(HANDLE, int, PVOID, ULONG, PULONG);

struct WlBasicInfo // PROCESS_BASIC_INFORMATION
{
    void *reserved1;
    void *peb;
    void *reserved2[2];
    ULONG_PTR pid;
    void *reserved3;
};

static int read_at(HANDLE p, ULONG_PTR addr, void *out, SIZE_T size)
{
    SIZE_T got = 0;
    return ReadProcessMemory(p, (LPCVOID)addr, out, size, &got) && got == size;
}

static int is_port_driver(const char *name)
{
    return !_stricmp(name, "inpout32.dll") || !_stricmp(name, "inpout32_d.dll") || !_stricmp(name, "inpoutx64.dll");
}

// A Windows component found in the game's folder is another loader's proxy (FFBArcadePlugin's
// dinput8.dll next to Wasteland Racers 2071, which opens the wheel itself): the game's import of it
// is pointed at Windows' own, by its full path
static int is_system_component(const char *name)
{
    static const char *names[] = {"dinput8.dll", "dinput.dll", "d3d8.dll", "d3d9.dll", "d3d11.dll", "dxgi.dll", "ddraw.dll",
                                  "opengl32.dll", "winmm.dll", "dsound.dll", "version.dll", "xinput1_3.dll", "xinput1_4.dll"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!_stricmp(name, names[i]))
            return 1;
    return 0;
}

// Memory for the new name within 4 GB above the image, an RVA's reach
static ULONG_PTR alloc_after(HANDLE p, ULONG_PTR base, DWORD imageSize, SIZE_T size)
{
    for (ULONG_PTR at = (base + imageSize + 0xFFFF) & ~(ULONG_PTR)0xFFFF; at - base < 0x7FFF0000; at += 0x10000)
    {
        void *mem = VirtualAllocEx(p, (LPVOID)at, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (mem)
            return (ULONG_PTR)mem;
    }
    return 0;
}

static void redirect_imports(const PROCESS_INFORMATION *pi, const wchar_t *dll, const wchar_t *gameDir)
{
    NtQueryInformationProcessFn query = (NtQueryInformationProcessFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");
    WlBasicInfo bi;
    ULONG_PTR base = 0;
    if (!query || query(pi->hProcess, 0 /* ProcessBasicInformation */, &bi, sizeof bi, NULL) < 0 ||
        !read_at(pi->hProcess, (ULONG_PTR)bi.peb + 2 * sizeof(void *), &base, sizeof base)) // PEB.ImageBaseAddress
        return;
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS nt; // the launcher has the game's bitness
    if (!read_at(pi->hProcess, base, &dos, sizeof dos) || !read_at(pi->hProcess, base + dos.e_lfanew, &nt, sizeof nt))
        return;
    const IMAGE_DATA_DIRECTORY &dir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (DWORD at = dir.VirtualAddress; dir.VirtualAddress && at + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= dir.VirtualAddress + dir.Size;
         at += sizeof(IMAGE_IMPORT_DESCRIPTOR))
    {
        IMAGE_IMPORT_DESCRIPTOR d;
        char name[64] = "";
        if (!read_at(pi->hProcess, base + at, &d, sizeof d) || !d.Name)
            break;
        read_at(pi->hProcess, base + d.Name, name, sizeof name - 1);
        // The replacement: windowsloader's DLL for a port driver, Windows' own for a component
        // whose proxy is in the game's folder
        wchar_t to[MAX_PATH], wname[64], local[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, name, -1, wname, 64);
        swprintf(local, MAX_PATH, L"%s\\%s", gameDir, wname);
        if (is_port_driver(name))
            wcscpy_s(to, MAX_PATH, dll);
        else if (is_system_component(name) && GetFileAttributesW(local) != INVALID_FILE_ATTRIBUTES && GetSystemDirectoryW(to, MAX_PATH))
        {
            swprintf(to + wcslen(to), MAX_PATH - wcslen(to), L"\\%s", wname);
            if (GetFileAttributesW(to) == INVALID_FILE_ATTRIBUTES)
                continue;
        }
        else
            continue;
        char path[MAX_PATH * 2];
        int n = WideCharToMultiByte(CP_ACP, 0, to, -1, path, sizeof path, NULL, NULL);
        ULONG_PTR mem = n > 0 ? alloc_after(pi->hProcess, base, nt.OptionalHeader.SizeOfImage, (SIZE_T)n) : 0;
        DWORD old;
        if (!mem || !WriteProcessMemory(pi->hProcess, (LPVOID)mem, path, (SIZE_T)n, NULL) ||
            !VirtualProtectEx(pi->hProcess, (LPVOID)(base + at), sizeof d, PAGE_READWRITE, &old))
        {
            say("windowsloader: could not keep the game's %s out\n", name);
            continue;
        }
        d.Name = (DWORD)(mem - base);
        d.TimeDateStamp = 0; // not bound: resolved from the replacement's exports
        WriteProcessMemory(pi->hProcess, (LPVOID)(base + at), &d, sizeof d, NULL);
        VirtualProtectEx(pi->hProcess, (LPVOID)(base + at), sizeof d, old, &old);
        if (is_port_driver(name))
            say("  ports:  %s, imported by the game, replaced by windowsloader's own\n", name);
        else
            say("  import: %s of the game's folder (another loader's) left out, Windows' own loaded\n", name);
    }
}

// LoadLibraryW(dll) in the suspended game. The remote thread also runs the process
// initialization (static imports, TLS callbacks) before our DLL, so the game's own imports
// are already loaded when DllMain installs the hooks.
static int inject(const PROCESS_INFORMATION *pi, const wchar_t *dll)
{
    size_t size = (wcslen(dll) + 1) * sizeof(wchar_t);
    void *remote = VirtualAllocEx(pi->hProcess, NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote)
    {
        say_error("VirtualAllocEx");
        return 0;
    }
    if (!WriteProcessMemory(pi->hProcess, remote, dll, size, NULL))
    {
        say_error("WriteProcessMemory");
        return 0;
    }

    LPTHREAD_START_ROUTINE loadLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE thread = CreateRemoteThread(pi->hProcess, NULL, 0, loadLibrary, remote, 0, NULL);
    if (!thread)
    {
        say_error("CreateRemoteThread");
        return 0;
    }

    HANDLE waits[2] = {thread, pi->hProcess};
    DWORD w = WaitForMultipleObjects(2, waits, FALSE, 60000);
    DWORD module = 0;
    GetExitCodeThread(thread, &module);
    CloseHandle(thread);

    if (w == WAIT_OBJECT_0 + 1)
    {
        DWORD code = 0;
        GetExitCodeProcess(pi->hProcess, &code);
        say("windowsloader: the game ended while starting (exit code 0x%08lX). 0xC0000135 means a missing DLL.\n", code);
        return 0;
    }
    if (w != WAIT_OBJECT_0)
    {
        say("windowsloader: the DLL did not load within 60 s.\n");
        return 0;
    }
    VirtualFreeEx(pi->hProcess, remote, 0, MEM_RELEASE);

    // The thread exit code is the low 32 bits of the module handle: 0 is an error,
    // except for a 64-bit base that happens to end in 32 zero bits.
    if (module == 0 && !remote_has_module(pi->dwProcessId, dll))
    {
        say("windowsloader: LoadLibraryW(%s) failed inside the game.\n", u8(dll));
        return 0;
    }
    return 1;
}

// A program the game needs beside it, started first under windowsloader too: an ALLS's
// amdaemon.exe, the platform's service, which the cabinet's script (App\Todoroki\StartApp.bat)
// starts from App\Todoroki 5 s before the game (Sega World Drivers Championship). Here a cabinet
// alone: amdaemon with the game's config.json and windowsloader's settings for a cabinet alone,
// which its module writes in the temporary folder ({temp}: alls.cpp); the game's -launch= from
// its option CABINET (g_gameOptionArgs). Initial D 6, 7 and 8's picodaemon.exe, the card
// reader-writer's daemon, which game.bat starts just before the game. Initial D Zero's two, in the
// table's order: amdaemon, then the shop's server box. The folders are the game's folder's parents.
static const struct
{
    const wchar_t *game; // the game's file name
    int up;              // its folder's parents up to the companion's folder (its working folder)
    const wchar_t *exe;  // from there
    const wchar_t *args; // {temp}: the temporary folder
    DWORD waitMs;        // before the game starts
} g_companions[] = {
    {L"Todoroki-Win64-Shipping.exe", 4, L"WindowsNoEditor\\AMDaemon\\amdaemon.exe",
     L"-c WindowsNoEditor\\AMDaemon\\config.json -c \"{temp}windowsloader-swdc-amdaemon.json\" -l WindowsNoEditor\\AMDaemon\\amdaemon.log",
     5000},
    // Initial D Arcade Stage Zero (Sega Nu): the export cabinet's settings, the first cabinet's
    // (the DIP switch alls.cpp gives), then windowsloader's for a cabinet alone
    {L"InitialD0_DX11_Nu.exe", 0, L"amdaemon.exe",
     L"-c configDHCP_Final_Common.json configDHCP_Final_EX.json configDHCP_Final_EX_ST1.json -c \"{temp}windowsloader-idz-amdaemon.json\"", 5000},
    // and the shop's server box, the cabinets' link to the game's online server (alls.cpp plays
    // that server on this PC)
    {L"InitialD0_DX11_Nu.exe", 0, L"ServerBoxD8_Nu_x64.exe", L"", 3000},
    // Initial D 6's game.bat starts it too: without it, error 5101 (417) at "do you have a licence?"
    {L"id6_dump_.exe", 0, L"picodaemon.exe", L"", 1000},
    {L"InitialD6_GLW_RE_SBUU.exe", 0, L"picodaemon.exe", L"", 1000},
    {L"InitialD7_GLW_RE_SBYD_dumped_.exe", 0, L"picodaemon.exe", L"", 1000},
    {L"InitialD7_GLW_RE_SBYD.exe", 0, L"picodaemon.exe", L"", 1000},
    {L"InitialD8_GLW_RE_SBZZ_dumped_.exe", 0, L"picodaemon.exe", L"", 1000},
    {L"InitialD8_GLW_RE_SBZZ_redumped_.exe", 0, L"picodaemon.exe", L"", 1000},
    {L"InitialD8_GLW_RE_SBZZ_redumped4k_.exe", 0, L"picodaemon.exe", L"", 1000},
};

// A game's option that gives it an argument, taken as the game starts: [Game.<short name>] KEY of
// the settings file (or of <short name>.ini beside windowsloader.ini, as the DLL reads it), or
// --set Game.KEY=VALUE; the game's module declares it for the settings menu. Each value its
// argument, the first one the default; nothing added when the command line has an argument of
// that name already (-launch= for -launch=Cabinet)
static const struct
{
    const wchar_t *game;         // the game's file name
    const wchar_t *name, *key;   // its short name, the option
    const wchar_t *value, *arg;  // a value and its argument
} g_gameOptionArgs[] = {
    // Sega World Drivers Championship (alls.cpp): the full cabinet, its dashboard (the wheel's
    // buttons, the speed) and its LED board; or the mini cabinet, as TeknoParrot runs it
    {L"Todoroki-Win64-Shipping.exe", L"swdc", L"CABINET", L"CABINET", L"-launch=Cabinet"},
    {L"Todoroki-Win64-Shipping.exe", L"swdc", L"CABINET", L"MINI", L"-launch=MiniCabinet"},
};

// The option's value: --set's last, else the file's, else def
static void game_option(const wchar_t *name, const wchar_t *key, const wchar_t *def, const wchar_t *iniPath, const wchar_t *sets,
                        wchar_t *out, size_t size)
{
    wcsncpy_s(out, size, def, _TRUNCATE);
    wchar_t file[MAX_PATH], section[64];
    wcsncpy_s(file, MAX_PATH, iniPath, _TRUNCATE);
    wchar_t *base = wcsrchr(file, L'\\');
    if (base && !_wcsicmp(base + 1, L"windowsloader.ini"))
    {
        swprintf(base + 1, MAX_PATH - (base + 1 - file), L"%s.ini", name);
        if (GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES)
            wcsncpy_s(file, MAX_PATH, iniPath, _TRUNCATE);
    }
    swprintf(section, 64, L"Game.%s", name);
    GetPrivateProfileStringW(section, key, def, out, (DWORD)size, file);
    for (const wchar_t *line = sets; line && *line;)
    {
        const wchar_t *end = wcschr(line, L'\n');
        size_t n = end ? (size_t)(end - line) : wcslen(line);
        size_t k = wcslen(key);
        if (n > 5 + k && !_wcsnicmp(line, L"Game.", 5) && !_wcsnicmp(line + 5, key, k) && line[5 + k] == L'=')
            swprintf(out, size, L"%.*s", (int)(n - 6 - k), line + 6 + k);
        line = end ? end + 1 : NULL;
    }
}

// The interface's language (lang.h): [Launcher] LANGUAGE of the settings file, else AUTO, or
// --set Launcher.LANGUAGE=; handed to the DLL
static void language_setting(const wchar_t *iniPath, const wchar_t *sets, wchar_t *out, size_t size)
{
    GetPrivateProfileStringW(L"Launcher", L"LANGUAGE", L"AUTO", out, (DWORD)size, iniPath);
    for (const wchar_t *line = sets; line && *line;)
    {
        const wchar_t *end = wcschr(line, L'\n');
        size_t n = end ? (size_t)(end - line) : wcslen(line);
        if (n >= 18 && !_wcsnicmp(line, L"Launcher.LANGUAGE=", 18))
            swprintf(out, size, L"%.*s", (int)(n - 18), line + 18);
        line = end ? end + 1 : NULL;
    }
}

// The arguments the game's options give it (g_gameOptionArgs), added to its command line
static void game_option_args(const wchar_t *gamePath, wchar_t *cmd, int argc, wchar_t **argv, int first, const wchar_t *iniPath,
                             const wchar_t *sets)
{
    const wchar_t *base = wcsrchr(gamePath, L'\\');
    base = base ? base + 1 : gamePath;
    for (int i = 0; i < (int)(sizeof g_gameOptionArgs / sizeof g_gameOptionArgs[0]); i++)
    {
        // an option's first line only: its value, then its argument among its lines
        if (_wcsicmp(base, g_gameOptionArgs[i].game) || (i && !_wcsicmp(g_gameOptionArgs[i - 1].game, base) &&
                                                         !_wcsicmp(g_gameOptionArgs[i - 1].key, g_gameOptionArgs[i].key)))
            continue;
        wchar_t value[64];
        game_option(g_gameOptionArgs[i].name, g_gameOptionArgs[i].key, g_gameOptionArgs[i].value, iniPath, sets, value, 64);
        const wchar_t *arg = g_gameOptionArgs[i].arg;
        for (int j = i; j < (int)(sizeof g_gameOptionArgs / sizeof g_gameOptionArgs[0]) && !_wcsicmp(g_gameOptionArgs[j].game, base) &&
                        !_wcsicmp(g_gameOptionArgs[j].key, g_gameOptionArgs[i].key);
             j++)
            if (!_wcsicmp(value, g_gameOptionArgs[j].value))
                arg = g_gameOptionArgs[j].arg;
        const wchar_t *eq = wcschr(arg, L'=');
        size_t named = eq ? (size_t)(eq - arg) + 1 : wcslen(arg);
        int given = 0;
        for (int a = first; a < argc; a++)
            given |= !_wcsnicmp(argv[a], arg, named);
        if (!given)
            append_arg(cmd, CMDLINE_MAX, arg);
    }
}

// The game's next companion from the table's entry *next on, in the table's order (*next then
// past it); 0 when there are no more
#define COMPANIONS 2
static int companion_of(const wchar_t *gamePath, const wchar_t *gameDir, int *next, wchar_t *exe, wchar_t *dir, wchar_t *cmd, DWORD *waitMs)
{
    const wchar_t *base = wcsrchr(gamePath, L'\\');
    base = base ? base + 1 : gamePath;
    for (int i = *next; i < (int)(sizeof g_companions / sizeof g_companions[0]); i++)
    {
        if (_wcsicmp(base, g_companions[i].game))
            continue;
        wcsncpy_s(dir, MAX_PATH, gameDir, _TRUNCATE);
        for (int k = 0; k < g_companions[i].up; k++)
            parent_dir(dir);
        swprintf(exe, MAX_PATH, L"%s\\%s", dir, g_companions[i].exe);
        if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES)
            continue;
        *next = i + 1;
        cmd[0] = 0;
        append_arg(cmd, CMDLINE_MAX, exe);
        size_t n = wcslen(cmd);
        cmd[n++] = L' ';
        wchar_t temp[MAX_PATH] = L"";
        GetTempPathW(MAX_PATH, temp);
        for (const wchar_t *a = g_companions[i].args; *a && n + MAX_PATH < CMDLINE_MAX;)
            if (!wcsncmp(a, L"{temp}", 6))
            {
                wcscpy_s(cmd + n, CMDLINE_MAX - n, temp);
                n += wcslen(temp);
                a += 6;
            }
            else
                cmd[n++] = *a++;
        cmd[n] = 0;
        // WINDOWSLOADER_COMPANION_ARGS: more of them, for tests (another amdaemon config)
        wchar_t more[1024];
        if (GetEnvironmentVariableW(L"WINDOWSLOADER_COMPANION_ARGS", more, 1024) && more[0])
        {
            n = wcslen(cmd);
            swprintf(cmd + n, CMDLINE_MAX - n, L" %s", more);
        }
        *waitMs = g_companions[i].waitMs;
        return 1;
    }
    return 0;
}

// Copies what the DLL appended to the log since the last call to our console
static void echo_log(HANDLE *log, const wchar_t *path)
{
    if (g_quiet)
        return;
    if (*log == INVALID_HANDLE_VALUE)
    {
        *log = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
        if (*log == INVALID_HANDLE_VALUE)
            return;
    }
    char buf[8192];
    DWORD rd;
    while (ReadFile(*log, buf, sizeof buf, &rd, NULL) && rd)
        out_raw(buf, rd);
}

// ------------------------------------------------------------------------------
// Agent mode: stdin goes to the DLL's pipe, its answers to stdout
// ------------------------------------------------------------------------------

static HANDLE connect_agent(HANDLE process)
{
    wchar_t name[64];
    swprintf(name, 64, WL_AGENT_PIPE, GetProcessId(process));
    ULONGLONG start = GetTickCount64();
    while (GetTickCount64() - start < 15000 && WaitForSingleObject(process, 0) == WAIT_TIMEOUT)
    {
        HANDLE pipe = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (pipe != INVALID_HANDLE_VALUE)
            return pipe;
        WaitNamedPipeW(name, 200);
    }
    say("windowsloader: no agent pipe from the game.\n");
    return INVALID_HANDLE_VALUE;
}

static DWORD WINAPI stdin_to_agent(LPVOID param)
{
    HANDLE pipe = (HANDLE)param;
    char buf[4096];
    DWORD rd, written;
    while (ReadFile(GetStdHandle(STD_INPUT_HANDLE), buf, sizeof buf, &rd, NULL) && rd)
        if (!WriteFile(pipe, buf, rd, &written, NULL))
            break;
    return 0;
}

// Polled rather than read by a thread: both directions share one synchronous handle, and a
// blocking read would hold the writes back
static void agent_to_stdout(HANDLE pipe)
{
    DWORD avail = 0, rd, written;
    char buf[8192];
    while (PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL) && avail)
    {
        if (!ReadFile(pipe, buf, avail < sizeof buf ? avail : sizeof buf, &rd, NULL) || !rd)
            break;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, rd, &written, NULL);
    }
}

// ------------------------------------------------------------------------------
// Main
// ------------------------------------------------------------------------------

int wmain(int argc, wchar_t **argv)
{
    // Without a game: the picker, which runs the chosen game through this program again
    if (argc == 1)
        return wl_picker_run();

    UINT oldCP = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);

    const wchar_t *logArg = L"windowsloader.log";
    int logGiven = 0;
    const wchar_t *writeArg = NULL;
    int windowed = 1;
    int timeoutSec = 0;
    int ioLimit = 500;
    int agent = 0;
    int controllers = 0;
    const wchar_t *configArg = NULL;
    static wchar_t sets[8192]; // --set's, one per line
    size_t setsLen = 0;

    int i = 1;
    for (; i < argc && argv[i][0] == L'-'; i++)
    {
        const wchar_t *a = argv[i];
        int hasValue = i + 1 < argc;
        if (!wcscmp(a, L"-h") || !wcscmp(a, L"--help"))
        {
            usage();
            return 0;
        }
        else if (!wcscmp(a, L"--list-games"))
        {
            list_games();
            return 0;
        }
        else if (!wcscmp(a, L"--log") && hasValue)
        {
            logArg = argv[++i];
            logGiven = 1;
        }
        else if (!wcscmp(a, L"--write-dir") && hasValue)
            writeArg = argv[++i];
        else if (!wcscmp(a, L"--fullscreen"))
            windowed = 0;
        else if (!wcscmp(a, L"--timeout") && hasValue)
            timeoutSec = _wtoi(argv[++i]);
        else if (!wcscmp(a, L"--iolimit") && hasValue)
            ioLimit = _wtoi(argv[++i]);
        else if (!wcscmp(a, L"--quiet"))
            g_quiet = 1;
        else if (!wcscmp(a, L"--controllers"))
            controllers = 1;
        else if ((!wcscmp(a, L"-c") || !wcscmp(a, L"--config")) && hasValue)
            configArg = argv[++i];
        else if (!wcsncmp(a, L"--set=", 6) || (!wcscmp(a, L"--set") && hasValue))
        {
            const wchar_t *value = a[5] == L'=' ? a + 6 : argv[++i];
            size_t n = wcslen(value);
            if (!wcschr(value, L'=') || !wcschr(value, L'.') || wcschr(value, L'\n') || setsLen + n + 2 > sizeof sets / sizeof sets[0])
            {
                say("windowsloader: --set %s: expected SECTION.KEY=VALUE\n", u8(value));
                return 1;
            }
            swprintf(sets + setsLen, sizeof sets / sizeof sets[0] - setsLen, L"%s%s", setsLen ? L"\n" : L"", value);
            setsLen = wcslen(sets);
        }
        else if (!wcscmp(a, L"--agent"))
        {
            agent = 1;
            g_msgOut = GetStdHandle(STD_ERROR_HANDLE);
        }
        else
        {
            say("windowsloader: unknown option or missing value: %s\n\n", u8(a));
            usage();
            return 1;
        }
    }
    if (i >= argc)
    {
        usage();
        return 1;
    }

    wchar_t gamePath[MAX_PATH], gameDir[MAX_PATH], logPath[MAX_PATH], selfDir[MAX_PATH], dllPath[MAX_PATH];
    if (!GetFullPathNameW(argv[i], MAX_PATH, gamePath, NULL) || !GetFullPathNameW(logArg, MAX_PATH, logPath, NULL))
    {
        say_error("GetFullPathName");
        return 1;
    }
    wcscpy_s(gameDir, MAX_PATH, gamePath);
    parent_dir(gameDir);
    GetModuleFileNameW(NULL, selfDir, MAX_PATH);
    parent_dir(selfDir);

    int bits = pe_bits(gamePath);
    if (bits < 0)
    {
        say("windowsloader: cannot open %s\n", u8(gamePath));
        return 1;
    }
    if (bits == 0)
    {
        say("windowsloader: %s is not an x86 or x64 Windows program.\n", u8(gamePath));
        return 1;
    }
    if (bits != WL_SELF_BITS)
        return run_sibling(selfDir, bits);

    swprintf(dllPath, MAX_PATH, L"%s\\%s", selfDir, WL_SELF_DLL);
    if (GetFileAttributesW(dllPath) == INVALID_FILE_ATTRIBUTES)
    {
        say("windowsloader: %s is missing next to this launcher.\n", u8(dllPath));
        return 1;
    }

    // Settings for the DLL, inherited by the game
    wchar_t num[16];
    wchar_t controlsPath[MAX_PATH];
    swprintf(controlsPath, MAX_PATH, L"%s\\controls.ini", selfDir);
    wchar_t iniPath[MAX_PATH];
    swprintf(iniPath, MAX_PATH, L"%s\\windowsloader.ini", selfDir);
    if (configArg && !GetFullPathNameW(configArg, MAX_PATH, iniPath, NULL))
    {
        say_error("GetFullPathName");
        return 1;
    }
    // The write folder (--write-dir, else [Paths] WRITE_DIR, writedir.h): the settings files and the
    // log at its root, the game's writes in WRITE_DIR\<game> (the DLL's)
    wchar_t writeDir[MAX_PATH] = L"";
    int write = wl_write_dir_root(selfDir, writeArg, iniPath, writeDir, MAX_PATH);
    // A file of the launcher's folder changed after its copy was made (a hand edit on a cabinet):
    // the copy is still the one read; said here, in the game's log and in its menu (the DLL's)
    static wchar_t older[4 * MAX_PATH + 8];
    older[0] = 0;
    if (write > 0)
    {
        wchar_t *paths[2] = {iniPath, controlsPath};
        const wchar_t *names[2] = {L"windowsloader.ini", L"controls.ini"};
        wchar_t first[2][MAX_PATH];
        for (int f = 0; f < 2 && write > 0; f++)
        {
            wcscpy_s(first[f], MAX_PATH, paths[f]);
            int r = wl_write_dir_file(writeDir, names[f], paths[f], MAX_PATH);
            if (r < 0)
            {
                // Not copied there: the folder is not used, the files read in their first place
                write = -3;
                for (int g = 0; g < f; g++)
                    wcscpy_s(paths[g], MAX_PATH, first[g]);
                older[0] = 0;
            }
            if (r <= 0)
                continue;
            say("windowsloader: %s, the one read, is older than %s: the changes made there since are not read (copy that file "
                "over it, or change the one in the write folder)\n", u8(paths[f]), u8(first[f]));
            size_t n = wcslen(older);
            swprintf(older + n, sizeof older / sizeof older[0] - n, L"%s|%s\n", paths[f], first[f]);
        }
        if (write > 0 && !logGiven)
            swprintf(logPath, MAX_PATH, L"%s\\windowsloader.log", writeDir);
    }
    // Not usable: nothing written anywhere, the usual places only read (writedir.h); no log but
    // --log's, the reason on this console and the debugger's output (the DLL's)
    if (write < 0)
    {
        say("windowsloader: the write folder %s is not used: %s (error %lu); nothing is saved: the settings and the game's saves "
            "are read in their usual places and not written, no log\n",
            u8(writeDir), write == -1 ? "it cannot be made" : write == -2 ? "a file cannot be written in it" : "a settings file cannot be copied there",
            wl_write_dir_error());
        if (!logGiven)
            logPath[0] = 0;
    }
    SetEnvironmentVariableW(WL_ENV_OLDER, older[0] ? older : NULL);
    SetEnvironmentVariableW(WL_ENV_WRITE_DIR, write ? writeDir : NULL);
    SetEnvironmentVariableW(WL_ENV_WRITE_DENIED, write < 0 ? L"1" : NULL);
    SetEnvironmentVariableW(WL_ENV_LOG, logPath);
    SetEnvironmentVariableW(WL_ENV_WINDOWED, windowed ? L"1" : L"0");
    swprintf(num, 16, L"%d", ioLimit);
    SetEnvironmentVariableW(WL_ENV_IOLIMIT, num);
    SetEnvironmentVariableW(WL_ENV_AGENT, agent ? L"1" : NULL);
    SetEnvironmentVariableW(WL_ENV_CONTROLLERS, controllers ? L"1" : NULL);
    SetEnvironmentVariableW(WL_ENV_CONTROLS, controlsPath);
    SetEnvironmentVariableW(WL_ENV_INI, iniPath);
    SetEnvironmentVariableW(WL_ENV_SET, setsLen ? sets : NULL);
    wchar_t language[16];
    language_setting(iniPath, setsLen ? sets : NULL, language, 16);
    SetEnvironmentVariableW(WL_ENV_LANGUAGE, language);
    // On a laptop with two graphics cards, an OpenGL game gets the dedicated NVIDIA one through the
    // Optimus shim's per-process switch (Direct3D games are given it by the DLL);
    // WINDOWSLOADER_GPU=default keeps Windows' choice
    wchar_t gpu[16] = L"";
    GetEnvironmentVariableW(L"WINDOWSLOADER_GPU", gpu, 16);
    if (_wcsicmp(gpu, L"default"))
        SetEnvironmentVariableW(L"SHIM_MCCOMPAT", L"0x800000001");

    static wchar_t cmd[CMDLINE_MAX];
    cmd[0] = 0;
    append_arg(cmd, CMDLINE_MAX, gamePath);
    for (int a = i + 1; a < argc; a++)
        append_arg(cmd, CMDLINE_MAX, argv[a]);
    game_option_args(gamePath, cmd, argc, argv, i + 1, iniPath, setsLen ? sets : NULL);

    say("windowsloader " WL_BUILD_VERSION " " WL_EDITION " (%d-bit): %s\n", WL_SELF_BITS, u8(gamePath));
    say("  folder: %s\n  log:    %s\n  mode:   %s\n", u8(gameDir), logPath[0] ? u8(logPath) : "none (the write folder cannot be written)",
        windowed ? "windowed" : "full screen allowed");
    if (write > 0)
        say("  writes: %s\\<game> (the game's folder only read)\n", u8(writeDir));
    else if (write < 0)
        say("  writes: none, all refused (the game's folder only read)\n");

    // Closing the launcher (Ctrl+C, agent stop) takes the game and its children down with it
    HANDLE job = CreateJobObjectW(NULL, NULL);
    if (job)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {0};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li))
        {
            CloseHandle(job);
            job = NULL;
        }
    }

    // The companions first (companion_of): their own logs, neither controllers nor agent (the game's)
    PROCESS_INFORMATION cpi[COMPANIONS] = {};
    wchar_t compExe[MAX_PATH], compDir[MAX_PATH], compLog[MAX_PATH];
    static wchar_t compCmd[CMDLINE_MAX];
    DWORD compWait = 0;
    for (int c = 0, next = 0; c < COMPANIONS && companion_of(gamePath, gameDir, &next, compExe, compDir, compCmd, &compWait); c++)
    {
        wcsncpy_s(compLog, MAX_PATH, logPath, _TRUNCATE); // game.log: game-companion.log, game-companion2.log
        wchar_t *dot = wcsrchr(compLog, L'.'), *slash = wcsrchr(compLog, L'\\');
        if (dot && (!slash || dot > slash))
            *dot = 0;
        if (logPath[0])
            wcscat_s(compLog, MAX_PATH, c ? L"-companion2.log" : L"-companion.log");
        else
            compLog[0] = 0; // no log
        SetEnvironmentVariableW(WL_ENV_LOG, compLog);
        SetEnvironmentVariableW(WL_ENV_AGENT, NULL);
        SetEnvironmentVariableW(WL_ENV_CONTROLS, NULL);
        SetEnvironmentVariableW(WL_ENV_COMPANION_OF, gamePath); // its game: the same write folder, no network outputs
        // Its console output to NUL, without a window: ours is the agent's protocol in agent
        // mode (amdaemon prints its version). NUL is the only handle it inherits.
        SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
        HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
        SIZE_T attrSize = 0;
        InitializeProcThreadAttributeList(NULL, 1, 0, &attrSize);
        LPPROC_THREAD_ATTRIBUTE_LIST attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrSize);
        STARTUPINFOEXW csi = {{sizeof csi}};
        csi.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        csi.StartupInfo.hStdInput = csi.StartupInfo.hStdOutput = csi.StartupInfo.hStdError = nul;
        int listed = attrs && InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) &&
                     UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &nul, sizeof nul, NULL, NULL);
        csi.lpAttributeList = listed ? attrs : NULL;
        BOOL created = nul != INVALID_HANDLE_VALUE && listed &&
                       CreateProcessW(compExe, compCmd, NULL, NULL, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, NULL,
                                      compDir, &csi.StartupInfo, &cpi[c]);
        if (listed)
            DeleteProcThreadAttributeList(attrs);
        if (attrs)
            HeapFree(GetProcessHeap(), 0, attrs);
        if (nul != INVALID_HANDLE_VALUE)
            CloseHandle(nul);
        if (created)
        {
            if (job)
                AssignProcessToJobObject(job, cpi[c].hProcess);
            if (inject(&cpi[c], dllPath))
            {
                say("  with:   %s (pid %lu, log %s)\n", u8(compExe), cpi[c].dwProcessId, u8(compLog));
                ResumeThread(cpi[c].hThread);
                Sleep(compWait);
            }
            else
            {
                TerminateProcess(cpi[c].hProcess, 1);
                CloseHandle(cpi[c].hProcess);
                cpi[c].hProcess = NULL;
            }
            CloseHandle(cpi[c].hThread);
        }
        else
            say_error("CreateProcess (the companion)");
        SetEnvironmentVariableW(WL_ENV_LOG, logPath);
        SetEnvironmentVariableW(WL_ENV_AGENT, agent ? L"1" : NULL);
        SetEnvironmentVariableW(WL_ENV_CONTROLS, controlsPath);
        SetEnvironmentVariableW(WL_ENV_COMPANION_OF, NULL);
    }

    // The game's standard handles are NUL, the only handle it inherits: in agent mode ours are the
    // agent's protocol, and a console game reading its input would wait for a key on a console
    // nobody sees (Storm Racer G stood still before its first frame). A console game gets no
    // console window.
    SECURITY_ATTRIBUTES nulSa = {sizeof nulSa, NULL, TRUE};
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &nulSa, OPEN_EXISTING, 0, NULL);
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attrSize);
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrSize);
    int listed = nul != INVALID_HANDLE_VALUE && attrs && InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) &&
                 UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &nul, sizeof nul, NULL, NULL);
    STARTUPINFOEXW sie = {{sizeof sie}};
    DWORD flags = CREATE_SUSPENDED;
    if (listed)
    {
        sie.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        sie.StartupInfo.hStdInput = sie.StartupInfo.hStdOutput = sie.StartupInfo.hStdError = nul;
        sie.lpAttributeList = attrs;
        flags |= EXTENDED_STARTUPINFO_PRESENT;
    }
    if (pe_console(gamePath))
        flags |= CREATE_NO_WINDOW;
    PROCESS_INFORMATION pi;
    BOOL started = CreateProcessW(gamePath, cmd, NULL, NULL, listed, flags, NULL, gameDir, &sie.StartupInfo, &pi);
    if (listed)
        DeleteProcThreadAttributeList(attrs);
    if (attrs)
        HeapFree(GetProcessHeap(), 0, attrs);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
    if (!started)
    {
        say_error("CreateProcess");
        for (int c = 0; c < COMPANIONS; c++)
            if (cpi[c].hProcess)
                TerminateProcess(cpi[c].hProcess, 1);
        return 1;
    }
    if (job && !AssignProcessToJobObject(job, pi.hProcess))
        say_error("putting the game in a job object (it will survive the launcher)");

    redirect_imports(&pi, dllPath, gameDir);
    if (!inject(&pi, dllPath))
    {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 1;
    }

    say("  pid:    %lu\n", pi.dwProcessId);
    // The launcher's right to put a window in front (started by the program in front, or by the
    // picker that passed it on) passed on to the game, whose window comes seconds later (display.cpp)
    AllowSetForegroundWindow(pi.dwProcessId);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    HANDLE pipe = INVALID_HANDLE_VALUE;
    if (agent)
    {
        pipe = connect_agent(pi.hProcess);
        HANDLE t = pipe != INVALID_HANDLE_VALUE ? CreateThread(NULL, 0, stdin_to_agent, pipe, 0, NULL) : NULL;
        if (t)
            CloseHandle(t);
    }

    HANDLE log = INVALID_HANDLE_VALUE;
    ULONGLONG start = GetTickCount64();
    int stopped = 0;
    while (WaitForSingleObject(pi.hProcess, pipe != INVALID_HANDLE_VALUE ? 2 : 100) == WAIT_TIMEOUT)
    {
        if (pipe != INVALID_HANDLE_VALUE)
            agent_to_stdout(pipe);
        echo_log(&log, logPath);
        if (timeoutSec > 0 && GetTickCount64() - start >= (ULONGLONG)timeoutSec * 1000)
        {
            TerminateProcess(pi.hProcess, 0xDEAD);
            WaitForSingleObject(pi.hProcess, 5000);
            stopped = 1;
            break;
        }
    }
    if (pipe != INVALID_HANDLE_VALUE)
    {
        agent_to_stdout(pipe);
        CloseHandle(pipe);
    }
    echo_log(&log, logPath);
    if (log != INVALID_HANDLE_VALUE)
        CloseHandle(log);

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    for (int c = COMPANIONS - 1; c >= 0; c--)
        if (cpi[c].hProcess) // the companions end with the game, as the cabinet's script ends them
        {
            TerminateProcess(cpi[c].hProcess, 0);
            WaitForSingleObject(cpi[c].hProcess, 5000);
            CloseHandle(cpi[c].hProcess);
        }
    if (stopped)
    {
        say("windowsloader: game stopped after %d s (--timeout).\n", timeoutSec);
        code = 0; // the expected end of a timed test
    }
    else
        say("windowsloader: game exited with code %lu (0x%08lX).\n", code, code);

    if (job)
        CloseHandle(job);
    SetConsoleOutputCP(oldCP);
    return (int)code;
}
