#include "include/Common.hlsl"

cbuffer PER_BATCH : register(b0)
{
  float4 psParams[16] : packoffset(c0);
}

SamplerState _tex0_s : register(s0);
SamplerState _tex1_s : register(s1);
Texture2D<float4> _tex0 : register(t0);
Texture2D<float4> _tex1 : register(t1);

// Screen space blur
void main(
  float4 v0 : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
  // LUMA FT: fixed the variable names being inverted (blurred and non blurred).
  float4 blurredColor = _tex0.Sample(_tex0_s, inBaseTC.xy).xyzw;
  float4 screenColor = _tex1.Sample(_tex1_s, inBaseTC.xy).xyzw;
  // LUMA FT: this is a blur shader but it could also be used to do sharpening (moving away from a blurred version of the image) (though based on CryEngine code, it can't be used for sharpening).
  float blurAmount = psParams[0].w;
#if !ENABLE_SHARPENING
  blurAmount = max(blurAmount, 0.0);
#endif
  // LUMA FT: we ignore "POST_PROCESS_SPACE_TYPE" here, it will look fine regardless (we could implement DecodeBackBufferToLinearSDRRange()/EncodeBackBufferFromLinearSDRRange() if ever needed, running them as UI)
  outColor = lerp(screenColor, blurredColor, blurAmount);
  outColor.rgb = FixUpSharpeningOrBlurring(outColor.rgb, screenColor.rgb);
  return;
}