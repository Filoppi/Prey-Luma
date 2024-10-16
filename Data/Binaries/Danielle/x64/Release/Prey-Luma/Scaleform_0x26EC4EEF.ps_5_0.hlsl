#include "include/Scaleform.hlsl"

void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;

  r0.xyzw = v1.xyzw * cBitmapColorTransform._m00_m01_m02_m03 + cBitmapColorTransform._m10_m11_m12_m13;
  r1.xyzw = float4(-1,-1,-1,-1) + r0.xyzw;
  r0.xyzw = r0.wwww * r1.xyzw + float4(1,1,1,1);
  o0.rgba = PremultiplyAlpha(r0);
  return;
}