#define _RT_SAMPLE0 1
#define _RT_SAMPLE5 1

cbuffer PER_BATCH : register(b0)
{
  float4 ScreenWidthHeight : packoffset(c0);
  float4 wposAndSize : packoffset(c1);
  float2 baseTexSize : packoffset(c2);
  row_major float3x4 xform : packoffset(c3);
  float4 ghostTileInfo : packoffset(c6);
  float4 dynamics : packoffset(c7);
}

#include "include/LensOptics.hlsl"

// lensGhostVS
// This one was already corrected by aspect ratio.
void main(
  float3 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Position0,
  out float2 o1 : TEXCOORD0,
  out float4 o2 : COLOR0)
{
  float4 r0,r1;

  r0.xy = v1.xy * float2(2,2) + float2(-1,-1);
  r0.xy = wposAndSize.ww * r0.xy;
  r0.yz = xform._m10_m11 * r0.yy;
  r0.xy = r0.xx * xform._m00_m01 + r0.yz;
  r0.xy = xform._m20_m21 + r0.xy;
  r0.z = max(ScreenWidthHeight.x, ScreenWidthHeight.y);
  r0.zw = ScreenWidthHeight.yx / r0.zz;
  r0.xy = r0.xy * r0.zw;
  r0.z = max(baseTexSize.x, baseTexSize.y);
  r0.zw = baseTexSize.xy / r0.zz;
  r1.xy = v0.xy * float2(1,-1) + float2(0,1);
  r1.xy = r1.xy + r1.xy;
  r0.xy = r0.xy * r0.zw + r1.xy;
  o0.xy = float2(-1,-1) + r0.xy;
  o0.zw = float2(0.5,1);
  o1.xy = v1.xy;
  r0.xyz = v2.zyx * v0.zzz;
  r0.w = v2.w;
  o2.xyzw = dynamics.xxxx * r0.xyzw;
  return;
}