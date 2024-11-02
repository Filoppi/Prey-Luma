#include "include/Common.hlsl"

cbuffer PER_BATCH : register(b0)
{
  float2 SkyDome_NightSkyZenithColShift : packoffset(c0);
  float3 SkyDome_PartialMieInScatteringConst : packoffset(c1);
  float3 SkyDome_NightSkyColBase : packoffset(c2);
  float3 SkyDome_PhaseFunctionConstants : packoffset(c3);
  float3 SkyDome_PartialRayleighInScatteringConst : packoffset(c4);
  float3 SkyDome_SunDirection : packoffset(c5);
  float3 SkyDome_NightSkyColDelta : packoffset(c6);
}

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState skyDomeSamplerMie_s : register(s0);
SamplerState skyDomeSamplerRayleigh_s : register(s1);
Texture2D<float4> skyDomeSamplerMie : register(t0);
Texture2D<float4> skyDomeSamplerRayleigh : register(t1);

#define g_PS_SunLightDir           CV_SunLightDir
#define PS_HDR_RANGE_ADAPT_MAX g_PS_SunLightDir.w

// This draws the sky (e.g. blue) and the sun
void main(
  float4 Position : SV_Position0,
  float2 inBaseTC : TEXCOORD0,
  float3 inSkyDir : TEXCOORD1,
  out float4 outColor : SV_Target0)
{
#if 1 //!%NO_DAY_SKY_GRADIENT
    float2 baseTC = inBaseTC;
#endif

#if 1 //!%NO_DAY_SKY_GRADIENT || !%NO_NIGHT_SKY_GRADIENT
  float3 skyDir = normalize(inSkyDir);
#endif

  float4 Color = float4(0, 0, 0, 1);

// Draws the sun
#if 1 //!%NO_DAY_SKY_GRADIENT
  float4 ColorMie = skyDomeSamplerMie.Sample(skyDomeSamplerMie_s, baseTC.xy );
  float4 ColorRayleigh = skyDomeSamplerRayleigh.Sample(skyDomeSamplerRayleigh_s, baseTC.xy );

  float miePart_g_2 = SkyDome_PhaseFunctionConstants.x;  // = pow(miePart, -2/3) * ( -2*g )
  float miePart_g2_1 = SkyDome_PhaseFunctionConstants.y; // = pow(miePart, -2/3) * ( 1 + g*g )
  
  float cosine = -dot( SkyDome_SunDirection, skyDir );
  float cosine2 = cosine * cosine;

  float miePhase = ( 1.0 + cosine2 ) * pow( miePart_g2_1 + miePart_g_2 * cosine, -1.5 );
  float rayleighPhase = 0.75 * ( 1.0 + cosine2 );
  
  Color.xyz = ColorMie.rgb * SkyDome_PartialMieInScatteringConst * miePhase + ColorRayleigh.rgb * SkyDome_PartialRayleighInScatteringConst * rayleighPhase;
#if SUNSHAFTS_LOOK_TYPE > 0 // LUMA FT: make the sun bigger to make it more realistic and more "HDR" (this also increases the intensity of sun shafts). The sun is only used in a couple scenes in Prey, as most of the times it's just a fixed sprite.
  // Alternatively we could directly scale "rayleighPhase" and the pow coefficent of "miePhase", but it doesn't look as good as this
  Color.xyz *= 2.0;
#endif
#endif

// Draws the sky atmosphere
#if 1 // !%NO_NIGHT_SKY_GRADIENT
  // add horizontal night sky gradient
  float gr = saturate( skyDir.z * SkyDome_NightSkyZenithColShift.x + SkyDome_NightSkyZenithColShift.y );
  gr *= 2 - gr;
  float3 additiveSkyColor = SkyDome_NightSkyColBase + SkyDome_NightSkyColDelta * gr;
  Color.xyz += additiveSkyColor; 
#endif

  // Re-scale range
  Color.xyz *= PS_HDR_RANGE_ADAPT_MAX;
	
#if 0 // LUMA FT: somewhat related to "ANTICIPATE_SUNSHAFTS", disabling the code might help with sun shafts anyway
  Color.xyz = min(Color.xyz, (float3)16384.0); // LUMA FT: this seems like a random peak number
#endif
  outColor = Color;

  return;
}