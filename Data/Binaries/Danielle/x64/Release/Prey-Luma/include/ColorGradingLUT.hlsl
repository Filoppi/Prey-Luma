#include "include/Common.hlsl"
#include "include/Oklab.hlsl"
#include "include/DarktableUCS.hlsl"

// Make sure to define these to your value, or set it to 0, so it retrieves the size from the LUT (in some functions)
#ifndef LUT_SIZE
#define LUT_SIZE 16u
#endif
#ifndef LUT_MAX
#define LUT_MAX (LUT_SIZE - 1u)
#endif
#ifndef LUT_3D
#define LUT_3D 0
#endif
// 0 None
// 1 Neutral LUT
// 2 Neutral LUT + bypass extrapolation
#ifndef FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE
#define FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE 0
#endif
#ifndef TEST_LUT_EXTRAPOLATION
#define TEST_LUT_EXTRAPOLATION 0
#endif

#if LUT_3D
#define LUT_TEXTURE_TYPE Texture3D
#else
#define LUT_TEXTURE_TYPE Texture2D
#endif

// NOTE: it's possible to add more of these, like PQ or Log3.
#define LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB 0
#define LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2 1
#define LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB_WITH_GAMMA_2_2_LUMINANCE 2
#define DEFAULT_LUT_EXTRAPOLATION_TRANSFER_FUNCTION LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2

uint3 ConditionalConvert3DTo2DLUTCoordinates(uint3 Coordinates3D, uint lutSize = LUT_SIZE)
{
#if LUT_3D
  return Coordinates3D;
#else
  return uint3(Coordinates3D.x + (Coordinates3D.z * lutSize), Coordinates3D.y, 0);
#endif
}

// WIP (rename if ever...)
#ifndef HIGH_QUALITY_ENCODING_TYPE
#define HIGH_QUALITY_ENCODING_TYPE 1
#endif

//TODOFT5: use Log instead of PQ? It's actually not making much difference
float3 Linear_to_PQ2(float3 LinearColor, int clampType = GCT_NONE)
{
#if HIGH_QUALITY_ENCODING_TYPE == 0
	return LinearColor;
#elif HIGH_QUALITY_ENCODING_TYPE == 1
	return Linear_to_PQ(LinearColor, clampType);
#else // HIGH_QUALITY_ENCODING_TYPE >= 2
	return linearToLog(LinearColor, clampType);
#endif
}
float3 PQ_to_Linear2(float3 ST2084Color, int clampType = GCT_NONE)
{
#if HIGH_QUALITY_ENCODING_TYPE == 0
	return ST2084Color;
#elif HIGH_QUALITY_ENCODING_TYPE == 1
	return PQ_to_Linear(ST2084Color, clampType);
#else // HIGH_QUALITY_ENCODING_TYPE >= 2
	return logToLinear(ST2084Color, clampType);
#endif
}

// 0 None
// 1 Reduce saturation and increase brightness until luminance is >= 0 (~gamut mapping)
// 2 Clip negative colors (makes luminance >= 0)
// 3 Snap to black
void FixColorGradingLUTNegativeLuminance(inout float3 col, uint type = 1)
{
  if (type <= 0) { return; }

  float luminance = GetLuminance(col.xyz);
  if (luminance < -FLT_MIN)
  {
    if (type == 1)
    {
      // Make the color more "SDR" (less saturated, and thus less beyond Rec.709) until the luminance is not negative anymore (negative luminance means the color was beyond Rec.709 to begin with, unless all components were negative).
      // This is preferrable to simply clipping all negative colors or snapping to black, because it keeps some HDR colors, even if overall it's still "black", luminance wise.
      // This should work even in case "positiveLuminance" was <= 0, as it will simply make the color black.
      float3 positiveColor = max(col.xyz, 0.0);
      float3 negativeColor = min(col.xyz, 0.0);
      float positiveLuminance = GetLuminance(positiveColor);
      float negativeLuminance = GetLuminance(negativeColor);
#pragma warning( disable : 4008 )
      float negativePositiveLuminanceRatio = positiveLuminance / -negativeLuminance;
#pragma warning( default : 4008 )
      negativeColor.xyz *= negativePositiveLuminanceRatio;
      col.xyz = positiveColor + negativeColor;
    }
    else if (type == 2)
    {
      // This can break gradients as it snaps colors to brighter ones (it depends on how the displays clips HDR10 or scRGB invalid colors)
      col.xyz = max(col.xyz, 0.0);
    }
    else //if (type >= 3)
    {
      col.xyz = 0.0;
    }
  }
}

// Restores the source color hue through Oklab (this works on colors beyond SDR in brightness and gamut too)
float3 RestoreHue(float3 targetColor, float3 sourceColor, float amount = 0.5)
{
  // Invalid or black colors fail oklab conversions or ab blending so early out
  if (GetLuminance(targetColor) <= FLT_MIN)
  {
    // Optionally we could blend the target towards the source, or towards black, but there's no need until proven otherwise
    return targetColor;
  }

  const float3 targetOklab = linear_srgb_to_oklab(targetColor);
  const float3 targetOklch = oklab_to_oklch(targetOklab);
  const float3 sourceOklab = linear_srgb_to_oklab(sourceColor);

  // First correct both hue and chrominance at the same time (oklab a and b determine both, they are the color xy coordinates basically).
  // As long as we don't restore the hue to a 100% (which should be avoided), this will always work perfectly even if the source color is pure white (or black, any "hueless" and "chromaless" color).
  // This method also works on white source colors because the center of the oklab ab diagram is a "white hue", thus we'd simply blend towards white (but never flipping beyond it (e.g. from positive to negative coordinates)),
  // and then restore the original chrominance later (white still conserving the original hue direction, so likely spitting out the same color as the original, or one very close to it).
  float3 correctedTargetOklab = float3(targetOklab.x, lerp(targetOklab.yz, sourceOklab.yz, amount));

  // Then restore chrominance
  float3 correctedTargetOklch = oklab_to_oklch(correctedTargetOklab);
  correctedTargetOklch.y = targetOklch.y;

  return oklch_to_linear_srgb(correctedTargetOklch);
}

// Takes any original color (before some post process is applied to it) and re-applies the same transformation the post process had applied to it on a different (but similar) color.
// The images are expected to have roughly the same mid gray.
// It can be used for example to apply any SDR LUT or SDR color correction on an HDR color.
float3 RestorePostProcess(const float3 nonPostProcessedTargetColor, const float3 nonPostProcessedSourceColor, const float3 postProcessedSourceColor, float hueRestoration = 0)
{
  static const float MaxShadowsColor = pow(1.f / 3.f, 2.2f); // The lower this value, the more "accurate" is the restoration (math wise), but also more error prone (e.g. division by zero)

	const float3 postProcessColorRatio = safeDivision(postProcessedSourceColor, nonPostProcessedSourceColor, 1);
	const float3 postProcessColorOffset = postProcessedSourceColor - nonPostProcessedSourceColor;
	const float3 postProcessedRatioColor = nonPostProcessedTargetColor * postProcessColorRatio;
	const float3 postProcessedOffsetColor = nonPostProcessedTargetColor + postProcessColorOffset;
	// Near black, we prefer using the "offset" (sum) pp restoration method, as otherwise any raised black would not work,
	// for example if any zero was shifted to a more raised color, "postProcessColorRatio" would not be able to replicate that shift due to a division by zero.
	float3 newPostProcessedColor = lerp(postProcessedOffsetColor, postProcessedRatioColor, max(saturate(abs(nonPostProcessedTargetColor / MaxShadowsColor)), saturate(abs(nonPostProcessedSourceColor / MaxShadowsColor))));

	// Force keep the original post processed color hue.
  // This often ends up shifting the hue too much, either looking too desaturated or too saturated, mostly because in SDR highlights are all burned to white by LUTs, and by the Vanilla SDR tonemappers.
	if (hueRestoration > 0)
	{
		newPostProcessedColor = RestoreHue(newPostProcessedColor, postProcessedSourceColor, hueRestoration);
	}

	return newPostProcessedColor;
}

// Encode.
// Set "mirrored" to true in case the input can have negative values,
// otherwise we run the non mirrored version that is more optimized but might have worse or broken results.
float3 ColorGradingLUTTransferFunctionIn(float3 col, uint transferFunction, bool mirrored = true)
{
  if (transferFunction == LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB)
  {
    return linear_to_sRGB_gamma(col, mirrored ? GCT_MIRROR : GCT_NONE);
  }
  else if (transferFunction == LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2)
  {
    return linear_to_gamma(col, mirrored ? GCT_MIRROR : GCT_NONE);
  }
  else // LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB_WITH_GAMMA_2_2_LUMINANCE
  {
    float3 gammaCorrectedColor = gamma_sRGB_to_linear(linear_to_gamma(col, mirrored ? GCT_MIRROR : GCT_NONE), mirrored ? GCT_MIRROR : GCT_NONE);
    return linear_to_sRGB_gamma(RestoreLuminance(col, gammaCorrectedColor), mirrored ? GCT_MIRROR : GCT_NONE);
  }
}
// Decode.
float3 ColorGradingLUTTransferFunctionOut(float3 col, uint transferFunction, bool mirrored = true)
{
  if (transferFunction == LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB)
  {
    return gamma_sRGB_to_linear(col, mirrored ? GCT_MIRROR : GCT_NONE);
  }
  else if (transferFunction == LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2)
  {
    return gamma_to_linear(col, mirrored ? GCT_MIRROR : GCT_NONE);
  }
  else // LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB_WITH_GAMMA_2_2_LUMINANCE
  {
    return RestoreLuminance(gamma_sRGB_to_linear(col, mirrored ? GCT_MIRROR : GCT_NONE), gamma_to_linear(col, mirrored ? GCT_MIRROR : GCT_NONE));
  }
}

// Use the LUT input transfer function within 0-1 and the LUT output transfer function beyond 0-1 (e.g. sRGB to gamma 2.2),
// this is because LUTs are baked with a gamma mismatch, but for extrapolation, we might only want to replicate the gamma mismatch within the 0-1 range.
float3 ColorGradingLUTTransferFunctionInCorrected(float3 col, uint transferFunctionIn, uint transferFunctionOut)
{
  if (transferFunctionIn != transferFunctionOut)
  {
    float3 reEncodedColor = ColorGradingLUTTransferFunctionIn(col, transferFunctionOut, true);
    float3 colorInExcess = reEncodedColor - saturate(reEncodedColor);
    return ColorGradingLUTTransferFunctionIn(saturate(col), transferFunctionIn, false) + colorInExcess;
  }
  return ColorGradingLUTTransferFunctionIn(col, transferFunctionIn, true);
}

// This perfectly mirrors "ColorGradingLUTTransferFunctionInCorrected()" (e.g. running this after that results in the original color).
float3 ColorGradingLUTTransferFunctionInCorrectedInverted(float3 col, uint transferFunctionIn, uint transferFunctionOut)
{
  if (transferFunctionIn != transferFunctionOut)
  {
    float3 reEncodedColor = ColorGradingLUTTransferFunctionOut(col, transferFunctionOut, true);
    float3 colorInExcess = reEncodedColor - saturate(reEncodedColor);
    return ColorGradingLUTTransferFunctionOut(saturate(col), transferFunctionIn, false) + colorInExcess;
  }
  return ColorGradingLUTTransferFunctionOut(col, transferFunctionIn, true);
}

// Use the LUT output transfer function within 0-1 and the LUT input transfer function beyond 0-1 (e.g. gamma 2.2 to sRGB),
// this is because LUTs are baked with a gamma mismatch, but we only want to replicate the gamma mismatch within the 0-1 range.
float3 ColorGradingLUTTransferFunctionOutCorrected(float3 col, uint transferFunctionIn, uint transferFunctionOut)
{
  if (transferFunctionIn != transferFunctionOut)
  {
    float3 reEncodedColor = ColorGradingLUTTransferFunctionOut(col, transferFunctionIn, true);
    float3 colorInExcess = reEncodedColor - saturate(reEncodedColor);
    return ColorGradingLUTTransferFunctionOut(saturate(col), transferFunctionOut, false) + colorInExcess;
  }
  return ColorGradingLUTTransferFunctionOut(col, transferFunctionOut, true);
}

// Optimized merged version of "ColorGradingLUTTransferFunctionInCorrected" and "ColorGradingLUTTransferFunctionOutCorrected".
// If "linearTolinear" is true, we assume linear in and out. Otherwise, we assume the input was encoded with "transferFunctionIn" and encode the output with "transferFunctionOut".
void ColorGradingLUTTransferFunctionInOutCorrected(inout float3 col, uint transferFunctionIn, uint transferFunctionOut, bool linearTolinear)
{
    if (transferFunctionIn != transferFunctionOut)
    {
      if (linearTolinear)
      {
        // E.g. decoding sRGB gamma with gamma 2.2 crushes blacks (which is what we want).
  #if 1 // Equivalent branches (this is the most optimized and most accurate)
        float3 colInExcess = col - saturate(col);
        col = ColorGradingLUTTransferFunctionOut(ColorGradingLUTTransferFunctionIn(saturate(col), transferFunctionIn, false), transferFunctionOut, false);
        col += colInExcess;
  #elif 1
        col = ColorGradingLUTTransferFunctionOutCorrected(ColorGradingLUTTransferFunctionIn(col, transferFunctionIn, true), transferFunctionIn, transferFunctionOut);
  #else
        col = ColorGradingLUTTransferFunctionOut(ColorGradingLUTTransferFunctionInCorrected(col, transferFunctionIn, transferFunctionOut), transferFunctionOut, true);
  #endif
      }
      else
      {
        // E.g. encoding "linear sRGB" with gamma 2.2 raises blacks (which is the opposite of what we want), so we do the opposite (encode "linear 2.2" with sRGB gamma).
  #if 1 // Equivalent branches (this is the most optimized and most accurate)
        float3 colInExcess = col - saturate(col);
        col = ColorGradingLUTTransferFunctionIn(ColorGradingLUTTransferFunctionOut(saturate(col), transferFunctionOut, false), transferFunctionIn, false);
        col += colInExcess;
  #elif 1
        col = ColorGradingLUTTransferFunctionIn(ColorGradingLUTTransferFunctionOutCorrected(col, transferFunctionIn, transferFunctionOut), transferFunctionIn, true);
  #else
        col = ColorGradingLUTTransferFunctionInCorrected(ColorGradingLUTTransferFunctionOut(col, transferFunctionOut, true), transferFunctionIn, transferFunctionOut);
  #endif
      }
    }
}

// Corrects transfer function encoded LUT coordinates to return more accurate LUT colors from linear in/out LUTs.
// This expects input coordinates within the 0-1 range (it should not be used to find the extrapolated (out of range) coordinates, but only on valid LUT coordinates).
float3 AdjustLUTCoordinatesForLinearLUT(const float3 clampedLUTCoordinatesGammaSpace, bool highQuality = true, uint lutTransferFunctionIn = DEFAULT_LUT_EXTRAPOLATION_TRANSFER_FUNCTION, bool lutInputLinear = false, bool lutOutputLinear = false, const float3 lutSize = LUT_SIZE, bool specifyLinearSpaceLUTCoordinates = false, float3 clampedLUTCoordinatesLinearSpace = 0)
{
	if (!specifyLinearSpaceLUTCoordinates)
	{
    clampedLUTCoordinatesLinearSpace = ColorGradingLUTTransferFunctionOut(clampedLUTCoordinatesGammaSpace, lutTransferFunctionIn, false);
	}
  if (lutInputLinear)
  {
#if FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE > 0
    if (highQuality && !lutOutputLinear)
    {
      // The "!lutOutputLinear" case would need coordinate adjustments to sample properly, but "linear in gamma out" LUTs don't really exist as they make no sense so we don't account for that case
    }
#endif
    return clampedLUTCoordinatesLinearSpace;
  }
	if (!lutOutputLinear || !highQuality)
	{
		return clampedLUTCoordinatesGammaSpace;
	}
	//if (!lutInputLinear && lutOutputLinear)
#if FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE > 0 // This case will skip LUT sampling so we shouldn't correct the input coordinates
  // Low quality version with no linear input correction
  return clampedLUTCoordinatesGammaSpace;
#else
//TODOFT4: when this branch runs, there's some speckles on the shotgun numerical decal in some scenes (e.g. when under a light, in front of the place where I tested AF on a decal a lot) (with DLSS they turn into black dots in the albedo view). Would this happen without "dev" settings enabled!? Probably not!
  // Given that we haven't scaled for the LUT half texel size, we floor and ceil with the LUT size as opposed to the LUT max
  float3 previousLUTCoordinatesGammaSpace = floor(clampedLUTCoordinatesGammaSpace * lutSize) / lutSize;
  float3 nextLUTCoordinatesGammaSpace = ceil(clampedLUTCoordinatesGammaSpace * lutSize) / lutSize;
  float3 previousLUTCoordinatesLinearSpace = ColorGradingLUTTransferFunctionOut(previousLUTCoordinatesGammaSpace, lutTransferFunctionIn, false);
  float3 nextLUTCoordinatesLinearSpace = ColorGradingLUTTransferFunctionOut(nextLUTCoordinatesGammaSpace, lutTransferFunctionIn, false);
  // Every step size is different as it depends on where we are within the transfer function range.
  const float3 stepSize = nextLUTCoordinatesLinearSpace - previousLUTCoordinatesLinearSpace;
  // If "stepSize" is zero (due to the LUT pixel coords being exactly an integer), whether alpha is zero or one won't matter as "previousLUTCoordinatesGammaSpace" and "nextLUTCoordinatesGammaSpace" will be identical.
  const float3 blendAlpha = safeDivision(clampedLUTCoordinatesLinearSpace - previousLUTCoordinatesLinearSpace, stepSize, 1);
  return lerp(previousLUTCoordinatesGammaSpace, nextLUTCoordinatesGammaSpace, blendAlpha);
#endif
}

// Color grading/charts tex lookup. Called "TexColorChart2D()" in Vanilla code.
float3 SampleLUT(LUT_TEXTURE_TYPE lut, SamplerState samplerState, float3 color, uint lutSize = LUT_SIZE, bool tetrahedralInterpolation = false, bool debugLutInputLinear = false, bool debugLutOutputLinear = false, uint debugLutTransferFunctionIn = DEFAULT_LUT_EXTRAPOLATION_TRANSFER_FUNCTION)
{
#if FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE > 0
  // Do not saturate() "color" on purpose
  if (debugLutInputLinear == debugLutOutputLinear)
  {
    return color;
  }
  return debugLutOutputLinear ? ColorGradingLUTTransferFunctionOut(color, debugLutTransferFunctionIn) : ColorGradingLUTTransferFunctionIn(color, debugLutTransferFunctionIn);
#endif // FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE > 0

	const uint chartDimUint = lutSize;
	const float chartDim	= (float)chartDimUint;
	const float chartDimSqr	= chartDim * chartDim;
	const float chartMax	= chartDim - 1.0;
	const uint chartMaxUint = chartDimUint - 1u;

  if (!tetrahedralInterpolation)
  {
#if LUT_3D
    const float scale = chartMax / chartDim;
    const float bias = 0.5 / chartDim;
    
    float3 lookup = saturate(color) * scale + bias;
    
    return lut.Sample(samplerState, lookup).rgb;
#else // !LUT_3D
    const float3 scale = float3(chartMax, chartMax, chartMax) / chartDim;
    const float3 bias = float3(0.5, 0.5, 0.0) / chartDim;

    float3 lookup = saturate(color) * scale + bias;
    
    // convert input color into 2d color chart lookup address
    float slice = lookup.z * chartDim;	
    float sliceFrac = frac(slice);	
    float sliceIdx = slice - sliceFrac;
    
    lookup.x = (lookup.x + sliceIdx) / chartDim;
    
    // lookup adjacent slices
    float3 col0 = lut.Sample(samplerState, lookup.xy).rgb;
    lookup.x += 1.0 / chartDim;
    float3 col1 = lut.Sample(samplerState, lookup.xy).rgb;

    // linearly blend between slices
    return lerp(col0, col1, sliceFrac); // LUMA FT: changed to be a lerp (easier to read)
#endif // LUT_3D
  }
  else // LUMA FT: added tetrahedral LUT interpolation (from Lilium) (note that this ignores the texture sampler)
  {
    // We need to clip the input coordinates as LUT texture samples below are not clamped.
    const float3 coords = saturate(color) * chartMax; // Pixel coords 

    // floorCoords are on [0,chartMaxUint]
    uint3 floorBaseCoords = coords;
    uint3 floorNextCoords = min(floorBaseCoords + 1u, chartMaxUint);
    
    // baseInd and nextInd are on [0,1]
    uint3 baseInd = floorBaseCoords;
    uint3 nextInd = floorNextCoords;

    // indV2 and indV3 are on [0,chartMaxUint]
    uint3 indV2;
    uint3 indV3;

    // fract is on [0,1]
    float3 fract = frac(coords);

    const float3 v1 = lut.Load(ConditionalConvert3DTo2DLUTCoordinates(baseInd, chartDimUint)).rgb;
    const float3 v4 = lut.Load(ConditionalConvert3DTo2DLUTCoordinates(nextInd, chartDimUint)).rgb;

    float3 f1, f2, f3, f4;

    [flatten]
    if (fract.r >= fract.g)
    {
      [flatten]
      if (fract.g >= fract.b)  // R > G > B
      {
        indV2 = uint3(1u, 0u, 0u);
        indV3 = uint3(1u, 1u, 0u);

        f1 = 1u - fract.r;
        f4 = fract.b;

        f2 = fract.r - fract.g;
        f3 = fract.g - fract.b;
      }
      else [flatten] if (fract.r >= fract.b)  // R > B > G
      {
        indV2 = uint3(1u, 0u, 0u);
        indV3 = uint3(1u, 0u, 1u);

        f1 = 1u - fract.r;
        f4 = fract.g;

        f2 = fract.r - fract.b;
        f3 = fract.b - fract.g;
      }
      else  // B > R > G
      {
        indV2 = uint3(0u, 0u, 1u);
        indV3 = uint3(1u, 0u, 1u);

        f1 = 1u - fract.b;
        f4 = fract.g;

        f2 = fract.b - fract.r;
        f3 = fract.r - fract.g;
      }
    }
    else
    {
      [flatten]
      if (fract.g <= fract.b)  // B > G > R
      {
        indV2 = uint3(0u, 0u, 1u);
        indV3 = uint3(0u, 1u, 1u);

        f1 = 1u - fract.b;
        f4 = fract.r;

        f2 = fract.b - fract.g;
        f3 = fract.g - fract.r;
      }
      else [flatten] if (fract.r >= fract.b)  // G > R > B
      {
        indV2 = uint3(0u, 1u, 0u);
        indV3 = uint3(1u, 1u, 0u);

        f1 = 1u - fract.g;
        f4 = fract.b;

        f2 = fract.g - fract.r;
        f3 = fract.r - fract.b;
      }
      else  // G > B > R
      {
        indV2 = uint3(0u, 1u, 0u);
        indV3 = uint3(0u, 1u, 1u);

        f1 = 1u - fract.g;
        f4 = fract.r;

        f2 = fract.g - fract.b;
        f3 = fract.b - fract.r;
      }
    }

    indV2 = min(floorBaseCoords + indV2, chartMax);
    indV3 = min(floorBaseCoords + indV3, chartMax);

    const float3 v2 = lut.Load(ConditionalConvert3DTo2DLUTCoordinates(indV2, chartDimUint)).rgb;
    const float3 v3 = lut.Load(ConditionalConvert3DTo2DLUTCoordinates(indV3, chartDimUint)).rgb;

    return (f1 * v1) + (f2 * v2) + (f3 * v3) + (f4 * v4);
  }
}

struct LUTExtrapolationData
{
  // The "HDR" color before or after tonemapping to the display capabilities (preferably before, to have more consistent results), it needs to be in the same range as the vanilla color (0.18 as mid gray), with values beyond 1 being out of vanila range (e.g. HDR as opposed to SDR).
  // In other words, this is the LUT input coordinate (once converted the LUT input transfer function).
  // Note that this can be in any color space (e.g. sRGB, scRGB, Rec.709, Rec.2020, ...), it's agnostic from that.
  float3 inputColor;
  
  // The vanilla color the game would have fed as LUT input (so usually after tonemapping, and SDR), it should roughly be in the 0-1 range (you can optionally manually saturate() this to make sure of that).
  // This is optional and only used if "vanillaLUTRestorationAmount" is > 0.
  float3 vanillaInputColor;
};

struct LUTExtrapolationSettings
{
  // Set to 0 to find it automatically
  uint lutSize;
  // Is the input color we pass in linear or encoded with a transfer function?
  // If false, the color is expectred to the in the "transferFunctionIn" space.
  bool inputLinear;
  // Does the LUT take linear or transfer function encoded input coordinates/colors?
  bool lutInputLinear;
  // Does the LUT output linear or transfer function encoded colors?
  bool lutOutputLinear;
  // Do we expect this function to output linear or transfer function encoded colors?
  bool outputLinear;
  // What transfer function the LUT used for its input coordinates, if it wasn't linear ("lutInputLinear" false)?
  // Note that this might still be used even if the LUT is linear in input, because the extrapolation logic needs to happen in perceptual space.
  uint transferFunctionIn;
  // What transfer function the LUT used for its output colors, if it wasn't linear ("lutOutputLinear" false)?
  // Note that if this is different from "transferFunctionIn", it doesn't mean that the LUT also directly applies a gamma mismatch within its colors (e.g. for an input of 0.1 it would could still return 0.1),
  // but that the LUT output color was intended to be visualized on a display that used this transfer function.
  // Leave this equal to "transferFunctionIn" if you want to completely ignore any possible transfer function mismatch correction (in case "lutInputLinear" and "lutOutputLinear" were true).
  // If this is different from "transferFunctionIn", then the code will apply a transfer function correction, even if the input or output are linear.
  // Many games use the sRGB transfer function for LUT input, but then they theoretically output gamma 2.2 (as they were developed on and for gamma 2.2 displays),
  // thus their gamma needs to be corrected for that, whether "outputLinear" was true not (set this to "LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2" to do the classic SDR gamma correction).
  // The transfer function correction only applies in the LUT range (0-1) and is ignored for colors out of range,
  // given that the transfer function mismatch in out of range values can go wild, and also because in the vanilla version the would have been clipped anyway
  // (this behaviour assumes both input and output were in the 0-1 range, which might not be true depending on the LUT transfer functions, but it's true in the ones we support).
  uint transferFunctionOut;
  // 0 Basic sampling
  // 1 Linear corrected sampling (if "lutOutputLinear" is false this is equal to "0", but if true, the LUT input coordinates need to be adjusted with the inverse of the transfer function, otherwise even a neutral LUT would shift colors that didn't fall precisely on a LUT texel)
  // 2 Linear corrected sampling + tetrahedral interpolation (it won't necessarily look better, especially with LUTs close to neutral)
  uint samplingQuality;
  // Basically an inverse LUT intensity setting.
  // How much we blend back towards the "neutral" LUT color (the unclamped source color (e.g. HDR)).
  // This has the same limitations of "inputTonemapToPeakWhiteNits" and should be used and not used in the same cases.
  // It's generally not suggested to use it as basically it undoes the LUT extrapolation, but if you have LUTs not far from being neutral,
  // you might set this to a smallish value and get better results (e.g. better hues).
  float neutralLUTRestorationAmount;
  // How much we blend back towards the vanilla LUT color (or hue).
  // It can be used to restore some of the vanilla hues on bright (or not bright) colors (they would likely be desaturated on highlights).
  // This adds one sample per pixel.
  float vanillaLUTRestorationAmount;

  // Enable or disable LUT extrapolation.
  // Use "neutralLUTRestorationAmount" to control the extrapolation intensity
  // (it wouldn't make sense to only partially extrapolate without scaling back the color intensity, otherwise LUT extrapolation would have an output range smaller than its input range).
  bool enableExtrapolation;
  // 0 Low (likely results in major hue shifts) (2 fixed samples per pixel)
  // 1 High (no major hue shifts) (1 fixed sample + 3 optional samples per pixel)
  // 2 Extreme (no major hue shifts, more accurately preserves the rate of change towards the edges of the original LUT (see "extrapolationQuality"), though it's often unnecessary) (1 fixed sample + 6 optional samples per pixel)
  uint extrapolationQuality;
  // LUT extrapolation works by taking more centered samples starting from the "clipped" LUT coordinates (in case the native ones were out of range).
  // This determines how much we go backwards towards the LUT center.
  // The value is supposed to be > 0 and <= 1, with 1 mapping to 50% centering (we shouldn't do any more than that or the extrapolation would not be accurate).
  // The smaller this value, the more "accurate" extrapolation will be, respecting more lawfully the way the LUT changed around its edges (as long as it ends up mapped beyond the center of the first and second texels).
  // The higher the value, the "smoother" the extrapolation will be, with gradients possibly looking nicer.
  float backwardsAmount;
  // What white level does the LUT have for its input coordinates (e.g. what's the expected brightness of an input color of 1 1 1?).
  // This value doesn't directly scale the brightness of the output but affects the logic of some internal math (e.g. tonemapping and transfer functions).
  // Ideally it would be set to the same brightness the developers of the LUTs had their screen set to, some good values for SDR LUTs are 80, 100 or 203.
  // Given that this is used as a scaler for PQ, using the Rec.709 white level of 100 nits is a good start, as that maps to ~50% of the PQ range.
  float whiteLevelNits;
  // If our input color was too high (and thus out of range, (e.g. beyond 0-1)), we can temporarily tonemap it to avoid the LUT extrapolation math going wild (e.g. too saturated, or hue shifted, or generating too strong highlights),
  // this is especially useful in the following conditions:
  //  -With LUTs that change colors a lot in brightness, especially towards the edges
  //  -When using lower "extrapolationQuality" modes
  //  -When feeding in an untonemapped input color (with values that can possibly go very high)
  // This should not be used in the following conditions:
  //  -With LUTs that change colors a lot in hue and saturation (it might still work)
  //  -With LUTs that at "clipped" (LUTs that reach their peak per axis values before its latest texel)
  //  -With LUTs that invert colors (the tonemapping logic isn't compatible with it increasingly higher input colors mapping to increasingly lower output colors)
  // This is relative to the "whiteLevelNits" and needs to be greater than it.
  // Tonemapping is disabled if this is <= 0.
  float inputTonemapToPeakWhiteNits;
  // How much we blend back towards the "clipped" LUT color.
  // This is different from the vanilla color, as it's sourced from the new (e.g. HDR) input color, but clipped the the LUT input coordinates range (0-1).
  // It can be used to hide some of the weird hues generated from too aggressive extrapolation (e.g. for overly bright input colors, or for the lower "extrapolationQuality" modes).
  float clampedLUTRestorationAmount;
  // LUT extrapolation can generate invalid colors (colors with a negative luminance) if the input color had values below 0,
  // this fixes them in the best possible way without altering their hue wherever possible.
  bool fixExtrapolationInvalidColors;
};

LUTExtrapolationData DefaultLUTExtrapolationData()
{
  LUTExtrapolationData data;
  data.vanillaInputColor = 0;
  return data;
}

LUTExtrapolationSettings DefaultLUTExtrapolationSettings()
{
  LUTExtrapolationSettings settings;
  settings.lutSize = LUT_SIZE;
  settings.inputLinear = true;
  settings.lutInputLinear = false;
  settings.lutOutputLinear = false;
  settings.outputLinear = true;
  settings.transferFunctionIn = DEFAULT_LUT_EXTRAPOLATION_TRANSFER_FUNCTION;
  settings.transferFunctionOut = DEFAULT_LUT_EXTRAPOLATION_TRANSFER_FUNCTION;
  settings.samplingQuality = 1;
  settings.neutralLUTRestorationAmount = 0;
  settings.vanillaLUTRestorationAmount = 0;
  settings.enableExtrapolation = true;
  settings.extrapolationQuality = 1;
  settings.backwardsAmount = 0.5;
  settings.whiteLevelNits = Rec709_WhiteLevelNits;
  settings.inputTonemapToPeakWhiteNits = 0;
  settings.clampedLUTRestorationAmount = 0;
  settings.fixExtrapolationInvalidColors = true;
  return settings;
}

float3 SampleLUT(LUT_TEXTURE_TYPE lut, SamplerState samplerState, float3 encodedCoordinates, LUTExtrapolationSettings settings, bool forceOutputLinear = false, bool specifyLinearColor = false, float3 linearCoordinates = 0)
{
  const bool highQualityLUTCoordinateAdjustments = settings.samplingQuality >= 1;
  const bool tetrahedralInterpolation = settings.samplingQuality >= 2;
  
#pragma warning( disable : 4000 ) // It's not clear why this function generates this error (sometimes?), maybe it's because we should add an else case for the return
  float3 sampleCoordinates = AdjustLUTCoordinatesForLinearLUT(encodedCoordinates, highQualityLUTCoordinateAdjustments, settings.transferFunctionIn, settings.lutInputLinear, settings.lutOutputLinear, settings.lutSize, specifyLinearColor, linearCoordinates);
  float3 color = SampleLUT(lut, samplerState, sampleCoordinates, settings.lutSize, tetrahedralInterpolation, settings.lutInputLinear, settings.lutOutputLinear, settings.transferFunctionIn);
  // We appply the transfer function even beyond 0-1 as if the color comes from a linear LUT, it shouldn't already have any kind of gamma correction applied to it (gamma correction runs later).
  if (!settings.lutOutputLinear && forceOutputLinear)
  {
			return ColorGradingLUTTransferFunctionOut(color, settings.transferFunctionIn, true);
  }
  return color;
#pragma warning( default : 4000 )
}

//TODOFT: store the acceleration around the lut's last texel in the alpha channel?

// LUT sample that allows to go beyond the 0-1 coordinates range through extrapolation.
// It finds the rate of change (acceleration) of the LUT color around the requested clamped coordinates, and guesses what color the sampling would have with the out of range coordinates.
// Extrapolating LUT by re-apply the rate of change has the benefit of consistency. If the LUT has the same color at (e.g.) uv 0.9 0.9 0.9 and 1.0 1.0 1.0, thus clipping to white (or black) earlier, the extrapolation will also stay clipped, preserving the artistic intention.
// Additionally, if the LUT had inverted colors or highly fluctuating colors or very hues shifted colors, extrapolation would work a lot better than a raw LUT out of range extraction with a luminance multiplier (or other similar simpler techniques).
// 
// This function allows the LUT to be in linear or transfer function encoded (e.g. gamma space) on input coordinates and output color separately.
// LUTs are expected to be of equal size on each axis (once unwrapped from 2D to 3D).
// LUT extrapolation works best on LUTs that are NOT "clipped" around their edges (e.g. if the 3 last texels on the red axis all map to 255 (in 8bit), LUT extrapolation would either end up also clipping (which was likely not intended in the vanilla LUT and would look bad in HDR), or extrapolating values after a clipped gradient, thus ending up with a gradient like 254 255 255 255 256)
float3 SampleLUTWithExtrapolation(LUT_TEXTURE_TYPE lut, SamplerState samplerState, LUTExtrapolationData data /*= DefaultLUTExtrapolationData()*/, LUTExtrapolationSettings settings /*= DefaultLUTExtrapolationSettings()*/)
{
	float3 lutMax3D;
	if (settings.lutSize == 0)
	{
		// LUT size in texels
		float lutWidth;
		float lutHeight;
#if LUT_3D
		float lutDepth;
		lut.GetDimensions(lutWidth, lutHeight, lutDepth);
		const float3 lutSize3D = float3(lutWidth, lutHeight, lutDepth);
#else
		lut.GetDimensions(lutWidth, lutHeight);
		lutWidth = sqrt(lutWidth); // 2D LUTs usually extend horizontally
		const float3 lutSize3D = float3(lutWidth, lutWidth, lutHeight);
#endif
		settings.lutSize = lutHeight;
		lutMax3D = lutSize3D - 1.0;
	}
	else
	{
		lutMax3D = settings.lutSize - 1u;
	}
	// The uv distance between the center of one texel and the next one (this is before applying the uv bias and scaling later on, that's done when sampling)
	float3 lutTexelRange = 1.0 / settings.lutSize;

  // Theoretically these input colors match the output of a "neutral" LUT, so we call like that for clarity
	float3 neutralLUTColorLinear = data.inputColor;
	float3 neutralLUTColorTransferFunctionEncoded = data.inputColor;
	float3 neutralVanillaColorLinear = data.vanillaInputColor;
	float3 neutralVanillaColorTransferFunctionEncoded = data.vanillaInputColor;

  // Here we need to pick an encoding for the 0-1 range, and one for the range beyond that.
  // For example, sRGB gamma doesn't really make sense beyond the 0-1 range (especially below 0), so it's not exactly compatible with scRGB colors (that go to negative values to represent colors beyond sRGB),
	// but either way, whether we use gamma 2.2 or sRGB encoding beyond the 0-1 range doesn't make that much difference, as neither of the two choices are "correct" or great,
	// using 2.2 might be a bit closer to human perception below 0 than sRGB, while sRGB might be closer to human perception beyond 1 than 2.2, so we can pick whatever is best for your case to increase the quality of extrapolation.
	// We still need to apply gamma correction on output anyway, this doesn't really influence that, it just makes parts of the extrapolation more perception friendly.
  // At the moment we simply use the LUT in transfer function for the whole range, as it's simple and tests shows it works fine.
	if (settings.inputLinear)
	{
		neutralLUTColorTransferFunctionEncoded = ColorGradingLUTTransferFunctionIn(neutralLUTColorLinear, settings.transferFunctionIn);
		neutralVanillaColorTransferFunctionEncoded = ColorGradingLUTTransferFunctionIn(neutralVanillaColorLinear, settings.transferFunctionIn);
	}
	else
	{
		neutralLUTColorLinear = ColorGradingLUTTransferFunctionOut(neutralLUTColorTransferFunctionEncoded, settings.transferFunctionIn);
		neutralVanillaColorLinear = ColorGradingLUTTransferFunctionOut(neutralVanillaColorTransferFunctionEncoded, settings.transferFunctionIn);
	}
	const float3 clampedNeutralLUTColorLinear = saturate(neutralLUTColorLinear);

  // Whether the LUT takes linear inputs or not, we encode the input coordinates with the specified input transfer function,
  // so we can later use the perceptual space UVs to run some extrapolation logic.
  // These LUT coordinates are in the 0-1 range (or beyond that), without acknowleding the lut size or lut max (like the half texel around each edge).
	// We purposely don't use "neutralLUTColorLinearTonemapped" here as we want the raw input color.
	const float3 unclampedUV = neutralLUTColorTransferFunctionEncoded;
	const float3 clampedUV = saturate(unclampedUV);
	const float distanceFromUnclampedToClampedUV = length(unclampedUV - clampedUV);
  // Some threshold is needed to avoid divisions by tiny numbers.
  // Ideally this check is enough to avoid black dots in output due to normalizing smallish vectors, if not, increase the threshold value (e.g. to FLT_EPSILON).
	const bool uvOutOfRange = distanceFromUnclampedToClampedUV > FLT_MIN;
  const bool doExtrapolation = settings.enableExtrapolation && uvOutOfRange;
  // The current working space of this function (all colors samples from LUTs need to be in this space, whether they natively already were or not).
  // All rgb colors within the extrapolation branch need to be in linear space (and so are the ones that will come out of it)
	bool lutOutputLinear = settings.lutOutputLinear || doExtrapolation;

  // Use "clampedUV" instead of "unclampedUV" as we don't know what kind of sampler was in use here (it's probably clamped)
	float3 clampedSample = SampleLUT(lut, samplerState, clampedUV, settings, lutOutputLinear, true, clampedNeutralLUTColorLinear);
  float3 outputSample = clampedSample;
  
	if (doExtrapolation)
	{
    float3 neutralLUTColorLinearTonemapped = neutralLUTColorLinear;
    float3 neutralLUTColorLinearTonemappedRestoreRatio = 1;
    // Tonemap colors beyond the 0-1 range (we don't touch colors within the 0-1 range), tonemapping will be inverted later
    if (settings.inputTonemapToPeakWhiteNits > 0)
    {
      const float maxExtrapolationColor = max((settings.inputTonemapToPeakWhiteNits / settings.whiteLevelNits) - 1.0, FLT_MIN);
      const float3 neutralLUTColorInExcessLinear = neutralLUTColorLinear - clampedNeutralLUTColorLinear;
      // Tonemap it with a basic Reinhard (we could do something better but it likely wouldn't improve the results much)
// We can either tonemap by channel or by max channel. Tonemapping by luminance here isn't a good idea because we are interested in reducing the range to a specific max channel value.
#if 1 // By max channel (hue conserving (at least in the color in excess of 0-1), but has inconsistent results depending on the luminance)
//TODOFT: this is causing incontiguous gradients!!! (it's neutralLUTColorLinearTonemappedRestoreRatio)
      float normalizedNeutralLUTColorInExcessLinear = max3(abs(neutralLUTColorInExcessLinear / maxExtrapolationColor));
      float normalizedNeutralLUTColorInExcessLinearTonemapped = normalizedNeutralLUTColorInExcessLinear / (normalizedNeutralLUTColorInExcessLinear + 1);
      float normalizedNeutralLUTColorInExcessLinearRestoreRatio = safeDivision(normalizedNeutralLUTColorInExcessLinearTonemapped, normalizedNeutralLUTColorInExcessLinear, 1);
      float3 neutralLUTColorInExcessLinearTonemapped = neutralLUTColorInExcessLinear * normalizedNeutralLUTColorInExcessLinearRestoreRatio;
      neutralLUTColorLinearTonemappedRestoreRatio = safeDivision(1.0, normalizedNeutralLUTColorInExcessLinearRestoreRatio, 1);
#else // By channel
      float3 normalizedNeutralLUTColorInExcessLinear = abs(neutralLUTColorInExcessLinear / maxExtrapolationColor);
      float3 neutralLUTColorInExcessLinearTonemapped = (normalizedNeutralLUTColorInExcessLinear / (normalizedNeutralLUTColorInExcessLinear + 1)) * maxExtrapolationColor * sign(neutralLUTColorInExcessLinear);
      neutralLUTColorLinearTonemappedRestoreRatio = safeDivision(neutralLUTColorInExcessLinear, neutralLUTColorInExcessLinearTonemapped, 1);
#endif
      neutralLUTColorLinearTonemapped = clampedNeutralLUTColorLinear + neutralLUTColorInExcessLinearTonemapped;
    }

    // While "centering" the UVs, we need to go backwards by a specific amount.
    // Going back 50% (e.g. from LUT coordinates 1 to 0.5, or 0 to 0.5) can be too much, so we should generally keep it lower than that.
    // Anything lower than 25% will be more accurate but prone to extrapolation looking more aggressive.
		float backwardsAmount = settings.backwardsAmount * 0.5;
// Extrapolation shouldn't run with a "backwards amount" smaller than half a texel, otherwise it will be almost like sampling the edge coordinates again.
// This is already explained in the settings description so we disabled the safety check.
#if 0
    if (backwardsAmount < lutTexelRange)
    {
      backwardsAmount = lutTexelRange;
    }
#endif

		const float PQNormalizationFactor = HDR10_MaxWhiteNits / settings.whiteLevelNits;

		const float3 clampedUV_PQ = Linear_to_PQ2(clampedNeutralLUTColorLinear / PQNormalizationFactor); // "clampedNeutralLUTColorLinear" is equal to "ColorGradingLUTTransferFunctionOut(clampedUV, settings.transferFunctionIn, false)"
		const float3 unclampedTonemappedUV_PQ = Linear_to_PQ2(neutralLUTColorLinearTonemapped / PQNormalizationFactor, GCT_MIRROR);
		const float3 clampedSample_PQ = Linear_to_PQ2(clampedSample / PQNormalizationFactor, GCT_MIRROR);
		const float3 clampedUV_UCS = DarktableUcs::RGBToUCSLUV(clampedNeutralLUTColorLinear);
		const float3 unclampedTonemappedUV_UCS = DarktableUcs::RGBToUCSLUV(neutralLUTColorLinearTonemapped);
		const float3 clampedSample_UCS = DarktableUcs::RGBToUCSLUV(clampedSample);
    
#pragma warning( default : 4000 )
		float3 extrapolatedSample;

    // Here we do the actual extrapolation logic, which is relatively different depending on the quality mode.
    // LUT extrapolation lerping is best run in perceptual color space instead of linear space.
    // We settled for using PQ after long tests, here's a comparison of all of them: 
    // -PQ allows for a very wide range, it's relatively cheap, and simple to use.
    // -sRGB or gamma 2.2 falters in the range beyond 1, as they were made for SDR.
    // -Oklab/Oklch or Darktable UCS can work, but they seem to break on very bright colors, and are harder to control
    //  (it's hard to find the actual ratio of change for the extrapolation, they easily create invalid colors or broken gradients, and their hue is very hard to control).
    // -Linear just can't work for LUT extrapolation, because it would act very differently depending on the extrapolation direction (e.g. beyond 1 or below 0), given that it's not adjusted by perceptual
    //  (e.g.1 the extrapolation strength between -0.01 and 0.01 or 0.99 and 1.01 would be massively different, even if both of them have the same offset)
		//  (e.g.2 if the LUT sampling coordinates are 1.1, we'd want to extrapolate ~10% more color, but in linear space it would be a lot less than that, thus the peak brightness would be compressed a lot more than it should).
		if (settings.extrapolationQuality <= 0) //TODOFT: muke oklab and also fix this not extrapolating contiguously... depending on the backwards factor
		{
      // Take the direction between the clamped and unclamped coordinates, flip it, and use it to determine how much to go backwards by when taking the centered sample.
      // For example, if our "centeringNormal" is -1 -1 -1, we'd want to go backwards by our fixed amount, but multiplied by sqrt(3) (the lenght of a cube internal diagonal),
      // while of -1 -1 0, we'd only want to go back by sqrt(2) (the length of a side diagonal), etc etc. This helps keep the centering results consistent independently of their "angle".
		  const float3 centeringNormal = normalize(unclampedUV - clampedUV); // This should always be valid as "unclampedUV" and guaranteed to be different from "clampedUV".
      const float3 centeringNormalAbs = abs(centeringNormal);
      const float lutBackwardsDiagonalMultiplier = centeringNormalAbs.x + centeringNormalAbs.y + centeringNormalAbs.z; //TODOFT: this is unnecessary? it moves the vector in the same direction twice!??

			const float3 centeredUV = clampedUV - (centeringNormal * backwardsAmount * lutBackwardsDiagonalMultiplier);
			float3 centeredSample = SampleLUT(lut, samplerState, centeredUV, settings, lutOutputLinear);
			float3 centeredSample_PQ = Linear_to_PQ2(centeredSample / PQNormalizationFactor, GCT_MIRROR);
			float3 centeredUV_PQ = Linear_to_PQ2(ColorGradingLUTTransferFunctionOut(centeredUV, settings.transferFunctionIn, false) / PQNormalizationFactor);

			const float distanceFromUnclampedToClampedUV_PQ = length(unclampedTonemappedUV_PQ - clampedUV_PQ);
			const float distanceFromClampedToCenteredUV_PQ = length(clampedUV_PQ - centeredUV_PQ);
			const float extrapolationRatio = safeDivision(distanceFromUnclampedToClampedUV_PQ, distanceFromClampedToCenteredUV_PQ, 0);
			extrapolatedSample = PQ_to_Linear2(lerp(centeredSample_PQ, clampedSample_PQ, 1.0 + extrapolationRatio), GCT_MIRROR) * PQNormalizationFactor;

#if DEVELOPMENT && 1
    bool oklab = LumaSettings.DevSetting06 >= 0.5;
#else
    bool oklab = false;
#endif
      if (oklab) //TODOFT4: try oklab again? (update the starfield code ok extrapolation and oklab) And fix up oklab+PQ description above. Also try per channel (quality 1+) and try UCS.
      {
#if 0
#define LINEAR_TO_OKLCH(x) DarktableUcs::RGBToUCSLUV(x)
#define OKLCH_TO_LINEAR(x) DarktableUcs::UCSLUCToRGB(x)
#else
#define LINEAR_TO_OKLCH(x) linear_srgb_to_oklab(x)
#define OKLCH_TO_LINEAR(x) oklch_to_linear_srgb(x)
#endif
        // OKLAB/OKLCH (it doesn't really look good, it limits the saturation too much, and though it retains vanilla hues more accurately, it just doesn't look that good, and it breaks on high luminances)
        float3 unclampedUVOklch = LINEAR_TO_OKLCH(neutralLUTColorLinear);
        float3 clampedUVOklch = LINEAR_TO_OKLCH(clampedNeutralLUTColorLinear);
        float3 centeredUVOklch = LINEAR_TO_OKLCH(ColorGradingLUTTransferFunctionOut(centeredUV, settings.transferFunctionIn, false));
        
        const float3 distanceFromUnclampedToClampedOklch = unclampedUVOklch - clampedUVOklch;
        const float3 distanceFromClampedToCenteredOklch = clampedUVOklch - centeredUVOklch;
        const float3 extrapolationRatioOklch = safeDivision(distanceFromUnclampedToClampedOklch, distanceFromClampedToCenteredOklch, 0); //TODOFT: 0 or 1 on safe div? // This has borked uncontiguous values on x y and z, in oklab and oklch...
        const float distanceFromUnclampedToClampedOklch2 = length(unclampedUVOklch.yz - clampedUVOklch.yz);
        const float distanceFromClampedToCenteredOklch2 = length(clampedUVOklch.yz - centeredUVOklch.yz);
        const float extrapolationRatioOklch2 = safeDivision(distanceFromUnclampedToClampedOklch2, distanceFromClampedToCenteredOklch2, 0);

        float3 derivedLUTColor = LINEAR_TO_OKLCH(clampedSample);
        float3 derivedLUTCenteredColor = LINEAR_TO_OKLCH(centeredSample);
        float3 derivedLUTColorChangeOffset = derivedLUTColor - derivedLUTCenteredColor;
        // Reproject the centererd color change ratio onto the full range
#if 0
        //float3 extrapolatedDerivedLUTColorChangeOffset = derivedLUTColorChangeOffset * extrapolationRatioOklch;
        //float3 extrapolatedDerivedLUTColorChangeOffset = derivedLUTColorChangeOffset * abs(extrapolationRatioOklch);
        float3 extrapolatedDerivedLUTColorChangeOffset = derivedLUTColorChangeOffset * float3(abs(extrapolationRatioOklch.x), extrapolationRatioOklch2.xx);
        //float3 extrapolatedDerivedLUTColorChangeOffset = derivedLUTColorChangeOffset * float3(abs(extrapolationRatioOklch.x), extrapolationRatioOklch2.xx);
#elif 1
        float3 extrapolatedDerivedLUTColorChangeOffset = derivedLUTColorChangeOffset * extrapolationRatio;
#else
        float3 extrapolatedDerivedLUTColorChangeOffset = derivedLUTColorChangeOffset * float3(abs(distanceFromUnclampedToClampedOklch.x), length(distanceFromUnclampedToClampedOklch.yz).xx);
#endif
  #if LUT_EXTRAPOLATION_DESATURATE >= 1 // Not a desaturate here (simply conserve mode hue), but it achieves similar results
        // only recover hue at 50%? Given that otherwise we use the one clipped from "SDR" with the wrong rgb ratio (e.g. if we try to extrapolate 5 3 2, it will clip to 1 1 1, so there won't be any hue...)?
        //extrapolatedDerivedLUTColorChangeOffset.z *= 0.5;
        //extrapolatedDerivedLUTColorChangeOffset.y *= 1.0 / 3.0;
        extrapolatedDerivedLUTColorChangeOffset.yz *= 2.0 / 3.0;
  #endif
        //extrapolatedDerivedLUTColorChangeOffset.yz *= 2.5;
        //extrapolatedDerivedLUTColorChangeOffset.yz *= 0.2;
        //extrapolatedDerivedLUTColorChangeOffset.x *= 0.2;

        //float3 extrapolatedDerivedLUTColor = derivedLUTColor + float3(0, extrapolatedDerivedLUTColorChangeOffset.yz);
        float3 extrapolatedDerivedLUTColor = derivedLUTColor + extrapolatedDerivedLUTColorChangeOffset;
        //float3 extrapolatedDerivedLUTColor = lerp(derivedLUTCenteredColor, derivedLUTColor, 1.0 + extrapolationRatioOklch);
        // Avoid negative luminance. This can happen in case "derivedLUTColorChangeOffset" intensity/luminance was negative, even if we were at a bright/colorful LUT edge,
        // especially if the input color is extremely bright. We can't really fix the color from ending up as black though, unless we find a way to auto detect it.
        extrapolatedDerivedLUTColor.x = max(extrapolatedDerivedLUTColor.x, 0.f);

        derivedLUTColor = oklab_to_oklch(derivedLUTColor);
        derivedLUTCenteredColor = oklab_to_oklch(derivedLUTCenteredColor);
        extrapolatedDerivedLUTColor = oklab_to_oklch(extrapolatedDerivedLUTColor);

#if DEVELOPMENT
        // Avoid flipping ab direction, if we reached white, stay on white.
        // We only do it on colors that have some chroma and brightness.
        if (LumaSettings.DevSetting05 >= 0.5)
        {
          // do abs() before fmod() for safety
          if ((fmod(abs(extrapolatedDerivedLUTColor.z - derivedLUTColor.z), PI * 2.0) >= PI / 1.f)
              && extrapolatedDerivedLUTColor.x > 0.f && derivedLUTColor.x > 0.f
              && extrapolatedDerivedLUTColor.y > 0.f && derivedLUTColor.y > 0.f)
          {
            extrapolatedDerivedLUTColor.z = derivedLUTColor.z;
            extrapolatedDerivedLUTColor.y = 0.f;
          }
        }
#endif

        unclampedUVOklch = oklab_to_oklch(unclampedUVOklch);
        clampedUVOklch = oklab_to_oklch(clampedUVOklch);
        centeredUVOklch = oklab_to_oklch(centeredUVOklch);
        const float3 distanceFromUnclampedToClampedOklch3 = unclampedUVOklch - clampedUVOklch;
        const float3 distanceFromClampedToCenteredOklch3 = clampedUVOklch - centeredUVOklch;
        const float3 extrapolationRatioOklch3 = safeDivision(distanceFromUnclampedToClampedOklch3, distanceFromClampedToCenteredOklch3, 0);
        float3 derivedLUTColorChangeOffset3 = derivedLUTColor - derivedLUTCenteredColor;
        //float3 extrapolatedDerivedLUTColorChangeOffset3 = derivedLUTColorChangeOffset3 * abs(extrapolationRatioOklch3);
        //float3 extrapolatedDerivedLUTColorChangeOffset3 = derivedLUTColorChangeOffset3 * float3(abs(extrapolationRatioOklch3.x), extrapolationRatioOklch2.xx);
        float3 extrapolatedDerivedLUTColorChangeOffset3 = derivedLUTColorChangeOffset3 * extrapolationRatio;
        float3 derivedLUTColorExtrap3 = derivedLUTColor + extrapolatedDerivedLUTColorChangeOffset3;
#if 0
        extrapolatedDerivedLUTColor.y = derivedLUTColorExtrap3.y;
        //extrapolatedDerivedLUTColor.xy = derivedLUTColorExtrap3.xy;
#endif

        // Avoid negative chroma, as it would likely flip the hue. Theoretically this breaks the accuracy of some "LUTExtrapolationColorSpace" modes but the results would be visually bad without it.
        //extrapolatedDerivedLUTColor.y = max(extrapolatedDerivedLUTColor.y, 0.f);
        // Mirror hue (not sure this would be automatically done when converting back from oklch to sRGB)
        //extrapolatedDerivedLUTColor.z = abs(extrapolatedDerivedLUTColor.z); // looks worse, hue can go negative...
        //extrapolatedDerivedLUTColor.yz = max(extrapolatedDerivedLUTColor.yz, -3.f);
        //extrapolatedDerivedLUTColor.yz = min(extrapolatedDerivedLUTColor.yz, 3.f);

        // Keep other extrapolation luminance
        float extrapolatedSampleLuminance = GetLuminance(extrapolatedSample);
        
        // Shift luminance and chroma to the extrapolated values, keep the original LUT edge hue (we can't just apply the same hue change, hue isn't really scalable).
        // This has problems in case the LUT color was white, so basically the hue is picked at random.
        extrapolatedSample = oklch_to_linear_srgb(extrapolatedDerivedLUTColor.xyz);
        //extrapolatedSample = oklab_to_linear_srgb(extrapolatedDerivedLUTColor.xyz); // OKLCH_TO_LINEAR?
        //extrapolatedSample = oklab_to_linear_srgb(float3(extrapolatedDerivedLUTColor.x, derivedLUTColor.yz)); // OKLCH_TO_LINEAR?
  #if 0 // Looks bad without this
        extrapolatedSample = oklch_to_linear_srgb(float3(extrapolatedDerivedLUTColor.xy, derivedLUTColor.z));
  #endif
        //extrapolatedSample = OKLCH_TO_LINEAR(float3(extrapolatedDerivedLUTColor.x, derivedLUTColor.yz));
        //extrapolatedSample = OKLCH_TO_LINEAR(float3(derivedLUTColor.x, extrapolatedDerivedLUTColor.y, derivedLUTColor.z));
        //extrapolatedSample = OKLCH_TO_LINEAR(float3(derivedLUTColor.xy, extrapolatedDerivedLUTColor.z));
        //extrapolatedSample = abs(extrapolationRatioOklch);

#if 0
      float extrapolatedSampleLuminance2 = GetLuminance(extrapolatedSample);
			extrapolatedSample = extrapolatedSample * lerp(1.0, max(safeDivision(extrapolatedSampleLuminance, extrapolatedSampleLuminance2, 1), 0.0), 0.5); //50%
#endif
      }
		}
		else //if (settings.extrapolationQuality >= 1)
		{
      // We always run the UV centering logic in the vanilla transfer function space (e.g. sRGB), not PQ, as all these transfer functions are reliable enough within the 0-1 range.
			float3 centeredUV = clampedUV + (backwardsAmount * (clampedUV >= 0.5 ? -1 : 1));
			float3 centeredSamples[3] = { clampedSample, clampedSample, clampedSample };
			float3 centeredSamples_PQ[3] = { clampedSample_PQ, clampedSample_PQ, clampedSample_PQ };
		  const float3 clampedSample_UCS = DarktableUcs::RGBToUCSLUV(clampedSample);
			float3 centeredSamples_UCS[3] = { clampedSample_UCS, clampedSample_UCS, clampedSample_UCS };

#if 1
      const bool secondSampleLessCentered = backwardsAmount > (0.25 + FLT_EPSILON);
			const float backwardsAmount_2 = secondSampleLessCentered ? (backwardsAmount / 2) : (backwardsAmount * 2); // Go in the most sensible direction
			float3 centeredUV_2 = clampedUV + (backwardsAmount_2 * (clampedUV >= 0.5 ? -1 : 1));
#else // This might be more accurate, though it might be more aggressive, and fails to extrapolate properly in case the user set "backwardsAmount" was too close to "lutTexelRange", or if the LUT clipped to the max value before its edges.
      const bool secondSampleLessCentered = backwardsAmount > lutTexelRange;
			const float backwardsAmount_2 = secondSampleLessCentered ? lutTexelRange : (backwardsAmount * 2);
			float3 centeredUV_2 = clampedUV + (backwardsAmount_2 * (clampedUV >= 0.5 ? -1 : 1));
#endif
			float3 centeredSamples_2[3] = { clampedSample, clampedSample, clampedSample };
			float3 centeredSamples_PQ_2[3] = { clampedSample_PQ, clampedSample_PQ, clampedSample_PQ };

      // Swap them to avoid having to write more branches below,
      // the second (2) sample is always meant to be closer to the edges (less centered).
      if (settings.extrapolationQuality >= 2 && !secondSampleLessCentered)
      {
        float3 tempCenteredUV = centeredUV;
        centeredUV = centeredUV_2;
        centeredUV_2 = tempCenteredUV;
      }

      float3 centeredUV_PQ = Linear_to_PQ2(ColorGradingLUTTransferFunctionOut(centeredUV, settings.transferFunctionIn, false) / PQNormalizationFactor);
      float3 centeredUV_UCS = DarktableUcs::RGBToUCSLUV(ColorGradingLUTTransferFunctionOut(centeredUV, settings.transferFunctionIn, false));
      float3 centeredUVs_UCS[3] = { centeredUV_UCS, centeredUV_UCS, centeredUV_UCS };

      [unroll]
			for (uint i = 0; i < 3; i++)
			{
				if (unclampedUV[i] != clampedUV[i]) // Optional optimization to avoid taking samples that won't be used
				{
					float3 localCenteredUV = float3(i == 0 ? centeredUV.r : clampedUV.r, i == 1 ? centeredUV.g : clampedUV.g, i == 2 ? centeredUV.b : clampedUV.b);
					centeredSamples[i] = SampleLUT(lut, samplerState, localCenteredUV, settings, lutOutputLinear);
					centeredSamples_PQ[i] = Linear_to_PQ2(centeredSamples[i] / PQNormalizationFactor, GCT_MIRROR);
					centeredSamples_UCS[i] = DarktableUcs::RGBToUCSLUV(centeredSamples[i]);
					centeredUVs_UCS[i] = DarktableUcs::RGBToUCSLUV(ColorGradingLUTTransferFunctionOut(localCenteredUV, settings.transferFunctionIn, false));

          // The highest quality takes more samples and then "averages" them later
					if (settings.extrapolationQuality >= 2)
					{
						localCenteredUV = float3(i == 0 ? centeredUV_2.r : clampedUV.r, i == 1 ? centeredUV_2.g : clampedUV.g, i == 2 ? centeredUV_2.b : clampedUV.b);
						centeredSamples_2[i] = SampleLUT(lut, samplerState, localCenteredUV, settings, lutOutputLinear);
						centeredSamples_PQ_2[i] = Linear_to_PQ2(centeredSamples_2[i] / PQNormalizationFactor, GCT_MIRROR);
					}
				}
			}

#if 0 // OLD
      // Find the "velocity", or "rate of change" of the color.
      // This isn't simply an offset, it's an offset (the lut sample colors difference) normalized by another offset (the uv coordinates difference),
      // so it's basically the speed with which color changes at this point in the LUT.
			float3 rgbRatioSpeed = safeDivision(clampedSample_PQ - float3(centeredSamples_PQ[0][0], centeredSamples_PQ[1][1], centeredSamples_PQ[2][2]), clampedUV_PQ - centeredUV_PQ);
      float3 rgbRatioAcceleration = 0;
      // Extreme quality: use two extrapolation samples per channel
      // Note that it would be possibly to do the same thing with 3+ channels too, but further samples would have diminishing returns and not help at all in 99% of cases.
			if (settings.extrapolationQuality >= 2)
			{
				float3 centeredUV_PQ_2 = Linear_to_PQ2(ColorGradingLUTTransferFunctionOut(centeredUV_2, settings.transferFunctionIn, false) / PQNormalizationFactor);
#if 1
        // Find the acceleration of each color channel as the LUT coordinates move towards the (external) edge.
        // The second (2) sample is always more external, so it's "newer" if we consider time.
			  rgbRatioSpeed = safeDivision(float3(centeredSamples_PQ_2[0][0], centeredSamples_PQ_2[1][1], centeredSamples_PQ_2[2][2]) - float3(centeredSamples_PQ[0][0], centeredSamples_PQ[1][1], centeredSamples_PQ[2][2]), centeredUV_PQ_2 - centeredUV_PQ);
				float3 rgbRatioSpeed_2 = safeDivision(clampedSample_PQ - float3(centeredSamples_PQ_2[0][0], centeredSamples_PQ_2[1][1], centeredSamples_PQ_2[2][2]), clampedUV_PQ - centeredUV_PQ_2);
#if 1 // Theoretically the best version, though it's very aggressive //TODOFT4
        rgbRatioAcceleration = safeDivision(rgbRatioSpeed_2 - rgbRatioSpeed, abs(clampedUV_PQ - centeredUV_PQ) / 1.0);
        rgbRatioSpeed = rgbRatioSpeed_2; // Set the latest velocity we found as the final velocity (this is the velocity we'll start from at the edge of the LUT, before adding acceleration)
#elif 0
        // Make an approximate prediction of what the next speed will be, based on the previous two samples (this doesn't consider for how long we travelled at that speed)
        rgbRatioSpeed = rgbRatioSpeed_2 + (rgbRatioSpeed_2 - rgbRatioSpeed);
#elif 1
        // Find the average of the two speeds, hoping they were going in roughly the same direction (otherwise this might make extrapolation go towards an incorrect direction)
				rgbRatioSpeed = lerp(rgbRatioSpeed, rgbRatioSpeed_2, 0.5);
#endif
#else // Smoother fallback case that doesn't use acceleration
        // Find the mid point between the two centered samples we had, to smooth out any inconsistencies and have a result that is closer to what would be expected by the ratio of change around the LUT edges.
        float3 centeredSamples_PQAverage = lerp(float3(centeredSamples_PQ[0][0], centeredSamples_PQ[1][1], centeredSamples_PQ[2][2]), float3(centeredSamples_PQ_2[0][0], centeredSamples_PQ_2[1][1], centeredSamples_PQ_2[2][2]), 0.5);
        float3 centeredUV_PQAverage = lerp(centeredUV_PQ, centeredUV_PQ_2, 0.5);
				rgbRatioSpeed = safeDivision(clampedSample_PQ - centeredSamples_PQAverage, clampedUV_PQ - centeredUV_PQAverage);
#endif
			}
      
      // Find the actual extrapolation "time", we'll travel away from the LUT edge for this "duration"
			float3 extrapolationRatio = unclampedTonemappedUV_PQ - clampedUV_PQ;
      
      // Calculate the final extrapolation offset (a "distance") from "speed" and "time"
			float3 extrapolatedOffset = rgbRatioSpeed * extrapolationRatio;
      // Higher quality modes use "acceleration" as opposed to "speed" only
      if (settings.extrapolationQuality >= 2)
			{
        // We are using the basic "distance from acceleration" formula "(v*t) + (0.5*a*t*t)".
        extrapolatedOffset = (rgbRatioSpeed * extrapolationRatio) + (0.5 * rgbRatioAcceleration * extrapolationRatio * extrapolationRatio);
      }
#else //TODOFT4: new rgb method...
			float3 rgbRatioSpeeds[3];
      [unroll]
			for (uint i = 0; i < 3; i++)
			{
		    rgbRatioSpeeds[i] = safeDivision(clampedSample_PQ - centeredSamples_PQ[i], clampedUV_PQ[i] - centeredUV_PQ[i]);
      }
      float3 rgbRatioAccelerations[3] = { float3(0, 0, 0), float3(0, 0, 0), float3(0, 0, 0) };
      if (settings.extrapolationQuality >= 2)
      {
        float3 centeredUV_PQ_2 = Linear_to_PQ2(ColorGradingLUTTransferFunctionOut(centeredUV_2, settings.transferFunctionIn, false) / PQNormalizationFactor);
        [unroll]
			  for (uint i = 0; i < 3; i++)
			  {
		      float3 rgbRatioSpeed_2 = safeDivision(centeredSamples_PQ_2[i] - centeredSamples_PQ[i], centeredUV_PQ_2[i] - centeredUV_PQ[i]); // "Velocity" more towards the center
#if 0
          rgbRatioSpeeds[i] = safeDivision(clampedSample_PQ - centeredSamples_PQ_2[i], clampedUV_PQ[i] - centeredUV_PQ_2[i]); // "Velocity" more towards the edge

          if (LumaSettings.DevSetting05 <= 0.25) // Wrong
            rgbRatioAccelerations[i] = safeDivision(rgbRatioSpeeds[i] - rgbRatioSpeed_2, abs(clampedUV_PQ[i] - centeredUV_PQ[i]) / 1.0); //TODOFT: / 2? Abs()?
          else if (LumaSettings.DevSetting05 <= 0.5)
            rgbRatioAccelerations[i] = safeDivision(rgbRatioSpeeds[i] - rgbRatioSpeed_2, abs(clampedUV_PQ[i] - centeredUV_PQ[i]) / 2.0);
          else if (LumaSettings.DevSetting05 <= 0.75) // Looks best with proper ACC branch
            rgbRatioAccelerations[i] = safeDivision(rgbRatioSpeeds[i] - rgbRatioSpeed_2, (clampedUV_PQ[i] - centeredUV_PQ[i]) / 2.0);
          else // Looks best with bad ACC branch
            rgbRatioAccelerations[i] = safeDivision(rgbRatioSpeeds[i] - rgbRatioSpeed_2, clampedUV_PQ[i] - centeredUV_PQ[i]);
#elif 0
				  rgbRatioSpeeds[i] = lerp(rgbRatioSpeed_2, rgbRatioSpeeds[i], 0.5);
#else
				  rgbRatioSpeeds[i] = rgbRatioSpeeds[i] + (rgbRatioSpeeds[i] - rgbRatioSpeed_2);
#endif
        }
      }
      
			float3 extrapolationRatio = unclampedTonemappedUV_PQ - clampedUV_PQ;
#if 0 // Bad test!!!? Testing what?
      const float3 centeringNormal = normalize(unclampedUV - clampedUV);
      const float3 centeringNormalAbs = abs(centeringNormal);
      const float3 centeringVectorAbs = abs(unclampedUV - clampedUV); //NOTE: to be tonemapped?
      float extrapolationRatioLength = length(extrapolationRatio);
      //extrapolationRatio = centeringVectorAbs / (centeringVectorAbs.x + centeringVectorAbs.y + centeringVectorAbs.z);
      extrapolationRatio = centeringNormalAbs * (length(extrapolationRatio) / length(centeringNormalAbs)) * sign(unclampedUV - clampedUV);
#endif

			float3 extrapolatedOffset = (rgbRatioSpeeds[0] * extrapolationRatio[0]) + (rgbRatioSpeeds[1] * extrapolationRatio[1]) + (rgbRatioSpeeds[2] * extrapolationRatio[2]);
      //extrapolatedOffset *= extrapolationRatioLength;
      if (settings.extrapolationQuality >= 2)
			{
#if 1
        extrapolatedOffset =  (rgbRatioSpeeds[0] * extrapolationRatio[0]) + (0.5 * rgbRatioAccelerations[0] * extrapolationRatio[0] * extrapolationRatio[0])
                            + (rgbRatioSpeeds[1] * extrapolationRatio[1]) + (0.5 * rgbRatioAccelerations[1] * extrapolationRatio[1] * extrapolationRatio[1])
                            + (rgbRatioSpeeds[2] * extrapolationRatio[2]) + (0.5 * rgbRatioAccelerations[2] * extrapolationRatio[2] * extrapolationRatio[2]);
#else
        extrapolatedOffset =  (rgbRatioSpeeds[0] * extrapolationRatio[0]) + (rgbRatioAccelerations[0] * extrapolationRatio[0] * extrapolationRatio[0])
                            + (rgbRatioSpeeds[1] * extrapolationRatio[1]) + (rgbRatioAccelerations[1] * extrapolationRatio[1] * extrapolationRatio[0])
                            + (rgbRatioSpeeds[2] * extrapolationRatio[2]) + (rgbRatioAccelerations[2] * extrapolationRatio[2] * extrapolationRatio[0]);
#endif
      }
#endif

      //TODOFT: why is the LUT extrapolation debug preview running on top of the last LUT square?

      //return (extrapolatedOffset) * 5;

			extrapolatedSample = PQ_to_Linear2(clampedSample_PQ + extrapolatedOffset, GCT_MIRROR) * PQNormalizationFactor;
      
#if DEVELOPMENT && 1
    bool oklab = LumaSettings.DevSetting06 >= 0.5;
#else
    bool oklab = false;
#endif
      if (oklab)
      {
#define USE_LENGTH 1
#define USE_PQ 0

        [unroll]
        for (uint i = 0; i < 3; i++)
        {
          float3 numerator = clampedSample_UCS - centeredSamples_UCS[i];
#if USE_LENGTH
          float divisor = length(clampedUV_UCS.yz - centeredUVs_UCS[i].yz);
#else
          float divisor = (abs(clampedUV_UCS.y - centeredUVs_UCS[i].y) + abs(clampedUV_UCS.z - centeredUVs_UCS[i].z)) * 0.5;
#endif
          rgbRatioSpeeds[i] = safeDivision(numerator, divisor); // This doesn't even need safe div
#if USE_PQ
          rgbRatioSpeeds[i] = safeDivision(numerator, abs(clampedUV_PQ[i] - centeredUV_PQ[i]));
#endif
        }
        
#if USE_LENGTH
        float extrapolationRatioUCS = length(unclampedTonemappedUV_UCS.yz - clampedUV_UCS.yz);
#else
        float extrapolationRatioUCS = (abs(unclampedTonemappedUV_UCS.y - clampedUV_UCS.y) + abs(unclampedTonemappedUV_UCS.z - clampedUV_UCS.z)) * 0.5;
#endif
#if USE_PQ
        extrapolationRatioUCS = length(unclampedTonemappedUV_PQ - clampedUV_PQ);
#endif

        extrapolationRatio = abs(extrapolationRatio); // This one is worse (more broken gradients), I can't explain why (what about with the last changes!???)
#if DEVELOPMENT
        if (LumaSettings.DevSetting05 > 0.5) // Seems to look better even if it makes little sense
        {
          //float3 unclampedUV_PQ = Linear_to_PQ2(neutralLUTColorLinear / PQNormalizationFactor, GCT_MIRROR);
          //const float3 centeringNormal = normalize(unclampedUV_PQ - clampedUV_PQ);
          const float3 centeringVectorAbs = abs(unclampedUV - clampedUV);
          extrapolationRatio = centeringVectorAbs;
          //return (extrapolationRatio - centeringVectorAbs) * 100;
        }
#endif

        extrapolationRatio /= extrapolationRatio.x + extrapolationRatio.y + extrapolationRatio.z;
        //extrapolationRatio = normalize(extrapolationRatio);

        extrapolatedOffset = (rgbRatioSpeeds[0] * extrapolationRatio[0]) + (rgbRatioSpeeds[1] * extrapolationRatio[1]) + (rgbRatioSpeeds[2] * extrapolationRatio[2]);
        //extrapolatedOffset *= extrapolationRatioUCS * (1.0 - LumaSettings.DevSetting05);
        extrapolatedOffset *= extrapolationRatioUCS;
        //extrapolatedOffset = clamp(extrapolatedOffset, -3, 3);
        // We exclusively extrapolate the color (hue and chroma) in UCS; we can't extrapolate the luminance for two major reasons:
        // - LStar, the brightness component of UCS is not directly perceptual (e.g. doubling its value doesn't match double perceived brightness), in fact, it's very far from it, its whole range is 0 to ~2.1, with 2.1 representing infinite brightness, so we can't do velocity operations with it, without massive math
        // - We find the color change on each channel (axis) before then extrapolating the color change in the target direction. To do so, we need to find the color velocity ratio on each axis. The "luminance" might not
        //   be relevant at all, because if the green channel was turned into red by the LUT, the luminance would be completely different and not comparable. Also we compare the ratio between the "backwards"/"centered" samples and the target UV one, but they are in completely different directions, so neither the luminance or their chroma/hue can be compared, if not for a generic chroma length test.
        const float3 extrapolatedSample_PQ = extrapolatedSample;
        extrapolatedSample = DarktableUcs::UCSLUVToRGB(float3(clampedSample_UCS.x, clampedSample_UCS.yz + extrapolatedOffset.yz));
        extrapolatedSample = RestoreLuminance(extrapolatedSample, extrapolatedSample_PQ); //TODOFT: this creates some broken gradients?
      }
		}
#pragma warning( disable : 4000 )

    // Apply the inverse of the original tonemap ratio on the new out of range values (this time they are not necessary out the values beyond 0-1, but the values beyond the clamped/vanilla sample).
    // We don't directly apply the inverse tonemapper formula here as that would make no sense.
		if (settings.inputTonemapToPeakWhiteNits > 0) // Optional optimization in case "inputTonemapToPeakWhiteNits" was static (or not...)
		{
#if 1 //TODOFT: fix text and polish code
      // Try to (partially) consider the new ratio for colors beyond 1, comparing the pre and post LUT (extrapolation) values.
      // For example, if after LUT extrapolation red has been massively compressed, we wouldn't want to apply the inverse of the original tonemapper up to a 100%, or red might go too bright again.
      // Given that we might be extrapolating on the direction of one channel only (as in, the only UV that was beyond 0-1 was the red channel), but that the extrapolation from a single channel direction
      // can actually change all 3 color channels, we can't adjust the tonemapping restoration by channel, and we are forced to do it by length.
      // Given this is about ratios and perception, it might arguably be better done in PQ space, but given the original tonemapper above was done in linear, for the sake of simplicity we also do this in linear.
#if 1 // 1D path (length) for per max channel tonemapper
			//float extrapolationRatio = safeDivision(length(Linear_to_PQ2(extrapolatedSample / PQNormalizationFactor, GCT_MIRROR) - clampedSample_PQ), length(unclampedTonemappedUV_PQ - saturate(unclampedTonemappedUV_PQ)), 0);
			float extrapolationRatio = safeDivision(length(extrapolatedSample - clampedSample), length(neutralLUTColorLinearTonemapped - saturate(neutralLUTColorLinearTonemapped)), 0);
#else // Per channel path for per channel tonemapper
			float3 extrapolationRatio = safeDivision(extrapolatedSample - clampedSample, neutralLUTColorLinearTonemapped - saturate(neutralLUTColorLinearTonemapped), 0); // This is the broken one
#endif
#if 0
      // To avoid too crazy results, we limit the min/max influence the extrapolation can have on the tonemap restoration (at 1, it won't have any influence). The higher the value, the more accurate and tolerant the results (theoretically, in reality they might cause outlier values).
      static const float maxExtrapolationInfluence = 2.5; // Note: expose parameter if needed
			extrapolatedSample = clampedSample + ((extrapolatedSample - clampedSample) * lerp(1, neutralLUTColorLinearTonemappedRestoreRatio, clamp(extrapolationRatio, 1.0 / maxExtrapolationInfluence, maxExtrapolationInfluence)));
#else
			//extrapolatedSample = clampedSample + ((extrapolatedSample - clampedSample) * lerp(1, neutralLUTColorLinearTonemappedRestoreRatio, abs(extrapolationRatio)));
			extrapolatedSample = clampedSample + ((extrapolatedSample - clampedSample) * lerp(1, neutralLUTColorLinearTonemappedRestoreRatio, max(extrapolationRatio, 0)));
#endif
#else // Simpler and faster implementation that doesn't account for the LUT extrapolation ratio of change when applying the inverse of the original tonemap ratio.
			extrapolatedSample = clampedSample + ((extrapolatedSample - clampedSample) * neutralLUTColorLinearTonemappedRestoreRatio);
#endif
		}

    // See the setting description for more information
		if (settings.clampedLUTRestorationAmount > 0)
		{
#if 1
      // Restore the extrapolated sample luminance onto the clamped sample, so we keep the clamped hue and saturation while maintaining the extrapolated luminance.
      float3 extrapolatedClampedSample = RestoreLuminance(clampedSample, extrapolatedSample);
#else // Disabled as this can have random results
      float3 unclampedUV_PQ = Linear_to_PQ2(neutralLUTColorLinear / PQNormalizationFactor, GCT_MIRROR); // "neutralLUTColorLinear" is equal to "ColorGradingLUTTransferFunctionOut(unclampedUV, settings.transferFunctionIn, true)"
			float3 extrapolationRatio = unclampedUV_PQ - clampedUV_PQ;
      // Restore the original unclamped color offset on the clamped sample in PQ space (so it's more perceptually accurate).
      // Note that this will cause hue shifts and possibly very random results, it only works on neutral LUTs.
      // This code is not far from "neutralLUTRestorationAmount".
      // Near black we opt for a sum as opposed to a multiplication, to avoid failing to restore the ratio when the source number is zero.
			float3 extrapolatedClampedSample = PQ_to_Linear2(lerp(clampedSample_PQ + extrapolationRatio, clampedSample_PQ * (1.0 + extrapolationRatio), saturate(abs(clampedSample_PQ))), GCT_MIRROR) * PQNormalizationFactor;
#endif
			extrapolatedSample = lerp(extrapolatedSample, extrapolatedClampedSample, settings.clampedLUTRestorationAmount);
		}

		// We can optionally leave or fix negative luminances colors here in case they were generated by the extrapolation, everything works by channel in Prey, not much is done by luminance, so this isn't needed until proven otherwise
		if (settings.fixExtrapolationInvalidColors) //TODOFT4: test more: does this reduce HDR colors!? It seems fine?
		{
      FixColorGradingLUTNegativeLuminance(extrapolatedSample);
    }

		outputSample = extrapolatedSample;
#if FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE == 2
		outputSample = neutralLUTColorLinear;
#endif // FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE == 2
#if TEST_LUT_EXTRAPOLATION
		outputSample = 0;
#endif // TEST_LUT_EXTRAPOLATION
	}

  // See the setting description for more information
  // We purposely do this one before "vanillaLUTRestorationAmount", otherwise we'd undo its effects.
  if (settings.neutralLUTRestorationAmount > 0)
	{
    if (!lutOutputLinear)
    {
			outputSample = ColorGradingLUTTransferFunctionOut(outputSample, settings.transferFunctionIn, true);
      lutOutputLinear = true;
    }
    outputSample = lerp(outputSample, neutralLUTColorLinear, settings.neutralLUTRestorationAmount);
  }
  
  // See the setting description for more information
	if (settings.vanillaLUTRestorationAmount > 0)
	{
    // Note that if the vanilla game had UNORM8 LUTs but for our mod they were modified to be FLOAT16, then maybe we'd want to saturate() "vanillaSample", but it's not really needed until proved otherwise
		float3 vanillaSample = SampleLUT(lut, samplerState, saturate(neutralVanillaColorTransferFunctionEncoded), settings, true, true, saturate(neutralVanillaColorLinear));
    if (!lutOutputLinear)
    {
			outputSample = ColorGradingLUTTransferFunctionOut(outputSample, settings.transferFunctionIn, true);
      lutOutputLinear = true;
    }
#if 1 // Advanced hue restoration
    outputSample = RestoreHue(outputSample, vanillaSample, settings.vanillaLUTRestorationAmount);
#else // Restoration by luminance
		float3 extrapolatedVanillaSample = RestoreLuminance(vanillaSample, outputSample);
		outputSample = lerp(outputSample, extrapolatedVanillaSample, settings.vanillaLUTRestorationAmount);
#endif
	}

  // If the input and output transfer functions are different, this will perform a transfer function correction (e.g. the typical SDR gamma mismatch: game encoded with gamma sRGB and was decode with gamma 2.2).
  // The best place to do "gamma correction" after LUT sampling and after extrapolation.
  // Most LUTs don't have enough precision (samples) near black to withstand baking in correction.
	// LUT extrapolation is also more correct when run in sRGB gamma, as that's the LUT "native" gamma, correction should still be computed later, only in the 0-1 range.
	// Encoding (gammification): sRGB (from 2.2) crushes blacks, 2.2 (from sRGB) raises blacks.
	// Decoding (linearization): sRGB (from 2.2) raises blacks, 2.2 (from sRGB) crushes blacks.
	if (!lutOutputLinear && settings.outputLinear)
	{
		outputSample.xyz = ColorGradingLUTTransferFunctionOutCorrected(outputSample.xyz, settings.transferFunctionIn, settings.transferFunctionOut);
	}
	else if (lutOutputLinear && !settings.outputLinear)
	{
		if (settings.transferFunctionIn != settings.transferFunctionOut)
		{
		  outputSample.xyz = ColorGradingLUTTransferFunctionIn(outputSample.xyz, settings.transferFunctionIn, true);
      ColorGradingLUTTransferFunctionInOutCorrected(outputSample.xyz, settings.transferFunctionIn, settings.transferFunctionOut, false);
		}
    else
    {
		  outputSample.xyz = ColorGradingLUTTransferFunctionIn(outputSample.xyz, settings.transferFunctionOut, true);
    }
	}
	else if (lutOutputLinear && settings.outputLinear)
	{
    ColorGradingLUTTransferFunctionInOutCorrected(outputSample.xyz, settings.transferFunctionIn, settings.transferFunctionOut, true);
	}
	else if (!lutOutputLinear && !settings.outputLinear)
	{
    ColorGradingLUTTransferFunctionInOutCorrected(outputSample.xyz, settings.transferFunctionIn, settings.transferFunctionOut, false);
	}
	return outputSample;
}

// Note that this function expects "LUT_SIZE" to be divisible by 2. If your LUT is (e.g.) 15x instead of 16x, move some math to be floating point and round to the closest pixel.
// "PixelPosition" is expected to be centered around texles center, so the first pixel would be 0.5 0.5, not 0 0.
// This partially mirrors "ShouldSkipPostProcess()".
float3 DrawLUTTexture(LUT_TEXTURE_TYPE lut, SamplerState samplerState, float2 PixelPosition, inout bool DrawnLUT)
{
	const uint LUTMinPixel = 0; // Extra offset from the top left
	uint LUTMaxPixel = LUT_MAX; // Bottom (right) limit
	uint LUTSizeMultiplier = 1;
  uint PixelScale = DRAW_LUT_TEXTURE_SCALE;
#if ENABLE_LUT_EXTRAPOLATION
	LUTSizeMultiplier = 2; // This will end up multiplying the number of shown cube slices as well
	// Shift the LUT coordinates generation to account for 50% of extra area beyond 1 and 50% below 0,
	// so "LUTPixelPosition3D" would represent the LUT from -0.5 to 1.5 before being normalized.
	// The bottom and top 25% squares (cube sections) will be completely outside of the valid cube range and be completely extrapolated,
	// while for the middle 50% squares, only their outer half would be extrapolated.
	LUTMaxPixel += LUT_SIZE * (LUTSizeMultiplier - 1);
	PixelScale = round(pow(PixelScale, 1.f / LUTSizeMultiplier));
#endif // ENABLE_LUT_EXTRAPOLATION

	PixelPosition -= 0.5f; //TODOFT2: make sure this math is all right and copy it to "ShouldSkipPostProcess()"

	const uint LUTPixelSideSize = LUT_SIZE * LUTSizeMultiplier; // LUT pixel size (one dimension) on screen (with extrapolated pixels too)
	const uint2 LUTPixelPosition2D = round(PixelPosition / PixelScale); // Round to avoid the color accidentally snapping to the lower integer
	const uint3 LUTPixelPosition3D = uint3(LUTPixelPosition2D.x % LUTPixelSideSize, LUTPixelPosition2D.y, LUTPixelPosition2D.x / LUTPixelSideSize);
	if (!any(LUTPixelPosition3D < LUTMinPixel) && !any(LUTPixelPosition3D > LUTMaxPixel))
	{
    // Note that the LUT sampling function will still use bilinear sampling, we are just manually centering the LUT coordinates to match the center of texels.
		static const bool NearestNeighbor = false;

		DrawnLUT = true;

		// The color the neutral LUT would have, in sRGB gamma space
    float3 LUTCoordinates;

    if (NearestNeighbor)
    {
      LUTCoordinates = LUTPixelPosition3D / float(LUTMaxPixel);
    }
    else
    {
		  const float2 LUTPixelPosition2DFloat = PixelPosition / (float)PixelScale;
		  float3 LUTPixelPosition3DFloat = float3(fmod(LUTPixelPosition2DFloat.x, LUTPixelSideSize), LUTPixelPosition2DFloat.y, (uint)(LUTPixelPosition2DFloat.x / LUTPixelSideSize));
      LUTCoordinates = LUTPixelPosition3DFloat / float(LUTMaxPixel);
    }
    LUTCoordinates *= LUTSizeMultiplier;
    LUTCoordinates -= (LUTSizeMultiplier - 1.f) / 2.f;
#if ENABLE_LUT_EXTRAPOLATION && TEST_LUT_EXTRAPOLATION
    if (any(LUTCoordinates < -FLT_MIN) || any(LUTCoordinates > 1.f + FLT_EPSILON))
    {
		  return 0;
    }
#endif // ENABLE_LUT_EXTRAPOLATION && TEST_LUT_EXTRAPOLATION

    LUTExtrapolationData extrapolationData = DefaultLUTExtrapolationData();
    extrapolationData.inputColor = LUTCoordinates.rgb;

    LUTExtrapolationSettings extrapolationSettings = DefaultLUTExtrapolationSettings();
    extrapolationSettings.enableExtrapolation = bool(ENABLE_LUT_EXTRAPOLATION);
    extrapolationSettings.extrapolationQuality = LUT_EXTRAPOLATION_QUALITY;
#if DEVELOPMENT && 1 // These match the settings defined in "HDRFinalScenePS" (in case you wanted to preview them)
    //extrapolationSettings.inputTonemapToPeakWhiteNits = 1000.0;
    extrapolationSettings.inputTonemapToPeakWhiteNits = 10000 * LumaSettings.DevSetting01;
    //extrapolationSettings.clampedLUTRestorationAmount = 1.0 / 4.0;
    //extrapolationSettings.vanillaLUTRestorationAmount = 1.0 / 3.0;
    
    extrapolationSettings.extrapolationQuality = LumaSettings.DevSetting03 * 2.99; //TODOFT
    extrapolationSettings.backwardsAmount = LumaSettings.DevSetting04;
    //if (extrapolationSettings.extrapolationQuality >= 2) extrapolationSettings.backwardsAmount = 2.0 / 3.0;
#endif
    extrapolationSettings.inputLinear = false;
    extrapolationSettings.lutInputLinear = false;
    extrapolationSettings.lutOutputLinear = bool(ENABLE_LINEAR_COLOR_GRADING_LUT);
    extrapolationSettings.outputLinear = bool(POST_PROCESS_SPACE_TYPE == 1);
    extrapolationSettings.transferFunctionIn = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB;
// We might not want gamma correction on the debug LUT, gamma correction comes after extrapolation and isn't directly a part of the LUT, so it shouldn't affect its "raw" visualization
#if 1
    extrapolationSettings.transferFunctionOut = (bool(POST_PROCESS_SPACE_TYPE == 1) && GAMMA_CORRECTION_TYPE == 1) ? LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2 : extrapolationSettings.transferFunctionIn;
#else
    extrapolationSettings.transferFunctionOut = extrapolationSettings.transferFunctionIn;
#endif
    extrapolationSettings.samplingQuality = (HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS || ENABLE_LUT_TETRAHEDRAL_INTERPOLATION) ? (ENABLE_LUT_TETRAHEDRAL_INTERPOLATION ? 2 : 1) : 0;

		const float3 LUTColor = SampleLUTWithExtrapolation(lut, samplerState, extrapolationData, extrapolationSettings);
    return LUTColor;
	}
	return 0;
}