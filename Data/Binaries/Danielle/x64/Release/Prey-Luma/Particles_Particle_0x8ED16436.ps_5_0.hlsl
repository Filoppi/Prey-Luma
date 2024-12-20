cbuffer PER_INSTANCE : register(b1)
{
  float4 SceneSelection : packoffset(c0);
  float4 ParticleLightParams : packoffset(c1);
}

cbuffer PER_MATERIAL : register(b3)
{
  float4 MatDifColor : packoffset(c0);
  float4 MatEmiColor : packoffset(c2);
  float4 CM_DetailTilingAndAlphaRef : packoffset(c4);
  float3 __0bendDetailFrequency__1bendDetailLeafAmplitude__2bendDetailBranchAmplitude__3 : packoffset(c15);
  float4 __0RefrBumpScale__1AnimSpeed__2PerturbationScale__3PerturbationStrength : packoffset(c19);
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

cbuffer CBPerFrame : register(b6)
{
  row_major float4x4 CF_ShadowSampling_TexGen0 : packoffset(c0);
  row_major float4x4 CF_ShadowSampling_TexGen1 : packoffset(c4);
  row_major float4x4 CF_ShadowSampling_TexGen2 : packoffset(c8);
  row_major float4x4 CF_ShadowSampling_TexGen3 : packoffset(c12);
  float4 CF_ShadowSampling_InvShadowMapSize : packoffset(c16);
  float4 CF_ShadowSampling_DepthTestBias : packoffset(c17);
  float4 CF_ShadowSampling_OneDivFarDist : packoffset(c18);
  float4 CF_ShadowSampling_KernelRadius : packoffset(c19);
  float4 CF_VolumetricFogParams : packoffset(c20);
  float4 CF_VolumetricFogRampParams : packoffset(c21);
  float4 CF_VolumetricFogSunDir : packoffset(c22);
  float4 CF_FogColGradColBase : packoffset(c23);
  float4 CF_FogColGradColDelta : packoffset(c24);
  float4 CF_FogColGradParams : packoffset(c25);
  float4 CF_FogColGradRadial : packoffset(c26);
  float4 CF_VolumetricFogSamplingParams : packoffset(c27);
  float4 CF_VolumetricFogDistributionParams : packoffset(c28);
  float4 CF_VolumetricFogScatteringParams : packoffset(c29);
  float4 CF_VolumetricFogScatteringBlendParams : packoffset(c30);
  float4 CF_VolumetricFogScatteringColor : packoffset(c31);
  float4 CF_VolumetricFogScatteringSecondaryColor : packoffset(c32);
  float4 CF_VolumetricFogHeightDensityParams : packoffset(c33);
  float4 CF_VolumetricFogHeightDensityRampParams : packoffset(c34);
  float4 CF_VolumetricFogDistanceParams : packoffset(c35);
  float4 CF_VolumetricFogGlobalEnvProbe0 : packoffset(c36);
  float4 CF_VolumetricFogGlobalEnvProbe1 : packoffset(c37);
  float4 CF_CloudShadingColorSun : packoffset(c38);
  float4 CF_CloudShadingColorSky : packoffset(c39);
  float CF_SSDOAmountDirect : packoffset(c40);
  float3 __padding0 : packoffset(c40.y);
  float4 CF_Timers[4] : packoffset(c41);
  float4 CF_RandomNumbers : packoffset(c45);
  float4 CF_irreg_kernel_2d[8] : packoffset(c46);
}

SamplerState ssMaterialAnisoHigh_s : register(s0);
SamplerState ssPointClamp_s : register(s9);
Texture2D<float4> diffuseTex : register(t0);
Texture2D<float4> normalsTex : register(t1);
Texture2D<float4> sceneCopyTex : register(t2);
Texture2D<float4> sceneLinearDepthTex : register(t3);
Texture2D<float4> sceneMaskLinearTex : register(t4);
Texture2D<float4> customTex : register(t9);

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
  float4 r0,r1,r2;

  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r0.x = sceneMaskLinearTex.Load(r0.xyz).x;
  r0.x = v0.w * CV_NearFarClipDist.w + -r0.x;
  r0.x = SceneSelection.x * r0.x;
  r0.x = cmp(r0.x < 0);
  if (r0.x != 0) discard;
  r0.xy = CV_ScreenSize.zw * v0.xy;
  r0.z = v0.w;
  r0.yzw = float3(2,2,1) * r0.xyz;
  r1.x = diffuseTex.Sample(ssMaterialAnisoHigh_s, v1.xy).w;
  r1.y = __0RefrBumpScale__1AnimSpeed__2PerturbationScale__3PerturbationStrength.z * v0.w;
  r1.y *= 0.05;
  r2.y = CF_Timers[asuint(CM_DetailTilingAndAlphaRef.w)].y * __0RefrBumpScale__1AnimSpeed__2PerturbationScale__3PerturbationStrength.y + 0.5;
  r0.x = ((r0.x * 2) / CV_HPosScale.x) - 0.5; // LUMA FT: fixed particles distortion result depending on resolution scaling value
  r1.z = r1.y * r0.x;
  r2.x = 0.5;
  r1.yw = r1.yy * r0.xx + r2.xy;
  r1.yw = customTex.Sample(ssMaterialAnisoHigh_s, r1.yw).xy;
  r2.xy = r1.zz * float2(1.5,1.5) + r2.xy;
  r2.xy = customTex.Sample(ssMaterialAnisoHigh_s, r2.xy).xy;
  r1.yz = r2.yx + r1.wy;
  r1.yz = __0RefrBumpScale__1AnimSpeed__2PerturbationScale__3PerturbationStrength.ww * r1.yz;
  r1.xy = r1.yz * r1.xx + v1.xy;
  r2.xyzw = diffuseTex.Sample(ssMaterialAnisoHigh_s, r1.xy).xyzw;
  r0.x = -v2.y + r2.w;
  r1.z = cmp(0.00400000019 >= r0.x);
  if (r1.z != 0) discard;
  r0.x = min(v2.z, r0.x);
  r0.x = saturate(v2.x * r0.x);
  r2.xyz = MatDifColor.xyz * r2.xyz;
  r1.xy = normalsTex.Sample(ssMaterialAnisoHigh_s, r1.xy).xy;
  r1.xy = __0RefrBumpScale__1AnimSpeed__2PerturbationScale__3PerturbationStrength.xx * r1.yx;
  r1.xy = (r1.xy * v2.xx * CV_HPosScale.xy) + r0.yz; // LUMA FT: fixed refraction offsetting textures more if dynamic resolution scaling was active
  r1.xy = max(float2(0,0), r1.xy);
  r1.xy = min(CV_HPosClamp.xy, r1.xy);
  r1.xyz = sceneCopyTex.Sample(ssPointClamp_s, r1.xy).xyz;
  r2.xyz = r2.xyz * r0.xxx;
  r1.xyz = r2.xyz * MatEmiColor.xyz + r1.xyz;
  r1.xyz = v5.xyz * r1.xyz;
  r1.xyz = r1.xyz * r0.xxx;
  r0.y = sceneLinearDepthTex.Sample(ssPointClamp_s, r0.yz).x;
  r0.y = r0.y * CV_NearFarClipDist.y + -r0.w;
  r0.y = min(r0.y, r0.w);
  r0.y = max(0, r0.y);
  r0.y = -r0.y * r0.y;
  r0.y = v2.w * r0.y;
  r0.y = 1.44269502 * r0.y;
  r0.y = exp2(r0.y);
  r0.y = 1 + -r0.y;
  o0.w = r0.x * r0.y;
  o0.xyz = r1.xyz * r0.yyy;
}