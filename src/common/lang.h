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

#pragma once

#include <stddef.h>

// The interface's languages, for the launcher (the picker) and the DLL (the settings menu). The
// texts are written in French in the code: wl_tr("...") where one is shown, WL_N("...") in a table
// shown later (wl_tr then translates the table's text). Each other language is a gettext
// catalogue, lang\<code>.po beside the programs (lang\en.po), read as it is at the start: its msgid
// the French text, its msgstr the translation; an entry left empty or marked fuzzy keeps the French
// text, the header's X-Language-Name is the language's name in the lists. scripts/lang_update.py
// gathers the marked texts in lang\windowsloader.pot and brings the .po files up to date (as
// xgettext --keyword=wl_tr --keyword=WL_N, then msgmerge, would). linuxloader's catalogues are made
// the same way, their source language English.
//
// The language: [Launcher] LANGUAGE of windowsloader.ini, AUTO (Windows' own: French, else its
// catalogue when there is one, pt_BR's then pt's, else English) or a code (fr, en, or that of
// another lang\<code>.po).

// A text marked for the catalogues, translated where it is shown
#define WL_N(text) text

// lang\'s folder (the programs'), the setting (AUTO or a code) and what to tell the log (NULL: nothing)
void wl_lang_init(const wchar_t *dir, const char *setting, void (*log)(const char *message));
// Another setting, the texts in that language at once; the catalogues read before stay in memory
// (the texts wl_tr gave remain valid)
void wl_lang_set(const char *setting);
const char *wl_lang_setting(void); // "AUTO", "fr", "en"...
const char *wl_lang_code(void);    // the language shown: "fr", "en"...

// The French text's translation in the language shown, else the text itself
const char *wl_tr(const char *text);

// The languages that can be chosen: French, then each lang\<code>.po; their code and their name in
// their own language ("English")
int wl_lang_count(void);
const char *wl_lang_code_at(int i);
const char *wl_lang_name_at(int i);

// The choice in a Dear ImGui list (the picker's and the menu's): AUTO, then the languages; 1 when
// another one was chosen (wl_lang_set done)
int wl_lang_combo(const char *label);
