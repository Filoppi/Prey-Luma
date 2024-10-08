#include "include/Scaleform.hlsl"

SamplerState texMap0_s : register(s0);
Texture2D<float4> texMap0 : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r1;

  r1.xyzw = texMap0.Sample(texMap0_s, v1.xy).xyzw;
  r1.xyzw = r1.xyzw * cBitmapColorTransform._m00_m01_m02_m03 + cBitmapColorTransform._m10_m11_m12_m13;
  o0.rgba = PremultiplyAlpha(r1);
  return;
}