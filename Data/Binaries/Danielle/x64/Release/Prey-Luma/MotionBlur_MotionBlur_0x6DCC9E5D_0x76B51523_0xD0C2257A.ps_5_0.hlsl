#include "include/Common.hlsl"

// Qualities
#if _76B51523
#define _RT_SAMPLE1 1
#elif _D0C2257A
#define _RT_SAMPLE2 1
#endif

cbuffer PER_BATCH : register(b0)
{
  float4 vMotionBlurParams : packoffset(c0); // xy are the inverse size of "_tex2", zw are unused and zero
}

#include "include/CBuffer_PerViewGlobal.hlsl"

Texture2D<float4> _tex0 : register(t0); // Color texture, R11G11B10F without Luma
Texture2D<float4> _tex1 : register(t1); // (R8G8B8A8UNORM) Motion Blur Motion Vectors X and Y offsets (produced by "PackVelocitiesPS()"), this is fullscreen and the inner size depends on DRS
Texture2D<float4> _tex2 : register(t2); // (R8G8B8A8UNORM) Motion Blur Motion Vectors Length and Depth, this is a downscaled 20x20 version of the texture above (first compressed horizontally, then vertically, then blurred (the DRS area is scaled to fullscreen)), unfortunately the size doesn't depend on aspect ratio.
SamplerState _tex0_s : register(s0); // Bilinear
SamplerState _tex1_s : register(s1); // Point
SamplerState _tex2_s : register(s2); // Point

#include "include/MotionBlur.hlsl"

// MotionBlurPS
// This shader is run with pre-multiplied alpha blend, so if it alpha zero, it's purely additive, while if it returns alpha 1, it's purely override.
// Somehow, this shader is used to emulate depth of field when taking the shape of objects through character powers (the "PackVelocities" pixel shader returns almost all white, through "vRadBlurParam" and "vDirectionalBlur").
float4 main(float4 WPos : SV_Position0, float4 inBaseTC : TEXCOORD0) : SV_Target0
{
#if !ENABLE_MOTION_BLUR
	return 0;
#endif

	// Samples num needs to be divisible by 2
#if MOTION_BLUR_QUALITY >= 1 // LUMA FT
	const int numSamples = 36; // Ultra spec
#else
#if _RT_SAMPLE2
	const int numSamples = 24; // High spec
#elif _RT_SAMPLE1
	const int numSamples = 14; // Medium spec
#else
	const int numSamples = 6;  // Low spec
#endif
#endif
 
	const float weightStep = 1.0 / ((float)numSamples);

	float2 baseTC = MapViewportToRaster(inBaseTC.xy);

	const int2 pixQuadIdx = fmod(WPos.xy, 2);
	float samplingDither = (-0.25 + 2.0 * 0.25 * pixQuadIdx.x) * (-1.0 + 2.0 * pixQuadIdx.y);
	
	// Randomize lookup into max velocity to reduce visibility of tiles with opposing directions
	float2 tileBorderDist = abs(frac(WPos.xy * vMotionBlurParams.xy) - 0.5) * 2;
	tileBorderDist *= (samplingDither < 0) ? float2(1, 0) : float2(0, 1);  // Don't randomize in diagonal direction
	float rndValue = NRand3(inBaseTC.xy).x - 0.5;
	float2 tileOffset = tileBorderDist * rndValue;
	
	// LUMA FT: moved the dejitter logic to "PackVelocitiesPS()", which should be more optimized and also have better results
 	float2 jitters = 0;
	
	// LUMA FT: Motion vectors in "uv space". Once multiplied by the rendering resolution, the value here represents the horizontal and vertical pixel offset (encoded to have more precision around smaller values) (so not the max velocity as the name would imply),
	// a value of 0.3 -2 means that we need to move 0.3 pixels on the x and -2 pixels on the y to find where this texel remapped on the previous frame buffers.
	float3 maxVel = _tex2.SampleLevel(_tex2_s, inBaseTC.xy + tileOffset * vMotionBlurParams.xy, 0).xyz;
	maxVel.xy = DecodeMotionVector(maxVel.xy, false, jitters);
	const float2 blurStep = maxVel.xy * weightStep;

#if TEST_MOTION_BLUR_TYPE == 1 // LUMA FT: test motion vectors
#if 0 // Sample the UV offsetted by the motion vectors
	// Note: we might need to acknwoledge "CV_ScreenSize.zw" too to do this properly
	if (baseTC.x < 0.333 || 1)
		return float4(_tex0.SampleLevel(_tex0_s, baseTC + maxVel.xy, 0).rgb, 1.0f);
	else if (baseTC.x > 0.667)
		return float4(_tex0.SampleLevel(_tex0_s, baseTC - maxVel.xy, 0).rgb, 1.0f);
	else
		return float4(_tex0.SampleLevel(_tex0_s, baseTC, 0).rgb, 1.0f);
#endif
	return float4(maxVel.x * 2.5, maxVel.y * 2.5, 0, 1); // "maxVel.z" seems to be the length
#endif // TEST_MOTION_BLUR_TYPE == 1

// LUMA FT: re-enabled this to avoid R11G11B10F motion blur buffers from writing back on the R16G16B16A16F main color buffer on pixels that have no motion blur and thus lowering its quality.
// Note that LUMA actually upgrades motion blur buffers to FP16 too so it doesn't matter as much.
#if 1
	// Crytek/Arkane: Early out when no motion in the blurred MVs texture (disabled to have more predictable costs)
	// LUMA FT: anything higher than this threshold will cause motion blur to blur stuff that isn't actually moving, because they forgot to take out the jitters from the MB calculations (there might also be some some floating point math errors).
	// This is already adjusted by delta time.
	if (length(maxVel.xy) < 0.001f)
	{
#if 1 // LUMA FT: modified to actually simply return zero, so it's faster to execute and ends up not influencing the back buffer (it might change its alpha, but that is ignored). This uses alpha blending (we could discard too but it's probably worse).
		return 0;
#else
		const float4 sampleCenter = _tex0.SampleLevel(_tex0_s, baseTC, 0);
		return float4(sampleCenter.rgb, 1.0f);
#endif
	}
#if TEST_MOTION_BLUR_TYPE == 2 // This only works if the early out branch is enabled, so put it inside of it
	return float4(1.0, 0, 0, 1);
#endif // TEST_MOTION_BLUR_TYPE == 2
#endif

	// LUMA FT: note that these also include jitters (in both the length/x component and depth/y component), so they account for sub-pixel movement, which generally isn't really wanted in MB,
 	const float2 centerLenDepth = UnpackLengthAndDepth(_tex1.SampleLevel(_tex1_s, baseTC, 0).zw, length(jitters)); // velocity (length) and depth
	
#if TEST_MOTION_BLUR_TYPE == 3
	return float4(centerLenDepth.x, 0, 0, 1);
#endif // TEST_MOTION_BLUR_TYPE == 3

	float4 acc = float4(0, 0, 0, 0);
	
	[unroll]
	for (int s = 0; s < numSamples/2; ++s)
	{
		const float curStep = (s + samplingDither);
		// LUMA FT: fixed additive tex coordinates not being scaled by DRS factor
		const float2 tc0 = ClampScreenTC(baseTC + blurStep * curStep * CV_HPosScale.xy);
		const float2 tc1 = ClampScreenTC(baseTC - blurStep * curStep * CV_HPosScale.xy);
	
		float2 lenDepth0 = UnpackLengthAndDepth(_tex1.SampleLevel(_tex1_s, tc0.xy, 0).zw, length(jitters));
		float2 lenDepth1 = UnpackLengthAndDepth(_tex1.SampleLevel(_tex1_s, tc1.xy, 0).zw, length(jitters));

		float weight0 = MBSampleWeight(centerLenDepth.y, lenDepth0.y, centerLenDepth.x, lenDepth0.x, s, 1.0 / length(blurStep));
		float weight1 = MBSampleWeight(centerLenDepth.y, lenDepth1.y, centerLenDepth.x, lenDepth1.x, s, 1.0 / length(blurStep));
		
		const bool2 mirrorWeight = bool2(lenDepth0.y > lenDepth1.y, lenDepth1.x > lenDepth0.x);
 		weight0 = all(mirrorWeight) ? weight1 : weight0;
 		weight1 = any(mirrorWeight) ? weight1 : weight0;

		acc += float4(_tex0.SampleLevel(_tex0_s, tc0.xy, 0).rgb, 1.0f) * weight0;
		acc += float4(_tex0.SampleLevel(_tex0_s, tc1.xy, 0).rgb, 1.0f) * weight1;
	}
	acc.rgba *= weightStep;

	return acc;
}