#include "PostEffectsGame_MergeColorChart.hlsl"

// LUMA: Unchanged.
// MergeColorChartsVS
void main(
  a2v_cch input,
  out v2f_cch output)
{
  output.baseTC = input.baseTC;

  output.HPosition.xy = input.Position.xy * float2(2.f, -2.f) - float2(1.f, -1.f);
  output.HPosition.zw = float2(0.f, 1.f);
  
  output.Color = 0;
  return;
}