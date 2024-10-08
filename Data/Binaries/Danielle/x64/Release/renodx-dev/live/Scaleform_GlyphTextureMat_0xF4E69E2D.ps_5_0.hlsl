#include "include/UI.hlsl"

cbuffer PER_BATCH : register(b0)
{
  row_major float2x4 cBitmapColorTransform : packoffset(c0);
  row_major float4x4 cColorTransformMat : packoffset(c2);
}

SamplerState texMap0_s : register(s0);
Texture2D<float4> texMap0 : register(t0);

// PS_GlyphTextureMat
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 outColor : SV_Target0)
{
	float4 c = texMap0.Sample(texMap0_s, v1.xy).xyzw;
	outColor = mul(c, cColorTransformMat) + cBitmapColorTransform[1] * (c.a + cBitmapColorTransform[1].a);
  
  outColor = ConditionalLinearizeUI(outColor);
#if TEST_UI && 0
  outColor.rgb = float3(0, 2, 0);
#endif
#if !ENABLE_UI
  outColor = 0;
#endif

  return;
}