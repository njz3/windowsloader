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

// A shadow projection left all zeros (hooks.h, wl_d3d9_zero_projection). NASCAR's night track
// (Richmond) has no sun shadow: its shadowProjection matrix is 0, yet its pixel shaders are told
// shadowed and divide the projected coordinates by their w (rcp of 0 then times 0, or texldp): NaN
// on today's graphics cards (IEEE), carried into the whole colour, the track, walls and stands drawn
// black (the cards of its time gave a number there). For such a draw (the bound vertex shader's
// matrix all zeros), the matrix's last row made (0, 0, 0, 1) while it draws (w = 1: no NaN), and
// the bound pixel shader's flag (by its name in its constant table), when it has one, made 0: the
// colour without the shadow.

#include <windows.h>
#include <d3d9.h>
#include <string.h>

#include "hooks.h"
#include "log.h"

#define MAX_SHADERS 1024

struct Shader
{
    void *shader;
    int reg;
};

static char g_matrix[64], g_flag[64];
static Shader g_vs[MAX_SHADERS], g_ps[MAX_SHADERS];
static LONG g_vsCount, g_psCount;
static SRWLOCK g_lock = SRWLOCK_INIT;
static int g_vsReg = -1, g_psReg = -1; // the bound shaders' registers (one device draws)

void wl_d3d9_zero_projection(const char *matrix, const char *flag)
{
    strncpy_s(g_matrix, sizeof g_matrix, matrix, _TRUNCATE);
    strncpy_s(g_flag, sizeof g_flag, flag, _TRUNCATE);
}

int wl_d3d9_zero_projection_on(void)
{
    return g_matrix[0] != 0;
}

// The float register of the constant named name in a shader's constant table (its CTAB comment),
// -1 for none
static int ctab_register(const DWORD *code, const char *name)
{
    const DWORD *t = code + 1;
    for (int guard = 0; guard < 4096; guard++)
    {
        DWORD token = *t;
        if ((token & 0xFFFF) != 0xFFFE) // the comments come first, before the instructions
            break;
        DWORD len = (token >> 16) & 0x7FFF;
        const unsigned char *data = (const unsigned char *)(t + 1);
        if (len >= 8 && !memcmp(data, "CTAB", 4))
        {
            const unsigned char *table = data + 4;
            size_t size = (size_t)len * 4 - 4;
            DWORD count = *(const DWORD *)(table + 12), info = *(const DWORD *)(table + 16);
            for (DWORD i = 0; i < count && info + (i + 1) * 20 <= size; i++)
            {
                const unsigned char *c = table + info + i * 20;
                DWORD nameAt = *(const DWORD *)c;
                WORD set = *(const WORD *)(c + 4), reg = *(const WORD *)(c + 6);
                if (nameAt < size && set == 2 /* D3DXRS_FLOAT4 */ && !strncmp((const char *)table + nameAt, name, size - nameAt))
                    return reg;
            }
            return -1;
        }
        t += 1 + len;
    }
    return -1;
}

static void remember(Shader *list, LONG *count, void *shader, int reg)
{
    AcquireSRWLockExclusive(&g_lock);
    if (*count < MAX_SHADERS)
    {
        list[*count].shader = shader;
        list[*count].reg = reg;
        (*count)++;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

static int lookup(const Shader *list, LONG count, void *shader)
{
    int reg = -1;
    if (!shader)
        return -1;
    AcquireSRWLockShared(&g_lock);
    for (LONG i = 0; i < count; i++)
        if (list[i].shader == shader)
        {
            reg = list[i].reg;
            break;
        }
    ReleaseSRWLockShared(&g_lock);
    return reg;
}

static HRESULT(STDMETHODCALLTYPE *oCreateVertexShader)(IDirect3DDevice9 *, const DWORD *, IDirect3DVertexShader9 **);
static HRESULT STDMETHODCALLTYPE hCreateVertexShader(IDirect3DDevice9 *dev, const DWORD *code, IDirect3DVertexShader9 **out)
{
    HRESULT hr = oCreateVertexShader(dev, code, out);
    int reg;
    if (SUCCEEDED(hr) && code && out && *out && (reg = ctab_register(code, g_matrix)) >= 0)
        remember(g_vs, &g_vsCount, *out, reg);
    return hr;
}

static HRESULT(STDMETHODCALLTYPE *oCreatePixelShader)(IDirect3DDevice9 *, const DWORD *, IDirect3DPixelShader9 **);
static HRESULT STDMETHODCALLTYPE hCreatePixelShader(IDirect3DDevice9 *dev, const DWORD *code, IDirect3DPixelShader9 **out)
{
    HRESULT hr = oCreatePixelShader(dev, code, out);
    int reg;
    if (SUCCEEDED(hr) && code && out && *out && (reg = ctab_register(code, g_flag)) >= 0)
        remember(g_ps, &g_psCount, *out, reg);
    return hr;
}

static HRESULT(STDMETHODCALLTYPE *oSetVertexShader)(IDirect3DDevice9 *, IDirect3DVertexShader9 *);
static HRESULT STDMETHODCALLTYPE hSetVertexShader(IDirect3DDevice9 *dev, IDirect3DVertexShader9 *vs)
{
    g_vsReg = lookup(g_vs, g_vsCount, vs);
    return oSetVertexShader(dev, vs);
}

static HRESULT(STDMETHODCALLTYPE *oSetPixelShader)(IDirect3DDevice9 *, IDirect3DPixelShader9 *);
static HRESULT STDMETHODCALLTYPE hSetPixelShader(IDirect3DDevice9 *dev, IDirect3DPixelShader9 *ps)
{
    g_psReg = lookup(g_ps, g_psCount, ps);
    return oSetPixelShader(dev, ps);
}

// Before a draw whose vertex shader's matrix is all zeros: its last row made (0, 0, 0, 1) (the
// coordinates divided by 1, not 0: no NaN) and the pixel shader's flag, when it has one, made 0 (the
// colour without the shadow); put back after
struct Fix
{
    int row, flag;
    float saved[4];
};

static void fix(IDirect3DDevice9 *dev, Fix *f)
{
    f->row = f->flag = 0;
    if (g_vsReg < 0)
        return;
    float m[16];
    if (FAILED(dev->GetVertexShaderConstantF(g_vsReg, m, 4)))
        return;
    for (int i = 0; i < 16; i++)
        if (m[i] != 0)
            return;
    static const float row[4] = {0, 0, 0, 1}, zero[4] = {0, 0, 0, 0};
    dev->SetVertexShaderConstantF(g_vsReg + 3, row, 1);
    f->row = 1;
    if (g_psReg >= 0 && SUCCEEDED(dev->GetPixelShaderConstantF(g_psReg, f->saved, 1)) && f->saved[0] != 0)
    {
        dev->SetPixelShaderConstantF(g_psReg, zero, 1);
        f->flag = 1;
    }
    static int told;
    if (!told++)
        wl_log("d3d9: %s all zeros at a draw: projected by w = 1, drawn without the shadow", g_matrix);
}

static void unfix(IDirect3DDevice9 *dev, const Fix *f)
{
    static const float zero[4] = {0, 0, 0, 0};
    if (f->row)
        dev->SetVertexShaderConstantF(g_vsReg + 3, zero, 1);
    if (f->flag)
        dev->SetPixelShaderConstantF(g_psReg, f->saved, 1);
}

static HRESULT(STDMETHODCALLTYPE *oDrawPrimitive)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT);
static HRESULT STDMETHODCALLTYPE hDrawPrimitive(IDirect3DDevice9 *dev, D3DPRIMITIVETYPE type, UINT start, UINT count)
{
    Fix f;
    fix(dev, &f);
    HRESULT hr = oDrawPrimitive(dev, type, start, count);
    unfix(dev, &f);
    return hr;
}

static HRESULT(STDMETHODCALLTYPE *oDrawIndexedPrimitive)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
static HRESULT STDMETHODCALLTYPE hDrawIndexedPrimitive(IDirect3DDevice9 *dev, D3DPRIMITIVETYPE type, INT base, UINT minIndex, UINT vertices,
                                                       UINT startIndex, UINT count)
{
    Fix f;
    fix(dev, &f);
    HRESULT hr = oDrawIndexedPrimitive(dev, type, base, minIndex, vertices, startIndex, count);
    unfix(dev, &f);
    return hr;
}
// From d3d9hook.cpp, at the first device (its vtable)
void wl_d3d9_zero_projection_hooks(void **vt)
{
    wl_log("d3d9: a vertex shader's %s all zeros: projected by w = 1 and its pixel shader's %s 0 while it draws", g_matrix, g_flag);
    wl_hook_code(vt[91], (void *)hCreateVertexShader, (void **)&oCreateVertexShader, "IDirect3DDevice9::CreateVertexShader (zero projection)");
    wl_hook_code(vt[92], (void *)hSetVertexShader, (void **)&oSetVertexShader, "IDirect3DDevice9::SetVertexShader (zero projection)");
    wl_hook_code(vt[106], (void *)hCreatePixelShader, (void **)&oCreatePixelShader, "IDirect3DDevice9::CreatePixelShader (zero projection)");
    wl_hook_code(vt[107], (void *)hSetPixelShader, (void **)&oSetPixelShader, "IDirect3DDevice9::SetPixelShader (zero projection)");
    wl_hook_code(vt[81], (void *)hDrawPrimitive, (void **)&oDrawPrimitive, "IDirect3DDevice9::DrawPrimitive (zero projection)");
    wl_hook_code(vt[82], (void *)hDrawIndexedPrimitive, (void **)&oDrawIndexedPrimitive, "IDirect3DDevice9::DrawIndexedPrimitive (zero projection)");
}
