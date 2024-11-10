#include "include/UI.hlsl"

cbuffer PER_BATCH : register(b0)
{
  float4 psParams[16] : packoffset(c0);
}

SamplerState ssArkFullscreenTex_s : register(s0);
Texture2D<float4> ArkFullscreenTex : register(t0);

// ArkFullscreenTexturePS
void main(
  float4 v0 : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
	const float fAlphaMultiplier = psParams[0].x;
	const float fAlphaTest       = psParams[0].y;

	float4 cTexture = ArkFullscreenTex.Sample(ssArkFullscreenTex_s, inBaseTC.xy);
	outColor.xyz = cTexture.xyz;
	outColor.w = cTexture.w * fAlphaMultiplier;

	// LUMA FT: Alpha mask (or simply an optimization threshold to avoid drawing pixels with alpha that is near zero and thus not perceivable)
	clip(outColor.w - fAlphaTest);

//TODOFT2: when does this run? Does this need linearization? Supposedly after AA.
//Does this need proper alpha blending with the UI? Should this use the UI or game paper white?
#if POST_PROCESS_SPACE_TYPE == 1
	if (LumaUIData.WritingOnSwapchain)
	{
		const float paperWhite = GamePaperWhiteNits / sRGB_WhiteLevelNits;
		outColor.rgb = game_gamma_to_linear(outColor.rgb);
		outColor.rgb *= paperWhite;
	}
#endif // POST_PROCESS_SPACE_TYPE == 1

#if TEST_UI
	outColor.rgb = float3(2, 0, 1);
#endif

#if !ENABLE_UI // We treat this as UI, as it likely is
	outColor = 0;
#endif // !ENABLE_UI

  return;
}