cbuffer PER_BATCH : register(b0)
{
  float4 lensDetailParams : packoffset(c0);
  float4 HDRParams : packoffset(c1);
}

#include "include/LensOptics.hlsl"

SamplerState orbMap_s : register(s0);
Texture2D<float4> orbMap : register(t0);

// CameraOrbsPS
// This draws a "orbs" on the camera, kinda like lens dirt
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float4 v3 : COLOR0,
  out float4 outColor : SV_Target0)
{
  float4 r0,r1,r2;

  r0.xyzw = orbMap.Sample(orbMap_s, v1.xy).xyzw;
  r0.xyzw = lensDetailParams.xxxx * r0.xyzw;
  r0.xyzw = v3.xyzw * r0.xyzw;
	outColor = ToneMappedPreMulAlpha(r0);
  return;
}