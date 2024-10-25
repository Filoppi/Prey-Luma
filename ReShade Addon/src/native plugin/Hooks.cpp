#include "Hooks.h"

#include <dxgi1_4.h>

#include "DKUtil/Impl/PCH.hpp"
#include "DKUtil/Impl/Hook/Shared.hpp"

namespace Hooks
{
	ID3D11DeviceContext* GetDeviceContext(RE::CD3D9Renderer* a_renderer)
	{
		if (a_renderer->m_nAsyncDeviceState) {
			while (a_renderer->m_nAsyncDeviceState) {
				SwitchToThread();
			}
		}
		return a_renderer->m_pDeviceContext;
	}

	void Hooks::Hook_FlashRenderInternal(RE::CD3D9Renderer* a_this, void* pPlayer, bool bStereo, bool bDoRealRender)
	{
		if (bDoRealRender) {
			auto context = GetDeviceContext(a_this);
			context->OMSetRenderTargets(1, &a_this->m_pBackBuffer, a_this->m_pNativeZSurface);
		}

		_Hook_FlashRenderInternal(a_this, pPlayer, bStereo, bDoRealRender);
	}

	bool Hooks::Hook_CreateDevice(RE::DeviceInfo* a_deviceInfo, uint64_t a2, uint64_t a3, uint64_t a4, int32_t a_width, int32_t a_height, int32_t a7, int32_t a_zbpp, void* a9, void* a10)
	{
		bool bReturn = _Hook_CreateDevice(a_deviceInfo, a2, a3, a4, a_width, a_height, a7, a_zbpp, a9, a10);

		// set colorspace
		IDXGISwapChain3* swapChain3 = nullptr;
		a_deviceInfo->m_pSwapChain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&swapChain3));

		DXGI_COLOR_SPACE_TYPE colorSpace;
		if (format == RE::ETEX_Format::eTF_R10G10B10A2) {
			colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
		} else {
			colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
		}

		swapChain3->SetColorSpace1(colorSpace);
		swapChain3->Release();

		return bReturn;
	}

	// Despite the name, this is called just once on startup, and then again when changing resolution
	bool Hooks::Hook_CreateRenderTarget(const char* a_szTexName, RE::CTexture*& a_pTex, int a_iWidth, int a_iHeight, void* a_cClear, bool a_bUseAlpha, bool a_bMipMaps, RE::ETEX_Format a_eTF, int a_nCustomID, int a_nFlags)
	{
		bool bReturn = _Hook_CreateRenderTarget(a_szTexName, a_pTex, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, a_eTF, a_nCustomID, a_nFlags);

#if ADD_NEW_RENDER_TARGETS
		// add ours
		_Hook_CreateRenderTarget("$TonemapTarget", ptexTonemapTarget, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, format, -1, a_nFlags);
		_Hook_CreateRenderTarget("$PostAATarget", ptexPostAATarget, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, format, -1, a_nFlags);
		_Hook_CreateRenderTarget("$UpscaleTarget", ptexUpscaleTarget, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, format, -1, a_nFlags);
#endif

		return bReturn;
	}

	// Despite the name, this is called just once on startup, and then again when changing resolution
	RE::CTexture* Hooks::Hook_CreateTextureObject(const char* a_name, uint32_t a_nWidth, uint32_t a_nHeight, int a_nDepth, RE::ETEX_Type a_eTT, uint32_t a_nFlags, RE::ETEX_Format a_eTF, int a_nCustomID, uint8_t a9)
	{
		RE::CTexture* pTex = _Hook_CreateTextureObject(a_name, a_nWidth, a_nHeight, a_nDepth, a_eTT, a_nFlags, a_eTF, a_nCustomID, a9);

#if ADD_NEW_RENDER_TARGETS
		// add ours
		ptexTonemapTarget = _Hook_CreateTextureObject("$TonemapTarget", a_nWidth, a_nHeight, a_nDepth, a_eTT, a_nFlags, format, -1, a9);
		ptexPostAATarget = _Hook_CreateTextureObject("$PostAATarget", a_nWidth, a_nHeight, a_nDepth, a_eTT, a_nFlags, format, -1, a9);
		ptexUpscaleTarget = _Hook_CreateTextureObject("$UpscaleTarget", a_nWidth, a_nHeight, a_nDepth, a_eTT, a_nFlags, format, -1, a9);
#endif

		return pTex;
	}

#if INJECT_TAA_JITTERS
	void Hooks::Hook_UpdateBuffer(RE::CConstantBuffer* a_this, void* a_src, size_t a_size, uint32_t a_numDataBlocks)
	{
		if (*Offsets::cvar_r_AntialiasingMode != 5) {
			// push jitter offsets instead of unused fxaa params
			auto constants = reinterpret_cast<RE::PostAAConstants*>(a_src);
			constants->fxaaParams.x = Offsets::pCD3D9Renderer->m_vProjMatrixSubPixoffset.x;
			constants->fxaaParams.y = Offsets::pCD3D9Renderer->m_vProjMatrixSubPixoffset.y;
		}

		// run original
		_Hook_UpdateBuffer(a_this, a_src, a_size, a_numDataBlocks);
	}
#endif

	void Hooks::PatchSwapchainDesc(DXGI_SWAP_CHAIN_DESC& a_desc)
	{
		// set flags (done by the code that we wrote over)
		a_desc.Flags = 2;
		
		// set format
		a_desc.BufferDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

		// set swap effect
		a_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	}

	void Install()
	{
		Patches::Patch();
		Hooks::Hook();
	}
}
