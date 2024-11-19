#pragma once

#include <stdint.h>

namespace NativePlugin
{
	void Init(const char* name, uint32_t version = 1);

	void SetHaltonSequencePhases(unsigned int renderResY, unsigned int outputResY, unsigned int basePhases = 8);
	void SetHaltonSequencePhases(unsigned int phases = 8);
}