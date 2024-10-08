// ArkSunPS
// LUMA: Unchanged.
// This is the pixel shader that draws the sun on a polygon, it acts as a sprite.
// Currently it has the defect of drawing a white circle in a black polygon, and given that there is no alpha, the black edges stick out when there's no sun shafts to cover it up (usually there are).
void main(
  float4 WPos : SV_Position0,
  float4 inPosInQuad : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
	float2 vPosInQuad = inPosInQuad.xy;
	float fStarIntensity = inPosInQuad.w;

	float fDistToCenter2 = dot(vPosInQuad, vPosInQuad);
	float fScale = saturate(1 - fDistToCenter2);

	float3 cFinal = fScale.xxx * fStarIntensity;

	outColor = float4(cFinal, fScale);
  return;
}