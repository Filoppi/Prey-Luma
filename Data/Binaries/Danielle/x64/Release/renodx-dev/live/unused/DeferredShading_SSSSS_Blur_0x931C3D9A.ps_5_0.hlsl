// ---- Created with 3Dmigoto v1.3.16 on Thu Jun 27 00:11:49 2024

cbuffer PER_BATCH : register(b0)
{
  float4 SSSBlurDir : packoffset(c0);
  float4 ViewSpaceParams : packoffset(c1);
}

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState _tex0_s : register(s0);
SamplerState _tex1_s : register(s1);
SamplerState _tex3_s : register(s3);
Texture2D<float4> _tex0 : register(t0);
Texture2D<float4> _tex1 : register(t1);
Texture2D<float4> _tex3 : register(t3);

// 3Dmigoto declarations
#define cmp -

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  const float4 icb[] = { { 3.300000, 2.800000, 1.400000, 0},
                              { 3.300000, 1.400000, 1.100000, 0},
                              { 1.000000, 1.000000, 1.000000, 0},
                              { 1.000000, 1.000000, 1.000000, 0},
                              { 0.030000, 0.030000, 0.080000, 8.000000},
                              { 0.015000, 0.020000, 0.025000, 1.000000},
                              { 0.100000, 0.100000, 0.100000, 10.000000},
                              { 0.100000, 0.100000, 0.100000, 10.000000} };
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = _tex3.Sample(_tex3_s, v1.xy).w;
  r0.y = cmp(r0.x == 0.000000);
  if (r0.y != 0) discard;
  r0.x = 3.99609375 * r0.x;
  r0.y = floor(r0.x);
  r0.x = frac(r0.x);
  r0.y = (uint)r0.y;
  r1.xyz = icb[r0.y+0].xyz + icb[r0.y+0].xyz;
  r1.xyz = float3(-1,-1,-1) / r1.xyz;
  r0.y = icb[r0.y+4].w * 0.00549999997;
  r0.x = r0.y * r0.x;
  r0.y = _tex1.Sample(_tex1_s, v1.xy).x;
  r2.xyz = _tex0.Sample(_tex0_s, v1.xy).xyz;
  r3.xy = v1.xy * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r3.z = 1;
  r3.xyz = r3.xyz * r0.yyy;
  r3.xyz = CV_NearFarClipDist.yyy * r3.xyz;
  r4.xyz = ddy_fine(r3.zxy);
  r5.xyz = ddx_fine(r3.yzx);
  r6.xyz = r5.xyz * r4.xyz;
  r4.xyz = r4.zxy * r5.yzx + -r6.xyz;
  r0.z = cmp(0.00100000005 < SSSBlurDir.x);
  r0.w = dot(r4.xz, r4.xz);
  r0.w = rsqrt(r0.w);
  r4.xw = r4.xz * r0.ww;
  r0.w = dot(-r3.xz, -r3.xz);
  r0.w = rsqrt(r0.w);
  r3.xw = -r3.xz * r0.ww;
  r0.w = dot(r4.xw, r3.xw);
  r1.w = dot(r4.yz, r4.yz);
  r1.w = rsqrt(r1.w);
  r3.xw = r4.yz * r1.ww;
  r1.w = dot(-r3.yz, -r3.yz);
  r1.w = rsqrt(r1.w);
  r3.yz = -r3.yz * r1.ww;
  r1.w = dot(r3.xw, r3.yz);
  r0.z = r0.z ? r0.w : r1.w;
  r0.z = max(0.300000012, r0.z);
  r3.xyzw = SSSBlurDir.xyxy * r0.zzzz;
  r3.xyzw = r3.xyzw * r0.xxxx;
  r0.x = CV_NearFarClipDist.y * r0.y;
  r3.xyzw = r3.xyzw / r0.xxxx;
  r4.xyzw = r3.zwzw * float4(0.064000003,0.064000003,0.130727276,0.130727276) + v1.xyxy;
  r4.xyzw = max(float4(0,0,0,0), r4.xyzw);
  r4.xyzw = min(CV_HPosClamp.xyxy, r4.xyzw);
  r0.x = _tex1.SampleLevel(_tex1_s, r4.xy, 0).x;
  r5.xyz = _tex0.SampleLevel(_tex0_s, r4.xy, 0).xyz;
  r0.x = r0.x + -r0.y;
  r0.x = CV_NearFarClipDist.y * r0.x;
  r0.x = 1000 * r0.x;
  r0.x = r0.x * r0.x + 0.123903997;
  r0.xzw = r0.xxx * r1.xyz;
  r0.xzw = float3(1.44269502,1.44269502,1.44269502) * r0.xzw;
  r0.xzw = exp2(r0.xzw);
  r6.xyz = float3(1,1,1) + r0.xzw;
  r0.xzw = r0.xzw * r5.xyz + r2.xyz;
  r1.w = _tex1.SampleLevel(_tex1_s, r4.zw, 0).x;
  r2.xyz = _tex0.SampleLevel(_tex0_s, r4.zw, 0).xyz;
  r1.w = r1.w + -r0.y;
  r1.w = CV_NearFarClipDist.y * r1.w;
  r1.w = 1000 * r1.w;
  r1.w = r1.w * r1.w + 0.516960979;
  r4.xyz = r1.www * r1.xyz;
  r4.xyz = float3(1.44269502,1.44269502,1.44269502) * r4.xyz;
  r4.xyz = exp2(r4.xyz);
  r5.xyz = r6.xyz + r4.xyz;
  r0.xzw = r4.xyz * r2.xyz + r0.xzw;
  r2.xyzw = r3.zwzw * float4(0.203090906,0.203090906,0.287090898,0.287090898) + v1.xyxy;
  r2.xyzw = max(float4(0,0,0,0), r2.xyzw);
  r2.xyzw = min(CV_HPosClamp.xyxy, r2.xyzw);
  r1.w = _tex1.SampleLevel(_tex1_s, r2.xy, 0).x;
  r4.xyz = _tex0.SampleLevel(_tex0_s, r2.xy, 0).xyz;
  r1.w = r1.w + -r0.y;
  r1.w = CV_NearFarClipDist.y * r1.w;
  r1.w = 1000 * r1.w;
  r1.w = r1.w * r1.w + 1.24768901;
  r6.xyz = r1.www * r1.xyz;
  r6.xyz = float3(1.44269502,1.44269502,1.44269502) * r6.xyz;
  r6.xyz = exp2(r6.xyz);
  r5.xyz = r6.xyz + r5.xyz;
  r0.xzw = r6.xyz * r4.xyz + r0.xzw;
  r1.w = _tex1.SampleLevel(_tex1_s, r2.zw, 0).x;
  r2.xyz = _tex0.SampleLevel(_tex0_s, r2.zw, 0).xyz;
  r1.w = r1.w + -r0.y;
  r1.w = CV_NearFarClipDist.y * r1.w;
  r1.w = 1000 * r1.w;
  r1.w = r1.w * r1.w + 2.49324107;
  r4.xyz = r1.www * r1.xyz;
  r4.xyz = float3(1.44269502,1.44269502,1.44269502) * r4.xyz;
  r4.xyz = exp2(r4.xyz);
  r5.xyz = r5.xyz + r4.xyz;
  r0.xzw = r4.xyz * r2.xyz + r0.xzw;
  r2.xyzw = r3.zwzw * float4(0.395818204,0.395818204,0.584181845,0.584181845) + v1.xyxy;
  r2.xyzw = max(float4(0,0,0,0), r2.xyzw);
  r2.xyzw = min(CV_HPosClamp.xyxy, r2.xyzw);
  r1.w = _tex1.SampleLevel(_tex1_s, r2.xy, 0).x;
  r4.xyz = _tex0.SampleLevel(_tex0_s, r2.xy, 0).xyz;
  r1.w = r1.w + -r0.y;
  r1.w = CV_NearFarClipDist.y * r1.w;
  r1.w = 1000 * r1.w;
  r1.w = r1.w * r1.w + 4.73932934;
  r6.xyz = r1.www * r1.xyz;
  r6.xyz = float3(1.44269502,1.44269502,1.44269502) * r6.xyz;
  r6.xyz = exp2(r6.xyz);
  r5.xyz = r6.xyz + r5.xyz;
  r0.xzw = r6.xyz * r4.xyz + r0.xzw;
  r1.w = _tex1.SampleLevel(_tex1_s, r2.zw, 0).x;
  r2.xyz = _tex0.SampleLevel(_tex0_s, r2.zw, 0).xyz;
  r1.w = r1.w + -r0.y;
  r1.w = CV_NearFarClipDist.y * r1.w;
  r1.w = 1000 * r1.w;
  r1.w = r1.w * r1.w + 10.323369;
  r4.xyz = r1.www * r1.xyz;
  r4.xyz = float3(1.44269502,1.44269502,1.44269502) * r4.xyz;
  r4.xyz = exp2(r4.xyz);
  r5.xyz = r5.xyz + r4.xyz;
  r0.xzw = r4.xyz * r2.xyz + r0.xzw;
  r2.xyzw = r3.zwzw * float4(-0.064000003,-0.064000003,-0.130727276,-0.130727276) + v1.xyxy;
  r2.xyzw = max(float4(0,0,0,0), r2.xyzw);
  r2.xyzw = min(CV_HPosClamp.xyxy, r2.xyzw);
  r1.w = _tex1.SampleLevel(_tex1_s, r2.xy, 0).x;
  r4.xyz = _tex0.SampleLevel(_tex0_s, r2.xy, 0).xyz;
  r1.w = r1.w + -r0.y;
  r1.w = CV_NearFarClipDist.y * r1.w;
  r1.w = 1000 * r1.w;
  r1.w = r1.w * r1.w + 0.123903997;
  r6.xyz = r1.www * r1.xyz;
  r6.xyz = float3(1.44269502,1.44269502,1.44269502) * r6.xyz;
  r6.xyz = exp2(r6.xyz);
  r5.xyz = r6.xyz + r5.xyz;
  r0.xzw = r6.xyz * r4.xyz + r0.xzw;
  r1.w = _tex1.SampleLevel(_tex1_s, r2.zw, 0).x;
  r2.xyz = _tex0.SampleLevel(_tex0_s, r2.zw, 0).xyz;
  r1.w = r1.w + -r0.y;
  r1.w = CV_NearFarClipDist.y * r1.w;
  r1.w = 1000 * r1.w;
  r1.w = r1.w * r1.w + 0.516960979;
  r4.xyz = r1.www * r1.xyz;
  r4.xyz = float3(1.44269502,1.44269502,1.44269502) * r4.xyz;
  r4.xyz = exp2(r4.xyz);
  r5.xyz = r5.xyz + r4.xyz;
  r0.xzw = r4.xyz * r2.xyz + r0.xzw;
  r2.xyzw = r3.zwzw * float4(-0.203090906,-0.203090906,-0.287090898,-0.287090898) + v1.xyxy;
  r2.xyzw = max(float4(0,0,0,0), r2.xyzw);
  r2.xyzw = min(CV_HPosClamp.xyxy, r2.xyzw);
  r1.w = _tex1.SampleLevel(_tex1_s, r2.xy, 0).x;
  r4.xyz = _tex0.SampleLevel(_tex0_s, r2.xy, 0).xyz;
  r1.w = r1.w + -r0.y;
  r1.w = CV_NearFarClipDist.y * r1.w;
  r1.w = 1000 * r1.w;
  r1.w = r1.w * r1.w + 1.24768901;
  r6.xyz = r1.www * r1.xyz;
  r6.xyz = float3(1.44269502,1.44269502,1.44269502) * r6.xyz;
  r6.xyz = exp2(r6.xyz);
  r5.xyz = r6.xyz + r5.xyz;
  r0.xzw = r6.xyz * r4.xyz + r0.xzw;
  r1.w = _tex1.SampleLevel(_tex1_s, r2.zw, 0).x;
  r2.xyz = _tex0.SampleLevel(_tex0_s, r2.zw, 0).xyz;
  r1.w = r1.w + -r0.y;
  r1.w = CV_NearFarClipDist.y * r1.w;
  r1.w = 1000 * r1.w;
  r1.w = r1.w * r1.w + 2.49324107;
  r4.xyz = r1.www * r1.xyz;
  r4.xyz = float3(1.44269502,1.44269502,1.44269502) * r4.xyz;
  r4.xyz = exp2(r4.xyz);
  r5.xyz = r5.xyz + r4.xyz;
  r0.xzw = r4.xyz * r2.xyz + r0.xzw;
  r2.xyzw = r3.xyzw * float4(-0.395818204,-0.395818204,-0.584181845,-0.584181845) + v1.xyxy;
  r2.xyzw = max(float4(0,0,0,0), r2.xyzw);
  r2.xyzw = min(CV_HPosClamp.xyxy, r2.xyzw);
  r1.w = _tex1.SampleLevel(_tex1_s, r2.xy, 0).x;
  r3.xyz = _tex0.SampleLevel(_tex0_s, r2.xy, 0).xyz;
  r1.w = r1.w + -r0.y;
  r1.w = CV_NearFarClipDist.y * r1.w;
  r1.w = 1000 * r1.w;
  r1.w = r1.w * r1.w + 4.73932934;
  r4.xyz = r1.www * r1.xyz;
  r4.xyz = float3(1.44269502,1.44269502,1.44269502) * r4.xyz;
  r4.xyz = exp2(r4.xyz);
  r5.xyz = r5.xyz + r4.xyz;
  r0.xzw = r4.xyz * r3.xyz + r0.xzw;
  r1.w = _tex1.SampleLevel(_tex1_s, r2.zw, 0).x;
  r2.xyz = _tex0.SampleLevel(_tex0_s, r2.zw, 0).xyz;
  r0.y = r1.w + -r0.y;
  r0.y = CV_NearFarClipDist.y * r0.y;
  r0.y = 1000 * r0.y;
  r0.y = r0.y * r0.y + 10.323369;
  r1.xyz = r0.yyy * r1.xyz;
  r1.xyz = float3(1.44269502,1.44269502,1.44269502) * r1.xyz;
  r1.xyz = exp2(r1.xyz);
  r3.xyz = r5.xyz + r1.xyz;
  r0.xyz = r1.xyz * r2.xyz + r0.xzw;
  o0.xyz = r0.xyz / r3.xyz;
  o0.w = 0;
  return;
}