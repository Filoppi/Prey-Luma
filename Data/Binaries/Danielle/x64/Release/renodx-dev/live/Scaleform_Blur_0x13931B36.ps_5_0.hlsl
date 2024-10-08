// ---- Created with 3Dmigoto v1.3.16 on Thu Jun 27 00:11:46 2024

cbuffer PER_BATCH : register(b0)
{
  row_major float2x4 cBitmapColorTransform : packoffset(c0);
  float2 cBlurFilterOffset : packoffset(c2);
  float4 cBlurFilterColor1 : packoffset(c3);
  float2 cBlurFilterScale : packoffset(c4);
  float4 cBlurFilterColor2 : packoffset(c5);
  float4 cBlurFilterSize : packoffset(c6);
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

  r0.xy = float2(0,0);
  r0.z = -cBlurFilterSize.x;
  while (true) {
    r0.w = cmp(cBlurFilterSize.x < r0.z);
    if (r0.w != 0) break;
    r1.x = cBlurFilterOffset.x + r0.z;
    r2.xy = r0.xy;
    r2.z = -cBlurFilterSize.y;
    while (true) {
      r0.w = cmp(cBlurFilterSize.y < r2.z);
      if (r0.w != 0) break;
      r1.y = cBlurFilterOffset.y + r2.z;
      r1.zw = r1.xy * cBlurFilterScale.xy + v1.xy;
      r0.w = texMap0.SampleLevel(texMap0_s, r1.zw, 0).w;
      r2.y = r2.y + r0.w;
      r1.yz = -r1.xy * cBlurFilterScale.xy + v1.xy;
      r0.w = texMap0.SampleLevel(texMap0_s, r1.yz, 0).w;
      r2.x = r2.x + r0.w;
      r2.z = 1 + r2.z;
    }
    r0.xy = r2.xy;
    r0.z = 1 + r0.z;
  }
  r1.xyzw = texMap1.SampleLevel(texMap1_s, v1.xy, 0).xyzw;
  r0.xy = cBlurFilterSize.ww * r0.xy;
  r2.xyzw = cBlurFilterColor2.xyzw * r0.xxxx;
  r0.xyzw = cBlurFilterColor1.xyzw * r0.yyyy + r2.xyzw;
  r2.x = 1 + -r1.w;
  r0.xyzw = r0.xyzw * r2.xxxx + r1.xyzw;
  r0.xyzw = r0.xyzw * r2.xxxx;
  r1.xyzw = cBitmapColorTransform._m10_m11_m12_m13 * r0.wwww;
  r0.xyzw = r0.xyzw * cBitmapColorTransform._m00_m01_m02_m03 + r1.xyzw;
  r1.xyzw = float4(-1,-1,-1,-1) + r0.xyzw;
  o0.xyzw = r0.wwww * r1.xyzw + float4(1,1,1,1);
  return;
}