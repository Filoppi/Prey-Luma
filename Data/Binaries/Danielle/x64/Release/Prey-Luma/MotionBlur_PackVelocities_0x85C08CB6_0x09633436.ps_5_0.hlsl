#include "include/Common.hlsl"

#include "include/CBuffer_PerViewGlobal.hlsl"

#if _85C08CB6
#define _RT_SAMPLE0 1
cbuffer PER_BATCH : register(b0)
{
  row_major float4x4 mViewProjPrev : packoffset(c0);
  // Seemengly x 0 y 0 z aspect ratio and w 1
  float4 vDirectionalBlur : packoffset(c4);
  // See below
  float4 vMotionBlurParams : packoffset(c5);
  // See below
  float4 vRadBlurParam : packoffset(c6);
}
#else
cbuffer PER_BATCH : register(b0)
{
  row_major float4x4 mViewProjPrev : packoffset(c0);
  float4 vMotionBlurParams : packoffset(c4);
}
#endif

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
  float4 Color : COLOR0;
};

// LUMA FT: fixed motion vectors not being scaled properly with DRS
float2 AdjustVelocityObjects(float2 VelocityObjects)
{
	VelocityObjects /= LumaData.RenderResolutionScale;
	return VelocityObjects;
}

// PackVelocitiesPS
// LUMA FT: this runs per pixel, and outputs the uv velocities per pixel (on a limited RGBA UNORM8), it's not necessarily matching the motion vectors, as the blur can be fake and generated for any reason (e.g. radial blur).
// This is later downscaled to a 20x20 texture containing the max velocity of each patch.
// In the actal motion blur application shader, this is kinda applied in patches (e.g. 6, 14 or 24 patches, depending on the user MB quality setting), each has a maximum velocity from the 20x20 texture, but it's still per pixel!
// Note that this outputs on a 8bit UNORM texture, with or without Luma, hence it's low quality and possibly clipped, and could start to lose a lot of accuracy at high resolutions, due to uv offsets being so small.
// If we wanted, we could make this have a fullscreen resolution, resulting in per pixel motion blur, but given the rarity of moving objects in this game, it's not really necessary.
pixout main(vtxOut IN)
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

	// "vMotionBlurParams.x" is directly tied to the frame time "(1 / (delta time * shutter speed))"", this should theoretically make the MB look right at any frame rate (without making it weaker at higher FPS)
	vVelocity *= vMotionBlurParams.x;

#if !ENABLE_CAMERA_MOTION_BLUR
	if (noVelocityObj)
	{
		vVelocity = 0;
	}
#endif
	
	// Limit velocity
	const float MaxVelocityLen = noVelocityObj ? vMotionBlurParams.z : vMotionBlurParams.y; // "vMotionBlurParams.z" would be "0.05 * camera motion blur scale", the other 0.05 (at least sometimes), it's seemengly the inverse resolution of the soon to be made mip maps
	float vVelocityLenght = length(vVelocity.xy);
#if 0 // LUMA FT: tried to re-write their velocity clamping code to make it more clear but I failed
	float2 vNormalizedVelocity = normalize(vVelocity.xy);
	vVelocity = vNormalizedVelocity * min(vVelocityLenght, MaxVelocityLen);
#else
	const float invLen = 1.0 / vVelocityLenght; // LUMA FT: fixed approximation that added noise to velocity (we could give the division below a tiny bit more threshold if ever needed)
	vVelocity *= vVelocityLenght == 0.0 ? 1.0 : saturate(MaxVelocityLen * invLen);
#endif
	
	// Apply radial blur (around the edges of whatever dynamic center we have set) (this looks similar to depth of field)
#if _RT_SAMPLE0
	float2 vBlur = 0;
	// "vRadBlurParam.xy" are the center of the blur (where there is no blur), after scaling the uv by "vDirectionalBlur.zw"
	vBlur = vRadBlurParam.xy - baseTC * vDirectionalBlur.zw;
	// LUMA FT: fixed this not scaling properly for ultrawide, even if they scaled both "vRadBlurParam.x" and "vDirectionalBlur.z" to account for the aspect ratio,
	// they shouldn't have, because that implies blur is a lot stronger within the 16:9 part of the image at wider aspect ratios (and even stronger at the edges).
	// It might still not match with the look at 16:9 perfectly, maybe we'd need to scale it in FOV tan space, or maybe this isn't entirely correct, but it's a step in the right direction and good enough.
	float screenAspectRatio = (float)CV_ScreenSize.w / (float)CV_ScreenSize.z;
	vBlur.x *= NativeAspectRatio / screenAspectRatio;
	// LUMA FT: "vDirectionalBlur.xy" would have been adjusted by the resolution (and thus aspect ratio), so it should be good.
	// It's also unclear why they do saturate() on the blur length here, given it could randomly clamp results? it seems to be some kind of radial fade out.
	// "vRadBlurParam" z should be its radius*amount and w just the amount (their values are not influenced by aspect ratio, even if they should have been!).
	// This stuff seemengly scales correctly with 16:9 resolutions, it was just broken in ultrawide.
	vBlur = vBlur * saturate(vRadBlurParam.w - length(vBlur) * vRadBlurParam.z) + vDirectionalBlur.xy;
	vVelocity += vBlur;
#endif
	
	// LUMA FT: if we ever changed the RT of this PS to not be a UNORM8, we should also change the output encoding function, as it's tailored for 8bit UNORM.
	OUT.Color.xy = EncodeMotionVector(vVelocity);
	OUT.Color.z = sqrt(length(vVelocity.xy) * 32.0f);
    OUT.Color.w = fDepth * CV_NearFarClipDist.y / 255.0f;
	return OUT; 
}