#include "DKUtil/Config.hpp"

#include "Hooks.h"
#include "Offsets.h"
#include "Settings.h"

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
		while (!IsDebuggerPresent()) {
			Sleep(100);
		}
#endif
		dku::Logger::Init(Plugin::NAME, std::to_string(Plugin::Version));

		// do stuff
		AllocTrampoline(1 << 9);

		Settings::Main::GetSingleton()->Load();

		Offsets::Init();
		Hooks::Install();
	}

	return TRUE;
}