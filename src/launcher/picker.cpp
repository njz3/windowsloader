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

// The game picker, shown when windowsloader.exe starts without a game, the way linuxloader's is
// (itself laid out as sm2-emu's): the games found in the games folder as a list, a description
// pane, Enter to play. The game runs through windowsloader.exe again, in a process of its own, and
// the picker comes back when it ends. Dear ImGui on Win32 and Direct3D 11.
//
// The games folder: [Launcher] GAMES_DIR of windowsloader.ini, else a "games" folder beside this
// program or one or two levels up (C:\Racecab\Games for C:\Racecab\Emulators\windowsloader). Each
// folder of it is a game when a program of the table (gamelist.cpp) is found in it, six levels
// down at most. A picture of the game, when its folder has one: snap, title, art or cover, or the
// folder's own name, .png or .jpg.

#include <windows.h>
#include <d3d11.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

#include "gamelist.h"
#include "lang.h"
#include "picker.h"
#include "wlshared.h"
#include "writedir.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct Entry
{
    const WlKnownGame *game;
    wchar_t folder[MAX_PATH];
    wchar_t exe[MAX_PATH];
    int depth;
    char label[512];      // in the list
    char folderName[512]; // UTF-8, as the others below
    char exeShown[MAX_PATH * 3];
    char args[256];
    ID3D11ShaderResourceView *art;
    int artW, artH, artTried;
};

static Entry *g_entries;
static int g_count;
static wchar_t g_selfDir[MAX_PATH], g_selfPath[MAX_PATH], g_ini[MAX_PATH], g_gamesDir[MAX_PATH];
static wchar_t g_logDir[MAX_PATH]; // windowsloader.log's: this program's folder, or the write folder
static int g_iniReadOnly;          // a write folder that cannot be written: g_ini only read (writedir.h)
static bool g_log = true;          // [Launcher] LOG: the games' log (writedir.h), from the next start
static int g_focusSelected = 1; // the keyboard's focus to put back on the chosen game
static ULONGLONG g_keysFrom;     // the shortcuts wait until then: Escape still held from the game does not quit
static char g_message[4096];     // shown over the picker until closed (a game that would not start)
static ID3D11Device *g_dev;
static ID3D11DeviceContext *g_ctx;
static IDXGISwapChain *g_swap;
static ID3D11RenderTargetView *g_rtv;
static UINT g_resizeW, g_resizeH;
static int g_retitle; // the language changed: the window's title too

static void u8(const wchar_t *w, char *out, int size)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, size, NULL, NULL))
        out[0] = 0;
}

// A UTF-8 text (wl_tr's) for Windows' own windows
static const wchar_t *wide(const char *text, wchar_t *out, int size)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, text, -1, out, size))
        out[0] = 0;
    return out;
}

// ------------------------------------------------------------------------------
// The games folder and its games
// ------------------------------------------------------------------------------

static int is_dir(const wchar_t *path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void find_games_dir(void)
{
    // [Launcher] GAMES_DIR, linuxloader's key; [Picker] GAMES_DIR, the release of 25/10/2026's
    wchar_t v[MAX_PATH] = L"";
    GetPrivateProfileStringW(L"Launcher", L"GAMES_DIR", L"", v, MAX_PATH, g_ini);
    if (!v[0])
        GetPrivateProfileStringW(L"Picker", L"GAMES_DIR", L"", v, MAX_PATH, g_ini);
    if (v[0])
    {
        wchar_t joined[MAX_PATH];
        if (v[1] == L':' || (v[0] == L'\\' && v[1] == L'\\'))
            wcscpy_s(joined, MAX_PATH, v);
        else
            swprintf(joined, MAX_PATH, L"%s\\%s", g_selfDir, v);
        if (!GetFullPathNameW(joined, MAX_PATH, g_gamesDir, NULL))
            wcscpy_s(g_gamesDir, MAX_PATH, joined);
        return;
    }
    static const wchar_t *tries[] = {L"games", L"..\\games", L"..\\..\\games"};
    for (int i = 0; i < 3; i++)
    {
        wchar_t joined[MAX_PATH];
        swprintf(joined, MAX_PATH, L"%s\\%s", g_selfDir, tries[i]);
        if (GetFullPathNameW(joined, MAX_PATH, g_gamesDir, NULL) && is_dir(g_gamesDir))
            return;
    }
    swprintf(g_gamesDir, MAX_PATH, L"%s\\games", g_selfDir);
}

// The shallowest program of the table under dir, depth levels down at most (e->depth: the levels
// that were left where it was found, so the larger the shallower)
static void find_program(const wchar_t *dir, int depth, Entry *e)
{
    wchar_t pattern[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%s\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pattern, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do
    {
        if (fd.cFileName[0] == L'.' && (!fd.cFileName[1] || (fd.cFileName[1] == L'.' && !fd.cFileName[2])))
            continue;
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            // Below: only when it can still give a shallower one
            if (depth > 1 && (!e->game || depth - 1 > e->depth) &&
                !(fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_REPARSE_POINT)))
                find_program(path, depth - 1, e);
        }
        else if ((!e->game || depth > e->depth) && wl_known_program_name(fd.cFileName))
        {
            const WlKnownGame *g = wl_known_game(path);
            if (g)
            {
                e->game = g;
                e->depth = depth;
                wcscpy_s(e->exe, MAX_PATH, path);
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void free_entries(void)
{
    for (int i = 0; i < g_count; i++)
        if (g_entries[i].art)
            g_entries[i].art->Release();
    free(g_entries);
    g_entries = NULL;
    g_count = 0;
}

static int by_label(const void *a, const void *b)
{
    return _stricmp(((const Entry *)a)->label, ((const Entry *)b)->label);
}

static void scan_games(void)
{
    free_entries();
    wchar_t pattern[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%s\\*", g_gamesDir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    int cap = 0;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.')
            continue;
        Entry e;
        memset(&e, 0, sizeof e);
        swprintf(e.folder, MAX_PATH, L"%s\\%s", g_gamesDir, fd.cFileName);
        find_program(e.folder, 7, &e);
        if (!e.game)
            continue;
        u8(fd.cFileName, e.folderName, sizeof e.folderName);
        u8(e.exe + wcslen(e.folder) + 1, e.exeShown, sizeof e.exeShown);
        if (e.game->args)
            u8(e.game->args, e.args, sizeof e.args);
        snprintf(e.label, sizeof e.label, "%s", e.game->title);
        if (g_count == cap)
        {
            cap = cap ? cap * 2 : 32;
            g_entries = (Entry *)realloc(g_entries, cap * sizeof(Entry));
        }
        g_entries[g_count++] = e;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    // The same game in several folders (Sega Rally 3's editions): the folder's name tells them apart
    for (int i = 0; i < g_count; i++)
        for (int k = 0; k < g_count; k++)
            if (k != i && g_entries[k].game == g_entries[i].game)
            {
                snprintf(g_entries[i].label, sizeof g_entries[i].label, "%s (%s)", g_entries[i].game->title, g_entries[i].folderName);
                break;
            }
    qsort(g_entries, g_count, sizeof(Entry), by_label);
    g_focusSelected = 1;
}

// ------------------------------------------------------------------------------
// The game's picture (WIC)
// ------------------------------------------------------------------------------

static ID3D11ShaderResourceView *load_picture(const wchar_t *path, int *w, int *h)
{
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *rgba = NULL;
    ID3D11ShaderResourceView *view = NULL;
    unsigned char *pixels = NULL;
    UINT width = 0, height = 0;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateDecoderFromFilename(path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&rgba)) &&
        SUCCEEDED(rgba->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(rgba->GetSize(&width, &height)) && width && height && width <= 8192 && height <= 8192 &&
        (pixels = (unsigned char *)malloc((size_t)width * height * 4)) != NULL && SUCCEEDED(rgba->CopyPixels(NULL, width * 4, width * height * 4, pixels)))
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data = {pixels, width * 4, 0};
        ID3D11Texture2D *texture = NULL;
        if (SUCCEEDED(g_dev->CreateTexture2D(&desc, &data, &texture)))
        {
            g_dev->CreateShaderResourceView(texture, NULL, &view);
            texture->Release();
            *w = (int)width;
            *h = (int)height;
        }
    }
    free(pixels);
    if (rgba)
        rgba->Release();
    if (frame)
        frame->Release();
    if (decoder)
        decoder->Release();
    if (factory)
        factory->Release();
    return view;
}

static void load_art(Entry *e)
{
    e->artTried = 1;
    static const wchar_t *names[] = {L"snap", L"title", L"art", L"cover", NULL};
    static const wchar_t *exts[] = {L".png", L".jpg"};
    const wchar_t *own = wcsrchr(e->folder, L'\\');
    for (int i = 0; i < 5 && !e->art; i++)
        for (int k = 0; k < 2 && !e->art; k++)
        {
            wchar_t path[MAX_PATH];
            swprintf(path, MAX_PATH, L"%s\\%s%s", e->folder, names[i] ? names[i] : own + 1, exts[k]);
            if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
                e->art = load_picture(path, &e->artW, &e->artH);
        }
}

// ------------------------------------------------------------------------------
// Playing a game: windowsloader.exe with it, waited for
// ------------------------------------------------------------------------------

// The end of what the launcher writes (UTF-8): its own errors, which come before the game's log
struct Tail
{
    HANDLE pipe;
    char text[2048];
    int len;
};

static DWORD WINAPI read_tail(LPVOID param)
{
    Tail *t = (Tail *)param;
    char chunk[1024];
    DWORD rd;
    while (ReadFile(t->pipe, chunk, sizeof chunk, &rd, NULL) && rd)
    {
        int keep = (int)sizeof t->text - 1 - (int)rd;
        if (t->len > keep)
        {
            memmove(t->text, t->text + t->len - keep, keep);
            t->len = keep;
        }
        memcpy(t->text + t->len, chunk, rd);
        t->len += rd;
        t->text[t->len] = 0;
    }
    return 0;
}

static void play(HWND wnd, const Entry *e)
{
    wchar_t cmd[4096];
    // --quiet: the game's log stays in windowsloader.log (beside this program), the pipe only gets
    // the launcher's messages
    swprintf(cmd, 4096, L"\"%s\" --quiet \"%s\"%s%s", g_selfPath, e->exe, e->game->args ? L" " : L"", e->game->args ? e->game->args : L"");
    static Tail tail;
    tail.len = 0;
    tail.text[0] = 0;
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE out = NULL;
    if (!CreatePipe(&tail.pipe, &out, &sa, 0))
        tail.pipe = out = NULL;
    else
        SetHandleInformation(tail.pipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si = {sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = out;
    PROCESS_INFORMATION pi;
    BOOL started = CreateProcessW(g_selfPath, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, g_selfDir, &si, &pi);
    DWORD err = GetLastError();
    if (out)
        CloseHandle(out); // the launcher's copy is the only one left: the pipe ends with it
    if (!started)
    {
        if (tail.pipe)
            CloseHandle(tail.pipe);
        char exe[MAX_PATH * 3];
        u8(e->exe, exe, sizeof exe);
        snprintf(g_message, sizeof g_message, wl_tr("Impossible de lancer %s (erreur %lu)."), exe, err);
        return;
    }
    HANDLE reader = tail.pipe ? CreateThread(NULL, 0, read_tail, &tail, 0, NULL) : NULL;
    CloseHandle(pi.hThread);
    // The picker in front passes its right to put a window in front to the launcher, which passes it
    // to the game: hidden, the picker is no longer the program in front
    AllowSetForegroundWindow(pi.dwProcessId);
    ShowWindow(wnd, SW_HIDE);
    ULONGLONG start = GetTickCount64();
    for (;;)
    {
        DWORD r = MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, INFINITE, QS_ALLINPUT);
        if (r != WAIT_OBJECT_0 + 1)
            break;
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (reader)
    {
        // A program the game left running may still hold the pipe
        if (WaitForSingleObject(reader, 1000) == WAIT_TIMEOUT)
        {
            CancelSynchronousIo(reader);
            WaitForSingleObject(reader, 1000);
        }
        CloseHandle(reader);
    }
    if (tail.pipe)
        CloseHandle(tail.pipe);
    ShowWindow(wnd, SW_SHOW);
    SetForegroundWindow(wnd);
    // Stopped at once: most likely the launcher's error (a missing DLL, a program it cannot
    // start), told on its output, there being no console
    if (code && GetTickCount64() - start < 10000)
    {
        char dir[MAX_PATH * 3];
        u8(g_logDir, dir, sizeof dir);
        if (g_log)
            snprintf(g_message, sizeof g_message,
                     wl_tr("Le jeu s'est arrêté aussitôt (code %ld).\n\n%s%sLe journal du jeu, s'il a démarré : %s\\windowsloader.log."), (long)code,
                     tail.text, tail.len ? "\n" : "", dir);
        else
            snprintf(g_message, sizeof g_message,
                     wl_tr("Le jeu s'est arrêté aussitôt (code %ld).\n\n%s%sPas de journal : cocher « Journal » et relancer le jeu."), (long)code,
                     tail.text, tail.len ? "\n" : "");
    }
    ImGui::GetIO().ClearInputKeys(); // the keys the game had are not the picker's
    g_keysFrom = GetTickCount64() + 600;
    g_focusSelected = 1;
}

// ------------------------------------------------------------------------------
// The games folder, chosen in a folder dialog
// ------------------------------------------------------------------------------

static void choose_games_dir(HWND wnd)
{
    IFileOpenDialog *dialog = NULL;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    wchar_t title[128];
    dialog->SetTitle(wide(wl_tr("Dossier des jeux"), title, 128));
    IShellItem *start = NULL;
    if (SUCCEEDED(SHCreateItemFromParsingName(g_gamesDir, NULL, IID_PPV_ARGS(&start))))
    {
        dialog->SetFolder(start);
        start->Release();
    }
    IShellItem *chosen = NULL;
    if (SUCCEEDED(dialog->Show(wnd)) && SUCCEEDED(dialog->GetResult(&chosen)))
    {
        wchar_t *path = NULL;
        if (SUCCEEDED(chosen->GetDisplayName(SIGDN_FILESYSPATH, &path)))
        {
            wcscpy_s(g_gamesDir, MAX_PATH, path);
            if (!g_iniReadOnly)
            {
                WritePrivateProfileStringW(L"Launcher", L"GAMES_DIR", g_gamesDir, g_ini);
                WritePrivateProfileStringW(L"Picker", L"GAMES_DIR", NULL, g_ini); // the old place, if any, gone
            }
            CoTaskMemFree(path);
            scan_games();
        }
        chosen->Release();
    }
    dialog->Release();
    ImGui::GetIO().ClearInputKeys();
}

// ------------------------------------------------------------------------------
// Direct3D 11 and the window
// ------------------------------------------------------------------------------

static void create_target(void)
{
    ID3D11Texture2D *back = NULL;
    if (SUCCEEDED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back))))
    {
        g_dev->CreateRenderTargetView(back, NULL, &g_rtv);
        back->Release();
    }
}

static void release_target(void)
{
    if (g_rtv)
    {
        g_rtv->Release();
        g_rtv = NULL;
    }
}

static int create_device(HWND wnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = wnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &got, &g_ctx);
    if (hr == DXGI_ERROR_UNSUPPORTED) // no graphics card: WARP, the processor's
        hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &got, &g_ctx);
    if (FAILED(hr))
        return 0;
    create_target();
    return 1;
}

static LRESULT CALLBACK wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ImGui_ImplWin32_WndProcHandler(wnd, msg, wp, lp))
        return 1;
    switch (msg)
    {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED)
        {
            g_resizeW = LOWORD(lp);
            g_resizeH = HIWORD(lp);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_KEYMENU) // Alt alone does not open a menu
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

// ------------------------------------------------------------------------------
// The picker's frame
// ------------------------------------------------------------------------------

static const char *state_text(int state, ImVec4 *color)
{
    switch (state)
    {
    case WL_GAME_PLAYS:
        *color = ImVec4(0.45f, 0.85f, 0.45f, 1);
        return wl_tr("Jouable");
    case WL_GAME_PARTLY:
        *color = ImVec4(0.95f, 0.75f, 0.3f, 1);
        return wl_tr("Jouable, avec des réserves");
    default:
        *color = ImVec4(0.9f, 0.45f, 0.4f, 1);
        return wl_tr("En cours : ne se joue pas encore");
    }
}

// A feature of the game (gamelist.h's WL_FEATURE_...), for its sheet
static const char *feature_text(int feature)
{
    return feature == WL_FEATURE_YES ? wl_tr("oui") : feature == WL_FEATURE_PARTLY ? wl_tr("en partie (voir BILAN.md)") : wl_tr("non");
}

static void field(const char *name, const char *value)
{
    if (!value || !value[0])
        return;
    ImGui::TextDisabled("%s", name);
    ImGui::SameLine(ImGui::GetFontSize() * 9);
    ImGui::TextWrapped("%s", value);
}

// What the frame asks for, done once it is shown: a scan in the middle of it would release the
// picture it still draws
enum
{
    ACT_NONE,
    ACT_PLAY,
    ACT_FOLDER,
    ACT_RESCAN
};

// Returns 1 to quit
static int draw(int *selected, int *action)
{
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##picker", NULL,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    char dir[MAX_PATH * 3];
    u8(g_gamesDir, dir, sizeof dir);
    ImGui::PushFont(NULL, ImGui::GetFontSize() * 1.5f);
    ImGui::Text("windowsloader");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("  ");
    ImGui::SameLine(0, 0);
    ImGui::TextDisabled(g_count > 1 ? wl_tr("%d jeux dans %s") : wl_tr("%d jeu dans %s"), g_count, dir);
    ImGui::Separator();

    float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (!g_count)
    {
        ImGui::TextWrapped(wl_tr("Aucun jeu trouvé dans %s. Choisissez le dossier des jeux (Tab) : chaque dossier qui y contient un jeu reconnu "
                                 "par windowsloader devient une ligne de la liste."),
                           dir);
    }
    else
    {
        if (*selected >= g_count)
            *selected = g_count - 1;
        ImGui::BeginChild("list", ImVec2(avail.x * 0.42f, avail.y - footer), ImGuiChildFlags_Borders);
        ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true); // Tab is the games folder's, not a move in the list
        for (int i = 0; i < g_count; i++)
        {
            ImGui::PushID(i);
            if (g_focusSelected && *selected == i)
            {
                ImGui::SetKeyboardFocusHere();
                ImGui::SetScrollHereY();
                g_focusSelected = 0;
            }
            if (ImGui::Selectable(g_entries[i].label, *selected == i, ImGuiSelectableFlags_AllowDoubleClick))
            {
                *selected = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    *action = ACT_PLAY;
            }
            if (ImGui::IsItemFocused())
                *selected = i;
            ImGui::PopID();
        }
        ImGui::PopItemFlag();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("pane", ImVec2(0, avail.y - footer), ImGuiChildFlags_Borders);
        Entry *e = &g_entries[*selected];
        if (!e->artTried)
            load_art(e);
        if (e->art)
        {
            float w = ImGui::GetContentRegionAvail().x, h = w * e->artH / e->artW;
            float maxH = ImGui::GetContentRegionAvail().y * 0.45f;
            if (h > maxH)
                w = maxH * e->artW / e->artH, h = maxH;
            ImGui::Image((ImTextureID)(intptr_t)e->art, ImVec2(w, h));
        }
        ImGui::PushFont(NULL, ImGui::GetFontSize() * 1.35f);
        ImGui::TextWrapped("%s", e->game->title);
        ImGui::PopFont();
        ImVec4 color;
        const char *state = state_text(e->game->state, &color);
        ImGui::TextColored(color, "%s", state);
        ImGui::Spacing();
        char platform[128];
        // The table's texts (WL_N) in the language shown
        snprintf(platform, sizeof platform, "%s%s%s", wl_tr(e->game->platform), e->game->year[0] ? ", " : "", e->game->year);
        field(wl_tr("Plateforme"), platform);
        field(wl_tr("Dossier"), e->folderName);
        field(wl_tr("Programme"), e->exeShown);
        field(wl_tr("Arguments"), e->args);
        field(wl_tr("Retour de force"), feature_text(e->game->ffb));
        field(wl_tr("Link"), feature_text(e->game->link));
        field(wl_tr("Émulé"), wl_tr(e->game->note));
        ImGui::Spacing();
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
        if (ImGui::Button(wl_tr("Jouer (Entrée)")))
            *action = ACT_PLAY;
        ImGui::PopItemFlag();
        ImGui::EndChild();
    }

    // The buttons are for the mouse: the keyboard and the gamepad stay in the list, Enter plays
    ImGui::Separator();
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    if (ImGui::Button(wl_tr("Dossier des jeux (Tab)")))
        *action = ACT_FOLDER;
    ImGui::SameLine();
    if (ImGui::Button(wl_tr("Actualiser (F5)")))
        *action = ACT_RESCAN;
    ImGui::SameLine();
    int quit = ImGui::Button(wl_tr("Quitter (Échap)"));
    // The interface's language (lang.h): at once, and kept in [Launcher] LANGUAGE, which the
    // game's menu reads too
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    if (wl_lang_combo("##language"))
    {
        wchar_t setting[16];
        if (!g_iniReadOnly)
            WritePrivateProfileStringW(L"Launcher", L"LANGUAGE", wide(wl_lang_setting(), setting, 16), g_ini);
        g_retitle = 1;
    }
    // The games' log, windowsloader.log ([Launcher] LOG): from the next start
    ImGui::SameLine();
    if (ImGui::Checkbox(wl_tr("Journal"), &g_log) && !g_iniReadOnly)
        WritePrivateProfileStringW(L"Launcher", L"LOG", g_log ? L"true" : L"false", g_ini);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        char dir[MAX_PATH * 3];
        u8(g_logDir, dir, sizeof dir);
        ImGui::SetTooltip(wl_tr("Le journal des jeux, %s\\windowsloader.log, refait à chaque lancement (pour le débogage).\n"
                                "Pris en compte au prochain lancement d'un jeu."),
                          dir);
    }
    ImGui::PopItemFlag();
    ImGui::SameLine();
    ImGui::TextDisabled("  %s", wl_tr("Entrée : jouer   Flèches : choisir   Tab : dossier des jeux"));

    // A game that would not start: what the launcher said, until OK. Its title in the language
    // shown, its name (###failed) the same in all
    int message = 0;
    if (g_message[0] && !ImGui::IsPopupOpen("###failed"))
        ImGui::OpenPopup("###failed");
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.6f, 0));
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    char failed[96];
    snprintf(failed, sizeof failed, "%s###failed", wl_tr("Le jeu n'a pas démarré"));
    if (ImGui::BeginPopupModal(failed, NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
    {
        message = 1;
        ImGui::TextWrapped("%s", g_message);
        ImGui::Spacing();
        int keys = GetTickCount64() >= g_keysFrom;
        if (ImGui::Button(wl_tr("OK (Entrée)")) ||
            (keys && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
                      ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false))))
        {
            g_message[0] = 0;
            g_focusSelected = 1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::End();

    if (message || GetTickCount64() < g_keysFrom)
        return quit;
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false))
        *action = ACT_PLAY;
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false))
        *action = ACT_FOLDER;
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
        *action = ACT_RESCAN;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_F9, false))
        quit = 1;
    if (*action == ACT_PLAY && !g_count)
        *action = ACT_NONE;
    return quit;
}

// "windowsloader <version> <edition> — choix du jeu", in the language shown
static const wchar_t *window_title(wchar_t *out, int size)
{
    char title[256];
    snprintf(title, sizeof title, wl_tr("windowsloader %s %s — choix du jeu"), WL_BUILD_VERSION, WL_EDITION);
    return wide(title, out, size);
}

int wl_picker_run(void)
{
    GetModuleFileNameW(NULL, g_selfPath, MAX_PATH);
    wcscpy_s(g_selfDir, MAX_PATH, g_selfPath);
    wchar_t *slash = wcsrchr(g_selfDir, L'\\');
    if (slash)
        *slash = 0;
    swprintf(g_ini, MAX_PATH, L"%s\\windowsloader.ini", g_selfDir);
    // With a write folder ([Paths] WRITE_DIR), its windowsloader.ini is the one read and written,
    // the log there too
    wcscpy_s(g_logDir, MAX_PATH, g_selfDir);
    wchar_t root[MAX_PATH];
    int write = wl_write_dir_root(g_selfDir, NULL, g_ini, root, MAX_PATH);
    if (write > 0 && wl_write_dir_file(root, L"windowsloader.ini", g_ini, MAX_PATH) < 0)
        write = -3;
    if (write > 0)
        wcscpy_s(g_logDir, MAX_PATH, root);
    else if (write < 0)
        g_iniReadOnly = 1; // nothing written in its stead (writedir.h): the launcher says why, the game shows it
    // The interface's language (lang.h): [Launcher] LANGUAGE, lang\ beside this program
    wchar_t language[16];
    char setting[16];
    GetPrivateProfileStringW(L"Launcher", L"LANGUAGE", L"AUTO", language, 16, g_ini);
    u8(language, setting, sizeof setting);
    g_log = wl_log_setting(g_ini) != 0;
    wl_lang_init(g_selfDir, setting, NULL);

    // Started from the Explorer, the console is ours alone: it goes away
    DWORD consoles[2];
    if (GetConsoleProcessList(consoles, 2) == 1)
        FreeConsole();

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc = {sizeof wc, CS_CLASSDC, wnd_proc, 0, 0, GetModuleHandleW(NULL)};
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"windowsloader picker";
    RegisterClassExW(&wc);
    float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    wchar_t title[256];
    HWND wnd = CreateWindowExW(0, wc.lpszClassName, window_title(title, 256), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, (int)(1280 * scale),
                               (int)(760 * scale), NULL, NULL, wc.hInstance, NULL);
    if (!wnd || !create_device(wnd))
    {
        wchar_t text[256];
        MessageBoxW(NULL, wide(wl_tr("Impossible d'ouvrir la fenêtre de choix du jeu (Direct3D 11)."), text, 256), L"windowsloader", MB_ICONERROR);
        return 1;
    }
    ShowWindow(wnd, SW_SHOWDEFAULT);
    UpdateWindow(wnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(scale);
    wchar_t fonts[MAX_PATH];
    char font[MAX_PATH * 3];
    GetWindowsDirectoryW(fonts, MAX_PATH);
    wcscat_s(fonts, MAX_PATH, L"\\Fonts\\segoeui.ttf");
    u8(fonts, font, sizeof font);
    if (GetFileAttributesW(fonts) == INVALID_FILE_ATTRIBUTES || !io.Fonts->AddFontFromFileTTF(font, 19.0f * scale))
        io.Fonts->AddFontDefault();
    ImGui_ImplWin32_Init(wnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    find_games_dir();
    scan_games();

    int selected = 0, done = 0;
    while (!done)
    {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                done = 1;
        }
        if (done)
            break;
        if (IsIconic(wnd)) // reduced: nothing to draw, and Present would no longer wait for the screen
        {
            MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLINPUT);
            continue;
        }
        if (g_resizeW)
        {
            release_target();
            g_swap->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            create_target();
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        int action = ACT_NONE;
        if (draw(&selected, &action))
            done = 1;
        if (g_retitle)
        {
            wchar_t title[256];
            SetWindowTextW(wnd, window_title(title, 256));
            g_retitle = 0;
        }
        ImGui::Render();
        static const float clear[4] = {0.08f, 0.08f, 0.1f, 1};
        g_ctx->OMSetRenderTargets(1, &g_rtv, NULL);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
        if (done)
            break;
        if (action == ACT_PLAY)
            play(wnd, &g_entries[selected]);
        else if (action == ACT_FOLDER)
            choose_games_dir(wnd);
        else if (action == ACT_RESCAN)
            scan_games();
    }

    free_entries();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    release_target();
    g_swap->Release();
    g_ctx->Release();
    g_dev->Release();
    DestroyWindow(wnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CoUninitialize();
    return 0;
}
