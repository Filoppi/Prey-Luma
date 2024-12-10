#include "include/Common.hlsl"

#define _RT_SAMPLE0 0
#define _RT_SAMPLE1 0
#define _RT_SAMPLE2 0
#define _RT_SAMPLE3 0
#define _RT_SAMPLE4 0
#define _RT_SAMPLE5 0

cbuffer PER_BATCH : register(b0)
{
  float4 texToTexParams0 : packoffset(c0); // Often set to the inverse of the output resolution (pixel size)
  float4 texToTexParams1 : packoffset(c1); // Often set to the inverse of the output resolution (pixel size)
}

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState _tex0_s : register(s0);
Texture2D<float4> _tex0 : register(t0);

// TexToTexSampledPS
// This is a resampling shader (from one resolution to another, e.g. upscale, downscale/blur/mipmap, stretch). It's run after upscaling (as least in the main use case).
// This always runs in post process if we forced sharpening or chromatic aberration, before them, so it probably does nothing in that case (they just have a fixed pipeline).
// Note that this also runs many times in the middle of the rendering pipeline, to downscale textures and stuff like that.
// Note that this can generate invalid luminances, but we can't fix it here as the shader is too generic.
void main(
  float4 inWPos : SV_Position0,
  float2 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
  // LUMA FT: fixed missing UV clamps
  float2 outputResolution;
  _tex0.GetDimensions(outputResolution.x, outputResolution.y); // We can't use "CV_ScreenSize" here as that's for the output resolution
  float2 sampleUVClamp = CV_HPosScale.xy - (0.5 / outputResolution);

  float2 baseTC = inBaseTC.xy * CV_HPosScale.xy;
  
  float4 colorSum = 0;
  float validColorCount = 0;
  float4 color = 0;
  
	color = _tex0.Sample(_tex0_s, min(baseTC, sampleUVClamp)); // LUMA FT: it's unclear why this takes a sample in the middle too, maybe as a bias?
  validColorCount += all(color == 0.0) ? 0.0 : 1.0;
  colorSum += color;
  color = _tex0.Sample(_tex0_s, min(baseTC + texToTexParams0.xy, sampleUVClamp));
  validColorCount += all(color == 0.0) ? 0.0 : 1.0;
  colorSum += color;
  color = _tex0.Sample(_tex0_s, min(baseTC + texToTexParams0.zw, sampleUVClamp));
  validColorCount += all(color == 0.0) ? 0.0 : 1.0;
  colorSum += color;
  color = _tex0.Sample(_tex0_s, min(baseTC + texToTexParams1.xy, sampleUVClamp));
  validColorCount += all(color == 0.0) ? 0.0 : 1.0;
  colorSum += color;
  color = _tex0.Sample(_tex0_s, min(baseTC + texToTexParams1.zw, sampleUVClamp));
  validColorCount += all(color == 0.0) ? 0.0 : 1.0;
  colorSum += color;

  // LUMA FT: ignore any color that was fully zero in color and alpha, we wouldn't want to spread full transparency in SSR.
  // We spread the last reflection color at the edge of valid SSR to the non valid SSR texels too, but keep a smooth alpha gradient, if we don't do this, SSR could have black edges where reflections fade.
  // Generally this looks better, though in some cases this can make it look worse, as color+alpha gradients fade out next to gradients that already went to black earlier because of this change (and thus their meeting point has a step).
  // This also kinda makes the assumption that there isn't anything pure black in the scene, but it's a good assumption, there never is, if necessary, we could encode the alpha to -1 for these texels (the ones that had no SSR calculated at all), and exclusively skip these.
  // Note that this can make reflections RGB color flicker a bit (due to TAA jitters), but once multiplied by the alpha, the flicker won't be visible anymore.
  if (LumaData.CustomData != 0 && validColorCount != 0.0)
  {
    colorSum.rgb /= validColorCount;
    colorSum.a /= 5.0;
  }
  else
  {
    colorSum /= 5.0;
  }

  outColor.xyzw = colorSum;
}