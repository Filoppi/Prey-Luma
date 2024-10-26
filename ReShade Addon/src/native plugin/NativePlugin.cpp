// c
#include <cassert>
#include <cctype>
#include <cerrno>
#include <cfenv>
#include <cfloat>
#include <cinttypes>
#include <climits>
#include <clocale>
#include <cmath>
#include <csetjmp>
#include <csignal>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cuchar>
#include <cwchar>
#include <cwctype>

// cxx
#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <barrier>
#include <bit>
#include <bitset>
#include <charconv>
#include <chrono>
#include <compare>
#include <complex>
#include <concepts>
#include <condition_variable>
#include <deque>
#include <exception>
#include <execution>
#include <filesystem>
#include <format>
#include <forward_list>
#include <fstream>
#include <functional>
#include <future>
#include <initializer_list>
#include <iomanip>
#include <ios>
#include <iosfwd>
#include <iostream>
#include <istream>
#include <iterator>
#include <latch>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <numbers>
#include <numeric>
#include <optional>
#include <ostream>
#include <queue>
#include <random>
#include <ranges>
#include <ratio>
#include <regex>
#include <scoped_allocator>
#include <semaphore>
#include <set>
#include <shared_mutex>
#include <source_location>
#include <span>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <syncstream>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <valarray>
#include <variant>
#include <vector>
#include <version>

// winnt
#include <ShlObj_core.h>

using namespace std::literals;

//{
//    "name": "packaging-vcpkg",
//        "hidden" : true,
//        "cacheVariables" : {
//        "CMAKE_TOOLCHAIN_FILE": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake",
//            "VCPKG_HOST_TRIPLET" : "x64-windows-static-md",
//            "VCPKG_TARGET_TRIPLET" : "x64-windows-static-md"
//    }
//},

// DKUtil re-defines a lot of random defines (from our code or ReShade's), so we need to back them up and restore them later
#ifdef DEBUG
#define DEBUG_ALT DEBUG
#undef DEBUG
#endif
#ifdef ERROR
#define ERROR_ALT ERROR
#undef ERROR
#endif
#define PLUGIN_MODE
#if 1 // Doesn't work at this moment
#define PROJECT_NAME "Luma"
#else // This is required by their logger
//#define PROJECT_NAME Plugin::NAME
#endif
#include "DKUtil/Config.hpp"
#include "DKUtil/Impl/PCH.hpp"
#include "DKUtil/Hook.hpp"
#include "DKUtil/Logger.hpp"
#include "DKUtil/Impl/Hook/Shared.hpp"

#include "Hooks.h"
#include "Offsets.h"
using namespace DKUtil::Alias;

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

	void main(const char* name, uint32_t version)
	{
		dku::Logger::Init(name, std::to_string(version));

		// do stuff
		AllocTrampoline(1 << 9); // Set the size big enough so that it works

		Offsets::Init();
		Hooks::Install();
	}
}

#undef DEBUG
#ifdef DEBUG_ALT
#define DEBUG DEBUG_ALT
#undef DEBUG_ALT
#endif
#undef ERROR
#ifdef ERROR_ALT
#define ERROR ERROR_ALT
#undef ERROR_ALT
#endif