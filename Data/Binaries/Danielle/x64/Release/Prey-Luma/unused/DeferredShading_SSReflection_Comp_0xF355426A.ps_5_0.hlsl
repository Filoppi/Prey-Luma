SamplerState ssReflectionLinear : register(s1);
Texture2D<float4> ssrComposeSpecularTex : register(t0);
Texture2D<float4> ssrComposeReflection0Tex : register(t1);
Texture2D<float4> ssrComposeReflection1Tex : register(t2);
Texture2D<float4> ssrComposeReflection2Tex : register(t3);
Texture2D<float4> ssrComposeReflection3Tex : register(t4);

#include "include/CBuffer_PerViewGlobal.hlsl"

struct MaterialAttribsCommon
{
	half3  NormalWorld;
	half3  Albedo;
	half3  Reflectance;
	half3  Transmittance;
	half   Smoothness;
	half   ScatteringIndex;
	half   SelfShadowingSun;
	int    LightingModel;
};

#define MAX_FRACTIONAL_8_BIT        (255.0f / 256.0f)
#define MIDPOINT_8_BIT              (127.0f / 255.0f)
#define TWO_BITS_EXTRACTION_FACTOR  (3.0f + MAX_FRACTIONAL_8_BIT)
#define LIGHTINGMODEL_STANDARD       0
#define LIGHTINGMODEL_TRANSMITTANCE  1
#define LIGHTINGMODEL_POM_SS         2
#define LIGHTINGMODEL_ALIEN          3

half3 DecodeColorYCC( half3 encodedCol, const bool useChrominance = true )
{
	encodedCol = half3(encodedCol.x, encodedCol.y / MIDPOINT_8_BIT - 1, encodedCol.z / MIDPOINT_8_BIT - 1);
	if (!useChrominance) encodedCol.yz = 0;
	
	// Y'Cb'Cr'
	half3 col;
	col.r = encodedCol.x + 1.402 * encodedCol.z;
	col.g = dot( half3( 1, -0.3441, -0.7141 ), encodedCol.xyz );
	col.b = encodedCol.x + 1.772 * encodedCol.y;

	return col * col;
}

MaterialAttribsCommon DecodeGBuffer( half4 bufferA, half4 bufferB, half4 bufferC )
{
	MaterialAttribsCommon attribs;
	
	attribs.LightingModel = (int)floor(bufferA.w * TWO_BITS_EXTRACTION_FACTOR);
	
	attribs.NormalWorld = normalize( bufferA.xyz * 2 - 1 );
	attribs.Albedo = bufferB.xyz * bufferB.xyz;
	attribs.Reflectance = DecodeColorYCC( bufferC.yzw, attribs.LightingModel == LIGHTINGMODEL_STANDARD );
	attribs.Smoothness = bufferC.x;
	attribs.ScatteringIndex = bufferB.w * TWO_BITS_EXTRACTION_FACTOR;
	
	attribs.Transmittance = half3( 0, 0, 0 );
	if (attribs.LightingModel == LIGHTINGMODEL_TRANSMITTANCE)
	{
		attribs.Transmittance = DecodeColorYCC( half3( frac(bufferA.w * TWO_BITS_EXTRACTION_FACTOR), bufferC.z, bufferC.w ) );
	}
	
	attribs.SelfShadowingSun = 0;
	if (attribs.LightingModel == LIGHTINGMODEL_POM_SS)
	{
		attribs.SelfShadowingSun = saturate(bufferC.z / MIDPOINT_8_BIT - 1);
	}
	
	return attribs;
}

// This blends in different downscaled versions of the gbuffer diffuse color buffer,
// to create a screen space reflections map.
void main(
  float4 v0 : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
	// LUMA FT: the uv doesn't need to be scaled by "CV_HPosScale.xy" here (it already is in the vertex shader)
	// LUMA FT: fixed missing clamp to "CV_HPosClamp.xy"
	const float2 baseTC = min(inBaseTC.xy, CV_HPosClamp.xy);

	float4 GBufferC = ssrComposeSpecularTex.SampleLevel(ssReflectionLinear, baseTC, 0);
	MaterialAttribsCommon attribs = DecodeGBuffer(0, 0, GBufferC);
	half gloss = attribs.Smoothness;
	gloss *= gloss;

	float weight = frac( min( gloss, 0.9999 ) * 3 );

	half4 refl0 = ssrComposeReflection0Tex.SampleLevel(ssReflectionLinear, baseTC, 0);
	half4 refl1 = ssrComposeReflection1Tex.SampleLevel(ssReflectionLinear, baseTC, 0);
	half4 refl2 = ssrComposeReflection2Tex.SampleLevel(ssReflectionLinear, baseTC, 0);
	half4 refl3 = ssrComposeReflection3Tex.SampleLevel(ssReflectionLinear, baseTC, 0);

	[branch] if (gloss > 2.0/3.0)
		outColor = lerp( refl1, refl0, weight * weight );
	else if (gloss > 1.0/3.0)
		outColor = lerp( refl2, refl1, weight );
	else
		outColor = lerp( refl3, refl2, 1 );
}