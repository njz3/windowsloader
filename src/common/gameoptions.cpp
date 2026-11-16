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

// The games' own options (gameoptions.h): every module's table, for the DLL (its module takes it by
// name, wl_game_use_options, and adds the callbacks only the DLL has) and for the picker, which shows
// and saves them without starting the game. Their texts in French (WL_N), translated where shown.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gameoptions.h"
#include "imgui.h"
#include "lang.h"

// The camera (src/hook/camera.cpp, camera.h): auto, the still picture; the DLL adds the PC's cameras
const char g_wlCameraLabel[] = WL_N("Caméra");
const char g_wlCameraHelp[] =
    WL_N("Les photos des joueurs (le link de Daytona ne tient pas sans caméra). Auto : la caméra du PC, sinon l'image fixe. Image fixe : une "
         "caméra émulée qui montre l'image ci-dessous. Ou une caméra du PC par son nom (l'image fixe si elle ne s'ouvre pas).");
const char g_wlCameraPictureLabel[] = WL_N("Image fixe (JPG ou PNG)");
const char g_wlCameraPictureHelp[] = WL_N("Le chemin de l'image de la caméra émulée ; vide : une image noire.");
const WlOptionChoice g_wlCameraChoicesBase[] = {
    {"AUTO", WL_N("Auto (la caméra du PC, sinon l'image fixe)")},
    {"PICTURE", WL_N("Image fixe")},
    {NULL, NULL},
};

#define WL_CAMERA_OPTIONS                                                                                                                     \
    {"CAMERA", g_wlCameraLabel, g_wlCameraHelp, WL_OPTION_CHOICE, g_wlCameraChoicesBase, 0, 0, "AUTO", 1, NULL},                             \
    {                                                                                                                                         \
        "CAMERA_PICTURE", g_wlCameraPictureLabel, g_wlCameraPictureHelp, WL_OPTION_TEXT, NULL, 0, 0, "", 1, NULL                              \
    }

// alls.cpp's
static const WlOptionChoice g_allsLanguages[] = {
    {"ENGLISH", WL_N("English")},
    {"JAPANESE", WL_N("Japanese")},
    {"CHINESE", WL_N("Chinese")},
    {NULL, NULL},
};
static const WlOptionChoice g_swdcCabinets[] = {
    {"CABINET", WL_N("Cabinet")},
    {"MINI", WL_N("MiniCabinet")},
    {NULL, NULL},
};
static const WlGameOption g_swdcOptions[] = {
    {"CABINET", WL_N("Type de borne"),
     WL_N("Cabinet : la borne complète, avec son tableau de bord (les boutons du volant, la vitesse) et sa carte LED, et leurs sorties "
          "réseau. MiniCabinet : la petite borne, comme sous TeknoParrot, sans eux. Pris au prochain lancement du jeu."),
     WL_OPTION_CHOICE, g_swdcCabinets, 0, 0, "CABINET", 1, NULL},
    {"LANGUAGE", WL_N("Langue des messages d'amdaemon"),
     WL_N("Les écrans d'erreur et d'avertissement de la plateforme (Error 4103...). Les textes du jeu restent en japonais."),
     WL_OPTION_CHOICE, g_allsLanguages, 0, 0, "ENGLISH", 1, NULL},
};
static const WlGameOption g_idzOptions[] = {
    {"LANGUAGE", WL_N("Langue des messages d'amdaemon"), WL_N("Les écrans d'erreur et d'avertissement de la plateforme."), WL_OPTION_CHOICE,
     g_allsLanguages, 0, 0, "ENGLISH", 1, NULL},
};

// codemasters.cpp's
static const WlGameOption g_showdownOptions[] = {
    {"CABINET", WL_N("Numéro de la borne (link)"),
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "
          "dans le fichier)."),
     WL_OPTION_INT, NULL, 0, 8, "0", 1, NULL},
};
static const WlGameOption g_gridOptions[] = {
    {"CABINET", WL_N("Numéro de la borne (link)"),
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "
          "dans le fichier)."),
     WL_OPTION_INT, NULL, 0, 6, "0", 1, NULL},
};

// daytona.cpp's
static const WlOptionChoice g_daytonaFfb[] = {
    {"BOARD", WL_N("Carte moteur de la borne")},
    {"PORT", WL_N("Octet du port parallèle")},
    {NULL, NULL},
};
static const WlGameOption g_daytonaOptions[] = {
    {"FFB", WL_N("Retour de force"),
     WL_N("Carte moteur de la borne : la carte de Daytona USA jouée d'après les commandes du jeu et la position du volant, avec le "
          "ressort du panneau (comme sm2-emu). Octet du port parallèle : la force que le jeu envoie au moteur, son centrage fait de "
          "forces constantes d'une image à l'autre, qui peut faire osciller un volant puissant."),
     WL_OPTION_CHOICE, g_daytonaFfb, 0, 0, "BOARD", 0, NULL},
    {"DRIVE_DIP", WL_N("Puissance de la carte (interrupteurs)"),
     WL_N("Les interrupteurs de la carte moteur : chaque cran ajoute 4 à la puissance des effets."), WL_OPTION_INT, NULL, 0, 7, "0", 0,
     NULL},
    {"PANEL_SPRING", WL_N("Ressort du panneau (%)"),
     WL_N("Le ressort mécanique du volant de la borne, qui le ramène au centre sans le moteur."), WL_OPTION_INT, NULL, 0, 100, "20", 0,
     NULL},
    {"CABINET", WL_N("Numéro de la borne (link)"),
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "
          "dans le fichier)."),
     WL_OPTION_INT, NULL, 0, 8, "0", 1, NULL},
    WL_CAMERA_OPTIONS,
};

// fordracing.cpp's
static const WlGameOption g_fordracingOptions[] = {
    {"CABINET", WL_N("Numéro de la borne (link)"),
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "
          "dans le fichier)."),
     WL_OPTION_INT, NULL, 0, 8, "0", 1, NULL},
};

// konami.cpp's
static const WlGameOption g_gticlub3Options[] = {
    {"CABINET", WL_N("Numéro de la borne (link)"),
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "
          "dans le fichier)."),
     WL_OPTION_INT, NULL, 0, 8, "0", 1, NULL},
};

// namco.cpp's
static const WlGameOption g_wmmt5Options[] = {
    {"HOLD_GEAR", WL_N("Garder le rapport"),
     WL_N("Le rapport reste engagé quand le levier en H est lâché, pour un levier dont les positions ne tiennent pas."), WL_OPTION_BOOL, NULL, 0, 0, "false", 0, NULL},
    {"CABINET", WL_N("Numéro de la borne (link)"),
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "
          "dans le fichier)."),
     WL_OPTION_INT, NULL, 0, 8, "0", 1, NULL},
};
#define MKDX_CABINET_OPTION                                                                                                             \
    {"CABINET", WL_N("Numéro de la borne (link)"),                                                                                      \
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, " \
          "dans le fichier)."),                                                                                                         \
     WL_OPTION_INT, NULL, 0, 4, "0", 1, NULL}
static const WlGameOption g_mkdxOptions[] = {
    MKDX_CABINET_OPTION,
    WL_CAMERA_OPTIONS,
};
static const WlGameOption g_mkdx105Options[] = {
    MKDX_CABINET_OPTION,
    {"CAMERA_WHEEL_ON", WL_N("Caméra et retour de force activés"),
     WL_N("Coché : NAMCAM et STEERING WHEEL REACTION (GAME OPTIONS du menu de test) mis à ON à chaque démarrage, sans changer le "
          "fichier des réglages du jeu. Décoché : les réglages du jeu tels qu'ils sont."),
     WL_OPTION_BOOL, NULL, 0, 0, "true", 1, NULL},
    WL_CAMERA_OPTIONS,
};

// ringedge.cpp's
static const WlGameOption g_creditOptions[] = {
    {"FREE_PLAY", WL_N("Jeu gratuit"),
     WL_N("Décoché : une partie par pièce. Le menu de test du jeu n'a pas ce réglage (c'est celui du système de la borne qui l'a)."),
     WL_OPTION_BOOL, NULL, 0, 0, "true", 1, NULL},
};
static const WlGameOption g_ssasrOptions[] = {
    {"FREE_PLAY", WL_N("Jeu gratuit"),
     WL_N("Décoché : une partie par pièce. Le menu de test du jeu n'a pas ce réglage (c'est celui du système de la borne qui l'a)."),
     WL_OPTION_BOOL, NULL, 0, 0, "true", 1, NULL},
    {"CABINET", WL_N("Numéro de la borne (link)"),
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "
          "dans le fichier). Le CABINET ID du menu de test du jeu doit être le même."),
     WL_OPTION_INT, NULL, 0, 8, "0", 1, NULL},
};
static const WlGameOption g_srcOptions[] = {
    {"DRIVE_DIP", WL_N("Puissance de la carte (interrupteurs)"),
     WL_N("Les interrupteurs 1 à 3 de la carte moteur (celle de Sega Rally) : chaque cran ajoute 1 à la puissance de chaque couple (32 "
          "le maximum du volant)."),
     WL_OPTION_INT, NULL, 0, 7, "0", 0, NULL},
    {"PANEL_SPRING", WL_N("Ressort du panneau (%)"),
     WL_N("Le ressort mécanique du volant de la borne, qui le ramène au centre sans le moteur."), WL_OPTION_INT, NULL, 0, 100, "20", 0,
     NULL},
    {"CABINET", WL_N("Numéro de la borne (link)"),
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "
          "dans le fichier). Menu de test du jeu, GAME ASSIGNMENTS : la borne 1 en MAIN (TOTAL NODES : le nombre de bornes), la borne k "
          "en SUB, CAR NUMBER k."),
     WL_OPTION_INT, NULL, 0, 4, "0", 1, NULL},
    {"PALETTE_FIX", WL_N("Palette lue au milieu de ses texels"),
     WL_N("Le shader du jeu lit sa palette (la colour RAM) au bord d'un texel, qu'une carte autre qu'ATI arrondit autrement : "
          "windowsloader le remplace par le même, lisant au milieu du texel."),
     WL_OPTION_BOOL, NULL, 0, 0, "true", 1, NULL},
};
static const WlGameOption g_kodriveOptions[] = {
    {"CABINET", WL_N("Numéro de la borne (link)"),
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "
          "dans le fichier). La borne 1 mène la course."),
     WL_OPTION_INT, NULL, 0, 6, "0", 1, NULL},
};

// segarally3.cpp's
static const WlGameOption g_segarally3Options[] = {
    {"CABINET", WL_N("Numéro de la borne (link)"),
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "
          "dans le fichier)."),
     WL_OPTION_INT, NULL, 0, 4, "0", 1, NULL},
};

// stormracer.cpp's
static const WlGameOption g_srgOptions[] = {
    {"CABINET", WL_N("Numéro de la borne (link)"),
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "
          "dans le fichier)."),
     WL_OPTION_INT, NULL, 0, 8, "0", 1, NULL},
};

// unis.cpp's
static const WlGameOption g_crazyrideOptions[] = {
    {"CABINET", WL_N("Numéro de la borne (link)"),
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "
          "dans le fichier)."),
     WL_OPTION_INT, NULL, 0, 8, "0", 1, NULL},
};

// wahlap.cpp's
static const WlGameOption g_stormriderOptions[] = {
    {"CABINET", WL_N("Numéro de la borne (link)"),
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "
          "dans le fichier)."),
     WL_OPTION_INT, NULL, 0, 4, "0", 1, NULL},
};

// taito.cpp's
static const WlOptionChoice g_bg4Cabinets[] = {
    {"STANDARD", WL_N("Standard")},
    {"PRO", WL_N("Pro")},
    {NULL, NULL},
};
static const WlOptionChoice g_bg4Speakers[] = {
    {"CABINET", WL_N("Comme la borne (7.1)")},
    {"STEREO", WL_N("Stéréo")},
    {NULL, NULL},
};
static const WlGameOption g_bg4Options[] = {
    {"CABINET", WL_N("Borne"),
     WL_N("Standard : 4:3, boîte AT ou MT séquentielle. Pro : écran large, embrayage et levier en H (une seconde carte d'E/S). Le jeu "
          "change ses propres réglages au lancement suivant et se ferme une fois : le relancer."),
     WL_OPTION_CHOICE, g_bg4Cabinets, 0, 0, "STANDARD", 1, NULL},
    {"HOLD_GEAR", WL_N("Garder le rapport (borne Pro)"),
     WL_N("Le rapport reste engagé quand le levier en H est lâché, pour un levier dont les positions ne tiennent pas."), WL_OPTION_BOOL, NULL, 0, 0, "false", 1, NULL},
    {"SPEAKERS", WL_N("Haut-parleurs"),
     WL_N("Comme la borne : le jeu joue en 7.1, Windows ramène le son aux haut-parleurs du PC ; sa musique en dépend. Stéréo : "
          "l'ancien réglage, sans la musique."),
     WL_OPTION_CHOICE, g_bg4Speakers, 0, 0, "CABINET", 1, NULL},
    {"NESYS_SERVER", WL_N("Serveur NESYS"),
     WL_N("Adresse[:port] du serveur NESYS (le jeu en ligne), le port 80 par défaut ; 124.146.156.106 par défaut. OFF : le jeu ne "
          "sort pas du PC, NESYS hors ligne."),
     WL_OPTION_TEXT, NULL, 0, 0, "124.146.156.106", 1, NULL},
    // ENTRY_KEY (the key in the reader from the start) is read from the file only: not on the page
    {"KEY_ID", WL_N("Identifiant de la clé NESYS"),
     WL_N("Les 7 lettres ou chiffres de la clé (par exemple AAT1234), lus par le jeu à chaque lecture de la clé ; l'action "
          "« EntryKey » (K par défaut) met la clé ou la retire. Vide : ceux "
          "d'OpenParrot\\KeyId.txt du dossier du jeu (le fichier du KeyManager de NESYS Revival), sinon une clé créée par "
          "windowsloader."),
     WL_OPTION_TEXT, NULL, 0, 0, "", 0, NULL},
};
#define LINK_OPTION(most)                                                                                                                                      \
    {"CABINET", WL_N("Numéro de la borne (link)"),                                                                                                             \
     WL_N("Sa place dans le link, chaque borne la sienne ; 0 : trouvée par l'adresse du PC dans LINK (la liste des adresses des bornes, "                    \
          "dans le fichier)."),                                                                                                                                \
     WL_OPTION_INT, NULL, 0, most, "0", 1, NULL}
static const WlGameOption g_link4Options[] = {LINK_OPTION(4)};
static const WlGameOption g_link2Options[] = {LINK_OPTION(2)};

#define TABLE(id) {#id, g_##id##Options, (int)(sizeof g_##id##Options / sizeof g_##id##Options[0])}
static const struct
{
    const char *id;
    const WlGameOption *options;
    int count;
} g_tables[] = {
    TABLE(swdc),
    TABLE(idz),
    TABLE(showdown),
    TABLE(grid),
    TABLE(daytona),
    TABLE(fordracing),
    TABLE(gticlub3),
    TABLE(wmmt5),
    TABLE(mkdx),
    TABLE(mkdx105),
    TABLE(credit),
    TABLE(ssasr),
    TABLE(src),
    TABLE(kodrive),
    TABLE(segarally3),
    TABLE(srg),
    TABLE(crazyride),
    TABLE(stormrider),
    TABLE(bg4),
    TABLE(link4),
    TABLE(link2),
};

const WlGameOption *wl_game_options_table(const char *id, int *count)
{
    for (int i = 0; id && i < (int)(sizeof g_tables / sizeof g_tables[0]); i++)
        if (!strcmp(g_tables[i].id, id))
        {
            *count = g_tables[i].count;
            return g_tables[i].options;
        }
    *count = 0;
    return NULL;
}

// The modules' choices (wl_game_use_options), by the picker's short name and version: a game's own
// table, or the one of the games alike
const char *wl_game_options_table_of(const char *name, const char *version)
{
    static const struct
    {
        const char *name, *version, *id; // version NULL: any
    } alike[] = {
        // namco.cpp: 1.05 and both 1.06 take its testmode_*.bin's camera and wheel settings
        {"mkdx", "1.05", "mkdx105"},
        {"mkdx", "1.06.35", "mkdx105"},
        {"mkdx", "1.06", "mkdx105"},
        {"mkdx", NULL, "mkdx"},
        {"daytona3", NULL, "daytona"},
        {"daytona3nse", NULL, "daytona"},
        {"wmmt5", NULL, "wmmt5"},
        {"wmmt5dxp", NULL, "wmmt5"},
        {"wmmt6", NULL, "wmmt5"},
        // taito.cpp: the links of 2 and 4 cabinets
        {"chasehq2", NULL, "link2"},
        {"d1gp", NULL, "link2"},
        {"wacky", NULL, "link4"},
        {"vlimitr", NULL, "link4"},
    };
    if (!name)
        return NULL;
    for (int i = 0; i < (int)(sizeof alike / sizeof alike[0]); i++)
        if (!strcmp(alike[i].name, name) && (!alike[i].version || (version && !strcmp(alike[i].version, version))))
            return alike[i].id;
    int count;
    return wl_game_options_table(name, &count) ? name : NULL;
}

void wl_game_option_normalize(const WlGameOption *o, const char *read, char *out, int size)
{
    const char *v = read && read[0] ? read : o->def ? o->def : "";
    if (o->type != WL_OPTION_BOOL)
    {
        snprintf(out, size, "%s", v);
        return;
    }
    int on;
    if (!_stricmp(v, "true") || !_stricmp(v, "yes") || !_stricmp(v, "on"))
        on = 1;
    else if (!_stricmp(v, "false") || !_stricmp(v, "no") || !_stricmp(v, "off"))
        on = 0;
    else
    {
        char *end;
        long n = strtol(v, &end, 10);
        on = end == v ? o->def && !_stricmp(o->def, "true") : n != 0;
    }
    snprintf(out, size, "%s", on ? "true" : "false");
}

int wl_game_option_widget(const WlGameOption *o, char *value, const WlOptionChoice *choices)
{
    int changed = 0;
    if (o->type == WL_OPTION_BOOL)
    {
        bool on = !_stricmp(value, "true");
        if (ImGui::Checkbox(wl_tr(o->label), &on))
        {
            snprintf(value, WL_OPTION_VALUE_SIZE, "%s", on ? "true" : "false");
            changed = 1;
        }
    }
    else if (o->type == WL_OPTION_TEXT)
    {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
        changed = ImGui::InputText(wl_tr(o->label), value, WL_OPTION_VALUE_SIZE);
    }
    else if (o->type == WL_OPTION_CHOICE)
    {
        if (!choices)
            choices = o->choices;
        const char *shown = value; // a value not in the list (a camera of another PC): as it is
        for (const WlOptionChoice *c = choices; c && c->value; c++)
            if (!_stricmp(c->value, value))
                shown = wl_tr(c->label);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
        if (ImGui::BeginCombo(wl_tr(o->label), shown))
        {
            for (const WlOptionChoice *c = choices; c && c->value; c++)
                if (ImGui::Selectable(wl_tr(c->label), !_stricmp(c->value, value)))
                {
                    snprintf(value, WL_OPTION_VALUE_SIZE, "%s", c->value);
                    changed = 1;
                }
            ImGui::EndCombo();
        }
    }
    else
    {
        int n = atoi(value);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
        if (ImGui::SliderInt(wl_tr(o->label), &n, o->min, o->max))
        {
            snprintf(value, WL_OPTION_VALUE_SIZE, "%d", n);
            changed = 1;
        }
    }
    return changed;
}
