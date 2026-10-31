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
#include "lang.h"

#include <stdio.h>
#include <wchar.h>

const WlKnownGame g_wlKnownGames[] = {
    {"Sega Rally 3", "segarally3", "", {L"Rally.exe"}, {0x9228B72E}, {NULL}, 0, NULL, "Sega Europa-R", "2008", WL_GAME_PLAYS,
     WL_N("Carte d'E/S USB émulée ; moteur du volant par le port d'E/S 0x1020."), WL_FEATURE_YES, WL_FEATURE_YES},
    {"Daytona Championship USA", "daytona3", "171221", {L"Daytona.exe"}, {0x08823BEF}, {NULL}, 0, NULL, "Sega (PC)", "2017", WL_GAME_PLAYS,
     WL_N("Carte d'E/S DirectInput « SEGA IO Controller », Shell de Sega (crédits) ; moteur par le port parallèle."),
     WL_FEATURE_YES, WL_FEATURE_PARTLY},
    // The New Season Edition (2019): its own program, the same name. Told apart by the DLL from its
    // Install.ini; here the line above comes first and the picker shows both editions as it
    {"Daytona Championship USA New Season Edition", "daytona3nse", "190306", {L"Daytona.exe"}, {0x9650CBB4}, {NULL}, 0, NULL, "Sega (PC)",
     "2019", WL_GAME_PLAYS, WL_N("Carte d'E/S DirectInput « SEGA IO Controller », Shell de Sega (crédits) ; moteur par le port parallèle."),
     WL_FEATURE_YES, WL_FEATURE_PARTLY},
    {"Ford Racing Full Blown", "fordracing", "", {L"fordracing.exe"}, {0xEB788677}, {NULL}, 0, NULL, "Sega (PC)", "2006", WL_GAME_PLAYS,
     WL_N("Carte d'E/S USB (Happ UGCI) ; moteur par le port parallèle. Menu de service : GameShell.exe."), WL_FEATURE_YES, WL_FEATURE_YES},
    {"Initial D Arcade Stage 6 AA", "initiald6", "", {L"id6_dump_.exe", L"InitialD6_GLW_RE_SBUU.exe"}, {0x0C422DA1, 0xBF55277C}, {NULL}, 0, NULL, "Sega RingEdge", "2011",
     WL_GAME_PLAYS, WL_N("Carte JVS, clé de la borne, carte de pilotage sur COM1. Menu de service : le jeu avec -t."),
     WL_FEATURE_PARTLY, WL_FEATURE_NO},
    {"Initial D Arcade Stage 7 AAX", "initiald7", "", {L"InitialD7_GLW_RE_SBYD_dumped_.exe", L"InitialD7_GLW_RE_SBYD.exe"}, {0x2B7DF1BC, 0x9C7449DD}, {L"SBYD_Table.dat"}, 0, NULL,
     "Sega RingEdge", "2012", WL_GAME_PLAYS, WL_N("Carte JVS (pilote mxjvs), carte de pilotage ; lecteur de cartes derrière picodaemon.exe."),
     WL_FEATURE_PARTLY, WL_FEATURE_NO},
    {"Initial D Arcade Stage 8 Infinity", "initiald8", "", {L"InitialD8_GLW_RE_SBZZ_redumped_.exe", L"InitialD8_GLW_RE_SBZZ_redumped4k_.exe"}, {0x56544E57, 0x8C985DE5},
     {L"SBZZ_Table.dat"}, 0, NULL, "Sega RingEdge", "2014", WL_GAME_PLAYS,
     WL_N("Carte JVS (pilote mxjvs), carte de pilotage ; lecteur de cartes derrière picodaemon.exe."), WL_FEATURE_PARTLY, WL_FEATURE_NO},
    {"GRID", "grid", "1.1", {L"GRID.exe"}, {0x264E2CCF}, {NULL}, 0, NULL, "Sega Europa-R", "2008", WL_GAME_PLAYS,
     WL_N("Carte d'E/S DirectInput « SEGA IO Controller » ; moteur par le port parallèle."), WL_FEATURE_YES, WL_FEATURE_NO},
    {"DiRT Showdown", "showdown", "1.2", {L"Showdown.exe"}, {0x879BB2CD}, {NULL}, 0, NULL, "Sega Amusements (PC)", "2015", WL_GAME_PLAYS,
     WL_N("Carte d'E/S DirectInput « SEGA IO Controller » ; moteur par le port parallèle."), WL_FEATURE_PARTLY, WL_FEATURE_YES},
    {"K.O. Drive", "kodrive", "", {L"M-DriveR_RingWide.exe", L"M-DriveR_RingWide 1080P.exe", L"M-DriveR_RingWide original.exe"}, {0x9B1CC57C, 0x9B1CC57C, 0x0C2B0515}, {NULL}, 0, NULL,
     "Sega RingWide", "2013", WL_GAME_PLAYS, WL_N("Carte JVS (pilote mxjvs), carte de pilotage. Menu de service : TestMode.exe."),
     WL_FEATURE_PARTLY, WL_FEATURE_NO},
    {"Sonic & Sega All-Stars Racing", "ssasr", "", {L"game.exe"}, {0xCEDBD852}, {L"SBVH_Table.dat"}, 0, NULL, "Sega RingWide", "2011", WL_GAME_PLAYS,
     WL_N("Carte JVS (pilote mxjvs), carte de pilotage. Menu de service : gametest.exe."), WL_FEATURE_PARTLY, WL_FEATURE_NO},
    {"Sega Racing Classic", "src", "", {L"d1a.exe"}, {0xFC87F309}, {L"rom\\SBTN_Table.dat"}, 0, NULL, "Sega RingEdge", "2009", WL_GAME_PLAYS,
     WL_N("Carte JVS, levier en H ; carte moteur Model 2/3 lue dans les données du jeu."), WL_FEATURE_PARTLY, WL_FEATURE_NO},
    {"Let's Go Safari", "lgs", "", {L"GameSampR_RingWide.exe"}, {0x02E0DDB6}, {NULL}, 0, NULL, "Sega RingWide", "2014", WL_GAME_PLAYS,
     WL_N("Carte JVS 837-14895 ; la vibration jouée en secousse. Menu de service : GameTestR_RingWide.exe."), WL_FEATURE_PARTLY, WL_FEATURE_NO},
    {"Battle Gear 4 Tuned", "bg4", "2.8", {L"game.exe", L"game4.exe"}, {0xD379A14B, 0x041ABD56}, {L"game.inf", L"data\\drivedata"}, 0x4675EF6F, L"_MTS_FULL_SCREEN_", "Taito Type X+",
     "2007", WL_GAME_PLAYS, WL_N("Carte JVS, volant Taito sur COM1. Borne Pro : [Game.bg4] CABINET = PRO."), WL_FEATURE_PARTLY, WL_FEATURE_NO},
    {"Battle Gear 4", "bg4eng", "", {L"game.exe", L"game4.exe"}, {0x282C6BB7}, {L"game.inf", L"data\\drivedata"}, 0x446D0EFA, L"_MTS_FULL_SCREEN_", "Taito Type X+", "2006",
     WL_GAME_PLAYS, WL_N("Carte JVS, volant Taito sur COM1."), WL_FEATURE_PARTLY, WL_FEATURE_NO},
    {"Chase H.Q. 2", "chasehq2", "", {L"game.exe"}, {0xBE64D00F}, {L"chq2.ini"}, 0, NULL, "Taito Type X2", "2007", WL_GAME_PLAYS,
     WL_N("Carte JVS ; le moteur par les sorties de la carte JVS."), WL_FEATURE_PARTLY, WL_FEATURE_NO},
    {"D1GP Arcade", "d1gp", "", {L"game.exe"}, {0x55A0BD9D}, {L"IFC23.dll", L"game.inf"}, 0, NULL, "Taito Type X2", "2009", WL_GAME_PLAYS,
     WL_N("Carte JVS ; volant DirectInput « Immersion » avec ses effets de retour de force."), WL_FEATURE_PARTLY, WL_FEATURE_NO},
    {"Valve Limit R", "vlimitr", "", {L"launcher.exe"}, {0x919C4D69}, {L"game.inf", L"data\\bin2"}, 0, NULL, "Taito Type X", "2005", WL_GAME_PLAYS,
     WL_N("Carte JVS, volant Taito sur COM1."), WL_FEATURE_PARTLY, WL_FEATURE_NO},
    {"Wacky Races", "wacky", "", {L"launcher.exe"}, {0x52FD5DFD}, {L"wr.ini"}, 0, NULL, "Taito Type X2", "", WL_GAME_PARTLY,
     WL_N("Carte JVS ; le moteur par les sorties de la carte JVS. Les fonds des menus restent en 800×600."), WL_FEATURE_PARTLY, WL_FEATURE_NO},
    {"GTI Club: Supermini Festa!", "gticlub3", "", {L"gti3.exe", L"gti3japkey.exe"}, {0xC0F782AF, 0xE41F76BE}, {L"libacio.dll"}, 0, NULL, "Konami PC", "2008", WL_GAME_PLAYS,
     WL_N("Carte ACIO de Konami, son volant."), WL_FEATURE_PARTLY, WL_FEATURE_YES},
    {"Wasteland Racers 2071", "wasteland", "1.6", {L"Arcade.exe"}, {0xA6CD1034}, {L"SDC2.dll", L"Inputs.xml"}, 0, NULL, "Trio Tech PC", "2006", WL_GAME_PLAYS,
     WL_N("Carte SDC2 de Trio Tech : commandes et volant en servo de position."), WL_FEATURE_YES, WL_FEATURE_NO},
    {"Mario Kart Arcade GP DX 1.18", "mkdx", "1.18", {L"MK_AGP3_FINAL.exe"}, {0x280295A6}, {L"bngrw.dll"}, 0, NULL, "Namco System ES3", "", WL_GAME_PLAYS,
     WL_N("Carte JVS Namco NA-JV (COM3), carte moteur Namco (COM1)."), WL_FEATURE_PARTLY, WL_FEATURE_YES},
    {"Mario Kart Arcade GP DX 1.10", "mkdx", "1.10", {L"MK_AGP3_FINAL.exe"}, {0x4D386327}, {NULL}, 0, NULL, "Namco System ES3", "", WL_GAME_PLAYS,
     WL_N("Carte JVS Namco NA-JV (COM3), carte moteur Namco (COM1), lecteur de cartes NFC (COM4)."), WL_FEATURE_PARTLY, WL_FEATURE_PARTLY},
    {"Crazy Ride", "crazyride", "1.9.0", {L"CrazyTaxiGame-Win64-Shipping.exe"}, {0x0C97E693}, {NULL}, 0, NULL, WL_N("UNIS PC (64 bits)"), "", WL_GAME_PLAYS,
     WL_N("E/S et volant UNIS, clé de la borne."), WL_FEATURE_PARTLY, WL_FEATURE_NO},
    {"Storm Racer G", "srg", "", {L"racing.exe"}, {0xE5A27313}, {L"usb_io.dll"}, 0, NULL, "Sega (PC)", "", WL_GAME_PARTLY,
     WL_N("Carte USB par usb_io.dll ; vidéos noires sans le décodeur WMV."), WL_FEATURE_PARTLY, WL_FEATURE_NO},
    {"Storm Rider", "stormrider", "1.2.6", {L"motobike.exe"}, {0xED34CEB0}, {L"motobikeio.dll"}, 0, NULL, "Wahlap (PC)", "2013", WL_GAME_WORK,
     WL_N("Moto. Carte par motobikeio.dll ; non pris en charge."),
     WL_FEATURE_NO, WL_FEATURE_NO},
    {"Initial D Arcade Stage Zero Ver.1.31", "idz", "1.31", {L"InitialD0_DX11_Nu.exe"}, {0xCF32AFCE}, {L"amdaemon.exe"}, 0x5BAB5915, NULL, WL_N("Sega Nu (64 bits)"), "2017",
     WL_GAME_PARTLY,
     WL_N("Plateforme Nu (amdaemon, carte AMEX), carte JVS, levier en H, moteur du volant (COM1), lampes (COM11), server box du magasin : "
     "partie invité ; les cartes Aime attendent le serveur en ligne du jeu."), WL_FEATURE_YES, WL_FEATURE_NO},
    {"Initial D Arcade Stage Zero Ver.2", "idz", "2.30", {L"InitialD0_DX11_Nu.exe"}, {0xC7F92CFF}, {L"amdaemon.exe"}, 0, NULL, WL_N("Sega Nu (64 bits)"), "2017", WL_GAME_PARTLY,
     WL_N("Plateforme Nu (amdaemon, carte AMEX), carte JVS, levier en H, moteur du volant (COM1), lampes (COM11), server box du magasin : "
     "partie invité ; les cartes Aime attendent le serveur en ligne du jeu."), WL_FEATURE_YES, WL_FEATURE_NO},
    {"Frenzy Express", "frenzyexpress", "", {L"xwin.exe"}, {0xDBAF1475}, {L"xio.dll"}, 0, NULL, "Uniana (PC)", "2001", WL_GAME_PLAYS,
     WL_N("Trottinette. Carte par xio.dll : guidon, poussées gauche et droite (l'accélérateur les alterne), frein, moteur du guidon."),
     WL_FEATURE_YES, WL_FEATURE_NO},
    {"Sega World Drivers Championship", "swdc", "", {L"Todoroki-Win64-Shipping.exe"}, {0x3A971F3C}, {NULL}, 0, L"-windowed", WL_N("Sega ALLS (64 bits)"), "", WL_GAME_PARTLY,
     WL_N("Plateforme ALLS (amdaemon), borne seule, complète (tableau de bord : boutons du volant, vitesse ; carte LED) ou mini "
     "(option CABINET) ; ALL.Net, Aime et le serveur de titre joués sur ce PC : partie invité (course, résultats, game "
     "over), retour de force par la carte du volant sur COM1. Reste le serveur de matching (GAME SERVER BAD, pas de "
     "course en réseau)."), WL_FEATURE_PARTLY, WL_FEATURE_NO},
    {"Wangan Midnight Maximum Tune 6", "wmmt6", "", {L"wmn6r.exe"}, {0xC4B9C773}, {NULL}, 0, NULL, WL_N("Namco System ES3X (64 bits)"), "2018", WL_GAME_PARTLY,
     WL_N("Clé HASP (identifiant 2808), carte JVS Namco, terminal de salle joué, lecteur Banapass sans carte, hors ligne (sans "
     "ALL.Net) : course en mode Story. Retour de force par la carte du volant sur COM1, une fois STEERING POWER mis à ON "
     "dans le menu de test. Restent les cartes et la caméra."), WL_FEATURE_YES, WL_FEATURE_NO},
    {"Wangan Midnight Maximum Tune 5 DX+", "wmmt5dxp", "", {L"wmn5r.exe"}, {0x32E089BC}, {L"hasp_windows_x64_106482.dll"}, 0, NULL, WL_N("Namco System ES3X (64 bits)"), "2016",
     WL_GAME_PARTLY,
     WL_N("Clé HASP (identifiant 2783), carte JVS Namco, terminal de salle joué : course en mode Story, levier en H. Retour de "
     "force par la carte du volant sur COM1, une fois STEERING POWER mis à ON dans le menu de test. Restent le lecteur de "
     "cartes et la caméra."), WL_FEATURE_YES, WL_FEATURE_NO},
    {"Wangan Midnight Maximum Tune 5", "wmmt5", "", {L"wmn5r.exe"}, {0x51060B63}, {NULL}, 0, NULL, WL_N("Namco System ES3X (64 bits)"), "2014", WL_GAME_PARTLY,
     WL_N("Clé HASP, carte JVS Namco, terminal de salle joué : course en mode Story, levier en H. Retour de force par la "
     "carte du volant sur COM1, une fois « ステアリング反力の使用 » mis à ON dans le menu de test. Restent le lecteur "
     "de cartes et la caméra."), WL_FEATURE_YES, WL_FEATURE_NO},
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
