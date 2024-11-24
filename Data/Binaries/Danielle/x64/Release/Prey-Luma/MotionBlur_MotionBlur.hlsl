#include "include/Common.hlsl"

cbuffer PER_BATCH : register(b0)
{
  float4 vMotionBlurParams : packoffset(c0);
}

#include "include/CBuffer_PerViewGlobal.hlsl"

Texture2D<float4> _tex0 : register(t0); // Color texture
Texture2D<float4> _tex1 : register(t1); // Motion Blur Motion Vectors X and Y offsets
Texture2D<float4> _tex2 : register(t2); // Motion Blur Motion Vectors Length and Depth (actually the same texture as above, probably just a different view, both produced by "PackVelocitiesPS()")
SamplerState _tex0_s : register(s0);
SamplerState _tex1_s : register(s1);
SamplerState _tex2_s : register(s2);

#include "include/MotionBlur.hlsl"

// This shader is run with pre-multiplied alpha blend, so if it alpha zero, it's purely additive, while if it returns alpha 1, it's purely override.
float4 MotionBlurPS(float4 WPos, float4 inBaseTC)
{
#if !ENABLE_MOTION_BLUR
	return 0;
#endif

#if _RT_SAMPLE2
	const int numSamples = 24; // High spec
#elif _RT_SAMPLE1
	const int numSamples = 14; // Medium spec
#else
	const int numSamples = 6;  // Low spec
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
	
#if 1 // LUMA FT: moved this logic to "PackVelocitiesPS()", which should be more optimized and also have better results
 	float2 jitters = 0;
#else
	row_major float4x4 projectionMatrix = mul( CV_ViewProjMatr, CV_InvViewMatr ); // The current projection matrix used to be stored in "CV_PrevViewProjMatr" in vanilla Prey
 	float2 jitters = float2(projectionMatrix[0][2], projectionMatrix[1][2]);
#endif
	
	// LUMA FT: Motion vectors in "uv space". Once multiplied by the rendering resolution, the value here represents the horizontal and vertical pixel offset (encoded to have more precision around smaller values) (so not the max velocity as the name would imply),
	// a value of 0.3 -2 means that we need to move 0.3 pixels on the x and -2 pixels on the y to find where this texel remapped on the previous frame buffers.
	float3 maxVel = _tex2.SampleLevel(_tex2_s, inBaseTC.xy + tileOffset * vMotionBlurParams.xy, 0).rgb;
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
#endif

// LUMA FT: re-enabled this to avoid R11G11B10F motion blur buffers from writing back on the R16G16B16A16F main color buffer on pixels that have no motion blur and thus lowering its quality.
// Note that LUMA actually upgrades motion blur buffers to FP16 too so it doesn't matter as much.
#if 1
	// Crytek/Arkane: Early out when no motion (disabled to have more predictable costs)
	// LUMA FT: anything higher than this threshold will cause motion blur to blur stuff that isn't actually moving, because they forgot to take out the jitters from the MB calculations (there might also be some some floating point math errors)
	if (length(maxVel.xy) < 0.001f) //TODOFT: normalize this with delta time? It doesn't seem to be needed, it's somehow independent from FPS.
	{
#if 1 // LUMA FT: modified to actually simply return zero, so it's faster to execute and ends up not influencing the back buffer (it might change its alpha, but that is ignored)
		return 0;
#else
		const float4 sampleCenter = _tex0.SampleLevel(_tex0_s, baseTC, 0);
		return float4(sampleCenter.rgb, 1.0f);
#endif
	}
#if TEST_MOTION_BLUR_TYPE == 2 // This only works if the early out branch is enabled, so put it inside of it
	return float4(1.0, 0, 0, 1);
#endif
#endif

	// LUMA FT: note that these also include jitters (in both the length/x component and depth/y component), so they account for sub-pixel movement, which generally isn't really wanted in MB,
 	const float2 centerLenDepth = UnpackLengthAndDepth(_tex1.SampleLevel(_tex1_s, baseTC, 0).zw, length(jitters)); // velocity (length) and depth
	
#if TEST_MOTION_BLUR_TYPE == 3
	return float4(centerLenDepth.x, 0, 0, 1);
#endif

	float4 acc = float4(0, 0, 0, 0);
	
	[unroll]
	for (int s = 0; s < numSamples/2; ++s)
	{
		const float curStep = (s + samplingDither);
		const float2 tc0 = ClampScreenTC(baseTC + blurStep * curStep);
		const float2 tc1 = ClampScreenTC(baseTC - blurStep * curStep);
	
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