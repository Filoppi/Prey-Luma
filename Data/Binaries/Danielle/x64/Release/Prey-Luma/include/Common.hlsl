#ifndef SRC_COMMON_HLSL
#define SRC_COMMON_HLSL

// Silence pow(x, n) issue complaining about negative pow possibly failing
#pragma warning( disable : 3571 )
// Silence for loop issue where multiple int i declarations overlap each other (because hlsl doesn't have stack/scope like c++ thus variables don't pop after their scope dies)
#pragma warning( disable : 3078 )

// These should only ever be included through "Common.hlsl" and never individually
#include "include/Math.hlsl"
#include "include/Color.hlsl"
#include "include/Settings.hlsl"

#define LUT_SIZE 16u
#define LUT_MAX (LUT_SIZE - 1u)

// The aspect ratio the game was developed against, in case some effects weren't scaling properly for other aspect ratios.
// For best results, we should consider the FOV Hor+ beyond 16:9 and Vert- below 16:9
// (so the 16:9 image is always visible, and the aspect ratio either extends the vertical or horizontal view).
static const float NativeAspectRatioWidth = 16.0;
static const float NativeAspectRatioHeight = 9.0;
static const float NativeAspectRatio = NativeAspectRatioWidth / NativeAspectRatioHeight;
// The vertical resolution that most likely was the most used by the game developers,
// we define this to scale up stuff that did not natively correctly scale by resolution.
// According to the developers, the game was mostly developed on 1080p displays, and some 1440p ones, so
// we are going for their middle point, but 1080 or 1440 would also work fine.
static const float BaseVerticalResolution = 1260.0;

// Exposure multiplier for sunshafts. It's useful to shift them towards a better range for float textures to avoid banding.
// This comes from vanilla values, it's not really meant to be changed.
static const float SunShaftsBrightnessMultiplier = 4.0;
// With "SUNSHAFTS_LOOK_TYPE" > 0 and "ENABLE_LENS_OPTICS_HDR", we apply exposure to sun shafts and lens optics as well.
// Given that exposure can deviate a lot from a value of 1, to the point where it would make lens optics effects look weird, we diminish its effect on them so it's less jarring, but still applies (which is visually nicer).
// The value should be between 0 and 1.
static const float SunShaftsAndLensOpticsExposureAlpha = 0.25; // Anything more than 0.25 can cause sun effects to be blinding if the exposure is too high (it's pretty high in some scenes)

//TODOFT: test increase?
static const float BinkVideosAutoHDRPeakWhiteNits = 400; // Values beyond 700 will make AutoHDR look bad
// The higher it is, the "later" highlights start
static const float BinkVideosAutoHDRShoulderPow = 2.75; // A somewhat conservative value

float3 RestoreLuminance(float3 targetColor, float3 sourceColor)
{
  float sourceColorLuminance = GetLuminance(sourceColor);
  float targetColorLuminance = GetLuminance(targetColor);
  return targetColor * max(safeDivision(sourceColorLuminance, targetColorLuminance, 1), 0.0);
}

// Formulas that either uses 2.2 or sRGB gamma depending on a global definition.
// Note that converting between linear and gamma space back and forth results in quality loss, especially over very high and very low values.
float3 game_gamma_to_linear_mirrored(float3 Color)
{
#if GAMMA_CORRECTION_TYPE >= 2
  return RestoreLuminance(gamma_sRGB_to_linear_mirrored(Color), gamma_to_linear_mirrored(Color));
#elif GAMMA_CORRECTION_TYPE == 1
	return gamma_to_linear_mirrored(Color);
#else
  return gamma_sRGB_to_linear_mirrored(Color);
#endif
}
float3 linear_to_game_gamma_mirrored(float3 Color)
{
#if GAMMA_CORRECTION_TYPE >= 2
	return RestoreLuminance(linear_to_sRGB_gamma_mirrored(Color), linear_to_gamma_mirrored(Color));
#elif GAMMA_CORRECTION_TYPE == 1
	return linear_to_gamma_mirrored(Color);
#else
  return linear_to_sRGB_gamma_mirrored(Color);
#endif
}

// Luma per pass or per frame data
cbuffer LumaData : register(b8)
{
  struct
  {
    // If true, DLSS SR or other upscalers have already run before the game's original upscaling pass,
    // and thus we need to work in full resolution space and not rendering resolution space.
    uint PostEarlyUpscaling;
    uint DummyPadding; // GPU has "32 32 32 32 | break" bits alignment on memory, so to not break the "float2" below, we need this (because we are using a unified struct).
    // Camera jitters in UV space (rendering resolution) (not in projection matrix space, so they don't need to be divided by the rendering resolution). You might need to multiply this by 0.5 and invert the horizontal axis before using it.
    float2 CameraJitters;
    // Previous frame's camera jitters in UV space (relative to its own resolution).
    float2 PreviousCameraJitters;
    float2 RenderResolutionScale;
    // This can be used instead of "CV_ScreenSize" in passes where "CV_ScreenSize" would have been
    // replaced with 1 because DLSS SR upscaled the image earlier in the rendering.
    float2 PreviousRenderResolutionScale;
    row_major float4x4 ViewProjectionMatrix;
    row_major float4x4 PreviousViewProjectionMatrix;
    // Same as the one on "PostAA" "AA" but fixed to include jitters as well
    row_major float4x4 ReprojectionMatrix;
  } LumaData : packoffset(c0);
}

// AdvancedAutoHDR pass to generate some HDR brightess out of an SDR signal.
// This is hue conserving and only really affects highlights.
// "SDRColor" is meant to be in "SDR range", as in, a value of 1 matching SDR white (something between 80, 100, 203, 300 nits, or whatever else)
// https://github.com/Filoppi/PumboAutoHDR
float3 PumboAutoHDR(float3 SDRColor, float _PeakWhiteNits, float _PaperWhiteNits, float ShoulderPow = 2.75)
{
	const float SDRRatio = max(GetLuminance(SDRColor), 0.f);
	// Limit AutoHDR brightness, it won't look good beyond a certain level.
	// The paper white multiplier is applied later so we account for that.
	const float AutoHDRMaxWhite = min(_PeakWhiteNits, PeakWhiteNits) / _PaperWhiteNits;
	const float AutoHDRShoulderRatio = 1.f - max(1.f - SDRRatio, 0.f);
	const float AutoHDRExtraRatio = pow(AutoHDRShoulderRatio, ShoulderPow) * (AutoHDRMaxWhite - 1.f);
	const float AutoHDRTotalRatio = SDRRatio + AutoHDRExtraRatio;
	return SDRColor * safeDivision(AutoHDRTotalRatio, SDRRatio, 1);
}

// LUMA FT: functions to convert an SDR color (optionally in gamma space) to an HDR one (optionally linear * paper white).
// This should be used for any color that writes on the color buffer (or back buffer) from tonemapping on.
float3 SDRToHDR(float3 Color, bool InGammaSpace = true, bool UI = false)
{
  bool OutLinearSpace = bool(POST_PROCESS_SPACE_TYPE == 1) || (bool(POST_PROCESS_SPACE_TYPE >= 2) && !UI);
  if (OutLinearSpace)
  {
    if (InGammaSpace)
    {
      Color.rgb = game_gamma_to_linear_mirrored(Color.rgb);
    }
    const float paperWhite = (UI ? UIPaperWhiteNits : GamePaperWhiteNits) / sRGB_WhiteLevelNits;
    Color.rgb *= paperWhite;
  }
  else
  {
    if (!InGammaSpace)
    {
      Color.rgb = linear_to_game_gamma_mirrored(Color.rgb);
    }
  }
	return Color;
}
float4 SDRToHDR(float4 Color, bool InGammaSpace = true, bool UI = false)
{
	return float4(SDRToHDR(Color.rgb, InGammaSpace, UI), Color.a);
}

// LUMA FT: added these functions to decode and re-encode the "back buffer" from any range to a range that roughly matched SDR linear space
float3 EncodeBackBufferFromLinearSDRRange(float3 color, bool UI = false)
{
  bool InLinearSpace = bool(POST_PROCESS_SPACE_TYPE == 1) || (bool(POST_PROCESS_SPACE_TYPE >= 2) && !UI);
  if (InLinearSpace)
  {
    const float paperWhite = GamePaperWhiteNits / sRGB_WhiteLevelNits;
    return color * paperWhite;
  }
  else
  {
  	return linear_to_game_gamma_mirrored(color);
  }
}
float3 DecodeBackBufferToLinearSDRRange(float3 color, bool UI = false)
{
  bool InLinearSpace = bool(POST_PROCESS_SPACE_TYPE == 1) || (bool(POST_PROCESS_SPACE_TYPE >= 2) && !UI);
  if (InLinearSpace)
  {
    const float paperWhite = GamePaperWhiteNits / sRGB_WhiteLevelNits;
    return color / paperWhite;
  }
  else
  {
  	return game_gamma_to_linear_mirrored(color);
  }
}

// Partially mirrors "DrawLUTTexture()".
// PassType:
//  0 Generic
//  1 TAA
bool ShouldSkipPostProcess(float2 PixelPosition, uint PassType = 0)
{
#if TEST_MOTION_BLUR_TYPE || TEST_SMAA_EDGES
  return true;
#endif // TEST_MOTION_BLUR_TYPE || TEST_SMAA_EDGES
#if TEST_TAA_TYPE
  if (PassType != 1) { return true; }
#endif // TEST_TAA_TYPE
#if DRAW_LUT
	const uint LUTMinPixel = 0;
	uint LUTMaxPixel = LUT_MAX;
	uint LUTSizeMultiplier = 1;
  uint PixelScale = DRAW_LUT_TEXTURE_SCALE;
#if ENABLE_LUT_EXTRAPOLATION
	LUTSizeMultiplier = 2;
	LUTMaxPixel += LUT_SIZE * (LUTSizeMultiplier - 1);
	PixelScale = round(pow(PixelScale, 1.f / LUTSizeMultiplier));
#endif // ENABLE_LUT_EXTRAPOLATION

	PixelPosition -= 0.5f;

	const uint LUTPixelSideSize = LUT_SIZE * LUTSizeMultiplier;
	const uint2 LUTPixelPosition2D = round(PixelPosition / PixelScale);
	const uint3 LUTPixelPosition3D = uint3(LUTPixelPosition2D.x % LUTPixelSideSize, LUTPixelPosition2D.y, LUTPixelPosition2D.x / LUTPixelSideSize);
	if (!any(LUTPixelPosition3D < LUTMinPixel) && !any(LUTPixelPosition3D > LUTMaxPixel))
	{
    return true;
  }
#endif // DRAW_LUT
  return false;
}

void ApplyDithering(inout float3 color, float2 uv, bool gammaSpace = true, float paperWhite = 1.0, uint bitDepth = DITHERING_BIT_DEPTH, float time = 0, bool useTime = false)
{
  // LUMA FT: added in/out encoding
  color /= paperWhite;
  float3 lastLinearColor = color;
  //TODO LUMA: use log10 gamma or HDR10 PQ, it should match human perception more accurately
  if (!gammaSpace)
  {
    color = linear_to_game_gamma_mirrored(color); // Just use the same gamma function we use across the code, to keep it simple
  }
  float3 lastGammaColor = color;

  uint ditherRatio; // LUMA FT: added dither bith depth support, 8 bit might be too much for 16 bit HDR
  // Optimized (static) branches
  if (bitDepth == 8) { ditherRatio = 255; }
  else if (bitDepth == 10) { ditherRatio = 1023; }
  else { ditherRatio = uint(round(pow(2, bitDepth) - 1.0)); }

  float3 rndValue;
	// Apply dithering in sRGB space to minimize quantization artifacts
	// Use a triangular distribution which gives a more uniform noise by avoiding low-noise areas
  if (useTime)
  {
    const float tr = frac(time / 1337.7331) + 0.5; // LUMA FT: added "time" randomization to avoid dithering being fixed per pixel over time
    rndValue = NRand3(uv, tr) + NRand3(uv + 0.5789, tr) - 1.0;
  }
  else
  {
    rndValue = NRand3(uv) + NRand3(uv + 0.5789) - 1.0; // LUMA FT: fixed this from subtracting 0.5 to 1 so it's mirrored and doesn't just raise colors
  }
#if TEST_DITHERING
  color += rndValue;
#else // TEST_DITHERING
  color += rndValue / ditherRatio;
#endif // TEST_DITHERING

  if (!gammaSpace)
  {
#if HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS
    color = lastLinearColor + (game_gamma_to_linear_mirrored(color) - game_gamma_to_linear_mirrored(lastGammaColor));
#else // HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS
    color = game_gamma_to_linear_mirrored(color);
#endif // HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS
  }
  color *= paperWhite;
}

// Fix up sharpening/blurring when done on HDR images in post processing. In SDR, the source color could only be between 0 and 1,
// so the halos (rings) that could result from rapidly changing colors were limited, but in HDR lights can go much brighter so the halos got noticeable with default settings.
// This should work with any "POST_PROCESS_SPACE_TYPE" setting.
float3 FixUpSharpeningOrBlurring(float3 postSharpeningColor, float3 preSharpeningColor)
{
#if ENABLE_SHARPENING
    // Either set it to 0.5, 0.75 or 1 to make results closer to SDR (this makes more sense when done in gamma space, but also works in linear space).
    // Lower values slightly diminish the effect of sharpening, but further avoid halos issues.
    static const float sharpeningMaxColorDifference = 0.5;
    postSharpeningColor.rgb = clamp(postSharpeningColor.rgb, preSharpeningColor - sharpeningMaxColorDifference, preSharpeningColor + sharpeningMaxColorDifference);
    
#if 0 // Not necessary until proven otherwise, the whole shader code base works in r g b individually so even if we had an invalid luminance, it'd be fine (it will likely be clipped on output anyway)
    postSharpeningColor.rgb = max(postSharpeningColor.rgb, min(preSharpeningColor.rgb, 0)); // Don't allow scRGB colors to go below the min we previously had
#endif
#endif // ENABLE_SHARPENING
  	return postSharpeningColor;
}

float2 RemapUV(float2 UV, float2 sourceResolution, float2 targetResolution)
{
  // First remap from a "+half source texel uv offset to 1-half source texel uv offset" range to a 0-1 range, then re-map acknowleding the half target texel uv offset.
  UV -= 0.5 / sourceResolution;
  UV *= (sourceResolution / (sourceResolution - 1.0)) * ((targetResolution - 1.0) / targetResolution); // Unified over one line to avoid shifting the UV range too many times
  UV += 0.5 / targetResolution;
  return UV;
}

// "resolutionsScale" is the "direct" resolution multiplier (e.g. 0.5 means 50% rendering resolution)
float2 RemapUVFromScale(float2 UV, float2 resolutionScale /*= CV_HPosScale.xy*/, float2 sourceResolution /*= CV_ScreenSize.xy*/)
{
  // Avoid "degrading" the quality if the resolution scale is 1
  return resolutionScale == 1 ? UV : RemapUV(UV, sourceResolution, sourceResolution / resolutionScale);
}

#endif // SRC_COMMON_HLSL