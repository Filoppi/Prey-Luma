#include "DKUtil/Config.hpp"

using namespace DKUtil::Alias;

BOOL APIENTRY DllMain(HMODULE a_hModule, DWORD a_ul_reason_for_call, LPVOID a_lpReserved)
{
	if (a_ul_reason_for_call == DLL_PROCESS_ATTACH) {
#ifndef NDEBUG
		while (!IsDebuggerPresent()) {
			Sleep(100);
		}
#endif

		dku::Logger::Init(Plugin::NAME, std::to_string(Plugin::Version));
		INFO("game type : {}", dku::Hook::GetProcessName());

		// do stuff
		// this allocates 1024 bytes for development builds, you can
		// adjust the value accordingly with the log result for release builds
		dku::Hook::Trampoline::AllocTrampoline(1 << 10);
	}

	return TRUE;
}