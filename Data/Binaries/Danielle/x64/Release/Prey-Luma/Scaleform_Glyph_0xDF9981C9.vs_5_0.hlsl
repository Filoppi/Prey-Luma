#include "include/Common.hlsl"
#include "include/Scaleform.hlsl"
#include "include/LensDistortion.hlsl"

#include "include/CBuffer_PerViewGlobal.hlsl"

cbuffer PER_BATCH : register(b0)
{
  row_major float4x4 cCompositeMat : packoffset(c0);
}

// Match both "VS_Glyph" and "VS_GlyphStereoVideo" vertex shaders (they are identical once compiled, the second one probably wasn't used by Prey)
// This is the only shader that uses float as position input, yet it's not the only one to maps world space positions to the display (this is relevant for our lens distortion), I'm not sure how...
void main(
  float4 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Position0,
  out float2 o1 : TEXCOORD0,
  out float4 o2 : COLOR0)
{
  o0.x = dot(cCompositeMat._m00_m01_m02_m03, v0.xyzw);
  o0.y = dot(cCompositeMat._m10_m11_m12_m13, v0.xyzw);
  o0.z = dot(cCompositeMat._m20_m21_m22_m23, v0.xyzw);
  o0.w = dot(cCompositeMat._m30_m31_m32_m33, v0.xyzw);
  o1.xy = v1.xy;
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