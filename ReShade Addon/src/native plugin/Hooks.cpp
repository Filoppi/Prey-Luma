#include "Hooks.h"

#include <dxgi1_4.h>

#include "includes/SharedBegin.h"

#include "DKUtil/Impl/Hook/Shared.hpp"
#include "DKUtil/Impl/Hook/API.hpp"

namespace Hooks
{
	void Patches::Patch()
	{
		// Patch internal CryEngine RGBA8 to RGBA16F (or whatever format)
		{
			// SPostEffectsUtils::Create
			const auto address = Offsets::GetAddress(Offsets::SPostEffectsUtils_Create);

			dku::Hook::WriteImm(address + Offsets::Get(Offsets::SPostEffectsUtils_Create_PrevFrameScaled_1), format);   // $PrevFrameScaled (recreate)
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::SPostEffectsUtils_Create_PrevFrameScaled_2), format);   // $PrevFrameScaled (initial)

			dku::Hook::WriteImm(address + Offsets::Get(Offsets::SPostEffectsUtils_Create_BackBufferScaled_d2_1), format);   // $BackBufferScaled_d2 (recreate)
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::SPostEffectsUtils_Create_BackBufferScaled_d2_2), format);   // $BackBufferScaled_d2 (initial)

			dku::Hook::WriteImm(address + Offsets::Get(Offsets::SPostEffectsUtils_Create_BackBufferScaledTemp_d2_1), format);   // $BackBufferScaledTemp_d2 (recreate)
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::SPostEffectsUtils_Create_BackBufferScaledTemp_d2_2), format);   // $BackBufferScaledTemp_d2 (initial)

			dku::Hook::WriteImm(address + Offsets::Get(Offsets::SPostEffectsUtils_Create_BackBufferScaled_d4_1), format);   // $BackBufferScaled_d4 (recreate)
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::SPostEffectsUtils_Create_BackBufferScaled_d4_2), format);   // $BackBufferScaled_d4 (initial)

			dku::Hook::WriteImm(address + Offsets::Get(Offsets::SPostEffectsUtils_Create_BackBufferScaledTemp_d4_1), format);   // $BackBufferScaledTemp_d4 (recreate)
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::SPostEffectsUtils_Create_BackBufferScaledTemp_d4_2), format);   // $BackBufferScaledTemp_d4 (initial)

			dku::Hook::WriteImm(address + Offsets::Get(Offsets::SPostEffectsUtils_Create_BackBufferScaled_d8_1), format);   // $BackBufferScaled_d8 (recreate)
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::SPostEffectsUtils_Create_BackBufferScaled_d8_2), format);   // $BackBufferScaled_d8 (initial)
		}

		{
			// CTexture::GenerateSceneMap
			const auto address = Offsets::GetAddress(Offsets::CTexture_GenerateSceneMap);

			dku::Hook::WriteImm(address + Offsets::Get(Offsets::CTexture_GenerateSceneMap_BackBuffer_1), format);   // $BackBuffer
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::CTexture_GenerateSceneMap_BackBuffer_2), format);   // $BackBuffer
		}

		{
			// CColorGradingControllerD3D::InitResources
			const auto address = Offsets::GetAddress(Offsets::CColorGradingControllerD3D_InitResources);

			dku::Hook::WriteImm(address + Offsets::Get(Offsets::CColorGradingControllerD3D_InitResources_ColorGradingMergeLayer0), format16f);  // ColorGradingMergeLayer0
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::CColorGradingControllerD3D_InitResources_ColorGradingMergeLayer1), format16f);  // ColorGradingMergeLayer1
		}

		{
			// CTexture::GenerateHDRMaps
			const auto address = Offsets::GetAddress(Offsets::CTexture_GenerateHDRMaps);

			dku::Hook::WriteImm(address + Offsets::Get(Offsets::CTexture_GenerateHDRMaps_BitsPerPixel), format16f);  // used to calculate bits per pixel 
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::CTexture_GenerateHDRMaps_HDRTargetPrev), format16f);  // $HDRTargetPrev
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::CTexture_GenerateHDRMaps_HDRTempBloom0), format16f);  // $HDRTempBloom0
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::CTexture_GenerateHDRMaps_HDRTempBloom1), format16f);  // $HDRTempBloom1
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::CTexture_GenerateHDRMaps_HDRFinalBloom), format16f);  // $HDRFinalBloom
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::CTexture_GenerateHDRMaps_SceneTargetR11G11B10F_0), format16f);  // $SceneTargetR11G11B10F_0
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::CTexture_GenerateHDRMaps_SceneTargetR11G11B10F_1), format16f);  // $SceneTargetR11G11B10F_1
		}

#if !ADD_NEW_RENDER_TARGETS && 0 // Force upgrade all the texture we'd replace later too (this leads to issues, like some objects having purple reflections etc) (only compatible with the Steam base game)
		{
			// CDeferredShading::CreateDeferredMaps
			const auto address = Offsets::baseAddress + 0xF08200;

			dku::Hook::WriteImm(address + 0xD0, format16f);   // SceneNormalsMap
			dku::Hook::WriteImm(address + 0x1DC, format16f);  // SceneDiffuse
			dku::Hook::WriteImm(address + 0x229, format16f);  // SceneSpecular
		}

		{
			// CTexture::LoadDefaultSystemTextures
			const auto address = Offsets::baseAddress + 0x100DA30;

			dku::Hook::WriteImm(address + 0x1B61, format16f);  // SceneNormalsMap
			dku::Hook::WriteImm(address + 0x1BDC, format16f);  // SceneDiffuse
			dku::Hook::WriteImm(address + 0x1C0F, format16f);  // SceneSpecular
		}
#endif

#if 0 // Old code branches to change the jitters scale depending on the rendering resolution (we tried *2, /2, etc), none of this was seemengly needed (Steam base game only)
		if (Offsets::gameVersion == Offsets::GameVersion::PreySteam)
		{
			const auto jittersAddress = Offsets::baseAddress + 0xF41CA0; 

			uint8_t nop4[] = { 0x90, 0x90, 0x90, 0x90 };
			uint8_t nop8[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
			uint8_t divss[] = { 0x5E };
#if 1
			dku::Hook::WriteData(jittersAddress + 0x5D2, &nop4, sizeof(nop4));  // addss (x * 2)
			dku::Hook::WriteData(jittersAddress + 0x5DE, &nop4, sizeof(nop4));  // addss
#elif 1
			dku::Hook::WriteData(jittersAddress + 0x5E2, &nop8, sizeof(nop8));  // mulss (size * curDownscaleFactor, removes the curDownscaleFactor)
			dku::Hook::WriteData(jittersAddress + 0x601, &nop8, sizeof(nop8));  // mulss
#else
			dku::Hook::WriteData(jittersAddress + 0x5E4, &divss, sizeof(divss));  // mulss into divss
			dku::Hook::WriteData(jittersAddress + 0x603, &divss, sizeof(divss));  // mulss into divss
#endif
		}
#endif

#if 0 // Not needed until we enable DLSS as it can actually damage the native TAA
		SetHaltonSequencePhases(8); // Default to 8 for now as it's the best looking one for DLAA (suggested by NV)
#endif
	}

	void Patches::SetHaltonSequencePhases(unsigned int phases)
	{
		static unsigned int lastWrittePhases = 16; // Default game value
		if (phases != lastWrittePhases && Offsets::gameVersion == Offsets::GameVersion::PreySteam) // TODO: add support for GOG and DLCs (and remove mention to lack of support from game.cfg, around "r_AntialiasingTAAPattern")
		{
			lastWrittePhases = phases;

			const auto jittersAddress = Offsets::baseAddress + 0xF41CA0;
			constexpr int validValues[] = { 1, 2, 4, 8, 16, 32, 64, 128 }; // 1 works, it disables jitters

			// Note that this needs to be a power of two due to how our hook is implemented (it's a modulo operator, implemented as bitwise filter).
			// Lazy list search code.
			auto closestPhases = phases;
			int minDifference = INT32_MAX;
			for (const auto validValue : validValues) {
				const int difference = std::abs((int)validValue - (int)phases);
				if (difference < minDifference) {
					closestPhases = validValue;
					minDifference = difference;
				}
			}

			// Our hook takes a value scaled down by 1.
			closestPhases--;

			// Change Halton pattern generation (r_AntialiasingTAAPattern 10, which is Halton 16 phases) to using a phase of x, this works a lot better with DLSS
			dku::Hook::WriteImm(jittersAddress + 0x57A, closestPhases);
		}
	}
	void Patches::SetHaltonSequencePhases(unsigned int renderResY, unsigned int outputResY, unsigned int basePhases)
	{
		// NV DLSS suggested formula
		unsigned int phases = std::rintf(float(basePhases) * powf(float(outputResY) / float(renderResY), 2.f));
		SetHaltonSequencePhases(phases);
	}

	void Hooks::Hook()
	{
		// Hook CD3D9Renderer::FlashRenderInternal because DXGI_SWAP_EFFECT_FLIP_DISCARD unbinds backbuffer during Present. So we need to call OMSetRenderTargets to bind it again every frame.
#if 0 // Only needed by Kingdom Come Deliverance
		{
			uintptr_t vtable = Offsets::baseAddress + 0x1DD2E08;
			auto      Hook = dku::Hook::AddVMTHook(&vtable, 0x13B, FUNC_INFO(Hook_FlashRenderInternal));

			using FlashRenderInternal_t = std::add_pointer_t<void(RE::CD3D9Renderer*, void*, bool, bool)>;
			_Hook_FlashRenderInternal = Hook->GetOldFunction<FlashRenderInternal_t>();
			Hook->Enable();
		}
#endif

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

			auto offset = std::make_pair(Offsets::Get(Offsets::SwapchainDesc_Start), Offsets::Get(Offsets::SwapchainDesc_End));
			auto hook = dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::SwapchainDesc_Func), offset, &patch);
			hook->Enable();
		}

		// Hook swapchain creation and set colorspace
		{
			_Hook_OnD3D11PostCreateDevice = dku::Hook::write_call(Offsets::GetAddress(Offsets::OnD3D11PostCreateDevice), Hook_OnD3D11PostCreateDevice);
		}

#if ADD_NEW_RENDER_TARGETS
		// Replicate how the game treats render targets
		{
			// Hook SD3DPostEffectsUtils::CreateRenderTarget for $SceneDiffuse in CDeferredShading::CreateDeferredMaps
			_Hook_CreateRenderTarget_SceneDiffuse = dku::Hook::write_call(Offsets::GetAddress(Offsets::CreateRenderTarget_SceneDiffuse), Hook_CreateRenderTarget_SceneDiffuse);

			// Hook CTexture::CreateTextureObject for $SceneDiffuse
			_Hook_CreateTextureObject_SceneDiffuse = dku::Hook::write_call(Offsets::GetAddress(Offsets::CreateTextureObject_SceneDiffuse), Hook_CreateTextureObject_SceneDiffuse);
		}
#endif

		// Hook PrevBackBuffer0/1 creation to set UAV flags
		{
			_Hook_CreateRenderTarget_PrevBackBuffer0A = dku::Hook::write_call(Offsets::GetAddress(Offsets::CreateRenderTarget_PrevBackBuffer0A), Hook_CreateRenderTarget_PrevBackBuffer0A);
			_Hook_CreateRenderTarget_PrevBackBuffer1A = dku::Hook::write_call(Offsets::GetAddress(Offsets::CreateRenderTarget_PrevBackBuffer1A), Hook_CreateRenderTarget_PrevBackBuffer1A);
			_Hook_CreateRenderTarget_PrevBackBuffer0B = dku::Hook::write_call(Offsets::GetAddress(Offsets::CreateRenderTarget_PrevBackBuffer0B), Hook_CreateRenderTarget_PrevBackBuffer0B);
			_Hook_CreateRenderTarget_PrevBackBuffer1B = dku::Hook::write_call(Offsets::GetAddress(Offsets::CreateRenderTarget_PrevBackBuffer1B), Hook_CreateRenderTarget_PrevBackBuffer1B);
		}

#if ADD_NEW_RENDER_TARGETS
		// Replace with our new RTs
		{
			// use TonemapTarget as target for tonemapper (instead of SceneDiffuse?)
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						push(rax);

						mov(rax, ptr[a_addr]);
						mov(rsi, rax);

						pop(rax);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(&ptexTonemapTarget));
				patch.ready();

				auto offset = std::make_pair(Offsets::Get(Offsets::TonemapTarget1_Start), Offsets::Get(Offsets::TonemapTarget1_End));
				auto hook = dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::TonemapTarget1_Func), offset, &patch);
				hook->Enable();
			}

			// Read TonemapTarget instead of SceneDiffuse
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						push(rax);

						mov(rax, ptr[a_addr]);
						mov(rdi, rax);

						pop(rax);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(&ptexTonemapTarget));
				patch.ready();

				auto offset = std::make_pair(Offsets::Get(Offsets::TonemapTarget2_Start), Offsets::Get(Offsets::TonemapTarget2_End));
				auto hook = dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::TonemapTarget2_Func), offset, &patch);
				hook->Enable();
			}

			// Read TonemapTarget instead of SceneDiffuse #2?
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						push(rax);

						mov(rax, ptr[a_addr]);
						mov(r8, rax);

						pop(rax);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(&ptexTonemapTarget));
				patch.ready();

				auto offset = std::make_pair(Offsets::Get(Offsets::TonemapTarget3_Start), Offsets::Get(Offsets::TonemapTarget3_End));
				auto hook = dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::TonemapTarget3_Func), offset, &patch);
				hook->Enable();
			}

			// Read TonemapTarget instead of SceneDiffuse #3?
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						push(rax);

						mov(rax, ptr[a_addr]);
						mov(r8, rax);

						pop(rax);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(&ptexTonemapTarget));
				patch.ready();

				auto offset = std::make_pair(Offsets::Get(Offsets::TonemapTarget4_Start), Offsets::Get(Offsets::TonemapTarget4_End));
				auto hook = dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::TonemapTarget4_Func), offset, &patch);
				hook->Enable();
			}

			// Read PostAATarget instead of SceneNormalsMap
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						push(rax);

						mov(rax, ptr[a_addr]);
						mov(r15, rax);

						pop(rax);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(&ptexPostAATarget));
				patch.ready();

				auto offset = std::make_pair(Offsets::Get(Offsets::PostAATarget1_Start), Offsets::Get(Offsets::PostAATarget1_End));
				auto hook = dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::PostAATarget1_Func), offset, &patch);
				hook->Enable();
			}

			// Use PostAATarget instead of SceneNormalsMap #1
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						push(rax);

						mov(rax, ptr[a_addr]);
						mov(r12, rax);

						pop(rax);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(&ptexPostAATarget));
				patch.ready();

				auto offset = std::make_pair(Offsets::Get(Offsets::PostAATarget2_Start), Offsets::Get(Offsets::PostAATarget2_End));
				auto hook = dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::PostAATarget2_Func), offset, &patch);
				hook->Enable();
			}

			// Use PostAATarget instead of SceneNormalsMap #2 (CPostAAStage::DoFinalComposition)
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						push(rax);

						mov(rax, ptr[a_addr]);
						switch (Offsets::gameVersion)
						{
						case Offsets::GameVersion::PreySteam:
						case Offsets::GameVersion::MooncrashSteam:
							cmovnz(r15, rax);
							break;
						case Offsets::GameVersion::PreyGOG:
						case Offsets::GameVersion::MooncrashGOG:
							cmovnz(r14, rax);
							break;
						}

						pop(rax);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(&ptexPostAATarget));
				patch.ready();

				auto offset = std::make_pair(Offsets::Get(Offsets::PostAATarget3_Start), Offsets::Get(Offsets::PostAATarget3_End));
				auto hook = dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::PostAATarget3_Func), offset, &patch);
				hook->Enable();
			}

			// Use UpscaleTarget instead of SceneSpecular (CPostAAStage::DoFinalComposition)
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						push(rax);

						mov(rax, ptr[a_addr]);
						mov(rcx, rax);

						pop(rax);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(&ptexUpscaleTarget));
				patch.ready();
				auto offset = std::make_pair(Offsets::Get(Offsets::UpscaleTarget1_Start), Offsets::Get(Offsets::UpscaleTarget1_End));
				auto hook = dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::UpscaleTarget1_Func), offset, &patch);
				hook->Enable();
			}

			// Use UpscaleTarget instead of SceneSpecular #2 (CD3D9Renderer::RT_SwitchToNativeResolutionBackbuffer)
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						mov(rax, ptr[a_addr]);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(&ptexUpscaleTarget));
				patch.ready();

				auto offset = std::make_pair(Offsets::Get(Offsets::UpscaleTarget2_Start), Offsets::Get(Offsets::UpscaleTarget2_End));
				auto hook = dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::UpscaleTarget2_Func), offset, &patch);
				hook->Enable();
			}

			// Use UpscaleTarget instead of SceneSpecular #3 (CD3D9Renderer::RT_SwitchToNativeResolutionBackbuffer)
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch(uintptr_t a_addr)
					{
						push(rax);

						mov(rax, ptr[a_addr]);
						mov(rdx, rax);

						pop(rax);
					}
				};

				Patch patch(reinterpret_cast<uintptr_t>(&ptexUpscaleTarget));
				patch.ready();

				auto offset = std::make_pair(Offsets::Get(Offsets::UpscaleTarget3_Start), Offsets::Get(Offsets::UpscaleTarget3_End));
				auto hook = dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::UpscaleTarget3_Func), offset, &patch);
				hook->Enable();
			}
		}
#endif

#if INJECT_TAA_JITTERS
		// TAA jitter
		if (Offsets::gameVersion == Offsets::GameVersion::PreySteam) {
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

	// Hook SD3DPostEffectsUtils::CreateRenderTarget for $SceneDiffuse in CDeferredShading::CreateDeferredMaps
	bool Hooks::Hook_CreateRenderTarget_SceneDiffuse(const char* a_szTexName, RE::CTexture*& a_pTex, int a_iWidth, int a_iHeight, void* a_cClear, bool a_bUseAlpha, bool a_bMipMaps, RE::ETEX_Format a_eTF, int a_nCustomID, RE::ETextureFlags a_nFlags)
	{
		bool bReturn = _Hook_CreateRenderTarget_SceneDiffuse(a_szTexName, a_pTex, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, a_eTF, a_nCustomID, a_nFlags);

#if ADD_NEW_RENDER_TARGETS
		// These are expected to have the following flags: FT_DONT_RELEASE, FT_DONT_STREAM, FT_USAGE_RENDERTARGET.
		// These other flags might or might not be present, it would probably not make a difference: FT_USAGE_ALLOWREADSRGB, FT_STATE_CLAMP, FT_USAGE_MSAA.
		// "bUseAlpha" is seemengly ignored. "bMipMaps" is expected to be false.
#if SUPPORT_MSAA
		a_nFlags |= RE::ETextureFlags::FT_USAGE_MSAA;
#endif
		auto nPostAAFlags = a_nFlags;
#if FORCE_DLSS_SMAA_UAV && 0
		nPostAAFlags |= RE::ETextureFlags::FT_USAGE_UNORDERED_ACCESS | RE::ETextureFlags::FT_USAGE_UAV_RWTEXTURE;
#endif
		_Hook_CreateRenderTarget_SceneDiffuse("$TonemapTarget", ptexTonemapTarget, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, format, -1, a_nFlags);
		_Hook_CreateRenderTarget_SceneDiffuse("$PostAATarget", ptexPostAATarget, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, format, -1, nPostAAFlags);
		_Hook_CreateRenderTarget_SceneDiffuse("$UpscaleTarget", ptexUpscaleTarget, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, format, -1, a_nFlags);
#endif

		return bReturn;
	}

	// Hook CTexture::CreateTextureObject for $SceneDiffuse
	RE::CTexture* Hooks::Hook_CreateTextureObject_SceneDiffuse(const char* a_name, uint32_t a_nWidth, uint32_t a_nHeight, int a_nDepth, RE::ETEX_Type a_eTT, RE::ETextureFlags a_nFlags, RE::ETEX_Format a_eTF, int a_nCustomID, uint8_t a9)
	{
		RE::CTexture* pTex = _Hook_CreateTextureObject_SceneDiffuse(a_name, a_nWidth, a_nHeight, a_nDepth, a_eTT, a_nFlags, a_eTF, a_nCustomID, a9);

#if ADD_NEW_RENDER_TARGETS
#if SUPPORT_MSAA
		a_nFlags |= RE::ETextureFlags::FT_USAGE_MSAA;
#endif
		auto nPostAAFlags = a_nFlags;
#if FORCE_DLSS_SMAA_UAV && 0
		nPostAAFlags |= RE::ETextureFlags::FT_USAGE_UNORDERED_ACCESS | RE::ETextureFlags::FT_USAGE_UAV_RWTEXTURE;
#endif
		ptexTonemapTarget = _Hook_CreateTextureObject_SceneDiffuse("$TonemapTarget", a_nWidth, a_nHeight, a_nDepth, a_eTT, a_nFlags, format, -1, a9);
		ptexPostAATarget = _Hook_CreateTextureObject_SceneDiffuse("$PostAATarget", a_nWidth, a_nHeight, a_nDepth, a_eTT, nPostAAFlags, format, -1, a9);
		ptexUpscaleTarget = _Hook_CreateTextureObject_SceneDiffuse("$UpscaleTarget", a_nWidth, a_nHeight, a_nDepth, a_eTT, a_nFlags, format, -1, a9);
#endif

		return pTex;
	}

	static RE::CTexture* ptexPrevBackBuffer = nullptr;

	// Hook CTexture::CreateRenderTarget for $PrevBackBuffer0 in SPostEffectsUtils::Create - initial
	// For some reason, the previous back buffers copies (only used by TAA?) are always FP16 textures even in the vanilla game, despite that seemengly not being needed (as the source textures were UNORM8).
	// These textures were used as flip flop history for TAA, one being used as output for this frame's TAA, and the other(s) as history from the previous frame(s).
	// With DLSS we could probably skip all these copies and re-use some other post processing textures (as the history is kept within DLSS), but there's no need to bother.
	RE::CTexture* Hooks::Hook_CreateRenderTarget_PrevBackBuffer0A(const char* a_name, int a_nWidth, int a_nHeight,
		void* a_cClear, RE::ETEX_Type a_eTT, RE::ETextureFlags a_nFlags, RE::ETEX_Format a_eTF, int a_nCustomID)
	{
		// Avoid creating more than one history texture as an extra DLSS optimization.
		// It's all single threaded so this is fine.
		if (ptexPrevBackBuffer) {
			auto ptexPrevBackBufferCopy = ptexPrevBackBuffer;
			ptexPrevBackBuffer = nullptr;
			return ptexPrevBackBufferCopy;
		}

#if FORCE_DLSS_SMAA_UAV
		a_nFlags |= RE::ETextureFlags::FT_USAGE_UNORDERED_ACCESS | RE::ETextureFlags::FT_USAGE_UAV_RWTEXTURE;
#endif
		auto _ptexPrevBackBuffer = _Hook_CreateRenderTarget_PrevBackBuffer0A(a_name, a_nWidth, a_nHeight, a_cClear, a_eTT, a_nFlags, a_eTF, a_nCustomID);
#if FORCE_DLSS_SMAA_SLIMMED_DOWN_HISTORY
		ptexPrevBackBuffer = _ptexPrevBackBuffer;
#endif
		return _ptexPrevBackBuffer;
	}

	// Hook CTexture::CreateRenderTarget for $PrevBackBuffer1 in SPostEffectsUtils::Create - initial
	RE::CTexture* Hooks::Hook_CreateRenderTarget_PrevBackBuffer1A(const char* a_name, int a_nWidth, int a_nHeight,
		void* a_cClear, RE::ETEX_Type a_eTT, RE::ETextureFlags a_nFlags, RE::ETEX_Format a_eTF, int a_nCustomID)
	{
		if (ptexPrevBackBuffer) {
			auto ptexPrevBackBufferCopy = ptexPrevBackBuffer;
			ptexPrevBackBuffer = nullptr;
			return ptexPrevBackBufferCopy;
		}

#if FORCE_DLSS_SMAA_UAV
		a_nFlags |= RE::ETextureFlags::FT_USAGE_UNORDERED_ACCESS | RE::ETextureFlags::FT_USAGE_UAV_RWTEXTURE;
#endif
		auto _ptexPrevBackBuffer = _Hook_CreateRenderTarget_PrevBackBuffer1A(a_name, a_nWidth, a_nHeight, a_cClear, a_eTT, a_nFlags, a_eTF, a_nCustomID);
#if FORCE_DLSS_SMAA_SLIMMED_DOWN_HISTORY
		ptexPrevBackBuffer = _ptexPrevBackBuffer;
#endif
		return _ptexPrevBackBuffer;
	}

	// Hook CTexture::CreateRenderTarget for $PrevBackBuffer0 in SPostEffectsUtils::Create - recreation
	bool Hooks::Hook_CreateRenderTarget_PrevBackBuffer0B(RE::CTexture* a_this, RE::ETEX_Format a_eTF, void* a_cClear)
	{
#if FORCE_DLSS_SMAA_UAV
		a_this->m_nFlags |= RE::ETextureFlags::FT_USAGE_UNORDERED_ACCESS | RE::ETextureFlags::FT_USAGE_UAV_RWTEXTURE;
#endif
		return _Hook_CreateRenderTarget_PrevBackBuffer0B(a_this, a_eTF, a_cClear);
	}

	// Hook CTexture::CreateRenderTarget for $PrevBackBuffer1 in SPostEffectsUtils::Create - recreation
	bool Hooks::Hook_CreateRenderTarget_PrevBackBuffer1B(RE::CTexture* a_this, RE::ETEX_Format a_eTF, void* a_cClear)
	{
#if FORCE_DLSS_SMAA_UAV
		a_this->m_nFlags |= RE::ETextureFlags::FT_USAGE_UNORDERED_ACCESS | RE::ETextureFlags::FT_USAGE_UAV_RWTEXTURE;
#endif
		return _Hook_CreateRenderTarget_PrevBackBuffer1B(a_this, a_eTF, a_cClear);
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