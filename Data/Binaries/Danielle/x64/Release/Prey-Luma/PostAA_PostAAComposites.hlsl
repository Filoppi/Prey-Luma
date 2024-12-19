#include "include/Common.hlsl"
#include "include/Tonemap.hlsl"
#include "include/RCAS.hlsl"
#include "include/LensDistortion.hlsl"

#include "include/CBuffer_PerViewGlobal.hlsl"

cbuffer CBComposites : register(b0)
{
  struct
  {
    float Sharpening;
    float ChromaShift;
    float MinExposure;
    float MaxExposure;
    float FilmGrainAmount;
    float FilmGrainTime;
    float __padding;
    float VignetteFalloff;
    float4 VignetteBorder;
    float4 VignetteColor;
  } cbComposites : packoffset(c0);
}

SamplerState ssCompositeSource : register(s0);
SamplerState ssLensOptics : register(s5);
SamplerState ssFilmGrain : register(s6);
SamplerState ssSceneLum : register(s7);
SamplerState ssCompositeSourceAnisotropicEdges : register(s10); // LUMA FT: added this for lens distortion
Texture2D<float4> compositeSourceTex : register(t0);
Texture2D<float4> lensOpticsTex : register(t5);
Texture3D<float4> filmGrainTex : register(t6);
Texture2D<float2> SceneLumTex : register(t7);
Texture2D<float2> dummyFloat2Texture : register(t8); // LUMA FT

float2 MapViewportToRaster(float2 normalizedViewportPos, float2 HPosScale /*= CV_HPosScale.xy*/)
{
		return normalizedViewportPos * HPosScale;
}

float GetExposure(float2 scaledTC)
{
#if 1 // This is a 1x1 texture, so any UV will return the same value
  const float fSceneLum = SceneLumTex.Load(0).x;
#else
	const float fSceneLum = SceneLumTex.Sample(ssSceneLum, scaledTC).x;
#endif
	const float fSceneKey = 1.03 - 2.0 / (2.0 + log2(fSceneLum + 1.0));
	float fExposure = fSceneKey / fSceneLum;
#if ENABLE_EXPOSURE_CLAMPING
	fExposure = clamp(fExposure, cbComposites.MinExposure, cbComposites.MaxExposure);
#endif
  return fExposure;
}

// Grain technique was moved out of HDR post process, since TAA filters pixel sized noise out
void ApplyFilmGrain(inout float4 cScene, float2 baseTC, float fExposure)
{
	// Film grain simulation
  static const float FilmGrainInvPixelSize = 4.0; // LUMA FT: separated hardcoded pixel scale (higher is smaller)
  // LUMA FT: the film grain updates at a fixed frequency (something like 24fps) and it has noticeably visible repeated patters,
  // it would be cool to improve this, at least with optional user settings, but it's not really worth it (it's possible to use a scaled up "CV_AnimGenParams.z" as source time).
  float filmGrainTime = cbComposites.FilmGrainTime * 3.0;
  float filmGrainAspectRatio = CV_ScreenSize.w / CV_ScreenSize.z; // LUMA FT: fixed aspect ratio not being 100% accurate when we had a rendering resolution scale
	float fFilmGrain = filmGrainTex.Sample(ssFilmGrain, float3(baseTC.xy * FilmGrainInvPixelSize * float2(filmGrainAspectRatio, 1.0f), filmGrainTime)).x;
	fFilmGrain = lerp(0.5, fFilmGrain, cbComposites.FilmGrainAmount); // LUMA FT: the game seems to default to a film grain intensity around 0.15
	fFilmGrain = lerp(fFilmGrain, 0.5, sqrt(fExposure));

// LUMA FT: clamp+tonemap (quick Reinhard, in gamma space) the film grain scene color and make film grain additive for better compatibility with the HDR tonemapper (negative values are fine, but values beyond 1 were not handled right),
// otherwise scene values beyond 1 (or below 0) could create weird results. We do this independently of "DELAY_HDR_TONEMAP", even if in case it's false, this would also depend on the peak brightness (theoretically we should invert our own tonemapper).
// An alternative would be to apply it in PQ or log10 encoding, but this is good enough for now.
#if TONEMAP_TYPE >= 1
  static const float TonemapShoulderStart = 0.5;
  float3 cSceneTonemapped = cScene.rgb / (1.0 + max(cScene.rgb, 0));
	float3 cScenePrepared = lerp(cScene.rgb, cSceneTonemapped, saturate((cScene.rgb - TonemapShoulderStart) / (1.0 - TonemapShoulderStart)));
#else // Vanilla code
  float3 cScenePrepared = cScene.rgb;
#endif
	float3 cSceneStepped = step(0.5, cScenePrepared.rgb); // Branch on middle value for each channel (gamma space)
  float3 cShadowFilmGrain = cScenePrepared.rgb * fFilmGrain * 2.0;
  float3 cHighlightFilmGrain = 1.0 - (2.0 * (1.0 - cScenePrepared.rgb) * (1.0 - fFilmGrain));
	float3 cSceneFilmGrain = lerp(cShadowFilmGrain, cHighlightFilmGrain, cSceneStepped.xyz); // Overlay blending
#if TONEMAP_TYPE >= 1 // Similar to "HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS" implementations
	float3 cAdditiveFilmGrain = cSceneFilmGrain - cScenePrepared.xyz;
  cScene.rgb += cAdditiveFilmGrain;
#else // Vanilla code
  cScene.rgb = cSceneFilmGrain;
#endif
}

void ApplyVignette(inout float4 cScene, in float4 cVignette, float2 baseTC)
{
#if 0 // Test vignette (purple)
  cVignette.rgb = float4(1.0, 0.0, 1.0, 0.5);
  cbComposites.VignetteFalloff = 0.667; // Higher means slower falloff (more vignette in the center)
  cbComposites.VignetteBorder = 1.0;
#endif

	float2 vguv = baseTC * 2.0 - 1.0; // UV to NDC

  // LUMA FT: this multiplication prevents vignette from working, supposedly these cvars aren't set if vignette isn't active (there's another vignette implementation in Prey)
	vguv.x *= vguv.x < 0 ? cbComposites.VignetteBorder.x : cbComposites.VignetteBorder.y;
	vguv.y *= vguv.y < 0 ? cbComposites.VignetteBorder.z : cbComposites.VignetteBorder.w;

  // LUMA FT: this is ultrawide friendly
	float fDist = sqrt(dot(vguv, vguv)) * cbComposites.VignetteFalloff;
	float fVignette = fDist*fDist + 1.0;
	fVignette = rcp(fVignette*fVignette);

	cVignette.a *= (1-fVignette);
	
  // Straight alpha multiplication
	cScene.rgb = (cVignette.rgb * cVignette.a) + (cScene.rgb * (1.0 - cVignette.a));
}

void ApplyLensOptics(inout float4 cScene, float2 scaledTC, float2 invRendRes, float fExposure)
{
#if _RT_SAMPLE1

  // LUMA FT: we don't use "ssCompositeSourceAnisotropicEdges" for lens distortion, it shouldn't really matter
	float4 cLensOpticsComposite = lensOpticsTex.Sample(ssLensOptics, scaledTC.xy); // LUMA FT: "Load()" doesn't seem to work here (sun becomes green?), maybe the texture is scaled by DRS even with DLSS (if it was, then clamping to "CV_HPosClamp.xy" wouldn't be right anymore), or maybe they use a weird sampler
#if _RT_SAMPLE3 && ENABLE_CHROMATIC_ABERRATION // LUMA FT: it's fine to do this after lens distortion is applied to the UV anyway (it should roughly look the same as distoring the UV twice again)!
	float2 vTexelShift = invRendRes * cbComposites.ChromaShift;
  float2 baseTCCenter = 0.5 * CV_HPosScale.xy; // LUMA FT: fixed source texture center not acknowledging resolution scaling (MapViewportToRaster()), resulting in CA being different depending on the resolution
	cLensOpticsComposite.r = lensOpticsTex.Sample(ssLensOptics, clamp((scaledTC.xy - baseTCCenter) * (1 + vTexelShift) + baseTCCenter, 0.f, CV_HPosClamp.xy)).r; // LUMA FT: fixed missing clamp to "CV_HPosClamp", and added clamp to 0 for lens distortion
#endif // _RT_SAMPLE3 && ENABLE_CHROMATIC_ABERRATION

#if !ENABLE_LENS_OPTICS
		cLensOpticsComposite = 0;
#elif TEST_LENS_OPTICS_TYPE == 1
    if (any(cLensOpticsComposite.rgb != 0) || !(cLensOpticsComposite.a == 1.0 || cLensOpticsComposite.a == 0.0))
    {
		  cScene.rgb = float3(0, 1, 1);
      cLensOpticsComposite = 0;
    }
#elif TEST_LENS_OPTICS_TYPE == 2
    cScene.rgb = 0;
#endif

// LUMA FT: add multiplication by exposure for consistency (the color of lens optics never depends on the post tonemapping background color, so they don't get double exposed).
// It's arguable whether they should be affected, but for visual consistency, given that theoretically they are generated from world images, they should also be affected by exposure,
// and visually it should look more consistent.
#if ENABLE_LENS_OPTICS_HDR
  cLensOpticsComposite.rgb *= lerp(1.0, fExposure, SunShaftsAndLensOpticsExposureAlpha);
#endif // ENABLE_LENS_OPTICS_HDR

// LUMA FT: By default, lens optics passes are rendered, tonemapped to a 0-1 range without applying gamma, and then stored on a R11G11B10F texture.
// Here, they were blended in in gamma space, without gamma correction. It's unclear why they were rendered in "baked" gamma space but stored on linear float textures.
// Possibly there was a mistake and they were meant to be linear and not treated as gamma space; it's unclear,
// but to emulate the vanilla look (after empirical tests), we assume their color was meant to be in gamma space (it's fair to assume so for anything that is additive to gamma space backgrounds).
// We tried linearizing their values before writing on their texture (independently of "POST_PROCESS_SPACE_TYPE"), which should retain more quality given they were stored on R11G11B10F low quality linear buffers (now R16G16B16A16F),
// but their blends end up looking different then.
// Note that we tried to scale them to HDR with some AutoHDR algorithm here, but it did not look good, so we implemented it directly in their drawing, in each pass.
#if ENABLE_LENS_OPTICS_HDR // LUMA FT: unlock full additive range, in HDR there's no need to only blend in part of them if the background is already white (also because we moved tonemapping to be after this). Its alpha is meant to be ignored anyway.
		  cScene.rgb += cLensOpticsComposite.rgb;
#else // !ENABLE_LENS_OPTICS_HDR
#if 1 // LUMA FT: this emulates SDR behaviour relatively accurately but looks a bit better (despite possibly clipping more), still, it does not look right in HDR as the background is already too bright due to the tonemapped sun shafts.
		  cScene.rgb += cLensOpticsComposite.rgb * (1.0-saturate(GetLuminance(cScene.rgb)));
#else
		  cScene.rgb += cLensOpticsComposite.rgb * (1.0-saturate(cScene.rgb)); // Crytek: should blend in linear space, but increases cost further
#endif
#endif // ENABLE_LENS_OPTICS_HDR

#endif // _RT_SAMPLE1
}

// This runs after any form of AA and before upscaling/MSAA.
// This uses "FullscreenTriVS" so "baseTC.xy" is always in 0-1 range (of the viewport).
void PostAAComposites_PS(float4 WPos, float4 baseTC, out float4 outColor)
{
  bool gammaSpace = bool(POST_PROCESS_SPACE_TYPE <= 0);
  
#if !ENABLE_SCREEN_DISTORTION // Shortcut to disable all of it
  LumaSettings.LensDistortion = false;
#endif // !ENABLE_SCREEN_DISTORTION
  
  float2 distortedTC = baseTC.xy;
  float2 invDistortedTC = baseTC.xy;
  float4 distortedHPosClamp = float4(0.0, 0.0, CV_HPosClamp.xy); // The clamps for pre-distorted images (it'd go beyond the lens distortion black edges if beyond this). left min, top min, right max, bottom max
  float borderAlpha = 0.f;
  if (LumaSettings.LensDistortion)
  {
    float2 outputResolution = 0.5 / CV_ScreenSize.zw; // Using "CV_ScreenSize.xy" directly would probably also be fine given this is always meant to be done after upscaling
    float FOVX = 1.0 / CV_ProjRatio.z;
    distortedTC = PerfectPerspectiveLensDistortion(distortedTC, FOVX, outputResolution, borderAlpha);
    // Caclulate what UV a border (e.g. 1 1, bottom right) would match to, so that we start applying vignette from there
    invDistortedTC = PerfectPerspectiveLensDistortion_Inverse(invDistortedTC, FOVX, outputResolution);
    invDistortedTC = saturate(baseTC.xy + (baseTC.xy - distortedTC)); //TODOFT: fix formula above
  }
	
  // LUMA: If DLSS run, buffers would have already been upscaled, so we want to ignore the logic that acknowledges a different rendering resolution here (CV_HPosScale.xy would have also been replaced by c++ code to be 1, and "CV_HPosClamp" too).

  // Scale down the UVs to map to the top left portion of the source texture, in case the resolution scale was < 1 (so UVs might scale to a range smaller than 0-1) (this does nothing with DLSS)
  // Note that clamping "scaledTC" is kinda useless as the source would already be within the bounds, but ... we do it anyway
  float2 scaledTC = min(MapViewportToRaster(baseTC.xy, CV_HPosScale.xy), CV_HPosClamp.xy);

  float2 forcedHPosClamp = LumaData.RenderResolutionScale - CV_ScreenSize.zw;
  float2 distortedScaledTC = clamp(MapViewportToRaster(distortedTC.xy, CV_HPosScale.xy), 0.0, CV_HPosClamp.xy); // Clamped to 0 for lens distortion support
  float2 distortedForcedScaledTC = clamp(MapViewportToRaster(distortedTC.xy, LumaData.RenderResolutionScale), 0.0, forcedHPosClamp); // Given that "CV_HPosScale" might be 1, use the real rendering resolution scale, in case we needed it for anything (e.g. sampling from the depth buffer)

#if 1 // LUMA FT: Optimization assuming a standard bilinear sampler would always have been used here
  outColor = compositeSourceTex.Load(WPos.xyz);
#else
  outColor = compositeSourceTex.Sample(ssCompositeSource, scaledTC.xy);
#endif
#if 0 // Show sharpening map (for fun)
  outColor = 1;
#endif
  if (LumaSettings.LensDistortion)
  {
    // Early out if the borders are black, we don't want to draw film grain on top of it!
    if (outColor.a <= 0.0)
    {
      outColor = 0;
      return;
    }
    // If we have a border, block as samples beyond it to avoid random sharpening results (e.g. over sharpened borders)
    if (outColor.a < 1.0 - FLT_EPSILON)
    {
      distortedHPosClamp = float4(scaledTC, scaledTC);
    }
  }

  // LUMA FT: fixed sharpening chromatic aberration using the wrong inverse resolution for sampling (they used "invOutputRes", which isn't adjusted by the rendering resolution scale),
  // though it's arguable whether sharpening should always be run by sampling the mid point between this texel and the adjacent ones, or if it can also sample UVs closer to the current texel (scaled by rend res scale): both have pros and cons).
  float2 invOutputRes = CV_ScreenSize.zw * 2.0; // MapViewportToRaster()
  float2 invRenderingRes = 1.0 / CV_ScreenSize.xy;

#if TEST_DYNAMIC_RESOLUTION_SCALING
  if (WPos.x < 250.0 * (CV_HPosScale.x / LumaData.RenderResolutionScale.x) && WPos.y < 250.0 * (CV_HPosScale.y / LumaData.RenderResolutionScale.y))
  {
    outColor = 1.0;
    return;
  }
#endif
#if TEST_EXPOSURE
  if (WPos.x >= 250 && WPos.x < 500 && WPos.y < 250)
  {
    outColor = GetExposure(scaledTC.xy);
    return;
  }
#endif

	if (ShouldSkipPostProcess(WPos.xy / CV_HPosScale.xy)) { return; }

	float paperWhite = GamePaperWhiteNits / sRGB_WhiteLevelNits;
	
// LUMA FT: this will seemengly only run when using SMAA 2TX and TAA
#if _RT_SAMPLE2 && POST_TAA_SHARPENING_TYPE > 0

  float sharpenAmount = cbComposites.Sharpening;
#if !ENABLE_SHARPENING
  sharpenAmount = min(sharpenAmount, 1.0);
#endif // !ENABLE_SHARPENING

#if ENABLE_SHARPENING && POST_TAA_SHARPENING_TYPE >= 2 // LUMA FT: added RCAS instead of basic sharpening

  float normalizationRange = 1.0;
#if POST_PROCESS_SPACE_TYPE >= 1
  normalizationRange = paperWhite;
#endif

  sharpenAmount -= 1.0; // Scale to the expected range
  //TODOFT: expose extra post TAA/DLSS sharpening multiplier (and hide the newly exposed sharpening setting?, replace other sharpening implementations with RCAS?)
  if (LumaSettings.DLSS) // Heuristically found scale to match the native game's TAA sharpness with DLAA
  {
    sharpenAmount *= 2.0;
  }
  if (LumaSettings.LensDistortion) // Heuristically found (it makes little difference but helps a bit)
  {
    sharpenAmount *= 2.25;
  }
  //TODO LUMA: pass in motion vectors to either increase or reduce sharpening on moving pixels (increase if it they were blurry, decreate it if they had sharpening artifacts)
  // This is probably fine, this code path is never used for "blurring", it's always exclusively for sharpening.
  // This should work independently of "POST_PROCESS_SPACE_TYPE".
	outColor.rgb = RCAS(WPos.xy, distortedHPosClamp.xy / invOutputRes, distortedHPosClamp.zw / invOutputRes, sharpenAmount, compositeSourceTex, dummyFloat2Texture, normalizationRange, true, outColor, false).rgb;

#else // POST_TAA_SHARPENING_TYPE <= 1

  if (LumaSettings.LensDistortion) // Heuristically found
  {
    sharpenAmount *= 1.25;
  }

	// Apply sharpening
  //TODO LUMA: we could do this with .Load() to gain performance
	float3 cTL = DecodeBackBufferToLinearSDRRange(compositeSourceTex.Sample(ssCompositeSource, clamp(scaledTC + invRenderingRes * float2(-0.5, -0.5), distortedHPosClamp.xy, distortedHPosClamp.zw)).rgb);
	float3 cTR = DecodeBackBufferToLinearSDRRange(compositeSourceTex.Sample(ssCompositeSource, clamp(scaledTC + invRenderingRes * float2( 0.5, -0.5), distortedHPosClamp.xy, distortedHPosClamp.zw)).rgb);
	float3 cBL = DecodeBackBufferToLinearSDRRange(compositeSourceTex.Sample(ssCompositeSource, clamp(scaledTC + invRenderingRes * float2(-0.5,  0.5), distortedHPosClamp.xy, distortedHPosClamp.zw)).rgb);
	float3 cBR = DecodeBackBufferToLinearSDRRange(compositeSourceTex.Sample(ssCompositeSource, clamp(scaledTC + invRenderingRes * float2( 0.5,  0.5), distortedHPosClamp.xy, distortedHPosClamp.zw)).rgb);

	float3 cFiltered = (cTL + cTR + cBL + cBR) * 0.25;
  float3 preSharpenColor = outColor.rgb;
	outColor.rgb = EncodeBackBufferFromLinearSDRRange(lerp( cFiltered, DecodeBackBufferToLinearSDRRange(outColor.rgb), sharpenAmount )); // LUMA FT: removed saturate() and fixed gamma functions
  // LUMA FT: correct sharpening to avoid negative luminances (invalid colors) on rapidly changing colors (they create rings artifacts). This should work independently of "POST_PROCESS_SPACE_TYPE".
	outColor.rgb = FixUpSharpeningOrBlurring(outColor.rgb, preSharpenColor);

#endif // ENABLE_SHARPENING && POST_TAA_SHARPENING_TYPE >= 2

#endif // _RT_SAMPLE2 && POST_TAA_SHARPENING_TYPE > 0

#if POST_PROCESS_SPACE_TYPE >= 1 // LUMA FT: added support for linear space input, making sure we blend in vignette and film grain and lens component in "SDR" gamma space
  float3 preEffectsLinearColor = outColor.rgb;
	outColor.rgb = linear_to_game_gamma(outColor.rgb / paperWhite);
  gammaSpace = true;
  float3 preEffectsGammaColor = outColor.rgb;
#endif

  float fExposure = GetExposure(distortedForcedScaledTC.xy);

  // Apply lens composite
  // LUMA FT: with DLSS, lens optics are also rendered at full resolution (we distort their UV too!)
  // We had not distorted them in the previous pass because their distortion quality doesn't really matter and can be approximate.
  // Ideally maybe we shouldn't distort these, but to do that we'd need to change their vertices etc, which we don't have easy access to (if we don't distort them, they won't match the scene!)
  ApplyLensOptics(outColor, distortedScaledTC.xy, invRenderingRes, fExposure);

// LUMA FT: moved vignette and film grain after lens optics, as especially with "ENABLE_LENS_OPTICS_HDR", they now render in "HDR"
// We still keep the sharpening before them, as sharpening is mostly meant to be to counter the blurriner from TAA, which didn't affect lens optics.

#if ENABLE_VIGNETTE
  // LUMA FT: vignette might have been best applied before lens distortion but it doesn't really matter, we apply it on the inverse UVs to make it match perfectly
	ApplyVignette(outColor, cbComposites.VignetteColor, invDistortedTC.xy);
#endif // ENABLE_VIGNETTE

#if ENABLE_FILM_GRAIN
  // LUMA FT: we are now passing in "baseTC" instead of "scaledTC" (or "distortedForcedScaledTC") to avoid film grain scaling in size with the dynamic render resolution scale
  // (otherwise now we'd draw the film grain now acknowledging the render scale and then it'd be scaled again later, so basically we draw it pretending we are running at "native res" but in the "render res" portion, so that DRS upscaling later will apply the opposite scaling and normalize it out)
	ApplyFilmGrain(outColor, baseTC.xy, fExposure);
#endif // ENABLE_FILM_GRAIN

// It's better to do tonemapping here than in "HDRPostProces.cfx" "HDRFinalScenePS", as this is after AA and after some other additive post process effects are drawn (e.g. tonemapping after film grain is good otherwise it could clip and not be visible on top of peak white backgrounds).
// Ideally we'd do tonemapping and dithering even later, in "PostEffectsGame.cfx" "UberGamePostProcess" but that's not always run.
// Note that this is more like a simple "display mapping" pass, it doesn't really change shadows and mid tones.
#if DELAY_HDR_TONEMAP

#if POST_PROCESS_SPACE_TYPE >= 1 && HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS
  outColor.rgb = preEffectsLinearColor + ((game_gamma_to_linear(outColor.rgb) - game_gamma_to_linear(preEffectsGammaColor)) * paperWhite);
#else
  float3 preTonemapGammaColor = outColor.rgb;
  float3 preTonemapLinearColor = game_gamma_to_linear(outColor.rgb); // Not scaled by paper white
	outColor.rgb = preTonemapLinearColor * paperWhite;
#endif
  gammaSpace = false;

#if TEST_HIGH_SATURATION_GAMUT
  static const float extraSaturationTest = 1.0;
  outColor.rgb = lerp(GetLuminance(outColor.rgb), outColor.rgb, 1.0 + extraSaturationTest);
#endif // TEST_HIGH_SATURATION_GAMUT

	const float peakWhite = PeakWhiteNits / sRGB_WhiteLevelNits;
  outColor.rgb = Tonemap_DICE(outColor.rgb, peakWhite);

#endif // DELAY_HDR_TONEMAP

#if POST_PROCESS_SPACE_TYPE <= 0 || POST_PROCESS_SPACE_TYPE >= 2 // Given this is the last conventional shader to always run and have a major effect on the picture, convert to gamma space here if necessary, we'll linearize with the same gamma at the end
  if (!gammaSpace) // This can only happen if "DELAY_HDR_TONEMAP" was true
  {
#if HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS && DELAY_HDR_TONEMAP && POST_PROCESS_SPACE_TYPE <= 0
    outColor.rgb = preTonemapGammaColor + (linear_to_game_gamma(outColor.rgb / paperWhite) - linear_to_game_gamma(preTonemapLinearColor));
#else
    outColor.rgb = linear_to_game_gamma(outColor.rgb / paperWhite);
#endif
    gammaSpace = true;
  }
#else // POST_PROCESS_SPACE_TYPE == 1
  if (gammaSpace) // This can only happen if "DELAY_HDR_TONEMAP" was false
  {
#if POST_PROCESS_SPACE_TYPE >= 1 && HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS
    outColor.rgb = preEffectsLinearColor + ((game_gamma_to_linear(outColor.rgb) - game_gamma_to_linear(preEffectsGammaColor)) * paperWhite);
#else
    outColor.rgb = game_gamma_to_linear(outColor.rgb) * paperWhite;
#endif
    gammaSpace = false;
  }
#endif // POST_PROCESS_SPACE_TYPE <= 0 || POST_PROCESS_SPACE_TYPE >= 2

#if ENABLE_DITHERING && DELAY_DITHERING // LUMA FT: moved dithering here, it's good to do it after tonemapping and AA etc
  if (all(CV_HPosScale.xy >= 1.0)) // It's done in "UpscaleImagePS" for the upscaling case, so it's per pixel
  {
    ApplyDithering(outColor.rgb, baseTC.xy, gammaSpace, gammaSpace ? 1.0 : paperWhite, DITHERING_BIT_DEPTH, CV_AnimGenParams.z, true);
  }
#endif // ENABLE_DITHERING && DELAY_DITHERING

	// Range rescaling
#if _RT_SAMPLE4 // LUMA FT: some full->limited range encoding, meant to be done in gamma space, probably unused
	outColor.xyz = (16.0/255.0) + outColor.xyz * ((235.0 - 16.0) / 255.0);
#endif // _RT_SAMPLE4

#if 0 // Test SDR like (clipped) output
  outColor.rgb /= gammaSpace ? 1.0 : paperWhite;
  outColor.rgb = saturate(outColor.rgb);
  outColor.rgb *= gammaSpace ? 1.0 : paperWhite;
#endif
}