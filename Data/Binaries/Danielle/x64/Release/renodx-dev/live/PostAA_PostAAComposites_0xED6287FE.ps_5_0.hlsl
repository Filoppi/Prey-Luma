// lens composite
#define _RT_SAMPLE1 1
// lens composite chromatic aberration
#define _RT_SAMPLE3 1

#include "PostAA_PostAAComposites.hlsl"

// Vignette+FilmGrain+LensComposite
void main(
  float4 WPos : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
  PostAAComposites_PS(WPos, inBaseTC, outColor);
  return;
}