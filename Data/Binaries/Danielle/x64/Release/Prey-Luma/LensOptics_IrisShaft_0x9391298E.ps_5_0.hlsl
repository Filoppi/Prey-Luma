cbuffer PER_BATCH : register(b0)
{
  float4 HDRParams : packoffset(c0);
}

#include "include/LensOptics.hlsl"

SamplerState irisBaseMap_s : register(s1);
Texture2D<float4> irisBaseMap : register(t1);

// IrisShaftsGlowPS()
// This one draws a kind of irregular "star" shaped rays around the sun.
// This requires aspect ratio correction as it was stretched in ultrawide.
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float4 v3 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;

  r0.xy = v1.xy * float2(1,-1) + float2(0,1);
  r0.xyz = irisBaseMap.Sample(irisBaseMap_s, r0.xy).xyz;
  r0.xyz = v3.xyz * r0.xyz;
  r0.w = v3.w;
	o0 = ToneMappedPreMulAlpha(r0);
  return;
}