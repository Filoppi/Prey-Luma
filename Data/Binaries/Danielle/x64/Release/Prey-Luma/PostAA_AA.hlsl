#include "include/Common.hlsl"

#include "include/CBuffer_PerViewGlobal.hlsl"

cbuffer CBPostAA : register(b0)
{
  struct
  {
	// Reprojection from this frame view projection matrix to the previous' frame one. Does not acknowledge jitters from any of the two frames.
	// The same variable is also called called "mReprojection" (vanilla CryEngine) or "mViewProjPrev" (Prey) or "PrevViewProjMatrix" (Prey) in vanilla CryEngine and in the motion blur passes.
    row_major float4x4 matReprojection;
	// sharpening (SMAA 1TX only), unused, TAA Falloff Hi-Freq (SMAA 1TX only), TAA Falloff Low-Freq (SMAA 1TX only).
	float4 params;
	// XY res, WZ inv res. This is the final/output resolution, not the rendering one, even if AA runs before before upscaling/downscaling (the texture size always matches the output res, but only a top left portion of it is used). xy is equal to "CV_ScreenSize.xy / CV_HPosScale.xy", and zw is equal to "2 * CV_ScreenSize.zw".
    float4 screenSize;
	// XYZ only. Unused.
	float4 worldViewPos;
	// Unused.
    float4 fxaaParams;
  } cbPostAA : packoffset(c0);
}

SamplerState ssPostAALinear : register(s0);
// LUMA FT: according to CryEngine source code (a different version) this might be the previous scene texture too during scope zoom transitions (though that wouldn't make much sense and it would make everything blurry?)
Texture2D<float4> PostAA_CurrentSceneTex : register(t0);
Texture2D<float4> PostAA_PreviousSceneTex : register(t1);
Texture2D<float> PostAA_DepthTex : register(t2); // LUMA FT: unused. This is the near/far direct depth (0 being the zero (a zero distance from the camera, not the near), and 1 being the far)
Texture2D<float2> PostAA_VelocityObjectsTex : register(t3);
Texture2D<float> PostAA_DeviceDepthTex : register(t16); // LUMA FT: This is the inverse depth (1 being the near, and 0 being the far). This is actually a R24G8_TYPELESS, and it's the depth used by the stencil (I think)

#define FXAA_EXTREME_QUALITY

#ifdef FXAA_EXTREME_QUALITY

	// extreme quality
  #define FXAA_QUALITY__PS 12
  #define FXAA_QUALITY__P0 1.0
  #define FXAA_QUALITY__P1 1.0
  #define FXAA_QUALITY__P2 1.0
  #define FXAA_QUALITY__P3 1.0
  #define FXAA_QUALITY__P4 1.0
  #define FXAA_QUALITY__P5 1.5
  #define FXAA_QUALITY__P6 2.0
  #define FXAA_QUALITY__P7 2.0
  #define FXAA_QUALITY__P8 2.0
  #define FXAA_QUALITY__P9 2.0
  #define FXAA_QUALITY__P10 4.0
  #define FXAA_QUALITY__P11 8.00

#else

	// default quality
	#define FXAA_QUALITY__PS 5
	#define FXAA_QUALITY__P0 1.0
	#define FXAA_QUALITY__P1 1.5
	#define FXAA_QUALITY__P2 2.0
	#define FXAA_QUALITY__P3 4.0
	#define FXAA_QUALITY__P4 12.0

#endif

float2 ClampScreenTC(float2 TC, float2 maxTC)
{
	return clamp(TC, 0, maxTC.xy);
}

float2 ClampScreenTC(float2 TC)
{
	return ClampScreenTC(TC, CV_HPosClamp.xy);
}

float2 ClampPreviousScreenTC(float2 TC)
{
	return ClampScreenTC(TC, CV_HPosClamp.zw);
}

float4 SampleCurrentScene(float2 _tc, float2 _pixelOffset = float2(0, 0))
{
	float2 tc = _tc + _pixelOffset * cbPostAA.screenSize.zw;
	return PostAA_CurrentSceneTex.SampleLevel(ssPostAALinear, ClampScreenTC(tc), 0);
}

float4 SamplePreviousScene(float2 _tc, float2 _pixelOffset = float2(0,0))
{
	float2 tc = _tc + _pixelOffset * cbPostAA.screenSize.zw;
	return PostAA_PreviousSceneTex.SampleLevel(ssPostAALinear, ClampPreviousScreenTC(tc), 0);
}

float4 sampler2DTop(float2 p) 
{
	float2 tc = ClampScreenTC(p);
	return SampleCurrentScene(tc);
}

float4 sampler2DOff(float2 p, float2 o, float2 r) 
{
	float2 tc = ClampScreenTC(p + (o * r));
	return SampleCurrentScene(tc);
}

float2 DecodeMotionVector(float2 vMotionEncoded, bool bFastEncoded = false)
{
	if (bFastEncoded)
		return vMotionEncoded;

	vMotionEncoded.xy = (vMotionEncoded.xy - 127.f/255.f) * 2.0f;
	return (vMotionEncoded.xy * vMotionEncoded.xy) * (vMotionEncoded.xy>=0.0f ? float2(1, 1) : float2(-1, -1)); // LUMA FT: corrected ">0"
}

#define VELOCITY_OBJECTS_FLOAT true

// AA motion vectors are stored in float textures so a velocity of zero already corresponds to a value of zero, they don't need decoding
float2 ReadVelocityObjects(float2 _value)
{
	return DecodeMotionVector(_value, VELOCITY_OBJECTS_FLOAT);
}

float GetLinearDepth(float fLinearDepth, bool bScaled = false)
{
    return fLinearDepth * (bScaled ? CV_NearFarClipDist.y : 1.0f); // Note: dividing by CV_NearFarClipDist.w is possibly more correct
}

float GetLinearDepth(Texture2D depthTexture, int3 vPixCoord, bool bScaled = false)
{
	float fDepth = depthTexture.Load(vPixCoord).x;
	return GetLinearDepth(fDepth, bScaled);
}

float3 ReconstructWorldPos(uint2 WPos, float linearDepth, bool bRelativeToCamera = false)
{
	float4 wposScaled = float4(WPos * linearDepth, linearDepth, bRelativeToCamera ? 0.0 : 1.0);
	return mul(CV_ScreenToWorldBasis, wposScaled);
}

float2 CalcPreviousTC(float2 _baseTC, float _depth)
{
#if FORCE_MOTION_VECTORS_JITTERED
	// LUMA FT: in this case, always used the jittered reprojection matrix, because we'll be removing jitters later.
	// This isn't really necessary for the MVs generation from the depth buffer, but it unifies the jitter removal code path to work under dynamic motions MVs and depth buffer generated MVs.
	const float4 vPosHPrev = mul(LumaData.ReprojectionMatrix, float4(_baseTC, _depth, 1.0));
#else
	// LUMA FT: use "fixed" matrix with jitters too when calculating motion vectors for DLSS (we wouldn't want (previous and current) jitters in the raw TAA as it would just blur things too much)
	const float4 vPosHPrev = mul(LumaSettings.DLSS ? LumaData.ReprojectionMatrix : cbPostAA.matReprojection, float4(_baseTC, _depth, 1.0));
#endif
	return vPosHPrev.xy / vPosHPrev.w;
}

float2 GetScaledScreenTC(float2 TC)
{
	return TC * CV_HPosScale.xy;
}

float IntersectAABB(float3 rayDir, float3 rayOrg, float3 boxExt)
{
	if (length(rayDir) < 1e-6) return 1;

	// Intersection using slabs
	float3 rcpDir = rcp(rayDir);
	float3 tNeg = ( boxExt - rayOrg) * rcpDir;
	float3 tPos = (-boxExt - rayOrg) * rcpDir;
	return max(max(min(tNeg.x, tPos.x), min(tNeg.y, tPos.y)), min(tNeg.z, tPos.z));
}

float ClipHistory(float3 cHistory, float3 cM, float3 cMin, float3 cMax)
{
	// Clip color difference against neighborhood min/max AABB
	// Clipped color is cHistory + rayDir * result
	
	float3 boxCenter = (cMax + cMin) * 0.5;
	float3 boxExtents = cMax - boxCenter;
	
	float3 rayDir = cM - cHistory;
	float3 rayOrg = cHistory - boxCenter;
	
	return saturate(IntersectAABB(rayDir, rayOrg, boxExtents));
}

float HaltonSequence(uint index, uint primeBase)
{
	float invBase = 1.0 / (float)primeBase;
	float f = invBase;
	float result = 0;
	
  for (uint i = index; i > 0; i /= primeBase, f *= invBase)
  {
		result += f * (float)(i % primeBase);
  }
	return result;
}

// LUMA FT: FXAA is meant to work in gamma space, though, given it exclusively works with luminance (that is stored in our alpha channel),
// we don't need to linearize the color values nor to scale them by paper white.
float4 Fxaa3(float2 baseTC, float4 screenSize)
{
  float4 outColor = 1;

	//	Convert CryENGINE inputs to fxaa inputs
	float2 pos = GetScaledScreenTC(baseTC);
	float2 fxaaQualityRcpFrame = screenSize.zw;
	//   1.00 - upper limit (softer)
	//   0.75 - default amount of filtering
	//   0.50 - lower limit (sharper, less sub-pixel aliasing removal)
	//   0.25 - almost off
	//   0.00 - completely off
	float fxaaQualitySubpix = 0.5;
	//   0.333 - too little (faster)
	//   0.250 - low quality
	//   0.166 - default
	//   0.125 - high quality 
	//   0.063 - overkill (slower)
	float fxaaQualityEdgeThreshold = 0.166;
	//   0.0833 - upper limit (default, the start of visible unfiltered edges)
	//   0.0625 - high quality (faster)
	//   0.0312 - visible limit (slower)
	float fxaaQualityEdgeThresholdMin = 0.0833;

  float2 posM;
  posM.x = pos.x;
  posM.y = pos.y;

  float4 rgbyM = sampler2DTop(posM);

  const float lumaM = rgbyM.w;

  float lumaS = sampler2DOff(posM, float2( 0, 1), fxaaQualityRcpFrame.xy).w;
  float lumaE = sampler2DOff(posM, float2( 1, 0), fxaaQualityRcpFrame.xy).w;
  float lumaN = sampler2DOff(posM, float2( 0,-1), fxaaQualityRcpFrame.xy).w;
  float lumaW = sampler2DOff(posM, float2(-1, 0), fxaaQualityRcpFrame.xy).w;

  float maxSM = max(lumaS, lumaM);
  float minSM = min(lumaS, lumaM);
  float maxESM = max(lumaE, maxSM);
  float minESM = min(lumaE, minSM);
  float maxWN = max(lumaN, lumaW);
  float minWN = min(lumaN, lumaW);
  float rangeMax = max(maxWN, maxESM);
  float rangeMin = min(minWN, minESM);
  float rangeMaxScaled = rangeMax * fxaaQualityEdgeThreshold;
  float range = rangeMax - rangeMin;
  float rangeMaxClamped = max(fxaaQualityEdgeThresholdMin, rangeMaxScaled);
  bool earlyExit = range < rangeMaxClamped;

  if(earlyExit)
  {
    outColor = rgbyM;
    outColor.w = 1; // Force alpha to 1
    return outColor;
  }

  float lumaNW = sampler2DOff(posM, float2(-1,-1), fxaaQualityRcpFrame.xy).w;
  float lumaSE = sampler2DOff(posM, float2( 1, 1), fxaaQualityRcpFrame.xy).w;
  float lumaNE = sampler2DOff(posM, float2( 1,-1), fxaaQualityRcpFrame.xy).w;
  float lumaSW = sampler2DOff(posM, float2(-1, 1), fxaaQualityRcpFrame.xy).w;

  float lumaNS = lumaN + lumaS;
  float lumaWE = lumaW + lumaE;
  float subpixRcpRange = 1.0/range;
  float subpixNSWE = lumaNS + lumaWE;
  float edgeHorz1 = (-2.0 * lumaM) + lumaNS;
  float edgeVert1 = (-2.0 * lumaM) + lumaWE;

  float lumaNESE = lumaNE + lumaSE;
  float lumaNWNE = lumaNW + lumaNE;
  float edgeHorz2 = (-2.0 * lumaE) + lumaNESE;
  float edgeVert2 = (-2.0 * lumaN) + lumaNWNE;

  float lumaNWSW = lumaNW + lumaSW;
  float lumaSWSE = lumaSW + lumaSE;
  float edgeHorz4 = (abs(edgeHorz1) * 2.0) + abs(edgeHorz2);
  float edgeVert4 = (abs(edgeVert1) * 2.0) + abs(edgeVert2);
  float edgeHorz3 = (-2.0 * lumaW) + lumaNWSW;
  float edgeVert3 = (-2.0 * lumaS) + lumaSWSE;
  float edgeHorz = abs(edgeHorz3) + edgeHorz4;
  float edgeVert = abs(edgeVert3) + edgeVert4;

  float subpixNWSWNESE = lumaNWSW + lumaNESE;
  float lengthSign = fxaaQualityRcpFrame.x;
  bool horzSpan = edgeHorz >= edgeVert;
  float subpixA = subpixNSWE * 2.0 + subpixNWSWNESE;

  if(!horzSpan) lumaN = lumaW;
  if(!horzSpan) lumaS = lumaE;
  if(horzSpan) lengthSign = fxaaQualityRcpFrame.y;

  float subpixB = (subpixA * (1.0/12.0)) - lumaM;

  float gradientN = lumaN - lumaM;
  float gradientS = lumaS - lumaM;
  float lumaNN = lumaN + lumaM;
  float lumaSS = lumaS + lumaM;
  bool pairN = abs(gradientN) >= abs(gradientS);
  float gradient = max(abs(gradientN), abs(gradientS));
  if(pairN) lengthSign = -lengthSign;

  float subpixC = saturate(abs(subpixB) * subpixRcpRange);

  float2 posB;
  posB.x = posM.x;
  posB.y = posM.y;
  float2 offNP;
  offNP.x = (!horzSpan) ? 0.0 : fxaaQualityRcpFrame.x;
  offNP.y = ( horzSpan) ? 0.0 : fxaaQualityRcpFrame.y;
  if(!horzSpan) posB.x += lengthSign * 0.5;
  if( horzSpan) posB.y += lengthSign * 0.5;

  float2 posN;
  posN.x = posB.x - offNP.x * FXAA_QUALITY__P0;
  posN.y = posB.y - offNP.y * FXAA_QUALITY__P0;
  float2 posP;
  posP.x = posB.x + offNP.x * FXAA_QUALITY__P0;
  posP.y = posB.y + offNP.y * FXAA_QUALITY__P0;
  float subpixD = ((-2.0)*subpixC) + 3.0;
  float lumaEndN = sampler2DTop(posN).w;
  float subpixE = subpixC * subpixC;
  float lumaEndP = sampler2DTop(posP).w;

  if(!pairN) lumaNN = lumaSS;
  float gradientScaled = gradient * 1.0/4.0;
  float lumaMM = lumaM - lumaNN * 0.5;
  float subpixF = subpixD * subpixE;
  bool lumaMLTZero = lumaMM < 0.0;

  lumaEndN -= lumaNN * 0.5;
  lumaEndP -= lumaNN * 0.5;
  bool doneN = abs(lumaEndN) >= gradientScaled;
  bool doneP = abs(lumaEndP) >= gradientScaled;
  if(!doneN) posN.x -= offNP.x * FXAA_QUALITY__P1;
  if(!doneN) posN.y -= offNP.y * FXAA_QUALITY__P1;
  bool doneNP = (!doneN) || (!doneP);
  if(!doneP) posP.x += offNP.x * FXAA_QUALITY__P1;
  if(!doneP) posP.y += offNP.y * FXAA_QUALITY__P1;

  if(doneNP) 
  {
      if(!doneN) lumaEndN = sampler2DTop(posN.xy).w;
      if(!doneP) lumaEndP = sampler2DTop(posP.xy).w;
      if(!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
      if(!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
      doneN = abs(lumaEndN) >= gradientScaled;
      doneP = abs(lumaEndP) >= gradientScaled;
      if(!doneN) posN.x -= offNP.x * FXAA_QUALITY__P2;
      if(!doneN) posN.y -= offNP.y * FXAA_QUALITY__P2;
      doneNP = (!doneN) || (!doneP);
      if(!doneP) posP.x += offNP.x * FXAA_QUALITY__P2;
      if(!doneP) posP.y += offNP.y * FXAA_QUALITY__P2;

      if(doneNP) 
      {
					if(!doneN) lumaEndN = sampler2DTop(posN.xy).w;
					if(!doneP) lumaEndP = sampler2DTop(posP.xy).w;
					if(!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
					if(!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
					doneN = abs(lumaEndN) >= gradientScaled;
					doneP = abs(lumaEndP) >= gradientScaled;
					if(!doneN) posN.x -= offNP.x * FXAA_QUALITY__P3;
					if(!doneN) posN.y -= offNP.y * FXAA_QUALITY__P3;
					doneNP = (!doneN) || (!doneP);
					if(!doneP) posP.x += offNP.x * FXAA_QUALITY__P3;
					if(!doneP) posP.y += offNP.y * FXAA_QUALITY__P3;

					if(doneNP) 
					{
							if(!doneN) lumaEndN = sampler2DTop(posN.xy).w;
							if(!doneP) lumaEndP = sampler2DTop(posP.xy).w;
							if(!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
							if(!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
							doneN = abs(lumaEndN) >= gradientScaled;
							doneP = abs(lumaEndP) >= gradientScaled;
							if(!doneN) posN.x -= offNP.x * FXAA_QUALITY__P4;
							if(!doneN) posN.y -= offNP.y * FXAA_QUALITY__P4;
							doneNP = (!doneN) || (!doneP);
							if(!doneP) posP.x += offNP.x * FXAA_QUALITY__P4;
							if(!doneP) posP.y += offNP.y * FXAA_QUALITY__P4;

#ifdef FXAA_EXTREME_QUALITY

						if(doneNP) 
						{
								if(!doneN) lumaEndN = sampler2DTop(posN.xy).w;
								if(!doneP) lumaEndP = sampler2DTop(posP.xy).w;
								if(!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
								if(!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
								doneN = abs(lumaEndN) >= gradientScaled;
								doneP = abs(lumaEndP) >= gradientScaled;
								if(!doneN) posN.x -= offNP.x * FXAA_QUALITY__P5;
								if(!doneN) posN.y -= offNP.y * FXAA_QUALITY__P5;
								doneNP = (!doneN) || (!doneP);
								if(!doneP) posP.x += offNP.x * FXAA_QUALITY__P5;
								if(!doneP) posP.y += offNP.y * FXAA_QUALITY__P5;

								if(doneNP) 
								{
										if(!doneN) lumaEndN = sampler2DTop(posN.xy).w;
										if(!doneP) lumaEndP = sampler2DTop(posP.xy).w;
										if(!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
										if(!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
										doneN = abs(lumaEndN) >= gradientScaled;
										doneP = abs(lumaEndP) >= gradientScaled;
										if(!doneN) posN.x -= offNP.x * FXAA_QUALITY__P6;
										if(!doneN) posN.y -= offNP.y * FXAA_QUALITY__P6;
										doneNP = (!doneN) || (!doneP);
										if(!doneP) posP.x += offNP.x * FXAA_QUALITY__P6;
										if(!doneP) posP.y += offNP.y * FXAA_QUALITY__P6;

										if(doneNP) 
										{
												if(!doneN) lumaEndN = sampler2DTop(posN.xy).w;
												if(!doneP) lumaEndP = sampler2DTop(posP.xy).w;
												if(!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
												if(!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
												doneN = abs(lumaEndN) >= gradientScaled;
												doneP = abs(lumaEndP) >= gradientScaled;
												if(!doneN) posN.x -= offNP.x * FXAA_QUALITY__P7;
												if(!doneN) posN.y -= offNP.y * FXAA_QUALITY__P7;
												doneNP = (!doneN) || (!doneP);
												if(!doneP) posP.x += offNP.x * FXAA_QUALITY__P7;
												if(!doneP) posP.y += offNP.y * FXAA_QUALITY__P7;

												if(doneNP) 
												{
														if(!doneN) lumaEndN = sampler2DTop(posN.xy).w;
														if(!doneP) lumaEndP = sampler2DTop(posP.xy).w;
														if(!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
														if(!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
														doneN = abs(lumaEndN) >= gradientScaled;
														doneP = abs(lumaEndP) >= gradientScaled;
														if(!doneN) posN.x -= offNP.x * FXAA_QUALITY__P8;
														if(!doneN) posN.y -= offNP.y * FXAA_QUALITY__P8;
														doneNP = (!doneN) || (!doneP);
														if(!doneP) posP.x += offNP.x * FXAA_QUALITY__P8;
														if(!doneP) posP.y += offNP.y * FXAA_QUALITY__P8;


														if(doneNP) 
														{
																if(!doneN) lumaEndN = sampler2DTop(posN.xy).w;
																if(!doneP) lumaEndP = sampler2DTop(posP.xy).w;
																if(!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
																if(!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
																doneN = abs(lumaEndN) >= gradientScaled;
																doneP = abs(lumaEndP) >= gradientScaled;
																if(!doneN) posN.x -= offNP.x * FXAA_QUALITY__P9;
																if(!doneN) posN.y -= offNP.y * FXAA_QUALITY__P9;
																doneNP = (!doneN) || (!doneP);
																if(!doneP) posP.x += offNP.x * FXAA_QUALITY__P9;
																if(!doneP) posP.y += offNP.y * FXAA_QUALITY__P9;

																if(doneNP) 
																{
																		if(!doneN) lumaEndN = sampler2DTop(posN.xy).w;
																		if(!doneP) lumaEndP = sampler2DTop(posP.xy).w;
																		if(!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
																		if(!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
																		doneN = abs(lumaEndN) >= gradientScaled;
																		doneP = abs(lumaEndP) >= gradientScaled;
																		if(!doneN) posN.x -= offNP.x * FXAA_QUALITY__P10;
																		if(!doneN) posN.y -= offNP.y * FXAA_QUALITY__P10;
																		doneNP = (!doneN) || (!doneP);
																		if(!doneP) posP.x += offNP.x * FXAA_QUALITY__P10;
																		if(!doneP) posP.y += offNP.y * FXAA_QUALITY__P10;


																		if(doneNP) 
																		{
																				if(!doneN) lumaEndN = sampler2DTop(posN.xy).w;
																				if(!doneP) lumaEndP = sampler2DTop(posP.xy).w;
																				if(!doneN) lumaEndN = lumaEndN - lumaNN * 0.5;
																				if(!doneP) lumaEndP = lumaEndP - lumaNN * 0.5;
																				doneN = abs(lumaEndN) >= gradientScaled;
																				doneP = abs(lumaEndP) >= gradientScaled;
																				if(!doneN) posN.x -= offNP.x * FXAA_QUALITY__P11;
																				if(!doneN) posN.y -= offNP.y * FXAA_QUALITY__P11;
																				doneNP = (!doneN) || (!doneP);
																				if(!doneP) posP.x += offNP.x * FXAA_QUALITY__P11;
																				if(!doneP) posP.y += offNP.y * FXAA_QUALITY__P11;
																		}
																}
														}
												}
										}
								}
						}

#endif
					}
      }
  }


  float dstN = posM.x - posN.x;
  float dstP = posP.x - posM.x;
  if(!horzSpan) dstN = posM.y - posN.y;
  if(!horzSpan) dstP = posP.y - posM.y;

  bool goodSpanN = (lumaEndN < 0.0) != lumaMLTZero;
  float spanLength = (dstP + dstN);
  bool goodSpanP = (lumaEndP < 0.0) != lumaMLTZero;
  float spanLengthRcp = 1.0/spanLength;

  bool directionN = dstN < dstP;
  float dst = min(dstN, dstP);
  bool goodSpan = directionN ? goodSpanN : goodSpanP;
  float subpixG = subpixF * subpixF;
  float pixelOffset = (dst * (-spanLengthRcp)) + 0.5;
  float subpixH = subpixG * fxaaQualitySubpix;

  float pixelOffsetGood = goodSpan ? pixelOffset : 0.0;
  float pixelOffsetSubpix = max(pixelOffsetGood, subpixH);
  if(!horzSpan) posM.x += pixelOffsetSubpix * lengthSign;
  if( horzSpan) posM.y += pixelOffsetSubpix * lengthSign;

  //TODO LUMA: verify that sampling is still done within the target texel range, even if our background luminance could have been beyond the 0-1 range (due to the HDR upgrade) 
  outColor = float4(sampler2DTop(posM).xyz, lumaM);
  outColor.w = 1.0; // Force alpha to 1

  return outColor;
}