cbuffer PER_BATCH : register(b0)
{
  float4 HDRParams : packoffset(c0);
}

#include "include/LensOptics.hlsl"

#define _RT_SAMPLE2 1

SamplerState spectrumMap_s : register(s0);
Texture2D<float4> spectrumMap : register(t0);

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
  r1.x = 0.5;
  r1.y = 1 + -v1.x;
  r0.yzw = spectrumMap.Sample(spectrumMap_s, r1.xy).xyz;
  r0.xyz = r0.xxx * r0.yzw;
  r0.xyz = v3.xyz * r0.xyz;
  r0.w = v3.w;
	o0 = ToneMappedPreMulAlpha(r0);
  return;
}