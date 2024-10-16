cbuffer PER_BATCH : register(b0)
{
  float4 texToTexParams0 : packoffset(c0);
  float4 texToTexParams1 : packoffset(c1);
}

SamplerState _tex0_s : register(s0);
Texture2D<float4> _tex0 : register(t0);

// LUMA: Unchanged.
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xyzw = texToTexParams0.xyzw + v1.xyxy;
  r0.x = _tex0.Sample(_tex0_s, r0.xy).x;
  r0.y = _tex0.Sample(_tex0_s, r0.zw).x;
  r1.xyzw = texToTexParams1.xyzw + v1.xyxy;
  r0.z = _tex0.Sample(_tex0_s, r1.xy).x;
  r0.w = _tex0.Sample(_tex0_s, r1.zw).x;
  r0.xy = min(r0.xy, r0.zw);
  o0.xyzw = min(r0.xxxx, r0.yyyy);
  return;
}