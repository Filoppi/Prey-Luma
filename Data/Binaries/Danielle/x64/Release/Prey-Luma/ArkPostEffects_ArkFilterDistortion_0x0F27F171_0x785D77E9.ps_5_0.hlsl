#include "include/Common.hlsl"

#if _0F27F171
#define _RT_SAMPLE0 0
#define _RT_SAMPLE1 1
// This could also be true, it doesn't matter
#define _RT_SAMPLE2 0
#elif _785D77E9
#define _RT_SAMPLE0 0
#define _RT_SAMPLE1 0
#define _RT_SAMPLE2 1
#elif 0 // Missing possibly used valid permutation
#define _RT_SAMPLE0 1
#define _RT_SAMPLE1 0
#define _RT_SAMPLE2 0
#endif

cbuffer PER_BATCH : register(b0)
{
  float4 psParams[16] : packoffset(c0);
}

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState ssScreenTex_s : register(s0);
Texture2D<float4> screenTex : register(t0);

// ArkFilterDistortionPS
// Screen space distortion effect. This is mostly already corrected by the aspect ratio and supports ultrawide fine:
// in UW, the distortion is focused around the 16:9 part of the image and it plays out closely there. 
// This runs after AA and upscaling.
// Note that 3D world to 2D screen mapped icons are distorted with the same code as here, so further changing this distortion will shift their placement, breaking their alignment (which actually already is?),
// Luma could fix this by intercepting the triangles the UI tries to draw that are projected to the screen from world space, but it's not worth it.
void main(
  float4 inWPos : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
#if !ENABLE_SCREEN_DISTORTION
	outColor = screenTex.Sample(ssScreenTex_s, inBaseTC.xy);
	return;
#endif

	// global inputs
	const float fEdgeAmount = psParams[0].x;
	const float fEdgeLensAspect = psParams[0].y; // = ScreenAspect / EdgeAspect

	const float2 vFocusCenter = psParams[0].zw; // pre-transformed to -1,1 // LUMA FT: This determines the offset from the top left (???)

	float fBulgeAmount = psParams[1].x; // LUMA FT: This determines the intensity of the effect (how much it "zooms in")
	const float fBulgeRadius = psParams[1].y; // LUMA FT: This determines the radius of the distortion from the center of the screen. It's best left untouched
	const float fBulgeLensAspect = psParams[1].z; // = ScreenAspect / BulgeAspect // LUMA FT: This determines the strength of the distortion at the edges (corners)
	
	// LUMA FT: added "hacky" approximated heuristically found (on 32:9) distortion adjustment for UW, given it was a tiny bit too strong.
	// It should work for any aspect ratio as long as it's Hor+ (even if the scaling logic isn't perfect), but it's disabled below 16:9.
	float fScreenAspectRatio = CV_ScreenSize.w / CV_ScreenSize.z;
	float fBulgeReduction = lerp(1.0, max(fScreenAspectRatio / NativeAspectRatio, 1.0), 0.125);
	fBulgeAmount /= fBulgeReduction;

	// local inputs
	const float2 vScreenUV = inBaseTC.xy;

	const float2 vAlignedCurrent = vScreenUV * 2.0 - 1.0; // transform to -1,1
	const float2 vAlignedFocusCenter = vFocusCenter;
	const float2 vCurrentOffset = vAlignedCurrent - vAlignedFocusCenter;

	float2 vTotalOffset = float2(0.0,0.0);

	////////////////////////////////////////////////////////////////////////////////
	// Edge Pulling/Pushing
#if _RT_SAMPLE0
	{
		//TODOFT4: this permutations stretches a little bit too much in ultrawide? test whenever you get it happening (with gravity bombs? it actually doesn't seem to be used in Prey). Also test in general how does this shader react to non default FOVs

		// radius need to be 1 at screen side
		float2 vCurrentScaled = vCurrentOffset * 2.0;
		float2 vCurrentForEdge = float2(vCurrentScaled.x * fEdgeLensAspect, vCurrentScaled.y);

		float r2 = dot(vCurrentForEdge, vCurrentForEdge);
		float r1 = sqrt(r2);
		float r3 = r1 * r2;

		float fLensDistort = 0.15 * fEdgeAmount;
		float fEdgeScale = r3 * fLensDistort + (1.0 - fLensDistort);

		// edge offset from center of screen
		float2 vEdgeSamplePoint = vCurrentOffset * fEdgeScale;

		// center + sample - me
		// Edge offset from current sample
		float2 vEdgeOffset = vEdgeSamplePoint - vAlignedCurrent;

		vTotalOffset += vEdgeOffset;
	}
#endif

	////////////////////////////////////////////////////////////////////////////////
	// Bulging / Shrinkage
#if _RT_SAMPLE1 || _RT_SAMPLE2
	{
		float2 vCurrentForBulging = float2(vCurrentOffset.x * fBulgeLensAspect, vCurrentOffset.y);

		float r2 = dot(vCurrentForBulging, vCurrentForBulging);
		float r4 = r2 * r2;

		float fAmount = 0.25 * abs(fBulgeAmount);
		float fBaseScalar = r4 * rcp(fBulgeRadius*fBulgeRadius);
#if _RT_SAMPLE1
		float fOffsetScale = saturate( lerp(1.0, fBaseScalar, fAmount) );
#else
		float fOffsetScale = 1 + max( lerp(0.0, 1 - fBaseScalar, fAmount), 0 );
#endif

		float2 vSamplePoint = vCurrentOffset * fOffsetScale;

		float2 vBulgeOffset = vSamplePoint - vAlignedCurrent;
		vTotalOffset += vBulgeOffset;
	}
#endif

	////////////////////////////////////////////////////////////////////////////////
	// Composition
	float2 vSampleUV = vTotalOffset + vScreenUV;
	float4 cScene = screenTex.Sample(ssScreenTex_s, vSampleUV);

	outColor = cScene;
}