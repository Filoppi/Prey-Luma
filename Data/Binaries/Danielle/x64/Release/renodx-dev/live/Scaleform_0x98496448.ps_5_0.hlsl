#include "include/Scaleform.hlsl"

SamplerState texMap0_s : register(s0);
Texture2D<float4> texMap0 : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  float4 v3 : COLOR1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;

  r0.xyzw = texMap0.Sample(texMap0_s, v1.xy).xyzw;
  r0.xyzw = -v2.xyzw + r0.xyzw;
  r0.xyzw = v3.zzzz * r0.xyzw + v2.xyzw;
  r0.xyzw = r0.xyzw * cBitmapColorTransform._m00_m01_m02_m03 + cBitmapColorTransform._m10_m11_m12_m13;
  r0.w = v3.w * r0.w;
  r1.xyzw = float4(-1,-1,-1,-1) + r0.xyzw;
  r0.xyzw = r0.wwww * r1.xyzw + float4(1,1,1,1);
  o0.rgba = PremultiplyAlpha(r0);
  return;
}