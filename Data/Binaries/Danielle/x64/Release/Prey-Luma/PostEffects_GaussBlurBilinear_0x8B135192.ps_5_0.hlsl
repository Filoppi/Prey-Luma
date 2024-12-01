cbuffer PER_BATCH : register(b0)
{
  float4 clampTC : packoffset(c0);
  float4 psWeights[16] : packoffset(c1);
}

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState _tex0_s : register(s0);
Texture2D<float4> _tex0 : register(t0);

// This blurs the image. It's run after upscaling (and usually after TexToTexSampledPS), so it supports DLSS fine and doesn't need any "CV_HPosScale"/"MapViewportToRaster()" adjustments for that case.
// It seems like it generally handles different aspect ratios correctly.
void main(
  float4 HPosition : SV_Position0,
  float4 tc0 : TEXCOORD0,
  float4 tc1 : TEXCOORD1,
  float4 tc2 : TEXCOORD2,
  float4 tc3 : TEXCOORD3,
  float4 tc4 : TEXCOORD4,
  out float4 outColor : SV_Target0)
{
  float4 sum = 0;
  
  // Perform downscaling clamp post-interpolation

#if 1 // LUMA FT: fixed missing UV clamps
  float2 sampleUVClamp = CV_HPosClamp.xy;
#elif 1
  float2 sampleUVClamp = CV_HPosScale.xy - (CV_ScreenSize.zw * 2.0); // The "CV_ScreenSize" seems to be updated with the current render target size (testing showed the source and target textures match in size, at least in most cases)
#else
  float2 outputResolution;
  _tex0.GetDimensions(outputResolution.x, outputResolution.y); // For 100% accurate results ("CV_ScreenSize" might be based on the render target size, which might not be the same as the source texture size?)
  float2 sampleUVClamp = CV_HPosScale.xy - (0.5 / outputResolution);
#endif

	float4 col = _tex0.Sample(_tex0_s, min(tc0.xy, sampleUVClamp));
	sum += col * psWeights[0].x;  

	col = _tex0.Sample(_tex0_s, min(tc0.zw, sampleUVClamp));
	sum += col * psWeights[1].x;  
	
  col = _tex0.Sample(_tex0_s, min(tc1.xy, sampleUVClamp));
	sum += col * psWeights[2].x;  

	col = _tex0.Sample(_tex0_s, min(tc1.zw, sampleUVClamp));
	sum += col * psWeights[3].x;

	col = _tex0.Sample(_tex0_s, min(tc2.xy, sampleUVClamp));
	sum += col * psWeights[4].x;  
	
	col = _tex0.Sample(_tex0_s, min(tc2.zw, sampleUVClamp));
	sum += col * psWeights[5].x;  
	
	col = _tex0.Sample(_tex0_s, min(tc3.xy, sampleUVClamp));
	sum += col * psWeights[6].x;  
	
	col = _tex0.Sample(_tex0_s, min(tc3.zw, sampleUVClamp));
	sum += col * psWeights[7].x;

  // LUMA FT: this seems to already be acknowledging the aspect ratio (in the vertex shader) and thus it blurs equally (in screen space) for ultrawide or 16:9 or any aspect ratio. This is used in menus backgrounds and other scene texture operations in Prey anyways
  // LUMA FT: note that this can cause invalid luminances in the downscaled image
  outColor = sum;
}