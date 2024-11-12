#include "include/Common.hlsl"

#define _RT_SAMPLE0 1

#include "include/CBuffer_PerViewGlobal.hlsl"

#define cmp -

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  nointerpolation float4 cVision : TEXCOORD3,
  uint v5 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
  v0.xy *= float2(BaseHorizontalResolution, BaseVerticalResolution) / (CV_ScreenSize.xy / CV_HPosScale.xy);
	v0.xy += LumaData.CameraJitters.xy * float2(0.5, -0.5) * (CV_ScreenSize.xy / CV_HPosScale.xy);

  float4 r0,r1;

  r0.x = dot(-v2.xyz, -v2.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = -v2.xyz * r0.xxx;
  r0.w = v5.x ? 1 : -1;
  r1.xyz = v3.xyz * r0.www;
  r0.x = saturate(dot(r0.xyz, r1.xyz));
  r0.x = 1 + -r0.x;
  r0.x = max(0.5, r0.x);
  r0.y = 0.349999994 * v0.y;
  r0.y = frac(r0.y);
  r0.y = r0.y * 2 + -1;
  r0.y = abs(r0.y) * 0.5 + 0.5;
  r0.yzw = cVision.xyz * r0.yyy;
  r0.xyz = r0.yzw * r0.xxx;
  o0.xyz = cVision.www * r0.xyz;
  o0.w = 1;
  o0 = SDRToHDR(o0);
#if !ENABLE_ARK_CUSTOM_POST_PROCESS
  o0 = 0;
#endif
  return;
}