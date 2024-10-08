cbuffer PER_BATCH : register(b0)
{
  float4 HDRColorBalance : packoffset(c0);
  float4 SunShafts_SunCol : packoffset(c1);
  float4 HDREyeAdaptation : packoffset(c2);
  float4 HDRFilmCurve : packoffset(c3);
  float4 HDRBloomColor : packoffset(c4);
  float4 ArkDistanceSat : packoffset(c5);
}

// FXAA
#define _RT_SAMPLE0 1
// Ark (Prey) distance based desaturation
#define _RT_SAMPLE2 1
// Sunshafts
#define _RT_SAMPLE3 1
// Legacy exposure
#define _RT_SAMPLE4 1

#include "HDRPostProcess_HDRFinalScene.hlsl"

// LUMA: this shader is never directly used by the game, as when all the above permutations are on at the same time,
// the game bugs out and outputs the albedo g-buffer instead, completely skipping tonemapping.
void main(
  float4 WPos : SV_Position0,
  float4 baseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
    HDRFinalScenePS(WPos, baseTC, outColor);
    return;
}