#include "include/Common.hlsl"

#define PREMULTIPLY_BENT_NORMALS 1
#define XE_GTAO_ENABLE_DENOISE ENABLE_SSAO_DENOISE
#define XE_GTAO_ENCODE_BENT_NORMALS 0
#include "include/XeGTAO.hlsl"

#include "include/CBuffer_PerViewGlobal.hlsl"

cbuffer PER_BATCH : register(b0)
{
  float4 SSAO_BlurKernel : packoffset(c0); // 2 / AOSizeX, 0, 2 / AOSizeY, 10 (weight coef)
  float4 BlurOffset : packoffset(c1); // 0.5 / depthSizeX, 0.5 / depthSizeY, 1 / AOSizeX, 1 / AOSizeY. Full size of the textures, ignoring the rendering resolution.
}

SamplerState ssSource : register(s0); // MIN_MAG_LINEAR_MIP_POINT CLAMP
SamplerState ssDepth : register(s1);  // MIN_MAG_MIP_POINT CLAMP
Texture2D<float4> sourceTex : register(t0); // AO (bent normals + obscurance)
Texture2D<float4> depthTex : register(t1); // Same as the "DirOccPass"
Texture2D<float4> depthHalfResTex : register(t2); // Same as the "DirOccPass" (unused here)
Texture2D<float> gtaoEdges : register(t3); // LUMA FT: added texture

float GetLinearDepth(float fLinearDepth)
{
    return fLinearDepth;
}

float4 GetTexture2D(Texture2D tex, SamplerState samplerState, float2 uv) { return tex.Sample(samplerState, uv); }

// LUMA FT: for SSDO, this function takes a 2x2 grid and blends them togther (given that each pixel used a different AO radious).
// For GTAO, it does a more advanced denoise mechanism.
// After this, there's another "filtering" pass where the diffuse color g-buffer is downscaled and blurred, to avoid the AO affecting bright colors (it's unclear why that would help) (it blurs a bit differently in ultrawide, but it's mostly fine).
void main(
  float4 v0 : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
#if !ENABLE_SSAO_DENOISE
#if 1
	outColor = sourceTex.Load(v0.rgb);
#else // Slower and lower quality
	outColor = GetTexture2D(sourceTex, ssSource, inBaseTC.xy);
#endif
	return;
#endif

	float2 normalizedTC = inBaseTC.xy / CV_HPosScale.xy;

#if SSAO_TYPE >= 1 // GTAO
	//TODO LUMA: plug in multiple denoise passes by doing ping pong on the two AO RTs we already have?
	//If changed, add code to set "lastDenoisePass", review "XE_GTAO_OCCLUSION_TERM_SCALE" and update this variable in the AO pixel shader.
	static const uint denoisePasses = 1;
	const bool lastDenoisePass = true;

    GTAOConstants consts;
	consts.ViewportPixelSize = CV_ScreenSize.zw * 2.0; // This is not set to "CV_HPosScale.xy" as in the AO shader because denoise needs it like this
	consts.DenoiseBlurBeta = (denoisePasses==0) ? 1e4f : 1.2f;
    outColor = XeGTAO_Denoise( v0.xy, consts, sourceTex, gtaoEdges, ssDepth, lastDenoisePass );
#if PREMULTIPLY_BENT_NORMALS // On the last denoising pass, do this as it's expected by Prey's code
	if (lastDenoisePass)
		outColor.xyz *= outColor.a;
#endif
    outColor.xyz = outColor.xyz * 0.5 + 0.5;

#else // SSDO

#if TEST_SSAO && 0
	if (normalizedTC.x > 0.5)
	{
		// LUMA: do XeGTAO denoise here if ever need to compare them
		return;
	}
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
	if (totalWeight > 0.01) //TODOFT: why this threshold?
		outColor = weight4.x * value0 + weight4.y * value1 + weight4.z * value2 + weight4.w * value3;
	
#endif // SSAO_TYPE >= 1
}