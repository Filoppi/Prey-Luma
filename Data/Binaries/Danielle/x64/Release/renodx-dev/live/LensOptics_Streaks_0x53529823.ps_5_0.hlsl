// ---- Created with 3Dmigoto v1.3.16 on Mon Jul 15 05:48:06 2024

cbuffer PER_BATCH : register(b0)
{
  float4 HDRParams : packoffset(c0);
}

#include "include/LensOptics.hlsl"

#define _RT_SAMPLE2 0

// StreaksPS
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float4 v3 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;

  r0.x = v1.x * v1.x;
  r0.x = -11.5415602 * r0.x;
  r0.x = exp2(r0.x);
  r0.xyz = v3.xyz * r0.xxx;
  r0.w = v3.w;
	o0 = ToneMappedPreMulAlpha(r0);
  return;
}