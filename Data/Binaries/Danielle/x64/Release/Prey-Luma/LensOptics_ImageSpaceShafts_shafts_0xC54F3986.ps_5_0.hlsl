cbuffer PER_BATCH : register(b0)
{
  float4 HDRParams : packoffset(c0);
}

#include "include/LensOptics.hlsl"

SamplerState occMap_s : register(s1);
Texture2D<float4> occMap : register(t1);

struct OutputShaftVS // LUMA FT: fixed "OutputShafeVS" typo
{
	float4 hpos:POSITION;
	float2 uv:TEXCOORD0;
	float4 center:TEXCOORD1;
	float4 color:COLOR0;
};

// shaftsPS
// This draws on a square around the sun, based on the occlusion+color buffer (shaftsOccPS).
// This also writes to an "SDR" buffer (though this time R16G16B16A16_UNORM for some reason), even with the LUMA mod (I think, but not 100% sure, it doesn't really matter as it doesn't need values beyond 1).
// This doesn't run at full resolution, but at 1/2.5 (of the output resolution, this isn't scaled with DRS) (might vary depending on the base resolution? there might be some mip map pow 2 rounding?).
void main(OutputShaftVS IN, out float4 outColor : SV_Target0)
{
#if 0 // LUMA FT: quick test to visualize the occlusion texture
	outColor = float4( occMap.Sample(occMap_s, IN.center.zw).rgb, 1 );
	return;
#endif

// LUMA FT: 30 already looks good, anything more is "extra" (also the base texture is pretty low quality, so it's not gonna help too much)
#if SUNSHAFTS_QUALITY <= 0
	const int N_SAMPLE = 30;
#elif SUNSHAFTS_QUALITY == 1
	const int N_SAMPLE = 40;
#elif SUNSHAFTS_QUALITY == 2
	const int N_SAMPLE = 50;
#else // SUNSHAFTS_QUALITY >= 3
	const int N_SAMPLE = 60;
#endif
	float2 duv = (IN.center.zw - IN.center.xy) / N_SAMPLE;
	float decayFactor = pow( 0.98, 30.0 / N_SAMPLE ); // LUMA FT: the numerator is meant to be 30 independently of "N_SAMPLE"
	float4 color = float4( occMap.Sample(occMap_s, IN.center.zw).rgb, 1 ) * decayFactor; // LUMA FT: this texture is actually black and white (all channels are the same) and SDR (UNORM)
	if (any(occMap.Sample(occMap_s, IN.center.zw).rgb < -0.01) )
	{
		outColor = 100;
		return;
	}
	#define decay (color.w)
	float2 cuv = IN.center.zw;
	[unroll]
	for( int i=0; i<N_SAMPLE; i++ )
	{
		cuv -= duv;
		color.rgb += occMap.Sample(occMap_s, cuv).rgb * decay; //TODO LUMA: this doesn't seem to need to be scaled by "CV_HPosScale.xy" (or maybe it was already, in that case, we should also saturate the uv to avoid reading texels out of bounds)
		decay *= decayFactor;
	}
	const float ssFalloff = decayFactor * 0.01;  // falloff curve base factor
	float2 ndcUV = IN.uv.xy*2-1;
	float vig = saturate( 1-pow( dot(ndcUV,ndcUV), 0.25) ); // vignette?
	outColor = ToneMappedPreMulAlpha(float4(color.rgb*IN.color.rgb*vig/N_SAMPLE, IN.color.a), false);
	return;
	#undef decay
}