#define _RT_SAMPLE0 0
#define _RT_SAMPLE1 0

#include "PostEffectsGame_MergeColorChart.hlsl"

// LUMA: Unchanged.
void main(
  v2f_cch input,
  out float4 outColor : SV_Target0)
{
	outColor = MergeColorChartsPS(input);
	return;
}