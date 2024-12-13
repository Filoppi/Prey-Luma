#pragma once

#include <stdint.h>

#include "RE.h"

namespace NativePlugin
{
	void Init(const char* name, uint32_t version = 1);
	void Uninit();

	void SetHaltonSequencePhases(unsigned int renderResY, unsigned int outputResY, unsigned int basePhases = 8);
	void SetHaltonSequencePhases(unsigned int phases = 8);

	void SetTexturesFormat(RE::ETEX_Format LDRPostProcessFormat = RE::ETEX_Format::eTF_R16G16B16A16F, RE::ETEX_Format HDRPostProcessFormat = RE::ETEX_Format::eTF_R16G16B16A16F);
}