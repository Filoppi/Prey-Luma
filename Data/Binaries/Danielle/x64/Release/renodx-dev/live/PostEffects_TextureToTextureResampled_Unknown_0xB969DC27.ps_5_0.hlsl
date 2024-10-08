#define _RT_SAMPLE0 0
#define _RT_SAMPLE1 0
#define _RT_SAMPLE2 0
#define _RT_SAMPLE3 0
#define _RT_SAMPLE4 0
#define _RT_SAMPLE0 0

cbuffer PER_BATCH : register(b0)
{
  float4 texToTexParams0 : packoffset(c0);
  float4 texToTexParams1 : packoffset(c1);
}

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState _tex0_s : register(s0);
Texture2D<float4> _tex0 : register(t0);

// LUMA: Unchanged.
// TexToTexSampledPS
// This is some kind of blurring (or sharpening? in case the blend mode was additive or something). It's run after upscaling (as least in the main use case).
// This always runs in post process if we forced sharpening or chromatic aberration, before them, so it probably does nothing in that case (they just have a fixed pipeline).
// Note that this also runs many times in the middle of the rendering pipeline, possibly to downscale textures and stuff like that.
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xy = CV_HPosScale.xy * v1.xy;
  r0.xyzw = _tex0.Sample(_tex0_s, r0.xy).xyzw;
  r1.xyzw = v1.xyxy * CV_HPosScale.xyxy + texToTexParams0.xyzw; // Offset the UVs
  r2.xyzw = _tex0.Sample(_tex0_s, r1.xy).xyzw;
  r1.xyzw = _tex0.Sample(_tex0_s, r1.zw).xyzw;
  r0.xyzw = r2.xyzw + r0.xyzw;
  r0.xyzw = r0.xyzw + r1.xyzw;
  r1.xyzw = v1.xyxy * CV_HPosScale.xyxy + texToTexParams1.xyzw;
  r2.xyzw = _tex0.Sample(_tex0_s, r1.xy).xyzw;
  r1.xyzw = _tex0.Sample(_tex0_s, r1.zw).xyzw;
  r0.xyzw = r2.xyzw + r0.xyzw;
  r0.xyzw = r0.xyzw + r1.xyzw;
  o0.xyzw = float4(0.200000003,0.200000003,0.200000003,0.200000003) * r0.xyzw;
  return;
}