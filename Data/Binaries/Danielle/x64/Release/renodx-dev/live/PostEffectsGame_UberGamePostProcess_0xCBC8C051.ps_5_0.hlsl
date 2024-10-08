// chroma shift
#define _RT_SAMPLE0 1
#define _RT_SAMPLE1 0

#include "PostEffectsGame_UberGamePostProcess.hlsl"

void main(
  float4 WPos : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
	UberGamePostProcessPS(WPos, inBaseTC, outColor);
  return;
}