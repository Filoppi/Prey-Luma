cbuffer PER_BATCH : register(b0)
{
  float4 lightColorInfo : packoffset(c0);
  row_major float3x4 xform : packoffset(c1);
  float4 dynamics : packoffset(c4);
  float3 lightProjPos : packoffset(c5);
}

// Unused. Needed to compile LensOptics.hlsl.
#define HDRParams float3(24.8, 0.5, 0.06)

#include "include/LensOptics.hlsl"

// CameraOrbsVS
void main(
  float3 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Position0,
  out float2 o1 : TEXCOORD0,
  out float3 o2 : TEXCOORD1,
  out float4 o3 : COLOR0)
{
  float4 r0,r1;

  r0.xy = v0.xy;
  r0.z = 1;
  r1.x = dot(xform._m00_m01_m02, r0.xyz);
  r1.y = dot(xform._m10_m11_m12, r0.xyz);
  o0.xy = r1.xy;
  o0.zw = float2(0,1);
  o1.xy = v1.xy;
  r0.x = 1 + -lightProjPos.y;
  r0.y = r0.x + r0.x;
  r0.x = lightProjPos.x + lightProjPos.x;
  r0.xy = float2(-1,-1) + r0.xy;
  r0.xy = r0.xy + -r1.xy;
  r0.z = 1;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  o2.xyz = r0.xyz * r0.www;
  r0.x = dot(r0.xy, r0.xy);
  r0.x = r0.x / lightColorInfo.w;
  r0.x = -10 * r0.x;
  r0.x = exp2(r0.x);
  r0.xyz = lightColorInfo.xyz * r0.xxx;
  r0.w = 1;
  r0.xyzw = v2.zyxw * r0.xyzw;
  o3.xyzw = dynamics.xxxx * r0.xyzw;
  return;
}