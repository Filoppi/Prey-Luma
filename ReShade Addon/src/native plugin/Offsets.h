#pragma once

#include "RE.h"

#include <array>
#include <unordered_map>
#include <string>

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

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CD3D9Renderer = { 0x2B24E80, 0x2C9E480, 0x2937080, 0x2A98680 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CCryNameR_Func1 = { 0xEAAE00, 0xECE740, 0xE6F3F0, 0xE940D0 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CCryNameR_Func2 = { 0xF035A0, 0xF26C40, 0xECA220, 0xEEF050 };

	// Patches
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create = { 0x1071510, 0x1094C40, 0x1037CC0, 0x105CB80 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_PrevFrameScaled_1 = { 0x435, 0x435, 0x472, 0x472 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_PrevFrameScaled_2 = { 0x47D, 0x47D, 0x4C2, 0x4C2 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaled_d2_1 = { 0x371, 0x371, 0x35B, 0x35B };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaled_d2_2 = { 0x3B9, 0x3B9, 0x3EF, 0x3EF };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaledTemp_d2_1 = { 0x4F9, 0x4F9, 0x545, 0x545 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaledTemp_d2_2 = { 0x541, 0x541, 0x595, 0x595 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaled_d4_1 = { 0x743, 0x743, 0x7B9, 0x7B9 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaled_d4_2 = { 0x78D, 0x78D, 0x80B, 0x80B };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaledTemp_d4_1 = { 0x80A, 0x80A, 0x892, 0x892 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaledTemp_d4_2 = { 0x852, 0x852, 0x8DA, 0x8DA };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaled_d8_1 = { 0x8FE, 0x8FE, 0x986, 0x986 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SPostEffectsUtils_Create_BackBufferScaled_d8_2 = { 0x92D, 0x92D, 0x9B5, 0x9B5 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateSceneMap = { 0xF57090, 0xF7A490, 0xF1EA60, 0xF438E0 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateSceneMap_BackBuffer_1 = { 0xEE, 0xEE, 0x10B, 0x10B };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateSceneMap_BackBuffer_2 = { 0x151, 0x151, 0x163, 0x163 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CColorGradingControllerD3D_InitResources = { 0xF036D0, 0xF26D70, 0xECA350, 0xEEF180 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CColorGradingControllerD3D_InitResources_ColorGradingMergeLayer0 = { 0xA3, 0xA3, 0x128, 0x128 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CColorGradingControllerD3D_InitResources_ColorGradingMergeLayer1 = { 0xFA, 0xFA, 0x185, 0x185 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateHDRMaps = { 0xF15280, 0xF38920, 0xEDD210, 0xF02040 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateHDRMaps_BitsPerPixel = { 0x11A, 0x116, 0x1D7, 0x1D7 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateHDRMaps_HDRTargetPrev = { 0x14B, 0x147, 0x207, 0x207 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateHDRMaps_HDRTempBloom0 = { 0x52A, 0x4B2, 0xA3D, 0xA3D };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateHDRMaps_HDRTempBloom1 = { 0x5B0, 0x52C, 0xB59, 0xB59 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateHDRMaps_HDRFinalBloom = { 0x630, 0x59C, 0xC79, 0xC79 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateHDRMaps_SceneTargetR11G11B10F_0 = { 0xAB7, 0x9AC, 0x11E5, 0x11E5 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CTexture_GenerateHDRMaps_SceneTargetR11G11B10F_1 = { 0xB52, 0xA2A, 0x1242, 0x1242 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CD3D9Renderer_RT_RenderScene = { 0xF41CA0, 0xF2F6B5, 0xF0A2A0, 0xF64F40 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CD3D9Renderer_RT_RenderScene_Jitters = { 0x57A, 0x5E5, 0x5DC, 0x585 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> OnHFOVChanged = { 0x17266D0, 0x1850B10, 0x16B4260, 0x17DCD10 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> OnHFOVChanged_Offset = { 0x8E, 0x8E, 0x89, 0x89 };

	// Hooks
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SwapchainDesc_Func = { 0xF50000, 0xF733E0, 0xF17790, 0xF3C5F0 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SwapchainDesc_Start = { 0x50E, 0x50E, 0x505, 0x505 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> SwapchainDesc_End = { 0x515, 0x515, 0x50C, 0x50C };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> OnD3D11PostCreateDevice = { 0xF53F66, 0xF77366, 0xF1BA76, 0xF408F6 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CreateRenderTarget_SceneDiffuse = { 0xF083F0, 0xF2BA90, 0xECF250, 0xEF4080 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CreateTextureObject_SceneDiffuse = { 0x100F61F, 0x1032A43, 0xFD1EA9, 0xFF6D9A };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CreateRenderTarget_PrevBackBuffer0A = { 0x1071714, 0x1094E44, 0x1037ECF, 0x105CD8F };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CreateRenderTarget_PrevBackBuffer1A = { 0x10717E7, 0x1094F17, 0x1037FAB, 0x105CE6B };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CreateRenderTarget_PrevBackBuffer0B = { 0x10716E2, 0x1094E12, 0x1037E92, 0x105CD52 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> CreateRenderTarget_PrevBackBuffer1B = { 0x10717B1, 0x1094EE1, 0x1037F78, 0x105CE38 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget1_Func = { 0xFB00C0, 0xFD3450, 0xF73A70, 0xF98880 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget1_Start = { 0x78D, 0x78D, 0x739, 0x739 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget1_End = { 0x794, 0x794, 0x740, 0x740 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget2_Func = { 0xF9C790, 0xFBFA40, 0xF5FF10, 0xF84D20 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget2_Start = { 0x10, 0x10, 0x10, 0x10 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget2_End = { 0x17, 0x17, 0x17, 0x17 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget3_Func = { 0xF3B860, 0xF5EB00, 0xF03A30, 0xF28860 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget3_Start = { 0x105, 0x105, 0xF7, 0xF7 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget3_End = { 0x10C, 0x10C, 0xFE, 0xFE };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget4_Func = { 0xFBD100, 0xFE0490, 0xF80AD0, 0xFA58F0 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget4_Start = { 0x41A, 0x41A, 0x36F, 0x36F };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> TonemapTarget4_End = { 0x421, 0x421, 0x376, 0x376 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> PostAATarget1_Func = { 0xF9C790, 0xFBFA40, 0xF5FF10, 0xF84D20 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> PostAATarget1_Start = { 0x28, 0x28, 0x28, 0x28 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> PostAATarget1_End = { 0x2F, 0x2F, 0x2F, 0x2F };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> PostAATarget2_Func = { 0xF99500, 0xFBC840, 0xF5D2D0, 0xF82170 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> PostAATarget2_Start = { 0x1A, 0x1A, 0x16, 0x16 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> PostAATarget2_End = { 0x21, 0x21, 0x1D, 0x1D };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> PostAATarget3_Func = { 0xF9BBB0, 0xFBEEF0, 0xF5F4C0, 0xF84360 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> PostAATarget3_Start = { 0x9B, 0xA0, 0x9B, 0xA0 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> PostAATarget3_End = { 0xA3, 0xA8, 0xA3, 0xA8 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> UpscaleTarget1_Func = { 0xF9BBB0, 0xFBEEF0, 0xF5F4C0, 0xF84360 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> UpscaleTarget1_Start = { 0x3B, 0x40, 0x3B, 0x40 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> UpscaleTarget1_End = { 0x42, 0x47, 0x42, 0x47 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> UpscaleTarget2_Func = { 0xF7EB20, 0xFA1CF0, 0xF42740, 0xF675F0 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> UpscaleTarget2_Start = { 0x47, 0x47, 0x4C, 0x4C };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> UpscaleTarget2_End = { 0x4E, 0x4E, 0x53, 0x53 };

	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> UpscaleTarget3_Func = { 0xF7EB20, 0xFA1CF0, 0xF42740, 0xF675F0 };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> UpscaleTarget3_Start = { 0x10E, 0x10E, 0x11A, 0x11A };
	static constexpr std::array<uintptr_t, static_cast<uint8_t>(GameVersion::COUNT)> UpscaleTarget3_End = { 0x115, 0x115, 0x121, 0x121 };


	static inline uintptr_t baseAddress;
	static inline GameVersion gameVersion;
	static inline RE::CD3D9Renderer* pCD3D9Renderer = nullptr;
	static inline uint32_t* cvar_r_AntialiasingMode = nullptr; // "INJECT_TAA_JITTERS"

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

	static bool Init()
	{
		auto handle = GetModuleHandle(TEXT("PreyDll.dll"));
		// Crash is unavoidable, there's no purpose in continuing
		if (handle == NULL) {
			return false;
		}
		baseAddress = reinterpret_cast<uintptr_t>(handle);

		uint32_t moduleTimestamp = GetModuleTimestamp(handle);
		const auto search = knownTimestamps.find(moduleTimestamp);
		if (search != knownTimestamps.end()) {
			gameVersion = search->second;
		} else {
			// Default to the latest known built version of the base game
			gameVersion = Offsets::GameVersion::PreyGOG;

			// Warn user that the native mod is probably not going to work, but let them continue just in case.
			std::string guessed_version = "Microsoft/Xbox Game Store"; // We don't know if this version has any particular DLLs
			// The steam dll seems to be loaded before us all the times
			if (GetModuleHandle(TEXT("steam_api64.dll")) == NULL) {
				guessed_version = "Steam";
			}
			else if (GetModuleHandle(TEXT("Galaxy64.dll")) == NULL) {
				guessed_version = "GOG";
			}
			// This might not work
			else if (GetModuleHandle(TEXT("eossdk-win64-shipping.dll")) == NULL) {
				guessed_version = "Epic Game Store";
			}
			// TODO: add "guessed_version" to the message
			if (MessageBoxA(NULL, "\"Prey Luma\" is only compatible with the current Steam and GOG versions of \"Prey (2017)\" and \"Prey: Mooncrash\".\nYou can continue running but it will probably crash.\n\nAbort?", "Incompatible Game Version", MB_OKCANCEL | MB_SETFOREGROUND) == IDOK) {
				exit(0);
				return false;
			}
		}

		pCD3D9Renderer = reinterpret_cast<RE::CD3D9Renderer*>(GetAddress(CD3D9Renderer));
		if (gameVersion == Offsets::GameVersion::PreySteam) {
			cvar_r_AntialiasingMode = reinterpret_cast<uint32_t*>(baseAddress + 0x2B1C750);
		}

		return true;
	}
};