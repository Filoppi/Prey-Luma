#include "include/Common.hlsl"

#if _3CA47504
// Mostly passthrough
#elif _5A887BAD
// v-sync effect
#define _RT_SAMPLE2 1
#elif _214945FF
// chroma shift
#define _RT_SAMPLE0 1
#define _RT_SAMPLE1 0
// v-sync effect
#define _RT_SAMPLE2 1
#elif _CBC8C051
// chroma shift
#define _RT_SAMPLE0 1
#define _RT_SAMPLE1 0
#endif

cbuffer PER_BATCH : register(b0)
{
  // None of these are scaled based on the screen's resolution
  float4 UberPostParams2 : packoffset(c0);
  float4 UberPostParams3 : packoffset(c1);
  float4 UberPostParams1 : packoffset(c2);
  float4 UberPostParams5 : packoffset(c3);
  float4 UberPostParams4 : packoffset(c4);
  float4 UberPostParams0 : packoffset(c5);
}

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState _tex0_s : register(s0);
SamplerState _tex1_s : register(s1);
SamplerState _tex2_s : register(s2);
Texture2D<float4> _tex0 : register(t0);
Texture2D<float4> _tex1 : register(t1);
Texture2D<float4> _tex2 : register(t2);

#define PS_ScreenSize CV_ScreenSize

// LUMA: this works both in linear and gamma space
void ApplyRadialBlur(inout float4 cScreen, float2 tcFinal)
{
#define RadialBlurParams UberPostParams5

	float2 vScreenPos = RadialBlurParams.xy;
  
	float2 vBlurVec = ( vScreenPos.xy - tcFinal.xy);
	// LUMA FT: matched the look to 16:9 (radial blur might still not be circular at it)
	float screenAspectRatio = CV_ScreenSize.x / CV_ScreenSize.y;
#if 1
	vBlurVec.x *= NativeAspectRatio / screenAspectRatio;
#else // Force circular radius
	vBlurVec.x /= screenAspectRatio;
#endif
  
	float fInvRadius = RadialBlurParams.z;
	float blurDist = saturate( 1- dot( vBlurVec.xy * fInvRadius, vBlurVec.xy * fInvRadius));
	RadialBlurParams.w *= blurDist*blurDist;

	vBlurVec *= RadialBlurParams.w;
  
	const int nSamples = 8; 
	const float fWeight = 1.0 / (float) nSamples;
  
	float4 cAcc = _tex0.Sample(_tex0_s, (tcFinal.xy + vBlurVec.xy ) );
	cAcc += _tex0.Sample(_tex0_s, (tcFinal.xy + vBlurVec.xy * 2 ) );
	cAcc += _tex0.Sample(_tex0_s, (tcFinal.xy + vBlurVec.xy * 3 ) );
	cAcc += _tex0.Sample(_tex0_s, (tcFinal.xy + vBlurVec.xy * 4 ) );
	cAcc += _tex0.Sample(_tex0_s, (tcFinal.xy + vBlurVec.xy * 5 ) );
	cAcc += _tex0.Sample(_tex0_s, (tcFinal.xy + vBlurVec.xy * 6 ) );
	cAcc += _tex0.Sample(_tex0_s, (tcFinal.xy + vBlurVec.xy * 7 ) );
	cAcc += _tex0.Sample(_tex0_s, (tcFinal.xy + vBlurVec.xy * 8 ) );

	cScreen = cAcc * fWeight;

#undef RadialBlurParams
}

// LUMA: this works both in linear and gamma space
void ApplyRadialBlurAndChromaShift(inout float4 cScreen, float2 tcFinal)
{
#define RadialBlurParams UberPostParams5

	float2 vScreenPos = RadialBlurParams.xy;
	
	// LUMA FT: matched the look to 16:9 (blur and chroma shift are stil not circular/squared but that's fine)
	float screenAspectRatio = CV_ScreenSize.x / CV_ScreenSize.y;
  
	float2 vBlurVec = ( vScreenPos.xy - tcFinal.xy);
	vBlurVec.x *= NativeAspectRatio / screenAspectRatio;
  
	float fInvRadius = RadialBlurParams.z;
	float blurDist = saturate( 1- dot( vBlurVec.xy * fInvRadius, vBlurVec.xy * fInvRadius));
	RadialBlurParams.w *= blurDist*blurDist;
  
	const int nSamples = 8; 
	const float fWeight = 1.0 / (float) nSamples;
  	
	float2 fChromaShiftScale = (1 - UberPostParams1.w*0.15) * float2(NativeAspectRatio / screenAspectRatio, 1.0);

	vBlurVec *= RadialBlurParams.w;
	
#if !ENABLE_CHROMATIC_ABERRATION
	fChromaShiftScale = 1;
#endif // ENABLE_CHROMATIC_ABERRATION
	
	float4 cAcc = _tex0.Sample(_tex0_s, ((tcFinal.xy + vBlurVec.xy ) -0.5) * fChromaShiftScale + 0.5 );
	cAcc += _tex0.Sample(_tex0_s, ((tcFinal.xy + vBlurVec.xy * 2 ) -0.5) * fChromaShiftScale + 0.5);
	cAcc += _tex0.Sample(_tex0_s, ((tcFinal.xy + vBlurVec.xy * 3 ) -0.5) * fChromaShiftScale + 0.5);
	cAcc += _tex0.Sample(_tex0_s, ((tcFinal.xy + vBlurVec.xy * 4 ) -0.5) * fChromaShiftScale + 0.5);
	cAcc += _tex0.Sample(_tex0_s, ((tcFinal.xy + vBlurVec.xy * 5 ) -0.5) * fChromaShiftScale + 0.5);
	cAcc += _tex0.Sample(_tex0_s, ((tcFinal.xy + vBlurVec.xy * 6 ) -0.5) * fChromaShiftScale + 0.5);
	cAcc += _tex0.Sample(_tex0_s, ((tcFinal.xy + vBlurVec.xy * 7 ) -0.5) * fChromaShiftScale + 0.5);
	cAcc += _tex0.Sample(_tex0_s, ((tcFinal.xy + vBlurVec.xy * 8 ) -0.5) * fChromaShiftScale + 0.5);
	cScreen =  cAcc * fWeight;

#if ENABLE_CHROMATIC_ABERRATION
	fChromaShiftScale = (1 - UberPostParams1.w*0.1) * float2(NativeAspectRatio / screenAspectRatio, 1.0);

	cAcc.gb = _tex0.Sample(_tex0_s, ((tcFinal.xy + vBlurVec.xy ) -0.5) * fChromaShiftScale + 0.5).gb;
	cAcc.gb += _tex0.Sample(_tex0_s, ((tcFinal.xy + vBlurVec.xy * 2 ) -0.5) * fChromaShiftScale + 0.5).gb;
	cAcc.gb += _tex0.Sample(_tex0_s, ((tcFinal.xy + vBlurVec.xy * 3 ) -0.5) * fChromaShiftScale + 0.5).gb;
	cAcc.gb += _tex0.Sample(_tex0_s, ((tcFinal.xy + vBlurVec.xy * 4 ) -0.5) * fChromaShiftScale + 0.5).gb;
	cAcc.gb += _tex0.Sample(_tex0_s, ((tcFinal.xy + vBlurVec.xy * 5 ) -0.5) * fChromaShiftScale + 0.5).gb;
	cAcc.gb += _tex0.Sample(_tex0_s, ((tcFinal.xy + vBlurVec.xy * 6 ) -0.5) * fChromaShiftScale + 0.5).gb;
	cAcc.gb += _tex0.Sample(_tex0_s, ((tcFinal.xy + vBlurVec.xy * 7 ) -0.5) * fChromaShiftScale + 0.5).gb;
	cAcc.gb += _tex0.Sample(_tex0_s, ((tcFinal.xy + vBlurVec.xy * 8 ) -0.5) * fChromaShiftScale + 0.5).gb;

	cScreen.gb = cAcc.gb * fWeight;
#endif
  
#undef RadialBlurParams
}

// UberGamePostProcessPS
// This is only called when any of the parameters are in use.
// This runs after upscaling/MSAA.
// We let these draw on top of lens distortion black borders, as it's a screen overlay
void main(float4 WPos : SV_Position0, float4 inBaseTC : TEXCOORD0, out float4 outColor : SV_Target0)
{
	outColor = 0;

	bool skipPostProcess = false;
#if !ENABLE_POST_PROCESS
	skipPostProcess = true;
#endif // ENABLE_POST_PROCESS
	if (skipPostProcess || ShouldSkipPostProcess(WPos.xy))
	{
		outColor = _tex0.Sample(_tex0_s, inBaseTC.xy);
		return;
	}
  
	/////////////////////////////////////////////////////////////////////////////////////////////////
	// Apply interlation

	float fInterlation = 1.0;

#if _RT_SAMPLE2

	float2 vInterlationRot = inBaseTC.xy;
	float fRotPhase = dot(inBaseTC.xy*2-1, inBaseTC.xy*2-1);
	float fAngle = UberPostParams0.w * (PI/180.0); // LUMA FT: correct "PI" value being approximate
	vInterlationRot = vInterlationRot.xy * cos(fAngle) + float2(-vInterlationRot.y, vInterlationRot.x) * sin(fAngle);

	// Compute interlation/vsync
	// LUMA FT: fix interlation and v-sync basically being invisible at higher resolutions (pixel lines got too small, and the v-sync wave got too fast)
	static const float BaseInterlationVerticalResolution = 1080.0; // Not based on the global "BaseVerticalResolution" as it's not a multiple of common resolutions
	float interlationVerticalResolution = CV_ScreenSize.y;
#if CORRECT_CRT_INTERLACING_SIZE
	// Find the closest matching interlation to the original size (we can't divide the resolution by anything other than 2 as it's a pixel based effect).
	// This will map 1440 to 720 (1440p is already too high to see individual lines, and scaling it to 1080 would look bad), and 2160 to 1080 etc. We halve until we are close enough to 1080p.
	int interlationVerticalResolutionScale = (interlationVerticalResolution / (BaseInterlationVerticalResolution * 1.5)) + FLT_EPSILON;
	interlationVerticalResolution /= interlationVerticalResolutionScale + 1;
#endif
	fInterlation = abs( frac(( vInterlationRot.y ) * interlationVerticalResolution * 0.25 * UberPostParams0.z) * 2 - 1) * 0.8 + 0.5;
	float fVsync = abs( frac((inBaseTC.y + UberPostParams1.x * CV_AnimGenParams.z) * BaseVerticalResolution * 0.01 ) * 2 - 1) * 0.05 + 1.0;
	fInterlation =  lerp(1, fVsync, UberPostParams0.x) * lerp( 1, fInterlation, saturate(UberPostParams0.y));

#endif // _RT_SAMPLE2

	/////////////////////////////////////////////////////////////////////////////////////////////////
	// Apply pixelation

	// Compute uvs for pixelized look
	float2 tcFinal = (ceil(inBaseTC.xy * PS_ScreenSize.xy / UberPostParams1.y + 0.5) - 0.5) * (UberPostParams1.y / (PS_ScreenSize.xy));

	// Apply sync wave
#if ENABLE_SCREEN_DISTORTION // LUMA FT: Let's pretend this is chromatic aberration
	tcFinal.x += UberPostParams3.w * (cos((inBaseTC.y * UberPostParams2.z + UberPostParams2.w * CV_AnimGenParams.z)));
#endif // ENABLE_SCREEN_DISTORTION

	/////////////////////////////////////////////////////////////////////////////////////////////////
	// Apply chroma shift

	float4 cScreen;

// LUMA FT: this isn't exactly chromatic aberration but it's close enough (this also does screen warping distortion a bit)
#if ENABLE_CHROMATIC_ABERRATION && _RT_SAMPLE0 && !_RT_SAMPLE1

	float screenAspectRatio = CV_ScreenSize.x / CV_ScreenSize.y;
  
	// LUMA FT: fixed chroma shift being broken in ultrawide (it wasn't scaling correctly by aspect ratio, "UberPostParams1.w" was fixed independently of the resolution or aspect ratio).
	// We fixed around 16:9, assuming that was the intended behaviour, even if the code seemed to target a 1:1 aspect ratio.
	float2 uvOffsetR = float2(UberPostParams1.w * 0.15 * (NativeAspectRatio / screenAspectRatio), UberPostParams1.w * 0.15);
	float2 uvOffsetGB = float2(UberPostParams1.w * 0.1 * (NativeAspectRatio / screenAspectRatio), UberPostParams1.w * 0.1);
	cScreen.ra = _tex0.Sample(_tex0_s, (tcFinal-0.5) * (1.0 - uvOffsetR) + 0.5).ra; // "a" is unused
	cScreen.gb = _tex0.Sample(_tex0_s, (tcFinal-0.5) * (1.0 - uvOffsetGB) + 0.5).gb;

#else // ENABLE_CHROMATIC_ABERRATION && _RT_SAMPLE0 && !_RT_SAMPLE1

	cScreen = _tex0.Sample(_tex0_s, tcFinal); 

#endif // ENABLE_CHROMATIC_ABERRATION

	/////////////////////////////////////////////////////////////////////////////////////////////////
	// Apply radial blur

#if !_RT_SAMPLE0 && _RT_SAMPLE1
	ApplyRadialBlur( cScreen, tcFinal);
#endif // !_RT_SAMPLE0 && _RT_SAMPLE1

	/////////////////////////////////////////////////////////////////////////////////////////////////
	// Apply radial blur and chroma shift version

#if _RT_SAMPLE0 && _RT_SAMPLE1
	ApplyRadialBlurAndChromaShift( cScreen, tcFinal );
#endif // _RT_SAMPLE0 && _RT_SAMPLE1

  // Calc noise
  float noise = 0;
#if 0 // LUMA FT: noise doesn't seem to do anything (maybe it's some kind of late dithering), so I've disabled it given that it was already accidentally disabled in the source code
	float2 noiseTC = UberPostParams4.xy + inBaseTC.xy * (PS_ScreenSize.xy/64.0) * UberPostParams2.y;
	noise = _tex1.Sample(_tex1_s, noiseTC).x;
	noise = UberPostParams2.x * (noise*2-1);
#endif

	// Apply interlation/vsync + tinting
	float3 tint = UberPostParams3.xyz;
  
	float paperWhite = 1.0;
#if POST_PROCESS_SPACE_TYPE == 1
	float3 lastLinearColor = cScreen.rgb;
	// Apply all effects in SDR gamma space
	paperWhite = GamePaperWhiteNits / sRGB_WhiteLevelNits;
	cScreen.rgb = linear_to_game_gamma(cScreen.rgb / paperWhite);
	float3 lastGammaColor = cScreen.rgb;
#endif // POST_PROCESS_SPACE_TYPE == 1

	float3 cImageFinalArtefacts = cScreen.rgb * fInterlation * tint.xyz + noise; // LUMA FT: fixed "noise" not being applied

#if POST_PROCESS_SPACE_TYPE == 1
#if HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS
	cImageFinalArtefacts = lastLinearColor + ((game_gamma_to_linear(cImageFinalArtefacts) - game_gamma_to_linear(lastGammaColor)) * paperWhite);
#else // HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS
	cImageFinalArtefacts = game_gamma_to_linear(cImageFinalArtefacts) * paperWhite;
#endif // HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS
#endif // POST_PROCESS_SPACE_TYPE == 1

	outColor.xyz = cImageFinalArtefacts;

#if TEST_TINT
	if (any(abs(tint - 1.0) > FLT_EPSILON))
	{
		outColor.xyz = float3(1, 0, 0) * paperWhite;
	}
#endif // TEST_TINT

	outColor.w = _tex2.Sample(_tex2_s, inBaseTC.xy).x; // lerp by mask with backbuffer

	return;
}