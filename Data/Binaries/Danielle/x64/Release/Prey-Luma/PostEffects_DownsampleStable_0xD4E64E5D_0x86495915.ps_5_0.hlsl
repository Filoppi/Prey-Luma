#include "include/Common.hlsl"

#define _RT_SAMPLE0 1
#if _86495915 // Always the first pass on the full resolution source texture
#define _RT_SAMPLE1 1
#else
#define _RT_SAMPLE1 0
#endif
// There's more permutations but they might only ever be used if "CV_r_HDRBloomQuality" is <= 0, which isn't exposed to the game's official settings

// 0 Vanilla (Reinhard by luminance)
// 1 Perception based (by luminance) (the most temporally stable)
// 2 Perception based (by channel)
#define DOWNSAMPLE_TONEMAPPING_TYPE 0

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState _samp0_s : register(s0); // MIN_MAG_LINEAR_MIP_POINT (basically normal bilinear as far as we are concerned, nearest neighbor looks bad here for some reason, even if the offsets should always match (probably because we donwscale?))
Texture2D<float4> _tex0_D3D11 : register(t0);

float3 DownsampleStableTap(float2 baseTC, float2 offsets, float2 sampleUVClamp)
{
	float2 sourcePixelSize = CV_ScreenSize.zw; // This is half the texel size of the render target, which is halved compared to the source texture resolution, so it should match the source texture texel size
	// LUMA FT: added scaling by res scale to fix bloom looking more blurry if DRS was engaged, this also helps the exposure calculations stabilize independently of the rendering res (yes it's ok even if we don't sample texels center anymore!)
	float3 result = _tex0_D3D11.Sample(_samp0_s, clamp(baseTC + offsets * sourcePixelSize * CV_HPosScale.xy, 0.0, sampleUVClamp)).rgb;
#if _RT_SAMPLE1
	result = all(isfinite(result)) ? result : float3(0,0,0);
#endif
	return result;
}

void TM(inout float3 c)
{
#if DOWNSAMPLE_TONEMAPPING_TYPE <= 0
	c /= 1 + GetLuminance(c);
#elif DOWNSAMPLE_TONEMAPPING_TYPE == 1
	c = sqrt(abs(c)) * sign(c);
#else
	c *= sqrt(GetLuminance(c)) / GetLuminance(c);
#endif
}

void ITM(inout float3 c)
{
#if DOWNSAMPLE_TONEMAPPING_TYPE <= 0
	// LUMA FT: luminance can't be beyond 1 so the hue won't flip, it also shouldn't be able to ever reach 1 anyway
	c /= 1 - GetLuminance(c);
#elif DOWNSAMPLE_TONEMAPPING_TYPE == 1
	c = c * c * sign(c);
#else
	c *= sqr(GetLuminance(c)) / GetLuminance(c);
#endif
}

// LUMA FT: used to downscale the HDR linear buffer before bloom and other post process, including exposure, for bloom. It runs twice. This is not the buffer re-used in the following frame for SSR.
void main(
  float4 WPos : SV_Position0,
  float2 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
	// High quality downsampling filter to reduce bloom flickering
	// Filter combines five 4x4 blocks (sampled bilinearly)
	// Reduces fireflies by applying tonemapping (simple Reinhard by luminance) before averaging samples for each block (we weight each sample by its own luminance)
	
// LUAM FT: Disabled "killing fireflies" in the first pass, as it seemengly was a mechanism to combat TAA induced shimmers from jitters, more than actual suppressing stuff like fireflies and particles sparks from generating too much bloom.
// This, united with the new dejittering below was causing temporally unstable bloom, while disabling this and simply enabling dejittering makes it totally stable (though a bit stronger sometimes).
// This doesn't seem to be useful or do anything in later passes, but we left it in in case we had missed a case where it helps.
#if _RT_SAMPLE0
#if _RT_SAMPLE1 && REJITTER_BLOOM
	const bool bKillFireflies = false;
#else // !_RT_SAMPLE1 || !REJITTER_BLOOM
	const bool bKillFireflies = true;
#endif // _RT_SAMPLE1 && REJITTER_BLOOM
#else
	const bool bKillFireflies = false;
#endif // _RT_SAMPLE0

// LUMA FT: added bloom and exposure texture dejittering. There's not much need to "dejitter" these samples given that they are extremely low resolution and end up applying a blur filter anyway, though this ends up
// helping produce a temporally stable image, tough it can occasionally be brighter (e.g. if there's light coming out of a grid, skipping tonemapping will make bloom stronger, and more flicker prone, if camera jitters match a Moire pattern).
// This can produce more flickery (or well, temporally unstable) results in case of a moire pattern, given that we wouldn't sample the center of texels anymore, and the bilinear sampling would pre-lerp some colors,
// making the difference between bright and dark pixels smaller, but also different frame by frame.
// This is basically a quick way to resolve TAA, at the cost of some blurryness.
// To avoid this increasing bloom on top of patterns, we could also sample all texels with nearest neightbor and then check the min and max brightness of them and reduce the final brightness a bit if there's a lot of variation between them.
#if _RT_SAMPLE1 && REJITTER_BLOOM
	float2 scaledJitters = LumaData.CameraJitters.xy * float2(0.5, -0.5) * CV_HPosScale.xy;
#if 0 // Snap to closest texel center (doesn't make sense as we are always within a half texel range, even with jitters)
	if (abs(scaledJitters.x) > CV_ScreenSize.z * 0.5)
	{
		scaledJitters.x = CV_ScreenSize.z * sign(scaledJitters.x);
	}
	else
	{
		scaledJitters.x = 0;
	}
	if (abs(scaledJitters.y) > CV_ScreenSize.w * 0.5)
	{
		scaledJitters.y = CV_ScreenSize.w * sign(scaledJitters.y);
	}
	else
	{
		scaledJitters.y = 0;
	}
#endif
	inBaseTC.xy -= scaledJitters;
#endif

// LUMA FT: attmpt to change weights. This could help avoids the bloom occasionally getting stronger if we dejitter it, though for now it's not good enough (barely does anything) so it's disabled.
#if _RT_SAMPLE1 && REJITTER_BLOOM && 0
	// These can darken the whole image so we don't wanna go too high, even if it'd further help aligning highlights intensity.
	// The current values are random, ideally we'd make the center have more weight, not the opposite.
	static const float centerWeight = 0.5;
	static const float closeEdgesWeight = 1.0;
	static const float midEdgesWeight = 1.5;
	static const float farEdgesWeight = 2.0;
#else // 100% neutral and vanilla
	static const float centerWeight = 1.0;
	static const float closeEdgesWeight = 1.0;
	static const float midEdgesWeight = 1.0;
	static const float farEdgesWeight = 1.0;
#endif

	// LUMA FT: fixed usage of "CV_HPosClamp" being wrong, it was based on the output resolution
	float2 inputResolution;
	_tex0_D3D11.GetDimensions(inputResolution.x, inputResolution.y); // We can't use "CV_ScreenSize" here as that's for the output resolution
	float2 sampleUVClamp = CV_HPosScale.xy - (0.5 / inputResolution);
	
	// 2 pixels away
	float3 blockTL = 0, blockTR = 0, blockBR = 0, blockBL = 0;
	float3 tex;
	
	// LUMA FT: Alternative weights (possibly unused)
	float3 block_All = 0;
	float all_min = FLT_MAX;
	float all_max = 0;
	
	tex = DownsampleStableTap(inBaseTC.xy, float2(-2, -2), sampleUVClamp);
	all_min = min(all_min, GetLuminance(tex));
	all_max = max(all_max, GetLuminance(tex));
	block_All += tex;
	if (bKillFireflies) TM(tex);
	blockTL += tex * farEdgesWeight;
	
	tex = DownsampleStableTap(inBaseTC.xy, float2( 0, -2), sampleUVClamp);
	all_min = min(all_min, GetLuminance(tex));
	all_max = max(all_max, GetLuminance(tex));
	block_All += tex * 2;
	if (bKillFireflies) TM(tex);
	blockTL += tex * midEdgesWeight; blockTR += tex * midEdgesWeight;
	
	tex = DownsampleStableTap(inBaseTC.xy, float2( 2, -2), sampleUVClamp);
	all_min = min(all_min, GetLuminance(tex));
	all_max = max(all_max, GetLuminance(tex));
	block_All += tex;
	if (bKillFireflies) TM(tex);
	blockTR += tex * farEdgesWeight;
	
	tex = DownsampleStableTap(inBaseTC.xy, float2(-2,  0), sampleUVClamp);
	all_min = min(all_min, GetLuminance(tex));
	all_max = max(all_max, GetLuminance(tex));
	block_All += tex * 2;
	if (bKillFireflies) TM(tex);
	blockTL += tex * midEdgesWeight; blockBL += tex * midEdgesWeight;
	
	// Central sample (4+ times more important)
	tex = DownsampleStableTap(inBaseTC.xy, float2( 0,  0), sampleUVClamp);
	all_min = min(all_min, GetLuminance(tex));
	all_max = max(all_max, GetLuminance(tex));
	block_All += tex * 8;
	if (bKillFireflies) TM(tex);
	blockTL += tex * centerWeight; blockTR += tex * centerWeight; blockBR += tex * centerWeight; blockBL += tex * centerWeight;
	
	tex = DownsampleStableTap(inBaseTC.xy, float2( 2,  0), sampleUVClamp);
	all_min = min(all_min, GetLuminance(tex));
	all_max = max(all_max, GetLuminance(tex));
	block_All += tex * 2;
	if (bKillFireflies) TM(tex);
	blockTR += tex * midEdgesWeight; blockBR += tex * midEdgesWeight;
	
	tex = DownsampleStableTap(inBaseTC.xy, float2(-2,  2), sampleUVClamp);
	all_min = min(all_min, GetLuminance(tex));
	all_max = max(all_max, GetLuminance(tex));
	block_All += tex;
	if (bKillFireflies) TM(tex);
	blockBL += tex * farEdgesWeight;
	
	tex = DownsampleStableTap(inBaseTC.xy, float2( 0,  2), sampleUVClamp);
	all_min = min(all_min, GetLuminance(tex));
	all_max = max(all_max, GetLuminance(tex));
	block_All += tex * 2;
	if (bKillFireflies) TM(tex);
	blockBL += tex * midEdgesWeight; blockBR += tex * midEdgesWeight;
	
	tex = DownsampleStableTap(inBaseTC.xy, float2( 2,  2), sampleUVClamp);
	all_min = min(all_min, GetLuminance(tex));
	all_max = max(all_max, GetLuminance(tex));
	block_All += tex;
	if (bKillFireflies) TM(tex);
	blockBR += tex * farEdgesWeight;
	
	// 1 pixel away
	float3 blockCC = 0;

	tex = DownsampleStableTap(inBaseTC.xy, float2(-1, -1), sampleUVClamp);
	all_min = min(all_min, GetLuminance(tex));
	all_max = max(all_max, GetLuminance(tex));
	block_All += tex * 3;
	if (bKillFireflies) TM(tex);
	blockCC += tex;

	tex = DownsampleStableTap(inBaseTC.xy, float2( 1, -1), sampleUVClamp);
	all_min = min(all_min, GetLuminance(tex));
	all_max = max(all_max, GetLuminance(tex));
	block_All += tex * 3;
	if (bKillFireflies) TM(tex);
	blockCC += tex;

	tex = DownsampleStableTap(inBaseTC.xy, float2( 1,  1), sampleUVClamp);
	all_min = min(all_min, GetLuminance(tex));
	all_max = max(all_max, GetLuminance(tex));
	block_All += tex * 3;
	if (bKillFireflies) TM(tex);
	blockCC += tex;

	tex = DownsampleStableTap(inBaseTC.xy, float2(-1,  1), sampleUVClamp);
	all_min = min(all_min, GetLuminance(tex));
	all_max = max(all_max, GetLuminance(tex));
	block_All += tex * 3;
	if (bKillFireflies) TM(tex);
	blockCC += tex;

	block_All /= 32.0;
	
	static const float edgesWeight = centerWeight + midEdgesWeight * 2.0 + farEdgesWeight;
	blockTL /= edgesWeight; blockTR /= edgesWeight; blockBR /= edgesWeight; blockBL /= edgesWeight;
	blockCC /= 4;

	// LUMA FT: this is ambiguous, it might make HDR look nicer if disabled? It's especially useless in the second pass (nah, it's fine)
	if (bKillFireflies) 
	{
		// Convert back to uncompressed/linear range
		ITM(blockTL);
		ITM(blockTR);
		ITM(blockBR);
		ITM(blockBL);
		ITM(blockCC);
	}
	
	outColor.rgb = (0.5 * blockCC * closeEdgesWeight) + (0.125 * (blockTL + blockTR + blockBR + blockBL));
	outColor.rgb /= lerp(closeEdgesWeight, 1.0, 0.5); // Normalize weight
#if 0 // LUMA FT: looks a bit different but not necessarily better
	outColor.rgb = block_All;
#endif
	outColor.a = 0;
}