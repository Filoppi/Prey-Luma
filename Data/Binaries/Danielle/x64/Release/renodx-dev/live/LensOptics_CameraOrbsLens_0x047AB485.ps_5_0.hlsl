cbuffer PER_BATCH : register(b0)
{
  float4 lensDetailParams : packoffset(c0);
  float4 HDRParams : packoffset(c1);
}

#include "include/LensOptics.hlsl"

SamplerState orbMap_s : register(s0);
SamplerState lensMap_s : register(s2);
Texture2D<float4> orbMap : register(t0);
Texture2D<float4> lensMap : register(t2);

// CameraLensPS
// This one seems to draw some kind of vertical streak, but the intensity is always ~0 so it's not visible
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float4 v3 : COLOR0,
  out float4 outColor : SV_Target0)
{
  float4 r0,r1,r2;

  r0.z = 0;
  r0.x = ddx_coarse(v1.x);
  r0.y = ddy_coarse(v1.y);
  r1.xyzw = r0.xzzy * float4(3,3,3,3) + v1.xyxy;
  r0.xy = float2(3,3) * r0.xy;
  r0.z = orbMap.Sample(orbMap_s, r1.xy).x;
  r1.x = orbMap.Sample(orbMap_s, r1.zw).x;
  r1.y = orbMap.Sample(orbMap_s, v1.xy).x;
  r0.z = -r1.y + r0.z;
  r1.x = r1.x + -r1.y;
  r1.x = lensDetailParams.z * r1.x;
  r0.w = lensDetailParams.z * r0.z;
  r0.z = dot(r0.xw, r0.xw);
  r0.z = rsqrt(r0.z);
  r2.yz = r0.wx * r0.zz;
  r1.z = r0.y;
  r0.x = dot(r1.xz, r1.xz);
  r0.x = rsqrt(r0.x);
  r1.y = 0;
  r0.xyz = r1.xyz * r0.xxx;
  r2.x = 0;
  r1.xyz = r0.xyz * r2.xyz;
  r0.xyz = r0.zxy * r2.yzx + -r1.xyz;
  r0.w = -2 * r0.z;
  r0.xyz = r0.xyz * -r0.www + float3(0,0,-1);
  r0.x = dot(r0.xyz, v2.xyz);
  r0.x = saturate(-0.0500000007 + r0.x);
  r0.x = log2(r0.x);
  r0.x = 7 * r0.x;
  r0.xyz = exp2(r0.xxx);
  r0.w = 1;
  r0.xyzw = lensDetailParams.yyyy * r0.xyzw;
  r1.xyzw = lensMap.Sample(lensMap_s, v1.xy).xyzw;
  r0.xyzw = r1.xyzw * lensDetailParams.xxxx + r0.xyzw;
  r0.xyzw = v3.xyzw * r0.xyzw;
	outColor = ToneMappedPreMulAlpha(r0);
  return;
}