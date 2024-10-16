#include "include/Common.hlsl"

cbuffer PER_BATCH : register(b0)
{
  float4 StarSizeIntensity : packoffset(c0);
}

cbuffer PER_INSTANCE : register(b1)
{
  row_major float3x4 ObjWorldMatrix : packoffset(c0);
}

#include "include/CBuffer_PerViewGlobal.hlsl"

// ArkSunVS
void main(
  uint vertexIdx : SV_VertexID0,
  out float4 HPosition : SV_Position0,
  out float4 PosInQuad : TEXCOORD0)
{
	uint vertexInQuad = vertexIdx % 4;
	float2 offset = float2( (vertexInQuad % 2) ? 1 : -1, (vertexInQuad & 2) ? -1 : 1);

  float4x4 InstMatrix = float4x4( float4(1, 0, 0, 0),
                                  float4(0, 1, 0, 0),
                                  float4(0, 0, 1, 0),
                                  float4(0, 0, 0, 1) );
  InstMatrix[0] = ObjWorldMatrix[0];
  InstMatrix[1] = ObjWorldMatrix[1];
  InstMatrix[2] = ObjWorldMatrix[2];

  float4 vWorldPos = mul( InstMatrix, float4(0,1,0,0) );
  HPosition = mul(CV_ViewProjZeroMatr, vWorldPos); // Note: we're already oriented towards the sun position

  // LUMA FT: fixed up sun growing in size when using a rendering resolution scale
  float2 screenResolution = CV_ScreenSize.xy / CV_HPosScale.xy;
	HPosition = HPosition.xyww + float4(offset * StarSizeIntensity.xy * CV_HPosScale.xy * (screenResolution.y / BaseVerticalResolution) * HPosition.w, 0, 0);
	PosInQuad.xy = offset;
	PosInQuad.zw = StarSizeIntensity.ww;
  return;
}