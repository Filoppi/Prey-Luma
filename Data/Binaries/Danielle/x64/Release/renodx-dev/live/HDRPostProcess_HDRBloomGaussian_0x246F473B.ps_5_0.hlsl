#include "include/Common.hlsl"

cbuffer PER_BATCH : register(b0)
{
  float4 HDRParams0 : packoffset(c0);
}

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState ssBloom : register(s0);
Texture2D<float4> bloomSourceTex : register(t0);

// 3Dmigoto declarations
#define cmp -

float2 MapViewportToRaster(float2 normalizedViewportPos, bool bOtherEye = false)
{
		return normalizedViewportPos * CV_HPosScale.xy;
}

float2 ClampScreenTC(float2 TC, float2 maxTC)
{
	return clamp(TC, 0, maxTC.xy);
}

float2 ClampScreenTC(float2 TC)
{
	return ClampScreenTC(TC, CV_HPosClamp.xy);
}

// This runs before tonemapping
void main(
  float4 WPos : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
  outColor = 0;

#if !ENABLE_BLOOM
  return;
#endif

  // LUMA FT: adjusted bloom UV offset by aspect ratio, assuming the value was always targeting 16:9 and never adjusted for other aspect ratios.
  // This also fixes bloom visibly changing in intensity when the rendering resolution scale was changed.
  float screenAspectRatio = CV_ScreenSize.w / CV_ScreenSize.z;
  float2 HDRParams0AspectRatioAdjusted = float2(HDRParams0.x * (NativeAspectRatio / screenAspectRatio), HDRParams0.y) * CV_HPosScale.xy;

  static const uint weightsNumVanilla = 15;
	static const float weightsVanilla[weightsNumVanilla] = { 153, 816, 3060, 8568, 18564, 31824, 43758, 48620, 43758, 31824, 18564, 8568, 3060, 816, 153 };
#if BLOOM_QUALITY <= 0
	static const uint weightsNum = weightsNumVanilla;
	static const float weights[weightsNum] = weightsVanilla;
	static const float weightSum = 262106.0;
#else // LUMA FT: added a higher (and dynamic) bloom quality to fix the visible bloom tiling from small light sources (in view space)
	static const uint weightsNum = (weightsNumVanilla * 2) - 1; // It needs to be odd (e.g. 15, 29)
	float weights[weightsNum];
	float weightSum = 0;
	[unroll]
  for (uint i = 0; i < weightsNum / 2; i++)
  {
    bool secondHalf = i >= (((weightsNumVanilla + 1) / 2) - 1);
    weights[i*2] = weightsVanilla[i];
    weights[(i*2)+1] = lerp(weightsVanilla[i], weightsVanilla[i+1], secondHalf ? 0.75 : 0.25); // The grow ratio between each other index was about 4x, so we replicate that
    weightSum += weights[(i*2)] + weights[(i*2)+1];
  }
  // Do the last index manually
  weights[weightsNum-1] = weightsVanilla[weightsNumVanilla-1];
  weightSum += weights[weightsNum-1];
#endif
	
	int3 pixelCoords = int3(WPos.xy, 0);
	float2 baseTC = MapViewportToRaster(inBaseTC.xy);
	float2 coords = baseTC - HDRParams0AspectRatioAdjusted.xy * float(uint(weightsNumVanilla / 2));
	
	[unroll]
	for (uint i = 0; i < weightsNum; ++i)
	{
		outColor.rgb += bloomSourceTex.Sample(ssBloom, coords).rgb * (weights[i] / weightSum);
    // LUMA FT: adjusted the coords scaling to make sure the "size" of the bloom remains consistent independently of the samples count
    static const float offsetAdjustment = ((float)weightsNumVanilla - 0.5) / (float)weightsNum;
		coords = ClampScreenTC(coords + (HDRParams0AspectRatioAdjusted.xy * offsetAdjustment));
	}

  return;
}