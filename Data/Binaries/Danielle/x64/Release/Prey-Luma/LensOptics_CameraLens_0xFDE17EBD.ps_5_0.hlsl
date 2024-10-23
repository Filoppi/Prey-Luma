#define _RT_SAMPLE2 0
#define _RT_SAMPLE3 0
#define _RT_SAMPLE4 0

cbuffer PER_BATCH : register(b0)
{
  float4 lensDetailParams : packoffset(c0);
  float4 HDRParams : packoffset(c1);
}

#include "include/LensOptics.hlsl"

SamplerState lensMap_s : register(s2);
Texture2D<float4> lensMap : register(t2);

// CameraLensPS
// This one seems to draw some kind of vertical streak, but the intensity is always ~0 so it's not visible
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float4 v3 : COLOR0,
  out float4 outColor : SV_Target0)
{
  float4 r0,r1,r2;

  r0.xyzw = lensMap.Sample(lensMap_s, v1.xy).xyzw;
  r0.xyzw = lensDetailParams.xxxx * r0.xyzw;
  r0.xyzw = v3.xyzw * r0.xyzw;
	outColor = ToneMappedPreMulAlpha(r0);
  return;
}