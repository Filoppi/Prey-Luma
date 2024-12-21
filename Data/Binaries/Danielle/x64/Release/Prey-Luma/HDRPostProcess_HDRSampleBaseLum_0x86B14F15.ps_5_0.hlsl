#include "include/Common.hlsl"
#include "include/CBuffer_PerViewGlobal.hlsl"
#include "include/GBuffer.hlsl"

cbuffer CBExposure : register(b0)
{
  struct
  {
    float4 SampleLumOffsets[2];
    float EyeAdaptationSpeed;
    float RangeAdaptationSpeed;
    float2 __padding;
  } cbExposure : packoffset(c0);
}

SamplerState _samp0_s : register(s0);
SamplerState _samp1_s : register(s1);
SamplerState _samp2_s : register(s2);
Texture2D<float4> _tex0_D3D11 : register(t0);
Texture2D<float4> _tex1_D3D11 : register(t1);
Texture2D<float4> _tex2_D3D11 : register(t2);

float2 MapViewportToRaster(float2 normalizedViewportPos, bool bOtherEye = false)
{
	return normalizedViewportPos * CV_HPosScale.xy;
}

float GetBaseSample(float2 _uv)
{
	float2 coord = saturate(_uv);
	// Use base color to get a coarse approximation of the (incoming) illuminance
	MaterialAttribsCommon attribs = DecodeGBuffer(
		_tex0_D3D11.Sample(_samp0_s, coord),
		_tex1_D3D11.Sample(_samp1_s, coord),
		_tex2_D3D11.Sample(_samp2_s, coord));
	half baseColorLum = max(max(GetLuminance(attribs.Albedo), GetLuminance(attribs.Reflectance)), 0.01);

	// Assume emissive surfaces (especially sky) have the typical scene reflectance to keep auto exposure more stable
	if (GetLuminance(attribs.Albedo) == 0)
		baseColorLum = 0.2;

	return baseColorLum;
}

// HDRSampleBaseLumPS
void main(
  float4 WPos : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
	float2 inputResolution;
	_tex0_D3D11.GetDimensions(inputResolution.x, inputResolution.y); // We can't use "CV_ScreenSize" here as that's for the output resolution
	float2 sampleUVClamp = CV_HPosScale.xy - (0.5 / inputResolution);

	// LUMA FT: fixed bad UVs DRS scaling
	outColor.x = GetBaseSample(clamp(MapViewportToRaster(inBaseTC.xy + cbExposure.SampleLumOffsets[0].xy), 0.0, sampleUVClamp.xy));
	outColor.y = GetBaseSample(clamp(MapViewportToRaster(inBaseTC.xy + cbExposure.SampleLumOffsets[0].zw), 0.0, sampleUVClamp.xy));
	outColor.z = GetBaseSample(clamp(MapViewportToRaster(inBaseTC.xy + cbExposure.SampleLumOffsets[1].xy), 0.0, sampleUVClamp.xy));
	outColor.w = GetBaseSample(clamp(MapViewportToRaster(inBaseTC.xy + cbExposure.SampleLumOffsets[1].zw), 0.0, sampleUVClamp.xy));
}