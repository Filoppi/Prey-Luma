#include "PostAA_AA.hlsl"

#define _RT_SAMPLE4 1

// PostAA_PS
void main(
  float4 inWPos : SV_Position0,
  float2 inBaseTC : TEXCOORD0,
  // "1 / CV_HPosScale.xy"
  nointerpolation float2 inBaseTCScale : TEXCOORD1,
  out float4 outColor : SV_Target0)
{
#if !ENABLE_AA
	outColor	= SampleCurrentScene(inBaseTC.xy * CV_HPosScale.xy);
	return;
#endif
	
#if _RT_SAMPLE4
	outColor = Fxaa3(inBaseTC.xy, cbPostAA.screenSize);
#endif

  return;
}