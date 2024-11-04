#include "include/Common.hlsl"
#include "include/ColorGradingLUT.hlsl"
#include "include/Tonemap.hlsl"

#include "include/CBuffer_PerViewGlobal.hlsl"

// LUMA FT: attempt at implementing MSAA support in the tonemapper (e.g. "DRAW_LUT"). This doesn't work an MSAA was deprecated or unfinished in the engine at the time of Prey so it's very buggy.
#define ALLOW_MSAA 0

SamplerState ssHdrLinearClamp : register(s0);
#if ALLOW_MSAA
Texture2DMS<float4> hdrSourceTex : register(t0);
#else
Texture2D<float4> hdrSourceTex : register(t0);
#endif
Texture2D<float2> adaptedLumTex : register(t1);
Texture2D<float4> bloomTex : register(t2);
Texture2D<float> depthTex : register(t5);
Texture2D<float4> vignettingTex : register(t7);
Texture2D<float4> colorChartTex : register(t8);
Texture2D<float4> sunshaftsTex : register(t9);

void TestOutput(inout float3 outColor)
{
  if (any(isinf(outColor.rgb)))
  {
    outColor.rgb = float3(1, 0, 0);
  }
  else if (any(isnan(outColor.rgb)))
  {
    outColor.rgb = float3(0, 1, 0);
  }
#if POST_PROCESS_SPACE_TYPE >= 1
  else if (GetLuminance(outColor.rgb) < -FLT_MIN)
#else // POST_PROCESS_SPACE_TYPE <= 0
  else if (GetLuminance(game_gamma_to_linear_mirrored(outColor.rgb)) < -FLT_MIN)
#endif // POST_PROCESS_SPACE_TYPE >= 1
  {
    outColor.rgb = float3(0, 0, 1);
  }
}

float2 MapViewportToRaster(float2 normalizedViewportPos, bool bOtherEye = false)
{
		return normalizedViewportPos * CV_HPosScale.xy;
}

float GetLinearDepth(float fDevDepth, bool bScaled = false)
{
	return fDevDepth * (bScaled ? CV_NearFarClipDist.y : 1.0f);
}

float2 GetJitters()
{
#if 1 // Equivalent versions
	row_major float4x4 projectionMatrix = mul( CV_ViewProjMatr, CV_InvViewMatr ); // The current projection matrix used to be stored in "CV_PrevViewProjMatr" in vanilla Prey
 	return float2(projectionMatrix[0][2], projectionMatrix[1][2]);
#else
	row_major float4x4 projectionMatrix = mul( transpose(CV_InvViewMatr), transpose(CV_ViewProjMatr) );
 	return float2(projectionMatrix[2][0], projectionMatrix[2][1]);
#endif
}

#define LIGHT_UNIT_SCALE 10000.0f

float ComputeExposure(float fIlluminance, float4 HDREyeAdaptation)
{
	// Compute EV with ISO 100 and standard camera settings
	float EV100 = log2(fIlluminance * LIGHT_UNIT_SCALE * 100.0 / 330.0);
	
	// Apply automatic exposure compensation based on scene key
	EV100 -= ((clamp(log10(fIlluminance * LIGHT_UNIT_SCALE + 1.0), 0.1, 5.2) - 3.0) / 2.0) * HDREyeAdaptation.z;
	
	// Clamp EV
	EV100 = clamp(EV100, HDREyeAdaptation.x, HDREyeAdaptation.y);
	
	// Compute maximum luminance based on Saturation Based Film Sensitivity (ISO 100, lens factor q=0.65)
	float maxLum = 1.2 * exp2(EV100) / LIGHT_UNIT_SCALE;
	
	return 1.0 / maxLum;
}

#if _RT_SAMPLE2
void ApplyArkDistanceSat(inout float3 _cImage, int3 _pixelCoord)
{
	float fDepth = GetLinearDepth(depthTex.Load(_pixelCoord), true);

	float fBaseAmount = ArkDistanceSat.x; // This should always be between 0-1
	float fFadeStart = ArkDistanceSat.y;
	float fFadeInvSlope = ArkDistanceSat.z;

	float fSaturationScalar = saturate( (fDepth - fFadeStart) * fFadeInvSlope );
	float fLuminance = max(GetLuminance(_cImage), 0.0f); // LUMA FT: added "max()" with 0 to avoid negative luminances

  // LUMA FT: this used to be applied in gamma space, but calculating "luminance" on gamma space colors (luma) isn't as accurate,
  // and after testing, we confirmed de-saturating in linear space looks better. It might have a slightly difference intensity,
  // especially on very bright colors (it's less intense), but it doesn't shift hues around so much.
  // If ever necessary, emulating gamma space desaturation is possible.
	_cImage = lerp(_cImage, fLuminance.xxx, saturate(fBaseAmount * fSaturationScalar));
}
#endif

#if 1 //TODOFT5: For this path we'd need to invert the DICE tonemapper too (do we?) and match mid gray after DICE had run, except that we delay DICE until the end and also we can't match it after because its peak is already set. Nah it's fine, in untonemapped it's good and with DICE it doesn't really touch mid tones so we can't pre match it
static const float SDRTMMidGrayOut = MidGray;
static const float SDRTMMidGrayIn = GetLuminance(Tonemap_Hable_Inverse(SDRTMMidGrayOut)); // The "HDRFilmCurve" are not used so we can pre-calculate this offline
#else
static const float SDRTMMidGrayIn = MidGray;
static const float SDRTMMidGrayOut = GetLuminance(Tonemap_Hable(SDRTMMidGrayIn)); // The "HDRFilmCurve" are not used so we can pre-calculate this offline
#endif
static const float SDRTMMidGrayRatio = SDRTMMidGrayOut / SDRTMMidGrayIn;

// Originally named "HDRToneMapSample" and "FilmMapping"
float4 FilmTonemapping( out float3 cSDRColor, in float4 cScene, in float4 cBloom, in float4 cSunShafts, in float2 vAdaptedLum, in float fVignetting, bool legacyExposure, float4 HDRColorBalance, float4 HDREyeAdaptation, float4 HDRFilmCurve, float4 HDRBloomColor )
{
  // Compute exposure
  float fExposure;
	
#if _RT_SAMPLE4 // Always true
  if (legacyExposure) 
  {
    // Legacy exposure mode
	  // Krawczyk scene key estimation adjusted to better fit our range - low (0.05) to high key (0.8) interpolation based on avg scene luminance
	  const float fSceneKey = 1.03 - 2.0 / (2.0 + log2(vAdaptedLum.x + 1.0));
	  fExposure = clamp(fSceneKey / vAdaptedLum.x, HDREyeAdaptation.y, HDREyeAdaptation.z);
  }
  else // LUMA FT: moved to else branch for optimization as this isn't used by Prey
#endif // _RT_SAMPLE4
  {
    fExposure = ComputeExposure(vAdaptedLum.y, HDREyeAdaptation);
  }

#if ENABLE_BLOOM
  float3 CustomHDRBloomColor = HDRBloomColor.rgb;
#else
  float3 CustomHDRBloomColor = 0;
#endif
  // LUMA FT: bloom is lower quality in the vanilla game (R11G11B10F), so theoretically blending to it lowers our colors quality (clipping negative scRGB colors and causing banding),
  // but it's not blended in to a high enough amount for it to be a problem. The Luma mod upgrades bloom to be R16G16B16A16F so this isn't a problem anymore.
  float3 cBloomedScene = lerp(cScene.xyz, cBloom.xyz, saturate(CustomHDRBloomColor.rgb));
#if ENABLE_BLOOM && TEST_BLOOM_TYPE == 1
  float3 cAdditiveBloom = cBloom.xyz - cScene.xyz;
  cBloomedScene = cAdditiveBloom;
#elif ENABLE_BLOOM && TEST_BLOOM_TYPE == 2
  cBloomedScene = cBloom.rgb;
#endif

  float3 cColor = fExposure * cBloomedScene;

#if _RT_SAMPLE3 && TEST_BLOOM_TYPE == 0 && ENABLE_SUNSHAFTS && ANTICIPATE_SUNSHAFTS
  // LUMA FT: moved sunshafts drawing to blend in before tonemapping. This way the are also affected by the exposure, which theoretically makes sense.
  // They are generated on the pre auto exposure linear HDR rendering, so theoretically they should also be exposure adjusted, then again in SDR
  // it probably wasn't to keep it consistently bright (though it was clipping a lot).
  // We also adjust them by the inverse tonemapper mid gray scaling, to avoid that shifting their brightness too much (the tonemapper code below could still modulate the sun shafts look).
#if SUNSHAFTS_LOOK_TYPE >= 1
  float normalization = lerp(1.0, fExposure, SunShaftsAndLensOpticsExposureAlpha) / lerp(SDRTMMidGrayRatio, 1.0, SunShaftsAndLensOpticsExposureAlpha);
  cSunShafts.rgb *= normalization;
#endif // SUNSHAFTS_LOOK_TYPE >= 1
  // LUMA FT: In HDR, we always blend sun shafts at 100%, given that we have no hard ceiling of 1 (and we run before tonemapping),
  // not like in SDR where they are only blended in if the background isn't white.
	cColor.rgb += cSunShafts.rgb; // Blend in hdr sunshafts
#endif // _RT_SAMPLE3 && TEST_BLOOM_TYPE == 0 && ENABLE_SUNSHAFTS && ANTICIPATE_SUNSHAFTS

  cColor *= fVignetting;

  // hdr color grading
  float fLuminance = GetLuminance(cColor.rgb);
  cColor.rgb = lerp(fLuminance, cColor.rgb, HDRColorBalance.a);	// saturation
  cColor.rgb *= HDRColorBalance.rgb;	// color balance

  cSDRColor = Tonemap_Hable(cColor.rgb, HDRFilmCurve.x, HDRFilmCurve.y, HDRFilmCurve.z, HDRFilmCurve.w);

#if TONEMAP_TYPE <= 0 // SDR TM

  cColor.rgb = cSDRColor;

#else // HDR

  // Bring back the color to the same range as SDR by matching the mid gray level.
  cColor.rgb *= SDRTMMidGrayRatio;

#if TONEMAP_TYPE == 1 // HDR TM

#if !DELAY_HDR_TONEMAP
	const float paperWhite = GamePaperWhiteNits / sRGB_WhiteLevelNits;
	const float peakWhite = PeakWhiteNits / sRGB_WhiteLevelNits;
  cColor.rgb = Tonemap_DICE(cColor.rgb, peakWhite, paperWhite) / paperWhite; // Multiply out the paper white to keep colors in the SDR range, we'll multiply it in back later
#endif // DELAY_HDR_TONEMAP

  // Fall back on the SDR tonemapper below mid gray (the HDR color shouldn't have any negative colors anyway)
#if 0 // By luminance
  cColor.rgb *= lerp(1.0, GetLuminance(cSDRColor) / GetLuminance(cColor.rgb), 1.0 - saturate(GetLuminance(cSDRColor) / MidGray));
#elif 0 // By channel, luminance based
  cColor.rgb = lerp(cSDRColor, cColor.rgb, saturate(GetLuminance(cSDRColor) / MidGray));
#elif 1 // By channel, channel based (most saturated and most similar to SDR)
  float3 cNegativeColor = min(cColor.rgb, 0);
//TODOFT2: optimize pow (remove) and tweak values (lower values make it more similar to untonemapped, maybe Hable crushed blacks too much, especially with gamma correction enabled). Try other branches now with by channel DICE
//Also, use DICE beyond "inLinearScale" Hable start point?
//Increase these values static const values???
//Review cNegativeColor (it creates invalid luminances)
#if 1
  static const float SDRRestorationScale = 2.0 / 3.0; //MidGray * SDRRestorationScale; // [0, 1] the lower, the more raised near black colors are. "Neutral" at "MidGray".
  static const float SDRRestorationPower = 4.0 / 3.0; // (0, inf) the lower, the more raised near black colors are. "Neutral" at 1.
#else
  static const float SDRRestorationScale = MidGray;
  static const float SDRRestorationPower = 1.5;
#endif
  cColor.rgb = lerp(cSDRColor, max(cColor.rgb, 0), pow(saturate(cSDRColor / SDRRestorationScale), SDRRestorationPower));
  cColor.rgb += cNegativeColor; // Always keep any negative colors that expand gamut (usually there aren't any, but you never know)
#endif

#else // TONEMAP_TYPE >= 2 // Untonemapped (HDR)

  // Nothing to do here

#endif // TONEMAP_TYPE

#endif // TONEMAP_TYPE

#if TEST_HIGH_SATURATION_GAMUT && !DELAY_HDR_TONEMAP
  static const float extraSaturationTest = 1.0;
  cColor.rgb = lerp(GetLuminance(cColor.rgb), cColor.rgb, 1.0 + extraSaturationTest);
#endif

  return float4(cColor, cScene.a);
}

void HDRFinalScenePS(float4 WPos, float4 baseTC, out float4 outColor)
{
  int3 pixelCoord = int3(WPos.xy, 0);
	float2 ScreenTC = MapViewportToRaster(baseTC.xy);

#if DRAW_LUT && !TEST_MOTION_BLUR_TYPE && !TEST_SMAA_EDGES && !TEST_TAA_TYPE
  uint sourceLevel = 0;
  uint sourceLevels = 1;
  uint2 sourceSize = 1;
#if ALLOW_MSAA
  hdrSourceTex.GetDimensions(sourceSize.x, sourceSize.y, sourceLevels);
#else // !ALLOW_MSAA
  hdrSourceTex.GetDimensions(sourceLevel, sourceSize.x, sourceSize.y, sourceLevels); // Doesn't work (that's not how MSAA works, at least not in Prey)
#endif // ALLOW_MSAA
  float3 scaledPixelCoord = float3(((WPos.xy / float(sourceLevels)) / CV_HPosScale.xy), 0); // Dynamic resolution scaling support

	bool drawnLUT = false;
	float3 LUTColor = DrawLUTTexture(colorChartTex, ssHdrLinearClamp, scaledPixelCoord.xy, drawnLUT);
	if (drawnLUT)
	{
#if POST_PROCESS_SPACE_TYPE == 1
    const float paperWhite = GamePaperWhiteNits / sRGB_WhiteLevelNits;
    LUTColor *= paperWhite;
#endif // POST_PROCESS_SPACE_TYPE == 1
    outColor.rgba = float4(LUTColor, 0);
    return;
	}
#endif // DRAW_LUT && !TEST_MOTION_BLUR_TYPE && !TEST_SMAA_EDGES && !TEST_TAA_TYPE

#if ALLOW_MSAA
	float4 cScene = hdrSourceTex.Load(pixelCoord.xy, 0);
#else // !ALLOW_MSAA
	float4 cScene = hdrSourceTex.Load(pixelCoord);
#endif // ALLOW_MSAA

#if ENABLE_VIGNETTE
  // LUMA FT: Ultrawide friendly vignette implementation. To alter the vignette strength, we can multiply the offset from 1 (away from it)
	float fVignetting = vignettingTex.Sample(ssHdrLinearClamp, baseTC.xy).x;
#else // !ENABLE_VIGNETTE
  float fVignetting = 1.0;
#endif // ENABLE_VIGNETTE
	float4 cBloom = bloomTex.Sample(ssHdrLinearClamp, ScreenTC);
	float2 vAdaptedLum = adaptedLumTex.Sample(ssHdrLinearClamp, baseTC.xy);

#if _RT_SAMPLE3 && ENABLE_SUNSHAFTS
	float4 sunShafts = sunshaftsTex.Load(SunShafts_SunCol.w * pixelCoord);

  // Apply the colorization (which also includes brightness scaling) in whatever linear/gamma space sun shafts were, before doing any other operation,
  // so to keep it as close to vanilla as possible. "SunShafts_SunCol" is neither in gamma nor linear space, as it's just a colorization vector, and can't really be linearized (also because it has values beyond the 0-1 range).
  // For the "SUNSHAFTS_LOOK_TYPE" 2 case, we boost the tint saturation to make it match SDR colors more.
#if SUNSHAFTS_LOOK_TYPE >= 2
	sunShafts.rgb *= lerp(GetLuminance(SunShafts_SunCol.xyz), SunShafts_SunCol.xyz, 1.25);
#else // SUNSHAFTS_LOOK_TYPE <= 1
	sunShafts.rgb *= SunShafts_SunCol.xyz;
#endif // SUNSHAFTS_LOOK_TYPE >= 2

#if 0 // LUMA FT: Dithering to minimize banding. Unfortunately it doesn't really work. We also also tried it in SunShaftsMaskGen (on the depth buffer) but it also didn't work.
  if (any(sunShafts.rgb != 0))
  {
    ApplyDithering(sunShafts.rgb, baseTC.xy, true, 1.0, 8u, CV_AnimGenParams.z, true);
  }
#endif

// LUMA FT: Branch to give the sun shafts look a different balance between vanilla (SDR reference) and "realism".
// In HDR it's hard to keep the vanilla look as it was often a blob of white. Making that (e.g.) 203 nits in HDR would be too dim,
// while making it match the peak brightness would make it blinding.
#if SUNSHAFTS_LOOK_TYPE <= 1
  // Linearize to emulate SDR blends as close as possible.
  // Given that sun shafts are generated from a linear space back ground and depth, theoretically they are also in linear space, though in reality it doesn't really matter (because it's an "additive" buffer),
  // in the vanilla code they were blended in in gamma space, so linearizing them still looks right (it's optional), even if they were not limited to the 0-1 range (they don't go below it),
  // so they end up reaching very high values after linearization.
  // We linearize with sRGB gamma because this is before the LUT (gamma correction is after).
  sunShafts.rgb = gamma_sRGB_to_linear(sunShafts.rgb);
#else // SUNSHAFTS_LOOK_TYPE > 1
  // Scale back sun shafts to a "neutral" range
  sunShafts.rgb /= SunShaftsBrightnessMultiplier;
#endif // SUNSHAFTS_LOOK_TYPE <= 1
#if SUNSHAFTS_LOOK_TYPE >= 1
  sunShafts.rgb *= 2.5; // Magic number we found empirically that makes sun shafts look better (tested across many game scenes).
#endif // SUNSHAFTS_LOOK_TYPE >= 1
#else // _RT_SAMPLE3 && ENABLE_SUNSHAFTS
  float4 sunShafts = 0;
#endif // _RT_SAMPLE3 && ENABLE_SUNSHAFTS

  float3 cSDRColor;
  outColor.rgb = FilmTonemapping(cSDRColor, cScene, cBloom, sunShafts, vAdaptedLum, fVignetting, true, HDRColorBalance, HDREyeAdaptation, HDRFilmCurve, HDRBloomColor).rgb;

#if 0 //TODOFT4: quick test output saturation: this can break some yellow flying lines LUTs and cause black pixels?(fixed) Why does the restore LUT path non have as many HDR colors?
  outColor.rgb = lerp(GetLuminance(outColor.rgb), outColor.rgb, 2.0);
  cSDRColor.rgb = lerp(GetLuminance(cSDRColor.rgb), cSDRColor.rgb, 2.0);
#endif

#if _RT_SAMPLE3 && TEST_BLOOM_TYPE == 0 && ENABLE_SUNSHAFTS && !ANTICIPATE_SUNSHAFTS
#if 1 // LUMA FT: in HDR, we always blend sun shafts at 100%, given that we have no hard ceiling of 1 (though tonemapping might have already happened). Blending in sun shafts like in Vanilla looks broken in HDR anyway.
  float sunShaftsAlpha = 1.0;
#else
  float3 sunShaftsAlpha = saturate(1.0 - outColor.rgb); // LUMA FT: saturated to avoid alpha flipping on HDR colors
#endif
#if 1
	outColor.rgb += sunShafts.rgb * sunShaftsAlpha; // Blend in ldr sunshafts
  cSDRColor.rgb += sunShafts.rgb * sunShaftsAlpha;
// LUMA FT: even if theoretically this is more correct and vanilla like, we can't do this because the sun center becomes overly too strong due to the pow(),
// and it ends up looking less colorful and more clipped. Maybe we could limit to blending in gamma space within the 0-1 range, but it's not worth the performance cost.
// If using this branch, make sure to also implement "HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS" here, and add to "cSDRColor" as well.
#else
  outColor.rgb = gamma_sRGB_to_linear(linear_to_sRGB_gamma(outColor.rgb) + (sunShafts.rgb * sunShaftsAlpha));
#endif
#endif // _RT_SAMPLE3 && TEST_BLOOM_TYPE == 0 && ENABLE_SUNSHAFTS && !ANTICIPATE_SUNSHAFTS
#if _RT_SAMPLE3 && ENABLE_SUNSHAFTS && TEST_SUN_SHAFTS
	outColor.rgb = cSunShafts.rgb;
#endif // _RT_SAMPLE3 && ENABLE_SUNSHAFTS && TEST_SUN_SHAFTS
  
#if ENABLE_COLOR_GRADING_LUT //TODOFT5: text LUT extrapolation one last time
  
  LUTExtrapolationData extrapolationData = DefaultLUTExtrapolationData();
  extrapolationData.inputColor = outColor.rgb;
  extrapolationData.vanillaInputColor = cSDRColor.rgb;

  LUTExtrapolationSettings extrapolationSettings = DefaultLUTExtrapolationSettings();
  extrapolationSettings.enableExtrapolation = bool(ENABLE_LUT_EXTRAPOLATION);
  extrapolationSettings.extrapolationQuality = LUT_EXTRAPOLATION_QUALITY;
#if LUT_EXTRAPOLATION_QUALITY >= 2
  extrapolationSettings.backwardsAmount = 2.0 / 3.0;
#endif
  // Empirically found value for Prey LUTs. Anything less will be too compressed, anything more won't have a noticieable effect.
  // This helps keep the extrapolated LUT colors at bay, avoiding them being overly saturated or overly desaturated.
  // At this point, Prey can have colors with brightness beyond 35000 nits, so obviously they need compressing.
  extrapolationSettings.inputTonemapToPeakWhiteNits = 1000.0; // Relative to "extrapolationSettings.whiteLevelNits"
  // Empirically found value for Prey LUTs. This helps to desaturate extrapolated colors more towards their Vanilla (HDR tonemapper but clipped) counterpart, often resulting in a more pleasing and consistent look.
  // This can sometimes look worse, but this value is balanced to avoid hue shifts.
  extrapolationSettings.clampedLUTRestorationAmount = 1.0 / 4.0;
  // Empirically found value for Prey LUTs. This helps to avoid staying too much from the SDR tonemapper Vanilla colors, which gave certain colors (and hues) to highlights.
  // We don't want to go too high, as SDR highlights hues were very distorted by the SDR tonemapper, and they often don't even match the diffuse color around the scene emitted by them (because it wasn't as bright and thus wouldn't have distorted),
  // so they can feel out of place.
  static const float vanillaLUTRestorationAmount = 1.0 / 3.0;
  extrapolationSettings.vanillaLUTRestorationAmount = vanillaLUTRestorationAmount;
  extrapolationSettings.inputLinear = true;
  extrapolationSettings.lutInputLinear = false;
  extrapolationSettings.lutOutputLinear = bool(ENABLE_LINEAR_COLOR_GRADING_LUT);
  extrapolationSettings.outputLinear = bool(POST_PROCESS_SPACE_TYPE >= 1);
  extrapolationSettings.transferFunctionIn = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB;
  // If we are working in gamma space ("POST_PROCESS_SPACE_TYPE" 0), we don't want gamma correction to be applied on the output color (beyond 0-1),
  // it will be up to the last pass to linearize that with the target gamma (which will automatically apply the correction)
  extrapolationSettings.transferFunctionOut = (bool(POST_PROCESS_SPACE_TYPE == 1) && (GAMMA_CORRECTION_TYPE == 1 || (GAMMA_CORRECTION_TYPE >= 2 && ANTICIPATE_ADVANCED_GAMMA_CORRECTION))) ? (GAMMA_CORRECTION_TYPE == 1 ? LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2 : LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB_WITH_GAMMA_2_2_LUMINANCE) : LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB;
  extrapolationSettings.samplingQuality = (HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS || ENABLE_LUT_TETRAHEDRAL_INTERPOLATION) ? (ENABLE_LUT_TETRAHEDRAL_INTERPOLATION ? 2 : 1) : 0;
#if DEVELOPMENT && 1 // Test LUT extrapolation parameters //TODOFT4 (//)
  extrapolationSettings.inputTonemapToPeakWhiteNits = 10000 * LumaSettings.DevSetting01;
  extrapolationSettings.neutralLUTRestorationAmount = LumaSettings.DevSetting02;
  extrapolationSettings.clampedLUTRestorationAmount = LumaSettings.DevSetting05;
  extrapolationSettings.vanillaLUTRestorationAmount = LumaSettings.DevSetting07;
  extrapolationSettings.extrapolationQuality = LumaSettings.DevSetting03 * 2.99;
  extrapolationSettings.backwardsAmount = LumaSettings.DevSetting04;
  //if (extrapolationSettings.extrapolationQuality >= 2) extrapolationSettings.backwardsAmount = 2.0 / 3.0;
  //extrapolationSettings.fixExtrapolationInvalidColors = LumaSettings.DevSetting05 >= 0.5;
  //extrapolationSettings.samplingQuality = (LumaSettings.DevSetting06 >= 0.5) ? 1 : 0; // Only makes a difference if "ENABLE_LINEAR_COLOR_GRADING_LUT" is true
#else //TODOFT5: we found that these looks best (at least under some scenes)
  //extrapolationSettings.inputTonemapToPeakWhiteNits = 0; 
  extrapolationSettings.backwardsAmount = 0.75;
#endif

#if ENABLE_LUT_EXTRAPOLATION
  outColor.rgb = SampleLUTWithExtrapolation(colorChartTex, ssHdrLinearClamp, extrapolationData, extrapolationSettings); // LUMA FT: replaced from "TexColorChart2D()"
#else
  extrapolationData.vanillaInputColor = 0;
  extrapolationSettings.vanillaLUTRestorationAmount = 0;
  // Force linear out, though as above, we only do gamma correction depending on the "POST_PROCESS_SPACE_TYPE" (we can restore its effects onto the HDR color) (theoretically we could also do it later manually, possibly with better results).
  extrapolationSettings.outputLinear = true;
  float3 cSDRColorLUT = SampleLUTWithExtrapolation(colorChartTex, ssHdrLinearClamp, extrapolationData, extrapolationSettings);

  // Fast and simple LUT "restoration": note that shifts the whole LUT range, not just HDR values beyond 0-1 as proper LUT extrapolation did.
	// Extrapolate colors beyond the 0-1 input coordinates by re-applying the same color offset ratio the LUT applied to the clamped color.
	// NOTE: this might slightly shift the output hues from what the LUT dictacted depending on how far the input is from the 0-1 range,
	// though we generally don't care about it as the positives outweight the negatives (edge cases).
  // It generally looks good and is fairly accurate, though it can either desaturate or saturate too much, because bright colors would have always been near white in SDR, so they map to a very limited LUT range.
  // This doesn't really generate any colors beyond sRGB, because it works by restoring the rgb ratio of change, so unless there were negative color values to begin with, there won't be any in the output.
  // We clamp the SDR color to make sure the LUT restoration starts from the right point, if the SDR color was beyond 1, it would later be clamped to 1 to sample the LUT, and we don't want to restore the effects of that clamping on the HDR color.
	outColor.rgb = RestorePostProcess(outColor.rgb, saturate(cSDRColor.rgb), cSDRColorLUT, vanillaLUTRestorationAmount);
#endif

#else // !ENABLE_COLOR_GRADING_LUT

#if POST_PROCESS_SPACE_TYPE == 1 && (GAMMA_CORRECTION_TYPE == 1 || (GAMMA_CORRECTION_TYPE >= 2 && ANTICIPATE_ADVANCED_GAMMA_CORRECTION))
  // Apply gamma correction (only in the 0-1 range) even if we are skipping the LUT
  ColorGradingLUTTransferFunctionInOutCorrected(outColor.rgb, LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB, GAMMA_CORRECTION_TYPE == 1 ? LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2 : LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB_WITH_GAMMA_2_2_LUMINANCE, true);
#endif

#endif // ENABLE_COLOR_GRADING_LUT

#if _RT_SAMPLE2
  // LUMA FT: this now applies in linear space depending on "POST_PROCESS_SPACE_TYPE", theoretically it should look similar and possibly even better than it did in gamma space
  //TODO LUMA: any shader that runs after this isn't aware of the desaturation effect, in particular, lens optics effects in "PostAAComposites" don't get desaturated.
  //We could store the desaturation amount in the alpha channel if FXAA wasn't running and also desaturate lens optics (at the moment they kinda stick out more then they should in this case).
	ApplyArkDistanceSat(outColor.rgb, pixelCoord);
#endif // _RT_SAMPLE2

	float paperWhite = 1.0;
#if POST_PROCESS_SPACE_TYPE >= 1
  // Scale back to paper white levels of brightness after applying the LUT
	paperWhite = GamePaperWhiteNits / sRGB_WhiteLevelNits;
	outColor.rgb *= paperWhite;
#endif // POST_PROCESS_SPACE_TYPE >= 1

#if ENABLE_DITHERING && !DELAY_DITHERING // Delayed dithering to happen after AA and other post processed, this is especially good if "POST_PROCESS_SPACE_TYPE" is 1+
  bool gammaSpace = bool(POST_PROCESS_SPACE_TYPE <= 0);
  ApplyDithering(outColor.rgb, baseTC.xy, gammaSpace, paperWhite);
#endif // ENABLE_DITHERING && !DELAY_DITHERING

#if 0 //TODOFT4: test DLSS scRGB passthrough
  outColor.rgb = lerp(GetLuminance(outColor.rgb), outColor.rgb, 5);
  //outColor.b -= GetLuminance(outColor.rgb) / 2.0;
  FixColorGradingLUTNegativeLuminance(outColor.xyz);
#endif

#if TEST_TONEMAP_OUTPUT
  TestOutput(outColor.rgb);
#endif // TEST_TONEMAP_OUTPUT

#if _RT_SAMPLE0
  // FXAA
#if POST_PROCESS_SPACE_TYPE >= 1 // LUMA FT: FXAA needs luma, not luminance
  outColor.w = GetLuminance(linear_to_game_gamma_mirrored(outColor.xyz / paperWhite)); 
#else // POST_PROCESS_SPACE_TYPE <= 0
  outColor.w = GetLuminance(outColor.xyz) / paperWhite; // LUMA FT: fixed BT.601 luminance being erroneously used
#endif // POST_PROCESS_SPACE_TYPE >= 1
#else // _RT_SAMPLE0
  outColor.w = 0;
#endif // _RT_SAMPLE0
}