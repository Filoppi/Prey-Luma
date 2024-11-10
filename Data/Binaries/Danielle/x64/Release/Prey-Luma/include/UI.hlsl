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
	// 4 Additive blend (source and destination are simply summed up, ignoring the alpha): result = source.RGB + dest.RGB
	uint AlphaBlendState;
	// Modulates the alpha of the background, decreasing it to emulate background tonemapping (we don't have access to the actual background color so we can only modulate the alpha).
	// Disabled at 0. Maxed out at 1.
	// Values beyond 0.5 probably create haloing on the UI so they are best avoided.
	float BackgroundTonemappingAmount;
  } LumaUIData : packoffset(c0);
}

// Whether to use the most theoretically mathematically accurate formulas (which are generally correct in the average case), or ones specifically tailored for Prey's use cases:
#define EMPYRICAL_UI_BLENDING_1 1
// This is currently disabled as it still doesn't look better.
#define EMPYRICAL_UI_BLENDING_2 0
#define EMPYRICAL_UI_BLENDING_3 1

// Guessed value of the average background color (or scene/UI brightness)
static const float AverageUIBackgroundColorGammaSpace = 1.0 / 3.0;

// Returns the perceptual "intensity" of the color
float UIColorIntensity(float3 Color, bool LinearSpace = false)
{
#if 1
	float luminance = GetLuminance(LinearSpace ? Color : game_gamma_to_linear(Color));
	return linear_to_game_gamma(luminance).x;
#elif 1 // This is less accurate as it doesn't calculate the luminance in linear space, though it looks about identical
	return GetLuminance(LinearSpace ? linear_to_game_gamma(Color) : Color);
#else // This isn't the most perceptually accurate one, though again it looks about identical so it could be considered as an optimization path
	return average(LinearSpace ? linear_to_game_gamma(Color) : Color);
#endif
}

float4 ConditionalLinearizeUI(float4 UIColor, bool PreMultipliedAlphaByAlpha = false, bool ForceStraightAlphaBlend = false)
{
	// Luma FT: In this case, the game is likely drawing on scene/world interactive computers that use Scaleform UI
	// (it's the first thing it does every frame, so as long as we are before "PostAAComposites", we know that'd be the case).
	// They'd be drawn in gamma space and then linearized in the scene with an sRGB texture view.
	// There's a good chance the developers developed these on a gamma 2.2 screen and thus the texture would be meant to be linearized with
	// gamma 2.2 instead of sRGB. We can't be certain, but if we ever wanted, we could pre-apply a 2.2<->sRGB mismatch here, to visualize these UIs correctly in the world.
	if (!LumaUIData.WritingOnSwapchain)
	{
#if 0 // Quick test
		if (UIColor.a > 0)
		{
			UIColor = 1;
		}
#endif
#if 0 // A WIP version of the idea mentioned above (we'd still need to make sure we don't re-apply the gamma correction on textures that are are first redrawn aside and then on the actual final render target) (this throws warning 4000?)
		UIColor.rgb = linear_to_gamma(gamma_sRGB_to_linear(UIColor.rgb));
#endif
		return UIColor;
	}
	
	bool gammaSpace = true;
	
#if POST_PROCESS_SPACE_TYPE == 1 // Disable this branch to leave the UI blend in in linear
	// Apply the "inverse" of the blend state transformation, and some other modulation of the UI color and alpha,
	// to emulate vanilla gamma space blends as closely as possible, while avoiding the hue shift from gamma space blends too (which will shift the look from Vanilla a bit, but might possibly look even better).
	if (LumaUIData.AlphaBlendState == 1 || ForceStraightAlphaBlend)
	{
		float3 UIColorLinearSpace = game_gamma_to_linear(UIColor.rgb);

#if EMPYRICAL_UI_BLENDING_1
		// This looks better on average in the inventory menu (the blends with the background)
		float targetUIAlpha = safePow(UIColor.a, pow(DefaultGamma, 0.75));
#else
		float targetUIAlpha = safePow(UIColor.a, DefaultGamma); // Same as "gamma_to_linear(GCT_MIRROR)", we can't modulate alpha with sRGB encoding)
#endif
		// This is equivalent to "game_gamma_to_linear(UIColor.rgb * UIColor.a)", despite that not being so intuitive, we can apply the same pow to either the color or the alpha to get the same result
		float3 targetPostPreMultipliedAlphaUIColorLinearSpace = UIColorLinearSpace * targetUIAlpha;

#if 1
		// This formula is the closest we can get to gamma space blends without knowing the color of the background.
		// As the UI color grows or shrinks, we pick a different alpha modulation for our background,
		// when the UI color is near black, we modulate the alpha in one direction,
		// while when its near white, we modulate it in the opposite direction.
		// The result is always as close as it can be to gamma space blends, especially when the UI color is near black,
		// and when the background color is either black or white.
		// Note that arguably, we should take the average of the UI color as the lerp alpha, instead of its luminance, because luminance
		// shouldn't really matter, and we wouldn't want green to react different from blue (ideally we'd have 3 alphas, one of each channel, but we don't),
		// but, at the same time luminance also makes sense as the average of a color is completely meaningless and depends on its color space (color primaries).
		// Note that if we wanted, we could always guess that the background is mid gray (or something like that) and modulate our alpha based on that assumption,
		// but while that would look better in some/most cases, it would look worse (possibly a lot worse) in other cases, so we prefer to do something
		// more conservative that never looks that bad.
		// 
		// The math we are trying to replicate is this:
		// ResultColorGammaSpace = (SourceColorGammaSpace * SourceAlpha) + (BackgroundColor * (1 - SourceAlpha))
		// But given the background is in linear space now, we need to modulate the alpha.
		// When the source color is dark, we use (the double "1 - x" in the lerp below is because of this):
		// ResultColorLinearSpace = (SourceColorGammaSpace^2.2 * SourceAlpha^2.2) + (BackgroundColor^2.2 * (1 - SourceAlpha)^2.2)
		// When the source color is bright, we use:
		// ResultColorLinearSpace = (SourceColorGammaSpace^2.2 * SourceAlpha^2.2) + (BackgroundColor^2.2 * 1 - SourceAlpha^2.2)
		// We only have on alpha shared by the source and target color, so we "pre-divide" the pre-multiplied color by the new alpha,
		// so that after alpha blending, it will have the value we expected it to have in the formula above.
		float targetBackgroundAlpha = lerp(1.0 - safePow(1.0 - UIColor.a, DefaultGamma), targetUIAlpha, saturate(UIColorIntensity(UIColorLinearSpace.rgb, true)));
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
	else if (LumaUIData.AlphaBlendState == 2 && UIColor.a != 0)
	{
		// Branch on whether the alpha channel was also pre-multiplied by itself.
		// It seems to look identical independently of the value it has, so we leave it off by default,
		// as it matches what the Scaleform code generally does (we can't find any alpha pre-multiply code, and having it off is also mathematically correct (it matches the GPU blend mode behaviour))
		float3 prePreMultipliedAlphaUIColor = UIColor.rgb / (PreMultipliedAlphaByAlpha ? sqrt(UIColor.a) : UIColor.a);

#if EMPYRICAL_UI_BLENDING_1
		float targetUIAlpha = safePow(UIColor.a, pow(DefaultGamma, 0.75));
#else
		float targetUIAlpha = safePow(UIColor.a, DefaultGamma);
#endif
#if EMPYRICAL_UI_BLENDING_2
		float targetBackgroundAlpha = lerp(1.0 - safePow(1.0 - UIColor.a, DefaultGamma), targetUIAlpha, saturate(UIColorIntensity(UIColor.rgb)));
#else // Theoretically this is more "mathematically accurate" and should provide the best results possible across the most cases
		float targetBackgroundAlpha = lerp(1.0 - safePow(1.0 - UIColor.a, DefaultGamma), targetUIAlpha, saturate(UIColorIntensity(prePreMultipliedAlphaUIColor)));
#endif
		
		targetBackgroundAlpha = lerp(targetBackgroundAlpha, max(targetBackgroundAlpha, 1), LumaUIData.BackgroundTonemappingAmount * saturate(UIColor.a));

		UIColor.a = targetBackgroundAlpha;
// This shouldn't be needed given that the UI color is pre-multiplied and already has the "perfect" alpha intensity,
// so theoretically we'd only need to modulate the background darkening alpha, but somehow this makes the output look more accurate in some cases.
#if EMPYRICAL_UI_BLENDING_2
		UIColor.rgb = prePreMultipliedAlphaUIColor * UIColor.a;
#endif
	}
	else if (LumaUIData.AlphaBlendState == 3 && UIColor.a != 0)
	{
#if 0 // Not needed until proven otherwise (it looks worse at the moment)
		// Bias towards the right result, at the cost of having a worse result if the background was too far from the guessed value
		float3 averageBlendedColor = (UIColor.rgb * UIColor.a) + AverageUIBackgroundColorGammaSpace;
		float3 averageLinearBlendedColor = linear_to_game_gamma((game_gamma_to_linear(UIColor.rgb) * UIColor.a) + safePow(AverageUIBackgroundColorGammaSpace, DefaultGamma));
		UIColor.rgb += (averageBlendedColor - averageLinearBlendedColor) / UIColor.a;
#endif

#if EMPYRICAL_UI_BLENDING_3
		// Theoretically, in this case, there's no need to do any gamma modulation given that we already convert the additive source color from gamma to linear space,
		// thus if we applied any further modulation on alpha, we'd double correct it, but... somehow doing this makes it look like vanilla (especially if we are blending on a black background)
		// so for now we leave it in (this case isn't often used in Prey, it's only in the inventory menu pretty much)
		UIColor.a = safePow(UIColor.a, DefaultGamma);
#endif
	}
	else if (LumaUIData.AlphaBlendState == 4)
	{
		// Nothing we can do here top make it look more like vanilla
	}
#endif // POST_PROCESS_SPACE_TYPE == 1

	return SDRToHDR(UIColor, gammaSpace, true);
}