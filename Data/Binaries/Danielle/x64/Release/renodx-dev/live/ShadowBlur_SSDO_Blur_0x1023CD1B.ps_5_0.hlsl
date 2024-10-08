#include "include/Common.hlsl"

cbuffer PER_BATCH : register(b0)
{
  float4 SSAO_BlurKernel : packoffset(c0);
  float4 BlurOffset : packoffset(c1);
}

SamplerState ssSource : register(s0);
SamplerState ssDepth : register(s1);
Texture2D<float4> sourceTex : register(t0);
Texture2D<float4> depthTex : register(t1);

float GetLinearDepth(float fDevDepth)
{
	return fDevDepth;
}

float4 GetTexture2D(Texture2D tex, SamplerState samplerState, float2 uv) { return tex.Sample(samplerState, uv); }

void main(
  float4 v0 : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
#if !ENABLE_SSAO_DENOISE || SSAO_TYPE >= 1
	outColor = GetTexture2D(sourceTex, ssSource, inBaseTC.zw * BlurOffset.zw);
	return;
#endif

	float4 depth4;
  
	// In order to get four bilinear-filtered samples(16 samples effectively)
  // +-+-+-+-+
  // +-0-+-1-+
  // +-+-+-+-+
  // +-2-+-3-+
  // +-+-+-+-+
	float2 addr0 = floor(inBaseTC.zw) * BlurOffset.zw;
	float2 addr1 = addr0 + SSAO_BlurKernel.xy;
	float2 addr2 = addr0 + SSAO_BlurKernel.yz;
	float2 addr3 = addr2 + SSAO_BlurKernel.xy;

	float4 value0 = GetTexture2D(sourceTex, ssSource, addr0);
	float4 value1 = GetTexture2D(sourceTex, ssSource, addr1);
	float4 value2 = GetTexture2D(sourceTex, ssSource, addr2);
	float4 value3 = GetTexture2D(sourceTex, ssSource, addr3);

	// Sample depth values
	const float4 vDepthAddrOffset = float4(1.0, 1.0, -1.0, -1.0) * BlurOffset.xyxy;
	depth4.x = GetTexture2D(depthTex, ssDepth, addr0 + vDepthAddrOffset.zw).x;
	depth4.y = GetTexture2D(depthTex, ssDepth, addr1 + vDepthAddrOffset.xw).x;
	depth4.z = GetTexture2D(depthTex, ssDepth, addr2 + vDepthAddrOffset.zy).x;
	depth4.w = GetTexture2D(depthTex, ssDepth, addr3 + vDepthAddrOffset.xy).x;

	float centerDepth = GetLinearDepth(GetTexture2D(depthTex, ssDepth, inBaseTC.xy).x);
	float4 weight4 = saturate(1.0 - 35.0 * abs(depth4 / centerDepth - 1.0));

	float totalWeight = dot(weight4, 1.0);
	weight4 /= totalWeight;

	outColor = (value0 + value1 + value2 + value3) * 0.25;
	if (totalWeight > 0.01)
		outColor = weight4.x * value0 + weight4.y * value1 + weight4.z * value2 + weight4.w * value3;

	return;
}