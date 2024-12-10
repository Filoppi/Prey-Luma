// ---- Created with 3Dmigoto v1.3.16 on Wed Oct 02 01:54:37 2024

cbuffer CBPerViewGlobal : register(b13)
{
  row_major float4x4 CV_ViewProjZeroMatr : packoffset(c0);
  float4 CV_AnimGenParams : packoffset(c4);
  row_major float4x4 CV_ViewProjMatr : packoffset(c5);
  row_major float4x4 CV_ViewProjNearestMatr : packoffset(c9);
  row_major float4x4 CV_InvViewProj : packoffset(c13);
  row_major float4x4 CV_PrevViewProjMatr : packoffset(c17);
  row_major float4x4 CV_PrevViewProjNearestMatr : packoffset(c21);
  row_major float3x4 CV_ScreenToWorldBasis : packoffset(c25);
  float4 CV_TessInfo : packoffset(c28);
  float4 CV_CameraRightVector : packoffset(c29);
  float4 CV_CameraFrontVector : packoffset(c30);
  float4 CV_CameraUpVector : packoffset(c31);
  float4 CV_ScreenSize : packoffset(c32);
  float4 CV_HPosScale : packoffset(c33);
  float4 CV_HPosClamp : packoffset(c34);
  float4 CV_ProjRatio : packoffset(c35);
  float4 CV_NearestScaled : packoffset(c36);
  float4 CV_NearFarClipDist : packoffset(c37);
  float4 CV_SunLightDir : packoffset(c38);
  float4 CV_SunColor : packoffset(c39);
  float4 CV_SkyColor : packoffset(c40);
  float4 CV_FogColor : packoffset(c41);
  float4 CV_TerrainInfo : packoffset(c42);
  float4 CV_DecalZFightingRemedy : packoffset(c43);
  row_major float4x4 CV_FrustumPlaneEquation : packoffset(c44);
  float4 CV_WindGridOffset : packoffset(c48);
  row_major float4x4 CV_ViewMatr : packoffset(c49);
  row_major float4x4 CV_InvViewMatr : packoffset(c53);
  float CV_LookingGlass_SunSelector : packoffset(c57);
  float CV_LookingGlass_DepthScalar : packoffset(c57.y);
  float CV_PADDING0 : packoffset(c57.z);
  float CV_PADDING1 : packoffset(c57.w);
}

Texture2D<float> LinearizeDepth_DepthTex : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r0.x = LinearizeDepth_DepthTex.Load(r0.xyz).x;
  r0.y = -CV_NearestScaled.x + r0.x;
  r0.y = CV_NearestScaled.y / r0.y;
  r0.z = -CV_ProjRatio.x + r0.x;
  r0.x = cmp(r0.x == 1.000000);
  r0.z = CV_ProjRatio.y / r0.z;
  r0.w = cmp(r0.z < CV_NearestScaled.w);
  r0.y = r0.w ? r0.y : r0.z;
  o0.x = r0.x ? 1 : r0.y;
  o0.yzw = float3(0,0,0);
  return;
}