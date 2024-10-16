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