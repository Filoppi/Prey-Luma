cbuffer PER_MATERIAL : register(b3)
{
  float4 MatDifColor : packoffset(c0);
  float4 MatEmiColor : packoffset(c2);
  float4 CM_DetailTilingAndAlphaRef : packoffset(c4);
  float3 __0bendDetailFrequency__1bendDetailLeafAmplitude__2bendDetailBranchAmplitude__3 : packoffset(c15);
  float __0RefrBumpScale__1__2__3 : packoffset(c19);
  float2 __0__1GlobalIlluminationAmount__2__3 : packoffset(c21);
}

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

SamplerState ssMaterialAnisoHigh_s : register(s0);
SamplerState ssPointClamp_s : register(s9);
Texture2D<float4> diffuseTex : register(t0);
Texture2D<float4> normalsTex : register(t1);
Texture2D<float4> sceneCopyTex : register(t2);

#define cmp -

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  float4 v6 : TEXCOORD5,
  float4 v7 : TEXCOORD6,
  float4 v8 : TEXCOORD7,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xyzw = diffuseTex.Sample(ssMaterialAnisoHigh_s, v1.xy).xyzw;
  r0.w = -v2.y + r0.w;
  r1.x = cmp(0.00400000019 >= r0.w);
  if (r1.x != 0) discard;
  r1.xy = CV_ScreenSize.zw * v0.xy;
  r1.xy += r1.xy; // * 2
  r0.w = min(v2.z, r0.w);
  r0.w = saturate(v2.x * r0.w);
  r0.xyz = MatDifColor.xyz * r0.xyz;
  r1.zw = normalsTex.Sample(ssMaterialAnisoHigh_s, v1.xy).xy;
  r1.zw = __0RefrBumpScale__1__2__3 * r1.wz;
  r1.xy = (r1.zw * v2.xx * CV_HPosScale.xy) + r1.xy; // LUMA FT: fixed refraction offsetting textures more if dynamic resolution scaling was active //TODOFT: there's many many others
  r1.xy = max(float2(0,0), r1.xy);
  r1.xy = min(CV_HPosClamp.xy, r1.xy);
  r1.xyz = sceneCopyTex.Sample(ssPointClamp_s, r1.xy).xyz;
  r0.xyz = r0.www * r0.xyz;
  r0.xyz = r0.xyz * MatEmiColor.xyz + r1.xyz;
  o0.xyz = v5.xyz * r0.xyz;
  o0.w = r0.w;
}