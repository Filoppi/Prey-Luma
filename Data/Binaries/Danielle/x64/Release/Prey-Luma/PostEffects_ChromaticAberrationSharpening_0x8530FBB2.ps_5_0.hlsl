#include "include/Common.hlsl"

cbuffer PER_BATCH : register(b0)
{
  float4 psParams[16] : packoffset(c0);
}

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState _tex0_s : register(s0);
SamplerState _tex1_s : register(s1);
Texture2D<float4> _tex0 : register(t0);
Texture2D<float4> _tex1 : register(t1);

#define PS_ScreenSize CV_ScreenSize

// We optionally sharpen in gamma space to keep it looking as it did (assuming "POST_PROCESS_SPACE_TYPE" was 0),
// though there's not much difference in the look, so for consistency we keep this on, even if it has a little performance cost in some configurations (it might actually look worse in gamma space).
#define FORCE_SHARPEN_IN_LINEAR_SPACE 1

// CA_SharpeningPS
// Runs after "PostAAComposites" and probably "UpscaleImage" too.
// It seems like this applies if the user has set sharpening to a positive value, or if the scene in the game has the sharpening increased for some reason.
void main(
  float4 v0 : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
  float4 screenColor = _tex0.Sample(_tex0_s, inBaseTC.xy);

#if ENABLE_CHROMATIC_ABERRATION
  // LUMA FT: Chromatic Aberration here already seems to correctly acknowledge the aspect ratio and support ultrawide
	screenColor.r = _tex0.Sample(_tex0_s, (inBaseTC.xy-0.5)*(1 + 2*psParams[0].x*PS_ScreenSize.zw) + 0.5).r;
	screenColor.b = _tex0.Sample(_tex0_s, (inBaseTC.xy-0.5)*(1 - 2*psParams[0].x*PS_ScreenSize.zw) + 0.5).b;
#endif

  // Screen space blur/sharpening
  float4 blurredColor = _tex1.Sample(_tex1_s, inBaseTC.xy);
#if FORCE_SHARPEN_IN_LINEAR_SPACE
  // LUMA FT: force sharpening to run in linear space
  bool isUI = true; // This runs after "PostAAComposites", so the backbuffer would have been in gamma space if "POST_PROCESS_SPACE_TYPE" was 0 or 2.
	screenColor.rgb = DecodeBackBufferToLinearSDRRange(screenColor.rgb, isUI);
	blurredColor.rgb = DecodeBackBufferToLinearSDRRange(blurredColor.rgb, isUI);
#endif // FORCE_SHARPEN_IN_LINEAR_SPACE

  float sharpenAmount = psParams[0].w;
#if !ENABLE_SHARPENING // LUMA FT: we still allow blurring (it should never happen in this shader anyway, based on the CryEngine source)
  sharpenAmount = min(sharpenAmount, 1.0);
#endif // !ENABLE_SHARPENING
  outColor = lerp(blurredColor, screenColor, sharpenAmount);
  outColor.rgb = FixUpSharpeningOrBlurring(outColor.rgb, screenColor.rgb);

#if FORCE_SHARPEN_IN_LINEAR_SPACE
	outColor.rgb = EncodeBackBufferFromLinearSDRRange(outColor.rgb, isUI);
#endif // FORCE_SHARPEN_IN_LINEAR_SPACE

  return;
}