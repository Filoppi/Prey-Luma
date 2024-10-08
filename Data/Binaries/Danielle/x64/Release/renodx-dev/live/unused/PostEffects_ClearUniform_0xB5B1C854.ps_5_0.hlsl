cbuffer PER_BATCH : register(b0)
{
  float4 vClearParam : packoffset(c0);
}

// LUMA: Unchanged.
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  o0.xyzw = vClearParam.xyzw;
  return;
}