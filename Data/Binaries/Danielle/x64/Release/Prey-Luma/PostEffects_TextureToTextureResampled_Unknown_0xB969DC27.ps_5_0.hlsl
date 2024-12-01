#define _RT_SAMPLE0 0
#define _RT_SAMPLE1 0
#define _RT_SAMPLE2 0
#define _RT_SAMPLE3 0
#define _RT_SAMPLE4 0
#define _RT_SAMPLE5 0

cbuffer PER_BATCH : register(b0)
{
  float4 texToTexParams0 : packoffset(c0); // Often set to the inverse of the output resolution (pixel size)
  float4 texToTexParams1 : packoffset(c1); // Often set to the inverse of the output resolution (pixel size)
}

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState _tex0_s : register(s0);
Texture2D<float4> _tex0 : register(t0);

// TexToTexSampledPS
// This is a resampling shader (from one resolution to another, e.g. upscale, downscale/blur/mipmap, stretch). It's run after upscaling (as least in the main use case).
// This always runs in post process if we forced sharpening or chromatic aberration, before them, so it probably does nothing in that case (they just have a fixed pipeline).
// Note that this also runs many times in the middle of the rendering pipeline, to downscale textures and stuff like that.
// Note that this can generate invalid luminances, but we can't fix it here as the shader is too generic.
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  // LUMA FT: fixed missing UV clamps
  float2 outputResolution;
  _tex0.GetDimensions(outputResolution.x, outputResolution.y); // We can't use "CV_ScreenSize" here as that's for the output resolution
  float2 sampleUVClamp = CV_HPosScale.xy - (0.5 / outputResolution);

  float4 r0,r1,r2;
  r0.xy = v1.xy * CV_HPosScale.xy;
  r0.xyzw = _tex0.Sample(_tex0_s, min(r0.xy, sampleUVClamp)).xyzw;
  r1.xyzw = v1.xyxy * CV_HPosScale.xyxy + texToTexParams0.xyzw;
  r2.xyzw = _tex0.Sample(_tex0_s, min(r1.xy, sampleUVClamp)).xyzw;
  r1.xyzw = _tex0.Sample(_tex0_s, min(r1.zw, sampleUVClamp)).xyzw;
  r0.xyzw += r1.xyzw + r2.xyzw;
  r1.xyzw = v1.xyxy * CV_HPosScale.xyxy + texToTexParams1.xyzw;
  r2.xyzw = _tex0.Sample(_tex0_s, min(r1.xy, sampleUVClamp)).xyzw;
  r1.xyzw = _tex0.Sample(_tex0_s, min(r1.zw, sampleUVClamp)).xyzw;
  r0.xyzw += r1.xyzw + r2.xyzw;
  o0.xyzw = r0.xyzw / 5.0;
  return;
}