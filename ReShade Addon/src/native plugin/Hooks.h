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

// Upgrading these from R11G11B10F to R16G16B16A16F is "optional" and returns little additional quality for the performance cost.
// Bloom might be the only exception, as it's got a large influence on every pixel of the final scene, so it could bring its quality down.
// Highly specular screen space reflections might also exhibit banding if this is not true (to be researched more accurately).
#define UPGRADE_INTERMEDIARY_TEXTURES 1

// Attempted code to keep the native support for MSAA. CryEngine had it but it's unclear how stable it was in Prey (it's not officially exposed to the user, it can just be forced on through configs)
#define SUPPORT_MSAA 0

// DLSS usually replaces the TAA pass ("PostAA") and writes to its render target, so that's what we are aiming to allow as UAV (which benefits performance by avoid two texture copies),
// but if we ever wanted DLSS to replace SMAA instead, we could also force its RT to be a UAV.
#define FORCE_DLSS_SMAA_UAV 1
// Extra optimization for DLSS (breaks the game's native TAA if DLSS is not engaged).
// This also probably results in DirectX debug layer warnings due to possibly the same texture being bound as render target and (pixel) shader resource at the same time (though we wouldn't be using it as shader resource).
#define FORCE_DLSS_SMAA_SLIMMED_DOWN_HISTORY 0

// Injects the TAA jitter values in the TAA cbuffers.
// Not necessary anymore, we directly intercept them through cbuffer writes. Only compatible with the Steam version of the base game.
#define INJECT_TAA_JITTERS 0

namespace Hooks
{
	// Note: if we wanted, we could replace this format and re-live patch all the functions. After the user changes the game resolution once, all textures would be re-generated with the new format.
	// For example, we could opt in for HDR10 textures (R10G10B10A2UNORM) (which also implies changing the swapchain color space) (the alpha channel might be necessary in some textures though), or classic SDR R8G8B8A8UNORM.
	constexpr RE::ETEX_Format format = RE::ETEX_Format::eTF_R16G16B16A16F; // Generic upgrade format
	constexpr RE::ETEX_Format format16f = RE::ETEX_Format::eTF_R16G16B16A16F; // Specific FP16 format

	class Patches
	{
	public:
		static void Patch();

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
#if INJECT_TAA_JITTERS
		static void          Hook_UpdateBuffer(RE::CConstantBuffer* a_this, void* a_src, size_t a_size, uint32_t a_numDataBlocks);

		//static inline std::add_pointer_t<decltype(Hook_UpdateBuffer)>        _Hook_UpdateBuffer;
#endif

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