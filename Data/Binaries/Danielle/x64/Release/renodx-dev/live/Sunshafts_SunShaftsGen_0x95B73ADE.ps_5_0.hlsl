#include "include/Common.hlsl"

#include "include/CBuffer_PerViewGlobal.hlsl"

cbuffer CBSunShafts : register(b0)
{
  struct
  {
    float4 sunPos;
    float4 params;
  } cbSunShafts : packoffset(c0);
}

SamplerState _tex0_s : register(s0);
Texture2D<float4> _tex0 : register(t0);

float2 MapViewportToRaster(float2 normalizedViewportPos, bool bOtherEye = false)
{
		return normalizedViewportPos * CV_HPosScale.xy;
}

// This draws after "SunShaftsMaskGen" ("_tex0").
// This is run twice, the first time "_tex0" is the output of "SunShaftsMaskGen", the second time is its own previous output.
void main(
  float4 HPosition : SV_Position0,
  float2 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
  float2 sunPosProj = cbSunShafts.sunPos.xy;
	float fSign = cbSunShafts.sunPos.w; // LUMA FT: this is actually not a -1/0/+1 value, but it probably represents something like the direction or dot product
	float fInvSize = cbSunShafts.params.y;
	float fDisplacement = cbSunShafts.params.x;
  
  // LUMA FT: fixed sun shafts not scaling correctly by aspect ratio.
  // They exclusively scaled properly with the FOV, so (e.g.) at 32:9 they'd look the same at 16:9 as long as the same horizontal FOV was the same (e.g. ~82), but that would cause major cropping of the picture in UW,
  // so this code assumes the user has scaled the FOV properly for their UW resolution, matching the 16:9 vertical FOV.
  // Ideally these calculations would be done by comparing vanilla FOV/AR and current FOV/AR but we don't have all that information accessible here.
  // Note that this adjustment results in a perfect match, it doesn't need to work in FOV tangent space.
  float screenAspectRatio = CV_ScreenSize.w / CV_ScreenSize.z;
  float aspectRatioCorrection = max(screenAspectRatio / NativeAspectRatio, 1.0);
  
  float2 sunVec = (sunPosProj.xy - inBaseTC.xy) * aspectRatioCorrection;
  
  // This code makes the sun shafts aspect ratio independent from our screen aspect ratio. By default we want them a bit wider than a square.
#if 1 // LUMA FT: de-approximated aspect ratio 1.333 multiplier and fixed aspect ratio scaling not being done on the actual output resolution (the math made now sense, it was trying to go from the rendering resolution to the output resolution but the math went in the opposite direction)
  static const float BaseAspectRatio = 4.0 / 3.0;
  float fAspectRatio = BaseAspectRatio / screenAspectRatio;
#else
  float fAspectRatio = 1.333 * (CV_ScreenSize.y*CV_HPosScale.x) / (CV_ScreenSize.x*CV_HPosScale.y);
#endif
  
  // Smaller value means bigger
  
// LUMA FT: make it a bit bigger in HDR so its more realistic (this isn't directly the size of the sun, but the size of the sun shafts central "white" blob)
#if SUNSHAFTS_LOOK_TYPE > 0
  //TODOFT2: the sun shafts god rays size can be too big in some levels... (central HUB)? Try sampling the background color of the sky or stuff like that to scale them? Or not show them if they are blocked!? (probably already done)?
#if 0
  const float sizeNormalized = saturate(fInvSize / 5.0); // Sun shafts size usually is between 1.5 and 5, it shouldn't ever be much smaller or bigger or it would look off
  const float sizeMultiplier = lerp(1.0, 0.75, 1); // The bigger it is, the less we scale it (basically acting as a pow function), because if it was already small, it's likely because the sun is partially occluded by stuff and we wouldn't want the sun shafts around the scene just "hanging" around for no purpose (they might also suddently disappear due to the sun bound found occluded by previous occlusion tests)
#else
  static const float sizeMultiplier = 1.0;
  fInvSize = pow(fInvSize, fInvSize >= 1 ? 0.825 : 1.175);
#endif
#else
  static const float sizeMultiplier = 1.0;
#endif

  float sunDist = saturate(fSign) * (1.0 - saturate(length(sunVec * float2(1, fAspectRatio)) * fInvSize * sizeMultiplier)); // LUMA FT: added a custom multiplier and removed unnecessary (duplicate) saturate() (we might remove it from "fSign" as well but I'm unsure it's safe).
  
  float2 sunDir = sunVec;
  sunDir.xy *= fDisplacement * fSign / aspectRatioCorrection;
  
  static const uint depthShaftsIterationsVanilla = 8;
  // LUMA FT: increased the samples to make bands smoother bethween each other. 12 is a good balance between performance and quality (16+ looks even better).
  // We boost their strenght on higher sample rates because they end up being slightly less visible.
#if SUNSHAFTS_QUALITY <= 0
  static const uint depthShaftsIterations = depthShaftsIterationsVanilla;
  static const float shaftsStrength = 1.0;
#elif SUNSHAFTS_QUALITY == 1
  static const uint depthShaftsIterations = 12;
  static const float shaftsStrength = 1.0 + (0.075 * 0.25);
#elif SUNSHAFTS_QUALITY == 2
  static const uint depthShaftsIterations = 16;
  static const float shaftsStrength = 1.0 + (0.075 * 0.5);
#else // SUNSHAFTS_QUALITY >= 3
  static const uint depthShaftsIterations = 32;
  static const float shaftsStrength = 1.0 + 0.075;
#endif
  
  float4 accumColor = 0; 
  // LUMA FT: re-wrote the code to use a for loop, so we can change the number of iterations and lower the "banding" that the shafts/rays have
	[unroll]
  for (uint i = 0; i < depthShaftsIterations; i++)
  {
    // LUMA FT: this prevents the UV sampling from straying from vanilla, while still allowing a higher number of iterations
    static const float uvScale = float(depthShaftsIterationsVanilla) / float(depthShaftsIterations);

    float4 baseColor = _tex0.Sample(_tex0_s, MapViewportToRaster(saturate(inBaseTC.xy + (sunDir.xy * float(i) * uvScale)))); // LUMA FT: added saturate to UVs to avoid them going over the used portion of the source texture
    accumColor += baseColor * (1.0-(float(i)/float(depthShaftsIterations)));
  }
  accumColor /= float(depthShaftsIterations);

  outColor = accumColor * 2.0 * shaftsStrength * float4(sunDist.xxx, 1); // LUMA FT: this now writes to a float texture so they are not clipped anymore (it used to write to a UNORM texutre). 
  // LUMA FT: this was already theoretically writing "linear" colors, not gamma space, independently of "PREEMPT_SUNSHAFTS", so we leave it as it was.
  // We couldn't really do proper gamma adjustments here as this shader runs twice on itself, and in the end, it's additive; there is no proper concept of "gamma" for additive color.

  outColor.w += 1.0 - saturate( fSign * 0.1 + 0.9 ); // LUMA FT: removed unnecessary (duplicate) saturate(). The alpha doesn't seem to influence the drawing anyway, so theoretically we could disable it.

  return;
}