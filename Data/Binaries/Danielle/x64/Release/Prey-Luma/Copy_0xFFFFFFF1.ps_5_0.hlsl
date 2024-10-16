Texture2D<float4> sourceTexture : register(t0);

// Custom Luma shader to copy a texture into another one (when e.g. they are of a different format but with matching size)
float4 main(float4 pos : SV_Position0) : SV_Target0
{
#if 0 // TEST
		return float4(1.f, 0.f, 0.f, 1.f);
#else
		float3 resolution;
		sourceTexture.GetDimensions(resolution.x, resolution.y);
		resolution.z = 1;
		pos.xyz += 0.5;
		pos.xyz *= resolution;
		return sourceTexture.Load((int3)pos.xyz);
#endif
}