#include "include/Common.hlsl"

cbuffer LumaUIData : register(b7)
{
  struct
  {
	// If true, this pixel shader render target is the swapchain texture
	uint WritingOnSwapchain;
	// 0 No alpha blend (or other unknown blend types that we can ignore)
	// 1 Straight alpha blend: "result = (source.RGB * source.A) + (dest.RGB * (1 - source.A))" or "result = lerp(dest.RGB, source.RGB, source.A)"
	// 2 Pre-multiplied alpha blend (alpha is also pre-multiplied, not just rgb): "result = source.RGB + (dest.RGB * (1 - source.A))"
	// 3 Additive alpha blend (source is "Straight alpha" while destination is retained at 100%): "result = (source.RGB * source.A) + dest.RGB"
	// 4 Additive blend (source and destination are simply summed up): result = source.RGB + dest.RGB
	uint AlphaBlendState;
	// Modulates the alpha of the background, decreasing it to emulate background tonemapping (we don't have access to the actual background color so we can only modulate the alpha).
	// Disabled at 0. Maxed out at 1.
	// Values beyond 0.5 probably create haloing on the UI so they are best avoided.
	float BackgroundTonemappingAmount;
  } LumaUIData : packoffset(c0);
}

// Whether to use the most theoretically mathematically accurate formulas, so the ones that actually look best (or generally better) in Prey's use case.
#define EMPYRICAL_UI_BLENDING 0

// Guessed value of the average background color (or scene/UI brightness)
static const float AverageUIBackgroundColorGammaSpace = 1.0 / 3.0;

float4 ConditionalLinearizeUI(float4 UIColor, bool ForceStraightAlphaBlend = false)
{
	if (!LumaUIData.WritingOnSwapchain)
	{
#if 0 // Quick test
		if (UIColor.a > 0)
		{
			UIColor = 1;
		}
#endif
		return UIColor; 
	}
	
	bool gammaSpace = true;
	
#if POST_PROCESS_SPACE_TYPE == 1 // Disable this branch to leave the UI blend in in linear
	// Apply the "inverse" of the blend state transformation, and some other modulation of the UI color and alpha,
	// to emulate vanilla gamma space blends as closely as possible, while avoiding the hue shift from gamma space blends too (which will shift the look from Vanilla a bit, but might possibly look even better).
	if (LumaUIData.AlphaBlendState == 1 || ForceStraightAlphaBlend)
	{
		float3 UIColorLinearSpace = game_gamma_to_linear_mirrored(UIColor.rgb);

		float targetUIAlpha = safePow(UIColor.a, DefaultGamma); // Same as "gamma_to_linear_mirrored()"
		// This is equivalent to "game_gamma_to_linear_mirrored(UIColor.rgb * UIColor.a)", despite that not being so intuitive, we can apply the same pow to either the color or the alpha to get the same result
		float3 targetPostPreMultipliedAlphaUIColorLinearSpace = UIColorLinearSpace * targetUIAlpha;

#if 1
		// This formula is the closest we can get to gamma space blends without knowing the color of the background.
		// As the UI color grows or shrinks, we pick a different alpha modulation for our background,
		// when the UI color is near black, we modulate the alpha in one direction,
		// while when its near white, we modulate it in the opposite direction.
		// The result is is always as close as it can be to gamma space blends, especially when the UI color is near black,
		// and when the background color is either black or white.
		// Note that we take the average of the UI color as the lerp alpha, instead of the luminance, because luminance
		// doesn't really matter here, and we wouldn't want green to react different from blue (ideally we'd have 3 alphas, one of each channel, but we don't).
		// Note that if we wanted, we could always guess that the background is mid gray (or something like that) and modulate our alpha based on that assumption,
		// but while that would look better in some/most cases, it would look worse (possibly a lot worse) in other cases, so we prefer to do something
		// more conservative that never looks that bad.
		float targetBackgroundAlpha = lerp(1.0 - safePow(1.0 - UIColor.a, DefaultGamma), targetUIAlpha, saturate(average(UIColor.rgb))); //TODOFT3: do blends by luminance (or inverse luminance) instead? (it looks about the same)
#else // This alternative is simpler but always worse. Generally it's 50% accurate, and revolves around the target result when the UI color goes from black to white.
		float targetBackgroundAlpha = UIColor.a;
#endif

		// Simplified emulated background tonemapping to make the UI more readable on HDR backgrounds.
		// We can't do much else other than increasing the UI alpha to further darken the background (we do that exponentially more based on how big the alpha was in the first place).
		// We might further modulate the alpha based on the current color of the UI (e.g. whether its black or white), but this is not needed until proven otherwise.
		targetBackgroundAlpha = lerp(targetBackgroundAlpha, max(targetBackgroundAlpha, 1), LumaUIData.BackgroundTonemappingAmount * saturate(UIColor.a));

		UIColor.a = targetBackgroundAlpha;
		// Pre-divide the color by the alpha it will be multiplied by later, so we can exactly control its final color independently of the alpha (this makes the alpha only affect the background)
		if (targetBackgroundAlpha != 0)
		{
			UIColor.rgb = targetPostPreMultipliedAlphaUIColorLinearSpace / targetBackgroundAlpha;
		}
		// We can't modulate the color based on the alpha in this case, so just leave it as it was (but in linear).
		// This case should never happen, and even if it did, it shouldn't matter.
		else
		{
			UIColor.rgb = UIColorLinearSpace;
		}
		gammaSpace = false;
	}
	else if (LumaUIData.AlphaBlendState == 2)
	{
		float3 prePreMultipliedAlphaUIColor = UIColor.a != 0 ? (UIColor.rgb / UIColor.a) : UIColor.rgb;
		float targetUIAlpha = safePow(UIColor.a, DefaultGamma); // Same as "gamma_to_linear_mirrored()"
#if EMPYRICAL_UI_BLENDING // This case seems to look ~identical to vanilla in Prey
		float targetBackgroundAlpha = lerp(1.0 - safePow(1.0 - UIColor.a, DefaultGamma), targetUIAlpha, saturate(average(UIColor.rgb)));
#else // Theoretically this is more "mathematically accurate" and should provide the best results possible (except it doesn't, at least in the Prey use cases)
		float targetBackgroundAlpha = lerp(1.0 - safePow(1.0 - UIColor.a, DefaultGamma), targetUIAlpha, saturate(average(prePreMultipliedAlphaUIColor)));
#endif
		
		targetBackgroundAlpha = lerp(targetBackgroundAlpha, max(targetBackgroundAlpha, 1), LumaUIData.BackgroundTonemappingAmount * saturate(UIColor.a));

		UIColor.a = targetBackgroundAlpha;
// This shouldn't be needed given that the UI color is pre-multiplied and already has the "perfect" alpha intensity,
// so theoretically we'd only need to modulate the background darkening alpha, but somehow this makes the output look more accurate.
#if EMPYRICAL_UI_BLENDING
		if (UIColor.a != 0)
		{
			UIColor.rgb = prePreMultipliedAlphaUIColor * UIColor.a;
		}
#endif
	}
	// There's no need to do any gamma modulation here given that we already convert the additive source color from gamma to linear space,
	// thus if we applied any further modulation on alpha, we'd double correct it.
	else if (LumaUIData.AlphaBlendState == 3)
	{
#if 0 // Not needed until proven otherwise
		// Bias towards the right result, at the cost of having a worse result if the background was too far from the guessed value
		float3 averageBlendedColor = (UIColor.rgb * UIColor.a) + AverageUIBackgroundColorGammaSpace;
		float3 averageLinearBlendedColor = linear_to_game_gamma_mirrored((game_gamma_to_linear_mirrored(UIColor.rgb) * UIColor.a) + safePow(AverageUIBackgroundColorGammaSpace, DefaultGamma));
		UIColor.rgb += safeDivision(averageBlendedColor - averageLinearBlendedColor, UIColor.a, 0);
#endif
	}
	else if (LumaUIData.AlphaBlendState == 4)
	{
		// Nothing we can do here top make it look more like vanilla
	}
#endif // POST_PROCESS_SPACE_TYPE == 1

	return SDRToHDR(UIColor, gammaSpace, true);
}