cbuffer PER_BATCH : register(b0)
{
  float4 HDRParams : packoffset(c0);
}

#include "include/LensOptics.hlsl"

SamplerState spectrumMap_s : register(s0);
SamplerState irisBaseMap_s : register(s1);
Texture2D<float4> spectrumMap : register(t0);
Texture2D<float4> irisBaseMap : register(t1);

// IrisShaftsGlowPS()
// This one draws stuff like a horizontal red line on the left and right of the sun, but it's not limited to that (it depends on the textures and vertices).
// This requires aspect ratio correction as it was stretched in ultrawide.
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float4 inColor : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;

  r0.xy = v1.xy * float2(1,-1) + float2(0,1);
  r0.xyz = irisBaseMap.Sample(irisBaseMap_s, r0.xy).xyz; // This one determines the base color of what we draw
  r1.xyz = spectrumMap.Sample(spectrumMap_s, v1.xy).xyz; // This one determines the shape, brightness (like, the alpha intensity) and color of what we draw
  r0.xyz = r1.xyz * r0.xyz;
// LUMA FT: desatura the iris to make up for the fact that lens flare are now 100% additive and can produce colors beyond 1 in SDR, so they don't clip anymore.
// This shader in particular can draw a red line that was often clipped (to white) in SDR due to the background already being 1, while in HDR its additive and always visible
// and it sticks out a lot more, to the point where it looks ugly. The color is usually mostly sourced from the "irisBaseMap" texture.
#if ENABLE_LENS_OPTICS_HDR
  r0.xyz = lerp(r0.xyz, GetLuminance(r0.xyz), 2.0 / 3.0);
#endif
  r0.xyz = inColor.xyz * r0.xyz; // This is the color tint of all lens optics sun effects
  r0.w = inColor.w;
	o0 = ToneMappedPreMulAlpha(r0);
  return;
}