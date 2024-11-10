#include "include/CBuffer_PerViewGlobal.hlsl"

// tex0 = edgesTex (HDR, generated based on the rendering)
// tex1 = areaTex (SDR/8bit, unclear who generates this, is it fixed in value?)
// tex2 = searchTex (SDR/8bit, unclear who generates this, is it fixed in value?)
SamplerState _tex0_s : register(s0);
SamplerState _tex1_s : register(s1);
SamplerState _tex2_s : register(s2);
Texture2D<float4> _tex0 : register(t0);
Texture2D<float4> _tex1 : register(t1);
Texture2D<float4> _tex2 : register(t2);

// 3Dmigoto declarations
#define cmp -

// LUMA: This is fine as it is, we don't need to change it as it doesn't directly work on the game colors, it works with edges
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
#if !ENABLE_AA || !ENABLE_SMAA // Optimization
  o0 = 0;
  return;
#endif

  float4 r0,r1,r2,r3,r4,r5,r6,r7;

  r0.xy = CV_ScreenSize.zw + CV_ScreenSize.zw;
  r0.zw = v1.xy / r0.xy;
  r1.xyz = CV_ScreenSize.zwz * float3(-0.5,-0.25,2.5) + v1.xyx;
  r2.xyz = CV_ScreenSize.zww * float3(-0.25,-0.5,2.5) + v1.xyy;
  r3.xy = r1.zx;
  r3.zw = r2.yz;
  r4.xyzw = CV_ScreenSize.zzww * float4(-32,32,-32,32) + r3.yxzw;
  r5.xy = _tex0.Sample(_tex0_s, v1.xy).xy;
  r5.xy = cmp(float2(0,0) < r5.yx);
  if (r5.x != 0) {
    r6.xy = r1.yx;
    r6.z = 1;
    r5.x = 0;
    while (true) {
      r1.w = cmp(r4.x < r6.y);
      r2.w = cmp(0.828100026 < r6.z);
      r1.w = r1.w ? r2.w : 0;
      r2.w = cmp(r5.x == 0.000000);
      r1.w = r1.w ? r2.w : 0;
      if (r1.w == 0) break;
      r5.xz = _tex0.SampleLevel(_tex0_s, r6.yx, 0).xy;
      r6.xy = -CV_ScreenSize.wz * float2(0,4) + r6.xy;
      r6.z = r5.z;
    }
    r1.x = CV_ScreenSize.z * 6.5 + r6.y;
    r6.x = 0.5 * r5.x;
    r1.w = _tex2.SampleLevel(_tex2_s, r6.xz, 0).x;
    r1.w = r1.w * r0.x;
    r6.x = -r1.w * 255 + r1.x;
    r6.y = r3.z;
    r1.x = _tex0.SampleLevel(_tex0_s, r6.xy, 0).x;
    r7.xy = r1.yz;
    r7.z = 1;
    r5.x = 0;
    while (true) {
      r2.w = cmp(r7.y < r4.y);
      r3.w = cmp(0.828100026 < r7.z);
      r2.w = r2.w ? r3.w : 0;
      r3.w = cmp(r5.x == 0.000000);
      r2.w = r2.w ? r3.w : 0;
      if (r2.w == 0) break;
      r5.xz = _tex0.SampleLevel(_tex0_s, r7.yx, 0).xy;
      r7.xy = CV_ScreenSize.wz * float2(0,4) + r7.xy;
      r7.z = r5.z;
    }
    r1.y = -CV_ScreenSize.z * 0.5 + r7.y;
    r1.z = 2 * CV_ScreenSize.z;
    r1.y = CV_ScreenSize.z * -6 + r1.y;
    r7.x = r5.x * 0.5 + 0.5;
    r2.w = _tex2.SampleLevel(_tex2_s, r7.xz, 0).x;
    r2.w = r2.w * r0.x;
    r6.z = r2.w * 255 + r1.y;
    r4.xy = r6.xz / r0.xx;
    r0.xz = r4.xy + -r0.zz;
    r4.xy = sqrt(abs(r0.xz));
    r5.xz = CV_ScreenSize.zw * float2(2,0) + r6.zy;
    r1.w = _tex0.SampleLevel(_tex0_s, r5.xz, 0).x;
    r1.xy = float2(4,4) * r1.xw;
    r1.xy = round(r1.xy);
    r1.xy = r1.xy * float2(16,16) + r4.xy;
    r1.xy = r1.xy * float2(0.00625000009,0.0017857143) + float2(0.00312500005,0.000892857148);
    r1.xy = _tex1.SampleLevel(_tex1_s, r1.xy, 0).xy;
    r6.xz = r0.xz * r1.zz;
    r6.y = 0;
    r5.xzw = v1.xyx + r6.xyz;
    r6.xyzw = CV_ScreenSize.zwzw * float4(0,2,0,-4) + r5.xzxz;
    r1.z = _tex0.SampleLevel(_tex0_s, r6.xy, 0).x;
    r0.x = cmp(abs(r0.x) < abs(r0.z));
    r1.w = _tex0.SampleLevel(_tex0_s, r6.zw, 0).x;
    r1.zw = saturate(float2(1,1) + -r1.zw);
    r1.zw = r1.xy * r1.zw;
    r1.xy = r0.xx ? r1.zw : r1.xy;
    r4.xy = CV_ScreenSize.zw * float2(2,2) + r5.wz;
    r4.x = _tex0.SampleLevel(_tex0_s, r4.xy, 0).x;
    r5.xz = CV_ScreenSize.zw * float2(2,-4) + r5.wz;
    r4.y = _tex0.SampleLevel(_tex0_s, r5.xz, 0).x;
    r4.xy = saturate(float2(1,1) + -r4.xy);
    r1.xy = r4.xy * r1.xy;
    o0.xy = r0.xx ? r1.zw : r1.xy;
  } else {
    o0.xy = float2(0,0);
  }
  if (r5.y != 0) {
    r1.xy = r2.xy;
    r1.z = 1;
    r0.x = 0;
    while (true) {
      r1.w = cmp(r4.z < r1.y);
      r2.w = cmp(0.828100026 < r1.z);
      r1.w = r1.w ? r2.w : 0;
      r2.w = cmp(r0.x == 0.000000);
      r1.w = r1.w ? r2.w : 0;
      if (r1.w == 0) break;
      r0.xz = _tex0.SampleLevel(_tex0_s, r1.xy, 0).yx;
      r1.xy = -CV_ScreenSize.zw * float2(0,4) + r1.xy;
      r1.z = r0.z;
    }
    r0.z = CV_ScreenSize.w * 6.5 + r1.y;
    r1.x = 0.5 * r0.x;
    r0.x = _tex2.SampleLevel(_tex2_s, r1.xz, 0).x;
    r0.x = r0.y * r0.x;
    r3.x = -r0.x * 255 + r0.z;
    r0.x = _tex0.SampleLevel(_tex0_s, r3.yx, 0).y;
    r1.xy = r2.xz;
    r1.z = 1;
    r2.y = 0;
    while (true) {
      r1.w = cmp(r1.y < r4.w);
      r3.w = cmp(0.828100026 < r1.z);
      r1.w = r1.w ? r3.w : 0;
      r3.w = cmp(r2.y == 0.000000);
      r1.w = r1.w ? r3.w : 0;
      if (r1.w == 0) break;
      r2.yw = _tex0.SampleLevel(_tex0_s, r1.xy, 0).yx;
      r1.xy = CV_ScreenSize.zw * float2(0,4) + r1.xy;
      r1.z = r2.w;
    }
    r1.y = -CV_ScreenSize.w * 0.5 + r1.y;
    r1.y = CV_ScreenSize.w * -6 + r1.y;
    r1.x = r2.y * 0.5 + 0.5;
    r1.x = _tex2.SampleLevel(_tex2_s, r1.xz, 0).x;
    r1.x = r1.x * r0.y;
    r3.z = r1.x * 255 + r1.y;
    r1.xy = r3.xz / r0.yy;
    r1.yz = r1.xy + -r0.ww;
    r0.yw = sqrt(abs(r1.yz));
    r2.xy = CV_ScreenSize.zw * float2(0,2) + r3.yz;
    r0.z = _tex0.SampleLevel(_tex0_s, r2.xy, 0).y;
    r0.xz = float2(4,4) * r0.xz;
    r0.xz = round(r0.xz);
    r0.xy = r0.xz * float2(16,16) + r0.yw;
    r0.xy = r0.xy * float2(0.00625000009,0.0017857143) + float2(0.00312500005,0.000892857148);
    r0.xy = _tex1.SampleLevel(_tex1_s, r0.xy, 0).xy;
    r1.x = 0;
    r2.xyz = float3(1,2,2) * CV_ScreenSize.zww;
    r2.xyz = r1.xyz * r2.xyz + v1.xyy;
    r3.xyzw = CV_ScreenSize.zwzw * float4(2,0,-4,0) + r2.xyxy;
    r0.z = _tex0.SampleLevel(_tex0_s, r3.xy, 0).y;
    r1.x = cmp(abs(r1.y) < abs(r1.z));
    r0.w = _tex0.SampleLevel(_tex0_s, r3.zw, 0).y;
    r0.zw = saturate(float2(1,1) + -r0.zw);
    r0.zw = r0.xy * r0.zw;
    r0.xy = r1.xx ? r0.zw : r0.xy;
    r1.yz = CV_ScreenSize.zw * float2(2,2) + r2.xz;
    r1.y = _tex0.SampleLevel(_tex0_s, r1.yz, 0).y;
    r2.xy = CV_ScreenSize.zw * float2(-4,2) + r2.xz;
    r1.z = _tex0.SampleLevel(_tex0_s, r2.xy, 0).y;
    r1.yz = saturate(float2(1,1) + -r1.yz);
    r0.xy = r1.yz * r0.xy;
    o0.zw = r1.xx ? r0.zw : r0.xy;
  } else {
    o0.zw = float2(0,0);
  }
  return;
}