#pragma once

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Resource;
struct IDXGIAdapter;

namespace NGX
{
	class DLSS
	{
	public:
		static bool HasInit();
		// Needs init
		static bool IsSupported();

		// Must be called once before usage.
		// Returns whether DLSS is supported by hardware and driver.
		static bool Init(ID3D11Device* device, IDXGIAdapter* adapter = nullptr);
		// Should be called before shutdown or on device destruction.
		static void Deinit(ID3D11Device* device);

		static bool UpdateSettings(ID3D11Device* device, ID3D11DeviceContext* commandList, unsigned int outputWidth, unsigned int outputHeight, unsigned int renderWidth, unsigned int renderHeight, bool hdr = true, int _DLSSMode = 0);

		// Returns true if drawing didn't fail
		static bool Draw(ID3D11DeviceContext* commandList, ID3D11Resource* outputColor, ID3D11Resource* sourceColor, ID3D11Resource* motionVectors, ID3D11Resource* depthBuffer, ID3D11Resource* exposure /*= nullptr*/, float preExposure /*= 0*/, float fJitterX, float fJitterY, bool reset = false, unsigned int renderWidth = 0, unsigned int renderHeight = 0);
	};
}
