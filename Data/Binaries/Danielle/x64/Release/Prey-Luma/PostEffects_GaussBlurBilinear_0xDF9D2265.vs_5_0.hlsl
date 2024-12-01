cbuffer PER_INSTANCE : register(b1)
{
  float4 PI_psOffsets[16] : packoffset(c0);
}

#include "include/CBuffer_PerViewGlobal.hlsl"

// This blurs the image. It's run after upscaling (and usually after TexToTexSampledPS), so it supports DLSS fine and doesn't need any "CV_HPosScale"/"MapViewportToRaster()" adjustments.
// It seems like it generally handles different aspect ratios correctly, but might not scale perfectly with DRS.
void main(
  float4 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Position0,
  out float4 o1 : TEXCOORD0,
  out float4 o2 : TEXCOORD1,
  out float4 o3 : TEXCOORD2,
  out float4 o4 : TEXCOORD3,
  out float4 o5 : TEXCOORD4)
{
  float4 r0;
  o0.xyzw = v0.xyzw * float4(2,-2,1,1) + float4(-1,1,0,0);
  // LUMA FT: fixed "PI_psOffsets" values not being scaled by rendere resolution scale, meaning the blurring would be more or less intense
  o1.xy = v1.xy * CV_HPosScale.xy + PI_psOffsets[0].xy * CV_HPosScale.xy;
  o1.zw = v1.xy * CV_HPosScale.xy + PI_psOffsets[1].xy * CV_HPosScale.xy;
  o2.xy = v1.xy * CV_HPosScale.xy + PI_psOffsets[2].xy * CV_HPosScale.xy;
  o2.zw = v1.xy * CV_HPosScale.xy + PI_psOffsets[3].xy * CV_HPosScale.xy;
  o3.xy = v1.xy * CV_HPosScale.xy + PI_psOffsets[4].xy * CV_HPosScale.xy;
  o3.zw = v1.xy * CV_HPosScale.xy + PI_psOffsets[5].xy * CV_HPosScale.xy;
  o4.xy = v1.xy * CV_HPosScale.xy + PI_psOffsets[6].xy * CV_HPosScale.xy;
  o4.zw = v1.xy * CV_HPosScale.xy + PI_psOffsets[7].xy * CV_HPosScale.xy;
  r0.xy = v1.xy * CV_HPosScale.xy;
  o5.xy = r0.xy;
  o5.zw = float2(0,0);
}