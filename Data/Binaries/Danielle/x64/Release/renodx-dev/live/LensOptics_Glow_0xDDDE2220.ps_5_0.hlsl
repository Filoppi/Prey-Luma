cbuffer PER_BATCH : register(b0)
{
  float4 HDRParams : packoffset(c0);
  float4 lumaParams : packoffset(c1);
}

#include "include/LensOptics.hlsl"

#define focusFactor lumaParams.x
#define gamma lumaParams.y

// lensGhostPS
// e.g. draws a "ghost" (bloom) sprite around the sun.
// This requires aspect ratio correction as it was stretched in ultrawide.
void main(
  float4 hpos : SV_Position0,
  float4 uv : TEXCOORD0,
  float3 center : TEXCOORD1,
  float4 color : COLOR0,
  out float4 outColor : SV_Target0)
{
	#define thou uv.x
	#define theta uv.y
	
	float constArea = 1;
	float fadingArea = pow( lerp( constArea, 0, saturate((thou-focusFactor)/(1.0-focusFactor)) ), gamma);

	const float fadingSpan = 0.001;
	float transition = saturate( lerp( 1, 0, (focusFactor-thou)/fadingSpan )  );
	float finalGrad = lerp( constArea, fadingArea, transition );

	outColor = ToneMappedPreMulAlpha(color * finalGrad);
  return;

  #undef thou
  #undef theta
}