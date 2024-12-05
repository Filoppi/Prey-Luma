#include "PostAA_AA.hlsl"

#define _RT_SAMPLE2 0

#define DRAW_TAA_JITTER_TEST_BANDS (DEVELOPMENT && 1)

#if TEST_TAA_TYPE == 1
#include "include/CBuffer_PerFrame.hlsl"
#endif // TEST_TAA_TYPE == 1

// PostAA_PS
// Shader used by SMAA 2TX and TAA alone too (the namings can be confused, but this just does TAA).
// We don't force early out in case DLSS was enabled by the user, because if this pass was ever reached, it means that then DLSS had failed to render (and fell back on the vanilla TAA).
// Luma's changes here are mostly mirrored in SMAA 1TX and 2TX.
void main(
  float4 inWPos : SV_Position0,
  float2 inBaseTC : TEXCOORD0,
  // "1 / CV_HPosScale.xy"
  nointerpolation float2 inBaseTCScale : TEXCOORD1,
  out float4 outColor : SV_Target0)
{
	// Output motion vectors for DLSS (simplified version of the same code below) because DLSS is replacing the pass that would have rendered this pixel shader (and also replacing its render target).
	//TODO LUMA: fully replace the pass with a dedicated DLSS Motion Vectors generation shader
	if (LumaSettings.DLSS)
	{
		uint3 pixelCoord = int3(inWPos.xy, 0);
		const float depth = PostAA_DeviceDepthTex.Load(pixelCoord).r;
		const float2 currTC = inBaseTC.xy;
#if 0 // LUMA FT: this is possibly more accurate but we haven't tested it enough (the reprojection matrxi already includes jitters anyway)
		float2 jitters = LumaData.CameraJitters.xy * float2(0.5, -0.5);
		float2 prevTC = CalcPreviousTC(currTC + jitters, depth) - jitters;
#else
		float2 prevTC = CalcPreviousTC(currTC, depth);
#endif
		float2 velocity = prevTC - currTC;
		float2 vObj = PostAA_VelocityObjectsTex.Load(pixelCoord);
		if (vObj.x != 0 || vObj.y != 0)
		{
			velocity = ReadVelocityObjects(vObj);
			velocity /= LumaData.RenderResolutionScale;
		}
		outColor.xy = velocity;
		outColor.zw = 0;
		return;
	}

	if (ShouldSkipPostProcess(inWPos.xy, 1))
	{
		outColor	= SampleCurrentScene(inBaseTC.xy * CV_HPosScale.xy);
		return;
	}

	// Convert from NDC space to UV space (y is flipped)
	float2 jitters = LumaData.CameraJitters.xy * float2(0.5, -0.5);
	float2 prevJitters = LumaData.PreviousCameraJitters.xy * float2(0.5, -0.5);

#if 0 // Test different jitters scales
	static const float numberOfBars = 2.0;
	if (inBaseTC.x <= 1.0 / numberOfBars)
	{
		jitters = 0;
	}
	else if (inBaseTC.x <= 2.0 / numberOfBars)
	{
		jitters /= 2.0;
	}
	jitters.x = -jitters.x;

#if DRAW_TAA_JITTER_TEST_BANDS && !TEST_MOTION_BLUR_TYPE && !TEST_SMAA_EDGES && !TEST_TAA_TYPE
	// Draw black bars
	for (uint i = 1; i < (uint)numberOfBars; i++)
	{
		static const float barLength = 0.00025;
		float u = (float)i / numberOfBars;
		if (inBaseTC.x > u - barLength && inBaseTC.x < u + barLength)
		{
			outColor = 0;
			return;
		}
	}
#endif // DRAW_TAA_JITTER_TEST_BANDS && !TEST_MOTION_BLUR_TYPE && !TEST_SMAA_EDGES && !TEST_TAA_TYPE
#endif
	
	const float2 dejitteredCurrTC = inBaseTC.xy - jitters; // This offsets the UV in the opposite direction of the jitters, so to attempt to normalize them out (at the cost of some blur)
	const float2 jitteredCurrTC = inBaseTC.xy + jitters;

#if TEST_TAA_TYPE == 1 // LUMA FT: quick jitter test (this will show the original jitter value "m_vProjMatrixSubPixoffset" on the game code)
	outColor = float4(LumaData.CameraJitters.xy * 0.5 * cbPostAA.screenSize.xy / sRGB_WhiteLevelNits, CF_VolumetricFogDistributionParams.w / sRGB_WhiteLevelNits, 0);
	return;
#endif

#if !ENABLE_AA || !ENABLE_TAA
	outColor	= SampleCurrentScene(dejitteredCurrTC * CV_HPosScale.xy);
	return;
#endif // !ENABLE_AA || !ENABLE_TAA

	uint3 pixelCoord = int3(inWPos.xy, 0);
	
	// LUMA FT: the depth buffer is jittered
	const float depth = PostAA_DeviceDepthTex.Load(pixelCoord).r;

#if TEST_TAA_TYPE == 2 // LUMA FT: quick depth test
	outColor = sqrt_mirrored(depth); // Adjust depth for perception (theoretically it's still a "linear" color)
    outColor = SDRToHDR(outColor, false);
	return;
#endif

	const float2 currTC = inBaseTC.xy; // Non (de)jittered, used for reprojection with the previous frame, given that theoretically it should have the jitters normalized out of it
	// LUMA FT: "CalcPreviousTC()" internally does not acknowledge the camera jitters difference between the current and previous frame, jitters were not included in any of the two matrices used to calculate "cbPostAA.matReprojection"
#if 1 // LUMA FT: this is probably more accurate
	float2 prevTC = CalcPreviousTC(jitteredCurrTC, depth) - jitters;
#else
	float2 prevTC = CalcPreviousTC(currTC, depth);
#endif

	// currTC and prevTC are in clip space, find their diff (velocity over this frame)
	float2 velocity = prevTC - currTC;
	
#if TEST_TAA_TYPE == 3  // LUMA FT: quick reprojection matrix (through depth buffer) test
	velocity = pow(abs(velocity) * 25000.0, 2.0) * sign(velocity);
	bool horOrVert = true;
	float velocityAxis = horOrVert ? velocity.x : velocity.y;
	outColor.x = abs(velocityAxis);
	outColor.y = velocityAxis >= 0.0 ? abs(velocityAxis) : 0.0;
	outColor.z = velocityAxis < 0.0 ? abs(velocityAxis) : 0.0;
	outColor.a = 0;
    outColor = SDRToHDR(outColor, false);
	return;
#endif

	// LUMA FT: this velocity is only set on objects that move on their own in world space, not "relative" to the camera.
	// There's no camera space motion vectors here, and thus this does not include the camera jitters offsets from the previous frame,
	// but the current result is calculated based on the current's frame jitters, which are also reprojected on the previous frame camera.
	// Camera motion vectors (with only camera movement) can be found in the blur pass.
	// With Luma though, we have fixed these motion vectors to always reliably include the previous and current jitters (depending on "FORCE_MOTION_VECTORS_JITTERED").
	// LUMA FT: Motion vectors in "uv space", once multiplied by the rendering resolution, the value here represents the horizontal and vertical pixel offset,
	// a value of 0.3 -2 means that we need to move 0.3 pixels on the x and -2 pixels on the y to find where this texel remapped on the previous frame buffers.
	float2 vObj = PostAA_VelocityObjectsTex.Load(pixelCoord);
	// LUMA FT: zero here means there was no velocity (because "VELOCITY_OBJECTS_FLOAT" is true), so we fall back on the camera movement matrix reprojection.
	// LUMA FT: fixed check not acknowledging the y axis.
	if (vObj.x != 0 || vObj.y != 0)
	{
		velocity = ReadVelocityObjects(vObj); // clip space // LUMA FT: this actually doesn't do anything as "VELOCITY_OBJECTS_FLOAT" is true
		// Note: for some reason this is required on dynamic objects MVs when we have a resolution scale != 1.
		// Not just the jitter offsets baked in them are scaled, but their whole movement.
		// We tried scaling the depth generated MVs by the opposite factor instead and it did not work.
		velocity /= LumaData.RenderResolutionScale;
	}

// For some reason both the camera depth/projection generation motion vectors and the dynamic objects motion vectors need to be dejittered the same way here, either way.
// Turning this on makes the TAA/MVs slightly more accurate and stable.
// Note: see MotionBlur PackVelocities for more info on Jitters and Motion Vectors etc
#if FORCE_MOTION_VECTORS_JITTERED
	velocity -= jitters;
	velocity += prevJitters;
#endif

#if TEST_TAA_TYPE == 4 // LUMA FT: quick combined motion vectors test
	// Turning the camera towards the right should show red, while turning it left should show blue. Going down should also be green and going up blue.
	// Theoretically the colors specified should be inverted, but testing showed that this is the way.
	velocity *= CV_ScreenSize.xy * (2.5 / sRGB_WhiteLevelNits); // Normalize velocity to output x nits for a value of 1
	outColor.rg = max(velocity, 0);
	outColor.b = -min(velocity.x, 0) + -min(velocity.y, 0);
	outColor.w = 0;
	outColor = SDRToHDR(outColor, false);
	return;
#endif

	float2 sampleCurrentTC = currTC;
#if 0 // LUMA FT: use the dejittered UVs here as they avoid shimmering and favour blur (this is currently generally looking worse and probably makes no sense)
	sampleCurrentTC = dejitteredCurrTC;
#endif
#if 0 // LUMA FT: optionally dejitter the previous frame UV, though in reality this is "wrong" because the previous frame is already the blended history of the TAA output and not the last raw pre-TAA jittered frame, so it's already somehow "dejittered" (we could dejitter it by 1/2 or 1/4 in case though)
	velocity -= prevJitters;
#endif
	const float2 tc  = sampleCurrentTC * CV_HPosScale.xy; // MapViewportToRaster()
	const float2 tcp = (sampleCurrentTC + velocity) * CV_HPosScale.zw;

#if !_RT_SAMPLE2
	// New SMAA 2TX Mode

	// Curr frame and neighbor texels
	float3 cM	= DecodeBackBufferToLinearSDRRange(SampleCurrentScene(tc).rgb);

#if TEST_TAA_TYPE == 5 // LUMA FT: quick temporal blending test (this should quickly reach temporal stability)
	float3 cPM = DecodeBackBufferToLinearSDRRange(SamplePreviousScene(tcp).rgb);
	outColor.rgb = float4(EncodeBackBufferFromLinearSDRRange( lerp(cPM, cM, 0.5/8.0) ), 0.0);
	return;
#endif

	float3 cTL = DecodeBackBufferToLinearSDRRange(SampleCurrentScene(tc, float2(-1.0f, -1.0f)).rgb);
	float3 cTR = DecodeBackBufferToLinearSDRRange(SampleCurrentScene(tc, float2( 1.0f, -1.0f)).rgb);
	float3 cBL = DecodeBackBufferToLinearSDRRange(SampleCurrentScene(tc, float2(-1.0f,  1.0f)).rgb);
	float3 cBR = DecodeBackBufferToLinearSDRRange(SampleCurrentScene(tc, float2( 1.0f,  1.0f)).rgb);

	float3 cMin = min3(min3(cTL, cTR, cBL), cBR, cM);
	float3 cMax = max3(max3(cTL, cTR, cBL), cBR, cM);

	float3 cHistory = DecodeBackBufferToLinearSDRRange(SamplePreviousScene(tcp).rgb);

	bool offscreen = max(abs((tcp.x) * 2 - 1), abs((tcp.y) * 2 - 1)) >= 1.0;
	float clipLength = 1;

	// LUMA FT: it would be better to convert all these calculations to linear space, but it would change how the TAA looks and we'd need to find new values for the parameters
	if (!offscreen)
	{
		clipLength = ClipHistory(cHistory, cM, cMin, cMax);

		// Try to identify subpixel changes
		float3 prevTL = DecodeBackBufferToLinearSDRRange(SamplePreviousScene(tcp, float2(-1.0f, -1.0f)).rgb);
		float3 prevTR = DecodeBackBufferToLinearSDRRange(SamplePreviousScene(tcp, float2( 1.0f, -1.0f)).rgb);
		float3 prevBL = DecodeBackBufferToLinearSDRRange(SamplePreviousScene(tcp, float2(-1.0f,  1.0f)).rgb);
		float3 prevBR = DecodeBackBufferToLinearSDRRange(SamplePreviousScene(tcp, float2( 1.0f,  1.0f)).rgb);

		float neighborDiff = length(clamp(prevTL, cMin, cMax) - prevTL) + length(clamp(prevTR, cMin, cMax) - prevTR) +
												 length(clamp(prevBL, cMin, cMax) - prevBL) + length(clamp(prevBR, cMin, cMax) - prevBR);

		if (neighborDiff < 0.02) clipLength = 0; // LUMA FT: Crytek/Arkane hardcoded magic number here
	}

	float blendAmount = saturate( length(cHistory - cM) * 10 ); // LUMA FT: Crytek/Arkane hardcoded magic number here

	// Apply color clipping
	cHistory = lerp(cHistory, cM, clipLength);

#if 0
	// Exponential moving average of current frame and history
	const float MaxFramesL = 2.5, MaxFramesH = 5;  // If too high, 8 bit sRGB precision not enough to converge
#else // LUMA FT: added using variables from code/config like SMAA 1TX (they are defaulted to 2 and 6) (this looks indistinguishable from the default branch)
	float MaxFramesL = cbPostAA.params.z;		// Frames to keep in history (low freq). Higher = less aliasing, but blurier result. Lower = sharper result, but more aliasing.
	float MaxFramesH = cbPostAA.params.w;		// Frames to keep in history (high freq). Higher = less aliasing, but blurier result. Lower = sharper result, but more aliasing.
#endif

	outColor.rgb = EncodeBackBufferFromLinearSDRRange( lerp( cHistory, cM, saturate(rcp(lerp(MaxFramesL, MaxFramesH, blendAmount))) ) ); // LUMA FT: added in/out linearization
	outColor.a = 0; // LUMA FT: if we ever wanted to improve this TAA for "free" we could store extra temporal information in the alpha, like the previous frame's luminance, or the difference from the previous frame, or encode 3 colors in it.

	// LUMA FT: add NaN check here to avoid them spreading over the history.
	// It would be better to check each channel individually, and maybe fallback to +/- FLT_MAX on the INF case, but it doesn't really matter as this should never happen.
	// We do not need this in the SMAA 1TX code.
	if (any(isnan(outColor.rgb)) || any(isinf(outColor.rgb)))
	{
		outColor.rgb = 0;
#if TEST_TONEMAP_OUTPUT
		outColor.rgb = float3(1.0, 0.0, 1.0);
#endif // TEST_TONEMAP_OUTPUT
	}
#endif // !_RT_SAMPLE2
}