/**
 * Copyright (C) 2015 Patrick Mours. All rights reserved.
 * License: https://github.com/crosire/d3d8to9#license
 *
 * Taken from https://github.com/crosire/d3d8to9 (commit 255338f, 2026-09-23), BSD 2-clause license
 * (LICENSE.md in this folder). Modified for windowsloader: Direct3DCreate8 replaced by
 * d3d8to9_create below; the other files are unchanged.
 */

#include "d3dx9.hpp"
#include "d3d8to9.hpp"

PFN_D3DXAssembleShader D3DXAssembleShader = nullptr;
PFN_D3DXDisassembleShader D3DXDisassembleShader = nullptr;
PFN_D3DXLoadSurfaceFromSurface D3DXLoadSurfaceFromSurface = nullptr;

#ifndef D3D8TO9NOLOG
 // Very simple logging for the purpose of debugging only.
std::ofstream LOG;
#endif

extern "C" HRESULT WINAPI ValidatePixelShader(const DWORD* pPixelShader, const D3DCAPS8* pCaps, BOOL ReturnErrors, char** pErrorsString)
{
#ifndef D3D8TO9NOLOG
	LOG << "Redirecting '" << "ValidatePixelShader " << "(" << pPixelShader << ", " << pCaps << ", " << ReturnErrors << ", " << pErrorsString << ")' ..." << std::endl;
#endif

	HRESULT hr = E_FAIL;
	const char* errorMessage = "";

	if (!pPixelShader)
	{
		errorMessage = "Invalid code pointer.\n";
	}
	else
	{
		switch (*pPixelShader)
		{
		case D3DPS_VERSION(1, 0):
		case D3DPS_VERSION(1, 1):
		case D3DPS_VERSION(1, 2):
		case D3DPS_VERSION(1, 3):
		case D3DPS_VERSION(1, 4):
			if (pCaps && *pPixelShader > pCaps->PixelShaderVersion)
			{
				errorMessage = "Shader version not supported by caps.\n";
				break;
			}
			hr = S_OK;
			break;

		default:
			errorMessage = "Unsupported shader version.\n";
		}
	}

	if (!ReturnErrors)
	{
		errorMessage = "";
	}

	if (pErrorsString)
	{
		const size_t size = strlen(errorMessage) + 1;

		*pErrorsString = (char*) HeapAlloc(GetProcessHeap(), 0, size);
		if (*pErrorsString)
		{
			memcpy(*pErrorsString, errorMessage, size);
		}
	}

	return hr;
}

extern "C" HRESULT WINAPI ValidateVertexShader(const DWORD* pVertexShader, const DWORD* pVertexDecl, const D3DCAPS8* pCaps, BOOL ReturnErrors, char** pErrorsString)
{
	UNREFERENCED_PARAMETER(pVertexDecl);

#ifndef D3D8TO9NOLOG
	LOG << "Redirecting '" << "ValidateVertexShader " << "(" << pVertexShader << ", " << pVertexDecl << ", " << pCaps << ", " << ReturnErrors << ", " << pErrorsString << ")' ..." << std::endl;
#endif

	HRESULT hr = E_FAIL;
	const char* errorMessage = "";

	if (!pVertexShader)
	{
		errorMessage = "Invalid code pointer.\n";
	}
	else
	{
		switch (*pVertexShader)
		{
		case D3DVS_VERSION(1, 0):
		case D3DVS_VERSION(1, 1):
			if (pCaps && *pVertexShader > pCaps->VertexShaderVersion)
			{
				errorMessage = "Shader version not supported by caps.\n";
				break;
			}
			hr = S_OK;
			break;

		default:
			errorMessage = "Unsupported shader version.\n";
		}
	}

	if (!ReturnErrors)
	{
		errorMessage = "";
	}

	if (pErrorsString)
	{
		const size_t size = strlen(errorMessage) + 1;

		*pErrorsString = (char*) HeapAlloc(GetProcessHeap(), 0, size);
		if (*pErrorsString)
		{
			memcpy(*pErrorsString, errorMessage, size);
		}
	}

	return hr;
}

extern "C" void WINAPI DebugSetMute()
{
#ifndef D3D8TO9NOLOG
	LOG << "Redirecting '" << "DebugSetMute" << "(" << ")' ..." << std::endl;
#endif
}

// windowsloader: Direct3DCreate8 replaced by d3d8to9_create, given the IDirect3D9 that
// windowsloader made (src/hook/d3d8hook.cpp), so that this DLL does not import d3d9.dll; without
// d3dx9_43.dll the shaders that need it fail, nothing is shown to the player (no message box, no
// web page opened). Returns 0 when D3DX is missing (the caller logs it).
int d3d8to9_create(IDirect3D9 *d3d, IDirect3D8 **out)
{
	*out = nullptr;
	if (d3d == nullptr)
	{
		return 1;
	}

	// Load D3DX
	if (!D3DXAssembleShader || !D3DXDisassembleShader || !D3DXLoadSurfaceFromSurface)
	{
		const HMODULE module = LoadLibrary(TEXT("d3dx9_43.dll"));

		if (module != nullptr)
		{
			D3DXAssembleShader = reinterpret_cast<PFN_D3DXAssembleShader>(GetProcAddress(module, "D3DXAssembleShader"));
			D3DXDisassembleShader = reinterpret_cast<PFN_D3DXDisassembleShader>(GetProcAddress(module, "D3DXDisassembleShader"));
			D3DXLoadSurfaceFromSurface = reinterpret_cast<PFN_D3DXLoadSurfaceFromSurface>(GetProcAddress(module, "D3DXLoadSurfaceFromSurface"));
		}
	}

	*out = new Direct3D8(d3d);
	return D3DXAssembleShader && D3DXDisassembleShader && D3DXLoadSurfaceFromSurface;
}
