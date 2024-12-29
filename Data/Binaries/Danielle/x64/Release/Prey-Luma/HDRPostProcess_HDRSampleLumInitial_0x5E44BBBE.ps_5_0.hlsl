#include "include/Common.hlsl"

#include "include/CBuffer_PerViewGlobal.hlsl"

cbuffer CBExposure : register(b0)
{
  struct
  {
    float4 SampleLumOffsets[2]; // These aren't adjusted by DRS
    float EyeAdaptationSpeed;
    float RangeAdaptationSpeed;
    float2 __padding;
  } cbExposure : packoffset(c0);
}

SamplerState _samp0_s : register(s0);
SamplerState _samp1_s : register(s1);
Texture2D<float4> _tex0_D3D11 : register(t0); // Scene texture
Texture2D<float4> _tex1_D3D11 : register(t1); // Exposure calculated by "HDRSampleBaseLum"

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

	half fCenterWeight = 1; // Crytek: saturate(1-length(inBaseTC.xy*2-1));
  
	float2 inputResolution;
	_tex1_D3D11.GetDimensions(inputResolution.x, inputResolution.y); // We can't use "CV_ScreenSize" here as that's for the output resolution
	float2 sampleUVClamp = CV_HPosScale.xy - (0.5 / inputResolution);

	float4 cBase = _tex1_D3D11.Sample(_samp1_s, clamp(MapViewportToRaster(inBaseTC.xy), 0.0, sampleUVClamp.xy));

	float  baseColor[4] = {cBase.x, cBase.y, cBase.z, cBase.w};
	float2 sampleOffsets[4] = { cbExposure.SampleLumOffsets[0].xy, cbExposure.SampleLumOffsets[0].zw, cbExposure.SampleLumOffsets[1].xy, cbExposure.SampleLumOffsets[1].zw };
	
	_tex0_D3D11.GetDimensions(inputResolution.x, inputResolution.y);
	sampleUVClamp = CV_HPosScale.xy - (0.5 / inputResolution);
	
#if 1 // LUMA FT: restored aspect ratio based exposure calculations to put more weight in samples at the center of the screen, on X only, to account for ultrawide
	float inputAspectRatio = max(inputResolution.x / inputResolution.y, 1.0);
	float2 NDC = inBaseTC.xy * 2.0 - 1.0;
	float minWeight = 1.0 / inputAspectRatio; // Make it relative to the AR, so it gets progressively smaller on wider ARs
	float edgesTotalWeight = lerp(1.0, minWeight, 0.5) * (inputAspectRatio - 1.0); // Lerp by 0.5 as we blend from full weight to "minWeight"
#if 0 // Optionally we could leave this at 1 too, or set it to any value > 1! We already adjust the total weight below anyway
	float maxWeight = 1.0 + edgesTotalWeight;
#else
	float maxWeight = 1.0;
#endif
	float centerTotalWeight = lerp(maxWeight, 1.0, 0.5);
	// From what NDC position we start being beyond a square in the center of the screen?
	// The weight if "maxWeight" from the center of the inner square, then gradually go to 1, then from the edges gradually go to "minWeight"
	if (abs(NDC.x) * inputAspectRatio > 1)
	{
		fCenterWeight = lerp(1.0, minWeight, remap(abs(NDC.x), 1.0 / inputAspectRatio, 1.0, 0.0, 1.0));
	}
	else
	{
		fCenterWeight = lerp(maxWeight, 1.0, remap(abs(NDC.x), 1.0 / inputAspectRatio, 0.0, 1.0, 0.0));
	}
	// Normalize all weights to make sure their average is 1
	fCenterWeight /= (edgesTotalWeight + centerTotalWeight) / inputAspectRatio;
#endif

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