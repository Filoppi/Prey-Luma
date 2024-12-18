cbuffer PER_BATCH : register(b0)
{
  float4 HDRParams : packoffset(c0);
}

#include "include/LensOptics.hlsl"

SamplerState baseMap_s : register(s0);
SamplerState depthMap_s : register(s1);
Texture2D<float4> baseMap : register(t0);
Texture2D<float4> depthMap : register(t1);

// 3Dmigoto declarations
#define cmp -

// shaftsOccPS
// This draws an occlusion and color map (in black and white) based on the depth and color buffer.
// This still writes to an SDR buffer (R8G8B8A8_UNORM), even with the LUMA mod (I think, but not 100% sure, it doesn't really matter as it doesn't need values beyond 1).
// This doesn't run at full resolution, but at 1/4 (of the output resolution, this isn't scaled with DRS) (might vary depending on the base resolution? there might be some mip map pow 2 rounding?).
void main(
  float4 v0 : SV_Position0,
  float3 v1 : TEXCOORD0,
  float2 v2 : TEXCOORD1,
  float4 v3 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;

  // LUMA FT: fixed depth map not being scaled by rendering resolution scale ("MapViewportToRaster()") (we are not sure whether "CV_ScreenSize"/"CV_HPosScale" are already updated to post DLSS full screen res here)
  // Dejitter as these draw after TAA and don't acknowledge jitters in their vertices (obviously), while the depth would be jittered.
#if REJITTER_SUNSHAFTS
  v2.xy -= LumaData.CameraJitters.xy * float2(0.5, -0.5);
#endif
  v2.xy *= LumaData.RenderResolutionScale;
  // LUMA FT: lens optics can't really read "CV_HPosScale" as it's forced to 1 when using DLSS, so we use our custom copy of it.
  float2 HPosClamp = LumaData.RenderResolutionScale - CV_ScreenSize.zw;
  // LUMA FT: it's not clear why this is sampling depth with possibly a bilinear sampler,
  // possibly because this pass can run at a lower resolution than rendering resolution,
  // or maybe because they forgot, but ultimately it doesn't really matter as this very low important so depth sampling doesn't need to be conservative of max (e.g. GatherRed()).
  // It's taking "x" as it's the "max" channel of the linearized downscaled depth (though that's not enough if the texture is more than half smaller).
  r0.x = depthMap.Sample(depthMap_s, min(v2.xy, HPosClamp)).x;
  r0.x = -v1.z + r0.x;
  r0.y = cmp(0 < r0.x);
  r0.x = cmp(r0.x < 0);
  r0.x = (int)-r0.y + (int)r0.x;
  r0.x = (int)r0.x;
  r0.x = saturate(r0.x);
  r0.yz = v1.xy * float2(2,2) + float2(-1,-1);
  r0.y = dot(r0.yz, r0.yz);
  r0.y = sqrt(r0.y);
  r0.y = 1 + -r0.y;
  r0.x = r0.x * r0.y;
  r1.xyzw = baseMap.Sample(baseMap_s, v1.xy).xyzw; // LUMA FT: this is usually a sprite that looks like the sun
  r0.xyzw = r1.xyzw * r0.xxxx;
	o0 = ToneMappedPreMulAlpha(r0, false); // LUMA FT: the alpha is ignored
#if 0 // LUMA FT: quick test to visualize the occlusion texture
  o0 = r1.xyzw;
#endif
  return;
}