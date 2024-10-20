#include "include/Common.hlsl"

#include "include/CBuffer_PerViewGlobal.hlsl"

Texture2D<float4> _tex0_D3D11 : register(t0);
Texture2D<float4> _tex2_D3D11 : register(t2);

#include "include/MotionBlur.hlsl"

struct vtxOut
{
  float4 WPos   : SV_Position0;
  float4 baseTC : TEXCOORD0;
};

struct pixout
{
  float4 Color  : COLOR0;
};

// LUMA FT: fixed motion vectors not being scaled properly with DRS
float2 AdjustVelocityObjects(float2 VelocityObjects)
{
	VelocityObjects /= LumaData.RenderResolutionScale;
#if 0 //TODOFT3: hack, see "FORCE_MOTION_VECTORS_JITTERED" below (this won't really work at high frame rates though) (not needed anymore, seems all fine now!)
	if (abs(VelocityObjects.x) <= (1.0 / CV_ScreenSize.x) && abs(VelocityObjects.y) <= (1.0 / CV_ScreenSize.y))
	{
		VelocityObjects = 0;
	}
#endif
	return VelocityObjects;
}

//TODOFT3: upgrade MB patches size to be smaller? So it's higher quality? It seems pretty awful atm.
// LUMA FT: this doesn't exactly produce motion vectors, but simply some patches of movement intensity.
// It's run in big patches (e.g. 6, 14 or 24 patches), depending on the quality of MB, that probably doesn't scale properly with aspect ratio.
pixout PackVelocitiesPS(vtxOut IN)
{	
	pixout OUT = (pixout)0;
	int3 pixelCoord = int3(IN.WPos.xy, 0);
	float2 baseTC = MapViewportToRaster(IN.baseTC.xy);

	const float fDepth = GetLinearDepth(_tex0_D3D11, pixelCoord).x; // LUMA FT: this should be the current's frame depth buffer
	const float3 vPosWS = ReconstructWorldPos(pixelCoord.xy, fDepth);

	// LUMA FT: "mViewProjPrev" is not jittered (it doesn't acknowledge jitters from this or the previous frame), which is kinda fine for MB (probably good!).
	// LUMA FT: There seems to be a good amount of imprecision into "vPrevPos".
#if 0
	float3 vPrevPos = mul(float4(vPosWS, 1.0), mViewProjPrev).xyw;
#else // LUMA FT: cheaper (original) version
	float3 vPrevPos = mViewProjPrev[0].xyw * vPosWS.x + (mViewProjPrev[1].xyw * vPosWS.y + (mViewProjPrev[2].xyw * vPosWS.z + mViewProjPrev[3].xyw));
#endif
	vPrevPos.xy /= vPrevPos.z; // Previous pixel screen space position

	float2 vCurrPos = IN.baseTC.xy; // Note: don't use the scaled position here!
  
	float2 jitters = 0;
	bool motion_vectors_need_dejittering = LumaSettings.DLSS;
#if FORCE_MOTION_VECTORS_JITTERED
	motion_vectors_need_dejittering = true;
#endif
	// LUMA FT: offset the current's frame jitters from the dynamic objects motion vectors, otherwise the motion blur always includes the velocity of the jitters in every pixel.
	// The motion vectors generated from depth (above) aren't exactly "jittered" (even if "FORCE_MOTION_VECTORS_JITTERED" was true) but they are calculated on jittered values, without compensating for the jitter offsets (it'd be hard to do so, and it would cause extra blur),
	// the "dynamic objects" motion vectors (below) on the other hand, they are jittered (if "FORCE_MOTION_VECTORS_JITTERED" was true, and also partially if not).
	if (motion_vectors_need_dejittering)
	{
#if 1
		// Convert from NDC space to UV space
//TODOFT5: this isn't working properly, the Motion Blur MVs are still generated with random hitches of movement even if we pause the game (especially on black worm enemies) (this possibly happens in the vanilla game too!). Maybe we could add a threshold as big as the jitters (1px)? Possibly disable this if we sort "ReadVelocityObjects()"
//Actually, we fixed it now (cbuffer params were not aligned correctly). We can clean the code below
#if 1 
		jitters -= LumaData.CameraJitters.xy * float2(0.5, -0.5);
		jitters += LumaData.PreviousCameraJitters.xy * float2(0.5, -0.5);
#elif DEVELOPMENT
		jitters -= LumaData.CameraJitters.xy * float2(remap(LumaSettings.DevSetting01, 0.0, 1.0, -2.0, 2.0), remap(LumaSettings.DevSetting02, 0.0, 1.0, -2.0, 2.0)) * LumaSettings.DevSetting03;
		jitters += LumaData.PreviousCameraJitters.xy * float2(remap(LumaSettings.DevSetting01, 0.0, 1.0, -2.0, 2.0), remap(LumaSettings.DevSetting02, 0.0, 1.0, -2.0, 2.0)) * LumaSettings.DevSetting04;
#endif
#elif 0 // We can't have the previous jitters like this, incomplete solution //TODOFT4: should we always dejitter the vanilla motion vectors if DLSS is off (and "motion_vectors_need_dejittering" is off)? Probably?
		row_major float4x4 projectionMatrix = mul( CV_ViewProjMatr, CV_InvViewMatr ); // The current projection matrix used to be stored in "CV_PrevViewProjMatr" in vanilla Prey
 		jitters -= float2(projectionMatrix[0][2], projectionMatrix[1][2]) * float2(0.5, -0.5);
#endif
	}

	const float2 vVelocityObjs = _tex2_D3D11.Load(pixelCoord).xy; // LUMA FT: if this is zero it means there was no movement in dynamic objects
	bool noVelocityObj = vVelocityObjs.x == 0 && vVelocityObjs.y == 0; // LUMA FT: fixed the y axis not being checked (maybe it was intentional, but it seems bad)
	vCurrPos.xy = noVelocityObj ? vCurrPos.xy : 0;
	vPrevPos.xy = noVelocityObj ? vPrevPos.xy : (AdjustVelocityObjects(ReadVelocityObjects(vVelocityObjs)) + jitters);

	float2 vVelocity = (vPrevPos.xy - vCurrPos) * vMotionBlurParams.x;
#if !ENABLE_CAMERA_MOTION_BLUR
	if (noVelocityObj)
	{
		vVelocity = 0;
	}
#endif
	
	// Limit velocity
	const float MaxVelocityLen = noVelocityObj ? vMotionBlurParams.z : vMotionBlurParams.y;
#if 0 // LUMA FT: tried to re-write their velocity clamping code to make it more clear but I failed
	float vVelocityLenght = length(vVelocity.xy);
	float2 vNormalizedVelocity = normalize(vVelocity.xy);
	vVelocity = vNormalizedVelocity * min(vVelocityLenght, MaxVelocityLen);
#else
	const float invLen = rsqrt(dot(vVelocity.xy, vVelocity.xy) + 1e-6f); //TODOFT: why this approximation?
	vVelocity *= saturate(MaxVelocityLen * invLen);
#endif
	
	// Apply radial blur (around the edges of whatever dynamic center we have set)
#if _RT_SAMPLE0
	float2 vBlur = 0;
	vBlur = vRadBlurParam.xy - baseTC * vDirectionalBlur.zw; // LUMA FT: this should scale correctly for ultrawide too, effectively making the 16:9 edges on wider screens blur less (or wherever the blur center is)
	vBlur = vBlur * saturate(vRadBlurParam.w - length(vBlur) * vRadBlurParam.z) + vDirectionalBlur.xy;
	vVelocity += vBlur;
#endif
	
	OUT.Color.xy = EncodeMotionVector(vVelocity);
	OUT.Color.z = sqrt(length(vVelocity.xy) * 32.0f);
	OUT.Color.w = fDepth * CV_NearFarClipDist.y / 255.0f;

	return OUT; 
}