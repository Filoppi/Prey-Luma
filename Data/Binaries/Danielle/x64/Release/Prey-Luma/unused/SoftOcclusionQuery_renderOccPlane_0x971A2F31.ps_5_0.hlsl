SamplerState depthMap_s : register(s0);
SamplerState sceneMaskMap_s : register(s1);
Texture2D<float4> depthMap : register(t0);
Texture2D<float4> sceneMaskMap : register(t1);

#define cmp -

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;

  r0.x = sceneMaskMap.Sample(sceneMaskMap_s, v1.xy).x;
  r0.x = v1.z + -r0.x;
  r0.x = v1.w * r0.x;
  r0.y = cmp(0 < r0.x);
  r0.x = cmp(r0.x < 0);
  r0.x = (int)-r0.y + (int)r0.x;
  r0.x = (int)r0.x;
  r0.x = saturate(r0.x);
  r0.y = cmp(v1.w == 0.000000);
  r0.x = r0.y ? 1 : r0.x;
  r0.y = depthMap.Sample(depthMap_s, v1.xy).x;
  r0.y = -v1.z + r0.y;
  r0.z = cmp(0 < r0.y);
  r0.y = cmp(r0.y < 0);
  r0.y = (int)-r0.z + (int)r0.y;
  r0.y = (int)r0.y;
  r0.y = saturate(r0.y);
  r0.x = r0.y * r0.x;
  r0.x = cmp(0 < r0.x);
  r0.x = (int)-r0.x;
  o0.xyzw = saturate(r0.xxxx);
}