#include "include/CBuffer_PerViewGlobal.hlsl"

#include "include/CBuffer_PerFrame.hlsl"

cbuffer CBShadowMask : register(b0)
{
  struct
  {
    row_major float4x4 unitMeshTransform;
    float4 lightVolumeSphereAdjust;
    row_major float4x4 lightShadowProj;
    float4 params;
    float3 vLightPos;
    float LG_SceneSelector;
  } cbShadowMaskConstants : packoffset(c0);
}

SamplerState ssShadowPointWrap_s : register(s7);
SamplerComparisonState ssShadowComparison_s : register(s3);
Texture2D<float4> sceneDepthTex : register(t0);
Texture2D<float4> ShadowMap : register(t1);
Texture2D<float4> shadowNoiseTex : register(t7);

#if 1 // LUMA FT: this fixes blocky shadow
// Increasing this decreases the amount of blockyness in the shadow (from the noise repeating blocky patters).
// The noise texture was 64x64 and a value of 64 looks good here, that might not be down to chance, but anyway the
// noise texels were way too big in screen space, especially for shadows close to the camera.
// With higher values (>16) the noise works as intended and adds noise but isn't directly visible.
static const float NoiseSizeDivisor = 64.0;
#else // Vanilla
static const float NoiseSizeDivisor = 1.0;
#endif
// Reducing this decreases the shadow attentuation, which might have been a bit too high in the Vanilla game, possibly to hide the noise,
// though overall it doesn't look that bad. 1 is Vanilla default.
static const float ShadowAttentuationDivisor = 1.0;

#define _RT_SCENE_SELECTION 0
#define _RT_SAMPLE2 0

// ShadowMaskGenPS
void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;

  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r0.z = sceneDepthTex.Load(r0.xyz).x;
  r1.xy = trunc(v0.xy);
  r0.xy = r1.xy * r0.zz;
  r1.x = dot(CV_ScreenToWorldBasis._m00_m01_m02, r0.xyz);
  r1.y = dot(CV_ScreenToWorldBasis._m10_m11_m12, r0.xyz);
  r1.z = dot(CV_ScreenToWorldBasis._m20_m21_m22, r0.xyz);
  r1.w = 1;
  r0.x = dot(cbShadowMaskConstants.lightShadowProj._m00_m01_m02_m03, r1.xyzw);
  r0.y = dot(cbShadowMaskConstants.lightShadowProj._m10_m11_m12_m13, r1.xyzw);
  r0.z = dot(cbShadowMaskConstants.lightShadowProj._m30_m31_m32_m33, r1.xyzw);
  r0.w = dot(cbShadowMaskConstants.lightShadowProj._m20_m21_m22_m23, r1.xyzw);
  r0.w = -cbShadowMaskConstants.params.w + r0.w;
  r1.xyzw = r0.xyxy / r0.zzzz;
  r0.xy = cbShadowMaskConstants.params.xx * NoiseSizeDivisor * r1.zw;
  r0.xy = float2(1000,1000) * r0.xy;
  r0.xy = shadowNoiseTex.Sample(ssShadowPointWrap_s, r0.xy).xy / ShadowAttentuationDivisor;
  r0.z = (1.0/512.0) * cbShadowMaskConstants.params.x;
  r2.xyz = r0.yxx * r0.zzz;
  r2.w = -r2.x;
  r3.xyzw = CF_irreg_kernel_2d[0].yyzz * r2.xzzw;
  r3.xyzw = r2.zwxz * CF_irreg_kernel_2d[0].xxww + r3.xyzw;
  r3.xyzw = r3.xyzw + r1.zwzw;
  r4.x = ShadowMap.SampleCmpLevelZero(ssShadowComparison_s, r3.xy, r0.w).x;
  r4.y = ShadowMap.SampleCmpLevelZero(ssShadowComparison_s, r3.zw, r0.w).x;
  r3.xyzw = CF_irreg_kernel_2d[1].yyzz * r2.xzzw;
  r3.xyzw = r2.zwxz * CF_irreg_kernel_2d[1].xxww + r3.xyzw;
  r3.xyzw = r3.xyzw + r1.zwzw;
  r4.z = ShadowMap.SampleCmpLevelZero(ssShadowComparison_s, r3.xy, r0.w).x;
  r4.w = ShadowMap.SampleCmpLevelZero(ssShadowComparison_s, r3.zw, r0.w).x;
  r0.x = dot(r4.xyzw, float4(0.0625,0.0625,0.0625,0.0625));
  r3.xyzw = CF_irreg_kernel_2d[2].yyzz * r2.xzzw;
  r3.xyzw = r2.zwxz * CF_irreg_kernel_2d[2].xxww + r3.xyzw;
  r3.xyzw = r3.xyzw + r1.zwzw;
  r4.x = ShadowMap.SampleCmpLevelZero(ssShadowComparison_s, r3.xy, r0.w).x;
  r4.y = ShadowMap.SampleCmpLevelZero(ssShadowComparison_s, r3.zw, r0.w).x;
  r3.xyzw = CF_irreg_kernel_2d[3].yyzz * r2.xzzw;
  r3.xyzw = r2.zwxz * CF_irreg_kernel_2d[3].xxww + r3.xyzw;
  r3.xyzw = r3.xyzw + r1.zwzw;
  r4.z = ShadowMap.SampleCmpLevelZero(ssShadowComparison_s, r3.xy, r0.w).x;
  r4.w = ShadowMap.SampleCmpLevelZero(ssShadowComparison_s, r3.zw, r0.w).x;
  r0.y = dot(r4.xyzw, float4(0.0625,0.0625,0.0625,0.0625));
  r0.x = r0.x + r0.y;
  r3.xyzw = CF_irreg_kernel_2d[4].yyzz * r2.xzzw;
  r3.xyzw = r2.zwxz * CF_irreg_kernel_2d[4].xxww + r3.xyzw;
  r3.xyzw = r3.xyzw + r1.zwzw;
  r4.x = ShadowMap.SampleCmpLevelZero(ssShadowComparison_s, r3.xy, r0.w).x;
  r4.y = ShadowMap.SampleCmpLevelZero(ssShadowComparison_s, r3.zw, r0.w).x;
  r3.xyzw = CF_irreg_kernel_2d[5].yyzz * r2.xzzw;
  r3.xyzw = r2.zwxz * CF_irreg_kernel_2d[5].xxww + r3.xyzw;
  r3.xyzw = r3.xyzw + r1.zwzw;
  r4.z = ShadowMap.SampleCmpLevelZero(ssShadowComparison_s, r3.xy, r0.w).x;
  r4.w = ShadowMap.SampleCmpLevelZero(ssShadowComparison_s, r3.zw, r0.w).x;
  r0.y = dot(r4.xyzw, float4(0.0625,0.0625,0.0625,0.0625));
  r0.x = r0.x + r0.y;
  r3.xyzw = CF_irreg_kernel_2d[6].yyzz * r2.xzzw;
  r3.xyzw = r2.zwxz * CF_irreg_kernel_2d[6].xxww + r3.xyzw;
  r3.xyzw = r3.xyzw + r1.zwzw;
  r4.x = ShadowMap.SampleCmpLevelZero(ssShadowComparison_s, r3.xy, r0.w).x;
  r4.y = ShadowMap.SampleCmpLevelZero(ssShadowComparison_s, r3.zw, r0.w).x;
  r3.xyzw = CF_irreg_kernel_2d[7].yyzz * r2.xyzw;
  r2.xyzw = r2.zwxz * CF_irreg_kernel_2d[7].xxww + r3.xyzw;
  r1.xyzw = r2.xyzw + r1.xyzw;
  r4.z = ShadowMap.SampleCmpLevelZero(ssShadowComparison_s, r1.xy, r0.w).x;
  r4.w = ShadowMap.SampleCmpLevelZero(ssShadowComparison_s, r1.zw, r0.w).x;
  r0.y = dot(r4.xyzw, float4(0.0625,0.0625,0.0625,0.0625));
  r0.x = r0.x + r0.y;
  o0.xyzw = float4(1,1,1,1) + -r0.xxxx;
#if 0 // LUMA FT: toggle shadow masks (these are point light, spot light and sun light shadow in screen space, similar to ambient occlusion)
  o0.xyzw = 0; // Only the red channel matters
#endif
  return;
}