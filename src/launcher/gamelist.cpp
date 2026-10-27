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

// The picker's table of the games windowsloader plays (gamelist.h). The entries with the most
// conditions come first: the first one that fits is the game.

#include "gamelist.h"

#include <stdio.h>
#include <wchar.h>

const WlKnownGame g_wlKnownGames[] = {
    {"Sega Rally 3", {L"Rally.exe"}, {NULL}, 0, NULL, "Sega Europa-R", "2008", WL_GAME_PLAYS,
     "Carte d'E/S USB émulée ; moteur du volant par le port d'E/S 0x1020."},
    {"Daytona Championship USA", {L"Daytona.exe"}, {NULL}, 0, NULL, "Sega (PC)", "2017", WL_GAME_PLAYS,
     "Carte d'E/S DirectInput « SEGA IO Controller », Shell de Sega (crédits) ; moteur par le port parallèle."},
    {"Ford Racing Full Blown", {L"fordracing.exe"}, {NULL}, 0, NULL, "Sega (PC)", "2006", WL_GAME_PLAYS,
     "Carte d'E/S USB (Happ UGCI) ; moteur par le port parallèle. Menu de service : GameShell.exe."},
    {"Initial D Arcade Stage 6 AA", {L"id6_dump_.exe", L"InitialD6_GLW_RE_SBUU.exe"}, {NULL}, 0, NULL, "Sega RingEdge", "2011",
     WL_GAME_PLAYS, "Carte JVS, clé de la borne, carte de pilotage sur COM1. Menu de service : le jeu avec -t."},
    {"Initial D Arcade Stage 7 AAX", {L"InitialD7_GLW_RE_SBYD_dumped_.exe", L"InitialD7_GLW_RE_SBYD.exe"}, {L"SBYD_Table.dat"}, 0, NULL,
     "Sega RingEdge", "2012", WL_GAME_PLAYS, "Carte JVS (pilote mxjvs), carte de pilotage ; lecteur de cartes derrière picodaemon.exe."},
    {"Initial D Arcade Stage 8 Infinity", {L"InitialD8_GLW_RE_SBZZ_redumped_.exe", L"InitialD8_GLW_RE_SBZZ_redumped4k_.exe"},
     {L"SBZZ_Table.dat"}, 0, NULL, "Sega RingEdge", "2014", WL_GAME_PLAYS,
     "Carte JVS (pilote mxjvs), carte de pilotage ; lecteur de cartes derrière picodaemon.exe."},
    {"GRID", {L"GRID.exe"}, {NULL}, 0, NULL, "Sega Europa-R", "2008", WL_GAME_PLAYS,
     "Carte d'E/S DirectInput « SEGA IO Controller » ; moteur par le port parallèle."},
    {"DiRT Showdown", {L"Showdown.exe"}, {NULL}, 0, NULL, "Sega Amusements (PC)", "2015", WL_GAME_PLAYS,
     "Carte d'E/S DirectInput « SEGA IO Controller » ; moteur par le port parallèle."},
    {"K.O. Drive", {L"M-DriveR_RingWide.exe", L"M-DriveR_RingWide 1080P.exe", L"M-DriveR_RingWide original.exe"}, {NULL}, 0, NULL,
     "Sega RingWide", "2013", WL_GAME_PLAYS, "Carte JVS (pilote mxjvs), carte de pilotage. Menu de service : TestMode.exe."},
    {"Sonic & Sega All-Stars Racing", {L"game.exe"}, {L"SBVH_Table.dat"}, 0, NULL, "Sega RingWide", "2011", WL_GAME_PLAYS,
     "Carte JVS (pilote mxjvs), carte de pilotage. Menu de service : gametest.exe."},
    {"Sega Racing Classic", {L"d1a.exe"}, {L"rom\\SBTN_Table.dat"}, 0, NULL, "Sega RingEdge", "2009", WL_GAME_PLAYS,
     "Carte JVS, levier en H ; carte moteur Model 2/3 lue dans les données du jeu."},
    {"Let's Go Safari", {L"GameSampR_RingWide.exe"}, {NULL}, 0, NULL, "Sega RingWide", "2014", WL_GAME_PLAYS,
     "Carte JVS 837-14895 ; la vibration jouée en secousse. Menu de service : GameTestR_RingWide.exe."},
    {"Battle Gear 4 Tuned", {L"game.exe", L"game4.exe"}, {L"game.inf", L"data\\drivedata"}, 0x4675EF6F, L"_MTS_FULL_SCREEN_", "Taito Type X+",
     "2007", WL_GAME_PLAYS, "Carte JVS, volant Taito sur COM1. Borne Pro : [Game.bg4] CABINET = PRO."},
    {"Battle Gear 4", {L"game.exe", L"game4.exe"}, {L"game.inf", L"data\\drivedata"}, 0x446D0EFA, L"_MTS_FULL_SCREEN_", "Taito Type X+", "2006",
     WL_GAME_PLAYS, "Carte JVS, volant Taito sur COM1."},
    {"Chase H.Q. 2", {L"game.exe"}, {L"chq2.ini"}, 0, NULL, "Taito Type X2", "2007", WL_GAME_PLAYS,
     "Carte JVS ; le moteur par les sorties de la carte JVS."},
    {"D1GP Arcade", {L"game.exe"}, {L"IFC23.dll", L"game.inf"}, 0, NULL, "Taito Type X2", "2009", WL_GAME_PLAYS,
     "Carte JVS ; volant DirectInput « Immersion » avec ses effets de retour de force."},
    {"Valve Limit R", {L"launcher.exe"}, {L"game.inf", L"data\\bin2"}, 0, NULL, "Taito Type X", "2005", WL_GAME_PLAYS,
     "Carte JVS, volant Taito sur COM1."},
    {"Wacky Races", {L"launcher.exe"}, {L"wr.ini"}, 0, NULL, "Taito Type X2", "", WL_GAME_PARTLY,
     "Carte JVS ; le moteur par les sorties de la carte JVS. Les fonds des menus restent en 800×600."},
    {"GTI Club: Supermini Festa!", {L"gti3.exe", L"gti3japkey.exe"}, {L"libacio.dll"}, 0, NULL, "Konami PC", "2008", WL_GAME_PLAYS,
     "Carte ACIO de Konami, son volant."},
    {"Wasteland Racers 2071", {L"Arcade.exe"}, {L"SDC2.dll", L"Inputs.xml"}, 0, NULL, "Trio Tech PC", "2006", WL_GAME_PLAYS,
     "Carte SDC2 de Trio Tech : commandes et volant en servo de position."},
    {"Mario Kart Arcade GP DX 1.18", {L"MK_AGP3_FINAL.exe"}, {L"bngrw.dll"}, 0, NULL, "Namco System ES3", "", WL_GAME_PLAYS,
     "Carte JVS Namco NA-JV (COM3), carte moteur Namco (COM1)."},
    {"Mario Kart Arcade GP DX 1.10", {L"MK_AGP3_FINAL.exe"}, {NULL}, 0, NULL, "Namco System ES3", "", WL_GAME_PLAYS,
     "Carte JVS Namco NA-JV (COM3), carte moteur Namco (COM1), lecteur de cartes NFC (COM4)."},
    {"Crazy Ride", {L"CrazyTaxiGame-Win64-Shipping.exe"}, {NULL}, 0, NULL, "UNIS PC (64 bits)", "", WL_GAME_PLAYS,
     "E/S et volant UNIS, clé de la borne."},
    {"Storm Racer G", {L"racing.exe"}, {L"usb_io.dll"}, 0, NULL, "Sega (PC)", "", WL_GAME_PARTLY,
     "Carte USB par usb_io.dll ; vidéos noires sans le décodeur WMV."},
    {"Sega World Drivers Championship", {L"Todoroki-Win64-Shipping.exe"}, {NULL}, 0, L"-windowed", "Sega ALLS (64 bits)", "", WL_GAME_PARTLY,
     "Plateforme ALLS (amdaemon), borne seule, complète (tableau de bord : boutons du volant, vitesse ; carte LED) ou mini "
     "(option CABINET) ; ALL.Net, Aime et le serveur de titre joués sur ce PC : partie invité (course, résultats, game "
     "over), retour de force par la carte du volant sur COM1. Reste le serveur de matching (GAME SERVER BAD, pas de "
     "course en réseau)."},
    {"Wangan Midnight Maximum Tune 6", {L"wmn6r.exe"}, {NULL}, 0, NULL, "Namco System ES3X (64 bits)", "2018", WL_GAME_PARTLY,
     "Clé HASP (identifiant 2808), carte JVS Namco, terminal de salle joué, lecteur Banapass sans carte, hors ligne (sans "
     "ALL.Net) : course en mode Story. Retour de force par la carte du volant sur COM1, une fois STEERING POWER mis à ON "
     "dans le menu de test. Restent les cartes et la caméra."},
    {"Wangan Midnight Maximum Tune 5 DX+", {L"wmn5r.exe"}, {L"hasp_windows_x64_106482.dll"}, 0, NULL, "Namco System ES3X (64 bits)", "2016",
     WL_GAME_PARTLY,
     "Clé HASP (identifiant 2783), carte JVS Namco, terminal de salle joué : course en mode Story, levier en H. Retour de "
     "force par la carte du volant sur COM1, une fois STEERING POWER mis à ON dans le menu de test. Restent le lecteur de "
     "cartes et la caméra."},
    {"Wangan Midnight Maximum Tune 5", {L"wmn5r.exe"}, {NULL}, 0, NULL, "Namco System ES3X (64 bits)", "2014", WL_GAME_PARTLY,
     "Clé HASP, carte JVS Namco, terminal de salle joué : course en mode Story, levier en H. Retour de force par la "
     "carte du volant sur COM1, une fois « ステアリング反力の使用 » mis à ON dans le menu de test. Restent le lecteur "
     "de cartes et la caméra."},
};

const int g_wlKnownGameCount = (int)(sizeof g_wlKnownGames / sizeof g_wlKnownGames[0]);

int wl_known_program_name(const wchar_t *name)
{
    for (int i = 0; i < g_wlKnownGameCount; i++)
        for (int k = 0; k < 3 && g_wlKnownGames[i].exes[k]; k++)
            if (!_wcsicmp(name, g_wlKnownGames[i].exes[k]))
                return 1;
    return 0;
}

// The PE header's time stamp, 0 when the file is not a program
static DWORD pe_stamp(const wchar_t *path)
{
    DWORD stamp = 0;
    FILE *f = _wfopen(path, L"rb");
    if (!f)
        return 0;
    unsigned char dos[64];
    if (fread(dos, 1, sizeof dos, f) == sizeof dos && dos[0] == 'M' && dos[1] == 'Z')
    {
        DWORD pe = *(const DWORD *)(dos + 0x3C);
        unsigned char nt[12];
        if (!fseek(f, (long)pe, SEEK_SET) && fread(nt, 1, sizeof nt, f) == sizeof nt && nt[0] == 'P' && nt[1] == 'E')
            stamp = *(const DWORD *)(nt + 8);
    }
    fclose(f);
    return stamp;
}

const WlKnownGame *wl_known_game(const wchar_t *exePath)
{
    const wchar_t *base = wcsrchr(exePath, L'\\');
    base = base ? base + 1 : exePath;
    wchar_t dir[MAX_PATH];
    wcsncpy_s(dir, MAX_PATH, exePath, base - exePath);
    for (int i = 0; i < g_wlKnownGameCount; i++)
    {
        const WlKnownGame *g = &g_wlKnownGames[i];
        int named = 0;
        for (int k = 0; k < 3 && g->exes[k]; k++)
            named |= !_wcsicmp(base, g->exes[k]);
        if (!named)
            continue;
        int all = 1;
        for (int k = 0; k < 3 && g->markers[k] && all; k++)
        {
            wchar_t marker[MAX_PATH];
            swprintf(marker, MAX_PATH, L"%s%s", dir, g->markers[k]);
            all = GetFileAttributesW(marker) != INVALID_FILE_ATTRIBUTES;
        }
        if (all && (!g->stamp || pe_stamp(exePath) == g->stamp))
            return g;
    }
    return NULL;
}
