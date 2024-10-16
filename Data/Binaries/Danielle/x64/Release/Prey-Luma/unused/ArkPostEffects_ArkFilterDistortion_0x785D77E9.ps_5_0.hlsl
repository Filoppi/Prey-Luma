#define _RT_SAMPLE0 0
#define _RT_SAMPLE1 1
#define _RT_SAMPLE2 0

cbuffer PER_BATCH : register(b0)
{
  float4 psParams[16] : packoffset(c0);
}

SamplerState ssScreenTex_s : register(s0);
Texture2D<float4> screenTex : register(t0);

// LUMA: Unchanged.
// Screen space distortion effect. This is already corrected by the aspect ratio and supports ultrawide fine:
// in UW, the distortion is focused around the 16:9 part of the image and it plays out identically there.
// This runs after AA and upscaling.
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;

  r0.x = psParams[1].y * psParams[1].y;
  r0.x = rcp(r0.x);
  r0.yz = v1.xy * float2(2,2) + float2(-1,-1);
  r1.yz = -psParams[0].zw + r0.yz;
  r1.x = psParams[1].z * r1.y;
  r0.w = dot(r1.xz, r1.xz);
  r0.w = r0.w * r0.w;
  r0.x = -r0.w * r0.x + 1;
  r0.w = 0.25 * abs(psParams[1].x);
  r0.x = r0.w * r0.x;
  r0.x = max(0, r0.x);
  r0.x = 1 + r0.x;
  r0.xy = r1.yz * r0.xx + -r0.yz;
  r0.xy = v1.xy + r0.xy;
  o0.xyzw = screenTex.Sample(ssScreenTex_s, r0.xy).xyzw;
  return;
}