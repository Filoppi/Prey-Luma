#include "include/ColorGradingLUT.hlsl" // Use this as it has some gamma correction helpers

Texture2D<float4> sourceTexture : register(t0);
Texture2D<float4> debugTexture : register(t1);

// Custom Luma shader to apply the display (or output) transfer function from a linear input (or apply custom gamma correction)
float4 main(float4 pos : SV_Position0) : SV_Target0
{
	// We can't account for the UI paper white at this point
	const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;

#if DEVELOPMENT
	float debugWidth;
	float debugHeight;
	debugTexture.GetDimensions(debugWidth, debugHeight);
	if (debugWidth != 0 && debugHeight != 0)
    {
		float2 resolutionScale = 1.0;
#if 1 // Stretch to fullscreen
		float targetWidth;
		float targetHeight;
		sourceTexture.GetDimensions(targetWidth, targetHeight);
		resolutionScale = float2(debugWidth / targetWidth, debugHeight / targetHeight);
#endif
#if 1 // Scale by rendering resolution
		resolutionScale *= LumaData.RenderResolutionScale;
#endif
		pos.xy = round((pos.xy - 0.5) * resolutionScale) + 0.5;
		float4 color = debugTexture.Load((int3)pos.xyz); // We don't have a sampler here so we just approimate to the closest texel
#if 0 // Linearize
        color.rgb = pow(color.rgb, 2.2f);
#elif 0
        color.rgb = pow(color.rgb, 1.f / 2.2f);
#endif
		return color * paperWhite;
    }
#endif

	float4 color = sourceTexture.Load((int3)pos.xyz);

	// SDR: In this case, paper white (game and UI) would have been 80 nits (neutral for SDR, thus having a value of 1)
	if (LumaSettings.DisplayMode == 0)
	{
		color.rgb = saturate(color.rgb); // Optional, but saves performance on the gamma pows below (the vanilla SDR tonemapper might have retained some values beyond 1 so we want to clip them anyway, for a "reliable" SDR look)

#if POST_PROCESS_SPACE_TYPE == 1
		// Revert whatever gamma adjustment "GAMMA_CORRECTION_TYPE" would have made, and get the color is sRGB gamma encoding (which would have been meant for 2.2 displays)
		color.rgb = linear_to_game_gamma(color.rgb, false);
#endif // POST_PROCESS_SPACE_TYPE == 1

		// In SDR, we ignore "GAMMA_CORRECTION_TYPE" as they are not that relevant
		// We are target the gamma 2.2 look here, which would likely match the average SDR screen, so
		// we linearize with sRGB because scRGB HDR buffers (Luma) in SDR are re-encoded with sRGB and then (likely) linearized by the display with 2.2, which would then apply the gamma correction.
		// For any user that wanted to play in sRGB, they'd need to have an sRGB monitor.
		// We could theoretically add a mode that fakes sRGB output on scRGB->2.2 but it wouldn't really be useful as the game was likely designed for 2.2 displays (unconsciously).
		color.rgb = gamma_sRGB_to_linear(color.rgb, GCT_NONE);
	}
	// HDR and SDR in HDR: in this case the UI paper white would have already been mutliplied in, relatively to the game paper white, so we only apply the game paper white.
	else if (LumaSettings.DisplayMode == 1 || LumaSettings.DisplayMode == 2)
	{
#if POST_PROCESS_SPACE_TYPE != 1 // Gamma->Linear space

		// At this point, in this case, the color would have been sRGB gamma space, normalized around SDR range (80 nits paper white).

#if GAMMA_CORRECTION_TYPE != 0 && 1 // Apply gamma correction only in the 0-1 range (generally preferred as anything beyond the 0-1 range was never seen and the consequence of correcting gamma on it would be random and extreme)

		color.rgb = ColorGradingLUTTransferFunctionOutCorrected(color.rgb, LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB, GAMMA_CORRECTION_TYPE);
		
#else // Apply gamma correction around the whole range

#if GAMMA_CORRECTION_TYPE >= 2
  		color.rgb = RestoreLuminance(gamma_sRGB_to_linear(color.rgb, GCT_MIRROR), gamma_to_linear(color.rgb, GCT_MIRROR));
#elif GAMMA_CORRECTION_TYPE == 1
		color.rgb = gamma_to_linear(color.rgb, GCT_MIRROR);
#else // GAMMA_CORRECTION_TYPE <= 0
  		color.rgb = gamma_sRGB_to_linear(color.rgb, GCT_MIRROR);
#endif // GAMMA_CORRECTION_TYPE >= 2

#endif // GAMMA_CORRECTION_TYPE != 0 && 1
		
		color.rgb *= paperWhite;

// The "GAMMA_CORRECTION_TYPE >= 2" type was always delayed until the end and treated as sRGB gamma before.
// We originally applied this gamma correction directly during tonemapping/grading and other later passes,
// but given that the formula is slow to execute and isn't easily revertible
// (mirroring back and forth is lossy, at least in the current lightweight implementation),
// we moved it to a single application here (it might not look as good but it's certainly good enough).
// Any linear->gamma->linear encoding (e.g. PostAACoposites) or linear->gamma->luminance encoding (e.g. Anti Aliasing)
// should fall back on gamma 2.2 instead of sRGB for this gamma correction type, but we haven't bothered implementing that (it's not worth it).
#elif GAMMA_CORRECTION_TYPE >= 2 // Linear->Linear space (POST_PROCESS_SPACE_TYPE == 1)

		// Implement the "GAMMA_CORRECTION_TYPE == 2" case, thus convert from sRGB to sRGB with 2.2 luminance.
		// Doing this is here is a bit late, as we can't acknowledge the UI brightness at this point, though that's not a huge deal.
		// Any other "POST_PROCESS_SPACE_TYPE == 1" case would already have the correct(ed) gamma at this point.
		color.rgb /= paperWhite;
   		float3 colorInExcess = color.rgb - saturate(color.rgb); // Only correct in the 0-1 range
		color.rgb = saturate(color.rgb);
#if 1 // This code mirrors "game_gamma_to_linear()"
		float3 gammaCorrectedColor = gamma_to_linear(linear_to_sRGB_gamma(color.rgb));
#else
		float gammaCorrectedColor = gamma_to_linear1(linear_to_sRGB_gamma1(GetLuminance(color.rgb))); // "gammaCorrectedLuminance"
#endif
		color.rgb = RestoreLuminance(color.rgb, gammaCorrectedColor);
		color.rgb += colorInExcess;
		color.rgb *= paperWhite;

#endif // POST_PROCESS_SPACE_TYPE != 1

#if 0 // Optionally clip in SDR to properly emulate SDR
		if (LumaSettings.DisplayMode == 1)
			color.rgb = saturate(color.rgb);
#endif
	}
	// This case means the game currently doesn't have Luma custom shaders built in (fallback in case of problems), so the value of most macro defines doesn't matter (we don't want it to)
	else
	{
#if 1 // HDR (we assume this is the default case for Luma users/devs, this isn't an officially supported case anyway) (if we wanted we could still check Luma defines or cbuffer settings)

		// Forcefully linearize with gamma 2.2 outside of a dev environment (gamma correction) (the default setting)
#if GAMMA_CORRECTION_TYPE <= 0 && DEVELOPMENT
		color.rgb = gamma_sRGB_to_linear(color.rgb, GCT_MIRROR);
#else // GAMMA_CORRECTION_TYPE >= 1
  		color.rgb = gamma_to_linear(color.rgb, GCT_MIRROR);
#endif // GAMMA_CORRECTION_TYPE <= 0
		color.rgb *= paperWhite;

#else // SDR (on SDR) //TODOFT: set the dispay mode to -1 and -2 to branch on this live, for better quick SDR/HDR Luma comparisons

		color.rgb = gamma_sRGB_to_linear(color.rgb, GCT_SATURATE);

#endif
	}

	//TODO LUMA: add "FixColorGradingLUTNegativeLuminance()" call here? It's not really needed until proven otherwise (we should never have negative luminances, no code can generate them (?)).
	//Either way this shader doesn't always run and there's stuff that optionally runs before this, like sharpening (which can randomly affect luminance as it's by channel).

#if 0 // Test
	color.rgb = float3(1, 0, 0);
#endif

	return float4(color.rgb, color.a);
}