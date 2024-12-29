RWTexture2D<float4> sourceTargetTexture : register(u0);

// This should be used with "command_list->Dispatch((output_texture_width + 7) / 8, (output_texture_height + 7) / 8, 1)" in DX11 c++
[numthreads(8,8,1)]
void main( uint2 vDispatchThreadId : SV_DispatchThreadID )
{
	const uint2 pixelPos = vDispatchThreadId.xy;
	const float2 uv = pixelPos + 0.5; // Divide by the texture x y size if needed to be normalized

#pragma warning( disable : 3206 ) // Not sure why it's needed
	float4 color = sourceTargetTexture.Load(uint3(pixelPos, 0));
#pragma warning( default : 3206 )
	sourceTargetTexture[pixelPos] = color;
}