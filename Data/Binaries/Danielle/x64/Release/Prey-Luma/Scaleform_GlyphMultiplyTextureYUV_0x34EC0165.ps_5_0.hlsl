#include "include/Scaleform.hlsl"

SamplerState texMapY_s : register(s0);
SamplerState texMapU_s : register(s1);
SamplerState texMapV_s : register(s2);
Texture2D<float4> texMapY : register(t0);
Texture2D<float4> texMapU : register(t1);
Texture2D<float4> texMapV : register(t2);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;

  r0.x = texMapV.Sample(texMapV_s, v1.xy).w;
  r0.x -= 128.f / 255.f;
  //TODOFT4: revert this? (talk to lilium again) (we put "TEST_UI" around these to know what they are drawing, we could check if they were going out of range to tell the color space)
  //Also specify that these are limited range.
  // LUMA FT: fixed wrong Y'Cb'Cr->RGB decode, decoding from/to BT.601 instead of BT.709 (supposedly the source was encoded to YCbCr with BT.709 correctly)
  r0.xy = float2(1.7927410602569580078125,0.532909333705902099609375) * r0.xx;
  r0.z = texMapU.Sample(texMapU_s, v1.xy).w;
  r0.z -= 128.f / 255.f;
  r0.y = r0.z * -0.21324861049652099609375 + -r0.y;
  r0.z = 2.1124017238616943359375 * r0.z;
  r0.w = texMapY.Sample(texMapY_s, v1.xy).w;
  r0.w -= 16.f / 255.f;
  r1.xyz = saturate(r0.w * 1.16438353f + r0.xyz); // LUMA FT: we might not need this saturate() here, but we leave it to conserve the vanilla look
  r1.w = 1;
  r0.xyzw = r1.xyzw * cBitmapColorTransform._m00_m01_m02_m03 + cBitmapColorTransform._m10_m11_m12_m13;
  r1.xyzw = float4(-1,-1,-1,-1) + r0.xyzw;
  r0.xyzw = r0.wwww * r1.xyzw + float4(1,1,1,1);
  o0.rgba = PremultiplyAlpha(r0);
#if TEST_UI
  o0.rgb = float3(0, 2, 0);
#endif
  return;
}