#include "include/Common.hlsl"

#include "include/CBuffer_PerViewGlobal.hlsl"

cbuffer CBExposure : register(b0)
{
  struct
  {
    float4 SampleLumOffsets[2];
    float EyeAdaptationSpeed;
    float RangeAdaptationSpeed;
    float2 __padding;
  } cbExposure : packoffset(c0);
}

SamplerState _samp0_s : register(s0);
SamplerState _samp1_s : register(s1);
Texture2D<float4> _tex0_D3D11 : register(t0);
Texture2D<float4> _tex1_D3D11 : register(t1);

float2 MapViewportToRaster(float2 normalizedViewportPos, bool bOtherEye = false)
{
	return normalizedViewportPos * CV_HPosScale.xy;
}

// HDRSampleLumInitialPS
void main(
  float4 WPos : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
  half fRecipSampleCount = 0.25h;
  half2 vLumInfo = 0;

	half fCenterWeight = 1; // saturate(1-length(inBaseTC.xy*2-1));
  
	float2 inputResolution;
	_tex1_D3D11.GetDimensions(inputResolution.x, inputResolution.y); // We can't use "CV_ScreenSize" here as that's for the output resolution
	float2 sampleUVClamp = CV_HPosScale.xy - (0.5 / inputResolution);

	float4 cBase = _tex1_D3D11.Sample(_samp1_s, clamp(MapViewportToRaster(inBaseTC.xy), 0.0, sampleUVClamp.xy));

	float  baseColor[4] = {cBase.x, cBase.y, cBase.z, cBase.w};
	float2 sampleOffsets[4] = { cbExposure.SampleLumOffsets[0].xy, cbExposure.SampleLumOffsets[0].zw, cbExposure.SampleLumOffsets[1].xy, cbExposure.SampleLumOffsets[1].zw };
	
	_tex0_D3D11.GetDimensions(inputResolution.x, inputResolution.y);
	sampleUVClamp = CV_HPosScale.xy - (0.5 / inputResolution);

	[unroll] for (int i = 0; i < 4; ++i)
	{
    // LUMA FT: fixed bad UVs DRS scaling
		half3 cTex = _tex0_D3D11.Sample(_samp0_s, clamp(MapViewportToRaster(inBaseTC.xy + sampleOffsets[i]), 0.0, sampleUVClamp.xy)).rgb;
		half fLum = all(isfinite(cTex)) ? GetLuminance(cTex) : 0;

		vLumInfo.x += log(fLum + 1e-6);                      // Luminance
		vLumInfo.y += log(fLum / baseColor[i] * PI + 1e-6);  // Illuminance
	}

  outColor.xy = fCenterWeight * fRecipSampleCount * vLumInfo;
  outColor.z = 0;
  outColor.w = 1;
}