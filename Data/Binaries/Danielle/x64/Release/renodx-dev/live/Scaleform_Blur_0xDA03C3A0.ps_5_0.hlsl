// ---- Created with 3Dmigoto v1.3.16 on Thu Jun 27 00:11:51 2024

cbuffer PER_BATCH : register(b0)
{
  row_major float2x4 cBitmapColorTransform : packoffset(c0);
  float2 cBlurFilterScale : packoffset(c2);
  float4 cBlurFilterSize : packoffset(c3);
}

SamplerState texMap0_s : register(s0);
Texture2D<float4> texMap0 : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = float4(0,0,0,0);
  r1.x = -cBlurFilterSize.x;
  while (true) {
    r1.y = cmp(cBlurFilterSize.x < r1.x);
    if (r1.y != 0) break;
    r1.yz = r1.xx * cBlurFilterScale.xy + v1.xy;
    r2.xyzw = texMap0.SampleLevel(texMap0_s, r1.yz, 0).xyzw;
    r0.xyzw = r2.xyzw + r0.xyzw;
    r1.x = 1 + r1.x;
  }
  r0.xyzw = cBlurFilterSize.wwww * r0.xyzw;
  r1.xyzw = cBitmapColorTransform._m10_m11_m12_m13 * r0.wwww;
  o0.xyzw = r0.xyzw * cBitmapColorTransform._m00_m01_m02_m03 + r1.xyzw;
  return;
}