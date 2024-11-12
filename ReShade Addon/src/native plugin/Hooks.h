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
#define FORCE_DLSS_SMAA_UAV 0

// Not necessary anymore, we directly intercept them through cbuffer writes
#define INJECT_TAA_JITTERS 0

namespace Hooks
{
	constexpr RE::ETEX_Format format = RE::ETEX_Format::eTF_R16G16B16A16F; // Generic upgrade format (we could also opt for HDR10 format here)
	constexpr RE::ETEX_Format format16f = RE::ETEX_Format::eTF_R16G16B16A16F;

	class Patches
	{
	public:
		static void Patch();
	};
	class Hooks
	{
	public:
		static void Hook();

	private:
		static void          Hook_FlashRenderInternal(RE::CD3D9Renderer* a_this, void* pPlayer, bool bStereo, bool bDoRealRender);
		static void          Hook_OnD3D11PostCreateDevice();
		static bool          Hook_CreateRenderTarget(const char* a_szTexName, RE::CTexture*& a_pTex, int a_iWidth, int a_iHeight, void* a_cClear, bool a_bUseAlpha, bool a_bMipMaps, RE::ETEX_Format a_eTF, int a_nCustomID = -1, /*RE::ETextureFlags*/ int a_nFlags = 0);
		static RE::CTexture* Hook_CreateTextureObject(const char* a_name, uint32_t a_nWidth, uint32_t a_nHeight, int a_nDepth, RE::ETEX_Type a_eTT, /*RE::ETextureFlags*/ uint32_t a_nFlags, RE::ETEX_Format a_eTF, int a_nCustomID /*= -1*/, uint8_t a9);
#if INJECT_TAA_JITTERS
		static void          Hook_UpdateBuffer(RE::CConstantBuffer* a_this, void* a_src, size_t a_size, uint32_t a_numDataBlocks);
#endif

		static inline std::add_pointer_t<decltype(Hook_FlashRenderInternal)> _Hook_FlashRenderInternal;
		static inline std::add_pointer_t<decltype(Hook_OnD3D11PostCreateDevice)> _Hook_OnD3D11PostCreateDevice;
		static inline std::add_pointer_t<decltype(Hook_CreateRenderTarget)> _Hook_CreateRenderTarget;
		static inline std::add_pointer_t<decltype(Hook_CreateTextureObject)> _Hook_CreateTextureObject;
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