#include "NativePlugin.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <ShlObj_core.h> // winnt

#include "includes/SharedBegin.h"

#include "DKUtil/Config.hpp"
#include "DKUtil/Hook.hpp"
#include "DKUtil/Logger.hpp"

#include "Hooks.h"
#include "Offsets.h"

using namespace DKUtil::Alias;
using namespace std::literals;

namespace NativePlugin
{
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

	void Init(const char* name, uint32_t version)
	{
#ifndef NDEBUG
        // It's unclear what happens if we don't initialize this, but it seems fine
		dku::Logger::Init(name, std::to_string(version));
#endif

		AllocTrampoline(1 << 9); // Set the size big enough so that it works

		Offsets::Init();
		Hooks::Install();
	}

    void Uninit()
    {
        Hooks::Uninstall();
    }

    void SetHaltonSequencePhases(unsigned int renderResY, unsigned int outputResY, unsigned int basePhases)
    {
        Hooks::Patches::SetHaltonSequencePhases(renderResY, outputResY, basePhases);
    }
    void SetHaltonSequencePhases(unsigned int phases)
    {
        Hooks::Patches::SetHaltonSequencePhases(phases);
    }
}

#include "includes/SharedEnd.h"