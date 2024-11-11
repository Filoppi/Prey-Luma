#define _RT_SAMPLE0 1
#define _RT_SAMPLE5 1

cbuffer PER_BATCH : register(b0)
{
  float4 ScreenWidthHeight : packoffset(c0);
  float4 wposAndSize : packoffset(c1);
  float2 baseTexSize : packoffset(c2);
  row_major float3x4 xform : packoffset(c3);
}

#include "include/LensOptics.hlsl"

// shaftsOccVS
// These scale correctly by aspect ratio, but they break if changing the resolution/aspect ratio at runtime, because the engine forgets to update their intermediary texture.
void main(
  float3 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Position0,
  out float3 o1 : TEXCOORD0,
  out float2 o2 : TEXCOORD1,
  out float4 o3 : COLOR0)
{
  float4 r0,r1;

  r0.xy = v0.xy * float2(1,-1) + float2(0,1);
  r0.xy = r0.xy * float2(2,2) + float2(-1,-1);
  r0.z = dot(r0.xy, r0.xy);
  r0.z = rsqrt(r0.z);
  r0.zw = r0.xy * r0.zz;
  r1.xy = v1.yx * float2(2,2) + float2(-1,-1);
  r1.xy = wposAndSize.ww * r1.xy;
  r1.z = r1.x * r0.w;
  r1.z = r1.y * r0.z + -r1.z;
  r0.z = dot(r1.xy, r0.zw);
  r0.zw = xform._m10_m11 * r0.zz;
  r0.zw = r1.zz * xform._m00_m01 + r0.zw;
  r0.zw = xform._m20_m21 + r0.zw;
#if _RT_SAMPLE5
 	float2 aspectCorrectionRatio = (ScreenWidthHeight.xy / max(ScreenWidthHeight.x, ScreenWidthHeight.y)).yx;
  r0.zw *= aspectCorrectionRatio;
#endif // _RT_SAMPLE5
  r1.x = max(baseTexSize.x, baseTexSize.y);
  r1.xy = baseTexSize.xy / r1.xx;
  r0.xy = r0.zw * r1.xy + r0.xy;
  o0.xy = r0.xy;
  r0.xy = r0.xy * float2(0.5,0.5) + float2(0.5,0.5);
  o0.zw = float2(0.5,1);
  o1.xy = v1.xy;
  o1.z = v0.z;
  r0.z = 1 + -r0.y;
  o2.xy = r0.xz;
  o3.xyz = v2.zyx * v0.zzz;
  o3.w = v2.w;
  return;
}