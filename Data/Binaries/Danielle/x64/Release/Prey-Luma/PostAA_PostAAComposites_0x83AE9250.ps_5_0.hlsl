#include "PostAA_PostAAComposites.hlsl"

// Vignette+FilmGrain
void main(
  float4 WPos : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
  PostAAComposites_PS(WPos, inBaseTC, outColor);
}