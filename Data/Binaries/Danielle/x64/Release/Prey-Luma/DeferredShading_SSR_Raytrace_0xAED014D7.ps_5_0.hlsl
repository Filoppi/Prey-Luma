cbuffer CBSSRRaytrace : register(b0)
{
  struct
  {
    row_major float4x4 mViewProj;
    row_major float4x4 mViewProjPrev;
    float2 screenScalePrev;
    float2 screenScalePrevClamp;
  } cbRefl : packoffset(c0);
}

#include "include/Common.hlsl"
#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState ssReflectionPoint : register(s0);
SamplerState ssReflectionLinear : register(s1);
SamplerState ssReflectionLinearBorder : register(s2);
Texture2D<float> reflectionDepthTex : register(t0); // Device depth
Texture2D<float4> reflectionNormalsTex : register(t1);
Texture2D<float4> reflectionSpecularTex : register(t2);
Texture2D<float4> reflectionDepthScaledTex : register(t3); // Half res 4 channel depth (each channel is slightly different)
Texture2D<float4> reflectionPreviousSceneTex : register(t4); // Pre-tonemapping HDR scene from the previous frame
Texture2D<float2> reflectionLuminanceTex : register(t5); // Scene exposure (probably from the previous frame)

float GetLinearDepth(float fLinearDepth, bool bScaled = false)
{
    return fLinearDepth * (bScaled ? CV_NearFarClipDist.y : 1.0f);
}

float LinearDepthFromDeviceDepth(float _device)
{
	return CV_ProjRatio.y / (_device - CV_ProjRatio.x);
}

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

float3 GetWorldViewPos()
{
	return CV_ScreenToWorldBasis._m03_m13_m23;
}

float3 ReconstructWorldPos(int2 WPos, float linearDepth, bool bRelativeToCamera = false)
{
	float4 wposScaled = float4(WPos * linearDepth, linearDepth, bRelativeToCamera ? 0.0 : 1.0);
	return mul(CV_ScreenToWorldBasis, wposScaled);
}

float2 ClampScreenTC(float2 TC, float2 maxTC)
{
	return clamp(TC, 0, maxTC.xy);
}

float2 ClampScreenTC(float2 TC)
{
	return ClampScreenTC(TC, CV_HPosClamp.xy);
}

void main(
  float4 inWPos : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
	outColor = 0;

	// Random values for jittering a ray marching step
	const half jitterOffsets[16] = {
		0.215168h, -0.243968h, 0.625509h, -0.623349h,
		0.247428h, -0.224435h, -0.355875h, -0.00792976h,
		-0.619941h, -0.00287403h, 0.238996h, 0.344431h,
		0.627993h, -0.772384h, -0.212489h, 0.769486h
	};

	const float2 halfTexel = CV_ScreenSize.zw;

	// LUMA FT: the uv doesn't need to be scaled by "CV_HPosScale.xy" here (it already is in the vertex shader)
	// LUMA FT: fixed missing clamps to "CV_HPosClamp.xy"
	inBaseTC.xy = min(inBaseTC.xy, CV_HPosClamp.xy);

	const float2 baseTC    = inBaseTC.xy;
	const float2 gbufferTC = baseTC + halfTexel; // LUMA FT: unclear why this is done, it seems a bit wrong
	
	const float fDepth = GetLinearDepth( reflectionDepthTex.SampleLevel(ssReflectionPoint, baseTC, 0) );

	// Make sure we do linear half pixel offset samples since we're rendering at half res
	float4 GBufferA = reflectionNormalsTex.SampleLevel(ssReflectionLinear, gbufferTC, 0);
	float4 GBufferC = reflectionSpecularTex.SampleLevel(ssReflectionLinear, gbufferTC, 0);
	MaterialAttribsCommon attribs = DecodeGBuffer(GBufferA, 0, GBufferC);
	
	float3 vPositionWS = ReconstructWorldPos(inWPos.xy, fDepth, true);
	float3 viewVec = normalize( vPositionWS );
	vPositionWS += GetWorldViewPos();
		
	const float maxReflDist = 1.5 * fDepth * CV_NearFarClipDist.y;
	float3 reflVec = normalize( reflect( viewVec, attribs.NormalWorld ) ) * maxReflDist;
	
	float dirAtten = saturate( dot( viewVec, reflVec ) + 0.5);
  
  // Ignore sky (draw black, no alpha) (there's no alternative apparently, we can't reflect it (or at least, the game always tried not to))
  // LUMA FT: change sky comparison from ==1 to >=0.9999999 as there was some precision loss in there, which made the SSR have garbage in the sky (trailed behind from the last drawn edge)
	if (dirAtten < 0.01 || (fDepth >= 0.9999999 && fDepth <= 1.0000001))  return;
	
	float4 rayStart = mul(cbRefl.mViewProj, float4( vPositionWS, 1 ));
	rayStart.z = fDepth;

	float4 rayEnd = mul(cbRefl.mViewProj, float4( vPositionWS + reflVec, 1 ));
	rayEnd.z = LinearDepthFromDeviceDepth(rayEnd.z / rayEnd.w);

	float4 ray = rayEnd - rayStart;
	
	const int numSamples = 4 + attribs.Smoothness * 28;
	
#if 0 // LUMA FT: this was disabled, it doesn't seem to help
	const int jitterIndex = (int)dot( frac( inBaseTC.zw ), float2( 4, 16 ) );
	const float jitter = jitterOffsets[jitterIndex] * 0.002;
#else
	const float jitter = 0;
#endif
	
	const float stepSize = 1.0 / numSamples + jitter;
	const float intervalSize = maxReflDist / (numSamples * 1.6) / CV_NearFarClipDist.y;
	
	// Perform raymarching
	float4 color = 0;
	float len = stepSize;
	float bestLen = 0;
  float2 sampleUVClamp = CV_HPosScale.xy - (CV_ScreenSize.zw * 2.0);
	[loop]
	for (int i = 0; i < numSamples; ++i)
	{
		float4 projPos = rayStart + ray * len;
		float2 depthTC = ClampScreenTC(projPos.xy / projPos.w, sampleUVClamp); // LUMA FT: Fixed clamp, it was using "CV_HPosClamp" here but "reflectionDepthScaledTex" is half resolution, so we need to clamp it differently

		float fLinearDepthTap = reflectionDepthScaledTex.SampleLevel(ssReflectionPoint, depthTC, 0).x; // half res R16F
		if (abs(fLinearDepthTap - projPos.z) < intervalSize)
		{
			bestLen = len;
			break;
		}

		len += stepSize;
	}

	[branch]
	if (bestLen > 0)
	{
		const float curAvgLum = reflectionLuminanceTex.SampleLevel(ssReflectionPoint, baseTC, 0).x;
    //TODOFT: review this clamp, does it actually help?
		const float maxLum = curAvgLum * 100;  // Limit brightness to reduce aliasing of specular highlights

		float4 bestSample = float4( vPositionWS + reflVec * bestLen, 1 );

		// Reprojection
		float4 reprojPos = mul(cbRefl.mViewProjPrev, bestSample);
		float2 prevTC = saturate(reprojPos.xy / reprojPos.w);

		const float borderSize = 0.070;  // Fade out at borders
    float screenAspectRatio = CV_ScreenSize.w / CV_ScreenSize.z;
    float remappedPrevTCX = prevTC.x * (screenAspectRatio / NativeAspectRatio); // LUMA FT: fixed border checks not accounting for wider aspect ratios and FOVs
		float borderDist = min(remappedPrevTCX, prevTC.y);
    remappedPrevTCX = 1.f - ((1.f - prevTC.x) * (screenAspectRatio / NativeAspectRatio));
		borderDist = min( 1 - max(remappedPrevTCX, prevTC.y), borderDist );
		float edgeWeight = borderDist > borderSize ? 1 : sqrt(borderDist / borderSize);

    prevTC *= cbRefl.screenScalePrev;
    // LUMA FT: this smapler had a border color (black), though given that we scaled the resolution clamped UVs, we never got that.
    // We fixed it by branching on samples that wouldn't touch any texel within the render resolution area.
    // It's unclear whether this actually is correct and helps visually, and whether the alpha should be forced to zero in that case too.
    sampleUVClamp = cbRefl.screenScalePrevClamp + (CV_ScreenSize.zw * 2.0);
    if (prevTC.x < sampleUVClamp.x && prevTC.y < sampleUVClamp.y)
    {
		  prevTC = ClampScreenTC(prevTC, cbRefl.screenScalePrevClamp);
      // LUMA FT: this sampler has a border color (black?), though we clamp the UV so we never get that
		  color.rgb = min( reflectionPreviousSceneTex.SampleLevel(ssReflectionLinearBorder, prevTC, 0).rgb, maxLum.xxx );
      
      // Filter out NANs that we still have sometimes, otherwise they get propagated and remain in the view
      color.rgb = isfinite( color.rgb ) ? color.rgb: 0;
    }
    else
    {
      color.rgb = 0; //TODOFT: test more. It seems fine
    }

		color.a = edgeWeight * dirAtten;  // Fade out where less information available
	}

	outColor = color;
}