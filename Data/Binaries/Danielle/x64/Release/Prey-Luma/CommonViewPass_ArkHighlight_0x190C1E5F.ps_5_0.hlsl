
#include "include/Common.hlsl"

#define _RT_SAMPLE0 1
#define _RT_ALPHATEST 1
#define _RT_SCENE_SELECTION 1
#define _RT_NEAREST 0

cbuffer PER_INSTANCE : register(b1)
{
  float4 SceneSelection : packoffset(c0);
}

cbuffer PER_MATERIAL : register(b3)
{
  float4 CM_DetailTilingAndAlphaRef : packoffset(c4);
  float3 __0bendDetailFrequency__1bendDetailLeafAmplitude__2bendDetailBranchAmplitude__3 : packoffset(c15);
  float __0EmittanceMapGamma__1__2__3 : packoffset(c19);
  float2 __0__1SSSIndex__2__3 : packoffset(c25);
}

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState ssMaterialAnisoHigh_s : register(s0);
Texture2D<float4> diffuseTex : register(t0);
Texture2D<float4> sceneMaskDeviceTex : register(t26);

#define cmp -

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  nointerpolation float4 v4 : TEXCOORD3,
  uint v5 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
  v0.xy *= float2(BaseHorizontalResolution, BaseVerticalResolution) / (CV_ScreenSize.xy / CV_HPosScale.xy);
	v0.xy += LumaData.CameraJitters.xy * float2(0.5, -0.5) * (CV_ScreenSize.xy / CV_HPosScale.xy);
  
  float4 r0,r1;

  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r0.x = sceneMaskDeviceTex.Load(r0.xyz).x;
  r0.x = v0.z + -r0.x;
  r0.y = CV_LookingGlass_DepthScalar * SceneSelection.x;
  r0.x = r0.y * r0.x;
  r0.x = cmp(r0.x < 0);
  if (r0.x != 0) discard;
  r0.x = diffuseTex.Sample(ssMaterialAnisoHigh_s, v1.xy).w;
  r0.x = -CM_DetailTilingAndAlphaRef.z + r0.x;
  r0.x = cmp(r0.x < 0);
  if (r0.x != 0) discard;
  r0.x = dot(-v2.xyz, -v2.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = -v2.xyz * r0.xxx;
  r0.w = v5.x ? 1 : -1;
  r1.xyz = v3.xyz * r0.www;
  r0.x = saturate(dot(r0.xyz, r1.xyz));
  r0.x = 1 + -r0.x;
  r0.x = max(0.5, r0.x);
  r0.y = 0.349999994 * v0.y;
  r0.y = frac(r0.y);
  r0.y = r0.y * 2 + -1;
  r0.y = abs(r0.y) * 0.5 + 0.5;
  r0.yzw = v4.xyz * r0.yyy;
  r0.xyz = r0.yzw * r0.xxx;
  o0.xyz = v4.www * r0.xyz;
  o0.w = 1;
  o0 = SDRToHDR(o0);
#if !ENABLE_ARK_CUSTOM_POST_PROCESS
  o0 = 0;
#endif
  return;
}