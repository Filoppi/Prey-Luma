#pragma once

#include <memory>
#include <type_traits>

namespace DKUtil
{
	namespace Hook
	{
		struct RelHookHandle;
		struct ASMPatchHandle;
	}
}

#include "Offsets.h"
#include "RE.h"

// Replacing (splitting up) some RTs is necessary otherwise:
// - If we upgraded all UNORM8 textures to FP16 (like RenoDX does), some texture copies done through the ID3D11DeviceContext::CopyResource() function would fail as they have mismatching formats, plus there's a multitude of other unknown visual issues
// - If we only upgraded the necessary textures (the ones we can without the issue mentioned above), then the tonemapper would be clipped to SDR because it re-uses (e.g.) the normal map texture that is UNORM8, which wouldn't be upgraded (if it is, that also causes issues)
// Note that this, like upgrading textures in general, could theoretically be handled from ReShade DX hooks with "heuristics", for example, we could check what Render Target of Shader Resource a certain shader is using (by hash) and replace the textures we know need replacing.
// For textures upgrades, we could check all the properties that textures have on creation, and based on their order, guess which is which and which needs upgrading
// (e.g. (made up) after the exposure texture is created, which has a particular desc flag, the backbuffer texture might be created, or the "HDRTarget" texture is created with the UAV flag).
#define ADD_NEW_RENDER_TARGETS 1

// Attempted code to keep the native support for MSAA (still doesn't work). CryEngine had it but it's unclear how stable it was in Prey (it's not officially exposed to the user, it can just be forced on through configs)
#define SUPPORT_MSAA 0

// DLSS usually replaces the TAA pass ("PostAA") and writes to its render target, so that's what we are aiming to allow as UAV (which benefits performance by avoid two texture copies),
// but if we ever wanted DLSS to replace SMAA instead, we could also force its RT to be a UAV.
#define FORCE_DLSS_SMAA_UAV 1
// Forces the SMAA target texture to be created with mip maps (more than 1, which would be the base/native texture), so that we can use it for lens distortion.
// This doesn't seem to work well yet, as once we change the game's resolution, for some reason it stops creating mips.
#define FORCE_SMAA_MIPS 0
// Extra optimization for DLSS (breaks the game's native TAA if DLSS is not engaged).
// This also probably results in DirectX debug layer warnings due to possibly the same texture being bound as render target and (pixel) shader resource at the same time (though we wouldn't be using it as shader resource).
#define FORCE_DLSS_SMAA_SLIMMED_DOWN_HISTORY 0

namespace Hooks
{
	// Note: these are mirrored in the ReShade addon code
	constexpr RE::ETEX_Format defaultLDRPostProcessFormat = RE::ETEX_Format::eTF_R16G16B16A16F;
	constexpr RE::ETEX_Format defaultHDRPostProcessFormat = RE::ETEX_Format::eTF_R16G16B16A16F;

	class Patches
	{
	public:
		// Global patch
		static void Patch();

		// Call this to change the texture formats "live", this will be reflected in the game once changing resolution or resetting the graphics settings to default (which creates a new DirectX device).
		// Textures without alpha have might not work (untested).
		static void SetTexturesFormats(RE::ETEX_Format _LDRPostProcessFormat = defaultLDRPostProcessFormat, RE::ETEX_Format _HDRPostProcessFormat = defaultHDRPostProcessFormat);

		static void SetHaltonSequencePhases(unsigned int renderResY, unsigned int outputResY, unsigned int basePhases = 8);
		static void SetHaltonSequencePhases(unsigned int phases = 8);
	};
	class Hooks
	{
	public:
		static void Hook();
		static void Unhook();

	private:
		static void          Hook_OnD3D11PostCreateDevice();
		static bool          Hook_CreateRenderTarget_SceneDiffuse(const char* a_szTexName, RE::CTexture*& a_pTex, int a_iWidth, int a_iHeight, void* a_cClear, bool a_bUseAlpha, bool a_bMipMaps, RE::ETEX_Format a_eTF, int a_nCustomID, RE::ETextureFlags a_nFlags);
		static RE::CTexture* Hook_CreateTextureObject_SceneDiffuse(const char* a_name, uint32_t a_nWidth, uint32_t a_nHeight, int a_nDepth, RE::ETEX_Type a_eTT, RE::ETextureFlags a_nFlags, RE::ETEX_Format a_eTF, int a_nCustomID, uint8_t a9);

		static RE::CTexture* Hook_CreateRenderTarget_PrevBackBuffer0A(const char* a_name, int a_nWidth, int a_nHeight, void* a_cClear, RE::ETEX_Type a_eTT, RE::ETextureFlags a_nFlags, RE::ETEX_Format a_eTF, int a_nCustomID);
		static RE::CTexture* Hook_CreateRenderTarget_PrevBackBuffer1A(const char* a_name, int a_nWidth, int a_nHeight, void* a_cClear, RE::ETEX_Type a_eTT, RE::ETextureFlags a_nFlags, RE::ETEX_Format a_eTF, int a_nCustomID);
		static bool          Hook_CreateRenderTarget_PrevBackBuffer0B(RE::CTexture* a_this, RE::ETEX_Format a_eTF, void* a_cClear);
		static bool          Hook_CreateRenderTarget_PrevBackBuffer1B(RE::CTexture* a_this, RE::ETEX_Format a_eTF, void* a_cClear);

		static void PatchSwapchainDesc(DXGI_SWAP_CHAIN_DESC& a_desc);
		static inline RE::CTexture* ptexTonemapTarget;
		static inline RE::CTexture* ptexPostAATarget;
		static inline RE::CTexture* ptexUpscaleTarget;

		static inline std::unique_ptr<DKUtil::Hook::RelHookHandle> hookHandle_OnD3D11PostCreateDevice = nullptr;
		static inline std::unique_ptr<DKUtil::Hook::RelHookHandle> hookHandle_CreateRenderTarget_SceneDiffuse = nullptr;
		static inline std::unique_ptr<DKUtil::Hook::RelHookHandle> hookHandle_CreateTextureObject_SceneDiffuse = nullptr;
		static inline std::unique_ptr<DKUtil::Hook::RelHookHandle> hookHandle_CreateRenderTarget_PrevBackBuffer0A = nullptr;
		static inline std::unique_ptr<DKUtil::Hook::RelHookHandle> hookHandle_CreateRenderTarget_PrevBackBuffer1A = nullptr;
		static inline std::unique_ptr<DKUtil::Hook::RelHookHandle> hookHandle_CreateRenderTarget_PrevBackBuffer0B = nullptr;
		static inline std::unique_ptr<DKUtil::Hook::RelHookHandle> hookHandle_CreateRenderTarget_PrevBackBuffer1B = nullptr;

		static inline std::unique_ptr<DKUtil::Hook::ASMPatchHandle> asmPatchHandle_swapchain = nullptr;
		static inline std::unique_ptr<DKUtil::Hook::ASMPatchHandle> asmPatchHandle_tonemapTarget1 = nullptr;
		static inline std::unique_ptr<DKUtil::Hook::ASMPatchHandle> asmPatchHandle_tonemapTarget2 = nullptr;
		static inline std::unique_ptr<DKUtil::Hook::ASMPatchHandle> asmPatchHandle_tonemapTarget3 = nullptr;
		static inline std::unique_ptr<DKUtil::Hook::ASMPatchHandle> asmPatchHandle_tonemapTarget4 = nullptr;
		static inline std::unique_ptr<DKUtil::Hook::ASMPatchHandle> asmPatchHandle_postAATarget1 = nullptr;
		static inline std::unique_ptr<DKUtil::Hook::ASMPatchHandle> asmPatchHandle_postAATarget2 = nullptr;
		static inline std::unique_ptr<DKUtil::Hook::ASMPatchHandle> asmPatchHandle_postAATarget3 = nullptr;
		static inline std::unique_ptr<DKUtil::Hook::ASMPatchHandle> asmPatchHandle_upscaleTarget1 = nullptr;
		static inline std::unique_ptr<DKUtil::Hook::ASMPatchHandle> asmPatchHandle_upscaleTarget2 = nullptr;
		static inline std::unique_ptr<DKUtil::Hook::ASMPatchHandle> asmPatchHandle_upscaleTarget3 = nullptr;
	};

	void Install();
	void Uninstall();
}