#include "DKUtil/Config.hpp"

#include "Hooks.h"
#include "Offsets.h"

using namespace DKUtil::Alias;

inline DKUtil::Hook::Trampoline::Trampoline& AllocTrampoline(size_t a_size)
{
	using namespace DKUtil::Hook;
	auto& trampoline = Trampoline::GetTrampoline();
	if (!trampoline.capacity()) {
		trampoline.release();

		const auto textx = Module::get("PreyDll.dll"sv).section(Module::Section::textx);
		uintptr_t from = textx.first + textx.second;

		trampoline.PageAlloc(a_size, from);
	}
	return trampoline;
}

BOOL APIENTRY DllMain(HMODULE a_hModule, DWORD a_ul_reason_for_call, LPVOID a_lpReserved)
{
	if (a_ul_reason_for_call == DLL_PROCESS_ATTACH) {
#ifndef NDEBUG
// Enable these if you want to wait until you attached the debugger while in "DEBUG" configuration
#if 0
		if (!IsDebuggerPresent()) {
			MessageBoxA(NULL, "Loaded. You can now attach the debugger or continue execution.", Plugin::NAME.data(), NULL);
		}
#elif 0
		while (!IsDebuggerPresent()) {
			Sleep(100);
		}
#endif
#endif
		dku::Logger::Init(Plugin::NAME, std::to_string(Plugin::Version));

		// do stuff
		AllocTrampoline(1 << 9); // Set the size big enough so that it works

		Offsets::Init();
		Hooks::Install();
	}

	return TRUE;
}