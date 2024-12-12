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

#if UPGRADE_INTERMEDIARY_TEXTURES //TODOFT: do we even need to upgrade these from R11G11B10F?
		{
			// CTexture::GenerateHDRMaps
			const auto address = Offsets::GetAddress(Offsets::CTexture_GenerateHDRMaps);

			dku::Hook::WriteImm(address + Offsets::Get(Offsets::CTexture_GenerateHDRMaps_BitsPerPixel), format16f);  // used to calculate bits per pixel
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::CTexture_GenerateHDRMaps_HDRTargetPrev), format16f);  // $HDRTargetPrev: used for screen space reflections (SSR), Water Volumes (? possibly not in Prey), SVO (? probably not in Prey), Motion Blur (if DoF is disabled?)
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::CTexture_GenerateHDRMaps_HDRTempBloom0), format16f);  // $HDRTempBloom0: Bloom intermediary texture
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::CTexture_GenerateHDRMaps_HDRTempBloom1), format16f);  // $HDRTempBloom1: Bloom intermediary texture
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::CTexture_GenerateHDRMaps_HDRFinalBloom), format16f);  // $HDRFinalBloom: Bloom final target
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::CTexture_GenerateHDRMaps_SceneTargetR11G11B10F_0), format16f);  // $SceneTargetR11G11B10F_0: used by Lens Optics, Motion Blur (?), and DoF (?)
			dku::Hook::WriteImm(address + Offsets::Get(Offsets::CTexture_GenerateHDRMaps_SceneTargetR11G11B10F_1), format16f);  // $SceneTargetR11G11B10F_1: used by Screen Space SubSurfaceScattering (SSSSS), Water Volume Caustics (?), ...
		}
#endif

		// Patch out the branch that clamps the "cl_hfov" cvar (horizontal FOV) to 120.f
		// Note: since exposing "cl_fov" (vertical FOV) to the game's settings, this might not be necessary anymore as we never pass through the horizontal FOV cvar, but in case the game ever changed it on the spot, then this will remove the clamps again.
		{
			// OnHFOVChanged
			const auto address = Offsets::GetAddress(Offsets::OnHFOVChanged);

			uint8_t nop8[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

			dku::Hook::WriteData(address + Offsets::Get(Offsets::OnHFOVChanged_Offset), &nop8, sizeof(nop8));  // minss -> nop
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
		static unsigned int lastWrittenPhases = 16; // Default game value
		if (phases != lastWrittenPhases)
		{
			lastWrittenPhases = phases;

			const auto jittersAddress = Offsets::GetAddress(Offsets::CD3D9Renderer_RT_RenderScene) + Offsets::Get(Offsets::CD3D9Renderer_RT_RenderScene_Jitters);
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
			dku::Hook::WriteImm(jittersAddress, closestPhases);
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
			asmPatchHandle_swapchain = std::move(dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::SwapchainDesc_Func), offset, &patch));
			asmPatchHandle_swapchain->Enable();
		}

		// Hook swapchain creation and set colorspace
		{
			hookHandle_OnD3D11PostCreateDevice = std::move(dku::Hook::AddRelHook<5, true>(Offsets::GetAddress(Offsets::OnD3D11PostCreateDevice), DKUtil::Hook::unrestricted_cast<std::uintptr_t>(Hook_OnD3D11PostCreateDevice)));
			hookHandle_OnD3D11PostCreateDevice->Enable();
		}

#if ADD_NEW_RENDER_TARGETS
		// Replicate how the game treats render targets
		{
			// Hook SD3DPostEffectsUtils::CreateRenderTarget for $SceneDiffuse in CDeferredShading::CreateDeferredMaps
			hookHandle_CreateRenderTarget_SceneDiffuse = std::move(dku::Hook::AddRelHook<5, true>(Offsets::GetAddress(Offsets::CreateRenderTarget_SceneDiffuse), DKUtil::Hook::unrestricted_cast<std::uintptr_t>(Hook_CreateRenderTarget_SceneDiffuse)));
			hookHandle_CreateRenderTarget_SceneDiffuse->Enable();

			// Hook CTexture::CreateTextureObject for $SceneDiffuse
			hookHandle_CreateTextureObject_SceneDiffuse = std::move(dku::Hook::AddRelHook<5, true>(Offsets::GetAddress(Offsets::CreateTextureObject_SceneDiffuse), DKUtil::Hook::unrestricted_cast<std::uintptr_t>(Hook_CreateTextureObject_SceneDiffuse)));
			hookHandle_CreateTextureObject_SceneDiffuse->Enable();
		}
#endif

		// Hook PrevBackBuffer0/1 creation to set UAV flags
		{
			hookHandle_CreateRenderTarget_PrevBackBuffer0A = std::move(dku::Hook::AddRelHook<5, true>(Offsets::GetAddress(Offsets::CreateRenderTarget_PrevBackBuffer0A), DKUtil::Hook::unrestricted_cast<std::uintptr_t>(Hook_CreateRenderTarget_PrevBackBuffer0A)));
			hookHandle_CreateRenderTarget_PrevBackBuffer0A->Enable();

			hookHandle_CreateRenderTarget_PrevBackBuffer1A = std::move(dku::Hook::AddRelHook<5, true>(Offsets::GetAddress(Offsets::CreateRenderTarget_PrevBackBuffer1A), DKUtil::Hook::unrestricted_cast<std::uintptr_t>(Hook_CreateRenderTarget_PrevBackBuffer1A)));
			hookHandle_CreateRenderTarget_PrevBackBuffer1A->Enable();

			hookHandle_CreateRenderTarget_PrevBackBuffer0B = std::move(dku::Hook::AddRelHook<5, true>(Offsets::GetAddress(Offsets::CreateRenderTarget_PrevBackBuffer0B), DKUtil::Hook::unrestricted_cast<std::uintptr_t>(Hook_CreateRenderTarget_PrevBackBuffer0B)));
			hookHandle_CreateRenderTarget_PrevBackBuffer0B->Enable();

			hookHandle_CreateRenderTarget_PrevBackBuffer1B = std::move(dku::Hook::AddRelHook<5, true>(Offsets::GetAddress(Offsets::CreateRenderTarget_PrevBackBuffer1B), DKUtil::Hook::unrestricted_cast<std::uintptr_t>(Hook_CreateRenderTarget_PrevBackBuffer1B)));
			hookHandle_CreateRenderTarget_PrevBackBuffer1B->Enable();
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
				asmPatchHandle_tonemapTarget1 = std::move(dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::TonemapTarget1_Func), offset, &patch));
				asmPatchHandle_tonemapTarget1->Enable();
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
				asmPatchHandle_tonemapTarget2 = std::move(dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::TonemapTarget2_Func), offset, &patch));
				asmPatchHandle_tonemapTarget2->Enable();
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
				asmPatchHandle_tonemapTarget3 = std::move(dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::TonemapTarget3_Func), offset, &patch));
				asmPatchHandle_tonemapTarget3->Enable();
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
				asmPatchHandle_tonemapTarget4 = std::move(dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::TonemapTarget4_Func), offset, &patch));
				asmPatchHandle_tonemapTarget4->Enable();
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
				asmPatchHandle_postAATarget1 = std::move(dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::PostAATarget1_Func), offset, &patch));
				asmPatchHandle_postAATarget1->Enable();
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
				asmPatchHandle_postAATarget2 = std::move(dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::PostAATarget2_Func), offset, &patch));
				asmPatchHandle_postAATarget2->Enable();
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
				asmPatchHandle_postAATarget3 = std::move(dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::PostAATarget3_Func), offset, &patch));
				asmPatchHandle_postAATarget3->Enable();
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
				asmPatchHandle_upscaleTarget1 = std::move(dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::UpscaleTarget1_Func), offset, &patch));
				asmPatchHandle_upscaleTarget1->Enable();
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
				asmPatchHandle_upscaleTarget2 = std::move(dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::UpscaleTarget2_Func), offset, &patch));
				asmPatchHandle_upscaleTarget2->Enable();
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
				asmPatchHandle_upscaleTarget3 = std::move(dku::Hook::AddASMPatch(Offsets::GetAddress(Offsets::UpscaleTarget3_Func), offset, &patch));
				asmPatchHandle_upscaleTarget3->Enable();
			}
		}
#endif
	}

	void Hooks::Unhook()
	{
		{
			asmPatchHandle_swapchain->Disable();
		}

		{
			hookHandle_OnD3D11PostCreateDevice->Disable();
		}

#if ADD_NEW_RENDER_TARGETS
		{
			hookHandle_CreateRenderTarget_SceneDiffuse->Disable();
			hookHandle_CreateTextureObject_SceneDiffuse->Disable();
		}
#endif

		{
			hookHandle_CreateRenderTarget_PrevBackBuffer0A->Disable();
			hookHandle_CreateRenderTarget_PrevBackBuffer1A->Disable();
			hookHandle_CreateRenderTarget_PrevBackBuffer0B->Disable();
			hookHandle_CreateRenderTarget_PrevBackBuffer1B->Disable();
		}

#if ADD_NEW_RENDER_TARGETS
		{
			asmPatchHandle_tonemapTarget1->Disable();
			asmPatchHandle_tonemapTarget2->Disable();
			asmPatchHandle_tonemapTarget3->Disable();
			asmPatchHandle_tonemapTarget4->Disable();
			asmPatchHandle_postAATarget1->Disable();
			asmPatchHandle_postAATarget2->Disable();
			asmPatchHandle_postAATarget3->Disable();
			asmPatchHandle_upscaleTarget1->Disable();
			asmPatchHandle_upscaleTarget2->Disable();
			asmPatchHandle_upscaleTarget3->Disable();
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

	void Hooks::Hook_OnD3D11PostCreateDevice()
	{
		// set colorspace
		IDXGISwapChain3* swapChain3 = nullptr;
		assert(Offsets::pCD3D9Renderer->m_devInfo.m_pSwapChain != nullptr);
		Offsets::pCD3D9Renderer->m_devInfo.m_pSwapChain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&swapChain3));

		if (swapChain3) {
			DXGI_COLOR_SPACE_TYPE colorSpace;
			if (format == RE::ETEX_Format::eTF_R10G10B10A2) { // This format could be SDR too, but let's assume HDR10
				colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
			} else {
				colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
			}

			swapChain3->SetColorSpace1(colorSpace);
			swapChain3->Release();
		}

		using OriginalFunction = void(*)();
		OriginalFunction originalFunc = *hookHandle_OnD3D11PostCreateDevice;
		originalFunc();
	}

	// Hook SD3DPostEffectsUtils::CreateRenderTarget for $SceneDiffuse in CDeferredShading::CreateDeferredMaps
	bool Hooks::Hook_CreateRenderTarget_SceneDiffuse(const char* a_szTexName, RE::CTexture*& a_pTex, int a_iWidth, int a_iHeight, void* a_cClear, bool a_bUseAlpha, bool a_bMipMaps, RE::ETEX_Format a_eTF, int a_nCustomID, RE::ETextureFlags a_nFlags)
	{
		using OriginalFunction = bool(*)(const char*, RE::CTexture*&, int, int, void*, bool, bool, RE::ETEX_Format, int, RE::ETextureFlags);
		OriginalFunction originalFunc = *hookHandle_CreateRenderTarget_SceneDiffuse;

		bool bReturn = originalFunc(a_szTexName, a_pTex, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, a_eTF, a_nCustomID, a_nFlags);

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
		originalFunc("$TonemapTarget", ptexTonemapTarget, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, format, -1, a_nFlags);
		originalFunc("$PostAATarget", ptexPostAATarget, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, format, -1, nPostAAFlags);
		originalFunc("$UpscaleTarget", ptexUpscaleTarget, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, format, -1, a_nFlags);
#endif

		return bReturn;
	}

	// Hook CTexture::CreateTextureObject for $SceneDiffuse
	RE::CTexture* Hooks::Hook_CreateTextureObject_SceneDiffuse(const char* a_name, uint32_t a_nWidth, uint32_t a_nHeight, int a_nDepth, RE::ETEX_Type a_eTT, RE::ETextureFlags a_nFlags, RE::ETEX_Format a_eTF, int a_nCustomID, uint8_t a9)
	{
		using OriginalFunction = RE::CTexture*(*)(const char*, uint32_t, uint32_t, int, RE::ETEX_Type, RE::ETextureFlags, RE::ETEX_Format, int, uint8_t);
		OriginalFunction originalFunc = *hookHandle_CreateTextureObject_SceneDiffuse;
		RE::CTexture* pTex = originalFunc(a_name, a_nWidth, a_nHeight, a_nDepth, a_eTT, a_nFlags, a_eTF, a_nCustomID, a9);

#if ADD_NEW_RENDER_TARGETS
#if SUPPORT_MSAA
		a_nFlags |= RE::ETextureFlags::FT_USAGE_MSAA;
#endif
		auto nPostAAFlags = a_nFlags;
#if FORCE_DLSS_SMAA_UAV && 0
		nPostAAFlags |= RE::ETextureFlags::FT_USAGE_UNORDERED_ACCESS | RE::ETextureFlags::FT_USAGE_UAV_RWTEXTURE;
#endif
		ptexTonemapTarget = originalFunc("$TonemapTarget", a_nWidth, a_nHeight, a_nDepth, a_eTT, a_nFlags, format, -1, a9);
		ptexPostAATarget = originalFunc("$PostAATarget", a_nWidth, a_nHeight, a_nDepth, a_eTT, nPostAAFlags, format, -1, a9);
		ptexUpscaleTarget = originalFunc("$UpscaleTarget", a_nWidth, a_nHeight, a_nDepth, a_eTT, a_nFlags, format, -1, a9);
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
		using OriginalFunction = RE::CTexture*(*)(const char*, int, int, void*, RE::ETEX_Type, RE::ETextureFlags, RE::ETEX_Format, int);
		OriginalFunction originalFunc = *hookHandle_CreateRenderTarget_PrevBackBuffer0A;
		auto _ptexPrevBackBuffer = originalFunc(a_name, a_nWidth, a_nHeight, a_cClear, a_eTT, a_nFlags, a_eTF, a_nCustomID);
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
		using OriginalFunction = RE::CTexture*(*)(const char*, int, int, void*, RE::ETEX_Type, RE::ETextureFlags, RE::ETEX_Format, int);
		OriginalFunction originalFunc = *hookHandle_CreateRenderTarget_PrevBackBuffer1A;
		auto _ptexPrevBackBuffer = originalFunc(a_name, a_nWidth, a_nHeight, a_cClear, a_eTT, a_nFlags, a_eTF, a_nCustomID);
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
		using OriginalFunction = bool(*)(RE::CTexture*, RE::ETEX_Format, void*);
		OriginalFunction originalFunc = *hookHandle_CreateRenderTarget_PrevBackBuffer0B;
		return originalFunc(a_this, a_eTF, a_cClear);
	}

	// Hook CTexture::CreateRenderTarget for $PrevBackBuffer1 in SPostEffectsUtils::Create - recreation
	bool Hooks::Hook_CreateRenderTarget_PrevBackBuffer1B(RE::CTexture* a_this, RE::ETEX_Format a_eTF, void* a_cClear)
	{
#if FORCE_DLSS_SMAA_UAV
		a_this->m_nFlags |= RE::ETextureFlags::FT_USAGE_UNORDERED_ACCESS | RE::ETextureFlags::FT_USAGE_UAV_RWTEXTURE;
#endif
		using OriginalFunction = bool(*)(RE::CTexture*, RE::ETEX_Format, void*);
		OriginalFunction originalFunc = *hookHandle_CreateRenderTarget_PrevBackBuffer1B;
		return originalFunc(a_this, a_eTF, a_cClear);
	}

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

	void Uninstall()
	{
		Hooks::Unhook();
	}
}

#include "includes/SharedEnd.h"