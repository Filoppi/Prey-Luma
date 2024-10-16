// screenQuadVS
void main(
  float3 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Position0,
  out float2 o1 : TEXCOORD0)
{
  o0.xy = v1.xy * float2(2,2) + float2(-1,-1);
  o0.zw = float2(0,1);
  o1.xy = v1.xy * float2(1,-1) + float2(0,1);
  return;
}