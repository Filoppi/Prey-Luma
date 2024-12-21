#include "include/Common.hlsl"

#define _RT_SAMPLE0 1
#if _86495915
#define _RT_SAMPLE1 1
#else
#define _RT_SAMPLE1 0
#endif

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState _samp0_s : register(s0);
Texture2D<float4> _tex0_D3D11 : register(t0);

float3 DownsampleStableTap(float2 baseTC, float2 offsets, float2 sampleUVClamp)
{
	float2 sourcePixelSize = CV_ScreenSize.zw;
	// LUMA FT: there's no need to "dejitter" these samples given that they are extremely low resolution and end up blurrying the image anyway, though it might help if done on the first pass
	float3 result = _tex0_D3D11.Sample(_samp0_s, clamp(baseTC + offsets * sourcePixelSize * CV_HPosScale.xy, 0.0, sampleUVClamp)).rgb; // LUMA FT: added scaling by res scale to fix bloom looking more blurry if DRS was engaged, this also helps the exposure calculations stabilize independently of the rendering res
#if _RT_SAMPLE1
	result = all(isfinite(result)) ? result : float3(0,0,0);
#endif
	return result;
}

// LUMA FT: used to downscale the HDR linear buffer before bloom and other post process, including exposure, for bloom. This is not the buffer re-used in the following frame for SSR.
void main(
  float4 WPos : SV_Position0,
  float2 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
	// High quality downsampling filter to reduce bloom flickering
	// Filter combines five 4x4 blocks (sampled bilinearly)
	// Reduces fireflies by applying tonemapping before averaging samples for each block
	
#if _RT_SAMPLE0	
	const bool bKillFireflies = true;
#else
	const bool bKillFireflies = false;
#endif

	// LUMA FT: fixed usage of "CV_HPosClamp" being wrong, it was based on the output resolution
	float2 inputResolution;
	_tex0_D3D11.GetDimensions(inputResolution.x, inputResolution.y); // We can't use "CV_ScreenSize" here as that's for the output resolution
	float2 sampleUVClamp = CV_HPosScale.xy - (0.5 / inputResolution);
	
	half3 blockTL = 0, blockTR = 0, blockBR = 0, blockBL = 0;
	half3 tex;
	
	tex = DownsampleStableTap(inBaseTC.xy, float2(-2, -2), sampleUVClamp);
	if (bKillFireflies) tex /= 1 + GetLuminance(tex);
	blockTL += tex;
	
	tex = DownsampleStableTap(inBaseTC.xy, float2( 0, -2), sampleUVClamp);
	if (bKillFireflies) tex /= 1 + GetLuminance(tex);
	blockTL += tex; blockTR += tex;
	
	tex = DownsampleStableTap(inBaseTC.xy, float2( 2, -2), sampleUVClamp);
	if (bKillFireflies) tex /= 1 + GetLuminance(tex);
	blockTR += tex;
	
	tex = DownsampleStableTap(inBaseTC.xy, float2(-2,  0), sampleUVClamp);
	if (bKillFireflies) tex /= 1 + GetLuminance(tex);
	blockTL += tex; blockBL += tex;
	
	tex = DownsampleStableTap(inBaseTC.xy, float2( 0,  0), sampleUVClamp);
	if (bKillFireflies) tex /= 1 + GetLuminance(tex);
	blockTL += tex; blockTR += tex; blockBR += tex; blockBL += tex;
	
	tex = DownsampleStableTap(inBaseTC.xy, float2( 2,  0), sampleUVClamp);
	if (bKillFireflies) tex /= 1 + GetLuminance(tex);
	blockTR += tex; blockBR += tex;
	
	tex = DownsampleStableTap(inBaseTC.xy, float2(-2,  2), sampleUVClamp);
	if (bKillFireflies) tex /= 1 + GetLuminance(tex);
	blockBL += tex;
	
	tex = DownsampleStableTap(inBaseTC.xy, float2( 0,  2), sampleUVClamp);
	if (bKillFireflies) tex /= 1 + GetLuminance(tex);
	blockBL += tex; blockBR += tex;
	
	tex = DownsampleStableTap(inBaseTC.xy, float2( 2,  2), sampleUVClamp);
	if (bKillFireflies) tex /= 1 + GetLuminance(tex);
	blockBR += tex;
	
	half3 blockCC = 0;
	tex = DownsampleStableTap(inBaseTC.xy, float2(-1, -1), sampleUVClamp);
	if (bKillFireflies) tex /= 1 + GetLuminance(tex);
	blockCC += tex;
	tex = DownsampleStableTap(inBaseTC.xy, float2( 1, -1), sampleUVClamp);
	if (bKillFireflies) tex /= 1 + GetLuminance(tex);
	blockCC += tex;
	tex = DownsampleStableTap(inBaseTC.xy, float2( 1,  1), sampleUVClamp);
	if (bKillFireflies) tex /= 1 + GetLuminance(tex);
	blockCC += tex;
	tex = DownsampleStableTap(inBaseTC.xy, float2(-1,  1), sampleUVClamp);
	if (bKillFireflies) tex /= 1 + GetLuminance(tex);
	blockCC += tex;
	
	blockTL /= 4; blockTR /= 4; blockBR /= 4; blockBL /= 4; blockCC /= 4;
	
	// LUMA FT: this is ambiguous, it might make HDR look nicer if disabled?
	if (bKillFireflies) 
	{
		// Convert back to uncompressed/linear range
		blockTL /= (1 - GetLuminance(blockTL));
		blockTR /= (1 - GetLuminance(blockTR));
		blockBR /= (1 - GetLuminance(blockBR));
		blockBL /= (1 - GetLuminance(blockBL));
		blockCC /= (1 - GetLuminance(blockCC));
	}
	
	outColor.rgb = 0.5 * blockCC + 0.125 * (blockTL + blockTR + blockBR + blockBL);
	outColor.a = 0;
}