// ---- Created with 3Dmigoto v1.3.16 on Thu Jun 27 00:11:46 2024

cbuffer PER_BATCH : register(b0)
{
  float4 SSSBlurDir : packoffset(c0);
  float4 ViewSpaceParams : packoffset(c1);
}

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState _tex0_s : register(s0);
SamplerState _tex1_s : register(s1);
SamplerState _tex2_s : register(s2);
SamplerState _tex3_s : register(s3);
SamplerState _tex4_s : register(s4);
SamplerState _tex5_s : register(s5);
Texture2D<float4> _tex0 : register(t0);
Texture2D<float4> _tex1 : register(t1);
Texture2D<float4> _tex2 : register(t2);
Texture2D<float4> _tex3 : register(t3);
Texture2D<float4> _tex4 : register(t4);
Texture2D<float4> _tex5 : register(t5);

// 3Dmigoto declarations
#define cmp -

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  const float4 icb[] = { { 0.030000, 0.030000, 0.080000, 8.000000},
                              { 0.015000, 0.020000, 0.025000, 1.000000},
                              { 0.100000, 0.100000, 0.100000, 10.000000},
                              { 0.100000, 0.100000, 0.100000, 10.000000},
                              { 3.300000, 2.800000, 1.400000, 0},
                              { 3.300000, 1.400000, 1.100000, 0},
                              { 1.000000, 1.000000, 1.000000, 0},
                              { 1.000000, 1.000000, 1.000000, 0} };
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = _tex3.Sample(_tex3_s, v1.xy).xyzw;
  r1.x = cmp(r0.w == 0.000000);
  if (r1.x != 0) discard;
  r1.x = _tex2.Sample(_tex2_s, v1.xy).w;
  r2.xzw = _tex4.Sample(_tex4_s, v1.xy).yzw;
  r1.x = 3.99609375 * r1.x;
  r1.x = floor(r1.x);
  r1.x = (int)r1.x;
  r0.xyz = r0.xyz * r0.xyz;
  r1.yz = r2.zw * float2(2.00787401,2.00787401) + float2(-1,-1);
  r2.yz = r1.xx ? float2(0,0) : r1.yz;
  r1.y = dot(float3(1,-0.344099998,-0.714100003), r2.xyz);
  r1.xz = r2.zy * float2(1.40199995,1.77199996) + r2.xx;
  r1.xyz = r1.xyz * r1.xyz;
  r0.w = 3.99609375 * r0.w;
  r1.w = floor(r0.w);
  r0.w = frac(r0.w);
  r2.x = (uint)r1.w;
  r2.y = 1 + -r0.w;
  r2.yzw = icb[r2.x+0].xyz * r2.yyy;
  r2.yzw = float3(10,10,10) * r2.yzw;
  r3.xyz = icb[r2.x+4].xyz + icb[r2.x+4].xyz;
  r3.xyz = float3(-1,-1,-1) / r3.xyz;
  r2.x = icb[r2.x+0].w * 0.00549999997;
  r0.w = r2.x * r0.w;
  r2.x = _tex1.Sample(_tex1_s, v1.xy).x;
  r4.xyz = _tex0.Sample(_tex0_s, v1.xy).xyz;
  r5.xy = v1.xy * ViewSpaceParams.xy + ViewSpaceParams.zw;
  r5.z = 1;
  r5.xyz = r5.xyz * r2.xxx;
  r5.xyz = CV_NearFarClipDist.yyy * r5.xyz;
  r6.xyz = ddy_fine(r5.zxy);
  r7.xyz = ddx_fine(r5.yzx);
  r8.xyz = r7.xyz * r6.xyz;
  r6.xyz = r6.zxy * r7.yzx + -r8.xyz;
  r3.w = cmp(0.00100000005 < SSSBlurDir.x);
  r4.w = dot(r6.xz, r6.xz);
  r4.w = rsqrt(r4.w);
  r6.xw = r6.xz * r4.ww;
  r4.w = dot(-r5.xz, -r5.xz);
  r4.w = rsqrt(r4.w);
  r5.xw = -r5.xz * r4.ww;
  r4.w = dot(r6.xw, r5.xw);
  r5.x = dot(r6.yz, r6.yz);
  r5.x = rsqrt(r5.x);
  r5.xw = r6.yz * r5.xx;
  r6.x = dot(-r5.yz, -r5.yz);
  r6.x = rsqrt(r6.x);
  r5.yz = r6.xx * -r5.yz;
  r5.x = dot(r5.xw, r5.yz);
  r3.w = r3.w ? r4.w : r5.x;
  r3.w = max(0.300000012, r3.w);
  r5.xyzw = SSSBlurDir.xyxy * r3.wwww;
  r5.xyzw = r5.xyzw * r0.wwww;
  r0.w = CV_NearFarClipDist.y * r2.x;
  r5.xyzw = r5.xyzw / r0.wwww;
  r6.xyzw = r5.zwzw * float4(0.064000003,0.064000003,0.130727276,0.130727276) + v1.xyxy;
  r6.xyzw = max(float4(0,0,0,0), r6.xyzw);
  r6.xyzw = min(CV_HPosClamp.xyxy, r6.xyzw);
  r0.w = _tex1.SampleLevel(_tex1_s, r6.xy, 0).x;
  r7.xyz = _tex0.SampleLevel(_tex0_s, r6.xy, 0).xyz;
  r0.w = r0.w + -r2.x;
  r0.w = CV_NearFarClipDist.y * r0.w;
  r0.w = 1000 * r0.w;
  r0.w = r0.w * r0.w + 0.123903997;
  r8.xyz = r0.www * r3.xyz;
  r8.xyz = float3(1.44269502,1.44269502,1.44269502) * r8.xyz;
  r8.xyz = exp2(r8.xyz);
  r9.xyz = float3(1,1,1) + r8.xyz;
  r4.xyz = r8.xyz * r7.xyz + r4.xyz;
  r0.w = _tex1.SampleLevel(_tex1_s, r6.zw, 0).x;
  r6.xyz = _tex0.SampleLevel(_tex0_s, r6.zw, 0).xyz;
  r0.w = r0.w + -r2.x;
  r0.w = CV_NearFarClipDist.y * r0.w;
  r0.w = 1000 * r0.w;
  r0.w = r0.w * r0.w + 0.516960979;
  r7.xyz = r0.www * r3.xyz;
  r7.xyz = float3(1.44269502,1.44269502,1.44269502) * r7.xyz;
  r7.xyz = exp2(r7.xyz);
  r8.xyz = r9.xyz + r7.xyz;
  r4.xyz = r7.xyz * r6.xyz + r4.xyz;
  r6.xyzw = r5.zwzw * float4(0.203090906,0.203090906,0.287090898,0.287090898) + v1.xyxy;
  r6.xyzw = max(float4(0,0,0,0), r6.xyzw);
  r6.xyzw = min(CV_HPosClamp.xyxy, r6.xyzw);
  r0.w = _tex1.SampleLevel(_tex1_s, r6.xy, 0).x;
  r7.xyz = _tex0.SampleLevel(_tex0_s, r6.xy, 0).xyz;
  r0.w = r0.w + -r2.x;
  r0.w = CV_NearFarClipDist.y * r0.w;
  r0.w = 1000 * r0.w;
  r0.w = r0.w * r0.w + 1.24768901;
  r9.xyz = r0.www * r3.xyz;
  r9.xyz = float3(1.44269502,1.44269502,1.44269502) * r9.xyz;
  r9.xyz = exp2(r9.xyz);
  r8.xyz = r9.xyz + r8.xyz;
  r4.xyz = r9.xyz * r7.xyz + r4.xyz;
  r0.w = _tex1.SampleLevel(_tex1_s, r6.zw, 0).x;
  r6.xyz = _tex0.SampleLevel(_tex0_s, r6.zw, 0).xyz;
  r0.w = r0.w + -r2.x;
  r0.w = CV_NearFarClipDist.y * r0.w;
  r0.w = 1000 * r0.w;
  r0.w = r0.w * r0.w + 2.49324107;
  r7.xyz = r0.www * r3.xyz;
  r7.xyz = float3(1.44269502,1.44269502,1.44269502) * r7.xyz;
  r7.xyz = exp2(r7.xyz);
  r8.xyz = r8.xyz + r7.xyz;
  r4.xyz = r7.xyz * r6.xyz + r4.xyz;
  r6.xyzw = r5.zwzw * float4(0.395818204,0.395818204,0.584181845,0.584181845) + v1.xyxy;
  r6.xyzw = max(float4(0,0,0,0), r6.xyzw);
  r6.xyzw = min(CV_HPosClamp.xyxy, r6.xyzw);
  r0.w = _tex1.SampleLevel(_tex1_s, r6.xy, 0).x;
  r7.xyz = _tex0.SampleLevel(_tex0_s, r6.xy, 0).xyz;
  r0.w = r0.w + -r2.x;
  r0.w = CV_NearFarClipDist.y * r0.w;
  r0.w = 1000 * r0.w;
  r0.w = r0.w * r0.w + 4.73932934;
  r9.xyz = r0.www * r3.xyz;
  r9.xyz = float3(1.44269502,1.44269502,1.44269502) * r9.xyz;
  r9.xyz = exp2(r9.xyz);
  r8.xyz = r9.xyz + r8.xyz;
  r4.xyz = r9.xyz * r7.xyz + r4.xyz;
  r0.w = _tex1.SampleLevel(_tex1_s, r6.zw, 0).x;
  r6.xyz = _tex0.SampleLevel(_tex0_s, r6.zw, 0).xyz;
  r0.w = r0.w + -r2.x;
  r0.w = CV_NearFarClipDist.y * r0.w;
  r0.w = 1000 * r0.w;
  r0.w = r0.w * r0.w + 10.323369;
  r7.xyz = r0.www * r3.xyz;
  r7.xyz = float3(1.44269502,1.44269502,1.44269502) * r7.xyz;
  r7.xyz = exp2(r7.xyz);
  r8.xyz = r8.xyz + r7.xyz;
  r4.xyz = r7.xyz * r6.xyz + r4.xyz;
  r6.xyzw = r5.zwzw * float4(-0.064000003,-0.064000003,-0.130727276,-0.130727276) + v1.xyxy;
  r6.xyzw = max(float4(0,0,0,0), r6.xyzw);
  r6.xyzw = min(CV_HPosClamp.xyxy, r6.xyzw);
  r0.w = _tex1.SampleLevel(_tex1_s, r6.xy, 0).x;
  r7.xyz = _tex0.SampleLevel(_tex0_s, r6.xy, 0).xyz;
  r0.w = r0.w + -r2.x;
  r0.w = CV_NearFarClipDist.y * r0.w;
  r0.w = 1000 * r0.w;
  r0.w = r0.w * r0.w + 0.123903997;
  r9.xyz = r0.www * r3.xyz;
  r9.xyz = float3(1.44269502,1.44269502,1.44269502) * r9.xyz;
  r9.xyz = exp2(r9.xyz);
  r8.xyz = r9.xyz + r8.xyz;
  r4.xyz = r9.xyz * r7.xyz + r4.xyz;
  r0.w = _tex1.SampleLevel(_tex1_s, r6.zw, 0).x;
  r6.xyz = _tex0.SampleLevel(_tex0_s, r6.zw, 0).xyz;
  r0.w = r0.w + -r2.x;
  r0.w = CV_NearFarClipDist.y * r0.w;
  r0.w = 1000 * r0.w;
  r0.w = r0.w * r0.w + 0.516960979;
  r7.xyz = r0.www * r3.xyz;
  r7.xyz = float3(1.44269502,1.44269502,1.44269502) * r7.xyz;
  r7.xyz = exp2(r7.xyz);
  r8.xyz = r8.xyz + r7.xyz;
  r4.xyz = r7.xyz * r6.xyz + r4.xyz;
  r6.xyzw = r5.zwzw * float4(-0.203090906,-0.203090906,-0.287090898,-0.287090898) + v1.xyxy;
  r6.xyzw = max(float4(0,0,0,0), r6.xyzw);
  r6.xyzw = min(CV_HPosClamp.xyxy, r6.xyzw);
  r0.w = _tex1.SampleLevel(_tex1_s, r6.xy, 0).x;
  r7.xyz = _tex0.SampleLevel(_tex0_s, r6.xy, 0).xyz;
  r0.w = r0.w + -r2.x;
  r0.w = CV_NearFarClipDist.y * r0.w;
  r0.w = 1000 * r0.w;
  r0.w = r0.w * r0.w + 1.24768901;
  r9.xyz = r0.www * r3.xyz;
  r9.xyz = float3(1.44269502,1.44269502,1.44269502) * r9.xyz;
  r9.xyz = exp2(r9.xyz);
  r8.xyz = r9.xyz + r8.xyz;
  r4.xyz = r9.xyz * r7.xyz + r4.xyz;
  r0.w = _tex1.SampleLevel(_tex1_s, r6.zw, 0).x;
  r6.xyz = _tex0.SampleLevel(_tex0_s, r6.zw, 0).xyz;
  r0.w = r0.w + -r2.x;
  r0.w = CV_NearFarClipDist.y * r0.w;
  r0.w = 1000 * r0.w;
  r0.w = r0.w * r0.w + 2.49324107;
  r7.xyz = r0.www * r3.xyz;
  r7.xyz = float3(1.44269502,1.44269502,1.44269502) * r7.xyz;
  r7.xyz = exp2(r7.xyz);
  r8.xyz = r8.xyz + r7.xyz;
  r4.xyz = r7.xyz * r6.xyz + r4.xyz;
  r5.xyzw = r5.xyzw * float4(-0.395818204,-0.395818204,-0.584181845,-0.584181845) + v1.xyxy;
  r5.xyzw = max(float4(0,0,0,0), r5.xyzw);
  r5.xyzw = min(CV_HPosClamp.xyxy, r5.xyzw);
  r0.w = _tex1.SampleLevel(_tex1_s, r5.xy, 0).x;
  r6.xyz = _tex0.SampleLevel(_tex0_s, r5.xy, 0).xyz;
  r0.w = r0.w + -r2.x;
  r0.w = CV_NearFarClipDist.y * r0.w;
  r0.w = 1000 * r0.w;
  r0.w = r0.w * r0.w + 4.73932934;
  r7.xyz = r0.www * r3.xyz;
  r7.xyz = float3(1.44269502,1.44269502,1.44269502) * r7.xyz;
  r7.xyz = exp2(r7.xyz);
  r8.xyz = r8.xyz + r7.xyz;
  r4.xyz = r7.xyz * r6.xyz + r4.xyz;
  r0.w = _tex1.SampleLevel(_tex1_s, r5.zw, 0).x;
  r5.xyz = _tex0.SampleLevel(_tex0_s, r5.zw, 0).xyz;
  r0.w = r0.w + -r2.x;
  r0.w = CV_NearFarClipDist.y * r0.w;
  r0.w = 1000 * r0.w;
  r0.w = r0.w * r0.w + 10.323369;
  r3.xyz = r0.www * r3.xyz;
  r3.xyz = float3(1.44269502,1.44269502,1.44269502) * r3.xyz;
  r3.xyz = exp2(r3.xyz);
  r6.xyz = r8.xyz + r3.xyz;
  r3.xyz = r3.xyz * r5.xyz + r4.xyz;
  r3.xyz = r3.xyz / r6.xyz;
  r4.xyz = _tex5.Sample(_tex5_s, v1.xy).xyz;
  r4.xyz = r4.xyz + -r3.xyz;
  r2.xyz = r2.yzw * r4.xyz + r3.xyz;
  r0.w = dot(r1.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r0.w = 1 + -r0.w;
  r0.w = max(0, r0.w);
  r0.xyz = r0.xyz * r0.www;
  r0.w = cmp(r1.w != 1.000000);
  r1.xyz = sqrt(r0.xyz);
  r0.xyz = r0.www ? r1.xyz : r0.xyz;
  o0.xyz = r2.xyz * r0.xyz;
  o0.w = 0;
  return;
}