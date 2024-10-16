#include "include/Common.hlsl"

cbuffer PER_BATCH : register(b0)
{
	// LUMA FT:
	// vParams.xy: full target width, full target height (only the top left portion of the source texture is written and should be read). Equal to "CV_ScreenSize.xy / CV_HPosScale.xy".
	// vParams.zw: target to source res scaling. Equal to "CV_HPosScale.xy" (if DLSS is off). There are not altered by Luma even when using DLSS and upscaling before.
	float4 vParams : packoffset(c0);
}

#if ENABLE_DITHERING && DELAY_DITHERING
#include "include/CBuffer_PerViewGlobal.hlsl"
#endif

SamplerState _tex0_s : register(s0); // Bilinear
Texture2D<float4> _tex0 : register(t0);

// UpscaleImagePS
// LUMA FT: this only runs when upscaling the image from a lower resolution (dynamic resolution scaling DRS), and it runs after all AA and "PostAAComposites".
// It would be possible to directly replace this pass with DLSS SR, but we'd then be forced to run DLSS after AA (if not forced disabled), Film Grain, vignette, etc etc which isn't ideal.
//
// DRS works by drawing to a dynamically sized top left portion of all textures (almost all of them are drawn like that).
// To achieve that, CryEngine uses a smaller DX viewport (and custom vertices?), matching the rendering resolution area;
// so vertex shaders output a position (e.g. for 1920x1080 with DRS at 0.75x, it would be from 0x0 to 1279x719) and UV texture coordinates from 0 to 1 (with 1 mapping to the edge of the rendering resolution area, not the full resolution area).
// A total of (e.g.) 1280x720 texels will then draw/write on the target texture, with the pixel shader having UVs in the 0-1 range from the source color (TEXCOORD0), which will then need scaling to sample from any other texture, as they were also 
// only drawn in their top left part (most textures).
// In other words, MapViewportToRaster() was taking the UV and scaling it by "CV_HPosScale.xy",
// (yes, UVs can scaled like that between texture sizes, by just multiplying them).
//
// This uses "FullscreenTriVS".
void main(
  float4 WPos : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
	//TODO LUMA: replace this with FSR 1 (a simpler single pass version?) if ever needed?
	//TODO LUMA: we could completely skip this pass from c++ if DLSS was running and simply copy the texture into the target directly

	if (!LumaSettings.DLSS) // LUMA FT: DLSS already uspcaled the image, don't upscale it again
	{
		float2 texCoords = inBaseTC.xy * vParams.xy + 0.5;
		float2 intPart = floor(texCoords);
		float2 f = texCoords - intPart;
		
		// Apply smoothstep function to get a mixture between nearest neighbor and linear filtering
		f = f*f * (3 - 2*f);

		texCoords = intPart + f;
		texCoords = (texCoords - 0.5) / vParams.xy;
		texCoords = saturate(texCoords * vParams.zw);
	
		outColor = _tex0.Sample(_tex0_s, texCoords);
	}
	else
	{
#if 1 // These are equivalent, but "Load()" is faster and more accurate
		outColor = _tex0.Load(uint3(WPos.xy, 0));
#else
		outColor = _tex0.Sample(_tex0_s, inBaseTC.xy);
#endif
	}
	
#if ENABLE_DITHERING && DELAY_DITHERING // LUMA FT: do dithering here in the case of upscaling, so it's done per pixel
	bool gammaSpace = bool(POST_PROCESS_SPACE_TYPE <= 0) || bool(POST_PROCESS_SPACE_TYPE >= 2);
	float paperWhite = GamePaperWhiteNits / sRGB_WhiteLevelNits;
	ApplyDithering(outColor.rgb, inBaseTC.xy, gammaSpace, gammaSpace ? 1.0 : paperWhite, DITHERING_BIT_DEPTH, CV_AnimGenParams.z, true);
#endif // ENABLE_DITHERING && DELAY_DITHERING

	return;
}