cbuffer PER_BATCH : register(b0)
{
  float4 texToTexParams0 : packoffset(c0);
  float4 texToTexParams1 : packoffset(c1);
  float4 texToTexParams2 : packoffset(c2);
}

SamplerState _tex0_s : register(s0);
Texture2D<float4> _tex0 : register(t0);

// 3Dmigoto declarations
#define cmp -

// LUMA: Unchanged.
// This is used by MSAA
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;

  r0.xy = texToTexParams1.zw + v1.xy;
  r1.xyzw = float4(0,0,0,0);
  r2.y = r0.y;
  r0.z = texToTexParams2.w;
  r0.w = 0;
  while (true) {
    r2.z = cmp(r0.z >= texToTexParams0.y);
    if (r2.z != 0) break;
    r3.xyzw = r1.xyzw;
    r2.x = r0.x;
    r2.z = texToTexParams2.z;
    r2.w = r0.w;
    while (true) {
      r4.x = cmp(r2.z >= texToTexParams0.x);
      if (r4.x != 0) break;
      r4.xy = texToTexParams0.zw * r2.xy;
      r4.xyzw = _tex0.SampleLevel(_tex0_s, r4.xy, 0).xyzw;
      r3.xyzw = r4.xyzw + r3.xyzw;
      r2.w = 1 + r2.w;
      r2.x = texToTexParams1.x + r2.x;
      r2.z = texToTexParams2.x + r2.z;
    }
    r1.xyzw = r3.xyzw;
    r0.w = r2.w;
    r2.y = texToTexParams1.y + r2.y;
    r0.z = texToTexParams2.y + r0.z;
  }
  o0.xyzw = r1.xyzw / r0.wwww;
  return;
}