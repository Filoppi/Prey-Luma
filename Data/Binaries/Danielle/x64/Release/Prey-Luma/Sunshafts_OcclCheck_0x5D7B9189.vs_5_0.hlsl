// LUMA: Unchanged.
// OcclCheckVS
// If draws at least a pixel, then the sun is deemed visible
//TODOFT4: test if we can help make the sun render more often if we increase the vertices size of the occlusion check? Can we even do that from shaders? should we increase the viewport size?

void main(
  float4 Position : POSITION0,
  float2 baseTC : TEXCOORD0,
  float3 CamVec : TEXCOORD1,
  out float4 o0 : SV_Position0)
{
  float4 r0;
  r0.xy = Position.xy * float2(1,-1) + float2(0,1);
  o0.xy = r0.xy * float2(2,2) + float2(-1,-1);
  o0.z = Position.z;
  o0.w = 1;
  return;
}