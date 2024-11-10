#pragma once

#include "RE.h"

#include <array>
#include <unordered_map>

class Offsets
{
public:
	enum class GameVersion : uint8_t
	{
		PreySteam,
		MooncrashSteam,
		PreyGOG,
		MooncrashGOG,

		COUNT
	};

	static inline std::unordered_map<uint32_t, GameVersion> knownTimestamps = {
		{ 0x5D1CB240, GameVersion::PreySteam },
		{ 0x5D2352B3, GameVersion::MooncrashSteam },
		{ 0x64827D4E, GameVersion::PreyGOG },
		{ 0x648266FD, GameVersion::MooncrashGOG }
	};

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CD3D9Renderer = { 0x2B24E80, 0x2C9E480 };

	// Patches
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create = { 0x1071510, 0x1094C40 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_PrevFrameScaled_1 = { 0x435, 0x435 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_PrevFrameScaled_2 = { 0x47D, 0x47D };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaled_d2_1 = { 0x371, 0x371 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaled_d2_2 = { 0x3B9, 0x3B9 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaledTemp_d2_1 = { 0x4F9, 0x4F9 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaledTemp_d2_2 = { 0x541, 0x541 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaled_d4_1 = { 0x743, 0x743 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaled_d4_2 = { 0x78D, 0x78D };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaledTemp_d4_1 = { 0x80A, 0x80A };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaledTemp_d4_2 = { 0x852, 0x852 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaled_d8_1 = { 0x8FE, 0x8FE };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaled_d8_2 = { 0x92D, 0x92D };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateSceneMap = { 0xF57090, 0xF7A490 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateSceneMap_BackBuffer_1 = { 0xEE, 0xEE };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateSceneMap_BackBuffer_2 = { 0x151, 0x151 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CColorGradingControllerD3D_InitResources = { 0xF036D0, 0xF26D70 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CColorGradingControllerD3D_InitResources_ColorGradingMergeLayer0 = { 0xA3, 0xA3 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CColorGradingControllerD3D_InitResources_ColorGradingMergeLayer1 = { 0xFA, 0xFA };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateHDRMaps = { 0xF15280, 0xF38920 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateHDRMaps_BitsPerPixel = { 0x11A, 0x116 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateHDRMaps_HDRTargetPrev = { 0x14B, 0x147 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateHDRMaps_HDRTempBloom0 = { 0x52A, 0x4B2 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateHDRMaps_HDRTempBloom1 = { 0x5B0, 0x52C };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateHDRMaps_HDRFinalBloom = { 0x630, 0x59C };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateHDRMaps_SceneTargetR11G11B10F_0 = { 0xAB7, 0x9AC };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateHDRMaps_SceneTargetR11G11B10F_1 = { 0xB52, 0xA2A };

	// Hooks
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SwapchainDesc_Func = { 0xF50000, 0xF733E0 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SwapchainDesc_Start = { 0x50E, 0x50E };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SwapchainDesc_End = { 0x515, 0x515 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> OnD3D11PostCreateDevice = { 0xF53F66, 0xF77366 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CreateRenderTarget = { 0xF083F0, 0xF2BA90 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CreateTextureObject = { 0x100F61F, 0x1032A43 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget1_Func = { 0xFB00C0, 0xFD3450 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget1_Start = { 0x78D, 0x78D };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget1_End = { 0x794, 0x794 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget2_Func = { 0xF9C790, 0xFBFA40 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget2_Start = { 0x10, 0x10 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget2_End = { 0x17, 0x17 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget3_Func = { 0xF3B860, 0xF5EB00 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget3_Start = { 0x105, 0x105 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget3_End = { 0x10C, 0x10C };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget4_Func = { 0xFBD100, 0xFE0490 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget4_Start = { 0x41A, 0x41A };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget4_End = { 0x421, 0x421 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> PostAATarget1_Func = { 0xF9C790, 0xFBFA40 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> PostAATarget1_Start = { 0x28, 0x28 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> PostAATarget1_End = { 0x2F, 0x2F };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> PostAATarget2_Func = { 0xF99500, 0xFBC840 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> PostAATarget2_Start = { 0x1A, 0x1A };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> PostAATarget2_End = { 0x21, 0x21 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> PostAATarget3_Func = { 0xF9BBB0, 0xFBEEF0 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> PostAATarget3_Start = { 0x9B, 0xA0 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> PostAATarget3_End = { 0xA3, 0xA8 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> UpscaleTarget1_Func = { 0xF9BBB0, 0xFBEEF0 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> UpscaleTarget1_Start = { 0x3B, 0x40 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> UpscaleTarget1_End = { 0x42, 0x47 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> UpscaleTarget2_Func = { 0xF7EB20, 0xFA1CF0 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> UpscaleTarget2_Start = { 0x47, 0x47 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> UpscaleTarget2_End = { 0x4E, 0x4E };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> UpscaleTarget3_Func = { 0xF7EB20, 0xFA1CF0 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> UpscaleTarget3_Start = { 0x10E, 0x10E };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> UpscaleTarget3_End = { 0x115, 0x115 };


	static inline uintptr_t baseAddress;
	static inline GameVersion gameVersion;
	static inline RE::CD3D9Renderer* pCD3D9Renderer = nullptr;

	static uintptr_t Get(const std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)>& a_offsetArray) {
		return a_offsetArray[static_cast<size_t>(gameVersion)];
	}

	static uintptr_t GetAddress(const std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)>& a_offsetArray) {
		return baseAddress + a_offsetArray[static_cast<size_t>(gameVersion)];
	}

	static uint32_t GetModuleTimestamp(HMODULE a_moduleHandle) {
		// Get the DOS header
		auto dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(a_moduleHandle);
		if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
			return 0;
		}

		// Get the NT headers
		auto ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(
			reinterpret_cast<uint8_t*>(a_moduleHandle) + dosHeader->e_lfanew);
		if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
			return 0;
		}

		// Retrieve TimeDateStamp from the file header
		return ntHeaders->FileHeader.TimeDateStamp;
	}

	static void Init()
	{
		auto handle = GetModuleHandle(TEXT("PreyDll.dll"));
		baseAddress = reinterpret_cast<uintptr_t>(handle);

		uint32_t moduleTimestamp = GetModuleTimestamp(handle);
		const auto search = knownTimestamps.find(moduleTimestamp);
		if (search != knownTimestamps.end()) {
			gameVersion = search->second;
		} else {
			// TODO: error out
		}

		pCD3D9Renderer = reinterpret_cast<RE::CD3D9Renderer*>(GetAddress(CD3D9Renderer));
	}
};