
#include "include/external/XeGTAO.hlsl"
#include "include/Common.hlsl"

#include "include/CBuffer_PerViewGlobal.hlsl"

cbuffer CBSSDO : register(b0)
{
  struct
  {
    float4 viewSpaceParams; // 2 * hor scale, 2 * ver scale, -1 / hor scale, -1 / ver scale
    float4 ssdoParams; // hor radius, ver radius, min radius, max radius
  } cbSSDO : packoffset(c0);
}

#if ENABLE_SSAO_TEMPORAL
// LUMA FT: added these as they are needed to get the frame index and game time and frame delta time
#include "include/CBuffer_PerFrame.hlsl"
#endif // ENABLE_SSAO_TEMPORAL

SamplerState ssSSDODepth : register(s0);
Texture2D<float4> _tex0_D3D11 : register(t0);
Texture2D<float4> _tex1_D3D11 : register(t1); // The linear depth (previously converted from the device g-buffer depth) (0 near 1 far). R32F
Texture2D<float4> _tex2_D3D11 : register(t2); // The "lower quality" half resolution linear depth (previously converted from the device g-buffer depth). RGBA16F (all channels are seemengly the same)

float2 MapViewportToRaster(float2 normalizedViewportPos, bool bOtherEye = false)
{
	return normalizedViewportPos * CV_HPosScale.xy;
}

float3 DecodeGBufferNormal( float4 bufferA )
{
	return normalize( bufferA.xyz * 2.0 - 1.0 );
}

float GetLinearDepth(float fDevDepth, bool bScaled = false)
{
	return fDevDepth * (bScaled ? CV_NearFarClipDist.y : 1.0f);
}

float GetLinearDepth(Texture2D depthTexture, int3 vPixCoord, bool bScaled = false)
{
	float fDepth = depthTexture.Load(vPixCoord).x;
	return GetLinearDepth(fDepth, bScaled);
}

// LUMA FT: added this to make the code easier to read. It can't be directly changed without adapting some code,
// but it shouldn't need to be changed, as this is simply a shader optimization to run 4 different samples at once on a float4,
// instead of doing 4 times a float1.
static const int samplesGroupNum = 4;

float4 SSDOFetchDepths(Texture2D<float4> _texture, float4 tc[samplesGroupNum/2], uint component)
{
	return float4( _texture.SampleLevel(ssSSDODepth, tc[0].xy, 0)[component],
	              _texture.SampleLevel(ssSSDODepth, tc[0].zw, 0)[component],
	              _texture.SampleLevel(ssSSDODepth, tc[1].xy, 0)[component],
	              _texture.SampleLevel(ssSSDODepth, tc[1].zw, 0)[component] );
}

float4 GTAO(float4 WPos, float4 inBaseTC)
{
	//TODOFT1: best define all params and temporal/denoising, force asd.
	static const uint frameCounter = 0;
	static const float2 localNoise = float2(0.5, 0.5);
	//static const float2 localNoise = 0.0;
#if SSAO_QUALITY <= -1
	static const float sliceCount = 1;
	static const float stepsPerSlice = 2;
#elif SSAO_QUALITY == 0
	static const float sliceCount = 2;
	static const float stepsPerSlice = 2;
#elif SSAO_QUALITY == 1
	static const float sliceCount = 3;
	static const float stepsPerSlice = 3;
#elif SSAO_QUALITY >= 2
	static const float sliceCount = 9;
	static const float stepsPerSlice = 3;
#endif
	static const uint denoisePasses = 0;
	
	GTAOConstants consts;
	
	row_major float4x4 projectionMatrix = mul( CV_ViewProjMatr, CV_InvViewMatr ); // The current projection matrix used to be stored in "CV_PrevViewProjMatr" in vanilla Prey

#if 0 // Identical but slower option (it requires "projectionMatrix" to be calculated) //TODOFT: actually, this looks less broken?
	bool rowMajor = true; //TODOFT
	float depthLinearizeMul = rowMajor ? (-projectionMatrix[2][3]) : (-projectionMatrix[3][2]);     // float depthLinearizeMul = ( clipFar * clipNear ) / ( clipFar - clipNear );
	float depthLinearizeAdd = projectionMatrix[2][2];     // float depthLinearizeAdd = clipFar / ( clipFar - clipNear );
    consts.DepthUnpackConsts = float2(depthLinearizeMul, depthLinearizeAdd);
#elif 0
    float depthLinearizeMul = ( CV_NearFarClipDist.y * CV_NearFarClipDist.x ) / ( CV_NearFarClipDist.y - CV_NearFarClipDist.x );
    float depthLinearizeAdd = CV_NearFarClipDist.y / ( CV_NearFarClipDist.y - CV_NearFarClipDist.x );
    consts.DepthUnpackConsts = float2(depthLinearizeMul, depthLinearizeAdd);
#else // Do this given it's unused atm (due to flags)
	consts.DepthUnpackConsts = float2(1, 0); // Multiplier (scale, x/depth) and addend (offset, depth+y)
#endif
    //consts.DepthUnpackConsts = CV_NearFarClipDist.xy; //TODOFT

#if 0 //TODOFT: DRS
	consts.ViewportSize = round(CV_ScreenSize.xy / CV_HPosScale.xy); // Round to make sure its an integer (this is probably unnecessary but we do it for extra safety)
	consts.ViewportPixelSize = CV_ScreenSize.zw * 2.0; // These already have "CV_HPosScale.xy" baked in
#else // This already supports DRS (I think)
	consts.ViewportSize = round(CV_ScreenSize.xy);
	consts.ViewportPixelSize = 1.0 / CV_ScreenSize.xy;
#endif
	consts.DenoiseBlurBeta = (denoisePasses==0) ? 1e4f : 1.2f; 
	consts.NoiseIndex = (denoisePasses>0) ? (frameCounter % 64) : 0;
	consts.FinalValuePower = XE_GTAO_DEFAULT_FINAL_VALUE_POWER;
	consts.DepthMIPSamplingOffset = XE_GTAO_DEFAULT_DEPTH_MIP_SAMPLING_OFFSET;
	consts.ThinOccluderCompensation = XE_GTAO_DEFAULT_THIN_OCCLUDER_COMPENSATION;
	consts.SampleDistributionPower = XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER;
	consts.EffectFalloffRange = XE_GTAO_DEFAULT_FALLOFF_RANGE;
	consts.RadiusMultiplier = XE_GTAO_DEFAULT_RADIUS_MULTIPLIER;
#if 1
	consts.EffectRadius = 0.5f; // Default copied from GTAO code
#else
	// Retrieve back the original radius ("r_ssdoRadius" cvar, defaulted to ~1.2).
	// Note that SSDO also multiplied the radius by 0.15 for some bands.
	float2 radius = (cbSSDO.ssdoParams.xy / float2(projectionMatrix[0][0], projectionMatrix[1][1])) * 2.0 * CV_NearFarClipDist.y;
	consts.EffectRadius = lerp(radius.x, radius.y, 0.5); // Take the average of x and y given that GTAO doesn't differentiate on them (if our calculations were correct, both x and y radiuses would have been identical anyway).
#endif
	consts.Padding0 = 0.0;
	
#if 1 // Identical but faster option (assuming we already used "projectionMatrix" for anything else)
	float tanHalfFOVY = 1.f / projectionMatrix[1][1];
	float tanHalfFOVX = 1.f / projectionMatrix[0][0];
	if (LumaSettings.DevSetting07 <= 0.25 || LumaSettings.DevSetting07 >= 0.75)
	{
		 tanHalfFOVY = 0.5f / projectionMatrix[1][1];
		 tanHalfFOVX = 0.5f / projectionMatrix[0][0];
	}
#else
	float FOVX = 1.f / CV_ProjRatio.z;
	float inverseAspectRatio = (float)CV_ScreenSize.z / (float)CV_ScreenSize.w; // Theoretically the projection matrix aspect ratio always matches the screen aspect ratio //TODOFT: is this the right formula or does it bake in "CV_HPosScale.xy"?
    float tanHalfFOVX = tan( FOVX * 0.5f );
    float tanHalfFOVY = tanHalfFOVX * inverseAspectRatio;
#endif
    consts.CameraTanHalfFOV             = float2( tanHalfFOVX, tanHalfFOVY );

#if 0 // LUMA FT: random test... (looks worse)
    consts.NDCToViewMul                 = float2( consts.CameraTanHalfFOV.x * 2.0f, consts.CameraTanHalfFOV.y * 2.0f );
    consts.NDCToViewAdd                 = float2( consts.CameraTanHalfFOV.x * -1.0f, consts.CameraTanHalfFOV.y * -1.0f );
#else
    consts.NDCToViewMul                 = float2( consts.CameraTanHalfFOV.x * 2.0f, consts.CameraTanHalfFOV.y * -2.0f );
    consts.NDCToViewAdd                 = float2( consts.CameraTanHalfFOV.x * -1.0f, consts.CameraTanHalfFOV.y * 1.0f );
#endif
    consts.NDCToViewMul_x_PixelSize     = consts.NDCToViewMul * consts.ViewportPixelSize;

	Texture2D<float4> depthTexture;
#if _RT_SAMPLE0 //TODOFT: add support for "_tex2_D3D11" half res AO (all is run at half res) (the full res is always bound anyway)
	depthTexture = _tex2_D3D11;
#else // !_RT_SAMPLE0
	depthTexture = _tex1_D3D11;
#endif // _RT_SAMPLE0

	//static const float3 normalsConversion = float3(1, -1, -1); //TODOFT: flip x too?
	/*static*/ const float3 normalsConversion = float3(LumaSettings.DevSetting01 * 2 - 1, -(LumaSettings.DevSetting02 * 2 - 1), -(LumaSettings.DevSetting03 * 2 - 1));
	float3 normal = DecodeGBufferNormal( _tex0_D3D11.Load(float3(WPos.xy, 0)) );
	float3 normalViewSpace = normal; // View Space normals
	if (LumaSettings.DevSetting07 >= 0.5)
	{
		normalViewSpace = normalize( mul( CV_ViewMatr, float4(normal, 0) ).xyz ) * normalsConversion; // View Space normals
	}

	// Our linearized (non device) depth buffer is NOT inverted, so "XE_GTAO_DEPTH_TEXTURE_INVERTED" is not needed, but it's normalized in near/far space, so "XE_GTAO_DEPTH_TEXTURE_LINEAR" is needed (actually we don't).
	float4 outColor = XeGTAO_MainPass(WPos.xy, sliceCount, stepsPerSlice, localNoise, normalViewSpace, consts, depthTexture, ssSSDODepth);
	
#if 1 //TODOFT: move this?
	if (LumaSettings.DevSetting04 >= 0.5) // Seems good!
	{
		if (LumaSettings.DevSetting05 >= 2.0 / 3.0) // Bad branch??? Seems like the best!!!
		{
			outColor.xyz = (outColor.xyz - 0.5) * 2.0f; // From 0|1 range to -1|+1
		}
		else if (LumaSettings.DevSetting05 >= 1.0 / 3.0) // Best branch??? Actually, no conversion is needed at all!!!
		{
			outColor.xyz = outColor.xyz * 0.5 + 0.5; // From -1|+1 range to 0|1
		}
		outColor.xyz = mul( CV_InvViewMatr, float4(outColor.xyz * normalsConversion, 0) ).xyz;
		if (LumaSettings.DevSetting05 >= 2.0 / 3.0)
		{
			outColor.xyz = outColor.xyz * 0.5 + 0.5; // From -1|+1 range to 0|1
		}
	}
#endif
	//TODOFT: quick test
  	//outColor.a = LumaSettings.DevSetting06;
  	outColor.a *= LumaSettings.DevSetting06 * 2;
	
#if !ENABLE_SSAO
  	outColor.a = 0;
#endif // !ENABLE_SSAO
	return outColor;
}

// This draws bent normals ("rgb") and the "ambient occlusion" on "a"
float4 DirOccPassPS(float4 WPos, float4 inBaseTC)
{
#if SSAO_TYPE >= 1 // LUMA FT: Added GTAO
	return GTAO(WPos, inBaseTC);
#else // SSAO_TYPE < 0

#if 0 //TODOFT4: remove and fix warning with it
	if (inBaseTC.x <= 0.5)
	{
		return GTAO(WPos, inBaseTC);
	}
#endif

	// Taps are arranged in a spiral pattern
	static const int samplesNum = samplesGroupNum * 2 * (max(SSAO_QUALITY, 0) + 1); // Anything beyond 16 samples has diminishing returns (32 looks a bit better but it's extremely slow)
#if SSAO_QUALITY <= 0
	static const float2 kernel[samplesNum] = {
		float2( -0.14, -0.02 ),
		float2( -0.04, 0.24 ),
		float2( 0.36, 0.08 ),
		float2( 0.26, -0.4 ),
		float2( -0.44, -0.34 ),
		float2( -0.52, 0.4 ),
		float2( 0.3, 0.68 ),
		float2( 0.84, -0.32 )
	};
#else // LUMA FT: increased the samples count to make SSDO look better
	static const float spiralLapses = 2.1; // From >1 to any number (2-3 is starting to be too much for the amount of samples we have).
	static const float spiralAngleOffset = 0.0; // Allow to consistently shift the direction of all the sample. From 0 to PI_X2.
	static float2 kernel[samplesNum];
	// Hopefully the compiler will statically build all this in
	[unroll]
	for (int i = 0; i < samplesNum; i += 1)
	{
		float progress = i / ((float)samplesNum - 1.0); // First iteration is always 0 and last is always 1. "Breaks" if "samplesNum" is 1.
#if 1 // Normalized progress by the number of samples (e.g. if we take 3 samples, we distribute them in an area of 4, with 6 equal splits around the 3 samples). Has a starting offset baked in.
		float samplesOffset = 0.5 / samplesNum;
		float samplesScale = 1.0 - (samplesOffset * 2.0);
		progress = (progress * samplesScale) + samplesOffset;
		float radius = progress;
#else // This version might give more control, but is slower and gives different results depending on "samplesNum"
		static const float spiralMaxRadius = 0.93; // From 0 to 1. Theoretically it could be more but I'm not sure what would happen.
		static const float spiralRadiusOffset = 0.05; // Allow to boost the radius (distance from certer) of the first sample. From 0 to 1.
		float radiusProgress = (progress * (1.0 - spiralRadiusOffset)) + spiralRadiusOffset;
		float radius = radiusProgress * spiralMaxRadius;
#endif
		float angle = (PI_X2 * (progress * spiralLapses)) + spiralAngleOffset;
		kernel[i] = float2(radius * cos(angle), radius * sin(angle));
	};
#endif

	int3 pixCoord = int3(WPos.xy, 0);

	float fCenterDepth = GetLinearDepth(_tex1_D3D11, pixCoord);
	float2 linearUV = inBaseTC.xy;

	float3 vReceiverPos = float3( linearUV.xy * cbSSDO.viewSpaceParams.xy + cbSSDO.viewSpaceParams.zw, 1 ) * fCenterDepth * CV_NearFarClipDist.y;
	
	const float2 targetRadius = cbSSDO.ssdoParams.xy;
	const float minRadius = cbSSDO.ssdoParams.z;
	// Vary maximum radius to get a good compromise between small and larger scale occlusion
	float maxRadius = cbSSDO.ssdoParams.w;
#if ENABLE_SSAO_DENOISE
	// LUMA FT: for each resolution axis being odd, halven the max radius.
	// This is probably connected to the 4x4 jitter/denoising that runs later.
	if (int(WPos.x) & 1) maxRadius *= 0.5;
	if (int(WPos.y) & 1) maxRadius *= 0.5;
#else // LUMA FT: apply the average max radius scale anyway
	maxRadius *= 0.5;
#endif // ENABLE_SSAO_DENOISE
	
	// Use 2 bands so that occlusion works better for small-scale geometry
	static const float smallRadiusMultiplier = 0.15; // LUMA FT: Crytek/Arkane hardcoded magic numbers
	const float2 radiusSmall = clamp( targetRadius * smallRadiusMultiplier / fCenterDepth, minRadius, maxRadius );
	const float2 radiusNormal = clamp( targetRadius / fCenterDepth, minRadius, maxRadius );

#if ENABLE_SSAO_DENOISE
	// Compute jittering matrix
	//TODOFT: add temporal randomization here if we get TAA done properly?
	// LUMA FT: flip the jitter every 4 pixels (this matches the blur that runs after, that is 4x4, so it should not be changed).
	// LUMA FT: SSAO should be affected by the world jitters, so theoretically TAA adds quality to it aver time.
	const float jitterIndex = dot( frac( WPos.xy * 0.25 ), float2( 1, 0.25 ) );

// LUMA FT: added temporal randomization here so that TAA can add more quality to it over time (it should theoretically only be done when TAA is on, but we can't know that here).
// This can look very noisy and weird.
#if ENABLE_SSAO_TEMPORAL
#if 1
	const float time = CV_AnimGenParams.z;
	const float angularTime = time * PI_X2 * 3.0; // 1 sec for 1 full 360 degrees turn by default, so we scale it by a factor
#else // LUMA FT: Ideally this would be done by frame index instead than by time, so it's not affected by frame rate, but we don't have access to the frame index here ("CF_VolumetricFogDistributionParams.w" doesn't seem to work here, nor does g_simulationParameters "c_simulationDeltaTime" or g_particleParameters "c_deltaTime").
	const float frameCount = CF_VolumetricFogDistributionParams.w; // From 0 to 1023
	const float angularTime = (frameCount / 1023.f) * PI_X2 * 1.0;
#endif
#else // !ENABLE_SSAO_TEMPORAL
	static const float angularTime = 0;
#endif // ENABLE_SSAO_TEMPORAL

	float2 vJitterSinCos = float2( sin( PI_X2 * jitterIndex + angularTime ), cos( PI_X2 * jitterIndex + angularTime ) );
	const float2x2 mSampleRotMat = { vJitterSinCos.y, vJitterSinCos.x, -vJitterSinCos.x, vJitterSinCos.y };

	// rotate kernel
	float2 rotatedKernel[samplesNum];
	
	[unroll]
	for (int i = 0; i < samplesNum; i += samplesGroupNum)
	{
		rotatedKernel[i+0] = mul( kernel[i+0].xy, mSampleRotMat );
		rotatedKernel[i+1] = mul( kernel[i+1].xy, mSampleRotMat );
		rotatedKernel[i+2] = mul( kernel[i+2].xy, mSampleRotMat );
		rotatedKernel[i+3] = mul( kernel[i+3].xy, mSampleRotMat );
	}
#else // !ENABLE_SSAO_DENOISE
	float2 rotatedKernel[samplesNum] = kernel;
#endif // ENABLE_SSAO_DENOISE
	
	// Compute normal in view space
	float3 vNormal = DecodeGBufferNormal( _tex0_D3D11.Load(pixCoord) );
	float3 vNormalVS = normalize( mul( CV_ViewMatr, float4(vNormal, 0) ).xyz ) * float3(1, -1, -1);
	
	float4 sh2 = 0;
	[unroll]
	for (int i = 0; i < samplesNum; i += samplesGroupNum)
	{
		const bool narrowBand = i < (samplesNum / 2);
		const float2 radius = narrowBand ? radiusSmall : radiusNormal;
		
		float4 vSampleUV[samplesGroupNum/2];
		vSampleUV[0].xy = linearUV.xy + rotatedKernel[i+0].xy * radius;
		vSampleUV[0].zw = linearUV.xy + rotatedKernel[i+1].xy * radius;
		vSampleUV[1].xy = linearUV.xy + rotatedKernel[i+2].xy * radius;
		vSampleUV[1].zw = linearUV.xy + rotatedKernel[i+3].xy * radius;
		
		float4 vSampleTC[samplesGroupNum/2];
	#if 1
		vSampleTC[0].xy = MapViewportToRaster(vSampleUV[0].xy);
		vSampleTC[0].zw = MapViewportToRaster(vSampleUV[0].zw);
		vSampleTC[1].xy = MapViewportToRaster(vSampleUV[1].xy);
		vSampleTC[1].zw = MapViewportToRaster(vSampleUV[1].zw);
	#else
		vSampleTC = vSampleUV;
	#endif
		
	#if _RT_SAMPLE0 // LUMA FT: Branch on half or full resolution depth buffer, depending on the quality the user set
		float4 fLinearDepthTap = SSDOFetchDepths( _tex2_D3D11, vSampleTC, 3 ) + 0.0000001; // LUMA FT: Arkane seems to have added 0.0000001 here, it's unclear why
	#else
		float4 fLinearDepthTap = SSDOFetchDepths( _tex1_D3D11, vSampleTC, 0 );
	#endif
	
		fLinearDepthTap *= CV_NearFarClipDist.y;

		// Compute view space position of emitter pixels
		float3 vEmitterPos[samplesGroupNum];
		vEmitterPos[0] = float3( vSampleUV[0].xy * cbSSDO.viewSpaceParams.xy + cbSSDO.viewSpaceParams.zw, 1 ) * fLinearDepthTap.x;
		vEmitterPos[1] = float3( vSampleUV[0].zw * cbSSDO.viewSpaceParams.xy + cbSSDO.viewSpaceParams.zw, 1 ) * fLinearDepthTap.y;
		vEmitterPos[2] = float3( vSampleUV[1].xy * cbSSDO.viewSpaceParams.xy + cbSSDO.viewSpaceParams.zw, 1 ) * fLinearDepthTap.z;
		vEmitterPos[3] = float3( vSampleUV[1].zw * cbSSDO.viewSpaceParams.xy + cbSSDO.viewSpaceParams.zw, 1 ) * fLinearDepthTap.w;

		// Compute the vectors from the receiver to the emitters
		float3 vSample[samplesGroupNum];
		vSample[0] = vEmitterPos[0] - vReceiverPos;
		vSample[1] = vEmitterPos[1] - vReceiverPos;
		vSample[2] = vEmitterPos[2] - vReceiverPos;
		vSample[3] = vEmitterPos[3] - vReceiverPos;
		
		// Compute squared vector length
		float4 fVecLenSqr = float4( dot( vSample[0], vSample[0] ), dot( vSample[1], vSample[1] ), dot( vSample[2], vSample[2] ), dot( vSample[3], vSample[3] ) );
		
		// Normalize vectors
		vSample[0] = normalize( vSample[0] );
		vSample[1] = normalize( vSample[1] );
		vSample[2] = normalize( vSample[2] );
		vSample[3] = normalize( vSample[3] );

		// Compute obscurance using form factor of disks
		const float radiusWS = (radius.x * fCenterDepth) * cbSSDO.viewSpaceParams.x * CV_NearFarClipDist.y;
		const float emitterScale = narrowBand ? 0.5 : 2.5; // LUMA FT: hardcoded magic numbers
		const float emitterArea = (emitterScale * PI * radiusWS * radiusWS) / (float)(samplesGroupNum); // LUMA FT: fixed the division being by "samplesNum / 2" instead of "samplesGroupNum"
		float4 fNdotSamp = float4( dot( vNormalVS, vSample[0] ), dot( vNormalVS, vSample[1] ), dot( vNormalVS, vSample[2] ), dot( vNormalVS, vSample[3] ) );
		float4 fObscurance = emitterArea * saturate( fNdotSamp ) / (fVecLenSqr + emitterArea);

		// Accumulate AO and bent normal as SH basis
		sh2.w += dot( fObscurance, 1.0 );
		sh2.xyz += fObscurance.x * vSample[0] + fObscurance.y * vSample[1] + fObscurance.z * vSample[2] + fObscurance.w * vSample[3];
	}
	
	// LUMA FT: fixed hardcoded division by ~8 samples and moved this before view matrix multiplication (the order doesn't matter).
	// The result was actually multiplied by 0.15 instead of 0.125, and that theoretically changes the AO strenght, but it might end up causing clipping on the normals (they are UNORM textures)?
	// Maybe they did it to bring the normals to a more acceptable range, so for consistency we allowed the same offset to be baked in the look.
	sh2.xyzw /= samplesNum;
	static const float hardcodedOfset = 0.15 * 8.0;
	bool wasBeyondOne = any(sh2.xyzw > 1) || any(sh2.xyz < 1);
	sh2.xyzw *= hardcodedOfset;

#if 0 // LUMA FT: quick test to see if the hardcoded offset caused any "clipping" (values beyond the 0-1 range) in the SSAO result (it seems fine)
	bool isBeyondOne = any(sh2.xyzw > 1) || any(sh2.xyz < 1);
	if (!wasBeyondOne && isBeyondOne)
	{
		sh2.xyzw = 1;
	}
#endif

	sh2.xyz = mul( CV_InvViewMatr, float4(sh2.xyz * float3(1, -1, -1), 0) ).xyz;

	float4 outColor;

	// Encode
	outColor.rgb = sh2.xyz * 0.5 + 0.5;
	outColor.a = sh2.w;
	//TODOFT: do saturate because this might end up saved on FP16 textures?
	//TODOFT2: fix "GaussBlurBilinear" color bleeding function not scaling ultrawide colors properly...

#if !ENABLE_SSAO
  	//outColor.rgb = 0; // Disabling bent normals breaks the scene, they'd need to be ignored in a later g-buffer compose pass
  	outColor.a = 0;
#endif // !ENABLE_SSAO
	return outColor;
#endif // SSAO_TYPE >= 1
}