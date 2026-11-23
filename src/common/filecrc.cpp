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


#include "filecrc.h"

#include <stdlib.h>

DWORD wl_file_crc32(const wchar_t *path)
{
    static DWORD table[256];
    if (!table[1])
        for (DWORD i = 0; i < 256; i++)
        {
            DWORD c = i;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? c >> 1 ^ 0xEDB88320 : c >> 1;
            table[i] = c;
        }
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    const DWORD size = 1 << 20;
    unsigned char *buf = (unsigned char *)malloc(size);
    DWORD crc = 0xFFFFFFFF, got = 0;
    BOOL ok = buf != NULL;
    while (ok && (ok = ReadFile(f, buf, size, &got, NULL)) && got)
        for (DWORD i = 0; i < got; i++)
            crc = table[(crc ^ buf[i]) & 0xFF] ^ crc >> 8;
    free(buf);
    CloseHandle(f);
    return ok ? crc ^ 0xFFFFFFFF : 0;
}
