// 3 LUTs blend
#define _RT_SAMPLE0 0
#define _RT_SAMPLE1 1

#include "PostEffectsGame_MergeColorChart.hlsl"

//TODOFT: this shader is missing
void main(
  v2f_cch input,
  out float4 outColor : SV_Target0)
{
	outColor = MergeColorChartsPS(input);
	return;
}