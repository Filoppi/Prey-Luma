// 2 LUTs blend
#define _RT_SAMPLE0 1
#define _RT_SAMPLE1 0

#include "PostEffectsGame_MergeColorChart.hlsl"

void main(
  v2f_cch input,
  out float4 outColor : SV_Target0)
{
	outColor = MergeColorChartsPS(input);
	return;
}