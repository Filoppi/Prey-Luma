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

	const float fDepth = GetLinearDepth(_tex0_D3D11, pixelCoord).x; // LUMA FT: this is the current's frame linear depth
	const float3 vPosWS = ReconstructWorldPos(pixelCoord.xy, fDepth);

	// LUMA FT: "mViewProjPrev" is not jittered (it doesn't acknowledge jitters from this or the previous frame, but it seem to acknowledge the current ones), which is kinda fine for MB (probably good!).
	// LUMA FT: There seems to be a good amount of imprecision into "vPrevPos".
#if 0
	float3 vPrevPos = mul(float4(vPosWS, 1.0), mViewProjPrev).xyw;
#else // LUMA FT: cheaper (original) CryEngine version
	float3 vPrevPos = mViewProjPrev[0].xyw * vPosWS.x + (mViewProjPrev[1].xyw * vPosWS.y + (mViewProjPrev[2].xyw * vPosWS.z + mViewProjPrev[3].xyw));
#endif
	vPrevPos.xy /= vPrevPos.z; // Previous pixel screen space position

	float2 vCurrPos = IN.baseTC.xy; // Note: don't use the scaled position here!
  	
	const float2 vVelocityObjs = _tex2_D3D11.Load(pixelCoord).xy; // LUMA FT: if this is zero it means there was no movement in dynamic objects
	bool noVelocityObj = vVelocityObjs.x == 0 && vVelocityObjs.y == 0; // LUMA FT: fixed the y axis not being checked (maybe it was intentional, but it seems bad)

	bool MVsNeedDejittering = LumaSettings.DLSS;
#if FORCE_MOTION_VECTORS_JITTERED // This seems to look a tiny bit better in MB
	MVsNeedDejittering = true;
#endif
	// LUMA FT: offset the current's frame jitters from the dynamic objects motion vectors, otherwise the motion blur always includes the velocity of the jitters in every pixel.
	// The motion vectors generated from depth (above) aren't exactly "jittered" (even if "FORCE_MOTION_VECTORS_JITTERED" was true) but they are calculated on jittered values, without compensating for the jitter offsets (it'd be hard to do so, and it would cause extra blur),
	// the "dynamic objects" motion vectors (below) on the other hand, they are jittered (if "FORCE_MOTION_VECTORS_JITTERED" was true, and also partially if not, but it seems stable (jitterless) enough in the false case).
	// 
	// Note that Dynamic Objects MVs are still generated with random hitches of movement even if we pause the game (especially on black worm enemies) (this possibly happens in the vanilla game too!), we gave up on fixing it to a 100%.
	float2 jitters = 0;
	if (!noVelocityObj && MVsNeedDejittering)
	{
        jitters -= LumaData.CameraJitters.xy;
        jitters += LumaData.PreviousCameraJitters.xy;
	}
	// This helps on camera/depth generated MVs, and possibly also helps the dynamic objects MVs (it doesn't seem to do much, but it doesn't hurt them)
	else
	{
        jitters -= LumaData.CameraJitters.xy;
	}
	// Convert from NDC space to UV space (y is flipped)
	jitters *= float2(0.5, -0.5);

	vCurrPos.xy = noVelocityObj ? vCurrPos.xy : 0;
	vPrevPos.xy = noVelocityObj ? vPrevPos.xy : AdjustVelocityObjects(ReadVelocityObjects(vVelocityObjs));

	float2 vVelocity = (vPrevPos.xy - vCurrPos.xy) + jitters;
	
// LUMA FT: Added hack to avoid velocities below the sub texel (1px) jittering from generating MV, given both the camera matrices and MVs low quality buffers and jitters cause imprecisions, it's good to clip the noise.
// This won't really work at high frame rates, but nothing should move this little within a frame, so in general it should be a positive.
// If ever necessary, we could try to adjust this threshold by frame rate, or split the x and y axes, or do it by jitter lenght.
#if 1
	if (abs(vVelocity.x) <= (1.0 / CV_ScreenSize.x) && abs(vVelocity.y) <= (1.0 / CV_ScreenSize.y))
	{
		vVelocity = 0;
	}
#elif 0 // This doesn't seem to work (it does in some frames but flickers)
	if (length(vVelocity) <= length(jitters))
	{
		vVelocity = 0;
	}
#endif

	vVelocity *= vMotionBlurParams.x;

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