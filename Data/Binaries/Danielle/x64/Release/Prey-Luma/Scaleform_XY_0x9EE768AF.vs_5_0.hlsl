#include "include/Common.hlsl"
#include "include/Scaleform.hlsl"
#include "include/LensDistortion.hlsl"

#include "include/CBuffer_PerViewGlobal.hlsl"

cbuffer PER_BATCH : register(b0)
{
  row_major float4x4 cCompositeMat : packoffset(c0);
  row_major float2x4 cTexGenMat0 : packoffset(c4);
}

void main(
  int4 v0 : POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : COLOR1,
  out float4 o0 : SV_Position0,
  out float2 o1 : TEXCOORD0,
  out float4 o2 : COLOR0,
  out float4 o3 : COLOR1)
{
  float4 r0;
  r0.xy = (int2)v0.xy;
  r0.z = 1;
  //TODO LUMA: these matrices are actually updated only at 60fps or so, making them lag behind at higher frame rates.
  //Given that they represent a world to screen position traslation, we could determine back the world position and re-project on screen it with the updated projection matrix.
  o0.x = dot(cCompositeMat._m00_m01_m03, r0.xyz);
  o0.y = dot(cCompositeMat._m10_m11_m13, r0.xyz);
  o0.z = dot(cCompositeMat._m20_m21_m23, r0.xyz);
  o0.w = dot(cCompositeMat._m30_m31_m33, r0.xyz);
  o1.x = dot(cTexGenMat0._m00_m01_m03, r0.xyz);
  o1.y = dot(cTexGenMat0._m10_m11_m13, r0.xyz);
  o2.xyzw = v1.xyzw;
  o3.xyzw = v2.xyzw;
  
#if ENABLE_SCREEN_DISTORTION
  // Inverse lens distortion
  //TODOFT: avoid distorting the mission indicators when they are at the edges of the screen?
  if (LumaUIData.WritingOnSwapchain == 1 && LumaSettings.LensDistortion && isViewProjectionMatrix(cCompositeMat))
  {
    o0.xyz /= o0.w; // From clip to NDC space
    o0.w = 1; // no need to convert it back to clip space, the GPU would do it again anyway
    o0.y = -o0.y; // Adapt to normal NDC coordinates
    o0.xy = PerfectPerspectiveLensDistortion_Inverse(o0.xy, 1.0 / CV_ProjRatio.z, CV_ScreenSize.xy, true);
    o0.y = -o0.y;
  }
#endif
}