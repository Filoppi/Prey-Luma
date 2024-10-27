#pragma once

#include <type_traits>

#include "Offsets.h"
#include "RE.h"

// Replacing (splitting up) some RTs is necessary otherwise:
// - If we upgraded all UNORM8 textures to FP16, some texture copies done through the ID3D11DeviceContext::CopyResource() function would fail as they have mismatching formats
// - If we only upgraded the necessary textures (the ones we can without the issue mentioned above), then the tonemapper would be clipped to SDR because it re-uses (e.g.) the normal map texture that is UNORM8
#define ADD_NEW_RENDER_TARGETS 1

// Not necessary anymore
#define INJECT_TAA_JITTERS 0

namespace Hooks
{
	constexpr RE::ETEX_Format format = RE::ETEX_Format::eTF_R16G16B16A16F;
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
		static bool          Hook_CreateRenderTarget(const char* a_szTexName, RE::CTexture*& a_pTex, int a_iWidth, int a_iHeight, void* a_cClear, bool a_bUseAlpha, bool a_bMipMaps, RE::ETEX_Format a_eTF, int a_nCustomID, int a_nFlags);
		static RE::CTexture* Hook_CreateTextureObject(const char* a_name, uint32_t a_nWidth, uint32_t a_nHeight, int a_nDepth, RE::ETEX_Type a_eTT, uint32_t a_nFlags, RE::ETEX_Format a_eTF, int a_nCustomID, uint8_t a9);
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
		static uintptr_t            GetTonemapTargetRT() { return reinterpret_cast<uintptr_t>(ptexTonemapTarget); }
		static uintptr_t            GetPostAATargetRT() { return reinterpret_cast<uintptr_t>(ptexPostAATarget); }
		static uintptr_t            GetUpscaleTargetRT() { return reinterpret_cast<uintptr_t>(ptexUpscaleTarget); }
		static inline RE::CTexture* ptexTonemapTarget;
		static inline RE::CTexture* ptexPostAATarget;
		static inline RE::CTexture* ptexUpscaleTarget;
	};

	void Install();
}