#include "include/UI.hlsl"

cbuffer PER_BATCH : register(b0)
{
  float4 psParams[16] : packoffset(c0);
}

SamplerState ssArkFullscreenTex_s : register(s0);
Texture2D<float4> ArkFullscreenTex : register(t0);

// ArkFullscreenTexturePS
// This runs after AA and PostCompositesAA. It possibly writes directly on the swapchain.
// This can draw a (possibly stretched) texture on screen, for example a vignette effect when zooming in with weapons (Z key) (though we can't know what it is upfront so we can't branch on "ENALBE_VIGNETTE").
// In ultrawide these textures simply stretch, possibly allowing for a much wider visibility range in case of vignette, but it's mostly fine (and kinda expected!).
void main(
  float4 v0 : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
#if !ENABLE_POST_PROCESS
	outColor = 0;
	return;
#endif

	const float fAlphaMultiplier = psParams[0].x;
	const float fAlphaTest       = psParams[0].y;

	float4 cTexture = ArkFullscreenTex.Sample(ssArkFullscreenTex_s, inBaseTC.xy);
	outColor.xyz = cTexture.xyz;
	outColor.w = cTexture.w * fAlphaMultiplier;

	// LUMA FT: Alpha mask (or simply an optimization threshold to avoid drawing pixels with alpha that is near zero and thus not perceivable)
	clip(outColor.w - fAlphaTest);

	outColor = ConditionalLinearizeUI(outColor, false, false, true); // This will take care of any "POST_PROCESS_SPACE_TYPE" case

//TODOFT: test this shader mode and detect what it draws, could it ever be UI stuff? Probably not anyway, and even if it was... it'd still be fine to be using the scene HDR paper white (and thus we wouldn't wanna hide it with the "ENABLE_UI" flag)
//Also, does this need linearization in all cases? It seems so, as it always runs at the end probably, whether it's writing on the swapchain or not.
//Delete this tests!
#if 0
	if (!LumaUIData.WritingOnSwapchain)
	{
		outColor.rgb = float3(2, 0, 1);
	}
#endif

  return;
}