#include "RE.h"

#include "Offsets.h"
#include "DKUtil/Impl/PCH.hpp"
#include "DKUtil/Impl/Hook/Shared.hpp"

namespace RE
{
	CCryNameR::CCryNameR(const char* s)
	{
		using func1_t = void* (*)(CCryNameR*);
		static func1_t func1 = reinterpret_cast<func1_t>(Offsets::baseAddress + 0xEAAE00);

		using func2_t = const char* (*)(void*, const char* s);
		static func2_t func2 = reinterpret_cast<func2_t>(Offsets::baseAddress + 0xF035A0);

		if (s && *s) {
			auto buf = func1(this);
			m_str = func2(buf, s) + 0xC;
			if (m_str) {
				InterlockedIncrement(reinterpret_cast<volatile uint32_t*>(reinterpret_cast<uintptr_t>(m_str) - 0xC));
			}
		}
	}
}
