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

// The write folder's root and its settings files (writedir.h), as linuxloader's
// src/loader/redirections/writeDir.c makes them (the same author).

#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <wchar.h>

#include "writedir.h"

static int is_folder(const wchar_t *path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

int wl_write_dir_root(const wchar_t *selfDir, const wchar_t *arg, const wchar_t *iniPath, wchar_t *root, size_t size)
{
    wchar_t v[MAX_PATH] = L"";
    if (arg && arg[0])
        wcscpy_s(v, MAX_PATH, arg);
    else if (iniPath && iniPath[0])
        GetPrivateProfileStringW(L"Paths", L"WRITE_DIR", L"", v, MAX_PATH, iniPath); // the quotes dropped by Windows
    if (!v[0])
        return 0;
    wchar_t joined[MAX_PATH];
    if ((v[0] && v[1] == L':') || (v[0] == L'\\' && v[1] == L'\\'))
        wcscpy_s(joined, MAX_PATH, v);
    else
        swprintf(joined, MAX_PATH, L"%s\\%s", selfDir, v);
    if (!GetFullPathNameW(joined, (DWORD)size, root, NULL))
        return 0;
    size_t length = wcslen(root);
    while (length > 3 && (root[length - 1] == L'\\' || root[length - 1] == L'/'))
        root[--length] = 0;
    SHCreateDirectoryExW(NULL, root, NULL);
    return is_folder(root) ? 1 : -1;
}

int wl_write_dir_file(const wchar_t *root, const wchar_t *name, wchar_t *path, size_t size)
{
    wchar_t there[MAX_PATH];
    swprintf(there, MAX_PATH, L"%s\\%s", root, name);
    int older = 0;
    WIN32_FILE_ATTRIBUTE_DATA first, copy;
    if (GetFileAttributesW(there) == INVALID_FILE_ATTRIBUTES && path[0] && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
        CopyFileW(path, there, TRUE);
    else if (path[0] && _wcsicmp(path, there) && GetFileAttributesExW(path, GetFileExInfoStandard, &first) &&
             GetFileAttributesExW(there, GetFileExInfoStandard, &copy))
        older = CompareFileTime(&first.ftLastWriteTime, &copy.ftLastWriteTime) > 0;
    wcscpy_s(path, size, there);
    return older;
}
