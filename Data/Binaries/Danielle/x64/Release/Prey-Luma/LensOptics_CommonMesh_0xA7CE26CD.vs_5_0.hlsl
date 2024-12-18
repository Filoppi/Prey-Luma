#define _RT_SAMPLE0 1

cbuffer PER_BATCH : register(b0)
{
  float4 wposAndSize : packoffset(c0);
  row_major float3x4 xform : packoffset(c1);
  float4 externTint : packoffset(c4);
  float4 dynamics : packoffset(c5);
  float4 meshCenterAndBrt : packoffset(c6);
}

#include "include/LensOptics.hlsl"

// commonMeshVS
void main(
  float3 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Position0,
  out float4 o1 : TEXCOORD0,
  out float3 o2 : TEXCOORD1,
  out float4 o3 : COLOR0)
{
  float4 r0,r1;
  r0.xy = meshCenterAndBrt.xy * float2(2,2) + float2(-1,-1);
  r0.z = dot(r0.xy, r0.xy);
  r0.z = rsqrt(r0.z);
  r0.xy = r0.xy * r0.zz;
  r0.zw = wposAndSize.ww * v0.yx;
  r1.x = r0.z * r0.y;
  r1.x = r0.w * r0.x + -r1.x;
  r0.x = dot(r0.zw, r0.xy);
  r0.xy = xform._m10_m11 * r0.xx;
  r0.xy = r1.xx * xform._m00_m01 + r0.xy;
  r0.xy = xform._m20_m21 + r0.xy;
#if 1 // LUMA FT: added proper aspect ratio correction, these were rendering a lot bigger in ultrawide
  float screenAspectRatio = CV_ScreenSize.w / CV_ScreenSize.z;
  r0.x *= min(NativeAspectRatio / screenAspectRatio, 1.0);
#if CORRECT_SUNSHAFTS_FOV
  // Note: this is a bit approximate, as it's based on the overall view FOV and it doesn't do FOV ratio calculations in tangent space,
  // furthermore it only acknowledges the vertical FOV (which might be ok), but to do it properly we should probably find the FOV from the center of the screen at this vertices in NDC space,
  // and scale based on that, but it's really not worth bothering.
	float FOVX = 1.f / CV_ProjRatio.z;
  float tanHalfFOVX = tan( FOVX * 0.5f );
  float tanHalfFOVY = tanHalfFOVX / screenAspectRatio;
	float FOVY = atan( tanHalfFOVY ) * 2.0;
	float FOVCorrection = FOVY / NativeVerticalFOV;
  r0 /= FOVCorrection;
#endif // CORRECT_SUNSHAFTS_FOV
#endif
  r0.xy = r0.xy * float2(0.5,0.5) + meshCenterAndBrt.xy;
  r0.z = 1 + -r0.y;
  r0.xy = r0.xz * float2(2,2) + float2(-1,-1);
  o0.xy = r0.xy;
  o1.zw = r0.xy;
  o0.zw = float2(0,1);
  o1.xy = v1.xy;
  r0.xy = meshCenterAndBrt.xy * float2(1,-1) + float2(0,1);
  o2.xy = r0.xy * float2(2,2) + float2(-1,-1);
  o2.z = meshCenterAndBrt.z;
  r0.xyzw = meshCenterAndBrt.wwww * v2.zyxw;
  r0.xyzw = externTint.xyzw * r0.xyzw;
  o3.xyzw = dynamics.xxxx * r0.xyzw;
}