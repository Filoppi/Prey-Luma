cbuffer PER_BATCH : register(b0)
{
  float4 LayerBlendAmount : packoffset(c0);
}

SamplerState layer0Sampler_s : register(s0);
Texture2D<float4> layer0Sampler : register(t0);
SamplerState layer1Sampler_s : register(s1);
Texture2D<float4> layer1Sampler : register(t1);
SamplerState layer2Sampler_s : register(s2);
Texture2D<float4> layer2Sampler : register(t2);
SamplerState layer3Sampler_s : register(s3);
Texture2D<float4> layer3Sampler : register(t3);

struct a2v_cch
{
  float4 Position  : POSITION0;
  float2 baseTC   : TEXCOORD0; 
  float4 Color     : COLOR0;
};

struct v2f_cch
{
	float4 HPosition : SV_Position0;	
	float2 baseTC : TEXCOORD0;
	float3 Color : TEXCOORD1;
};

float4 MergeColorChartsPS(v2f_cch IN)
{
	//TODO LUMA: if this is actually used beyond 1 LUT at a time,
	//blend them in linear space so they don't result in hue shifts (linearize with sRGB gamma)?

	float3 col = layer0Sampler.Sample(layer0Sampler_s, IN.baseTC.xy).rgb * LayerBlendAmount.x;
	
	#if _RT_SAMPLE1 || _RT_SAMPLE0
		col += layer1Sampler.Sample(layer1Sampler_s, IN.baseTC.xy).rgb * LayerBlendAmount.y;
	#endif
	
	#if _RT_SAMPLE1
		col += layer2Sampler.Sample(layer2Sampler_s, IN.baseTC.xy).rgb * LayerBlendAmount.z;
	#endif
	
	#if _RT_SAMPLE1 && _RT_SAMPLE0
		col += layer3Sampler.Sample(layer3Sampler_s, IN.baseTC.xy).rgb * LayerBlendAmount.w;
	#endif
 
	return float4(col, 1);
}