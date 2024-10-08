#include "include/Common.hlsl"

#include "include/CBuffer_PerViewGlobal.hlsl"

float4 GetHPos_FromTriVertexID(uint vertexID)
{
	return float4(float2(((vertexID << 1) & 2) * 2.0, (vertexID == 0) ? -4.0 : 0.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float2 GetBaseTC_FromTriVertexID(uint vertexID)
{
	return float2((vertexID << 1) & 2, (vertexID == 0) ? 2.0 : 0.0);
}

float2 GetScaledScreenTC(float2 TC) // Similar to "MapViewportToRaster()"
{
	return TC * CV_HPosScale.xy;
}

// LUMA: Unchanged.
// FullscreenTriVS
// This shader is called by many "post process" (and similar) effects.
// After DLSS runs though, it's only called by a couple passes, notably "UpscaleImagePS" (is it?) and "PostAAComposites_PS".
// Similar to "FullscreenTriScaledVS".
void main(
  uint VertexID : SV_VertexID0,
  out float4 HPosition : SV_Position0,
  out float4 baseTC : TEXCOORD0)
{
	HPosition = GetHPos_FromTriVertexID(VertexID);
	baseTC.xy = GetBaseTC_FromTriVertexID(VertexID);
  baseTC.zw = 0;
  return;
}