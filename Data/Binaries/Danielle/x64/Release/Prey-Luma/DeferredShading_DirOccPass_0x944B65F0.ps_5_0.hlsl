#define _RT_SAMPLE0 1

#include "DeferredShading_DirOccPass.hlsl"

void main(
  float4 WPos : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
	outColor = DirOccPassPS(WPos, inBaseTC);
	return;
}