#include "include/Common.hlsl"

#define _RT_SAMPLE1 1

cbuffer PER_BATCH : register(b0)
{
  float4 VisionMtlParams : packoffset(c0);
}

SamplerState PNoiseSampler_s : register(s1);
Texture2D<float4> PNoiseSampler : register(t1);


// 3Dmigoto declarations
#define cmp -

// This draws some objects highlights directly on the back buffer, after tonemapping but before AA (the output alpha is ignored, it's adding the color anyway).
// These look best linearized by channel at the end, even if it's additive and thus the concept of gamma on additive colors is a bit fuzzy "(linear+linear) != toLinear(gamma+gamma)" (there's no way to emulate the SDR gamma space additive blends look).
void main(
  float4 WPos : SV_Position0,
  float4 baseTC : TEXCOORD0,
  float4 vInView : TEXCOORD1,
  float4 vInNormal : TEXCOORD2,
  nointerpolation float4 cVision : TEXCOORD3,
  bool bIsFrontFace : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
#if 0 //TODOFT2: do this?
	float fGlintSpeed = VisionMtlParams.x;
	float fGlintIntensity = VisionMtlParams.y;
	float fTimeOffset = VisionMtlParams.z;
	float fGlintDuration = VisionMtlParams.w;

	float fNoiseOffset = fTimeOffset * fGlintSpeed;
	float fTime01 = (fTimeOffset / fGlintDuration);
	float fFadeScale = saturate( 1.2f * sin(fTime01 * 3.1415926) );

  half3 vNormal = vInNormal.xyz * (bIsFrontFace ? 1.0f : -1.0f);
  half3 vView = normalize( -vInView.xyz );

  half fEdotN = saturate( dot( vView.xyz, vNormal ) );
  fEdotN  = (1.0 - fEdotN) ;
  fEdotN  = clamp(fEdotN, .5, 1);

  // Smooth interlace
  half fInterlace = abs( frac( WPos.y * 0.35 ) *2-1 ) * 0.5 + 0.5;

  // Test using inverse cloud noise for interesting electrical look
  half fNoise = PNoiseSampler.Sample(PNoiseSampler_s, baseTC.xy).x;
  half fAnimNoise = abs( frac(fNoise + fNoiseOffset) - 0.5 );

  o0 = half4( fInterlace * cVision.xyz * fEdotN * fAnimNoise * fGlintIntensity * fFadeScale, 1 );
#else
  float4 r0,r1;

  r0.x = dot(-vInView.xyz, -vInView.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = -vInView.xyz * r0.xxx;
  r0.w = bIsFrontFace ? 1 : -1;
  r1.xyz = vInNormal.xyz * r0.www;
  r0.x = saturate(dot(r0.xyz, r1.xyz));
  r0.x = 1 + -r0.x;
  r0.x = max(0.5, r0.x);
  r0.y = 0.349999994 * WPos.y;
  r0.y = frac(r0.y);
  r0.y = r0.y * 2 + -1;
  r0.y = abs(r0.y) * 0.5 + 0.5;
  r0.yzw = cVision.xyz * r0.yyy;
  r0.xyz = r0.yzw * r0.xxx;
  r0.w = PNoiseSampler.Sample(PNoiseSampler_s, baseTC.xy).x;
  r0.w = VisionMtlParams.z * VisionMtlParams.x + r0.w;
  r0.w = frac(r0.w);
  r0.w = -0.5 + r0.w;
  r0.xyz = r0.xyz * abs(r0.www);
  r0.xyz = VisionMtlParams.yyy * r0.xyz;
  r0.w = VisionMtlParams.z / VisionMtlParams.w;
  r0.w = 3.1415925 * r0.w;
  r0.w = sin(r0.w);
  r0.w = saturate(1.20000005 * r0.w);
  //TODOFT1: test ark glint (mostly done) and highlight effects
  o0.xyz = r0.xyz * r0.www; // Pre-multiplied alpha (it doesn't seem to do anything as alpha is 1)
  o0.w = 1;
  // We could call "ConditionalLinearizeUI()", though "LumaUIData.AlphaBlendState" here seems to be 1.
  o0 = SDRToHDR(o0);
#if !ENABLE_ARK_CUSTOM_POST_PROCESS
  o0 = 0;
#endif
#endif
}