cbuffer PER_BATCH : register(b0)
{
  float4 vClearColor : packoffset(c0);
}

// LUMA: Unchanged.
void main(
  out float4 o0 : SV_Target0)
{
  o0.xyzw = vClearColor.xyzw;
  return;
}