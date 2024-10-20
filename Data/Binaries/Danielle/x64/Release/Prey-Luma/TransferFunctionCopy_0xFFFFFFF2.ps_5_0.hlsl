#include "include/Common.hlsl"

Texture2D<float4> sourceTexture : register(t0);

// Custom Luma shader to apply the display (or output) transfer function from a linear input
float4 main(float4 pos : SV_Position0) : SV_Target0
{
	float4 color = sourceTexture.Load((int3)pos.xyz);
	return color;
}