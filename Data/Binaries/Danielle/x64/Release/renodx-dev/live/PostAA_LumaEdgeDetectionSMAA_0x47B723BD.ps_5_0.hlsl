#include "include/Common.hlsl"

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState _tex0_s : register(s0);
Texture2D<float4> _tex0 : register(t0);

#define SCALE_PIXELSIZE 2
#define SMAA_THRESHOLD 0.1

float GetLuma(float3 cColor)
{
#if POST_PROCESS_SPACE_TYPE >= 1
	const float paperWhite = GamePaperWhiteNits / sRGB_WhiteLevelNits;
  cColor = linear_to_game_gamma_mirrored(cColor / paperWhite);
#endif // POST_PROCESS_SPACE_TYPE >= 1
  return GetLuminance(cColor);
}

void main(
  float4 v0 : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
#if !ENABLE_AA || !ENABLE_SMAA // Optimization
  outColor = 0;
  return;
#endif

	float2 pixelOffset = CV_ScreenSize.zw * SCALE_PIXELSIZE;

  float2 threshold = SMAA_THRESHOLD;

  // Calculate lumas:
  float L = GetLuma(_tex0.Sample(_tex0_s, inBaseTC.xy).rgb);
  float Lleft = GetLuma(_tex0.Sample(_tex0_s, inBaseTC.xy + float2(-1, 0) * pixelOffset).rgb);
  float Ltop  = GetLuma(_tex0.Sample(_tex0_s, inBaseTC.xy + float2( 0, -1) * pixelOffset).rgb);

  // We do the usual threshold:
  float4 delta;
  delta.xy = abs(L.xx - float2(Lleft, Ltop));
  float2 edges = step(threshold, delta.xy);

  // Then discard if there is no edge:
  if (dot(edges, 1.0) == 0.0)
      discard;// this supported on cg ? else clip(-1)

  // Calculate right and bottom deltas:
  float Lright = GetLuma(_tex0.Sample(_tex0_s, inBaseTC.xy + float2(1, 0) * pixelOffset).rgb);
  float Lbottom  = GetLuma(_tex0.Sample(_tex0_s, inBaseTC.xy + float2(0, 1) * pixelOffset).rgb);
  delta.zw = abs(L.xx - float2(Lright, Lbottom));

  // Calculate the maximum delta in the direct neighborhood:
  float maxDelta = max(max(max(delta.x, delta.y), delta.z), delta.w);

  // Calculate left-left and top-top deltas:
  float Lleftleft = GetLuma(_tex0.Sample(_tex0_s, inBaseTC.xy + float2(-2, 0) * pixelOffset).rgb);
  float Ltoptop = GetLuma(_tex0.Sample(_tex0_s, inBaseTC.xy + float2(0, -2) * pixelOffset).rgb);
  delta.zw = abs(float2(Lleft, Ltop) - float2(Lleftleft, Ltoptop));

  // Calculate the final maximum delta:
  maxDelta = max(max(maxDelta, delta.z), delta.w);
  edges.xy *= step(0.5 * maxDelta, delta.xy);

	outColor = float4(edges, 0.0, 0.0);
  return;
}