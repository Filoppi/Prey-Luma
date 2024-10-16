// ---- Created with 3Dmigoto v1.3.16 on Thu Jun 27 00:11:46 2024

cbuffer PER_BATCH : register(b0)
{
  row_major float2x4 cBitmapColorTransform : packoffset(c0);
  float2 cBlurFilterOffset : packoffset(c2);
  float4 cBlurFilterColor1 : packoffset(c3);
  float2 cBlurFilterScale : packoffset(c4);
  float4 cBlurFilterSize : packoffset(c5);
}

SamplerState texMap0_s : register(s0);
SamplerState texMap1_s : register(s1);
Texture2D<float4> texMap0 : register(t0);
Texture2D<float4> texMap1 : register(t1);


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

  r0.x = 0;
  r0.y = -cBlurFilterSize.x;
  while (true) {
    r0.z = cmp(cBlurFilterSize.x < r0.y);
    if (r0.z != 0) break;
    r1.x = cBlurFilterOffset.x + r0.y;
    r2.x = r0.x;
    r2.y = -cBlurFilterSize.y;
    while (true) {
      r0.z = cmp(cBlurFilterSize.y < r2.y);
      if (r0.z != 0) break;
      r1.y = cBlurFilterOffset.y + r2.y;
      r0.zw = r1.xy * cBlurFilterScale.xy + v1.xy;
      r0.z = texMap0.SampleLevel(texMap0_s, r0.zw, 0).w;
      r2.x = r2.x + r0.z;
      r2.y = 1 + r2.y;
    }
    r0.x = r2.x;
    r0.y = 1 + r0.y;
  }
  r1.xyzw = texMap1.SampleLevel(texMap1_s, v1.xy, 0).xyzw;
  r0.x = cBlurFilterSize.w * r0.x;
  r2.xyzw = -cBlurFilterColor1.xyzw + r1.xyzw;
  r0.xyzw = r0.xxxx * r2.xyzw + cBlurFilterColor1.xyzw;
  r0.xyzw = r0.xyzw * r1.wwww;
  r1.xyzw = cBitmapColorTransform._m10_m11_m12_m13 * r0.wwww;
  o0.xyzw = r0.xyzw * cBitmapColorTransform._m00_m01_m02_m03 + r1.xyzw;
  return;
}