cbuffer PER_BATCH : register(b0)
{
  float4 HDRColorBalance : packoffset(c0);
  float4 SunShafts_SunCol : packoffset(c1);
  float4 HDREyeAdaptation : packoffset(c2);
  float4 HDRFilmCurve : packoffset(c3);
  float4 HDRBloomColor : packoffset(c4);
}

// Sunshafts
#define _RT_SAMPLE3 1
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