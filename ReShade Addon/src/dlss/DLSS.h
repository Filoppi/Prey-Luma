#pragma once

#define ENABLE_NGX 1

#if ENABLE_NGX

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Resource;
struct IDXGIAdapter;

namespace NGX
{
	struct DLSSInstanceData;

	class DLSS
	{
	public:
		static bool HasInit(const DLSSInstanceData* data);
		// Needs init
		static bool IsSupported(const DLSSInstanceData* data);

		// Must be called once before usage. Still expects Deinit() to be called even if it failed.
		// Returns whether DLSS is supported by hardware and driver.
		// Fill in a data "handle", there can be more than one at a time.
		static bool Init(DLSSInstanceData*& data, ID3D11Device* device, IDXGIAdapter* adapter = nullptr);
		// Should be called before shutdown or on device destruction.
		static void Deinit(DLSSInstanceData*& data, ID3D11Device* optional_device = nullptr);

		// Expects the same command list all the times
		static bool UpdateSettings(DLSSInstanceData* data, ID3D11DeviceContext* commandList, unsigned int outputWidth, unsigned int outputHeight, unsigned int renderWidth, unsigned int renderHeight, bool hdr = true, bool dynamicResolution = false);

		// Returns true if drawing didn't fail
		// Expects the same command list all the times
		static bool Draw(const DLSSInstanceData* data, ID3D11DeviceContext* commandList, ID3D11Resource* outputColor, ID3D11Resource* sourceColor, ID3D11Resource* motionVectors, ID3D11Resource* depthBuffer, ID3D11Resource* exposure /*= nullptr*/, float preExposure /*= 0*/, float fJitterX, float fJitterY, bool reset = false, unsigned int renderWidth = 0, unsigned int renderHeight = 0);
	};
}

#endif