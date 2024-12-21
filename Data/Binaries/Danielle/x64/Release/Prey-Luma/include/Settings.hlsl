/////////////////////////////////////////
// Prey LUMA advanced settings
// (note that the defaults might be mirrored in cpp, the shader values will be overridden anyway)
/////////////////////////////////////////

// Whether we store the post process buffers in linear space scRGB or gamma space (sRGB under normal circumstances) (like in the vanilla game, though now we use FP16 textures as opposed to UNORM8 ones).
// Note that converting between linear and gamma space back and forth results in quality loss, especially over very high and very low values, so this is best left on.
// 0 Gamma space:
//   vanilla like (including UI), but on float/linear buffers
//   this has as tiny loss of quality due to storing sRGB gamma space on linear buffers
//   gamma correction happens at the end, in the linearization pass
// 1 Linear space:
//   UI looks slightly different from vanilla (in alpha blends)
//   gamma correction happens early, in tonemapping
// 2 Linear space until UI, then gamma space:
//   more specifically, linear until PostAAComposites, included
//   this has the some of the advantage of both linear and gamma methods
//   UI looks like vanilla
//   gamma correction happens at the end, in the linearization pass
//   (to avoid a billion different formulas around the code and to make gamma blends look like vanilla,
//    if we corrected in the tonemap/grading shader, then we'd need to use sRGB gamma in the end,
//    also we wouldn't know whether to correct the 0-1 range of the whole range)
//   ideally we would have gamma corrected before HDR tonemapping, but the complexity cost is too big for the small visual gains
#ifndef POST_PROCESS_SPACE_TYPE
#define POST_PROCESS_SPACE_TYPE 1
#endif
// Higher qualy gamma<->linear conversions, it avoids the error generated from the conversion by restoring the change on the original color in an additive way.
// This has a relatively high performance cost for the visual gains it returns.
#define HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS 1
// The LUMA mod changed LUTs textures from UNORM 8 bit to FP 16 bit, so their sRGB (scRGB) values can theoretically go negative to conserve HDR colors
#define ENABLE_HDR_COLOR_GRADING_LUT 1
// We changed LUT format to R16G16B16A16F so in their mixing shaders, we store them in linear space, for higher output quality and to keep output values beyond 1 (their input coordinates are still in gamma sRGB (and then need 2.2 gamma correction)).
// This has a relatively small performance cost.
#ifndef ENABLE_LINEAR_COLOR_GRADING_LUT
#define ENABLE_LINEAR_COLOR_GRADING_LUT 1
#endif
// As many games, Prey rendered and tonemapped in linear space, though applied the sRGB gamma transfer function to apply the color grading LUT.
// Almost all TVs follow gamma 2.2 and most monitors also do, so to mantain the SDR look (and near black level), we need to linearize with gamma 2.2 and not sRGB (1).
// Disabling this will linearize with gamma sRGB, ignoring that the game would have been developed on (and for) gamma 2.2 displays (<=0).
// If you want something in between, thus keeping the sRGB color hue (channels ratio) but with the gamma 2.2 corrected luminance, set this to a higher value (>=2).
// Note that if "POST_PROCESS_SPACE_TYPE" is 0, this simply determines how gamma is linearized for intermediary operations,
// while everything stays in sRGB gamma (as theoretically it would have been originally, even if it was displayed on 2.2) when stored in textures,
// so this determines how the final shader should linearize (if >=1, from 2.2, if <=0, from sRGB, thus causing raised blacks compared to how the gamma would have appeared on gamma 2.2 displays).
// Note that by gamma correction we mean fixing up the game's bad gamma implementation, though sometimes this term is used to imply "display encoding".
// We do not have a gamma 2.4 setting, because the game was seemengly not meant for that. It'd be easily possible to add one if ever needed (e.g. replace the "DefaultGamma" value, or directly expose that to the user (don't!)).
// 
// 0 sRGB
// 1 Gamma 2.2
// 2 sRGB (color hues) with gamma 2.2 luminance
#ifndef GAMMA_CORRECTION_TYPE
#define GAMMA_CORRECTION_TYPE 1
#endif
// Necessary for HDR to work correctly
#ifndef ENABLE_LUT_EXTRAPOLATION
#define ENABLE_LUT_EXTRAPOLATION 1
#endif
// See "LUTExtrapolationSettings::extrapolationQuality"
#ifndef LUT_EXTRAPOLATION_QUALITY
#define LUT_EXTRAPOLATION_QUALITY 1
#endif
// It's better to leave the classic LUT interpolation (bilinear/trilinear),
// LUTs in Prey are very close to being neutral so tetrahedral interpolation just shifts their colors without gaining much, possibly actually losing quality.
// This is even less necessary while using "ENABLE_LINEAR_COLOR_GRADING_LUT".
#ifndef ENABLE_LUT_TETRAHEDRAL_INTERPOLATION
#define ENABLE_LUT_TETRAHEDRAL_INTERPOLATION 0
#endif
// 0 Vanilla SDR (Hable), 1 Luma HDR (Vanilla+ Hable/DICE mix) (also works for SDR), 2 Untonemapped
#ifndef TONEMAP_TYPE
#define TONEMAP_TYPE 1
#endif
// Better kept to true for sun shafts and lens optics and other post process effects, and AA too.
// Note that this will theoretically apply tonemap on object highlights too, but they are additive and were clipped in SDR, plus we don't really tonemap until beyond 203 nits, so it's ok.
#ifndef TRY_DELAY_HDR_TONEMAP
#define TRY_DELAY_HDR_TONEMAP 1
#endif
#define DELAY_HDR_TONEMAP (TRY_DELAY_HDR_TONEMAP && TONEMAP_TYPE == 1)
// Sun shafts were drawn after tonemapping in the Vanilla game, thus they were completely SDR, Luma has implemented an HDR version of them which tries to retain the artistic direction.
// This looks better as true in SDR too, as it avoids heavy clipping.
#define ANTICIPATE_SUNSHAFTS (!DELAY_HDR_TONEMAP || 1)
// 0 Vanilla
// 1 Medium (Vanilla+)
// 2 High
// 3 Extreme (worst performance)
#define SUNSHAFTS_QUALITY 2
// 0 Raw Vanilla: raw sun shafts values, theoretically close to vanilla but in reality not always, it might not look good
// 1 Vanilla+: similar to vanilla but HDR and tweaked to be even closer (it's more "realistic" than SDR vanilla)
// 2 LUMA HDR: a bit dimmer but more realistic, so it works best with "ENABLE_LENS_OPTICS_HDR", which compensates for the lower brightness/area
#ifndef SUNSHAFTS_LOOK_TYPE
#define SUNSHAFTS_LOOK_TYPE 2
#endif
// Unjitter the sun shafts depth buffer and re-jitter their generation.
// This is because they draw before TAA/DLSS but with screen space logic, so jittering needs to be done manually.
#define REJITTER_SUNSHAFTS 1
// Some lens optics effects (and maybe sun shafts?) did not acknowledge FOV (they drew in screen space, independently of FOV),
// so if you zoomed in, you'd get "smaller" (they'd be the same size in screen space, thus smaller relative to the rest).
// This would theoretically change the intended size of these effects during cutscenes if they changed the FOV from the gameplay one,
// but there really aren't any in Prey.
#define CORRECT_SUNSHAFTS_FOV 1
// Lens optics were clipped to 1 due to being rendered before tonemapping. As long as "DELAY_HDR_TONEMAP" is true, now these will also be tonemapped instead of clipped (even in SDR, so "TONEMAP_TYPE" needs to be HDR).
#if !defined(ENABLE_LENS_OPTICS_HDR) || ENABLE_LENS_OPTICS_HDR >= 1
#undef ENABLE_LENS_OPTICS_HDR
#define ENABLE_LENS_OPTICS_HDR (TONEMAP_TYPE >= 1)
#endif
#ifndef AUTO_HDR_VIDEOS
#define AUTO_HDR_VIDEOS 1
#endif
#define DELAY_DITHERING 1
//TODOFT: test more with this off (which theoretically should be better), and possibly disable it (or remove it if you move the AA pass)
#ifndef DLSS_RELATIVE_PRE_EXPOSURE
#define DLSS_RELATIVE_PRE_EXPOSURE 1
#endif
// Disable to keep the vanilla behaviour of CRT like emulated effects becoming near inperceptible at higher resolutions (which defeats their purpose)
#ifndef CORRECT_CRT_INTERLACING_SIZE
#define CORRECT_CRT_INTERLACING_SIZE 1
#endif
// Disable to force lens distortion to crop all black borders (further increasing FOV is suggested if you turn this off)
#ifndef ALLOW_LENS_DISTORTION_BLACK_BORDERS
#define ALLOW_LENS_DISTORTION_BLACK_BORDERS 1
#endif
// If true, the motion vectors generated for dynamic objects are generated with both the current and previous jitters acknowledged in the calculations (and baked in their velocity, so they wouldn't be zero even if nothing was moving).
// If false, motion vectors are generated (and then interpreted in Motion Blur and TAA) like in the vanilla code, so they kinda include the jitter of the current frame, but not the one from the previous frame, which isn't really great and caused micro shimmers in blur and TAA.
// This needs to be mirrored in c++ so do not change it directly from ere. In post process shaders it simply determines how to interpret/dejitter the MVs. When DLSS is on, the behaviour is always is if this was true.
#ifndef FORCE_MOTION_VECTORS_JITTERED
#define FORCE_MOTION_VECTORS_JITTERED 1
#endif
// Allows to disable this given it might not be liked (it can't be turned off individually) and can make DLSS worse. This needs "r_MotionBlurCameraMotionScale" to not be zero too (it's not by default).
#ifndef ENABLE_CAMERA_MOTION_BLUR
#define ENABLE_CAMERA_MOTION_BLUR 0
#endif
// Do it higher than 8 bit for HDR
#ifndef DITHERING_BIT_DEPTH
#define DITHERING_BIT_DEPTH 9u
#endif
// 0 SSDO (Vanilla, CryEngine)
// 1 GTAO (Luma)
#ifndef SSAO_TYPE
#define SSAO_TYPE 1
#endif
// 0 Vanilla
// 1 High (best balance for 2024 GPUs)
// 2 Extreme (bad performance)
#ifndef SSAO_QUALITY
#define SSAO_QUALITY 1
#endif
// 0 Small (makes the screen space limitations less appearent)
// 1 Vanilla
// 2 Large (can look more realistic, but also over darkening and bring out the screen space limitations (e.g. stuff de-occluding around the edges when turning the camera))
// GTAO only
#ifndef SSAO_RADIUS
#define SSAO_RADIUS 1
#endif
// Makes AO jitter a bit to add blend in more quality over time.
// Requires TAA enabled to not look terrible.
#ifndef ENABLE_SSAO_TEMPORAL
#define ENABLE_SSAO_TEMPORAL 1
#endif
// 0 Vanilla
// 1 High
#ifndef BLOOM_QUALITY
#define BLOOM_QUALITY 1
#endif
// 0 Vanilla (based on user setting)
// 1 Ultra
#ifndef MOTION_BLUR_QUALITY
#define MOTION_BLUR_QUALITY 1
#endif
// 0 Vanilla
// 1 High (best balance)
// 2 Extreme (slow)
#ifndef SSR_QUALITY
#define SSR_QUALITY 1
#endif
// 0 None: disabled
// 1 Vanilla: basic sharpening
// 2 RCAS: AMD improved sharpening
#ifndef POST_TAA_SHARPENING_TYPE
#define POST_TAA_SHARPENING_TYPE 2
#endif
// Disabled as we are now in HDR (10 or 16 bits)
#ifndef ENABLE_DITHERING
#define ENABLE_DITHERING 0
#endif
// Disables development features if off
#ifndef DEVELOPMENT
#define DEVELOPMENT 0
#endif

//TODOFT2: try to boost the chrominance on highlights? Or desaturate, the opposite.
//TODOFT3: lower mid tones to boost highlights? Nah
//TODOFT: add viewport print debug, but it seems like everything uses full viewport
//TODOFT0: disable all dev/debug settings below, even for dev mode
//TODOFT: add test setting to disable all exposure, and see if the game looks more "HDR" (though tonemapping would break...?)
//TODOFT0: fix formatting/spacing of all shaders
//TODOFT: test reflections mips flickering or disappearing?
//TODOFT4: review "D3D11 ERROR: ID3D11DeviceContext::Dispatch: The resource return type for component 0 declared in the shader code (FLOAT) is not compatible with the resource type bound to Unordered Access View slot 0 of the Compute Shader unit (UNORM). This mismatch is invalid if the shader actually uses the view (e.g. it is not skipped due to shader code branching). [ EXECUTION ERROR #2097372: DEVICE_UNORDEREDACCESSVIEW_RETURN_TYPE_MISMATCH]"

/////////////////////////////////////////
// Rendering features toggles (development)
/////////////////////////////////////////

#ifndef ENABLE_POST_PROCESS
#define ENABLE_POST_PROCESS 1
#endif
// The game already has a setting for this
#define ENABLE_MOTION_BLUR (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
#define ENABLE_BLOOM (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
// The game already has a setting for this
#define ENABLE_SSAO (!DEVELOPMENT || 1)
// Spacial (not temporal) SSAO denoising. Needs to be enabled for it to look good.
#define ENABLE_SSAO_DENOISE (!DEVELOPMENT || 1)
// Disables all kinds of AA (SMAA, FXAA, TAA, ...) (disabling "ENABLE_SHARPENING" is also suggested if disabling AA). Doesn't affect DLSS.
#define ENABLE_AA (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
// Optional SMAA pass being run before TAA
#define ENABLE_SMAA (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
// Optional TAA pass being run after the optional SMAA pass
#define ENABLE_TAA (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
#if !defined(ENABLE_COLOR_GRADING_LUT) || !DEVELOPMENT || !ENABLE_POST_PROCESS
#undef ENABLE_COLOR_GRADING_LUT
#define ENABLE_COLOR_GRADING_LUT (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
#endif
// Note that this only disables the tonemap step sun shafts, not the secondary ones from lens optics
#define ENABLE_SUNSHAFTS (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
// Note that these might ignore "ShouldSkipPostProcess()"
#define ENABLE_ARK_CUSTOM_POST_PROCESS (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
#define ENABLE_LENS_OPTICS (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
// Disable this for a softer image
// (not really needed anymore as now we have "POST_TAA_SHARPENING_TYPE" for the TAA sharpening, which is the only one that usually runs)
#if !defined(ENABLE_SHARPENING) || !DEVELOPMENT || !ENABLE_POST_PROCESS
#undef ENABLE_SHARPENING
#define ENABLE_SHARPENING (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
#endif
#define ENABLE_CHROMATIC_ABERRATION (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
// Lens distortion and such
#define ENABLE_SCREEN_DISTORTION (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
#if !defined(ENABLE_VIGNETTE) || !ENABLE_POST_PROCESS
#undef ENABLE_VIGNETTE
#define ENABLE_VIGNETTE (ENABLE_POST_PROCESS && 1)
#endif
// This is used for gameplay effects too, so it's best not disabled
#if !defined(ENABLE_FILM_GRAIN) || !DEVELOPMENT || !ENABLE_POST_PROCESS
#undef ENABLE_FILM_GRAIN
#define ENABLE_FILM_GRAIN (ENABLE_POST_PROCESS && (!DEVELOPMENT || 1))
#endif
// Note: when disabling this, exposure can go to 0 or +INF when the game is paused somehow
#define ENABLE_EXPOSURE_CLAMPING (!DEVELOPMENT || 1)
// This might also disable decals interfaces (like computer screens) in the 3D scene
#define ENABLE_UI (!DEVELOPMENT || 1)

/////////////////////////////////////////
// Debug toggles
/////////////////////////////////////////

// Test extra saturation to see if it passes through (HDR colors)
#define TEST_HIGH_SATURATION_GAMUT (DEVELOPMENT && 0)
#define TEST_TONEMAP_OUTPUT (DEVELOPMENT && 0)
// 0 None
// 1 Neutral LUT
// 2 Neutral LUT + bypass extrapolation
#if !defined(FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE) || !DEVELOPMENT
#undef FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE
#define FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE (DEVELOPMENT && 0)
#endif
#if !defined(DRAW_LUT) || !DEVELOPMENT
#undef DRAW_LUT
#define DRAW_LUT (DEVELOPMENT && 0)
#endif
// Debug LUT Pixel scale (this is rounded to the closest integer value for the size of the LUT)
// 10u is a good value for 2560 horizontal res. 20 for 5120 horizontal res or more.
#define DRAW_LUT_TEXTURE_SCALE 10u
#define TEST_LUT_EXTRAPOLATION (DEVELOPMENT && 0)
#define TEST_LUT (DEVELOPMENT && 1)
#define TEST_TINT (DEVELOPMENT && 1)
// Tests some alpha blends stuff
#define TEST_UI (DEVELOPMENT && 1)
// 0 None
// 1 Motion Blur Motion Vectors
// 2 Motion Vectors discard check
// 3 Motion Vectors length
#define TEST_MOTION_BLUR_TYPE (DEVELOPMENT ? 0 : 0)
// 0 None
// 1 Jitters
// 2 Depth Buffer
// 3 Reprojection Matrix
// 4 Motion Vectors (of dynamic geometry that moves in world space, not relatively to the camera)
// 5 Force blending with the previous frame to test temporal stability
#define TEST_TAA_TYPE (DEVELOPMENT ? 0 : 0)
// 0 None
// 1 Additive Bloom
// 2 Native Bloom
#define TEST_BLOOM_TYPE (DEVELOPMENT ? 0 : 0)
#define TEST_SUNSHAFTS (DEVELOPMENT && 0)
// 0 None
// 1 Show fixed color
// 2 Show only lens optics
#define TEST_LENS_OPTICS_TYPE (DEVELOPMENT ? 0 : 0)
#define TEST_DITHERING (DEVELOPMENT && 0)
#define TEST_SMAA_EDGES (DEVELOPMENT && 0)
#define TEST_DYNAMIC_RESOLUTION_SCALING (DEVELOPMENT && 0)
#define TEST_EXPOSURE (DEVELOPMENT && 0)
#define TEST_SSAO (DEVELOPMENT && 0)

/////////////////////////////////////////
// Prey LUMA user settings
/////////////////////////////////////////

// Registers 2, 4, 7, 8, 9, 10, 11 and 12 are 100% safe to be used for any post processing or late rendering passes.
// Register 2 is never used in the whole Prey code. Register 4, 7 and 8 are also seemengly never actively used by Prey.
// Register 3 seems to be used during post processing so it might not be safe.
// CryEngine pushes the registers that are used by each shader again for every draw, so it's generally safe to overridden them anyway (they are all reset between frames).
cbuffer LumaSettings : register(b2)
{
  struct
  {
    // 0 for SDR (80 nits) (gamma sRGB output)
    // 1 for HDR
    // 2 for SDR on HDR (203 nits) (gamma 2.2 output)
    uint DisplayMode;
    float PeakWhiteNits; // Access this through the global variables below
    float GamePaperWhiteNits; // Access this through the global variables below
    float UIPaperWhiteNits; // Access this through the global variables below
    uint DLSS; // Is DLSS enabled (implies it engaged and it's compatible) (this is on even in fullscreen UI menus that don't use upscaling)
    uint LensDistortion;
#if DEVELOPMENT
    // These are reflected in ImGui (the number is hardcoded in c++).
    // You can add up to 3 numbers as comment to their right to define the UI settings sliders default, min and max values, and their name.
    float DevSetting01; // 0, 0, 1
    float DevSetting02; // 0, 0, 1
    float DevSetting03; // 0, 0, 1
    float DevSetting04; // 0, 0, 1
    float DevSetting05; // 0, 0, 1
    float DevSetting06; // 0, 0, 1
    float DevSetting07; // 0, 0, 1
    float DevSetting08; // 0, 0, 1
    float DevSetting09; // 0, 0, 1
    float DevSetting10; // 0, 0, 1
#endif
  } LumaSettings : packoffset(c0);
}

// These parameters are already pushed directly from c++
bool ShouldForceWhiteLevel() { return LumaSettings.DisplayMode == 0; }
float GetForcedWhileLevel() { return (LumaSettings.DisplayMode == 0) ? sRGB_WhiteLevelNits : ITU_WhiteLevelNits; }

#ifdef HDR_TONEMAP_PAPER_WHITE_BRIGHTNESS
static const float GamePaperWhiteNits = HDR_TONEMAP_PAPER_WHITE_BRIGHTNESS;
static const float UIPaperWhiteNits = HDR_TONEMAP_PAPER_WHITE_BRIGHTNESS;
#elif DEVELOPMENT
static const float GamePaperWhiteNits = ShouldForceWhiteLevel() ? GetForcedWhileLevel() : (LumaSettings.GamePaperWhiteNits != 0 ? LumaSettings.GamePaperWhiteNits : ITU_WhiteLevelNits);
static const float UIPaperWhiteNits = ShouldForceWhiteLevel() ? GetForcedWhileLevel() : (LumaSettings.UIPaperWhiteNits != 0 ? LumaSettings.UIPaperWhiteNits : ITU_WhiteLevelNits);
#else // HDR_TONEMAP_PAPER_WHITE_BRIGHTNESS
static const float GamePaperWhiteNits = LumaSettings.GamePaperWhiteNits;
static const float UIPaperWhiteNits = LumaSettings.UIPaperWhiteNits;
#endif // HDR_TONEMAP_PAPER_WHITE_BRIGHTNESS
#ifdef HDR_TONEMAP_PEAK_BRIGHTNESS
static const float PeakWhiteNits = HDR_TONEMAP_PEAK_BRIGHTNESS;
#elif DEVELOPMENT
static const float PeakWhiteNits = ShouldForceWhiteLevel() ? GetForcedWhileLevel() : (LumaSettings.PeakWhiteNits != 0 ? LumaSettings.PeakWhiteNits : 1000.0); // Same peak white default as in c++
#else // HDR_TONEMAP_PEAK_BRIGHTNESS
static const float PeakWhiteNits = LumaSettings.PeakWhiteNits;
#endif // HDR_TONEMAP_PEAK_BRIGHTNESS