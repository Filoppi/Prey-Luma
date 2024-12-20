#include "include/Common.hlsl"

cbuffer CBSSRRaytrace : register(b0)
{
  struct
  {
    row_major float4x4 mViewProj;
    row_major float4x4 mViewProjPrev;
    float2 screenScalePrev; // Same as "CV_HPosScale.zw"
    float2 screenScalePrevClamp; // Same as "CV_HPosClamp.zw"
  } cbRefl : packoffset(c0);
}

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState ssReflectionPoint : register(s0);
SamplerState ssReflectionLinear : register(s1); // Bilinear sampler with clamp
SamplerState ssReflectionLinearBorder : register(s2); // Bilinear sampler with edge color
Texture2D<float> reflectionDepthTex : register(t0); // Full res linear Depth (0 camera origin, 1 far)
Texture2D<float4> reflectionNormalsTex : register(t1);
Texture2D<float4> reflectionSpecularTex : register(t2);
Texture2D<float4> reflectionDepthScaledTex : register(t3); // Half or quarter res (depends on the "r_arkssr" and "r_SSReflHalfRes" cvars) 4 channel depth (each channel is slightly different) (it's linearized so 0 is not the near plane but the camera origin)
Texture2D<float4> reflectionPreviousSceneTex : register(t4); // Pre-tonemapping HDR scene from the previous frame (jittered)
Texture2D<float2> reflectionLuminanceTex : register(t5); // Scene exposure (probably from the previous frame)

// 0 None (vanilla mip map specular based blurring)
// 1 Local (expensive), world reflection ray length based. This logic has been moved to "SSReflection_Comp", we achieve it by outputting an extra "diffuseness" amount buffer here and using mip maps for reflections there
// 2 Mip Map, world reflection ray length based (accounting for specularity too)
// 3 Mip Map, screen space distance based (accounting for specularity too)
#define BLUR_REFLECTIONS_TYPE 2
// Vanilla CryEngine was trailing reflections outside of the screen range with whatever color they had at the closest edge. That looked pretty awful and the cubemap fall back almost always looks a lot better.
// This also optimizes reflections.
#define SKIP_OUT_OF_BOUNDS_REFLECTIONS 1
// This was the vanilla CryEngine behaviour, but it was broken when adding DRS as we always clamped UVs to 0-1, thus never really sampled beyond their limits and made the border color visible or useful.
// Especially since adding "SKIP_OUT_OF_BOUNDS_REFLECTIONS", this doesn't really have much use anymore, as we stop ray marching when we try to sample out of view UVs
// (though the reflection is based on the previous frame final HDR color buffer, so after reprojecting the UV, it could still be out of bounds, but we are talking about a one frame difference).
// This does not look good even when on, it's better to trail the last color at the texture edge.
#define ENFORCE_BORDER_COLOR 0
// Disable this if it gets too distracting
#define ALWAYS_BLEND_IN_CUBEMAPS 1
#define REJITTER_RELFECTIONS 1

// LUMA FT: added device depth support (for no reason really)
float GetLinearDepth(float fDepth, bool bScaled = false, bool bDeviceDepth = false)
{
  if (bScaled)
  {
    if (bDeviceDepth)
    {
      fDepth = ((1.0 - fDepth) * (CV_NearFarClipDist.y - CV_NearFarClipDist.x)) + CV_NearFarClipDist.x;
    }
    else
    {
      fDepth *= CV_NearFarClipDist.y;
    }
  }
  else if (bDeviceDepth)
  {
    float fRelativeNear = CV_NearFarClipDist.x / CV_NearFarClipDist.y;
    fDepth = (1.0 - fDepth) + (fRelativeNear * fDepth);
  }
  return fDepth;
}

float LinearDepthFromDeviceDepth(float _device)
{
	return CV_ProjRatio.y / (_device - CV_ProjRatio.x);
}

struct MaterialAttribsCommon
{
	half3  NormalWorld;
	half3  Albedo;
	half3  Reflectance;
	half3  Transmittance;
	half   Smoothness;
	half   ScatteringIndex;
	half   SelfShadowingSun;
	int    LightingModel;
};

#define MAX_FRACTIONAL_8_BIT        (255.0f / 256.0f)
#define MIDPOINT_8_BIT              (127.0f / 255.0f)
#define TWO_BITS_EXTRACTION_FACTOR  (3.0f + MAX_FRACTIONAL_8_BIT)
#define LIGHTINGMODEL_STANDARD       0
#define LIGHTINGMODEL_TRANSMITTANCE  1
#define LIGHTINGMODEL_POM_SS         2
#define LIGHTINGMODEL_ALIEN          3

half3 DecodeColorYCC( half3 encodedCol, const bool useChrominance = true )
{
	encodedCol = half3(encodedCol.x, encodedCol.y / MIDPOINT_8_BIT - 1, encodedCol.z / MIDPOINT_8_BIT - 1);
	if (!useChrominance) encodedCol.yz = 0;
	
	// Y'Cb'Cr'
	half3 col;
	col.r = encodedCol.x + 1.402 * encodedCol.z;
	col.g = dot( half3( 1, -0.3441, -0.7141 ), encodedCol.xyz );
	col.b = encodedCol.x + 1.772 * encodedCol.y;

	return col * col;
}

MaterialAttribsCommon DecodeGBuffer( half4 bufferA, half4 bufferB, half4 bufferC )
{
	MaterialAttribsCommon attribs;
	
	attribs.LightingModel = (int)floor(bufferA.w * TWO_BITS_EXTRACTION_FACTOR);
	
	attribs.NormalWorld = normalize( bufferA.xyz * 2 - 1 );
	attribs.Albedo = bufferB.xyz * bufferB.xyz;
	attribs.Reflectance = DecodeColorYCC( bufferC.yzw, attribs.LightingModel == LIGHTINGMODEL_STANDARD );
	attribs.Smoothness = bufferC.x;
	attribs.ScatteringIndex = bufferB.w * TWO_BITS_EXTRACTION_FACTOR;
	
	attribs.Transmittance = half3( 0, 0, 0 );
	if (attribs.LightingModel == LIGHTINGMODEL_TRANSMITTANCE)
	{
		attribs.Transmittance = DecodeColorYCC( half3( frac(bufferA.w * TWO_BITS_EXTRACTION_FACTOR), bufferC.z, bufferC.w ) );
	}
	
	attribs.SelfShadowingSun = 0;
	if (attribs.LightingModel == LIGHTINGMODEL_POM_SS)
	{
		attribs.SelfShadowingSun = saturate(bufferC.z / MIDPOINT_8_BIT - 1);
	}
	
	return attribs;
}

float3 GetWorldViewPos()
{
	return CV_ScreenToWorldBasis._m03_m13_m23;
}

float3 ReconstructWorldPos(int2 WPos, float linearDepth, bool bRelativeToCamera = false)
{
	float4 wposScaled = float4(WPos * linearDepth, linearDepth, bRelativeToCamera ? 0.0 : 1.0);
	return mul(CV_ScreenToWorldBasis, wposScaled); // This also converts the depth to world space
}

float3 RotateNormal(float3 normal, float3 axis, float angle)
{
    // Ensure the axis is normalized
    axis = normalize(axis);

    // Compute sine and cosine of the angle
    float s = sin(angle);
    float c = cos(angle);

    // Construct the rotation matrix components
    float3x3 rotationMatrix = float3x3(
		c + axis.x * axis.x * (1 - c), axis.x * axis.y * (1 - c) - axis.z * s, axis.x * axis.z * (1 - c) + axis.y * s,
		axis.y * axis.x * (1 - c) + axis.z * s, c + axis.y * axis.y * (1 - c), axis.y * axis.z * (1 - c) - axis.x * s,
		axis.z * axis.x * (1 - c) - axis.y * s, axis.z * axis.y * (1 - c) + axis.x * s, c + axis.z * axis.z * (1 - c)
    );

    // Rotate the normal vector
    return mul(rotationMatrix, normal);
}

float2 ClampScreenTC(float2 TC, float2 maxTC)
{
	return min(TC, maxTC.xy); // LUMA FT: optmized away the max with 0, it's not needed and it breaks border sampling, see "ENFORCE_BORDER_COLOR"
}
float2 ClampScreenTC(float2 TC)
{
	return ClampScreenTC(TC, CV_HPosClamp.xy);
}

// This draws the "first" ray traced (ray marched) screen space reflections buffer, either at full or half resolution (depending on the "r_arkssr" and "r_SSReflHalfRes" cvars).
void main(
  float4 inWPos : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0,
  out float outDiffuse : SV_Target1)
{
	outColor = 0; // No alpha, black
	outDiffuse = 1; // Default to 100% diffuseness just in case (it's more common that 0%)

	// LUMA FT: the uv doesn't need to be scaled by "CV_HPosScale.xy" here (it already is in the vertex shader)
	// LUMA FT: fixed missing clamps to "CV_HPosClamp.xy"
	inBaseTC.xy = min(inBaseTC.xy, CV_HPosClamp.xy);

	const float2 baseTC    = inBaseTC.xy;
#if 1 // LUMA FT: fixed random UV offset
	const float2 gbufferTC = baseTC;
#else // LUMA FT: unclear why this is done, it seems wrong, whether the render target resolution was halved or not (there's no need to snap the G-Buffer samples to their closest texel)
	const float2 halfTexel = CV_ScreenSize.zw;
	// Make sure we do linear half pixel offset samples since we might be rendering at half res
	const float2 gbufferTC = baseTC + halfTexel;
#endif
	
    const float fDepth = GetLinearDepth( reflectionDepthTex.SampleLevel(ssReflectionPoint, baseTC, 0), false ); // "linear" depth, relative to the camera plane

	float4 GBufferA = reflectionNormalsTex.SampleLevel(ssReflectionLinear, gbufferTC, 0);
	float4 GBufferC = reflectionSpecularTex.SampleLevel(ssReflectionLinear, gbufferTC, 0);
	MaterialAttribsCommon attribs = DecodeGBuffer(GBufferA, 0, GBufferC);
	
	float3 cameraToWorldPos = ReconstructWorldPos(inWPos.xy, fDepth, true); // World location relative to the camera location (but not rotation)
	float3 viewVec = normalize( cameraToWorldPos ); // View vector from the camera origin to the view world location
	float3 vPositionWS = cameraToWorldPos + GetWorldViewPos(); // Actual 3D world location, relative to the world origin
	
	// LUMA FT: the higher this coefficient (relative to the far plane...), the bigger the angle range that won't get reflections, and the taller the Z (height) range of reflections we'll get.
	// This will also increase the intervals between reflections depth samples so it will lower the SSR quality if it goes too high, unless you also increase the samples number.
	// In the original code, it was made very short to avoid SSR constantly coming and out of view (e.g. when looking down), even if you might see them suddenly ending "cropped" out,
	// we've improved the edges blending so that's not necessary anymore.
#if SSR_QUALITY >= 2
	static const float reflectionDistanceMultiplier = 4;
#elif SSR_QUALITY == 1
	static const float reflectionDistanceMultiplier = 3.333; // This seems like the best balance between distance and quality (it's high, it reflects far in the distance, though we could go even higher!), we shouldn't go below 2.5, nor beyond 4
#else
	static const float reflectionDistanceMultiplier = 1.5;
#endif
	const float maxReflDist = reflectionDistanceMultiplier * fDepth * CV_NearFarClipDist.y; // The further away from the camera, the further we try to reflect (it's a bit random, but it works)

	// We shoot the reflection in the direction determined by the normal, as we'd expect, though while in reality the normal would probably have infinite "detail" and thus lead reflections in all kind of directions,
	// in Prey normals are low resolution and don't portray the specular property of the material (minute details (bumps) in the surface), so to emulate diffuse reflections, we can randomize the direction a bit.
	float3 worldNormal = attribs.NormalWorld;

#if 0 //TODO LUMA: if we ever wanted to actually randomize the reflection ray a bit (per pixel?) to emulate diffuseness we could, but it might not be such a good idea as it'd get noisy
	float randomizationAmount = 1.0 - sqr(attribs.Smoothness);
	float randomizationIteration = (LumaData.FrameIndex % 8) / 7.0;
#if 1
    float3 axis = normalize(NRand3(inBaseTC.xy * randomizationIteration));
    float angle = radians(45.0) * randomizationAmount * NRand3(inBaseTC.yx * randomizationIteration).x;
    worldNormal = RotateNormal(worldNormal, axis, angle); // I got this formula from AI so it might not be correct
#else
	worldNormal += NRand3(inBaseTC.xy * randomizationIteration) * randomizationAmount * 0.125; // This is currently not limited to a 45 degrees max rotation
	worldNormal = normalize(worldNormal);
#endif
#endif

	float3 reflVec = normalize( reflect( viewVec, worldNormal ) );
	
	// Normals dot product results are:
	//  - 1 if they are facing the exact same direction
	//  - (0,1) if they are facing anything between 0 and 90 degrees away
	//  - 0 if they are facing 90 degrees away
	//  - <0 if they are facing more than 90 degrees away
	//  - -1 if they are facing the exact opposite direction
#if 0 // LUMA FT: original code (worse)
	// LUMA FT: for some reason this was applied in before calculating "dirAtten", but that lowered the angular range of surfaces that reflect (possibly to make them less appalling when they come in and out of view, given there's no cubemap fallback),
	// and also caused issues with ultrawide at the edges.
	// The reasoning is that FoV can't be greater than 90 degrees (on each side), so these reflections wouldn't catch a texel on screen?
	reflVec *= maxReflDist;
	float dirAtten = saturate( dot( viewVec, reflVec ) + 0.5 ); // LUMA FT: Adding an offset here (e.g. 0.5) seems worse than not doing it!
#elif 0 // LUMA FT: second best implementation
	// Ultimately what this does is blend out of view reflections that have angle close to values that would make them not visible anymore, starting from a ~45 deg threshold,
	// so below ~45 deg there'd be full intensity, and as they go beyond, they go more transparent, so when they reach the edges of the screen, they've been blended out organically without having a jarring jump.
	// if the offset was at 1, we'd leave all valid reflections in view with no blend in/out, and thus they jump in and out of the screen as the camera is moving.
	float dirAtten = saturate( dot( viewVec, reflVec ) ); // Adding an offset here causes some additional "cool" reflections, but the problem is that they pop in and out of view and have untiguous gradients and some artifacts as they try to reflect off screen or something
	reflVec *= maxReflDist;
#else // LUMA FT: new improved implementation that generates a lot of reflections and all of them with smooth gradients (they don't pop in and out weirdly)
	float dirAtten = saturate( dot( viewVec, reflVec ) * 2.0 ); // A 2 scale looks about right here, but this could be further tweaked, and maybe changed to pow scaling (it's probably fine as it is, given it's a dot product result)
	reflVec *= maxReflDist;
#endif

#if 0 // Test: view smoothness map
	outDiffuse = attribs.Smoothness;
	return;
#endif

	// LUMA FT: lowered this threshold (from 0.01) to avoid rough cuts to SSR (this is not quantized to 8 bit, at least with Luma), we could even go to 0
	// This is an optimization to avoid tracing reflections that are backwards or that would be set to alpha 0, it's theoretically not necessary anymore since adding "SKIP_OUT_OF_BOUNDS_REFLECTIONS".
	if (dirAtten <= 0.001) return;
	// Ignore sky (draw black, no alpha) (there's no reason to try and draw SSR on the sky texels... (and g-buffers don't get drawn on it and trail behind from the last drawn object in the screen space texels))
	// LUMA FT: change sky comparison from ==1 to >=0.9999999 as there was some precision loss in there, which made the SSR have garbage in the sky (trailed behind from the last drawn edge)
	if (fDepth >= 0.9999999) return;
	
	float4 rayStart = mul(cbRefl.mViewProj, float4( vPositionWS, 1 ));
	rayStart.z = fDepth;

	float4 rayEnd = mul(cbRefl.mViewProj, float4( vPositionWS + reflVec, 1 ));
	rayEnd.z = LinearDepthFromDeviceDepth(rayEnd.z / rayEnd.w);

	float4 ray = rayEnd - rayStart;
	
  // Most surfaces are smooth to a good degree
  // LUMA FT: added higher quality (we scale the non smoothness fixed samples separately as it seemed like a good idea)
#if SSR_QUALITY >= 3 // Undocumented max quality (we don't want users to even try it, it's too slow, even if it's not as high as we'd need to go for proper clean reflections)
	uint numSamples = 8 + attribs.Smoothness * 84;
#elif SSR_QUALITY >= 2
	uint numSamples = 6 + attribs.Smoothness * 56;
#elif SSR_QUALITY == 1
	uint numSamples = 5 + attribs.Smoothness * 42;
#else
	uint numSamples = 4 + attribs.Smoothness * 28;
#endif
	
#if 0 // LUMA FT: this was disabled, it doesn't seem to help
	// Random values for jittering a ray marching step
	const half jitterOffsets[16] = {
		0.215168h, -0.243968h, 0.625509h, -0.623349h,
		0.247428h, -0.224435h, -0.355875h, -0.00792976h,
		-0.619941h, -0.00287403h, 0.238996h, 0.344431h,
		0.627993h, -0.772384h, -0.212489h, 0.769486h
	};

	const int jitterIndex = (int)dot( frac( inBaseTC.zw ), float2( 4, 16 ) );
	const float jitter = jitterOffsets[jitterIndex] * 0.002;
#else
	const float jitter = 0;
#endif
	
	const float stepSize = 1.0 / numSamples + jitter;
	// LUMA FT: Higher values mean we'll have a lower acceptance threshold of a raymarched attempted reflected ray, and thus reflections would look sharper (higher quality, more realistic, not related to diffuseness),
	// while lower values make it look more segmented, as the reflected location could accidentally go back and forth (because we stop at the first found match), though it's got a smaller performance cost!
	// Too high values could mean we don't find a matching ray to reflect, so, avoid them, Crytek balanced this value near perfectly already, we couldn't do any better.
	// (I might have flipped the description of two cases above)
	static const float samplesIntervalScale = 1.6; // Heuristically found by Crytek
	const float intervalSize = maxReflDist / (numSamples * samplesIntervalScale) / CV_NearFarClipDist.y;
	// LUMA FT: added variation in the length of the steps, the closer we are to the reflection, the shorter we make the steps, and the away we are, the longer we make them.
	// This provies higher quality close to the camera, and lower "quality" for reflections that will likely end up being diffused anyway, so it's both an optimization and improvement on looks.
	// Note that this might not scale the intervals up a 100% perfectly and might leave some holes between iterations that fail to find a match, thus reflections could look more segmented, but generally it looks good.
	// Set to 0 to disable it (ignoring the implementation).
	static const float range = 0.5; // A value around 0.5 looks the most balanced and best. For now we do this even at "SSR_QUALITY" 0
	
	// Perform raymarching
	float rayProgress = numSamples == 1 ? 0.5 : 0.f; // "i" is 0
	float len = lerp(stepSize * (1.0 - range), stepSize * (1.0 + range), rayProgress); // Compiler should be smart enough to detect this value is static
	float bestHitLenght = 0;
	float3 bestHitDepth = 0;
	float2 sampleUVClamp = CV_HPosScale.xy - CV_ScreenSize.zw;
	float2 prevDepthTC = 0.5;
	float furthestdepthFromCameraCenter = 0.0;
	[loop]
	for (uint i = 0; i < numSamples;)
	{
		float4 projPos = rayStart + ray * len;
		float2 depthTC = projPos.xy / projPos.w; // Somehow this is already scaled by "CV_HPosScale.xy"

// LUMA FT: added early out to not sample world position that map beyond the borders of our textures,
// given they'd just trail the last texel at the edge with a not great perspective.
// This makes "reflectionDistanceMultiplier" partially unnecessary as we can just go towards infinite and then stop when we are out of the screen (it's faster too)
#if SKIP_OUT_OF_BOUNDS_REFLECTIONS
		// If we are out of bounds and we are going in the same direction as the previous sample (just an extra safety check), stop the search
		if ((any(depthTC > 1) || any(depthTC < 0)) && dot(depthTC - 0.5, prevDepthTC - 0.5) > 0.0)
		{
			break;
		}
		prevDepthTC = depthTC;
#endif
		
		// LUMA FT: fixed depth clamp, it was using "CV_HPosClamp" here but "reflectionDepthScaledTex" is half resolution, so we need to clamp it differently
		float fLinearDepthTap = reflectionDepthScaledTex.SampleLevel(ssReflectionPoint, ClampScreenTC(depthTC, sampleUVClamp), 0).x; // half res R16F
		bool skip = false;

// LUMA FT: stop if the depth becomes higher, given that we couldn't realistically be reflecting that.
// Currently disabled as it hides too many reflections that would otherwise would look "good" (unless you stopped to look at them and observe they make no sense and are flipped in direction) (especially with lowerish samples numbers).
// Reflecting impossible stuff is simply an assumption that SSR make and they work okish with that.
// If we wanted to do this properly, we'd need to consider the reflection angle as well and only discard samples based on depth depending on the angle.
#if 0
		float depthFromCameraCenter = ReconstructWorldPos(depthTC * CV_ScreenSize.xy, fLinearDepthTap, true).z; // This should be more correct that directly using "fLinearDepthTap"
		if (furthestdepthFromCameraCenter != 0 && (depthFromCameraCenter / furthestdepthFromCameraCenter) > 1.0 + FLT_EPSILON)
		{
			skip = true;
		}
		furthestdepthFromCameraCenter = max(furthestdepthFromCameraCenter, depthFromCameraCenter);
#endif

		float currentIntervalSize = (range == 0.0) ? intervalSize : lerp(intervalSize * (1.0 - range), intervalSize * (1.0 + range), rayProgress);
		if (!skip && abs(fLinearDepthTap - projPos.z) <= currentIntervalSize) // Acceptance threshold
		{
			bestHitDepth = fLinearDepthTap;
			bestHitLenght = len;
			break;
		}

		i++;

		// We have to do this before changing "len" as that's for the next loop
		rayProgress = ((float)i) / (numSamples - 1.0);

		len += (range == 0.0) ? stepSize : lerp(stepSize * (1.0 - range), stepSize * (1.0 + range), rayProgress);
	}

	float4 color = 0;
	[branch]
	if (bestHitLenght > 0)
	{
		const float curAvgLum = reflectionLuminanceTex.SampleLevel(ssReflectionPoint, baseTC, 0).x; // LUMA FT: this is 1px so it could be replaced with a Load() but it's using a nearest neightbor sampler so it should be the same
		// LUMA FT: this doesn't really seem to do to anything (the value is too high?), even on strong lights, but then again, we don't really need it as we have TAA and mip map that blur small overly bright sparks
		const float maxLum = curAvgLum * 100;  // Limit brightness to reduce aliasing of specular highlights

		float4 bestSample = float4( vPositionWS + reflVec * bestHitLenght, 1 );

		//TODO LUMA: cache the last "reflectionPreviousSceneTex" texture of when the character looked straight (with its "cbRefl.mViewProjPrev"), and fall back on it in case the reflection UV fell out of the current "reflectionPreviousSceneTex"

		// Reprojection
		// This matrix should includes the jitters from the previous frame, so it will dejitter the UVs accordingly, avoiding shimmering that can't be properly reconstructed by TAA in reflections.
		// In a way, if we left them jittered, TAA could reconstruct more detail for them over time, but... the jitters are flipped so TAA will likely instead fail at detecting the stable color of that pixel (only relevant when we are not moving the camera really).
		float4 reprojPos = mul(cbRefl.mViewProjPrev, bestSample);
#if ENFORCE_BORDER_COLOR
		float2 prevTC = reprojPos.xy / reprojPos.w;
		float2 clampedPrevTC = saturate(prevTC);
#else
		float2 prevTC = saturate(reprojPos.xy / reprojPos.w);
		float2 clampedPrevTC = prevTC;
#endif

		// Fade out at borders (bigger values mean bigger borders)
		// Note that given that the output is low quality, you could see banding in it unless we upgrade its textures
#if SSR_QUALITY >= 1 // This isn't needed with the short max reflection distance of the lowest setting
		static const float borderSize = 0.175; // LUMA FT: made the border size a lot better for stuff on the floor (a bigger radius hurts a bit for reflections within the front view, when looking forward, but there's no fixing both without hacks)
		static const float borderPow = 1.875;
		static const float borderTargetAspectRatio = 1.0; // LUMA FT: made the border target a square aspect ratio instead of 16:9 as vanilla would have "usually" targeted, it looks better
		static const float bottomBorderScale = 0.667; // LUMA FT: hacky way of avoiding the border from the bottom being too big. Players often look down, so it's important to make the transitions downwards smooth, and we still want the transitions upwards to be smooth, but we also don't want their SSR to disappear until they really have to...
#else
		static const float borderSize = 0.07;
		static const float borderPow = 2.0; // Original was pow 0.5 but it made no sense, it made things worse
		static const float borderTargetAspectRatio = NativeAspectRatio;
		static const float bottomBorderScale = 1.0;
#endif
		// LUMA FT: fixed border checks not accounting for wider aspect ratios and FOVs (this change barely seem to do anything) (this could make things slightly worse at 4:3).
		// Maybe we should use the ratio between the current FoV in tangent space and the one at the native aspect ratio, or anyway scale the borders with FoV, but this is good enough.
		//TODO LUMA: this reflections "dead zone" is a square, we should probably try for an ellypse around the screen, similar to vignette, so it wouldn't have "hard" corners
		float screenAspectRatio = CV_ScreenSize.w / CV_ScreenSize.z;
		float TCXScale = lerp(screenAspectRatio / borderTargetAspectRatio, 1.0, clampedPrevTC.x);
		float borderDist = min(clampedPrevTC.x * TCXScale, clampedPrevTC.y);
		float TCYScale = lerp(bottomBorderScale, 1.0, clampedPrevTC.x);
		borderDist = min( 1 - max(clampedPrevTC.x / TCXScale, clampedPrevTC.y * TCYScale), borderDist );
		float edgeWeight = (borderSize > 0) ? saturate(pow(borderDist / borderSize, borderPow)) : 1.0; // LUMA FT: changed sqrt() to pow() to make the blending out smoother (the higher the pow exponent, the more "gradual" the blend out is)

		// LUMA FT: this sampler had a border color (black), though given that we scaled the resolution clamped UVs, we never got that.
		// We fixed it by branching on samples that wouldn't touch any texel within the render resolution area.
		// It's unclear whether this actually is correct and helps visually (maybe the border was set to the scene average color or sky color as a fallback), and whether the alpha should be forced to zero in that case too (why would anything reflect downwards anyway?).
		sampleUVClamp = cbRefl.screenScalePrevClamp + CV_ScreenSize.zw;

#if BLUR_REFLECTIONS_TYPE == 1 //TODOFT: finish or delete (this is extremely expensive and doesn't look better really)
		static const uint samplesByBlurriness = 8;
		static const float globalBlurrinessRange = LumaSettings.DevSetting04 / 50.0; // UV 0.749 / 50

		// The distance of the viewer from the reflecting point shouldn't matter
		float reflectionLength = length(reflVec) * bestHitLenght;
		float blurriness = reflectionLength * LumaSettings.DevSetting02 * (1.0 - sqr(attribs.Smoothness));
		uint blurrinessSamples = clamp(blurriness * samplesByBlurriness, 0, 8 * LumaSettings.DevSetting03) + 2;
		float blurrinessRange = globalBlurrinessRange * blurriness; // Radius
		for (uint i = 0; i < blurrinessSamples; ++i)
		{
			for (uint k = 0; k < blurrinessSamples; ++k)
			{
#endif // BLUR_REFLECTIONS_TYPE == 1
				float2 localPrevTC = prevTC;
#if BLUR_REFLECTIONS_TYPE == 1
				float2 progress = float2(i, k) / ((float)blurrinessSamples - 1.0); // First iteration is always 0 and last is always 1. "Breaks" if "blurrinessSamples" is 1.
				float2 blurrinessAmountAndDirection = progress * 2.0 - 1.0;
				localPrevTC += blurrinessAmountAndDirection * blurrinessRange;
#endif // BLUR_REFLECTIONS_TYPE == 1
#if REJITTER_RELFECTIONS // LUMA FT: re-jitter the dejittered reflections UV with the current's frame jitter, so that TAA will reconstruct them better
				localPrevTC += LumaData.CameraJitters.xy * float2(0.5, -0.5); // Even if the texture we sampled had a different resolution, we want to apply the jitters with the UV offset of the main rendering resolution
#endif
				localPrevTC *= cbRefl.screenScalePrev;
#if ENFORCE_BORDER_COLOR
				if (localPrevTC.x < sampleUVClamp.x && localPrevTC.y < sampleUVClamp.y)
				{
#endif // ENFORCE_BORDER_COLOR
					localPrevTC = ClampScreenTC(localPrevTC, cbRefl.screenScalePrevClamp);
#if !ENFORCE_BORDER_COLOR // Without this the can be a tiny bit of flickering on reflections at the top left edge of the reflections, due to jittering UVs, it might be related to "REJITTER_RELFECTIONS"
					localPrevTC = max(localPrevTC.xy, CV_ScreenSize.zw /*half texel size*/);
#endif
					//TODO LUMA: add depth rejection (with the depth from the previous frame) to avoid your own weapon (e.g.) being reflected into bodies of water when turning the camera
					color.rgb += reflectionPreviousSceneTex.SampleLevel(ssReflectionLinearBorder, localPrevTC , 0).rgb;
#if ENFORCE_BORDER_COLOR
				}
				else
				{
#if 0 // Disabled as color was already zero and if it's additive it won't change the value
					color.rgb += 0;
#elif 0 // Code to actually use the real border color, or to simply test it (sample out of bounds, e.g. coords 2 2) (it seems like it's always black)
					color.rgb += reflectionPreviousSceneTex.SampleLevel(ssReflectionLinearBorder, 2.0, 0).rgb;
#endif // 0
				}
#endif // ENFORCE_BORDER_COLOR
#if BLUR_REFLECTIONS_TYPE == 1
			}
		}
		color.rgb /= blurrinessSamples * blurrinessSamples;
#endif // BLUR_REFLECTIONS_TYPE == 1

		// Map these to a value that is "perceptually" linear, as in, doubling the value would produce a result twice as smooth from the point of view of a human (and in screen space reflections space!)
		float perceptualSmoothness = sqr(attribs.Smoothness);
		float perceptualDiffuseness = 1.0 - perceptualSmoothness;

#if BLUR_REFLECTIONS_TYPE >= 2
#if BLUR_REFLECTIONS_TYPE == 2
		// LUMA FT: Calculate the "screen space diffuseness", given that this directly goes to control what mip of a texture we use.
		// Theoretically, in reality, the only thing that determines how diffuse a reflection is from a observer's point of view is the "specularity" of the material,
		// and the distance of the reflecting ray, with the observer distance having no influence on it,
		// but here we are using this to blur a screen space render texture (through mip maps), so as we get closer to the reflected point, we need to blur the texture more to cause
		// the same percevied diffuseness/blurring effect (that the same exact reflection would have had if we were further away).
		float reflectionLength = length(reflVec) * bestHitLenght; // The reflection's ray bounced length (in world units)
		static const float maxDiffusenessReflectionLength = 7.5; // We picked this value (in meter) heuristically, based on tests in many places. Values between 5 and 15 look good in different reflections.

		float currentMaxDiffusenessReflectionLength;
		// This formula dynamically scales the target length at which we go fully diffused based on the specularity of the material.
		// We map a "perceptual" smoothness of 0.5 to a specific reflection distance to diffuse the reflection to the maximum we can. 
		// If the material is fully diffuse, it will take an infinite distance to blur textures to the bluerriest mip map with have (which is an arbitrary max, but looks good!).
		// If the material is half diffuse (and thus half specular), it will take (e.g.) 5m to go to the bluerriest.
		// If the material is fully specular, it will never blur, independently of the ray distance.
		// 
		// There's possibly better math to achieve the same result, but this works, and seems to be contiguous around the 0.5 branch anyway.
		if (perceptualSmoothness >= 0.5)
		{
			currentMaxDiffusenessReflectionLength = pow(maxDiffusenessReflectionLength + 1.0, 0.5 / perceptualDiffuseness) - 1.0; // With a bigger pow (> 1) (and thus less diffuseness), the radius grows towards infinite (which is what we want), going to +INF for a 100% specularity
		}
		else
		{
			currentMaxDiffusenessReflectionLength = (1.0 / (pow(1.0 / (maxDiffusenessReflectionLength + 1.0), perceptualSmoothness * 2.0))) - 1.0; // With a smaller pow (< 1) (and thus less specularity), the radius shrinks to zero (which is what we want), going to 0 for a 100% diffuseness
		}
#if 0 // Old method that wasn't contiguous around 0.5, disabled as it's a bit random, even if it can look good
		currentMaxDiffusenessReflectionLength = (perceptualSmoothness >= 0.5) ? lerp(maxDiffusenessReflectionLength * 10.0, maxDiffusenessReflectionLength, perceptualDiffuseness * 2.0) : lerp(0.f, maxDiffusenessReflectionLength, perceptualSmoothness * 2.0);
#endif
		//TODO LUMA: rebalance this a bit, maybe apply a pow? reflections don't get that much bluerried with distance, they don't scale exactly as one'd expect
		outDiffuse = reflectionLength / currentMaxDiffusenessReflectionLength; // We tried applied a pow to this result but it's good as it is

#if 0 //TODOFT: test more and finish or delete, it doesn't really seem to be needed? We could use a rebalancing of the ray distance control to blur more aggressively when the distance changes
		// As the reflected point gets closer to the screen (up to a threshold), scale up its diffuness, as a way to make them blur more, because they'd be bigger in the view and require a lower mip map to give the same perceived diffuseness
		float cameraPlaneDistance = fDepth * CV_NearFarClipDist.y; // Distance from the camera plane to the reflecting point (in world units)		
		outDiffuse *= max(maxDiffusenessReflectionLength / cameraPlaneDistance, 1.0); // Re-use "maxDiffusenessReflectionLength" even if theoretically this is unrelated
#endif
#elif BLUR_REFLECTIONS_TYPE >= 3 // Alternative: blur based on the UV space distance of the reflection point and the reflected point (this might be closer to the look we'd expect)
		outDiffuse = pow(perceptualDiffuseness, 0.667 / length((inBaseTC.xy / float2(screenAspectRatio, 1.0) / CV_HPosScale.xy) - (prevDepthTC.xy / float2(screenAspectRatio, 1.0) / CV_HPosScale.xy))); // Heuristically found values
#endif // BLUR_REFLECTIONS_TYPE == 2

		// Avoid NaN spreading through (just extra safety) (smoothness can't be beyond 0-1 as the G-Buffers are UNORM textures)
		if (attribs.Smoothness <= 0 || attribs.Smoothness >= 1)
		{
			outDiffuse = perceptualDiffuseness;
		}
		// Theoretically: 0 deg at 0 and 90 deg (on each side) at 1
		// We tried re-using the alpha channel but we can't
#else // BLUR_REFLECTIONS_TYPE <= 1
#if BLUR_REFLECTIONS_TYPE == 1
		outDiffuse = 0; // Diffuseness is (for the most part) already baked in this texture
#else // BLUR_REFLECTIONS_TYPE == 0
		outDiffuse = 1.0 - sqr(attribs.Smoothness);
#endif // BLUR_REFLECTIONS_TYPE == 1
#endif // BLUR_REFLECTIONS_TYPE >= 1
		
		// Filter out NANs that we still have sometimes, otherwise they get propagated and remain in the view
		color.rgb = isfinite( color.rgb ) ? min( color.rgb, maxLum.xxx ) : 0;

		color.a = edgeWeight;  // Fade out at the edges
		color.a *= dirAtten;   // Fade out where less information available (it looks awful without this)
#if ALWAYS_BLEND_IN_CUBEMAPS
		// LUMA FT: let the fallback cubemaps blend in to a small extent, so SSR is less jarring when moving out of view (this makes cubemaps always visible through SSR, so that can look weird too, as their perspective and distortion is often broken).
		// This might slightly boost the overall brightness of the scene (due to the added color and to cubemaps being brighter?), giving the impression of SSGI or something.
		static const float cubemapInverseAmount = 0.667;
		float currentCubemapInverseAmount = pow(cubemapInverseAmount, 1.0 - attribs.Smoothness); // We can scale it to one as the material gets more specular (we don't want to modulate the smoothness parameter, it looks good applied in "linear")
		color.a *= currentCubemapInverseAmount;
		color.rgb /= lerp(currentCubemapInverseAmount, 1.0, 0.5); // Boost the color brightness (by half the amount) to counter for the reduced alpha (this is optional) (at least with Luma, the output texture in float and thus allows values beyond 1, so this won't clip, but it'd be okish even if it did probably)
#endif

		outColor = color;
	}
}