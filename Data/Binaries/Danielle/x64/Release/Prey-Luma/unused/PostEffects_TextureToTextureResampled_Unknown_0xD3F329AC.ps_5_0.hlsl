cbuffer PER_BATCH : register(b0)
{
  float4 texToTexParams0 : packoffset(c0);
  float4 texToTexParams1 : packoffset(c1);
}

SamplerState _tex0_s : register(s0);
SamplerState _tex1_s : register(s1);
Texture2D<float4> _tex0 : register(t0);
Texture2D<float4> _tex1 : register(t1);

// LUMA: Unchanged.
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xyzw = texToTexParams0.xyzw + v1.xyxy;
  r1.x = _tex0.Sample(_tex0_s, r0.xy).x;
  r1.y = _tex0.Sample(_tex0_s, r0.zw).x;
  r2.xyzw = texToTexParams1.xyzw + v1.xyxy;
  r1.z = _tex0.Sample(_tex0_s, r2.xy).x;
  r1.w = _tex0.Sample(_tex0_s, r2.zw).x;
  r1.xy = min(r1.xy, r1.zw);
  r1.x = min(r1.x, r1.y);
  r0.x = _tex1.Sample(_tex1_s, r0.xy).x;
  r0.y = _tex1.Sample(_tex1_s, r0.zw).x;
  r0.z = _tex1.Sample(_tex1_s, r2.xy).x;
  r0.w = _tex1.Sample(_tex1_s, r2.zw).x;
  r0.xy = min(r0.xy, r0.zw);
  r0.x = min(r0.x, r0.y);
  o0.xyzw = max(r0.xxxx, r1.xxxx);
  return;
}