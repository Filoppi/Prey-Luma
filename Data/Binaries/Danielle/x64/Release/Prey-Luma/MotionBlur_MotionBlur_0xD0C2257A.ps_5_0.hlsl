#define _RT_SAMPLE2 1

#include "MotionBlur_MotionBlur.hlsl"

void main(
  float4 WPos : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
  outColor = MotionBlurPS(WPos, inBaseTC);
  return;
}