Texture2D<float4> sourceTexture : register(t0);

// Custom Luma shader to copy a texture into another one (when e.g. they are of a different format but with matching size)
float4 main(float4 pos : SV_Position0) : SV_Target0
{
#if 0 // TEST
	return float4(1, 0, 0, 1);
#else
	return sourceTexture.Load((int3)pos.xyz);
#endif
}