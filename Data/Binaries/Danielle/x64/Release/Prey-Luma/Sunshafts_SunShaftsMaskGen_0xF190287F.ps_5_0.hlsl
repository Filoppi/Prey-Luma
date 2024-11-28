#include "include/Common.hlsl"

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState _tex0_s : register(s0);
SamplerState _tex1_s : register(s1);
Texture2D<float4> _tex0 : register(t0);
Texture2D<float4> _tex1 : register(t1);

float2 MapViewportToRaster(float2 normalizedViewportPos, bool bOtherEye = false)
{
		return normalizedViewportPos * CV_HPosScale.xy;
}

// This draws before "SunShaftsGen"
void main(
  float4 HPosition : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
  float2 sampleUV = MapViewportToRaster(inBaseTC.xy);

#if REJITTER_SUNSHAFTS
  // Dejitter the background depth/color, to get a more consistent result over time (basically a quick way of resolving TAA)
  sampleUV -= LumaData.CameraJitters.xy * float2(0.5, -0.5);
#endif
	
	sampleUV = min(sampleUV, CV_HPosScale.xy);
	
  //TODO LUMA: use .Load() for these textures?
  float sceneDepth = _tex0.Sample(_tex0_s, sampleUV).x; // Linear depth (0 camera origin or near, 1 far)
  outColor = float4(sceneDepth, sceneDepth, sceneDepth, 1 - sceneDepth.x);

  float3 sceneCol = _tex1.Sample(_tex1_s, sampleUV).xyz; // comes straight from hdr scaled target (exposure isn't adjusted yet)
	
#if 1 // LUMA FT: changed the formula with luminance (doesn't make much difference, but it should look better and make more sense for HDR) (now sun shafts are HDR and "linear" space)
  outColor.xyz *= GetLuminance(sceneCol) * SunShaftsBrightnessMultiplier;
#else
  // cheaper and looks nicer
  outColor.xyz *= dot(sceneCol, (1.0/3.0) * SunShaftsBrightnessMultiplier);
#endif
  
  return;
}