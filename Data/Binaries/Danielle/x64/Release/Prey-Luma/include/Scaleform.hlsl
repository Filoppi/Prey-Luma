#include "include/UI.hlsl"

cbuffer PER_BATCH : register(b0)
{
  row_major float2x4 cBitmapColorTransform : packoffset(c0);
  float cPremultiplyAlpha : packoffset(c2);
}

float4 AdjustForMultiply( float4 col )
{
	return lerp( float4( 1, 1, 1, 1 ), col, col.a );
}

float4 PremultiplyAlpha( float4 col )
{
#if !ENABLE_UI //TODOFT: some of these are still missing? In blur shaders?
	return 0;
#endif

	// LUMA FT: added optional UI linearization (with some advanced alpha modulations to emulate gamma space blends in linear space, and paper white brightness scaling).
	// Note that Prey sometimes uses the "straight alpha" (LumaUIData.AlphaBlendState == 1) formula over a temporary/separate texture, which is then stored as pre-multiplied alpha,
	// and then drawn later on the swapchain; though while doing so, they accidentally also multiply the alpha by itself, so if it was 0.9 in the source,
	// it will end up being 0.9*0.9 in the pre-multiplied alpha texture (this would only maybe make sense if you keep drawing multiple layers on top of the same texture).
	bool ForceStraightAlphaBlend = cPremultiplyAlpha >= 0.5;
	col = ConditionalLinearizeUI(col, ForceStraightAlphaBlend);

#if 0 // LUMA FT: this was needed in "Kingdom Come Deliverance" to avoid fade outs on hud tooltips causing a black after image they appear, it's not needed until proven otherwise in Prey
	col.a = saturate(col.a);
#endif

#if TEST_UI
	// LUMA FT: this doesn't actually seem to be used by Prey, it simply sets the blend state to pre-multiplied alpha. The alpha of the background (render target) is ignored and never affects UI.
	// The formula also didn't seem to make sense as it was pre-multiplied the alpha channel by itself too.
	if (cPremultiplyAlpha)
	{
		col.rgba = float4(2, 0, 0, 1);
	}
#endif

	return cPremultiplyAlpha ? col * col.a : col;
}