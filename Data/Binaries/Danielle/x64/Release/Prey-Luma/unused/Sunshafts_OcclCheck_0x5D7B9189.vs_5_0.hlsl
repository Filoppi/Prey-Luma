// LUMA: Unchanged.
// OcclCheckVS
// If draws at least a pixel, then the sun is deemed visible (actually the engine checks if at least 1% of the rendering resolution drew (it's not clear if they correctly acknowledge resolution scaling or if they use the output resolution for that check))
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
  // LUMA FT: I tried to return vertices positions forced to be near the center of the screen, with near zero depth, but I wasn't able to force sun shafts to be visible
  return;
}