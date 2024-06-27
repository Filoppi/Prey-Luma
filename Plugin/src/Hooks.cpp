#include "Hooks.h"

#include "Settings.h"

#include <dxgi1_4.h>

static const float MaxGamutExpansion[2] =
{
	0.0037935f,
	0.0102255f
};

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

		DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

		swapChain3->SetColorSpace1(colorSpace);
		swapChain3->Release();

		// good moment late enough to register reshade settings
		Settings::Main::GetSingleton()->RegisterReshadeOverlay();

		return bReturn;
	}

	bool Hooks::Hook_CreateRenderTarget(const char* a_szTexName, RE::CTexture*& a_pTex, int a_iWidth, int a_iHeight, void* a_cClear, bool a_bUseAlpha, bool a_bMipMaps, RE::ETEX_Format a_eTF, int a_nCustomID, int a_nFlags)
	{
		bool bReturn = _Hook_CreateRenderTarget(a_szTexName, a_pTex, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, a_eTF, a_nCustomID, a_nFlags);

		// add ours
		_Hook_CreateRenderTarget("$TonemapTarget", ptexTonemapTarget, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, RE::eTF_R16G16B16A16F, -1, a_nFlags);
		_Hook_CreateRenderTarget("$PostAATarget", ptexPostAATarget, a_iWidth, a_iHeight, a_cClear, a_bUseAlpha, a_bMipMaps, RE::eTF_R16G16B16A16F, -1, a_nFlags);

		return bReturn;
	}

	RE::CTexture* Hooks::Hook_CreateTextureObject(const char* a_name, uint32_t a_nWidth, uint32_t a_nHeight, int a_nDepth, RE::ETEX_Type a_eTT, uint32_t a_nFlags, RE::ETEX_Format a_eTF, int a_nCustomID, uint8_t a9)
	{
		RE::CTexture* pTex = _Hook_CreateTextureObject(a_name, a_nWidth, a_nHeight, a_nDepth, a_eTT, a_nFlags, a_eTF, a_nCustomID, a9);

		// add ours
		ptexTonemapTarget = _Hook_CreateTextureObject("$TonemapTarget", a_nWidth, a_nHeight, a_nDepth, a_eTT, a_nFlags, RE::eTF_R16G16B16A16F, -1, a9);
		ptexPostAATarget = _Hook_CreateTextureObject("$PostAATarget", a_nWidth, a_nHeight, a_nDepth, a_eTT, a_nFlags, RE::eTF_R16G16B16A16F, -1, a9);

		return pTex;
	}

	float NitsToPQ(float Y)
	{
		Y = std::powf(Y / 10000.f, 0.1593017578125f);

		// E'
		return std::powf((0.8359375f + 18.8515625f * Y)
		               / (1.f        + 18.6875f    * Y)
		       , 78.84375f);
	}

	bool Hooks::Hook_FXSetPSFloat(RE::CShader* a_this, const RE::CCryNameR& a_nameParam, RE::Vec4* a_fParams, int a_nParams)
	{
		// run original
		_Hook_FXSetPSFloat(a_this, a_nameParam, a_fParams, a_nParams);

		// run with ours
		static RE::CCryNameR lumaParamsName { "LumaTonemappingParams" };

		const auto settings = Settings::Main::GetSingleton();

		// max luminance
		float fMaxLuminance = settings->PeakBrightness.GetValue();
		float fMaxLuminanceHalf = fMaxLuminance * 0.5f;
		fMaxLuminance = NitsToPQ(fMaxLuminance);
		fMaxLuminanceHalf = NitsToPQ(fMaxLuminanceHalf);

		// game paperwhite
		RE::HDRSetupParams hdrParams;
		RE::C3DEngine* c3Engine = *Offsets::pC3DEngine;
		c3Engine->GetHDRSetupParams(hdrParams);

		float fPaperWhite = settings->GamePaperWhite.GetValue();
		fPaperWhite /= std::powf(10.f, 0.034607309f + 0.7577371f * log10(hdrParams.HDRFilmCurve.w * 203.f));

		// extend gamut
		float fExtendGamut = settings->ExtendGamut.GetValue();
		int32_t iExtendGamutTarget = settings->ExtendGamutTarget.GetValue();
		fExtendGamut *= MaxGamutExpansion[iExtendGamutTarget];
		fExtendGamut += 1.f;
		// store gamut target as sign
		fExtendGamut *= float(iExtendGamutTarget * 2 - 1);

		RE::Vec4 lumaParams = { fMaxLuminance, fMaxLuminanceHalf, fPaperWhite, fExtendGamut };

		bool bSuccess = _Hook_FXSetPSFloat(a_this, lumaParamsName, &lumaParams, 1);
		return bSuccess;
	}

	bool Hooks::Hook_mfParseParamComp(void* a_this, int comp, RE::SCGParam* pCurParam, const char* szSemantic, char* params, const char* szAnnotations, void* FXParams, void* ef, uint32_t nParamFlags, RE::EHWShaderClass eSHClass, bool bExpressionOperand)
	{
		// run original
		bool bResult = _Hook_mfParseParamComp(a_this, comp, pCurParam, szSemantic, params, szAnnotations, FXParams, ef, nParamFlags, eSHClass, bExpressionOperand);

		// our param will fail to parse fully because there's no relevant entry in the sParams array, so finish it up manually
		if (!bResult && pCurParam && stricmp(szSemantic, "PB_SFLumaUILuminance") == 0) {
			pCurParam->m_eCGParamType = RE::ECGParam::ECGP_LumaUILuminance;
			return true; 
		}

		return bResult;
	}

	void Hooks::SetUIShaderParameters(float* pVal, RE::ECGParam paramType)
	{
		if (paramType == RE::ECGParam::ECGP_LumaUILuminance) {
			const auto settings = Settings::Main::GetSingleton();
			float      fUILuminance = settings->UILuminance.GetValue();
			pVal[0] = fUILuminance / 80.f;
			pVal[1] = 0.f;
			pVal[2] = 0.f;
			pVal[3] = 0.f;
		}
	}

	void Install()
	{
		Patches::Patch();
		Hooks::Hook();
	}
}
