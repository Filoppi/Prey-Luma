#include "include/Common.hlsl"
#include "include/Scaleform.hlsl"
#include "include/LensDistortion.hlsl"

#include "include/CBuffer_PerViewGlobal.hlsl"

cbuffer PER_BATCH : register(b0)
{
  row_major float4x4 cCompositeMat : packoffset(c0);
}

void main(
  int4 v0 : POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : COLOR1,
  out float4 o0 : SV_Position0,
  out float4 o1 : COLOR0,
  out float4 o2 : COLOR1)
{
  float4 r0;
  r0.xy = (int2)v0.xy;
  r0.z = 1;
  o0.x = dot(cCompositeMat._m00_m01_m03, r0.xyz);
  o0.y = dot(cCompositeMat._m10_m11_m13, r0.xyz);
  o0.z = dot(cCompositeMat._m20_m21_m23, r0.xyz);
  o0.w = dot(cCompositeMat._m30_m31_m33, r0.xyz);
  o1.xyzw = v1.xyzw;
  o2.xyzw = v2.xyzw;
  
#if ENABLE_SCREEN_DISTORTION
  // Inverse lens distortion
  if (LumaUIData.WritingOnSwapchain == 1 && LumaSettings.LensDistortion && isLinearProjectionMatrix(cCompositeMat))
  {
    o0.xyz /= o0.w; // From clip to NDC space
    o0.w = 1; // no need to convert it back to clip space, the GPU would do it again anyway
    o0.y = -o0.y; // Adapt to normal NDC coordinates
    o0.xy = PerfectPerspectiveLensDistortion_Inverse(o0.xy, 1.0 / CV_ProjRatio.z, CV_ScreenSize.xy, true);
    o0.y = -o0.y;
  }
#endif
}