#pragma once

#include <type_traits>

#include "Offsets.h"
#include "RE.h"

// Replacing (splitting up) some RTs is necessary otherwise:
// - If we upgraded all UNORM8 textures to FP16 (like RenoDX does), some texture copies done through the ID3D11DeviceContext::CopyResource() function would fail as they have mismatching formats, plus there's a multitude of other unknown visual issues
// - If we only upgraded the necessary textures (the ones we can without the issue mentioned above), then the tonemapper would be clipped to SDR because it re-uses (e.g.) the normal map texture that is UNORM8, which wouldn't be upgraded (if it is, that also causes issues)
#define ADD_NEW_RENDER_TARGETS 1

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
	constexpr RE::ETEX_Format format = RE::ETEX_Format::eTF_R16G16B16A16F; // Generic upgrade format (we could also opt for HDR10 format here)
	constexpr RE::ETEX_Format format16f = RE::ETEX_Format::eTF_R16G16B16A16F;

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

	private:
		static void          Hook_FlashRenderInternal(RE::CD3D9Renderer* a_this, void* pPlayer, bool bStereo, bool bDoRealRender);
		static void          Hook_OnD3D11PostCreateDevice();
		static bool          Hook_CreateRenderTarget_SceneDiffuse(const char* a_szTexName, RE::CTexture*& a_pTex, int a_iWidth, int a_iHeight, void* a_cClear, bool a_bUseAlpha, bool a_bMipMaps, RE::ETEX_Format a_eTF, int a_nCustomID, RE::ETextureFlags a_nFlags);
		static RE::CTexture* Hook_CreateTextureObject_SceneDiffuse(const char* a_name, uint32_t a_nWidth, uint32_t a_nHeight, int a_nDepth, RE::ETEX_Type a_eTT, RE::ETextureFlags a_nFlags, RE::ETEX_Format a_eTF, int a_nCustomID, uint8_t a9);

		static RE::CTexture* Hook_CreateRenderTarget_PrevBackBuffer0A(const char* a_name, int a_nWidth, int a_nHeight, void* a_cClear, RE::ETEX_Type a_eTT, RE::ETextureFlags a_nFlags, RE::ETEX_Format a_eTF, int a_nCustomID);
		static RE::CTexture* Hook_CreateRenderTarget_PrevBackBuffer1A(const char* a_name, int a_nWidth, int a_nHeight, void* a_cClear, RE::ETEX_Type a_eTT, RE::ETextureFlags a_nFlags, RE::ETEX_Format a_eTF, int a_nCustomID);
		static bool          Hook_CreateRenderTarget_PrevBackBuffer0B(RE::CTexture* a_this, RE::ETEX_Format a_eTF, void* a_cClear);
		static bool          Hook_CreateRenderTarget_PrevBackBuffer1B(RE::CTexture* a_this, RE::ETEX_Format a_eTF, void* a_cClear);
#if INJECT_TAA_JITTERS
		static void          Hook_UpdateBuffer(RE::CConstantBuffer* a_this, void* a_src, size_t a_size, uint32_t a_numDataBlocks);
#endif

		static inline std::add_pointer_t<decltype(Hook_FlashRenderInternal)> _Hook_FlashRenderInternal;
		static inline std::add_pointer_t<decltype(Hook_OnD3D11PostCreateDevice)> _Hook_OnD3D11PostCreateDevice;
		static inline std::add_pointer_t<decltype(Hook_CreateRenderTarget_SceneDiffuse)> _Hook_CreateRenderTarget_SceneDiffuse;
		static inline std::add_pointer_t<decltype(Hook_CreateTextureObject_SceneDiffuse)> _Hook_CreateTextureObject_SceneDiffuse;
		static inline std::add_pointer_t<decltype(Hook_CreateRenderTarget_PrevBackBuffer0A)> _Hook_CreateRenderTarget_PrevBackBuffer0A;
		static inline std::add_pointer_t<decltype(Hook_CreateRenderTarget_PrevBackBuffer1A)> _Hook_CreateRenderTarget_PrevBackBuffer1A;
		static inline std::add_pointer_t<decltype(Hook_CreateRenderTarget_PrevBackBuffer0B)> _Hook_CreateRenderTarget_PrevBackBuffer0B;
		static inline std::add_pointer_t<decltype(Hook_CreateRenderTarget_PrevBackBuffer1B)> _Hook_CreateRenderTarget_PrevBackBuffer1B;
#if INJECT_TAA_JITTERS
		static inline std::add_pointer_t<decltype(Hook_UpdateBuffer)>        _Hook_UpdateBuffer;
#endif

		static void PatchSwapchainDesc(DXGI_SWAP_CHAIN_DESC& a_desc);
		static inline RE::CTexture* ptexTonemapTarget;
		static inline RE::CTexture* ptexPostAATarget;
		static inline RE::CTexture* ptexUpscaleTarget;
	};

	void Install();
}