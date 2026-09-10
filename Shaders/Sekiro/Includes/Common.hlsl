#include "GameCBuffers.hlsl"
#include "../../Includes/Common.hlsl"
#include "Settings.hlsl"

#define GS LumaSettings.GameSettings
#define HDR_ENABLED LumaSettings.DisplayMode == 1
#define HDR_PEAK PeakWhiteNits / GamePaperWhiteNits
#define HDR_INTSCALING GamePaperWhiteNits / UIPaperWhiteNits
#define HDR_SHOULDERSTART GS.TonemapperRolloffStart / GamePaperWhiteNits
#define HDR_MAXEXPECTED GS.TonemapperMaxExpected / GamePaperWhiteNits

/////////////////////////////////////////////////////////////////////////////////////

float3 RestoreHueAndChrominanceUcsInternal(float3 targetUcs, float3 sourceUcs, float currentChrominance, float hueStrength, float chrominanceStrength, float minChromaRatio = 0.f)
{
  if (targetUcs.x == 0) return targetUcs;

  if (hueStrength != 0.0)
  {
    const float chrominancePre = currentChrominance;
    targetUcs.yz = lerp(targetUcs.yz, sourceUcs.yz, hueStrength);
    const float chrominancePost = length(targetUcs.yz);
    float chrominanceRatio = safeDivision(chrominancePre, chrominancePost, 1);
    targetUcs.yz *= chrominanceRatio;
  }

  if (chrominanceStrength != 0.0)
  {
    const float sourceChrominance = length(sourceUcs.yz);
    float targetChrominanceRatio = safeDivision(sourceChrominance, currentChrominance, 1);
    targetChrominanceRatio = clamp(targetChrominanceRatio, minChromaRatio, FLT_MAX);
    targetUcs.yz *= lerp(1.0, targetChrominanceRatio, chrominanceStrength);
  }

  return targetUcs;
}

float3 RestoreHueAndChrominanceUcs(float3 targetUcs, float3 sourceUcs, float hueStrength, float chrominanceStrength, float minChromaRatio = 0.f)
{
  return RestoreHueAndChrominanceUcsInternal(targetUcs, sourceUcs, length(targetUcs.yz), hueStrength, chrominanceStrength, minChromaRatio);
}

/////////////////////////////////////////////////////////////////////////////////////

// https://www.desmos.com/calculator/vyegkra6zo
float ReinhardSekiro(float x, float4 g_ReinhardParam, float4 g_ToneMapParam)
{
  float2 r0;
  r0.x = (0.2 * x) / (1 - x);
  r0.x = g_ReinhardParam.y * r0.x;
  r0.x = pow(r0.x, g_ReinhardParam.x);
  r0.y = 1 + r0.x;
  r0.x = r0.x / r0.y;
  r0.x = pow(r0.x, rcp(g_ToneMapParam.y));
  return r0.x;
}

float3 ReinhardSekiro(float3 x, float4 g_ReinhardParam, float4 g_ToneMapParam)
{
  return float3(
    ReinhardSekiro(x.x, g_ReinhardParam, g_ToneMapParam),
    ReinhardSekiro(x.y, g_ReinhardParam, g_ToneMapParam),
    ReinhardSekiro(x.z, g_ReinhardParam, g_ToneMapParam)
  );
}

/////////////////////////////////////////////////////////////////////////////////////

// in/out gamma encoded BT709
float3 HDRTonemap(float3 x) {
  float p = HDR_PEAK;

  x = gamma_sRGB_to_linear(x);

  #if TONEMAP_BT2020 == 1
    x = BT709_To_BT2020(x);
  #endif

  // x = x / ((x / p) + 1); //reinhard
  x = (x * p) * rsqrt(x * x + p * p); //neutwo

  #if TONEMAP_BT2020 == 1
    x = BT2020_To_BT709(x);
  #endif

  x = linear_to_sRGB_gamma(x);

  return x;
}

// cbuffer cbSceneParam : register(b8)
// {
//   float4 FC_FarClipInfo : packoffset(c0);
//   float4 FC_ScreenSize : packoffset(c1);
//   float4 FC_DepthComputeParam : packoffset(c2);
//   float4 SC_CameraPos : packoffset(c3); // isnt affected by jitter
//   float4 VC_ClipPlane : packoffset(c4);
//   float4x3 FC_MatrixView : packoffset(c5);
//   float4x4 FC_MatrixInvViewProj : packoffset(c8);
//   float4x4 FC_MatrixInvProj : packoffset(c12);
//   float4x4 FC_MatrixProj : packoffset(c16);
//   float4x4 FC_MatrixInvView : packoffset(c20);
//   float4x4 VC_MatrixViewProj : packoffset(c24);
//   float4 FC_ShadowMapParam : packoffset(c28);
//   float4 FC_ShadowColor : packoffset(c29);
//   float4 FC_ShadowDir : packoffset(c30);
//   float4x4 VC_ShadowMapMatrix : packoffset(c31);
//   float FC_ShadowDepthRate : packoffset(c35);
//   float FC_ShadowDepthScale : packoffset(c35.y);
//   float FC_ShadowKernelScale : packoffset(c35.z);
//   float FC_ShadowRandTexInvScale : packoffset(c35.w);
//   float FC_ShadowTexInvScaleX : packoffset(c36);
//   float FC_ShadowTexInvScaleY : packoffset(c36.y);
//   float FC_ShadowDepthOffsetScale : packoffset(c36.z);
//   float4 FC_ShadowViewKernelScale : packoffset(c37);
//   float4x4 FC_ProjSpaceToShadowMatrix[4] : packoffset(c38);
//   float4 FC_CascadeSelectDist : packoffset(c54);
//   float FC_FrustumNear : packoffset(c55);
//   float FC_FrustumSliceScale : packoffset(c55.y);
//   float FC_FrustumSliceBias : packoffset(c55.z);
//   uint FC_LightTile_Width : packoffset(c56);
//   uint FC_LightTile_NumCellX : packoffset(c56.y);
//   float4 FC_CloudShadowParam0 : packoffset(c57);
//   float4 FC_CloudShadowParam1 : packoffset(c58);
//   float FC_ShadowScale0 : packoffset(c59);
//   float FC_ShadowScale1 : packoffset(c59.y);
//   bool FC_InsideWaterMode : packoffset(c59.z);
//   bool FC_CameraFadeEnable : packoffset(c59.w);
//   float4 FC_VelocityEncodeParam : packoffset(c60);
//   float4 SC_RenderCameraPos : packoffset(c61); // isnt affected by jitter
//   float SC_ScreenResolutionScale : packoffset(c62);
//   float SC_ViewFrustumEpsilon : packoffset(c62.y);
//   float SC_PreExposure : packoffset(c62.z);
//   float SC_InvPreExposure : packoffset(c62.w);
//   float4 SC_ViewFrustumPlanes[4] : packoffset(c63);
//   float4 FC_VolumeFogParam[4] : packoffset(c67);
// }
