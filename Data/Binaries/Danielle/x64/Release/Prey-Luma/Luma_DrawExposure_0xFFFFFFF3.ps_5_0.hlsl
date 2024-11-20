#include "include/Common.hlsl"
#include "include/Tonemap.hlsl"

cbuffer PER_BATCH : register(b0)
{
  float4 Param1 : packoffset(c1); // Could be "SunShafts_SunCol" or "HDREyeAdaptation"
  float4 Param2 : packoffset(c2); // Could be "HDREyeAdaptation" or "HDRFilmCurve" (the latter one is unused so it's garbage)
  float4 Param3 : packoffset(c3); // Could be "HDRFilmCurve" or "HDRBloomColor"
}

Texture2D<float2> adaptedLumTex : register(t1);

// Custom Luma shader to draw the final exposure value from textures+cbuffers (copied from HDRPostProcesss HDRFinalScene shader)
float main() : SV_Target0
{
#if 0 // Doesn't work as "HDRFilmCurve" seems to contain gargbage (it's never used/read so it's probably not set)
	// No need to check for these too: "Param3.x == HableShoulderScale && Param3.y == HableLinearScale && Param3.z == HableToeScale",
	// as in all other parameters, the w element is 1 (or could be zero in case).
	bool isParam3HDRFilmCurve = Param3.w == HableWhitepoint;
#elif 0 // There's no guarantee this will work, we proved the sun scale being 0.5 in some scenes but we don't know if it's ever set to 1
	bool isParam1SunShaftsSunCol = Param1.w != 1.f;
#else
	// This would be the most common case anyway
	bool isParam1HDREyeAdaptation = Param1.x == 0.18f && Param1.w == 1.f;
#endif

	// The HDR tonemap shader (HDRPostProcess HDRFinalScene) has branches for sun shafts, which shift the "HDREyeAdaptation" cbuffer value,
	// instead of passing in a new bool cbuffer from the CPU, we simply detect which one it is in the shader with "heuristics" (they are very safe).
	float4 HDREyeAdaptation = isParam1HDREyeAdaptation ? Param1 : Param2;
	
	float vAdaptedLum = adaptedLumTex.Load(0).x; // This is a 1x1 texture, so any UV will return the same value

    // Legacy exposure mode (always used in Prey) (HDREyeAdaptation.x isn't used and is always 0.18)
	const float fSceneKey = 1.03 - 2.0 / (2.0 + log2(vAdaptedLum.x + 1.0));
	float fExposure = fSceneKey / vAdaptedLum.x;
#if ENABLE_EXPOSURE_CLAMPING
	fExposure = clamp(fExposure, HDREyeAdaptation.y /*MinExposure*/, HDREyeAdaptation.z /*MaxExposure*/);
#endif
#if DLSS_RELATIVE_PRE_EXPOSURE >= 1
	float fExposureMidPoint = lerp(HDREyeAdaptation.y, HDREyeAdaptation.z, 0.5);
	// What's important for DLSS is the relative exposure value (how much it changes from the "baseline", most commonly used value in a level, or the average anyway),
	// so we divide it by the mid point, hoping it'd match that (it probably doesn't, but it's a good start).
	return fExposure / fExposureMidPoint;
#else
	return fExposure;
#endif
}