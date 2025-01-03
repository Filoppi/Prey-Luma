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
		borderAlpha = saturate(max(tcDiff.x / texelSize.x, tcDiff.y / texelSize.y));
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

	//TODO LUMA: could this benefit from running FSR 1 for upscaling/sharpening after? Not particularly, it seems fine with the settings we use, but in case... here's some open source implementations:
	// https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/ffx-fsr/ffx_fsr1.h
	// https://github.com/cdozdil/OptiScaler/blob/master/OptiScaler/shaders/fsr1/ffx_fsr1.h
	// https://github.com/TreyM/SHADERDECK/blob/main/shaders/SHADERDECK/FSR1_2X.fx
	// https://github.com/40163650/FSRForReShade
	// https://github.com/Blinue/Magpie/blob/dev/src/Effects/FSR/FSR_EASU.hlsl
	//TODO LUMA: Psi effects, when charing them (e.g. holding right mouse button) will create a screen space ring like transparent particle effect, this is already looking decent with ultrawide aspect ratios,
	//though with lens distortion it'd cropped a bit more than it should, it's not really a problem though, so we haven't taken care of it (the particle effect is drawn after g-buffers compositions as all transparency draws, and probably uses specific vertex shader (materials) that we could uniquely replace).

   	outColor.rgb = lerp(outColor.rgb, 0, borderAlpha); // Blend towards black as the border is black (we could ignore this if "CroppingFactor" was 1 but it doesn't matter)
	outColor.a = 1.0 - borderAlpha; // Take advantage of the alpha texture to store whether this is a black texel (outside of the new lens distortion edges) (doesn't support R11G11B10F)
}