cbuffer CBSSRRaytrace : register(b0)
{
  struct
  {
    row_major float4x4 mViewProj;
    row_major float4x4 mViewProjPrev;
    float2 screenScalePrev;
    float2 screenScalePrevClamp;
  } cbRefl : packoffset(c0);
}

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState ssReflectionPoint_s : register(s0);
SamplerState ssReflectionLinear_s : register(s1);
SamplerState ssReflectionLinearBorder_s : register(s2);
Texture2D<float> reflectionDepthTex : register(t0); // Device depth
Texture2D<float4> reflectionNormalsTex : register(t1);
Texture2D<float4> reflectionSpecularTex : register(t2);
Texture2D<float4> reflectionDepthScaledTex : register(t3); // Half res 4 channel depth (each channel is slightly different)
Texture2D<float4> reflectionPreviousSceneTex : register(t4);
Texture2D<float2> reflectionLuminanceTex : register(t5);

#define cmp -

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7;

  r0.xy = CV_ScreenSize.zw + v1.xy; // LUMA FT: unclear why this is done, it seems a bit wrong
	// LUMA FT: the uv doesn't need to be scaled by "CV_HPosScale.xy" here (it already is in the vertex shader)
	// LUMA FT: fixed missing clamps to "CV_HPosClamp.xy"
	v1.xy = min(v1.xy, CV_HPosClamp.xy);
	r0.xy = min(r0.xy, CV_HPosClamp.xy);
  r1.z = reflectionDepthTex.SampleLevel(ssReflectionPoint_s, v1.xy, 0).x;
  r2.xyz = reflectionNormalsTex.SampleLevel(ssReflectionLinear_s, r0.xy, 0).xyz;
  r2.xyz = r2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.z = dot(r2.xyz, r2.xyz);
  r0.z = rsqrt(r0.z);
  r2.xyz = r2.xyz * r0.zzz;
  r0.zw = trunc(v0.xy);
  r1.xy = r0.zw * r1.zz;
  r3.x = dot(CV_ScreenToWorldBasis._m00_m01_m02, r1.xyz);
  r3.y = dot(CV_ScreenToWorldBasis._m10_m11_m12, r1.xyz);
  r3.z = dot(CV_ScreenToWorldBasis._m20_m21_m22, r1.xyz);
  r0.z = dot(r3.xyz, r3.xyz);
  r0.z = rsqrt(r0.z);
  r4.xyz = r3.xyz * r0.zzz;
  r0.z = CV_NearFarClipDist.y * r1.z;
  r0.z = 1.5 * r0.z;
  r0.w = dot(r4.xyz, r2.xyz);
  r0.w = r0.w + r0.w;
  r2.xyz = r2.xyz * -r0.www + r4.xyz;
  r0.w = dot(r2.xyz, r2.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = r2.xyz * r0.www;
  r5.xyz = r2.xyz * r0.zzz;
  r0.w = dot(r4.xyz, r5.xyz);
  r0.w = saturate(0.5 + r0.w);
  r2.w = cmp(r0.w < 0.00999999978);
  // Ignore sky (draw black, no alpha) (there's no alternative apparently, we can't reflect it (or at least, the game always tried not to))
  r3.w = cmp(r1.z >= 0.9999999 && r1.z <= 1.0000001); // LUMA FT: change sky comparison from ==1 to >=0.9999999 as there was some precision loss in there, which made the SSR have garbage in the sky (trailed behind from the last drawn edge)
  r2.w = (int)r2.w | (int)r3.w;
  if (r2.w != 0) {
    o0.xyzw = float4(0,0,0,0);
    return;
  }
  r0.x = reflectionSpecularTex.SampleLevel(ssReflectionLinear_s, r0.xy, 0).x;
  r4.x = CV_ScreenToWorldBasis._m03 + r3.x;
  r4.y = CV_ScreenToWorldBasis._m13 + r3.y;
  r4.z = CV_ScreenToWorldBasis._m23 + r3.z;
  r4.w = 1;
  r1.x = dot(cbRefl.mViewProj._m00_m01_m02_m03, r4.xyzw);
  r1.y = dot(cbRefl.mViewProj._m10_m11_m12_m13, r4.xyzw);
  r1.w = dot(cbRefl.mViewProj._m30_m31_m32_m33, r4.xyzw);
  r2.xyz = r2.xyz * r0.zzz + r4.xyz;
  r2.w = 1;
  r3.x = dot(cbRefl.mViewProj._m00_m01_m02_m03, r2.xyzw);
  r3.y = dot(cbRefl.mViewProj._m10_m11_m12_m13, r2.xyzw);
  r0.y = dot(cbRefl.mViewProj._m20_m21_m22_m23, r2.xyzw);
  r3.w = dot(cbRefl.mViewProj._m30_m31_m32_m33, r2.xyzw);
  r0.y = r0.y / r3.w;
  r0.y = -CV_ProjRatio.x + r0.y;
  r3.z = CV_ProjRatio.y / r0.y;
  r2.xyzw = r3.xyzw + -r1.xyzw;
  r0.x = r0.x * 28 + 4;
  r0.y = (int)r0.x;
  r0.x = trunc(r0.x);
  r3.x = 1 / r0.x;
  r0.x = 1.60000002 * r0.x;
  r0.x = r0.z / r0.x;
  r0.x = r0.x / CV_NearFarClipDist.y;
  r6.y = 0;
  r3.z = r3.x;
  r3.y = 0;
  r0.z = 0;
  float2 sampleUVClamp = CV_HPosScale.xy - (CV_ScreenSize.zw * 2.0);
  while (true) {
    r3.w = cmp((int)r0.z >= (int)r0.y);
    if (r3.w != 0) break;
    r7.xyzw = r2.xyzw * r3.zzzz + r1.xyzw;
    r6.zw = r7.xy / r7.ww;
    r6.zw = max(float2(0,0), r6.zw);
    r6.zw = min(sampleUVClamp, r6.zw); // LUMA FT: Fixed clamp, it was using "CV_HPosClamp" here but "reflectionDepthScaledTex" is half resolution, so we need to clamp it differently
    r3.w = reflectionDepthScaledTex.SampleLevel(ssReflectionPoint_s, r6.zw, 0).x;
    r3.w = r3.w + -r7.z;
    r3.w = cmp(abs(r3.w) < r0.x);
    if (r3.w != 0) {
      r3.y = r3.z;
      break;
    }
    r6.x = r3.z + r3.x;
    r0.z = (int)r0.z + 1;
    r3.yz = r6.yx;
  }
  r0.x = cmp(0 < r3.y);
  if (r0.x != 0) {
    r0.x = reflectionLuminanceTex.SampleLevel(ssReflectionPoint_s, v1.xy, 0).x;
    r0.x = 100 * r0.x;
    r1.xyz = r5.xyz * r3.yyy + r4.xyz;
    r1.w = 1;
    r2.x = dot(cbRefl.mViewProjPrev._m00_m01_m02_m03, r1.xyzw);
    r2.y = dot(cbRefl.mViewProjPrev._m10_m11_m12_m13, r1.xyzw);
    r0.y = dot(cbRefl.mViewProjPrev._m30_m31_m32_m33, r1.xyzw);
    r0.yz = saturate(r2.xy / r0.yy);
    r1.x = min(r0.y, r0.z);
    r1.y = max(r0.y, r0.z);
    r1.y = 1 + -r1.y;
    r1.x = min(r1.y, r1.x);
    r1.y = cmp(0.0700000003 < r1.x);
    r1.x = 14.2857141 * r1.x;
    r1.x = sqrt(r1.x);
    r1.x = r1.y ? 1 : r1.x;
    r0.yz = cbRefl.screenScalePrev.xy * r0.yz;
    r0.yz = max(float2(0,0), r0.yz);
    r0.yz = min(cbRefl.screenScalePrevClamp.xy, r0.yz);
    // LUMA FT: this sampler has a border color (black?), though we clamp the UV so we never get that
    r1.yzw = reflectionPreviousSceneTex.SampleLevel(ssReflectionLinearBorder_s, r0.yz, 0).xyz;
    r0.xyz = min(r1.yzw, r0.xxx);
    r1.yzw = (int3)r0.xyz & int3(0x7f800000,0x7f800000,0x7f800000);
    r1.yzw = cmp((int3)r1.yzw != int3(0x7f800000,0x7f800000,0x7f800000));
    o0.xyz = r1.yzw ? r0.xyz : 0;
    o0.w = r1.x * r0.w;
  } else {
    o0.xyzw = float4(0,0,0,0);
  }
}