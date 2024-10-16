// The order of these parameters is different depending on the "HDRFinalScenePS" permutation (a CryEngine quirk)
cbuffer PER_BATCH : register(b0)
{
  float4 HDRColorBalance : packoffset(c0);
  float4 HDREyeAdaptation : packoffset(c1);
  float4 HDRFilmCurve : packoffset(c2);
  float4 HDRBloomColor : packoffset(c3);
}

// Legacy exposure
#define _RT_SAMPLE4 1

#include "HDRPostProcess_HDRFinalScene.hlsl"

void main(
  float4 WPos : SV_Position0,
  float4 baseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
  HDRFinalScenePS(WPos, baseTC, outColor);
  return;
}