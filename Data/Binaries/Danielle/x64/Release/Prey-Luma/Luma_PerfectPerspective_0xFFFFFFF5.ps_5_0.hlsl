#include "include/Common.hlsl"
#include "include/LensDistortion.hlsl"

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState sourceTextureSampler : register(s10); // Anisotropic + Black Border/Edges
Texture2D<float4> sourceTexture : register(t0);

// Runs in place of "PostAAComposites_PS"
void main(float4 WPos : SV_Position0, float4 inBaseTC : TEXCOORD0, out float4 outColor : SV_Target0)
{
	float2 outputResolution = 0.5 / CV_ScreenSize.zw; // Using "CV_ScreenSize.xy" directly would probably also be fine given this is always meant to be done after upscaling
	float FOVX = 1.0 / CV_ProjRatio.z;
	float borderAlpha = 0.f;
	// Note that we don't acknowledge any "POST_PROCESS_SPACE_TYPE" here, we treat it as if it was in linear for best performance
	float2 distortedTC = PerfectPerspectiveLensDistortion(inBaseTC.xy, FOVX, outputResolution, borderAlpha);

	// Scale the UV coordinates with DRS
	bool drs = any(CV_HPosScale.xy != 1.0);
	float2 preScaleDistortedTC = distortedTC;
	distortedTC *= CV_HPosScale.xy;
	float2 preClampDistortedTC = distortedTC;
	if (drs) // Rely on borders color if we don't use resolution scaling, it gives better quality
	{
		preScaleDistortedTC = min(distortedTC, 1.0); // Clamp this to an approximate value too for consistency
		distortedTC = min(distortedTC, CV_HPosClamp.xy);

		float2 tcDiff = preClampDistortedTC - distortedTC;
		// Give a 1 texel tolerance before fully going to black
		float2 texelSize = CV_ScreenSize.zw * 2.0;
		borderAlpha = saturate(max(tcDiff.x / texelSize.x, tcDiff.y / CV_ScreenSize.y));
	}

#if ENABLE_SCREEN_DISTORTION && 1 // Use mips
	// perspective projection lookup with mip-mapping and anisotropic filtering (and black edges)
	// It's unclear whether we should use "preScaleDistortedTC" or "distortedTC" in the ddx/ddy, but probably we want to factor the DRS in!
    outColor = sourceTexture.SampleGrad(sourceTextureSampler, distortedTC, ddx(distortedTC), ddy(distortedTC));
#elif ENABLE_SCREEN_DISTORTION // No mips
    outColor = sourceTexture.Sample(sourceTextureSampler, distortedTC);
#else // Disabled
    outColor = sourceTexture.Load(WPos.xyz);
	borderAlpha = 0;
#endif
//TODOFT: could this benefit from running FSR 1 for upscaling/sharpening after? Probably not!
//TODOFT: why does this look awful if DLSS is off?

   	outColor.rgb = lerp(outColor.rgb, 0, borderAlpha); // Blend towards black as the border is black (we could ignore this if "CroppingFactor" was 1 but it doesn't matter)
	outColor.a = 1.0 - borderAlpha; // Take advantage of the alpha texture to store whether this is a black texel (outside of the new lens distortion edges) (doesn't support R11G11B10F)
}