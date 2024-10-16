#include "PostEffectsGame_CombineColorGradingWithColorChart.hlsl"

void main(
  float4 HPosition : SV_Position0,
  float2 baseTC : TEXCOORD0,
  float3 Color : TEXCOORD1,
  out float4 outColor : SV_Target0)
{
	float4 col = float4(Color, 1.f);

  bool adjustLevels = true; // _RT_SAMPLE0
  bool photoFilter = true; // _RT_SAMPLE4
  bool selectiveColorAdjustment = false; // _RT_SAMPLE2

	col.xyz = CombineColorGradingWithColorChartPS(col.xyz, adjustLevels, photoFilter, selectiveColorAdjustment);

  outColor.xyz = col.xyz;
  outColor.w = 1;
  return;
}