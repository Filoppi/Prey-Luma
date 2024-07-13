#pragma once

#include "Offsets.h"
#include "RE/RE.h"

namespace Hooks
{
	constexpr RE::ETEX_Format format = RE::ETEX_Format::eTF_R16G16B16A16F;
	constexpr RE::ETEX_Format format16f = RE::ETEX_Format::eTF_R16G16B16A16F;

	class Patches
	{
	public:
		static void Patch()
		{
			
			// Patch internal CryEngine RGBA8 to RGBA16F
			{
				// SPostEffectsUtils::Create
				const auto address = Offsets::baseAddress + 0x1071510;

				dku::Hook::WriteImm(address + 0x47D, format);   // $PrevFrameScaled

				dku::Hook::WriteImm(address + 0x3B9, format);   // $BackBufferScaled_d2
				dku::Hook::WriteImm(address + 0x541, format);   // $BackBufferScaledTemp_d2
				dku::Hook::WriteImm(address + 0x78D, format);   // $BackBufferScaled_d4
				dku::Hook::WriteImm(address + 0x852, format);   // $BackBufferScaledTemp_d4
				dku::Hook::WriteImm(address + 0x92D, format);   // $BackBufferScaled_d8
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

			// Patch swapchain DXGI_FORMAT from RGBA8 and DXGI_SWAP_EFFECT to DXGI_SWAP_EFFECT_FLIP_DISCARD
			{
				const auto address = Offsets::baseAddress + 0xF50000;
				dku::Hook::WriteImm(address + 0x4CE, format);

				constexpr uint8_t swap_effect = 0x4;
				dku::Hook::WriteImm(address + 0x50C, swap_effect);
			}
		}
	};
	class Hooks
	{
	public:
		static void Hook()
		{
			// Hook CD3D9Renderer::FlashRenderInternal because DXGI_SWAP_EFFECT_FLIP_DISCARD unbinds backbuffer during Present. So we need to call OMSetRenderTargets to bind it again every frame.
			{
				uintptr_t vtable = Offsets::baseAddress + 0x1DD2E08;
				auto      Hook = dku::Hook::AddVMTHook(&vtable, 0x13B, FUNC_INFO(Hook_FlashRenderInternal));

				using FlashRenderInternal_t = std::add_pointer_t<void(RE::CD3D9Renderer*, void*, bool, bool)>;
				_Hook_FlashRenderInternal = Hook->GetOldFunction<FlashRenderInternal_t>();
				Hook->Enable();
			}

			// Hook swapchain creation and set colorspace
			{
				_Hook_CreateDevice = dku::Hook::write_call(Offsets::baseAddress + 0xF53F37, Hook_CreateDevice);
			}

			// Replicate how the game treats render targets
			{
				// Hook SD3DPostEffectsUtils::CreateRenderTarget for $SceneDiffuse in CDeferredShading::CreateDeferredMaps
				_Hook_CreateRenderTarget = dku::Hook::write_call(Offsets::baseAddress + 0xF083F0, Hook_CreateRenderTarget);

				// Hook CTexture::CreateTextureObject for $SceneDiffuse
				_Hook_CreateTextureObject = dku::Hook::write_call(Offsets::baseAddress + 0x100F61F, Hook_CreateTextureObject);
			}

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

							pop(rax);
							pop(rcx);
						}
					};

					Patch patch(reinterpret_cast<uintptr_t>(GetUpscaleTargetRT));
					patch.ready();

					auto offset = std::make_pair(0x10E, 0x115);
					auto hook = dku::Hook::AddASMPatch(Offsets::baseAddress + 0xF7EB20, offset, &patch);
					hook->Enable();
				}
			}

			// Push our parameters
			{
				// Hook last FXSetPSFloat in CPostAA::RenderComposites to call it again pushing our params
				const auto address = Offsets::baseAddress + 0xFBD100;
				_Hook_FXSetPSFloat = dku::Hook::write_call(address + 0x933, Hook_FXSetPSFloat);
			}

			// UI shader
			{
				// Hook shader parsing (CShaderMan::mfParseParamComp) so we inject our own enum value into m_eCGParamType so we can pick it up in a later hook
				// We're not emplacing a new value into the static SParamDB sParams array because there's not enough empty space at the end. This is much simpler.
				{
					const auto address = Offsets::baseAddress + 0x1044580;
					_Hook_mfParseParamComp = dku::Hook::write_call(address + 0x2EF, Hook_mfParseParamComp);
				}

				// CHWShader_D3D::mfSetParameters checks the enum in a switch statement.
				// We'll end up in the default case as there as our enum value is not in the switch statement.
				// We hook there to handle our enum value manually.
				{
					struct Patch : Xbyak::CodeGenerator
					{
						Patch(uintptr_t a_addr)
						{
							// call our function
							mov(rcx, r9);
							mov(rdx, eax);
							mov(rax, a_addr);
							call(rax);

							// original code
							mov(eax, ptr[rdi - 0x20]);
							test(al, 2);
						}
					};

					Patch patch(reinterpret_cast<uintptr_t>(SetUIShaderParameters));
					patch.ready();

					auto offset = std::make_pair(0xB90, 0xB95);
					auto hook = dku::Hook::AddASMPatch(Offsets::baseAddress + 0xF1C290, offset, &patch);
					hook->Enable();
				}
			}

			// TAA jitter
			{
				// Hook CConstantBuffer::UpdateBuffer to push jitter offset instead of the unused fxaa params
				const auto address = Offsets::baseAddress + 0xF99FE0;
				_Hook_UpdateBuffer = dku::Hook::write_call(address + 0x1AAA, Hook_UpdateBuffer);
			}
		}

	private:
		static void          Hook_FlashRenderInternal(RE::CD3D9Renderer* a_this, void* pPlayer, bool bStereo, bool bDoRealRender);
		static bool          Hook_CreateDevice(RE::DeviceInfo* a_deviceInfo, uint64_t a2, uint64_t a3, uint64_t a4, int32_t a_width, int32_t a_height, int32_t a7, int32_t a_zbpp, void* a9, void* a10);
		static bool          Hook_CreateRenderTarget(const char* a_szTexName, RE::CTexture*& a_pTex, int a_iWidth, int a_iHeight, void* a_cClear, bool a_bUseAlpha, bool a_bMipMaps, RE::ETEX_Format a_eTF, int a_nCustomID, int a_nFlags);
		static RE::CTexture* Hook_CreateTextureObject(const char* a_name, uint32_t a_nWidth, uint32_t a_nHeight, int a_nDepth, RE::ETEX_Type a_eTT, uint32_t a_nFlags, RE::ETEX_Format a_eTF, int a_nCustomID, uint8_t a9);
		static bool          Hook_FXSetPSFloat(RE::CShader* a_this, const RE::CCryNameR& a_nameParam, RE::Vec4* a_fParams, int a_nParams);
		static bool          Hook_mfParseParamComp(void* a_this, int comp, RE::SCGParam* pCurParam, const char* szSemantic, char* params, const char* szAnnotations, void* FXParams, void* ef, uint32_t nParamFlags, RE::EHWShaderClass eSHClass, bool bExpressionOperand);
		static void          Hook_UpdateBuffer(RE::CConstantBuffer* a_this, void* a_src, size_t a_size, uint32_t a_numDataBlocks);

		static inline std::add_pointer_t<decltype(Hook_FlashRenderInternal)> _Hook_FlashRenderInternal;
		static inline std::add_pointer_t<decltype(Hook_CreateDevice)>        _Hook_CreateDevice;
		static inline std::add_pointer_t<decltype(Hook_CreateRenderTarget)> _Hook_CreateRenderTarget;
		static inline std::add_pointer_t<decltype(Hook_CreateTextureObject)> _Hook_CreateTextureObject;
		static inline std::add_pointer_t<decltype(Hook_FXSetPSFloat)>        _Hook_FXSetPSFloat;
		static inline std::add_pointer_t<decltype(Hook_mfParseParamComp)>    _Hook_mfParseParamComp;
		static inline std::add_pointer_t<decltype(Hook_UpdateBuffer)>        _Hook_UpdateBuffer;

		static uintptr_t            GetTonemapTargetRT() { return reinterpret_cast<uintptr_t>(ptexTonemapTarget); }
		static uintptr_t            GetPostAATargetRT() { return reinterpret_cast<uintptr_t>(ptexPostAATarget); }
		static uintptr_t            GetUpscaleTargetRT() { return reinterpret_cast<uintptr_t>(ptexUpscaleTarget); }
		static inline RE::CTexture* ptexTonemapTarget;
		static inline RE::CTexture* ptexPostAATarget;
		static inline RE::CTexture* ptexUpscaleTarget;

		static void SetUIShaderParameters(float* pVal, RE::ECGParam paramType);
	};

	void Install();
}