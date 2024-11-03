#include "Hooks.h"

#include <dxgi1_4.h>

#include "includes/SharedBegin.h"

#include "DKUtil/Impl/Hook/Shared.hpp"
#include "DKUtil/Impl/Hook/API.hpp"

namespace Hooks
{
	void Patches::Patch()
	{
		// Patch internal CryEngine RGBA8 to RGBA16F
		{
			// SPostEffectsUtils::Create
			const auto address = Offsets::baseAddress + 0x1071510;

			dku::Hook::WriteImm(address + 0x435, format);   // $PrevFrameScaled (recreate)
			dku::Hook::WriteImm(address + 0x47D, format);   // $PrevFrameScaled (initial)

			dku::Hook::WriteImm(address + 0x371, format);   // $BackBufferScaled_d2 (recreate)
			dku::Hook::WriteImm(address + 0x3B9, format);   // $BackBufferScaled_d2 (initial)

			dku::Hook::WriteImm(address + 0x4F9, format);   // $BackBufferScaledTemp_d2 (recreate)
			dku::Hook::WriteImm(address + 0x541, format);   // $BackBufferScaledTemp_d2 (initial)

			dku::Hook::WriteImm(address + 0x743, format);   // $BackBufferScaled_d4 (recreate)
			dku::Hook::WriteImm(address + 0x78D, format);   // $BackBufferScaled_d4 (initial)

			dku::Hook::WriteImm(address + 0x80A, format);   // $BackBufferScaledTemp_d4 (recreate)
			dku::Hook::WriteImm(address + 0x852, format);   // $BackBufferScaledTemp_d4 (initial)

			dku::Hook::WriteImm(address + 0x8FE, format);   // $BackBufferScaled_d8 (recreate)
			dku::Hook::WriteImm(address + 0x92D, format);   // $BackBufferScaled_d8 (initial)
		}

		{
			// CTexture::GenerateSceneMap
			const auto address = Offsets::baseAddress + 0xF57090;

			dku::Hook::WriteImm(address + 0xEE, format);    // $BackBuffer
			dku::Hook::WriteImm(address + 0x151, format);   // $BackBuffer
		}

		{
			// CColorGradingControllerD3D::InitResources
			const auto address = Offsets::baseAddress + 0xF036D0;

			dku::Hook::WriteImm(address + 0xA3, format16f);  // ColorGradingMergeLayer0
			dku::Hook::WriteImm(address + 0xFA, format16f);  // ColorGradingMergeLayer1
		}

		{
			// CTexture::GenerateHDRMaps
			const auto address = Offsets::baseAddress + 0xF15280;

			dku::Hook::WriteImm(address + 0x11A, format16f);  // used to calculate bits per pixel 
			dku::Hook::WriteImm(address + 0x14B, format16f);  // $HDRTargetPrev
			dku::Hook::WriteImm(address + 0x52A, format16f);  // $HDRTempBloom0
			dku::Hook::WriteImm(address + 0x5B0, format16f);  // $HDRTempBloom1
			dku::Hook::WriteImm(address + 0x630, format16f);  // $HDRFinalBloom
			dku::Hook::WriteImm(address + 0xAB7, format16f);  // $SceneTargetR11G11B10F_0
			dku::Hook::WriteImm(address + 0xB52, format16f);  // $SceneTargetR11G11B10F_1
		}
	}

	void Hooks::Hook()
	{
		//// Hook CD3D9Renderer::FlashRenderInternal because DXGI_SWAP_EFFECT_FLIP_DISCARD unbinds backbuffer during Present. So we need to call OMSetRenderTargets to bind it again every frame.
		//{
		//	uintptr_t vtable = Offsets::baseAddress + 0x1DD2E08;
		//	auto      Hook = dku::Hook::AddVMTHook(&vtable, 0x13B, FUNC_INFO(Hook_FlashRenderInternal));

		//	using FlashRenderInternal_t = std::add_pointer_t<void(RE::CD3D9Renderer*, void*, bool, bool)>;
		//	_Hook_FlashRenderInternal = Hook->GetOldFunction<FlashRenderInternal_t>();
		//	Hook->Enable();
		//}

		// Patch swapchain desc to change DXGI_FORMAT from RGBA8 and DXGI_SWAP_EFFECT to DXGI_SWAP_EFFECT_FLIP_DISCARD
		{
			struct Patch : Xbyak::CodeGenerator
			{
				Patch(uintptr_t a_addr)
				{
					// call our function
					mov(rax, a_addr);
					mov(rcx, rdi);
					call(rax);
				}
			};

			Patch patch(reinterpret_cast<uintptr_t>(PatchSwapchainDesc));
			patch.ready();

			auto offset = std::make_pair(0x50E, 0x515);
			auto hook = dku::Hook::AddASMPatch(Offsets::baseAddress + 0xF50000, offset, &patch);
			hook->Enable();
		}

		// Hook swapchain creation and set colorspace
		{
			_Hook_OnD3D11PostCreateDevice = dku::Hook::write_call(Offsets::baseAddress + 0xF53F66, Hook_OnD3D11PostCreateDevice);
		}

#if ADD_NEW_RENDER_TARGETS
		// Replicate how the game treats render targets
		{
			// Hook SD3DPostEffectsUtils::CreateRenderTarget for $SceneDiffuse in CDeferredShading::CreateDeferredMaps
			_Hook_CreateRenderTarget = dku::Hook::write_call(Offsets::baseAddress + 0xF083F0, Hook_CreateRenderTarget);

			// Hook CTexture::CreateTextureObject for $SceneDiffuse
			_Hook_CreateTextureObject = dku::Hook::write_call(Offsets::baseAddress + 0x100F61F, Hook_CreateTextureObject);
		}
#endif

#if ADD_NEW_RENDER_TARGETS
		// Replace with our new RTs
		{
			// use TonemapTarget as target for tonemapper
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						push(rax);

						// call our function
						mov(rax, a_addr);
						call(rax);
						mov(rsi, rax);

						pop(rax);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(GetTonemapTargetRT));
				patch.ready();

				auto offset = std::make_pair(0x78D, 0x794);
				auto hook = dku::Hook::AddASMPatch(Offsets::baseAddress + 0xFB00C0, offset, &patch);
				hook->Enable();
			}

			// Read TonemapTarget instead of SceneDiffuse
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						// call our function
						mov(rax, a_addr);
						call(rax);
						mov(rdi, rax);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(GetTonemapTargetRT));
				patch.ready();

				auto offset = std::make_pair(0x10, 0x17);
				auto hook = dku::Hook::AddASMPatch(Offsets::baseAddress + 0xF9C790, offset, &patch);
				hook->Enable();
			}

			// Read TonemapTarget instead of SceneDiffuse #2?
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						// call our function
						mov(rax, a_addr);
						call(rax);
						mov(r8, rax);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(GetTonemapTargetRT));
				patch.ready();

				auto offset = std::make_pair(0x105, 0x10C);
				auto hook = dku::Hook::AddASMPatch(Offsets::baseAddress + 0xF3B860, offset, &patch);
				hook->Enable();
			}

			// Read TonemapTarget instead of SceneDiffuse #3?
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						// call our function
						mov(rax, a_addr);
						call(rax);
						mov(r8, rax);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(GetTonemapTargetRT));
				patch.ready();

				auto offset = std::make_pair(0x41A, 0x421);
				auto hook = dku::Hook::AddASMPatch(Offsets::baseAddress + 0xFBD100, offset, &patch);
				hook->Enable();
			}

			// Read PostAATarget instead of SceneNormalsMap
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						push(rcx);

						// call our function
						mov(rax, a_addr);
						call(rax);
						mov(r15, rax);

						pop(rcx);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(GetPostAATargetRT));
				patch.ready();

				auto offset = std::make_pair(0x28, 0x2F);
				auto hook = dku::Hook::AddASMPatch(Offsets::baseAddress + 0xF9C790, offset, &patch);
				hook->Enable();
			}

			// Use PostAATarget instead of SceneNormalsMap #1
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						// call our function
						mov(rax, a_addr);
						call(rax);
						mov(r12, rax);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(GetPostAATargetRT));
				patch.ready();

				auto offset = std::make_pair(0x1A, 0x21);
				auto hook = dku::Hook::AddASMPatch(Offsets::baseAddress + 0xF99500, offset, &patch);
				hook->Enable();
			}

			// Use UpscaleTarget instead of SceneSpecular (CPostAAStage::DoFinalComposition)
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						push(rax);

						// call our function
						mov(rax, a_addr);
						call(rax);
						mov(rcx, rax);

						pop(rax);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(GetUpscaleTargetRT));
				patch.ready();

				auto offset = std::make_pair(0x3B, 0x42);
				auto hook = dku::Hook::AddASMPatch(Offsets::baseAddress + 0xF9BBB0, offset, &patch);
				hook->Enable();
			}

			// Use PostAATarget instead of SceneNormalsMap #2 (CPostAAStage::DoFinalComposition)
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						push(rax);

						// call our function
						mov(rax, a_addr);
						call(rax);
						cmovnz(rcx, rax);

						pop(rax);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(GetPostAATargetRT));
				patch.ready();

				auto offset = std::make_pair(0x9B, 0xA3);
				auto hook = dku::Hook::AddASMPatch(Offsets::baseAddress + 0xF9BBB0, offset, &patch);
				hook->Enable();
			}

			// Use UpscaleTarget instead of SceneSpecular #2 (CD3D9Renderer::RT_SwitchToNativeResolutionBackbuffer)
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						// call our function
						mov(rax, a_addr);
						call(rax);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(GetUpscaleTargetRT));
				patch.ready();

				auto offset = std::make_pair(0x47, 0x4E);
				auto hook = dku::Hook::AddASMPatch(Offsets::baseAddress + 0xF7EB20, offset, &patch);
				hook->Enable();
			}

			// Use UpscaleTarget instead of SceneSpecular #3 (CD3D9Renderer::RT_SwitchToNativeResolutionBackbuffer)
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						push(rax);
						push(rcx);

						// call our function
						mov(rax, a_addr);
						call(rax);
						mov(rdx, rax);

						pop(rcx);
						pop(rax);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(GetUpscaleTargetRT));
				patch.ready();

				auto offset = std::make_pair(0x10E, 0x115);
				auto hook = dku::Hook::AddASMPatch(Offsets::baseAddress + 0xF7EB20, offset, &patch);
				hook->Enable();
			}
		}
#endif

#if INJECT_TAA_JITTERS
		// TAA jitter
		{
			// Hook CConstantBuffer::UpdateBuffer to push jitter offset instead of the unused fxaa params
			const auto address = Offsets::baseAddress + 0xF99FE0;
			_Hook_UpdateBuffer = dku::Hook::write_call(address + 0x1AAA, Hook_UpdateBuffer);
		}
#endif
	}

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

	void Hooks::Hook_OnD3D11PostCreateDevice()
	{
		// set colorspace
		IDXGISwapChain3* swapChain3 = nullptr;
		Offsets::pCD3D9Renderer->m_devInfo.m_pSwapChain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&swapChain3));

		DXGI_COLOR_SPACE_TYPE colorSpace;
		if (format == RE::ETEX_Format::eTF_R10G10B10A2) { // This format could be SDR too, but let's assume HDR10
			colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
		} else {
			colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
		}

		swapChain3->SetColorSpace1(colorSpace);
		swapChain3->Release();

		_Hook_OnD3D11PostCreateDevice();
	}

	// Despite the name, due to our specific hook, this is called just once on startup, and then again when changing resolution
	bool Hooks::Hook_CreateRenderTarget(const char* a_szTexName, RE::CTexture*& a_pTex, int a_iWidth, int a_iHeight, void* a_cClear, bool a_bUseAlpha, bool a_bMipMaps, RE::ETEX_Format a_eTF, int a_nCustomID, /*RE::ETextureFlags*/ int a_nFlags)
	{
		bool bReturn = _Hook_CreateRenderTarget(a_szTexName, a_pTex, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, a_eTF, a_nCustomID, a_nFlags);

#if ADD_NEW_RENDER_TARGETS
		// These are expected to have the following flags: FT_DONT_RELEASE, FT_DONT_STREAM, FT_USAGE_RENDERTARGET.
		// These other flags might or might not be present, it would probably not make a difference: FT_USAGE_ALLOWREADSRGB, FT_STATE_CLAMP, FT_USAGE_MSAA.
		// "bUseAlpha" is seemengly ignored. "bMipMaps" is expected to be false.
#if SUPPORT_MSAA
		a_nFlags |= RE::ETextureFlags::FT_USAGE_MSAA;
#endif
		_Hook_CreateRenderTarget("$TonemapTarget", ptexTonemapTarget, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, format, -1, a_nFlags);
		_Hook_CreateRenderTarget("$PostAATarget", ptexPostAATarget, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, format, -1, a_nFlags);
		_Hook_CreateRenderTarget("$UpscaleTarget", ptexUpscaleTarget, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, format, -1, a_nFlags);
#endif

		return bReturn;
	}

	// Despite the name, due to our specific hook, this is called just once on startup, and then again when changing resolution
	RE::CTexture* Hooks::Hook_CreateTextureObject(const char* a_name, uint32_t a_nWidth, uint32_t a_nHeight, int a_nDepth, RE::ETEX_Type a_eTT, /*RE::ETextureFlags*/ uint32_t a_nFlags, RE::ETEX_Format a_eTF, int a_nCustomID, uint8_t a9)
	{
		RE::CTexture* pTex = _Hook_CreateTextureObject(a_name, a_nWidth, a_nHeight, a_nDepth, a_eTT, a_nFlags, a_eTF, a_nCustomID, a9);

#if ADD_NEW_RENDER_TARGETS
#if SUPPORT_MSAA
		a_nFlags |= RE::ETextureFlags::FT_USAGE_MSAA;
#endif
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

#include "includes/SharedEnd.h"