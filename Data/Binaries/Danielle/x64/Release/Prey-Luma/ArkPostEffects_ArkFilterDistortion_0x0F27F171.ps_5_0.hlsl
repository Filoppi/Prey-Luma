#include "include/Common.hlsl"

#define _RT_SAMPLE0 0
#define _RT_SAMPLE1 0
#define _RT_SAMPLE2 1

cbuffer PER_BATCH : register(b0)
{
  float4 psParams[16] : packoffset(c0);
}

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState ssScreenTex_s : register(s0);
Texture2D<float4> screenTex : register(t0);

// LUMA: Unchanged.
// Screen space distortion effect. This is already corrected by the aspect ratio and supports ultrawide fine:
// in UW, the distortion is focused around the 16:9 part of the image and it plays out identically there. 
// This runs after AA and upscaling.
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;

  //TODOFT: this (all the permutations?) stretches a little bit too much in ultrawide? test again with the first hand weapon you unlock (press Z to zoom) 
	float bulgeScreenAspect = psParams[1].z; // = ScreenAspect / BulgeAspect
  float screenAspectRatio = CV_ScreenSize.w / CV_ScreenSize.z;
  float bulgeAspect = screenAspectRatio / bulgeScreenAspect;
  //bulgeScreenAspect = NativeAspectRatio / bulgeAspect;
#if 1 // Identical but faster option (if we calculated "projectionMatrix" for any other reason), possibly more reliable
	row_major float4x4 projectionMatrix = mul( CV_ViewProjMatr, CV_InvViewMatr );
	float tanHalfFOVX = 1.f / projectionMatrix[0][0];
	float tanHalfFOVY = 1.f / projectionMatrix[1][1];
#else
	float FOVX = 1.f / CV_ProjRatio.z;
	float inverseAspectRatio = CV_ScreenSize.z / CV_ScreenSize.w; // Theoretically the projection matrix aspect ratio always matches the screen aspect ratio
	float tanHalfFOVX = tan( FOVX * 0.5f );
	float tanHalfFOVY = tanHalfFOVX * inverseAspectRatio;
#endif

  r0.x = psParams[1].y * psParams[1].y;
  r0.x = rcp(r0.x);
  r0.yz = v1.xy * float2(2,2) + float2(-1,-1);
  r1.yz = -psParams[0].zw + r0.yz;
  r1.x = bulgeScreenAspect * r1.y;
  r0.w = dot(r1.xz, r1.xz);
  r0.w = r0.w * r0.w;
  r0.x = r0.w * r0.x + -1;
  r0.w = 0.25 * abs(psParams[1].x);
  r0.x = saturate(r0.w * r0.x + 1);
  r0.xy = r1.yz * r0.xx + -r0.yz;
  r0.xy = v1.xy + r0.xy;
  o0.xyzw = screenTex.Sample(ssScreenTex_s, r0.xy).xyzw;
  return;
}