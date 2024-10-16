#include "include/Scaleform.hlsl"

SamplerState texMap0_s : register(s0);
Texture2D<float4> texMap0 : register(t0);

// This one seems to draw text (and maybe more)
// PS_GlyphAlphaTexture
void main(
  float4 pos : SV_Position0,
  float2 tex0 : TEXCOORD0,
  float4 col0 : COLOR0,
  out float4 outColor : SV_Target0)
{
	float4 res = float4(col0.rgb, col0.a * texMap0.Sample(texMap0_s, tex0).a);
	outColor = PremultiplyAlpha( res * cBitmapColorTransform[0] + cBitmapColorTransform[1] );
  return;
}