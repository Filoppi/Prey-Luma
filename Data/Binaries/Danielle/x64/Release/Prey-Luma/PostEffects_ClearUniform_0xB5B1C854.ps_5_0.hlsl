#include "include/UI.hlsl"

cbuffer PER_BATCH : register(b0)
{
  float4 vClearParam : packoffset(c0);
}

// LUMA FT: this is used to clean any texture to a fixed color
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  o0.xyzw = vClearParam.xyzw;
  
  // LUMA FT: support any image encoding and blend type etc, just in case this shader was ever "exploited" to draw a fixed (non black) color on the swapchain
	o0 = ConditionalLinearizeUI(o0, false, false, true); // This will take care of any "POST_PROCESS_SPACE_TYPE" case
}