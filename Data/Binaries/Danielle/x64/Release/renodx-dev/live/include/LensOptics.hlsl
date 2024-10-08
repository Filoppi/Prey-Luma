#include "include/Common.hlsl"

// This cbuffer is not actually set by lens optics shaders, but it should be still readable from the previous passes that set it (nobody else uses register b13)
#include "include/CBuffer_PerViewGlobal.hlsl"

// Formula "y = (x * (b * x + c)) / (x * (b * x + 1.7) + d)"
float3 LensOpticsTonemap(float3 color, float shoulderScale, float linearScale, float toeScale, float linearScaleDivisor)
{
	return (color * (shoulderScale * color + linearScale)) / (color * (shoulderScale * color + linearScaleDivisor) + toeScale);
}
float LensOpticsInverseTonemap(float color, float shoulderScale, float linearScale, float toeScale, float linearScaleDivisor)
{
	static const float constant1 = 10.0;
	float divisor = (shoulderScale * color) - shoulderScale;
	float part1 = (0.5 * (linearScale - (linearScaleDivisor * color))) / divisor;
	float part2 = ((0.5 / constant1) * (sqrt(sqr((linearScaleDivisor * constant1 * color) - (constant1 * linearScale)) - (4.0 * constant1 * toeScale * color * ((constant1 * shoulderScale * color) - (constant1 * shoulderScale)))))) / divisor;
#if 1 // "part2" is usually negative so the subtractive branch is usually the solution, though we take the max of the two for safety, given we almost always deal with positive colors
	return max(part1 - part2, part1 + part2);
#else
	return part1 - part2;
#endif
}

float4 ToneMappedPreMulAlpha( float4 color, bool allowHDR = true )
{
	// Use-pre multiplied alpha for LDR version (consoles)
	color.rgb *= color.a;

	// LUMA FT: it's unclear why this applies tonemapping without then applying gamma, is it baked in gamma space? The source color has values beyond 1, especially before pre-multiplying alpha,
	// though that doesn't mean its linear. It's hard to determine especially what these colors were, especially because they are drawn on top of each other many many times, each layer is additive,
	// and so through small values it builds up to high numbers. This implies there's no concept of gamma/linear, just raw numbers, but we should interpret the final color as gamma space for accurate results,
	// as it was in the vanilla code.

	const float shoulderScale = HDRParams.x; // Same parameter as "HDRFinalScenePS" HDR tonemapping film curve shoulder scale (multiplied by 6.2, so 4*6.2=24.8)
	const float linearScale = HDRParams.y; // Same parameter as "HDRFinalScenePS" HDR tonemapping film curve midtones scale (multiplied by 0.5, so 1*0.5=0.5)
	const float toeScale = HDRParams.z; // Same parameter as "HDRFinalScenePS" HDR tonemapping film curve toe scale (multiplied by 0.06, so 1*0.06=0.06)
	static const float linearScaleDivisor = 1.7;

	// Apply tone mapping on the fly
	// LUMA FT: some kind of advanced custom Reinhard tonemapper, it doesn't seem to output any values > 1 with its parameters. This actually massively increase the average brightness.
	float3 compressedCol = LensOpticsTonemap(color.rgb, shoulderScale, linearScale, toeScale, linearScaleDivisor);

#if 0 // Test the inverse formula
	color.r = LensOpticsInverseTonemap(compressedCol.r, shoulderScale, linearScale, toeScale, linearScaleDivisor);
	color.g = LensOpticsInverseTonemap(compressedCol.g, shoulderScale, linearScale, toeScale, linearScaleDivisor);
	color.b = LensOpticsInverseTonemap(compressedCol.b, shoulderScale, linearScale, toeScale, linearScaleDivisor);
	compressedCol = LensOpticsTonemap(color.rgb, shoulderScale, linearScale, toeScale, linearScaleDivisor);
#endif

#if ENABLE_LENS_OPTICS_HDR // Enable inverse tonemapping to keep untonemapped colors beyond a certain threshold (most of them are not, so this barely does anything, but it works)
	if (allowHDR)
	{
		static const float midGrayOut = 0.5; // 0.5 because this is all supposedly in gamma space
		float midGrayIn = LensOpticsInverseTonemap(midGrayOut, shoulderScale, linearScale, toeScale, linearScaleDivisor);
		float3 adjustedCol = color.rgb * (midGrayOut / midGrayIn); // Adjust the brightness around the mid gray tonemapper scale
		compressedCol = color.rgb > midGrayIn ? adjustedCol : compressedCol;
		
#if 0
		// LUMA FT: to make these more "HDR" and brighter, we can multiply them by a fixed value.
		// In some scenes, it looks great, the problem with doing so is that some of these lens effects are simply full screen tints, so boosting their brightness can raise the tint,
		// but even if we excluded these, some alpha blends are very rough and only look good when done at low intensity, if boosting the color, you can clearly see their steps (the moment they start blending in).
		static const float AdditionalLensOpticsAmount = 0.5;
		compressedCol.rgb *= 1.0 + AdditionalLensOpticsAmount;
#endif
	}
#endif

#if 0 // LUMA FT: removed saturate()
	compressedCol = saturate(compressedCol);
#endif

	return float4( compressedCol, color.a ); // LUMA FT: this seems to output on a R11G11B10F texture in the vanilla game, alpha was ignored, and still is, even on R16G16B16A16F textures.
}