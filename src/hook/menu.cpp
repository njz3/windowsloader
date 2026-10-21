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

// The F10 menu. It opens in the Present hook, on the game's render thread, and keeps that thread
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
#include <d3d11.h>
#include <stdio.h>
#include <string.h>

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"

#include "agent.h"
#include "controls.h"
#include "display.h"
#include "ffb.h"
#include "game.h"
#include "hooks.h"
#include "inputs.h"
#include "log.h"
#include "menu.h"
#include "pause.h"
#include "wlshared.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static volatile LONG g_open;
static volatile LONG g_request = -1; // the agent's: 1 open, 0 close
static int g_ready;                  // 1 set up, -1 failed
static int g_backend;                // 9 or 11: the renderer ImGui was set up for
static HWND g_wnd;
static int g_bbW, g_bbH;

static volatile LONG g_tabRequest = -1;

void wl_menu_request_tab(int tab)
{
    InterlockedExchange(&g_tabRequest, tab);
}

void wl_menu_request(int open)
{
    InterlockedExchange(&g_request, open ? 1 : 0);
}

int wl_menu_is_open(void)
{
    return g_open;
}

static int foreground_is_ours(void)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// F10 pressed since the last call (the key is read directly: the game's message loop does not
// run while the menu is open)
static int f10_pressed(void)
{
    static int was;
    int down = foreground_is_ours() && (GetAsyncKeyState(VK_F10) & 0x8000);
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
    RECT pic;
    wl_display_picture(&pic);
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

static const struct
{
    const char *action;
    const char *label;
} g_labels[] = {
    {"P1_Steer", "Volant"},        {"P1_Gas", "Accélérateur"}, {"P1_Brake", "Frein"},       {"P1_Start", "Start"},
    {"ViewChange", "Vue"},         {"Handbrake", "Frein à main"}, {"GearUp", "Rapport +"},  {"GearDown", "Rapport −"},
    {"P1_Coin", "Pièce"},          {"Test", "Test"},           {"P1_Service", "Service"},
    {"View1", "Vue 1 (rouge)"},    {"View2", "Vue 2 (bleu)"},  {"View3", "Vue 3 (jaune)"},  {"View4", "Vue 4 (vert)"},
    {"Gear1", "1re"},              {"Gear2", "2e"},            {"Gear3", "3e"},             {"Gear4", "4e"},
};

static const char *label_of(const WlInput *in)
{
    for (int i = 0; i < (int)(sizeof g_labels / sizeof g_labels[0]); i++)
        if (!strcmp(in->action, g_labels[i].action))
            return g_labels[i].label;
    return in->name;
}

static void draw_value(const WlInput *in)
{
    int raw = wl_input_get(in);
    if (in->type == WL_INPUT_SWITCH)
    {
        ImGui::TextUnformatted(raw != in->rawLow ? "appuyé" : "—");
        return;
    }
    float f = in->rawHigh != in->rawLow ? (float)(raw - in->rawLow) / (float)(in->rawHigh - in->rawLow) : 0;
    char text[16];
    snprintf(text, sizeof text, "%d", raw);
    ImGui::ProgressBar(f < 0 ? 0 : f > 1 ? 1 : f, ImVec2(-1, 0), text);
}

// The capture of a binding in progress
static int g_capInput = -1, g_capWhich, g_capAppend;
static char g_capPrompt[128];

static void start_capture(int input, int which, int append, const WlInput *in)
{
    g_capInput = input;
    g_capWhich = which;
    g_capAppend = append;
    if (in->type == WL_INPUT_AXIS && which == 0)
        snprintf(g_capPrompt, sizeof g_capPrompt, "Tournez le volant vers la GAUCHE");
    else if (in->type == WL_INPUT_ANALOG && which == 0)
        snprintf(g_capPrompt, sizeof g_capPrompt, "Appuyez à fond sur la pédale « %s »", label_of(in));
    else
        snprintf(g_capPrompt, sizeof g_capPrompt, "Appuyez sur la touche ou le bouton « %s »", label_of(in));
    wl_controls_capture(input, which);
    ImGui::OpenPopup("Assigner");
}

static void capture_popup(void)
{
    if (!ImGui::BeginPopupModal("Assigner", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextUnformatted(g_capPrompt);
    ImGui::TextDisabled("Clavier, manette ou volant. Échap pour annuler.");
    char got[64];
    if (g_capInput >= 0 && wl_controls_capture_result(got, sizeof got))
    {
        char value[1024] = "";
        if (g_capAppend)
            wl_controls_binding(g_capInput, g_capWhich, value, sizeof value);
        size_t n = strlen(value);
        snprintf(value + n, sizeof value - n, "%s%s", n ? ", " : "", got);
        wl_controls_set_binding(g_capInput, g_capWhich, value);
        g_capInput = -1;
        ImGui::CloseCurrentPopup();
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::Button("Annuler"))
    {
        wl_controls_capture_cancel();
        g_capInput = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

static void binding_row(int index, const WlInput *in, int which, const char *label)
{
    char text[1024];
    wl_controls_binding(index, which, text, sizeof text);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextWrapped("%s", text[0] ? text : "(aucune)");
    ImGui::TableSetColumnIndex(2);
    if (which == 0)
        draw_value(in);
    ImGui::TableSetColumnIndex(3);
    ImGui::PushID(index * 4 + which);
    ImGui::BeginDisabled(!wl_controls_running());
    if (ImGui::SmallButton("Assigner"))
        start_capture(index, which, 0, in);
    ImGui::SameLine();
    if (ImGui::SmallButton("Ajouter"))
        start_capture(index, which, 1, in);
    ImGui::SameLine();
    if (ImGui::SmallButton("Effacer"))
        wl_controls_set_binding(index, which, "");
    ImGui::EndDisabled();
    ImGui::PopID();
}

static void page_controls(void)
{
    if (!wl_controls_running())
        ImGui::TextWrapped("Le jeu est piloté par l'agent (--agent) : les manettes ne sont pas lues, l'assignation est désactivée.");
    else
    {
        const char *pad = wl_controls_player_name(0);
        ImGui::Text("Joueur 1 : %s", pad[0] ? pad : "aucune manette (clavier seulement)");
    }
    if (!wl_controls_has_file())
        return;
    ImGui::TextDisabled("Liaisons enregistrées dans controls.ini, à côté du lanceur.");
    ImGui::Spacing();

    if (!ImGui::BeginTable("controls", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("Liaisons", ImGuiTableColumnFlags_WidthStretch, 2.2f);
    ImGui::TableSetupColumn("Valeur", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    char label[96];
    for (int i = 0; i < wl_inputs_count(); i++)
    {
        WlInput *in = wl_input_at(i);
        if (!in->action)
            continue;
        if (in->type == WL_INPUT_AXIS)
        {
            snprintf(label, sizeof label, "%s (axe)", label_of(in));
            binding_row(i, in, 0, label);
            snprintf(label, sizeof label, "%s à gauche", label_of(in));
            binding_row(i, in, 1, label);
            snprintf(label, sizeof label, "%s à droite", label_of(in));
            binding_row(i, in, 2, label);
        }
        else if (in->type == WL_INPUT_ANALOG)
        {
            snprintf(label, sizeof label, "%s (axe)", label_of(in));
            binding_row(i, in, 0, label);
            snprintf(label, sizeof label, "%s (touche)", label_of(in));
            binding_row(i, in, 1, label);
        }
        else
            binding_row(i, in, 0, label_of(in));
    }
    ImGui::EndTable();
}

static void page_ffb(void)
{
    double strength;
    int invert;
    wl_controls_ffb_settings(&strength, &invert);
    const char *pad = wl_controls_player_name(0);
    ImGui::Text("Volant : %s", pad[0] ? pad : "aucun (joueur 1)");
    ImGui::Spacing();
    int pct = (int)(strength * 100 + 0.5);
    if (ImGui::SliderInt("Force", &pct, 0, 200, "%d %%"))
        wl_controls_set_ffb(pct / 100.0, invert);
    bool inv = invert != 0;
    if (ImGui::Checkbox("Inverser le sens", &inv))
        wl_controls_set_ffb(strength, inv);
    ImGui::Spacing();
    double f = wl_ffb_get(NULL);
    char text[64];
    snprintf(text, sizeof text, "%+.2f (%s)", f, f > 0 ? "vers la gauche" : f < 0 ? "vers la droite" : "aucune");
    ImGui::TextUnformatted("Force demandée par le jeu :");
    ImGui::ProgressBar((float)(1 - f) / 2, ImVec2(-1, 0), text);
    ImGui::TextDisabled("Réglages enregistrés dans [Config] de controls.ini (FFB_Strength, FFB_Invert).");
}

// Display: linuxloader's three settings, the first two applied at once
static void page_display(void)
{
    static const char *aspects[WL_ASPECT_COUNT] = {"Original (forme de l'image du jeu)", "4:3", "16:9", "4:3 avec bandes noires"};
    static const char *modes[WL_MODE_COUNT] = {"Fenêtre (pixels carrés)", "Fenêtre étirée", "Plein écran", "Plein écran étiré"};
    static const int sizes[][2] = {{0, 0},     {640, 480},   {800, 600},   {1024, 768},  {1280, 720},  {1280, 1024}, {1360, 768},
                                   {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
    int aspect = wl_display_aspect(), mode = wl_display_mode();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    if (ImGui::Combo("Rapport d'aspect", &aspect, aspects, WL_ASPECT_COUNT))
        wl_display_set(aspect, mode);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    if (ImGui::Combo("Mode d'affichage", &mode, modes, WL_MODE_COUNT))
        wl_display_set(aspect, mode);
    ImGui::TextDisabled("F11 ou Alt+Entrée : fenêtre / plein écran.");
    ImGui::Spacing();

    int w, h, current = -1;
    wl_display_internal(&w, &h);
    char saved[32] = "Native";
    {
        wchar_t ini[MAX_PATH], v1[16], v2[16];
        if (GetEnvironmentVariableW(WL_ENV_INI, ini, MAX_PATH))
        {
            GetPrivateProfileStringW(L"Display", L"WIDTH", L"AUTO", v1, 16, ini);
            GetPrivateProfileStringW(L"Display", L"HEIGHT", L"AUTO", v2, 16, ini);
            int sw = _wtoi(v1), sh = _wtoi(v2);
            if (sw > 0 && sh > 0)
                snprintf(saved, sizeof saved, "%dx%d", sw, sh);
            for (int i = 0; i < (int)(sizeof sizes / sizeof sizes[0]); i++)
                if (sizes[i][0] == (sw > 0 ? sw : 0) && sizes[i][1] == (sh > 0 ? sh : 0))
                    current = i;
        }
    }
    ImGui::BeginDisabled(!wl_display_internal_supported());
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    if (ImGui::BeginCombo("Résolution interne", saved))
    {
        for (int i = 0; i < (int)(sizeof sizes / sizeof sizes[0]); i++)
        {
            char name[32];
            if (sizes[i][0])
                snprintf(name, sizeof name, "%dx%d", sizes[i][0], sizes[i][1]);
            else
                snprintf(name, sizeof name, "Native");
            if (ImGui::Selectable(name, i == current))
                wl_display_set_internal(sizes[i][0], sizes[i][1]);
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Taille de calcul et de rendu du jeu, avant l'aspect et le mode.\nPrise en compte au prochain lancement.");
    if (!wl_display_internal_supported())
        ImGui::TextDisabled("Pas encore prise en charge pour ce jeu.");
    else
        ImGui::TextDisabled("Rendu actuel : %dx%d (%s). Prise en compte au prochain lancement.", g_bbW, g_bbH, w ? "windowsloader.ini" : "réglage du jeu");
    ImGui::TextDisabled("Enregistré dans [Display] de windowsloader.ini, à côté du lanceur.");
}

static void page_game(void)
{
    ImGui::Text("%s est en pause.", wl_game_title());
    ImGui::Spacing();
    if (ImGui::Button("Reprendre (F10)"))
        InterlockedExchange(&g_open, 0);
    ImGui::Spacing();
    RECT rc = {};
    GetClientRect(g_wnd, &rc);
    ImGui::TextDisabled("Image %dx%d, fenêtre %ldx%ld.", g_bbW, g_bbH, rc.right, rc.bottom);
    ImGui::Spacing();
    if (ImGui::Button("Quitter le jeu"))
        ImGui::OpenPopup("Quitter");
    if (ImGui::BeginPopupModal("Quitter", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Quitter le jeu ?");
        if (ImGui::Button("Oui"))
        {
            wl_log("menu: quit");
            TerminateProcess(GetCurrentProcess(), 0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Non") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void page_about(void)
{
    ImGui::Text("windowsloader %s (%d bits)", WL_VERSION, (int)sizeof(void *) * 8);
    ImGui::TextUnformatted("Benjamin Maurin - njz3, licence GPL v3.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Avec :");
    ImGui::BulletText("Dear ImGui, Omar Cornut (MIT)");
    ImGui::BulletText("MinHook, Tsuda Kageyu (BSD 2 clauses)");
    ImGui::BulletText("SDL3 (zlib)");
    ImGui::Spacing();
    ImGui::TextDisabled("Conventions de linuxloader : controls.ini, menu F10, mode agent.");
}

static void draw_menu(void)
{
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.64f, io.DisplaySize.y * 0.74f), ImGuiCond_Always);
    char title[128];
    snprintf(title, sizeof title, "windowsloader — %s###menu", wl_game_title());
    if (ImGui::Begin(title, NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        int tab = InterlockedExchange(&g_tabRequest, -1);
        if (ImGui::BeginTabBar("pages"))
        {
            if (ImGui::BeginTabItem("Jeu", NULL, tab == 0 ? ImGuiTabItemFlags_SetSelected : 0))
            {
                page_game();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Contrôles", NULL, tab == 1 ? ImGuiTabItemFlags_SetSelected : 0))
            {
                page_controls();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Affichage", NULL, tab == 2 ? ImGuiTabItemFlags_SetSelected : 0))
            {
                page_display();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Retour de force", NULL, tab == 3 ? ImGuiTabItemFlags_SetSelected : 0))
            {
                page_ffb();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("À propos", NULL, tab == 4 ? ImGuiTabItemFlags_SetSelected : 0))
            {
                page_about();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        capture_popup();
    }
    ImGui::End();
}

// ------------------------------------------------------------------------------
// The menu's frames
// ------------------------------------------------------------------------------

// While the menu is open: the window's messages, F10 or the agent to close it, a frame each time
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
        if (f10_pressed() || InterlockedExchange(&g_request, -1) == 0)
            InterlockedExchange(&g_open, 0);
        if (g_open)
            frame(arg);
    }
    if (g_capInput >= 0)
        wl_controls_capture_cancel();
    g_capInput = -1;
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
    if (FAILED(f->present(dev, NULL, wl_display_dest(), NULL, NULL)))
        Sleep(16);
}

void wl_menu_present(IDirect3DDevice9 *dev, WlPresentFn present)
{
    int want = f10_pressed();
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
    int want = f10_pressed();
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
