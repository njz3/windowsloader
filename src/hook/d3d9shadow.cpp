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
// (Richmond) has no sun shadow: its shadowProjection matrix is 0, yet its pixel shaders read the
// shadow, dividing the projected coordinates by their w (rcp of 0 then times 0, or texldp): NaN on
// today's graphics cards (IEEE), carried into the whole colour, the track, walls and stands drawn
// black (the cards of its time gave a number there). The shadow texture then bound is whatever an
// earlier draw left (none for that track): lit on one card (RTX 3050), black on another (GTX 1050
// Ti). For such a draw (the bound vertex shader's matrix all zeros), while it draws: the matrix's
// last row made (0, 0, 0, 1) (w = 1: no NaN), a white texture of 1x1 given to the pixel shader's
// shadow sampler (no shadow), and its flag, when it has one, made 0 (the colour without the shadow).
// The shaders known by their names in their constant tables.

#include <windows.h>
#include <d3d9.h>
#include <string.h>

#include "hooks.h"
#include "log.h"

#define MAX_SHADERS 1024

struct Shader
{
    void *shader;
    int reg;     // the vertex shader's matrix, the pixel shader's flag (-1 for none)
    int sampler; // the pixel shader's shadow sampler (-1 for none)
};

static char g_matrix[64], g_flag[64], g_sampler[64];
static Shader g_vs[MAX_SHADERS], g_ps[MAX_SHADERS];
static LONG g_vsCount, g_psCount;
static SRWLOCK g_lock = SRWLOCK_INIT;
static Shader g_boundVs = {NULL, -1, -1}, g_boundPs = {NULL, -1, -1}; // one device draws
static IDirect3DTexture9 *g_white;
static int g_whiteFailed;

// The constants as the game set them, kept here: a pure device does not give them back
// (GetVertexShaderConstantF fails)
#define VS_CONSTANTS 256
#define PS_CONSTANTS 224
static float g_vsc[VS_CONSTANTS][4], g_psc[PS_CONSTANTS][4];

static void keep(float (*copy)[4], int size, UINT start, const float *data, UINT count)
{
    if (!data)
        return;
    for (UINT i = 0; i < count && start + i < (UINT)size; i++)
        memcpy(copy[start + i], data + i * 4, sizeof copy[0]);
}

static HRESULT(STDMETHODCALLTYPE *oSetVertexShaderConstantF)(IDirect3DDevice9 *, UINT, const float *, UINT);
static HRESULT STDMETHODCALLTYPE hSetVertexShaderConstantF(IDirect3DDevice9 *dev, UINT start, const float *data, UINT count)
{
    HRESULT hr = oSetVertexShaderConstantF(dev, start, data, count);
    if (SUCCEEDED(hr))
        keep(g_vsc, VS_CONSTANTS, start, data, count);
    return hr;
}

static HRESULT(STDMETHODCALLTYPE *oSetPixelShaderConstantF)(IDirect3DDevice9 *, UINT, const float *, UINT);
static HRESULT STDMETHODCALLTYPE hSetPixelShaderConstantF(IDirect3DDevice9 *dev, UINT start, const float *data, UINT count)
{
    HRESULT hr = oSetPixelShaderConstantF(dev, start, data, count);
    if (SUCCEEDED(hr))
        keep(g_psc, PS_CONSTANTS, start, data, count);
    return hr;
}

void wl_d3d9_zero_projection(const char *matrix, const char *flag, const char *sampler)
{
    strncpy_s(g_matrix, sizeof g_matrix, matrix, _TRUNCATE);
    strncpy_s(g_flag, sizeof g_flag, flag ? flag : "", _TRUNCATE);
    strncpy_s(g_sampler, sizeof g_sampler, sampler ? sampler : "", _TRUNCATE);
}

int wl_d3d9_zero_projection_on(void)
{
    return g_matrix[0] != 0;
}

// The register of the constant named name in a shader's constant table (its CTAB comment), of the
// set given (2 float4, 3 sampler); -1 for none
static int ctab_register(const DWORD *code, const char *name, WORD wanted)
{
    if (!name[0])
        return -1;
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
                if (nameAt < size && set == wanted && !strncmp((const char *)table + nameAt, name, size - nameAt))
                    return reg;
            }
            return -1;
        }
        t += 1 + len;
    }
    return -1;
}

static void remember(Shader *list, LONG *count, void *shader, int reg, int sampler)
{
    AcquireSRWLockExclusive(&g_lock);
    if (*count < MAX_SHADERS)
    {
        list[*count].shader = shader;
        list[*count].reg = reg;
        list[*count].sampler = sampler;
        (*count)++;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

static Shader lookup(const Shader *list, LONG count, void *shader)
{
    Shader s = {shader, -1, -1};
    if (!shader)
        return s;
    AcquireSRWLockShared(&g_lock);
    for (LONG i = 0; i < count; i++)
        if (list[i].shader == shader)
        {
            s = list[i];
            break;
        }
    ReleaseSRWLockShared(&g_lock);
    return s;
}

static HRESULT(STDMETHODCALLTYPE *oCreateVertexShader)(IDirect3DDevice9 *, const DWORD *, IDirect3DVertexShader9 **);
static HRESULT STDMETHODCALLTYPE hCreateVertexShader(IDirect3DDevice9 *dev, const DWORD *code, IDirect3DVertexShader9 **out)
{
    HRESULT hr = oCreateVertexShader(dev, code, out);
    int reg;
    if (SUCCEEDED(hr) && code && out && *out && (reg = ctab_register(code, g_matrix, 2)) >= 0)
        remember(g_vs, &g_vsCount, *out, reg, -1);
    return hr;
}

static HRESULT(STDMETHODCALLTYPE *oCreatePixelShader)(IDirect3DDevice9 *, const DWORD *, IDirect3DPixelShader9 **);
static HRESULT STDMETHODCALLTYPE hCreatePixelShader(IDirect3DDevice9 *dev, const DWORD *code, IDirect3DPixelShader9 **out)
{
    HRESULT hr = oCreatePixelShader(dev, code, out);
    if (SUCCEEDED(hr) && code && out && *out)
    {
        int flag = ctab_register(code, g_flag, 2), sampler = ctab_register(code, g_sampler, 3);
        if (flag >= 0 || sampler >= 0)
            remember(g_ps, &g_psCount, *out, flag, sampler);
    }
    return hr;
}

static HRESULT(STDMETHODCALLTYPE *oSetVertexShader)(IDirect3DDevice9 *, IDirect3DVertexShader9 *);
static HRESULT STDMETHODCALLTYPE hSetVertexShader(IDirect3DDevice9 *dev, IDirect3DVertexShader9 *vs)
{
    g_boundVs = lookup(g_vs, g_vsCount, vs);
    return oSetVertexShader(dev, vs);
}

static HRESULT(STDMETHODCALLTYPE *oSetPixelShader)(IDirect3DDevice9 *, IDirect3DPixelShader9 *);
static HRESULT STDMETHODCALLTYPE hSetPixelShader(IDirect3DDevice9 *dev, IDirect3DPixelShader9 *ps)
{
    g_boundPs = lookup(g_ps, g_psCount, ps);
    return oSetPixelShader(dev, ps);
}

// A white texture of 1x1 (managed: kept through a Reset), made at its first use
static IDirect3DTexture9 *white(IDirect3DDevice9 *dev)
{
    if (g_white || g_whiteFailed)
        return g_white;
    IDirect3DTexture9 *t = NULL;
    D3DLOCKED_RECT r;
    if (SUCCEEDED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, NULL)) && SUCCEEDED(t->LockRect(0, &r, NULL, 0)))
    {
        *(DWORD *)r.pBits = 0xFFFFFFFF;
        t->UnlockRect(0);
        g_white = t;
    }
    else
    {
        if (t)
            t->Release();
        g_whiteFailed = 1;
        wl_log("d3d9: the white texture for %s not made: the shadow texture left as it is", g_sampler);
    }
    return g_white;
}

// Before a draw whose vertex shader's matrix is all zeros: what is changed, put back after
struct Fix
{
    int row, flag, sampler;
    float saved[4];
    IDirect3DBaseTexture9 *texture; // the one the shadow sampler had (a reference)
};

static void fix(IDirect3DDevice9 *dev, Fix *f)
{
    f->row = f->flag = 0;
    f->sampler = -1;
    f->texture = NULL;
    int reg = g_boundVs.reg;
    if (reg < 0 || reg + 4 > VS_CONSTANTS)
        return;
    for (int r = 0; r < 4; r++)
        for (int i = 0; i < 4; i++)
            if (g_vsc[reg + r][i] != 0)
                return;
    static const float row[4] = {0, 0, 0, 1}, zero[4] = {0, 0, 0, 0};
    dev->SetVertexShaderConstantF(reg + 3, row, 1);
    f->row = 1;
    Shader ps = g_boundPs;
    if (ps.reg >= 0 && ps.reg < PS_CONSTANTS && g_psc[ps.reg][0] != 0)
    {
        memcpy(f->saved, g_psc[ps.reg], sizeof f->saved);
        dev->SetPixelShaderConstantF(ps.reg, zero, 1);
        f->flag = 1;
    }
    IDirect3DTexture9 *w;
    if (ps.sampler >= 0 && ps.sampler < 16 && (w = white(dev)) != NULL && SUCCEEDED(dev->GetTexture(ps.sampler, &f->texture)))
    {
        dev->SetTexture(ps.sampler, w);
        f->sampler = ps.sampler;
    }
    static int told;
    if (!told++)
        wl_log("d3d9: %s all zeros at a draw: projected by w = 1, drawn without the shadow (%s 0, %s white)", g_matrix, g_flag, g_sampler);
}

static void unfix(IDirect3DDevice9 *dev, const Fix *f)
{
    static const float zero[4] = {0, 0, 0, 0};
    if (f->row)
        dev->SetVertexShaderConstantF(g_boundVs.reg + 3, zero, 1);
    if (f->flag)
        dev->SetPixelShaderConstantF(g_boundPs.reg, f->saved, 1);
    if (f->sampler >= 0)
    {
        dev->SetTexture(f->sampler, f->texture);
        if (f->texture)
            f->texture->Release();
    }
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
    wl_log("d3d9: a vertex shader's %s all zeros: projected by w = 1, its pixel shader's %s 0 and %s white while it draws", g_matrix, g_flag,
           g_sampler);
    wl_hook_code(vt[91], (void *)hCreateVertexShader, (void **)&oCreateVertexShader, "IDirect3DDevice9::CreateVertexShader (zero projection)");
    wl_hook_code(vt[92], (void *)hSetVertexShader, (void **)&oSetVertexShader, "IDirect3DDevice9::SetVertexShader (zero projection)");
    wl_hook_code(vt[106], (void *)hCreatePixelShader, (void **)&oCreatePixelShader, "IDirect3DDevice9::CreatePixelShader (zero projection)");
    wl_hook_code(vt[107], (void *)hSetPixelShader, (void **)&oSetPixelShader, "IDirect3DDevice9::SetPixelShader (zero projection)");
    wl_hook_code(vt[94], (void *)hSetVertexShaderConstantF, (void **)&oSetVertexShaderConstantF,
                 "IDirect3DDevice9::SetVertexShaderConstantF (zero projection)");
    wl_hook_code(vt[109], (void *)hSetPixelShaderConstantF, (void **)&oSetPixelShaderConstantF,
                 "IDirect3DDevice9::SetPixelShaderConstantF (zero projection)");
    wl_hook_code(vt[81], (void *)hDrawPrimitive, (void **)&oDrawPrimitive, "IDirect3DDevice9::DrawPrimitive (zero projection)");
    wl_hook_code(vt[82], (void *)hDrawIndexedPrimitive, (void **)&oDrawIndexedPrimitive, "IDirect3DDevice9::DrawIndexedPrimitive (zero projection)");
}
