#pragma once

#include "RE.h"

class Offsets
{
public:
	static inline uintptr_t baseAddress;
	static inline RE::C3DEngine** pC3DEngine = nullptr;
	static inline RE::CD3D9Renderer* pCD3D9Renderer = nullptr;
	static inline uint32_t* cvar_r_AntialiasingMode = nullptr;

	static void Init()
	{
		baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(TEXT("PreyDll.dll")));
		pC3DEngine = reinterpret_cast<RE::C3DEngine**>(baseAddress + 0x224D988);
		pCD3D9Renderer = reinterpret_cast<RE::CD3D9Renderer*>(baseAddress + 0x2B24E80);
		cvar_r_AntialiasingMode = reinterpret_cast<uint32_t*>(baseAddress + 0x2B1C750);
	}
};