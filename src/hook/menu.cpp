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

// The settings menu, on Tab (as linuxloader's, Flycast's and Supermodel's). It opens in the Present hook, on the game's render thread, and keeps that thread
// until it is closed: the game's logic stands still, its clocks and its sound too (pause.cpp).
// Each frame of the menu draws the game's last frame, then Dear ImGui over it, and presents.
// Direct3D 9 (Sega Rally 3) and Direct3D 11 (Daytona Championship USA) each have their frame;
// ImGui is set up for the first renderer the menu opens on.
//
// The picture seldom has the window's size (a 1920x1080 picture in a 1674x942 window, or with
// black bars around it): ImGui works in the picture's size, and the mouse is scaled from the part
// of the window showing the picture. The keyboard comes through the game window's procedure
// (display.cpp); the game gets neither the mouse nor the keyboard while the menu is open.

#include <windows.h>
#include <d3d9.h>
#include <d3d10_1.h>
#include <d3d11.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <wctype.h>
#include <string.h>

#include <GL/gl.h>

#include "imgui.h"
#include "backends/imgui_impl_dx10.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_opengl2.h"
#include "backends/imgui_impl_win32.h"

#include "agent.h"
#include "controls.h"
#include "display.h"
#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "lang.h"
#include "log.h"
#include "menu.h"
#include "outputs.h"
#include "pause.h"
#include "vdev.h"
#include "settings.h"
#include "wlshared.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static volatile LONG g_open;
static volatile LONG g_request = -1; // the agent's: 1 open, 0 close
static int g_ready;                  // 1 set up, -1 failed
static int g_backend;                // 9, 10 or 11: the renderer ImGui was set up for
static HWND g_wnd;
static int g_bbW, g_bbH;

static volatile LONG g_tabRequest = -1;
static int g_captureKeys; // a binding's input being captured (the controls page): the keys are its, not ImGui's

void wl_menu_request_tab(int tab)
{
    InterlockedExchange(&g_tabRequest, tab);
}

static volatile LONG g_mapRequest = -2; // the agent's "menu map": a device's bindings, -1 closed, -2 none asked

void wl_menu_request_map(int device)
{
    InterlockedExchange(&g_mapRequest, device);
}

void wl_menu_request(int open)
{
    InterlockedExchange(&g_request, open ? 1 : 0);
}

int wl_menu_is_open(void)
{
    return g_open;
}

static RECT g_menuRect; // the menu's window, in the picture

int wl_menu_rect(RECT *r)
{
    if (!g_open || IsRectEmpty(&g_menuRect))
        return 0;
    *r = g_menuRect;
    return 1;
}

static int foreground_is_ours(void)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// Tab pressed since the last call (the key is read directly: the game's message loop does not
// run while the menu is open); not Alt+Tab, the switch between windows. Never with [Launcher]
// SETTINGS_MENU = false (linuxloader's key): on a public cabinet, Tab no longer pauses the game.
// The agent's "menu on" still opens it
static int menu_key_pressed(void)
{
    static int was, allowed = -1;
    if (allowed < 0)
    {
        allowed = wl_settings_get_bool("Launcher", "SETTINGS_MENU", 1);
        if (!allowed)
            wl_log("menu: Tab does not open the settings ([Launcher] SETTINGS_MENU = false)");
    }
    if (!allowed)
        return 0;
    int down = foreground_is_ours() && (GetAsyncKeyState(VK_TAB) & 0x8000) && !(GetAsyncKeyState(VK_MENU) & 0x8000);
    int pressed = down && !was;
    was = down;
    return pressed;
}

// ------------------------------------------------------------------------------
// Window procedure
// ------------------------------------------------------------------------------

int wl_menu_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp, LRESULT *result)
{
    *result = 0;
    // F10 is a system key: DefWindowProc would open the window menu with it and keep the keyboard
    if ((msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) && wp == VK_F10)
        return 1;
    if (!g_open || g_ready <= 0)
        return 0;
    if (msg == WM_MOUSEWHEEL)
    {
        ImGui::GetIO().AddMouseWheelEvent(0, (float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA);
        return 1;
    }
    if (msg >= WM_KEYFIRST && msg <= WM_KEYLAST)
    {
        // A key pressed for a binding is not a move in the page nor a button's Enter (Escape still cancels)
        if (!g_captureKeys || wp == VK_ESCAPE)
            ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp);
        return 1;
    }
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS)
        ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp);
    return msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST; // read by feed_mouse, scaled to the picture
}

// ------------------------------------------------------------------------------
// Set-up, once: ImGui, its fonts sized for the picture, the window procedure
// ------------------------------------------------------------------------------

static int setup_imgui(HWND wnd)
{
    g_wnd = wnd ? wnd : GetActiveWindow();
    if (!g_wnd)
        return 0;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
    io.MouseDrawCursor = true; // the game may hide the system cursor

    float scale = g_bbH > 720 ? g_bbH / 720.0f : 1.0f;
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 6;
    style.FrameRounding = 4;
    style.ScaleAllSizes(scale);
    char font[MAX_PATH];
    GetWindowsDirectoryA(font, MAX_PATH);
    strcat_s(font, MAX_PATH, "\\Fonts\\segoeui.ttf");
    if (GetFileAttributesA(font) == INVALID_FILE_ATTRIBUTES || !io.Fonts->AddFontFromFileTTF(font, 21.0f * scale))
    {
        io.Fonts->AddFontDefault();
        style.FontScaleMain = scale * 1.4f;
    }

    return ImGui_ImplWin32_Init(g_wnd);
}

static int setup(IDirect3DDevice9 *dev)
{
    D3DDEVICE_CREATION_PARAMETERS cp = {};
    dev->GetCreationParameters(&cp);
    if (!setup_imgui(cp.hFocusWindow) || !ImGui_ImplDX9_Init(dev))
    {
        wl_log("menu: ImGui could not start on this device");
        return 0;
    }
    g_backend = 9;
    wl_log("menu: ready (Direct3D 9, window %p, picture %dx%d)", g_wnd, g_bbW, g_bbH);
    return 1;
}

static int setup11(HWND wnd, ID3D11Device *dev, ID3D11DeviceContext *ctx)
{
    if (!setup_imgui(wnd) || !ImGui_ImplDX11_Init(dev, ctx))
    {
        wl_log("menu: ImGui could not start on this device");
        return 0;
    }
    g_backend = 11;
    wl_log("menu: ready (Direct3D 11, window %p, picture %dx%d)", g_wnd, g_bbW, g_bbH);
    return 1;
}

// A Direct3D 10 game (Mario Kart Arcade GP DX): what a Direct3D 11 context draws on its swap chain's
// buffer is not seen in the picture presented, the menu draws through the game's device
static int setup10(HWND wnd, ID3D10Device *dev)
{
    if (!setup_imgui(wnd) || !ImGui_ImplDX10_Init(dev))
    {
        wl_log("menu: ImGui could not start on this device");
        return 0;
    }
    g_backend = 10;
    wl_log("menu: ready (Direct3D 10, window %p, picture %dx%d)", g_wnd, g_bbW, g_bbH);
    return 1;
}

void wl_menu_device_reset(void)
{
    if (g_ready > 0 && g_backend == 9)
        ImGui_ImplDX9_InvalidateDeviceObjects();
}

static void feed_mouse(void)
{
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)g_bbW, (float)g_bbH);
    POINT pt;
    RECT pic = {0, 0, 0, 0};
    if (wl_display_hwnd())
        wl_display_picture(&pic);
    else
        GetClientRect(g_wnd, &pic); // a window the display leaves as it is (OpenGL): the picture fills it
    int pw = pic.right - pic.left, ph = pic.bottom - pic.top;
    if (GetCursorPos(&pt) && ScreenToClient(g_wnd, &pt) && pw > 0 && ph > 0)
        io.AddMousePosEvent((pt.x - pic.left) * (float)g_bbW / pw, (pt.y - pic.top) * (float)g_bbH / ph);
    int fg = foreground_is_ours();
    int swap = GetSystemMetrics(SM_SWAPBUTTON);
    io.AddMouseButtonEvent(0, fg && (GetAsyncKeyState(swap ? VK_RBUTTON : VK_LBUTTON) & 0x8000));
    io.AddMouseButtonEvent(1, fg && (GetAsyncKeyState(swap ? VK_LBUTTON : VK_RBUTTON) & 0x8000));
}

// ------------------------------------------------------------------------------
// Pages
// ------------------------------------------------------------------------------

// ------------------------------------------------------------------------------
// Controls: linuxloader's page (its guiControls.cpp, itself laid out as Flycast's): the devices
// found, each with a page of its own that binds the actions of controls.ini to that device's
// inputs; then what the game gets and the [Config] options. windowsloader's own besides: each row's
// "Ajouter" (one more input of the device, "Lier" replacing them) and "Défaut" (the device's inputs
// of the action as a new controls.ini has them; linuxloader resets the whole device only), and what
// the game gets. Written to controls.ini at each change, the bindings reloaded at once.
// ------------------------------------------------------------------------------

// The name of an action without its player (P1_Steer: Steer, player 1)
static const char *generic_name(const char *key, int *player)
{
    *player = 0;
    if (key[0] == 'P' && key[1] >= '1' && key[1] <= '4' && key[2] == '_')
    {
        *player = key[1] - '0';
        return key + 3;
    }
    return key;
}

static const struct
{
    const char *name;
    const char *label;
} g_actionLabels[] = {
    {"Test", WL_N("Test")},
    {"Coin", WL_N("Pièce")},
    {"Start", WL_N("Départ")},
    {"Service", WL_N("Service")},
    {"ExitGame", WL_N("Quitter le jeu")},
    {"Steer", WL_N("Volant")},
    {"Gas", WL_N("Accélérateur")},
    {"Brake", WL_N("Frein")},
    {"Clutch", WL_N("Embrayage")},
    {"Steer_Left", WL_N("Volant à gauche")},
    {"Steer_Right", WL_N("Volant à droite")},
    {"Gas_Digital", WL_N("Accélérateur (tout ou rien)")},
    {"Brake_Digital", WL_N("Frein (tout ou rien)")},
    {"Clutch_Digital", WL_N("Embrayage (tout ou rien)")},
    {"GearUp", WL_N("Vitesse +")},
    {"GearDown", WL_N("Vitesse −")},
    {"ViewChange", WL_N("Changer de vue")},
    {"Handbrake", WL_N("Frein à main")},
    {"Hazard", WL_N("Feux de détresse")},
    {"Overtake", WL_N("Dépassement")},
    {"Nitro", WL_N("Nitro")},
    {"ShiftNitro", WL_N("Nitro (levier)")},
    {"Action", WL_N("Action")},
    {"SelectUp", WL_N("Choix : haut")},
    {"SelectDown", WL_N("Choix : bas")},
    {"SelectLeft", WL_N("Choix : gauche")},
    {"SelectRight", WL_N("Choix : droite")},
    {"SelectEnter", WL_N("Choix : valider")},
    {"View1", WL_N("Vue 1 (rouge)")},
    {"View2", WL_N("Vue 2 (bleu)")},
    {"View3", WL_N("Vue 3 (jaune)")},
    {"View4", WL_N("Vue 4 (vert)")},
    {"Gear1", WL_N("1re")},
    {"Gear2", WL_N("2e")},
    {"Gear3", WL_N("3e")},
    {"Gear4", WL_N("4e")},
    {"Gear5", WL_N("5e")},
    {"Gear6", WL_N("6e")},
    {"PedalLeft", WL_N("Pédale gauche")},
    {"PedalRight", WL_N("Pédale droite")},
    {"Boost", WL_N("Boost")},
    {"MusicChange", WL_N("Changer de musique")},
};

static const char *action_label(const char *name)
{
    for (int i = 0; i < (int)(sizeof g_actionLabels / sizeof g_actionLabels[0]); i++)
        if (!_stricmp(name, g_actionLabels[i].name))
            return wl_tr(g_actionLabels[i].label);
    return name;
}

static int is_pedal(const char *name)
{
    return !_stricmp(name, "Gas") || !_stricmp(name, "Brake") || !_stricmp(name, "Clutch");
}

static int is_analog(const char *name)
{
    return is_pedal(name) || !_stricmp(name, "Steer");
}

// A binding's alternatives ("A, B + C": "A" and "B + C"), trimmed
#define MAX_TOKENS 16
typedef char Token[96];

static int alternatives(const char *value, Token *out, int max)
{
    int n = 0;
    while (*value && n < max)
    {
        const char *end = strchr(value, ',');
        size_t len = end ? (size_t)(end - value) : strlen(value);
        while (len && (*value == ' ' || *value == '\t'))
            value++, len--;
        while (len && (value[len - 1] == ' ' || value[len - 1] == '\t'))
            len--;
        if (len)
            snprintf(out[n++], sizeof out[0], "%.*s", (int)len, value);
        if (!end)
            break;
        value = end + 1;
    }
    return n;
}

static void join(const Token *tokens, int n, char *out, size_t size)
{
    out[0] = 0;
    for (int i = 0; i < n; i++)
    {
        size_t used = strlen(out);
        snprintf(out + used, size - used, "%s%s", used ? ", " : "", tokens[i]);
    }
}

// The alternative belongs to the device when its first input is one of the device's
static int owns(const WlControlDevice *d, const char *token)
{
    if (d->player < 0)
        return !_strnicmp(token, "KEY_", 4);
    char joy[16], gc[16];
    snprintf(joy, sizeof joy, "JOY%d_", d->player);
    snprintf(gc, sizeof gc, "GC%d_", d->player);
    return !_strnicmp(token, joy, strlen(joy)) || !_strnicmp(token, gc, strlen(gc));
}

// The device's alternatives of a value (mine 1), or the others' (mine 0)
static int device_tokens(const WlControlDevice *d, const char *value, int mine, Token *out, int max)
{
    Token all[MAX_TOKENS];
    int n = alternatives(value, all, MAX_TOKENS), k = 0;
    for (int i = 0; i < n && k < max; i++)
        if (!owns(d, all[i]) == !mine)
            memcpy(out[k++], all[i], sizeof all[i]);
    return k;
}

static const char *axis_suffix(const char *s, char *out, size_t size)
{
    if (!_stricmp(s, "_POSITIVE"))
        return " +";
    if (!_stricmp(s, "_NEGATIVE"))
        return " -";
    if (!_stricmp(s, "_POSITIVE_HALF") || !_stricmp(s, "_NEGATIVE_HALF"))
        snprintf(out, size, "%s%s", s[1] == 'P' ? " +" : " -", wl_tr(" (moitié)"));
    else if (!_stricmp(s, "_POSITIVE_SIDE") || !_stricmp(s, "_NEGATIVE_SIDE"))
        snprintf(out, size, "%s%s", s[1] == 'P' ? " +" : " -", wl_tr(" (pédale)"));
    else if (!_stricmp(s, "_INVERTED"))
        snprintf(out, size, "%s", wl_tr(" (inversé)"));
    else
        snprintf(out, size, "%s", s);
    return out;
}

// An input as the page shows it: "Axe 0 - (pédale)", "Bouton 3", "Left"...
static void describe(const char *token, char *out, size_t size)
{
    out[0] = 0;
    if (strchr(token, '+'))
    {
        char part[96];
        for (const char *p = token; *p;)
        {
            const char *end = strchr(p, '+');
            size_t len = end ? (size_t)(end - p) : strlen(p);
            while (len && *p == ' ')
                p++, len--;
            while (len && p[len - 1] == ' ')
                len--;
            snprintf(part, sizeof part, "%.*s", (int)len, p);
            char shown[96];
            describe(part, shown, sizeof shown);
            size_t used = strlen(out);
            snprintf(out + used, size - used, "%s%s", used ? " + " : "", shown);
            if (!end)
                break;
            p = end + 1;
        }
        return;
    }
    int device = 0, number = 0, length = 0;
    char name[32] = "", suffix[48];
    if (!_strnicmp(token, "KEY_", 4))
        snprintf(out, size, "%s", token + 4);
    else if (sscanf(token, "JOY%d_BUTTON_%d%n", &device, &number, &length) == 2 && !token[length])
        snprintf(out, size, wl_tr("Bouton %d"), number);
    else if (sscanf(token, "JOY%d_AXIS_%d%n", &device, &number, &length) == 2)
    {
        snprintf(out, size, wl_tr("Axe %d"), number);
        size_t used = strlen(out);
        snprintf(out + used, size - used, "%s", axis_suffix(token + length, suffix, sizeof suffix));
    }
    else if (sscanf(token, "JOY%d_HAT%d_%31s", &device, &number, name) == 3)
    {
        const char *dir = !_stricmp(name, "UP") ? wl_tr("haut") : !_stricmp(name, "DOWN") ? wl_tr("bas")
                          : !_stricmp(name, "LEFT") ? wl_tr("gauche") : wl_tr("droite");
        snprintf(out, size, wl_tr("Chapeau %d"), number);
        size_t used = strlen(out);
        snprintf(out + used, size - used, " %s", dir);
    }
    else if (sscanf(token, "GC%d_BUTTON_%31s", &device, name) == 2)
        snprintf(out, size, "%s", name);
    else if (sscanf(token, "GC%d_AXIS_%31[^_]%n", &device, name, &length) == 2)
        snprintf(out, size, "%s%s", name, axis_suffix(token + length, suffix, sizeof suffix));
    else
        snprintf(out, size, "%s", token);
}

// A pedal, 0 released to 1 pressed, as its input reads the axis
static float pedal_level(const char *token, int raw)
{
    float level;
    if (strstr(token, "_POSITIVE_SIDE") || strstr(token, "_POSITIVE_HALF"))
        level = raw > 0 ? raw / 32767.0f : 0.0f;
    else if (strstr(token, "_NEGATIVE_SIDE") || strstr(token, "_NEGATIVE_HALF"))
        level = raw < 0 ? -raw / 32768.0f : 0.0f;
    else if (strstr(token, "TRIGGER"))
        level = raw / 32767.0f;
    else
        level = (raw + 32768.0f) / 65535.0f;
    return strstr(token, "_INVERTED") ? 1.0f - level : level;
}

// sm2-emu's centre bar (centre_bar, github.com/dmanlfc/sm2-emu, GPL v3, Daniel Martin (dmanlfc)
// and the sm2-emu contributors), as linuxloader has it: the axis around its middle, the dead zone
// shaded
static void centre_bar(const char *id, int raw, float deadzone)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    float width = ImGui::GetContentRegionAvail().x, height = ImGui::GetFrameHeight() * 0.6f;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(width > 1 ? width : 1, height));
    ImDrawList *draw = ImGui::GetWindowDrawList();
    float mid = pos.x + width * 0.5f;
    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
    if (deadzone > 0)
    {
        float half = width * 0.5f * deadzone;
        draw->AddRectFilled(ImVec2(mid - half, pos.y), ImVec2(mid + half, pos.y + height), ImGui::GetColorU32(ImGuiCol_FrameBgHovered));
    }
    draw->AddLine(ImVec2(mid, pos.y), ImVec2(mid, pos.y + height), ImGui::GetColorU32(ImGuiCol_TextDisabled));
    float value = raw / 32767.0f;
    value = value < -1 ? -1 : value > 1 ? 1 : value;
    float x = (float)(int)(mid + width * 0.5f * value + 0.5f);
    int inside = (value < 0 ? -value : value) <= deadzone;
    draw->AddRectFilled(ImVec2(x - 2, pos.y), ImVec2(x + 2, pos.y + height), ImGui::GetColorU32(inside ? ImGuiCol_TextDisabled : ImGuiCol_SliderGrabActive));
}

// A dot lit while the input is pressed
static void press_dot(int on)
{
    float size = ImGui::GetFontSize() * 0.7f;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, ImGui::GetFontSize()));
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + size * 0.5f, pos.y + ImGui::GetFontSize() * 0.5f), size * 0.5f,
                                                ImGui::GetColorU32(on ? ImGuiCol_SliderGrabActive : ImGuiCol_FrameBg));
}

// A title bar across the page, as Flycast's and linuxloader's
static void header(const char *label)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    ImGui::Spacing();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x, height = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(ImGuiCol_Header), style.FrameRounding);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + style.FramePadding.x, pos.y + style.FramePadding.y));
    ImGui::TextUnformatted(label);
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + height + style.ItemSpacing.y));
    ImGui::Dummy(ImVec2(0, 0));
}

// controls.ini's rows, read when the page opens and after each change
#define MAX_ROWS 96
static const char *g_rowSections[2] = {"Common", "Driving"};
static WlControlRow g_rows[2][MAX_ROWS], g_configRows[32];
static int g_rowCounts[2], g_configCount, g_rowsRead;

static void read_rows(void)
{
    for (int s = 0; s < 2; s++)
        g_rowCounts[s] = wl_controls_rows(g_rowSections[s], g_rows[s], MAX_ROWS);
    g_configCount = wl_controls_rows("Config", g_configRows, 32);
    g_rowsRead = 1;
}

static void set_row(int section, const char *key, const char *value)
{
    wl_controls_set_row(g_rowSections[section], key, value);
    g_rowsRead = 0;
}

// The device whose page is open, and the input being captured for one of its rows
static struct
{
    int requestOpen, requestClose;
    WlControlDevice device;
} g_mapping;

static struct
{
    int active, requestOpen;
    int append; // "Ajouter": the input joins the device's ones; "Lier": it replaces them
    int section;
    char key[64];
} g_capture;

static void cancel_capture(void)
{
    if (g_capture.active)
        wl_controls_capture_cancel();
    g_capture.active = g_captureKeys = 0;
}

// The row's value with the device's inputs replaced by those given (the other devices' kept)
static void replace_device_tokens(const char *value, const Token *mine, int n, char *out, size_t size)
{
    Token tokens[MAX_TOKENS];
    int k = device_tokens(&g_mapping.device, value, 0, tokens, MAX_TOKENS);
    for (int i = 0; i < n && k < MAX_TOKENS; i++)
        memcpy(tokens[k++], mine[i], sizeof mine[i]);
    join(tokens, k, out, size);
}

// The row as a new controls.ini has it for the device; 1 when the device's inputs differ from those
static int default_of_row(int section, const WlControlRow *row, char *out, size_t size, Token *mine, int *n)
{
    char def[512];
    wl_controls_default_row(g_rowSections[section], row->key, def, sizeof def);
    *n = device_tokens(&g_mapping.device, def, 1, mine, MAX_TOKENS);
    replace_device_tokens(row->value, mine, *n, out, size);
    Token now[MAX_TOKENS];
    int k = device_tokens(&g_mapping.device, row->value, 1, now, MAX_TOKENS);
    if (k != *n)
        return 1;
    for (int i = 0; i < k; i++)
        if (_stricmp(now[i], mine[i]))
            return 1;
    return 0;
}

static void reset_device(void)
{
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < g_rowCounts[s]; i++)
        {
            char value[1024];
            Token mine[MAX_TOKENS];
            int n;
            if (default_of_row(s, &g_rows[s][i], value, sizeof value, mine, &n))
                wl_controls_set_row(g_rowSections[s], g_rows[s][i].key, value);
        }
    g_rowsRead = 0;
}

static const char *config_value(const char *key)
{
    for (int i = 0; i < g_configCount; i++)
        if (!_stricmp(g_configRows[i].key, key))
            return g_configRows[i].value;
    return NULL;
}

static void binding_row(int section, int i, const char *name)
{
    WlControlRow *row = &g_rows[section][i];
    ImGui::PushID(section * MAX_ROWS + i);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(action_label(name));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", row->key);

    Token tokens[MAX_TOKENS];
    int n = device_tokens(&g_mapping.device, row->value, 1, tokens, MAX_TOKENS);
    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    char text[1024] = "", shown[192];
    for (int k = 0; k < n; k++)
    {
        describe(tokens[k], shown, sizeof shown);
        size_t used = strlen(text);
        snprintf(text + used, sizeof text - used, "%s%s", used ? ", " : "", shown);
    }
    if (!text[0])
        ImGui::TextDisabled("-");
    else
        ImGui::TextUnformatted(text);
    if (n && ImGui::IsItemHovered())
    {
        char raw[1024];
        join(tokens, n, raw, sizeof raw);
        ImGui::SetTooltip("%s", raw);
    }

    // The input now: an axis's position, a pedal's travel, a button's dot
    ImGui::TableSetColumnIndex(2);
    int on = 0, any = 0, raw = 0, axis = -1;
    for (int k = 0; k < n; k++)
    {
        int pressedNow, value;
        int kind = wl_controls_input_state(tokens[k], &pressedNow, &value);
        if (kind == 2 && axis < 0)
        {
            axis = k;
            raw = value;
        }
        on |= kind == 1 && pressedNow;
        any |= kind != 0;
    }
    if (is_analog(name) && axis >= 0)
    {
        if (is_pedal(name))
            ImGui::ProgressBar(pedal_level(tokens[axis], raw), ImVec2(-FLT_MIN, ImGui::GetFrameHeight() * 0.6f), "");
        else
        {
            char key[96];
            snprintf(key, sizeof key, "%s_DeadZone", name);
            const char *dz = config_value(key);
            centre_bar("##axis", raw, dz ? atoi(dz) / 32767.0f : 0.0f);
        }
    }
    else if (any && !is_analog(name))
        press_dot(on);

    ImGui::TableSetColumnIndex(3);
    ImGui::BeginDisabled(!wl_controls_running()); // the capture is the controls thread's
    int map = ImGui::Button(wl_tr("Lier")), add = 0;
    ImGui::SameLine();
    ImGui::BeginDisabled(!n); // nothing to add to: "Lier"
    add = ImGui::Button(wl_tr("Ajouter"));
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (map || add)
    {
        g_capture.active = g_capture.requestOpen = 1;
        g_capture.append = add;
        g_capture.section = section;
        snprintf(g_capture.key, sizeof g_capture.key, "%s", row->key);
        g_captureKeys = 1;
        wl_controls_capture(g_mapping.device.player,
                            is_pedal(name) ? WL_CAPTURE_PEDAL : is_analog(name) ? WL_CAPTURE_STEERING : WL_CAPTURE_BUTTON);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!n);
    if (ImGui::Button(wl_tr("Délier")))
    {
        char value[1024];
        replace_device_tokens(row->value, NULL, 0, value, sizeof value);
        set_row(section, row->key, value);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    char def[1024];
    Token mine[MAX_TOKENS];
    int defCount;
    ImGui::BeginDisabled(!default_of_row(section, row, def, sizeof def, mine, &defCount));
    if (ImGui::Button(wl_tr("Défaut")))
        set_row(section, row->key, def);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        char list[1024] = "";
        for (int k = 0; k < defCount; k++)
        {
            describe(mine[k], shown, sizeof shown);
            size_t used = strlen(list);
            snprintf(list + used, sizeof list - used, "%s%s", used ? ", " : "", shown);
        }
        ImGui::SetTooltip(wl_tr("Par défaut : %s"), list[0] ? list : "-");
    }
    ImGui::PopID();
}

// One section's rows on the device's page, under a title bar per player: the rows for all players
// first, then player 1's, player 2's...
static void draw_bindings(int section)
{
    static const char *labels[2] = {WL_N("Tous les jeux"), WL_N("Conduite")};
    int tableOpen = 0;
    for (int player = 0; player <= 4; player++)
    {
        int shown = 0;
        for (int i = 0; i < g_rowCounts[section]; i++)
        {
            int p;
            const char *name = generic_name(g_rows[section][i].key, &p);
            if (p != player)
                continue;
            if (!shown)
            {
                if (tableOpen)
                    ImGui::EndTable();
                char title[96], playerText[32];
                snprintf(playerText, sizeof playerText, wl_tr("Joueur %d"), player);
                if (player)
                    snprintf(title, sizeof title, "%s - %s", wl_tr(labels[section]), playerText);
                else
                    snprintf(title, sizeof title, "%s", wl_tr(labels[section]));
                header(title);
                char id[32];
                snprintf(id, sizeof id, "##rows%d_%d", section, player);
                tableOpen = ImGui::BeginTable(id, 4, ImGuiTableFlags_SizingStretchProp);
                if (tableOpen)
                {
                    ImGui::TableSetupColumn("##action", ImGuiTableColumnFlags_WidthStretch, 0.34f);
                    ImGui::TableSetupColumn("##binding", ImGuiTableColumnFlags_WidthStretch, 0.30f);
                    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 0.22f);
                    ImGui::TableSetupColumn("##buttons", ImGuiTableColumnFlags_WidthFixed);
                }
                shown = 1;
            }
            if (tableOpen)
                binding_row(section, i, name);
        }
    }
    if (tableOpen)
        ImGui::EndTable();
}

// The device's page, over the whole picture: its inputs for every action
static void mapping_page(void)
{
    if (g_mapping.requestOpen)
    {
        ImGui::OpenPopup("###mapping");
        g_mapping.requestOpen = 0;
    }
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowBgAlpha(1.0f);
    char title[192];
    snprintf(title, sizeof title, wl_tr("Liaisons : %s"), g_mapping.device.name);
    size_t used = strlen(title);
    snprintf(title + used, sizeof title - used, "###mapping");
    if (!ImGui::BeginPopupModal(title, NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
        return;
    if (!g_rowsRead)
        read_rows();

    float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    if (ImGui::Button(wl_tr("Fermer")) || g_mapping.requestClose)
    {
        cancel_capture();
        ImGui::CloseCurrentPopup();
    }
    g_mapping.requestClose = 0;
    // On the right: the device's bindings back to the defaults
    const char *resetLabel = wl_tr("Réinitialiser...");
    const ImGuiStyle &style = ImGui::GetStyle();
    ImGui::SameLine(right - ImGui::CalcTextSize(resetLabel).x - style.FramePadding.x * 2);
    if (ImGui::Button(resetLabel))
        ImGui::OpenPopup("##reset");
    if (ImGui::BeginPopupModal("##reset", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    {
        ImGui::TextUnformatted(wl_tr("Remettre les liaisons de ce périphérique par défaut ?"));
        if (ImGui::Button(wl_tr("Oui")))
        {
            reset_device();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(wl_tr("Non")))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginChild("##bindings", ImVec2(0, 0), ImGuiChildFlags_Borders))
    {
        draw_bindings(0);
        draw_bindings(1);
        if (!g_rowCounts[1])
            ImGui::TextDisabled("%s", wl_tr("Cette section de controls.ini est vide."));
    }
    ImGui::EndChild();

    // The capture: the device's next input, written to the row
    if (g_capture.requestOpen)
    {
        ImGui::OpenPopup("##capture");
        g_capture.requestOpen = 0;
    }
    if (ImGui::BeginPopupModal("##capture", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    {
        char got[64];
        int player;
        const char *name = generic_name(g_capture.key, &player);
        if (!g_capture.active)
            ImGui::CloseCurrentPopup();
        else if (wl_controls_capture_result(got, sizeof got))
        {
            for (int i = 0; i < g_rowCounts[g_capture.section]; i++)
                if (!strcmp(g_rows[g_capture.section][i].key, g_capture.key))
                {
                    // "Lier": the device's inputs replaced by it; "Ajouter": it after them, once
                    Token mine[MAX_TOKENS];
                    int k = g_capture.append ? device_tokens(&g_mapping.device, g_rows[g_capture.section][i].value, 1, mine, MAX_TOKENS - 1) : 0, there = 0;
                    for (int t = 0; t < k; t++)
                        there |= !_stricmp(mine[t], got);
                    if (!there)
                        snprintf(mine[k++], sizeof mine[0], "%s", got);
                    char value[1024];
                    replace_device_tokens(g_rows[g_capture.section][i].value, mine, k, value, sizeof value);
                    set_row(g_capture.section, g_capture.key, value);
                }
            g_capture.active = g_captureKeys = 0;
            ImGui::CloseCurrentPopup();
        }
        else
        {
            ImGui::Text(wl_tr("Appuyez sur une commande de %s pour : %s"), g_mapping.device.name, action_label(name));
            if (is_analog(name) && !is_pedal(name))
                ImGui::TextUnformatted(wl_tr("Tournez vers la gauche."));
            ImGui::TextDisabled("%s", wl_tr("Échap : annuler"));
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::Button(wl_tr("Annuler")))
            {
                cancel_capture();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::EndPopup();
}

// The [Config] options but the force feedback's (its own page): dead zones as sliders, toggles as
// boxes, the rest as numbers; written once a change is done
static void draw_options(void)
{
    for (int i = 0; i < g_configCount; i++)
    {
        WlControlRow *row = &g_configRows[i];
        if (!_strnicmp(row->key, "FFB_", 4))
            continue;
        size_t len = strlen(row->key);
        int deadZone = len > 9 && !_stricmp(row->key + len - 9, "_DeadZone");
        int toggle = len > 7 && !_stricmp(row->key + len - 7, "_Toggle");
        char label[160], bare[64];
        snprintf(bare, sizeof bare, "%.*s", (int)(deadZone ? len - 9 : len), row->key);
        snprintf(label, sizeof label, "%s##%s", deadZone ? action_label(bare) : row->key, row->key);
        int changed = 0;
        if (deadZone)
        {
            int value = atoi(row->value);
            if (ImGui::SliderInt(label, &value, 0, 16000))
                snprintf(row->value, sizeof row->value, "%d", value);
            changed = ImGui::IsItemDeactivatedAfterEdit();
        }
        else if (toggle)
        {
            bool value = atoi(row->value) != 0;
            if ((changed = ImGui::Checkbox(label, &value)) != 0)
                snprintf(row->value, sizeof row->value, "%d", value ? 1 : 0);
        }
        else if (strchr(row->value, '.'))
        {
            float value = (float)atof(row->value);
            if (ImGui::InputFloat(label, &value, 0.05f, 0.1f, "%.2f"))
                snprintf(row->value, sizeof row->value, "%.2f", value);
            changed = ImGui::IsItemDeactivatedAfterEdit();
        }
        else
        {
            int value = atoi(row->value);
            if (ImGui::InputInt(label, &value))
                snprintf(row->value, sizeof row->value, "%d", value);
            changed = ImGui::IsItemDeactivatedAfterEdit();
        }
        if (changed)
            wl_controls_set_row("Config", row->key, row->value);
    }
}

// What the game gets of each of its inputs (windowsloader's: the raw value its board reads)
static void draw_value(const WlInput *in)
{
    int raw = wl_input_get(in);
    if (in->type == WL_INPUT_SWITCH)
    {
        ImGui::TextUnformatted(raw != in->rawLow ? wl_tr("appuyé") : "—");
        return;
    }
    float f = in->rawHigh != in->rawLow ? (float)(raw - in->rawLow) / (float)(in->rawHigh - in->rawLow) : 0;
    char text[16];
    snprintf(text, sizeof text, "%d", raw);
    ImGui::ProgressBar(f < 0 ? 0 : f > 1 ? 1 : f, ImVec2(-1, 0), text);
}

static void page_controls(void)
{
    if (!wl_controls_running())
        ImGui::TextWrapped("%s", wl_tr("Le jeu est piloté par l'agent (--agent) : les manettes ne sont pas lues, l'assignation est désactivée."));
    if (!wl_controls_has_file())
        return;
    if (ImGui::IsWindowAppearing() || !g_rowsRead)
        read_rows();

    header(wl_tr("Périphériques"));
    WlControlDevice devices[16];
    int count = wl_controls_devices(devices, 16);
    if (ImGui::BeginTable("##devices", 3, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn(wl_tr("Nom"), ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn(wl_tr("Dans controls.ini"), ImGuiTableColumnFlags_WidthStretch, 0.4f);
        ImGui::TableSetupColumn("##map", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (int i = 0; i < count; i++)
        {
            WlControlDevice *d = &devices[i];
            if (d->player < 0)
                snprintf(d->name, sizeof d->name, "%s", wl_tr("Clavier"));
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s%s", d->name, d->ffb ? wl_tr(" (retour de force)") : "");
            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            if (d->player < 0)
                ImGui::TextDisabled("KEY");
            else if (d->gamepad)
                ImGui::TextDisabled("GC%d / JOY%d", d->player, d->player);
            else
                ImGui::TextDisabled("JOY%d", d->player);
            ImGui::TableSetColumnIndex(2);
            ImGui::BeginDisabled(!wl_controls_running());
            if (ImGui::Button(wl_tr("Lier")))
            {
                g_mapping.device = *d;
                g_mapping.requestOpen = 1;
                g_rowsRead = 0;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (count < 2)
        ImGui::TextDisabled("%s", wl_tr("Aucune manette branchée."));
    LONG map = InterlockedExchange(&g_mapRequest, -2); // the agent's "menu map N"
    if (map >= 0 && map < count)
    {
        g_mapping.device = devices[map];
        g_mapping.requestOpen = 1;
        g_rowsRead = 0;
    }
    else if (map == -1)
        g_mapping.requestClose = 1;

    // windowsloader's: what each input of the game gets now, as its board reads it
    header(wl_tr("Ce que reçoit le jeu"));
    if (ImGui::BeginTable("##game", 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("##input", ImGuiTableColumnFlags_WidthStretch, 0.4f);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        for (int i = 0; i < wl_inputs_count(); i++)
        {
            WlInput *in = wl_input_at(i);
            if (!in->action)
                continue;
            int player;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(action_label(generic_name(in->action, &player)));
            ImGui::TableSetColumnIndex(1);
            draw_value(in);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    header(wl_tr("Zones mortes et options"));
    draw_options();

    mapping_page();
}

static void page_ffb(void)
{
    double strength;
    int invert;
    wl_controls_ffb_settings(&strength, &invert);
    const char *pad = wl_controls_player_name(0);
    ImGui::Text(wl_tr("Volant : %s"), pad[0] ? pad : wl_tr("aucun (joueur 1)"));
    // The device the effects go to: Automatique (player 1's wheel) or one by its name
    char device[128], names[16][128], label[160], playing[128];
    int connected = wl_controls_ffb_device(device, sizeof device);
    int count = wl_controls_ffb_devices(names, 16);
    if (!device[0])
        snprintf(label, sizeof label, "%s", wl_tr("Automatique"));
    else
        snprintf(label, sizeof label, connected ? "%s" : wl_tr("%s (non connecté)"), device);
    if (ImGui::BeginCombo(wl_tr("Dispositif de retour de force"), label))
    {
        if (ImGui::Selectable(wl_tr("Automatique"), !device[0]))
            wl_controls_set_ffb_device("");
        for (int i = 0; i < count; i++)
        {
            ImGui::PushID(i);
            if (ImGui::Selectable(names[i], device[0] && !_stricmp(names[i], device)))
                wl_controls_set_ffb_device(names[i]);
            ImGui::PopID();
        }
        if (device[0] && !connected)
            ImGui::Selectable(label, true);
        ImGui::EndCombo();
    }
    wl_controls_ffb_playing(playing, sizeof playing);
    ImGui::TextDisabled(wl_tr("Effets joués sur : %s%s"), playing[0] ? playing : wl_tr("aucun"),
                        device[0] && !connected ? wl_tr(" (le choisi n'est pas branché : automatique)") : "");
    ImGui::Spacing();
    int pct = (int)(strength * 100 + 0.5);
    if (ImGui::SliderInt(wl_tr("Force"), &pct, 0, 200, "%d %%"))
        wl_controls_set_ffb(pct / 100.0, invert);
    bool inv = invert != 0;
    if (ImGui::Checkbox(wl_tr("Inverser le sens"), &inv))
        wl_controls_set_ffb(strength, inv);
    ImGui::Spacing();
    double f = wl_ffb_get(NULL);
    char text[64];
    snprintf(text, sizeof text, "%+.2f (%s)", f, f > 0 ? wl_tr("vers la gauche") : f < 0 ? wl_tr("vers la droite") : wl_tr("aucune"));
    ImGui::TextUnformatted(wl_tr("Force demandée par le jeu :"));
    ImGui::ProgressBar((float)(1 - f) / 2, ImVec2(-1, 0), text);
    ImGui::TextDisabled("%s", wl_tr("Réglages enregistrés dans [Config] de controls.ini (FFB_Device, FFB_Strength, FFB_Invert)."));
}

// Display: linuxloader's settings, applied at the next frame (the resolution at the next start)
static void page_display(void)
{
    // linuxloader's seven choices (4:3_LETTERBOX shown as 4:3)
    static const int choices[] = {WL_ASPECT_ORIGINAL,     WL_ASPECT_4_3,      WL_ASPECT_16_9,     WL_ASPECT_4_3_STRETCH,
                                  WL_ASPECT_16_9_STRETCH, WL_ASPECT_4_3_CROP, WL_ASPECT_16_9_CROP};
    static const char *aspectTexts[] = {WL_N("Original"),   WL_N("4:3 bandes noires"), WL_N("16:9 bandes noires"), WL_N("4:3 étiré"),
                                        WL_N("16:9 étiré"), WL_N("4:3 recadré"),       WL_N("16:9 recadré")};
    static const char *modeTexts[WL_MODE_COUNT] = {WL_N("Fenêtre (pixels carrés)"),           WL_N("Fenêtre étirée"),
                                                   WL_N("Plein écran"),                       WL_N("Plein écran étiré"),
                                                   WL_N("Plein écran fenêtré (sans bordure)"), WL_N("Plein écran fenêtré étiré")};
    // In the language shown
    const char *aspects[sizeof aspectTexts / sizeof aspectTexts[0]], *modes[WL_MODE_COUNT];
    for (int i = 0; i < (int)(sizeof aspects / sizeof aspects[0]); i++)
        aspects[i] = wl_tr(aspectTexts[i]);
    for (int i = 0; i < WL_MODE_COUNT; i++)
        modes[i] = wl_tr(modeTexts[i]);
    static const int sizes[][2] = {{0, 0},     {640, 480},   {800, 600},   {1024, 768},  {1280, 720},  {1280, 1024}, {1360, 768},
                                   {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
    int aspect = wl_display_aspect(), mode = wl_display_mode(), choice = 0;
    for (int i = 0; i < (int)(sizeof choices / sizeof choices[0]); i++)
        if (choices[i] == (aspect == WL_ASPECT_4_3_LETTERBOX ? WL_ASPECT_4_3 : aspect))
            choice = i;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    if (ImGui::Combo(wl_tr("Rapport d'aspect"), &choice, aspects, (int)(sizeof aspects / sizeof aspects[0])))
        wl_display_set(choices[choice], mode);
    aspect = wl_display_aspect();
    if (aspect == WL_ASPECT_4_3_CROP || aspect == WL_ASPECT_16_9_CROP)
    {
        // Read at each frame: the picture follows the sliders
        int position, barrel, center, radius;
        wl_display_crop(&position, &barrel, &center, &radius);
        bool bend = barrel != 0;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        ImGui::SliderInt(wl_tr("Position du recadrage"), &position, -100, 100);
        ImGui::TextDisabled("%s", wl_tr("-100 : à gauche ou en haut, 0 : au milieu, 100 : à droite ou en bas."));
        ImGui::Checkbox(wl_tr("Courber les parties coupées sur les bords"), &bend);
        if (bend)
        {
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
            ImGui::SliderInt(wl_tr("Milieu non déformé"), &center, 0, 100, "%d %%");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
            ImGui::SliderInt(wl_tr("Rayon de courbure"), &radius, 10, 300, "%d %%");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", wl_tr("Toute l'image reste visible : le milieu (en pourcentage du cadre) tel que recadré, les parties que le recadrage "
                                            "couperait resserrées sur les bords comme sur un cylindre vu de face. Un petit rayon courbe fort au "
                                            "bord, un grand resserre uniformément."));
            if (!wl_display_gdi())
                ImGui::TextDisabled("%s", wl_tr("Courbure seulement quand windowsloader recopie l'image (PRESENT = GDI) : ici l'image est seulement "
                                                "recadrée."));
            ImGui::PopTextWrapPos();
        }
        wl_display_set_crop(position, bend, center, radius);
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", wl_tr("Original : la forme de la résolution de rendu du jeu. Bandes noires : la forme du cadre, l'image entière "
                                    "dedans à sa forme. Étiré : l'image remplit le cadre, déformée. Recadré : l'image remplit le cadre à sa "
                                    "forme, coupée sur les côtés (ou en haut et en bas)."));
    ImGui::PopTextWrapPos();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    if (ImGui::Combo(wl_tr("Mode d'affichage"), &mode, modes, WL_MODE_COUNT))
        wl_display_set(aspect, mode);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", wl_tr("Plein écran : l'écran passe à la taille de l'image du jeu. Plein écran fenêtré : une fenêtre sans "
                                    "bordure couvre l'écran, à la résolution du bureau. F11 ou Alt+Entrée : fenêtre / plein écran."));
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    int w, h, sw, sh, current = -1;
    wl_display_internal(&w, &h);
    wl_display_internal_next(&sw, &sh);
    char saved[32];
    snprintf(saved, sizeof saved, "%s", wl_tr("Native"));
    if (sw > 0 && sh > 0)
        snprintf(saved, sizeof saved, "%dx%d", sw, sh);
    for (int i = 0; i < (int)(sizeof sizes / sizeof sizes[0]); i++)
        if (sizes[i][0] == sw && sizes[i][1] == sh)
            current = i;
    ImGui::BeginDisabled(!wl_display_internal_supported());
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    if (ImGui::BeginCombo(wl_tr("Résolution interne"), saved))
    {
        for (int i = 0; i < (int)(sizeof sizes / sizeof sizes[0]); i++)
        {
            char name[32];
            if (sizes[i][0])
                snprintf(name, sizeof name, "%dx%d", sizes[i][0], sizes[i][1]);
            else
                snprintf(name, sizeof name, "%s", wl_tr("Native"));
            if (ImGui::Selectable(name, i == current))
                wl_display_set_internal(sizes[i][0], sizes[i][1]);
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", wl_tr("Taille de calcul et de rendu du jeu, avant l'aspect et le mode.\nPrise en compte au prochain lancement."));
    if (!wl_display_internal_supported())
        ImGui::TextDisabled("%s", wl_tr("Pas encore prise en charge pour ce jeu."));
    else
        ImGui::TextDisabled(wl_tr("Rendu actuel : %dx%d (%s). Prise en compte au prochain lancement."), g_bbW, g_bbH,
                            w ? wl_tr("réglages") : wl_tr("réglage du jeu"));
    ImGui::TextDisabled("%s", wl_tr("Gardé par « Sauvegarder » dans [Display] du fichier de réglages."));
}

// The settings file: "Sauvegarder" to the file read, "Sauvegarder pour ce jeu" to the game's own
// <short name>.ini (a copy of the file read the first time, then read in its place at each start
// and the file the next saves go to), as linuxloader's buttons
int wl_menu_save(int forGame, char *status, size_t size)
{
    char u[MAX_PATH * 2];
    const wchar_t *file = forGame ? wl_settings_game_file(1) : wl_settings_file();
    if (forGame && !file)
    {
        const wchar_t *own = wl_settings_game_file(0);
        if (own)
            snprintf(status, size, wl_tr("Impossible d'écrire %s"), wl_u8(own, u, sizeof u));
        else
            snprintf(status, size, "%s", wl_tr("Pas de jeu reconnu"));
        return 0;
    }
    if (!file[0])
    {
        snprintf(status, size, "%s", wl_tr("Pas de fichier de réglages (lancé sans windowsloader.exe)"));
        return 0;
    }
    int ok = wl_display_save(file);
    ok &= wl_game_options_save(file);
    ok &= wl_serial_save(file);
    snprintf(status, size, ok ? (forGame ? wl_tr("Enregistré dans %s (ce jeu seulement)") : wl_tr("Enregistré dans %s")) : wl_tr("Impossible d'écrire %s"),
             wl_u8(file, u, sizeof u));
    return ok;
}

static void save_buttons(void)
{
    static char status[MAX_PATH * 2 + 64];
    char u[MAX_PATH * 2];
    if (ImGui::Button(wl_tr("Sauvegarder")))
        wl_menu_save(0, status, sizeof status);
    if (wl_settings_game_file(0))
    {
        ImGui::SameLine();
        if (ImGui::Button(wl_tr("Sauvegarder pour ce jeu")))
            wl_menu_save(1, status, sizeof status);
    }
    ImGui::SameLine();
    if (ImGui::Button(wl_tr("Fermer")))
        InterlockedExchange(&g_open, 0);
    if (status[0])
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", status);
    }
    else
    {
        ImGui::SameLine();
        ImGui::TextDisabled(wl_tr("Réglages : %s"), wl_u8(wl_settings_file(), u, sizeof u));
    }
}

// The game's title; "jeu inconnu" (game.cpp's) in the language shown
static const char *game_title(void)
{
    return strcmp(wl_game_name(), "unknown") ? wl_game_title() : wl_tr(wl_game_title());
}

static void page_game(void)
{
    ImGui::Text(wl_tr("%s est en pause."), game_title());
    // The cabinet's boards, as the game's module describes them
    const char *boards[2] = {wl_game_controls(), wl_game_motor()};
    for (int i = 0; i < 2; i++)
        if (boards[i])
        {
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextWrapped("%s", boards[i]);
        }
    ImGui::Spacing();
    if (ImGui::Button(wl_tr("Reprendre (Tab)")))
        InterlockedExchange(&g_open, 0);
    ImGui::Spacing();
    RECT rc = {};
    GetClientRect(g_wnd, &rc);
    ImGui::TextDisabled(wl_tr("Image %dx%d, fenêtre %ldx%ld."), g_bbW, g_bbH, rc.right, rc.bottom);
    ImGui::Spacing();
    if (ImGui::Button(wl_tr("Quitter le jeu")))
        ImGui::OpenPopup("###quit");
    char title[64];
    snprintf(title, sizeof title, "%s###quit", wl_tr("Quitter"));
    if (ImGui::BeginPopupModal(title, NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(wl_tr("Quitter le jeu ?"));
        if (ImGui::Button(wl_tr("Oui")))
        {
            wl_log("menu: quit");
            TerminateProcess(GetCurrentProcess(), 0);
        }
        ImGui::SameLine();
        if (ImGui::Button(wl_tr("Non")) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// The PC's serial ports, for the passthrough lists: COM1..COM255 that Windows knows now
static int pc_ports(char ports[][16], int max)
{
    static wchar_t all[65536];
    int n = 0;
    if (!QueryDosDeviceW(NULL, all, sizeof all / sizeof all[0]))
        return 0;
    for (const wchar_t *p = all; *p && n < max; p += wcslen(p) + 1)
        if (!_wcsnicmp(p, L"COM", 3) && iswdigit(p[3]))
            snprintf(ports[n++], 16, "%S", p);
    // In their order: COM2 before COM10
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && atoi(ports[j - 1] + 3) > atoi(ports[j] + 3); j--)
        {
            char t[16];
            memcpy(t, ports[j], 16);
            memcpy(ports[j], ports[j - 1], 16);
            memcpy(ports[j - 1], t, 16);
        }
    return n;
}

// "Cartes": each serial port the game finds emulated, or a real board on one of the PC's ports
// instead ([Emulation] and [Paths], as linuxloader's), taken at the next start. 1 when one changed.
static int page_boards(void)
{
    int count = wl_serial_count(), pending = 0;
    if (!count)
        return 0;
    static char ports[64][16];
    static int portCount = -1;
    if (ImGui::IsWindowAppearing() || portCount < 0)
        portCount = pc_ports(ports, 64);
    ImGui::Spacing();
    ImGui::SeparatorText(wl_tr("Cartes"));
    for (int k = 0; k < count; k++)
    {
        char *value = wl_serial_edit(k), label[160];
        snprintf(label, sizeof label, wl_tr("%s (%s du jeu)"), wl_tr(wl_serial_label(k)), wl_serial_name(k));
        ImGui::PushID(1000 + k);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
        if (ImGui::BeginCombo(label, value[0] ? value : wl_tr("Émulée")))
        {
            if (ImGui::Selectable(wl_tr("Émulée"), !value[0]))
                value[0] = 0;
            for (int i = 0; i < portCount; i++)
            {
                char item[64];
                snprintf(item, sizeof item, wl_tr("Vraie carte sur %s"), ports[i]);
                if (ImGui::Selectable(item, !_stricmp(value, ports[i])))
                    snprintf(value, 32, "%s", ports[i]);
            }
            ImGui::EndCombo();
        }
        if (_stricmp(value, wl_serial_started(k)))
            pending = 1;
        ImGui::PopID();
    }
    ImGui::Indent();
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", wl_tr("Au prochain lancement. Émulée : windowsloader joue la carte. Un port du PC : le jeu parle à la vraie "
                                    "carte branchée dessus ([Emulation] EMULATE_JVS ou EMULATE_DRIVEBOARD = false, [Paths] JVS_PATH ou "
                                    "SERIAL_n_PATH)."));
    ImGui::PopTextWrapPos();
    ImGui::Unindent();
    return pending;
}

// The game's own options (game.h), declared by its module: a box, a list or a slider each, its
// help below; those taken at the next start say so, and a reminder comes when one was changed
static void page_game_options(void)
{
    char u[MAX_PATH * 2];
    const wchar_t *file = wl_settings_file(), *base = wcsrchr(file, L'\\');
    ImGui::TextUnformatted(game_title());
    ImGui::TextDisabled(wl_tr("[%s] de %s, pour ce jeu seulement"), wl_game_option_section(),
                        file[0] ? wl_u8(base ? base + 1 : file, u, sizeof u) : wl_tr("(pas de fichier)"));
    ImGui::Separator();
    int count = wl_game_option_count(), pending = 0;
    if (!count && !wl_serial_count())
        ImGui::TextUnformatted(wl_tr("Aucune option pour ce jeu."));
    // The module's texts (WL_N) in the language shown
    for (int i = 0; i < count; i++)
    {
        const WlGameOption *o = wl_game_option_at(i);
        char *value = wl_game_option_edit(i), was[WL_OPTION_VALUE_SIZE];
        memcpy(was, value, sizeof was);
        ImGui::PushID(i);
        if (o->type == WL_OPTION_BOOL)
        {
            bool on = !_stricmp(value, "true");
            if (ImGui::Checkbox(wl_tr(o->label), &on))
                snprintf(value, WL_OPTION_VALUE_SIZE, "%s", on ? "true" : "false");
        }
        else if (o->type == WL_OPTION_CHOICE)
        {
            const char *shown = value;
            for (const WlOptionChoice *c = o->choices; c && c->value; c++)
                if (!_stricmp(c->value, value))
                    shown = wl_tr(c->label);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
            if (ImGui::BeginCombo(wl_tr(o->label), shown))
            {
                for (const WlOptionChoice *c = o->choices; c && c->value; c++)
                    if (ImGui::Selectable(wl_tr(c->label), !_stricmp(c->value, value)))
                        snprintf(value, WL_OPTION_VALUE_SIZE, "%s", c->value);
                ImGui::EndCombo();
            }
        }
        else
        {
            int n = atoi(value);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
            if (ImGui::SliderInt(wl_tr(o->label), &n, o->min, o->max))
                snprintf(value, WL_OPTION_VALUE_SIZE, "%d", n);
        }
        if (o->nextStart)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", wl_tr("(au prochain lancement)"));
            if (_stricmp(value, wl_game_option_started(i)))
                pending = 1;
        }
        else if (o->apply && strcmp(was, value))
            o->apply(value);
        if (o->help)
        {
            ImGui::Indent();
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("%s", wl_tr(o->help));
            ImGui::PopTextWrapPos();
            ImGui::Unindent();
        }
        ImGui::Spacing();
        ImGui::PopID();
    }
    pending |= page_boards();
    if (pending)
    {
        ImGui::Separator();
        ImGui::TextWrapped("%s", wl_tr("Des changements ne prendront effet qu'au prochain lancement du jeu : enregistrez-les (« Sauvegarder » "
                                       "ou « Sauvegarder pour ce jeu »), puis relancez le jeu."));
    }
}

int wl_menu_outputs(int on, char *status, size_t size)
{
    if (!wl_outputs_enable(on))
    {
        snprintf(status, size, "%s", wl_tr("Le serveur précédent s'arrête encore : réessayez dans un instant."));
        return 0;
    }
    char u[MAX_PATH * 2];
    const wchar_t *file = wl_settings_file();
    const char *value = on ? "true" : "false";
    if (!file[0])
        snprintf(status, size, "%s", wl_tr("Pas de fichier de réglages (lancé sans windowsloader.exe) : pour cette fois seulement"));
    else if (wl_settings_write(file, "Outputs", "OUTPUTS_NETWORK", value))
        snprintf(status, size, wl_tr("[Outputs] OUTPUTS_NETWORK = %s, enregistré dans %s"), value, wl_u8(file, u, sizeof u));
    else
        snprintf(status, size, wl_tr("Impossible d'écrire %s"), wl_u8(file, u, sizeof u));
    wl_log("menu: network outputs %s", on ? "on" : "off");
    return 1;
}

// "Sorties et réseau": what the game can send to BackForceFeeder or MAMEHooker ([Outputs]
// OUTPUTS_NETWORK, outputs.cpp), each value's type and its last one, in a list of its own that
// scrolls. The box starts or stops the server at once, the game going on
static void page_outputs(void)
{
    int tcp = 0, udp = 0, clients = 0;
    static char status[MAX_PATH * 2 + 96];
    bool on = wl_outputs_enabled() != 0;
    if (ImGui::Checkbox(wl_tr("Envoyer les sorties réseau (OUTPUTS_NETWORK)"), &on))
        wl_menu_outputs(on, status, sizeof status);
    ImGui::PushTextWrapPos(0);
    if (status[0])
        ImGui::TextDisabled("%s", status);
    int error = wl_outputs_error();
    if (wl_outputs_server(&tcp, &udp, &clients))
    {
        if (udp > 0)
            ImGui::Text(wl_tr("Sorties réseau actives : port TCP %d, %d client(s) connecté(s), début annoncé à 127.0.0.1:%d (UDP)."), tcp, clients,
                        udp);
        else
            ImGui::Text(wl_tr("Sorties réseau actives : port TCP %d, %d client(s) connecté(s)."), tcp, clients);
    }
    else if (wl_outputs_enabled())
        ImGui::Text(wl_tr("Sorties réseau : ouverture du port TCP %d..."), tcp);
    else if (error == 10048) // WSAEADDRINUSE
        ImGui::Text(wl_tr("Sorties réseau coupées : le port TCP %d est déjà pris par un autre programme."), tcp);
    else if (error)
        ImGui::Text(wl_tr("Sorties réseau coupées : le port TCP %d n'a pas pu être ouvert (erreur %d)."), tcp, error);
    else
        ImGui::TextUnformatted(wl_tr("Sorties réseau coupées. Les valeurs ci-dessous sont celles que le jeu donne."));
    ImGui::TextDisabled("%s", wl_tr("Lignes « nom = valeur » terminées par \\r, comme MAME (BackForceFeeder, MAMEHooker)."));
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    static WlOutputInfo list[256];
    int n = wl_outputs_list(list, 256);
    float height = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2;
    if (height < ImGui::GetTextLineHeightWithSpacing() * 8)
        height = ImGui::GetTextLineHeightWithSpacing() * 8;
    ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV |
                            ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("outputs", 4, flags, ImVec2(0, height)))
    {
        float em = ImGui::GetFontSize();
        ImGui::TableSetupScrollFreeze(0, 1); // the titles stay
        ImGui::TableSetupColumn(wl_tr("Nom"), ImGuiTableColumnFlags_WidthFixed, em * 7);
        ImGui::TableSetupColumn(wl_tr("Type"), ImGuiTableColumnFlags_WidthFixed, em * 10);
        ImGui::TableSetupColumn(wl_tr("Valeur"), ImGuiTableColumnFlags_WidthFixed, em * 6);
        ImGui::TableSetupColumn(wl_tr("Description"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (int i = 0; i < n; i++)
        {
            const WlOutputInfo *o = &list[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(o->name);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(o->type);
            ImGui::TableNextColumn();
            if (o->text)
                ImGui::TextUnformatted(o->text);
            else if (o->known)
                ImGui::Text("%d", o->value);
            else
                ImGui::TextDisabled("—");
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", o->about);
        }
        ImGui::EndTable();
    }
    if (n <= 2)
        ImGui::TextDisabled("%s", wl_tr("Ce jeu n'envoie ni lampes ni carte moteur pour l'instant."));
}

static void page_about(void)
{
    ImGui::Text(wl_tr("windowsloader %s %s (%d bits)"), WL_BUILD_VERSION, WL_EDITION, (int)sizeof(void *) * 8);
    ImGui::TextUnformatted(wl_tr("Benjamin Maurin - njz3, licence GPL v3."));
    ImGui::Spacing();
    ImGui::TextUnformatted(wl_tr("Avec :"));
    ImGui::BulletText("Dear ImGui, Omar Cornut (MIT)");
    ImGui::BulletText("%s", wl_tr("MinHook, Tsuda Kageyu (BSD 2 clauses)"));
    ImGui::BulletText("SDL3 (zlib)");
    ImGui::Spacing();
    ImGui::TextDisabled("%s", wl_tr("Conventions de linuxloader : controls.ini, menu Tab, mode agent."));

    // The interface's language (lang.h): at once, and written at once to the launcher's file, which
    // the launcher reads it from
    ImGui::Spacing();
    ImGui::SeparatorText(wl_tr("Langue"));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
    static char status[MAX_PATH * 2 + 64];
    if (wl_lang_combo(wl_tr("Langue de l'interface")))
    {
        char u[MAX_PATH * 2];
        const wchar_t *file = wl_settings_launcher_file();
        if (wl_settings_write(file, "Launcher", "LANGUAGE", wl_lang_setting()))
            snprintf(status, sizeof status, wl_tr("[Launcher] LANGUAGE = %s, enregistré dans %s"), wl_lang_setting(), wl_u8(file, u, sizeof u));
        else
            snprintf(status, sizeof status, "%s", wl_tr("Pas de fichier de réglages (lancé sans windowsloader.exe) : pour cette fois seulement"));
        wl_log("menu: language %s (LANGUAGE = %s)", wl_lang_code(), wl_lang_setting());
    }
    ImGui::PushTextWrapPos(0);
    if (status[0])
        ImGui::TextDisabled("%s", status);
    ImGui::TextDisabled("%s", wl_tr("Les textes des jeux décrits au lancement (sorties réseau) changent de langue au prochain lancement. "
                                    "Une autre langue : un fichier lang\\<code>.po à côté du lanceur (voir README.md)."));
    ImGui::PopTextWrapPos();
}

static void draw_menu(void)
{
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.64f, io.DisplaySize.y * 0.74f), ImGuiCond_Always);
    char title[128];
    snprintf(title, sizeof title, "windowsloader — %s###menu", game_title());
    if (ImGui::Begin(title, NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImVec2 at = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        SetRect(&g_menuRect, (int)at.x - 2, (int)at.y - 2, (int)(at.x + size.x) + 2, (int)(at.y + size.y) + 2);
        int tab = InterlockedExchange(&g_tabRequest, -1);
        // The pages above, the settings file's buttons below
        ImGui::BeginChild("pages", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y));
        if (ImGui::BeginTabBar("pages"))
        {
            // Each page's name in the language shown, its ID (###...) the same in all: the page
            // stays the one shown when the language changes
            static const struct
            {
                const char *name, *id;
                int tab;
                void (*draw)(void);
            } pages[] = {
                {WL_N("Jeu"), "###game", 0, page_game},
                {WL_N("Options du jeu"), "###options", 5, page_game_options},
                {WL_N("Contrôles"), "###controls", 1, page_controls},
                {WL_N("Affichage"), "###display", 2, page_display},
                {WL_N("Retour de force"), "###ffb", 3, page_ffb},
                {WL_N("Sorties et réseau"), "###outputs", 6, page_outputs},
                {WL_N("À propos"), "###about", 4, page_about},
            };
            for (int i = 0; i < (int)(sizeof pages / sizeof pages[0]); i++)
            {
                char name[96];
                snprintf(name, sizeof name, "%s%s", wl_tr(pages[i].name), pages[i].id);
                if (ImGui::BeginTabItem(name, NULL, tab == pages[i].tab ? ImGuiTabItemFlags_SetSelected : 0))
                {
                    pages[i].draw();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
        ImGui::Separator();
        save_buttons();
    }
    ImGui::End();
}

// ------------------------------------------------------------------------------
// The menu's frames
// ------------------------------------------------------------------------------

// While the menu is open: the window's messages, Tab or the agent to close it, a frame each time
static void run_menu(void (*frame)(void *), void *arg)
{
    wl_log("menu: open");
    InterlockedExchange(&g_open, 1);
    wl_pause_begin();
    while (g_open)
    {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                PostQuitMessage((int)msg.wParam); // for the game's own loop
                InterlockedExchange(&g_open, 0);
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (menu_key_pressed() || InterlockedExchange(&g_request, -1) == 0)
            InterlockedExchange(&g_open, 0);
        if (g_open)
            frame(arg);
    }
    cancel_capture();
    wl_pause_end();
    wl_log("menu: closed");
}

// ------------------------------------------------------------------------------
// Direct3D 9
// ------------------------------------------------------------------------------

struct Frame9
{
    IDirect3DDevice9 *dev;
    IDirect3DSurface9 *frozen;
    WlPresentFn present;
};

static void render_frame(void *arg)
{
    Frame9 *f = (Frame9 *)arg;
    IDirect3DDevice9 *dev = f->dev;
    IDirect3DSurface9 *frozen = f->frozen;
    IDirect3DSurface9 *bb = NULL, *oldTarget = NULL;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)))
    {
        Sleep(16);
        return;
    }
    D3DVIEWPORT9 vp;
    dev->GetViewport(&vp);
    dev->GetRenderTarget(0, &oldTarget);
    dev->SetRenderTarget(0, bb);
    if (frozen)
        dev->StretchRect(frozen, NULL, bb, NULL, D3DTEXF_NONE);
    else
        dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    feed_mouse();
    ImGui::NewFrame();
    draw_menu();
    ImGui::Render();
    if (SUCCEEDED(dev->BeginScene()))
    {
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        dev->EndScene();
    }
    wl_d3d9_take_pending_shot(dev); // the agent's pictures show the menu too

    if (oldTarget)
    {
        dev->SetRenderTarget(0, oldTarget);
        oldTarget->Release();
    }
    dev->SetViewport(&vp);
    bb->Release();
    const RECT *place = wl_display_dest();
    if (FAILED(f->present(dev, wl_display_source(), place, NULL, NULL)))
        Sleep(16);
}

void wl_menu_present(IDirect3DDevice9 *dev, WlPresentFn present)
{
    int want = menu_key_pressed();
    if (InterlockedExchange(&g_request, -1) == 1)
        want = 1;
    if (!want)
        return;

    IDirect3DSurface9 *bb = NULL;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)))
        return;
    D3DSURFACE_DESC d;
    bb->GetDesc(&d);
    g_bbW = (int)d.Width;
    g_bbH = (int)d.Height;
    if (!g_ready)
        g_ready = setup(dev) ? 1 : -1;
    if (g_ready < 0 || g_backend != 9)
    {
        bb->Release();
        return;
    }

    // The game's frame, drawn under the menu and given back to the game's Present afterwards
    IDirect3DSurface9 *frozen = NULL;
    if (SUCCEEDED(dev->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &frozen, NULL)))
        dev->StretchRect(bb, NULL, frozen, NULL, D3DTEXF_NONE);
    bb->Release();

    Frame9 f = {dev, frozen, present};
    run_menu(render_frame, &f);

    if (frozen)
    {
        if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)))
        {
            dev->StretchRect(frozen, NULL, bb, NULL, D3DTEXF_NONE);
            bb->Release();
        }
        frozen->Release();
    }
}

// ------------------------------------------------------------------------------
// Direct3D 11
// ------------------------------------------------------------------------------

struct Frame11
{
    IDXGISwapChain *sc;
    ID3D11Device *dev;
    ID3D11DeviceContext *ctx;
    ID3D11Texture2D *frozen;
    WlPresent11Fn present;
};

static void render_frame11(void *arg)
{
    Frame11 *f = (Frame11 *)arg;
    ID3D11Texture2D *bb = NULL;
    if (FAILED(f->sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb)))
    {
        Sleep(16);
        return;
    }
    // The game's render target and viewport, put back after the menu's frame
    ID3D11RenderTargetView *oldRtv = NULL, *rtv = NULL;
    ID3D11DepthStencilView *oldDsv = NULL;
    D3D11_VIEWPORT oldVp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT vpCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    f->ctx->OMGetRenderTargets(1, &oldRtv, &oldDsv);
    f->ctx->RSGetViewports(&vpCount, oldVp);

    if (f->frozen)
        f->ctx->CopyResource(bb, f->frozen);
    if (SUCCEEDED(f->dev->CreateRenderTargetView(bb, NULL, &rtv)))
    {
        if (!f->frozen)
        {
            const float black[4] = {0, 0, 0, 1};
            f->ctx->ClearRenderTargetView(rtv, black);
        }
        f->ctx->OMSetRenderTargets(1, &rtv, NULL);
        D3D11_VIEWPORT vp = {0, 0, (float)g_bbW, (float)g_bbH, 0, 1};
        f->ctx->RSSetViewports(1, &vp);
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        feed_mouse();
        ImGui::NewFrame();
        draw_menu();
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        rtv->Release();
    }
    wl_d3d11_take_pending_shot(f->sc); // the agent's pictures show the menu too

    f->ctx->OMSetRenderTargets(1, &oldRtv, oldDsv);
    f->ctx->RSSetViewports(vpCount, oldVp);
    if (oldRtv)
        oldRtv->Release();
    if (oldDsv)
        oldDsv->Release();
    bb->Release();
    wl_display_dest(); // F11 and Alt+Enter work in the menu too
    HRESULT hr = f->present(f->sc, 1, 0);
    if (FAILED(hr) || hr == DXGI_STATUS_OCCLUDED)
        Sleep(16); // nothing shown, no vertical sync to wait for
}

void wl_menu_present11(IDXGISwapChain *sc, WlPresent11Fn present)
{
    int want = menu_key_pressed();
    if (InterlockedExchange(&g_request, -1) == 1)
        want = 1;
    if (!want)
        return;

    ID3D11Texture2D *bb = NULL;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb)))
        return;
    D3D11_TEXTURE2D_DESC d;
    bb->GetDesc(&d);
    g_bbW = (int)d.Width;
    g_bbH = (int)d.Height;
    Frame11 f = {sc, NULL, NULL, NULL, present};
    bb->GetDevice(&f.dev);
    f.dev->GetImmediateContext(&f.ctx);
    if (!g_ready)
    {
        DXGI_SWAP_CHAIN_DESC sd;
        g_ready = SUCCEEDED(sc->GetDesc(&sd)) && setup11(sd.OutputWindow, f.dev, f.ctx) ? 1 : -1;
    }
    if (g_ready > 0 && g_backend == 11)
    {
        // The game's frame, drawn under the menu and given back to the game's Present afterwards
        D3D11_TEXTURE2D_DESC fd = d;
        fd.Usage = D3D11_USAGE_DEFAULT;
        fd.BindFlags = d.SampleDesc.Count > 1 ? D3D11_BIND_RENDER_TARGET : 0;
        fd.CPUAccessFlags = 0;
        fd.MiscFlags = 0;
        if (SUCCEEDED(f.dev->CreateTexture2D(&fd, NULL, &f.frozen)))
            f.ctx->CopyResource(f.frozen, bb);
        bb->Release();
        bb = NULL;

        run_menu(render_frame11, &f);

        if (f.frozen)
        {
            if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb)))
                f.ctx->CopyResource(bb, f.frozen);
            f.frozen->Release();
        }
    }
    if (bb)
        bb->Release();
    f.ctx->Release();
    f.dev->Release();
}

// The same through a Direct3D 10 device
struct Frame10
{
    IDXGISwapChain *sc;
    ID3D10Device *dev;
    ID3D10Texture2D *frozen;
    WlPresent11Fn present;
};

static void render_frame10(void *arg)
{
    Frame10 *f = (Frame10 *)arg;
    ID3D10Texture2D *bb = NULL;
    if (FAILED(f->sc->GetBuffer(0, __uuidof(ID3D10Texture2D), (void **)&bb)))
    {
        Sleep(16);
        return;
    }
    ID3D10RenderTargetView *oldRtv = NULL, *rtv = NULL;
    ID3D10DepthStencilView *oldDsv = NULL;
    D3D10_VIEWPORT oldVp[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT vpCount = D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    f->dev->OMGetRenderTargets(1, &oldRtv, &oldDsv);
    f->dev->RSGetViewports(&vpCount, oldVp);

    if (f->frozen)
        f->dev->CopyResource(bb, f->frozen);
    if (SUCCEEDED(f->dev->CreateRenderTargetView(bb, NULL, &rtv)))
    {
        if (!f->frozen)
        {
            const float black[4] = {0, 0, 0, 1};
            f->dev->ClearRenderTargetView(rtv, black);
        }
        f->dev->OMSetRenderTargets(1, &rtv, NULL);
        D3D10_VIEWPORT vp = {0, 0, (UINT)g_bbW, (UINT)g_bbH, 0, 1};
        f->dev->RSSetViewports(1, &vp);
        ImGui_ImplDX10_NewFrame();
        ImGui_ImplWin32_NewFrame();
        feed_mouse();
        ImGui::NewFrame();
        draw_menu();
        ImGui::Render();
        ImGui_ImplDX10_RenderDrawData(ImGui::GetDrawData());
        rtv->Release();
    }
    wl_d3d11_take_pending_shot(f->sc); // the agent's pictures show the menu too

    f->dev->OMSetRenderTargets(1, &oldRtv, oldDsv);
    f->dev->RSSetViewports(vpCount, oldVp);
    if (oldRtv)
        oldRtv->Release();
    if (oldDsv)
        oldDsv->Release();
    bb->Release();
    wl_display_dest();
    HRESULT hr = f->present(f->sc, 1, 0);
    if (FAILED(hr) || hr == DXGI_STATUS_OCCLUDED)
        Sleep(16);
}

void wl_menu_present10(IDXGISwapChain *sc, WlPresent11Fn present)
{
    int want = menu_key_pressed();
    if (InterlockedExchange(&g_request, -1) == 1)
        want = 1;
    if (!want)
        return;

    ID3D10Texture2D *bb = NULL;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D10Texture2D), (void **)&bb)))
        return;
    D3D10_TEXTURE2D_DESC d;
    bb->GetDesc(&d);
    g_bbW = (int)d.Width;
    g_bbH = (int)d.Height;
    Frame10 f = {sc, NULL, NULL, present};
    bb->GetDevice(&f.dev);
    if (!g_ready)
    {
        DXGI_SWAP_CHAIN_DESC sd;
        g_ready = SUCCEEDED(sc->GetDesc(&sd)) && setup10(sd.OutputWindow, f.dev) ? 1 : -1;
    }
    if (g_ready > 0 && g_backend == 10)
    {
        D3D10_TEXTURE2D_DESC fd = d;
        fd.Usage = D3D10_USAGE_DEFAULT;
        fd.BindFlags = d.SampleDesc.Count > 1 ? D3D10_BIND_RENDER_TARGET : 0;
        fd.CPUAccessFlags = 0;
        fd.MiscFlags = 0;
        if (SUCCEEDED(f.dev->CreateTexture2D(&fd, NULL, &f.frozen)))
            f.dev->CopyResource(f.frozen, bb);
        bb->Release();
        bb = NULL;

        run_menu(render_frame10, &f);

        if (f.frozen)
        {
            if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D10Texture2D), (void **)&bb)))
                f.dev->CopyResource(bb, f.frozen);
            f.frozen->Release();
        }
    }
    if (bb)
        bb->Release();
    f.dev->Release();
}

// ------------------------------------------------------------------------------
// OpenGL (legacy, the fixed pipeline: Frenzy Express, Initial D 6 to 8), from SwapBuffers on the
// game's thread with its context current. A game drawing in windowsloader's framebuffer
// (glhook.cpp) keeps its last picture there: it is drawn under the menu at each of its frames, where
// the display puts it now. Otherwise the game's frame is read back into a texture, drawn under the
// menu at each of its frames and once more as the menu closes, for the game's own swap.
// windowsloader's own GL calls go past its hooks (glhook.cpp: wl_busy). A window the display
// leaves alone has its procedure taken here for the menu's keys.
// ------------------------------------------------------------------------------

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

struct FrameGl
{
    HDC dc;
    GLuint frozen; // 0: none, the menu over black
    WlSwapFn swap;
    int framebuffer; // the game's picture in windowsloader's framebuffer (wl_gl_present)
};

static WNDPROC g_glProc;
static int g_glUnicode;

static LRESULT CALLBACK gl_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT result;
    if (wl_menu_wndproc(h, msg, wp, lp, &result))
        return result;
    return g_glUnicode ? CallWindowProcW(g_glProc, h, msg, wp, lp) : CallWindowProcA(g_glProc, h, msg, wp, lp);
}

// The fixed pipeline for the menu, whatever the game left at SwapBuffers (Initial D 6: Cg programs,
// buffers, a framebuffer object): what glPushAttrib keeps (the enables, the textures) is turned off
// under it; what it does not keep (the GLSL program, the buffers, the framebuffers, the active
// texture unit) is saved here, set to none, and put back. The functions past OpenGL 1.1 are the
// driver's (wglGetProcAddress), each one used only when there is one.
#define GL_ACTIVE_TEXTURE_ 0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE_ 0x84E1
#define GL_TEXTURE0_ 0x84C0
#define GL_ARRAY_BUFFER_ 0x8892
#define GL_ELEMENT_ARRAY_BUFFER_ 0x8893
#define GL_ARRAY_BUFFER_BINDING_ 0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING_ 0x8895
#define GL_PIXEL_PACK_BUFFER_ 0x88EB
#define GL_PIXEL_UNPACK_BUFFER_ 0x88EC
#define GL_PIXEL_PACK_BUFFER_BINDING_ 0x88ED
#define GL_PIXEL_UNPACK_BUFFER_BINDING_ 0x88EF
#define GL_CURRENT_PROGRAM_ 0x8B8D
#define GL_DRAW_FRAMEBUFFER_ 0x8CA9
#define GL_READ_FRAMEBUFFER_ 0x8CA8
#define GL_DRAW_FRAMEBUFFER_BINDING_ 0x8CA6
#define GL_READ_FRAMEBUFFER_BINDING_ 0x8CAA
#define GL_VERTEX_PROGRAM_ARB_ 0x8620
#define GL_FRAGMENT_PROGRAM_ARB_ 0x8804
#define GL_TEXTURE_CUBE_MAP_ 0x8513
#define GL_TEXTURE_3D_ 0x806F
#define GL_TEXTURE_RECTANGLE_ 0x84F5
#define GL_FRAMEBUFFER_SRGB_ 0x8DB9
#define GL_FUNC_ADD_ 0x8006

typedef void(APIENTRY *GlUintFn)(GLenum);
typedef void(APIENTRY *GlBindFn)(GLenum, GLuint);
typedef void(APIENTRY *GlProgramFn)(GLuint);

static struct
{
    int looked;
    GlUintFn activeTexture, clientActiveTexture, blendEquation;
    GlBindFn bindBuffer, bindFramebuffer;
    GlProgramFn useProgram;
} g_glx;

struct GlSaved
{
    GLint activeTexture, clientActiveTexture, arrayBuffer, elementBuffer, packBuffer, unpackBuffer, program, drawFb, readFb;
};

static void gl_functions(void)
{
    if (g_glx.looked)
        return;
    g_glx.looked = 1;
    g_glx.activeTexture = (GlUintFn)wglGetProcAddress("glActiveTexture");
    g_glx.clientActiveTexture = (GlUintFn)wglGetProcAddress("glClientActiveTexture");
    g_glx.bindBuffer = (GlBindFn)wglGetProcAddress("glBindBuffer");
    g_glx.bindFramebuffer = (GlBindFn)wglGetProcAddress("glBindFramebuffer");
    if (!g_glx.bindFramebuffer)
        g_glx.bindFramebuffer = (GlBindFn)wglGetProcAddress("glBindFramebufferEXT");
    g_glx.useProgram = (GlProgramFn)wglGetProcAddress("glUseProgram");
    g_glx.blendEquation = (GlUintFn)wglGetProcAddress("glBlendEquation");
    if (!g_glx.blendEquation)
        g_glx.blendEquation = (GlUintFn)wglGetProcAddress("glBlendEquationEXT");
}

// After glPushAttrib(GL_ALL_ATTRIB_BITS) and glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS)
static void gl_neutral(GlSaved *s)
{
    gl_functions();
    memset(s, 0, sizeof *s);
    glGetError(); // a parameter the driver does not know only sets an error, read and dropped below
    if (g_glx.activeTexture)
    {
        glGetIntegerv(GL_ACTIVE_TEXTURE_, &s->activeTexture);
        for (int unit = 1; unit < 4; unit++)
        {
            g_glx.activeTexture(GL_TEXTURE0_ + unit);
            glDisable(GL_TEXTURE_2D);
        }
        g_glx.activeTexture(GL_TEXTURE0_);
    }
    if (g_glx.clientActiveTexture)
    {
        glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE_, &s->clientActiveTexture);
        g_glx.clientActiveTexture(GL_TEXTURE0_);
    }
    if (g_glx.bindBuffer)
    {
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING_, &s->arrayBuffer);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING_, &s->elementBuffer);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING_, &s->packBuffer);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING_, &s->unpackBuffer);
        g_glx.bindBuffer(GL_ARRAY_BUFFER_, 0);
        g_glx.bindBuffer(GL_ELEMENT_ARRAY_BUFFER_, 0);
        g_glx.bindBuffer(GL_PIXEL_PACK_BUFFER_, 0);
        g_glx.bindBuffer(GL_PIXEL_UNPACK_BUFFER_, 0);
    }
    if (g_glx.useProgram)
    {
        glGetIntegerv(GL_CURRENT_PROGRAM_, &s->program);
        g_glx.useProgram(0);
    }
    if (g_glx.bindFramebuffer)
    {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_, &s->drawFb);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_, &s->readFb);
        g_glx.bindFramebuffer(GL_DRAW_FRAMEBUFFER_, 0);
        g_glx.bindFramebuffer(GL_READ_FRAMEBUFFER_, 0);
    }
    glDisable(GL_VERTEX_PROGRAM_ARB_); // Cg's ARB programs
    glDisable(GL_FRAGMENT_PROGRAM_ARB_);
    glDisable(GL_TEXTURE_CUBE_MAP_); // above GL_TEXTURE_2D on unit 0
    glDisable(GL_TEXTURE_3D_);
    glDisable(GL_TEXTURE_RECTANGLE_);
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
    glDisable(GL_TEXTURE_GEN_R);
    glDisable(GL_TEXTURE_GEN_Q);
    for (int plane = 0; plane < 6; plane++)
        glDisable(GL_CLIP_PLANE0 + plane);
    glDisable(GL_FRAMEBUFFER_SRGB_);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_POLYGON_STIPPLE);
    // What Dear ImGui's backend leaves as it finds it: Initial D 6 keeps a GL_MAX blending (its
    // menu came out see-through over a bright picture), fog or an alpha test would fade it too
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    if (g_glx.blendEquation)
        g_glx.blendEquation(GL_FUNC_ADD_);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glGetError();
}

// Before glPopClientAttrib and glPopAttrib
static void gl_restore(const GlSaved *s)
{
    if (g_glx.bindFramebuffer)
    {
        g_glx.bindFramebuffer(GL_DRAW_FRAMEBUFFER_, (GLuint)s->drawFb);
        g_glx.bindFramebuffer(GL_READ_FRAMEBUFFER_, (GLuint)s->readFb);
    }
    if (g_glx.useProgram)
        g_glx.useProgram((GLuint)s->program);
    if (g_glx.bindBuffer)
    {
        g_glx.bindBuffer(GL_ARRAY_BUFFER_, (GLuint)s->arrayBuffer);
        g_glx.bindBuffer(GL_ELEMENT_ARRAY_BUFFER_, (GLuint)s->elementBuffer);
        g_glx.bindBuffer(GL_PIXEL_PACK_BUFFER_, (GLuint)s->packBuffer);
        g_glx.bindBuffer(GL_PIXEL_UNPACK_BUFFER_, (GLuint)s->unpackBuffer);
    }
    if (g_glx.clientActiveTexture && s->clientActiveTexture)
        g_glx.clientActiveTexture((GLenum)s->clientActiveTexture);
    if (g_glx.activeTexture && s->activeTexture)
        g_glx.activeTexture((GLenum)s->activeTexture);
    glGetError();
}

// Around each of windowsloader's drawings: the game's state kept, the fixed pipeline set, the
// matrices pushed (the texture matrix as identity)
static void gl_enter(GlSaved *s)
{
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    gl_neutral(s);
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
}

static void gl_leave(const GlSaved *s)
{
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    gl_restore(s);
    glPopClientAttrib();
    glPopAttrib();
}

static int setup_gl(HWND wnd)
{
    if (!setup_imgui(wnd) || !ImGui_ImplOpenGL2_Init())
    {
        wl_log("menu: ImGui could not start on this OpenGL context");
        return 0;
    }
    if (wl_display_hwnd() != g_wnd)
    {
        g_glUnicode = IsWindowUnicode(g_wnd);
        g_glProc = (WNDPROC)(g_glUnicode ? SetWindowLongPtrW(g_wnd, GWLP_WNDPROC, (LONG_PTR)gl_wndproc)
                                         : SetWindowLongPtrA(g_wnd, GWLP_WNDPROC, (LONG_PTR)gl_wndproc));
    }
    g_backend = 2;
    wl_log("menu: ready (OpenGL, window %p, picture %dx%d)", g_wnd, g_bbW, g_bbH);
    return 1;
}

// The frozen frame over the whole window, or black
static void draw_frozen(GLuint frozen)
{
    glViewport(0, 0, g_bbW, g_bbH);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!frozen)
        return;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 1, 0, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, frozen);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(0, 0);
    glTexCoord2f(1, 0);
    glVertex2f(1, 0);
    glTexCoord2f(1, 1);
    glVertex2f(1, 1);
    glTexCoord2f(0, 1);
    glVertex2f(0, 1);
    glEnd();
}

static void render_frame_gl(void *arg)
{
    FrameGl *f = (FrameGl *)arg;
    GlSaved saved;
    RECT client;
    if (f->framebuffer && GetClientRect(g_wnd, &client) && client.right > 0 && client.bottom > 0)
    {
        g_bbW = client.right; // the window as the display has it now (F11, the Affichage page)
        g_bbH = client.bottom;
    }
    wl_busy_enter();
    gl_enter(&saved);
    if (f->framebuffer)
        wl_gl_present();
    else
        draw_frozen(f->frozen);
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplWin32_NewFrame();
    feed_mouse();
    ImGui::NewFrame();
    draw_menu();
    ImGui::Render();
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    wl_gl_take_pending_shot(g_bbW, g_bbH); // the agent's pictures show the menu too
    gl_leave(&saved);
    wl_busy_leave();
    wl_display_dest(); // F11 and Alt+Enter work in the menu too
    if (!f->swap(f->dc))
        Sleep(16);
}

void wl_menu_swap_gl(HDC dc, WlSwapFn swap)
{
    int want = menu_key_pressed();
    if (InterlockedExchange(&g_request, -1) == 1)
        want = 1;
    if (!want)
        return;
    HWND wnd = WindowFromDC(dc);
    RECT r;
    if (!wnd || !GetClientRect(wnd, &r) || r.right <= 0 || r.bottom <= 0)
        return;
    g_bbW = r.right;
    g_bbH = r.bottom;
    if (!g_ready)
        g_ready = setup_gl(wnd) ? 1 : -1;
    if (g_ready < 0 || g_backend != 2)
        return;

    // The game's frame, its back buffer as it is about to be swapped (or windowsloader's framebuffer)
    FrameGl f = {dc, 0, swap, wl_gl_redirected()};
    GlSaved saved;
    if (f.framebuffer)
    {
        run_menu(render_frame_gl, &f);
        return;
    }
    unsigned char *pixels = (unsigned char *)malloc((size_t)g_bbW * g_bbH * 4);
    wl_busy_enter();
    if (pixels)
    {
        gl_enter(&saved);
        glReadPixels(0, 0, g_bbW, g_bbH, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
        glGenTextures(1, &f.frozen);
        glBindTexture(GL_TEXTURE_2D, f.frozen);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_bbW, g_bbH, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
        gl_leave(&saved);
        free(pixels);
    }
    wl_busy_leave();

    run_menu(render_frame_gl, &f);

    // The game's frame back in the back buffer, for its own swap
    wl_busy_enter();
    gl_enter(&saved);
    draw_frozen(f.frozen);
    gl_leave(&saved);
    if (f.frozen)
        glDeleteTextures(1, &f.frozen);
    wl_busy_leave();
}
