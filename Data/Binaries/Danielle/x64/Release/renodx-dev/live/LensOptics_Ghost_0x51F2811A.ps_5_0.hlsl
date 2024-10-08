cbuffer PER_BATCH : register(b0)
{
  float4 HDRParams : packoffset(c0);
}

#include "include/LensOptics.hlsl"

SamplerState ghostSpriteTiles_s : register(s0);
Texture2D<float4> ghostSpriteTiles : register(t0);

#define SCENE_HDR_MULTIPLIER 32.0

// lensGhostPS
// e.g. draws a "ghost" (bloom) sprite around the sun.
// This one was already corrected by aspect ratio.
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 outColor : SV_Target0)
{
  float4 r0,r1,r2;

  r0.xyzw = ghostSpriteTiles.Sample(ghostSpriteTiles_s, v1.xy).xyzw;
  r1.x = SCENE_HDR_MULTIPLIER * r0.w;
  r0.xyz = r1.xxx * r0.xyz;
  r0.xyzw = v2.xyzw * r0.xyzw;
	outColor = ToneMappedPreMulAlpha(r0);
  return;
}