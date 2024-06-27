#pragma once

#include "RE/RE.h"

class Offsets
{
public:
	static inline uintptr_t baseAddress;
	static inline RE::C3DEngine** pC3DEngine = nullptr;

	static void Init()
	{
		baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(TEXT("PreyDll.dll")));
		pC3DEngine = reinterpret_cast<RE::C3DEngine**>(baseAddress + 0x224D988);
	}
};