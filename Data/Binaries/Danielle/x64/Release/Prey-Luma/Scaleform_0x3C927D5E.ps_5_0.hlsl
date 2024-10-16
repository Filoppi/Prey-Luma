#include "include/Scaleform.hlsl"

void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  o0.rgba = PremultiplyAlpha(cBitmapColorTransform._m00_m01_m02_m03);
  return;
}