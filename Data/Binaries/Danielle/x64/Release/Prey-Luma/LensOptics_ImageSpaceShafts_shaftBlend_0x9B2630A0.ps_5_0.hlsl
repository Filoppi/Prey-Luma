cbuffer PER_BATCH : register(b0)
{
  float4 HDRParams : packoffset(c0);
}

#include "include/LensOptics.hlsl"

SamplerState shaftMap_s : register(s1);
Texture2D<float4> shaftMap : register(t1);

// shaftBlendPS
// This draws on the whole screen (or well, the rendering resolution area), after shaftsOccPS and shaftsPS.
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;

  r0.xyzw = shaftMap.Sample(shaftMap_s, v1.xy).xyzw;
	o0 = ToneMappedPreMulAlpha(r0, false); // LUMA FT: avoid making this HDR as it can both draw "sun shafts" but also a full screen additive tint, which we wouldn't want to raise
  o0.w = 1;
  return;
}