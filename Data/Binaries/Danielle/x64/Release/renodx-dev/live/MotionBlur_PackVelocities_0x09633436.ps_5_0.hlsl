cbuffer PER_BATCH : register(b0)
{
  row_major float4x4 mViewProjPrev : packoffset(c0);
  float4 vMotionBlurParams : packoffset(c4);
}

#include "MotionBlur_PackVelocities.hlsl"

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  vtxOut OUT;
  OUT.WPos = v0;
  OUT.baseTC = v1;
  pixout IN = PackVelocitiesPS(OUT);
  o0 = IN.Color;
  return;
}