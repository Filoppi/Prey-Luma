cbuffer PER_INSTANCE : register(b1)
{
  float4 PI_psOffsets[16] : packoffset(c0);
}

// LUMA: Unchanged.
// LUMA FT: for some reason this is never multiplied by "CV_HPosScale", probably because it doesn't run on actual screen space textures.
void main(
  float4 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Position0,
  out float4 o1 : TEXCOORD0,
  out float2 o2 : TEXCOORD1,
  out float2 p2 : TEXCOORD2,
  out float2 o3 : TEXCOORD3,
  out float2 p3 : TEXCOORD4,
  out float2 o4 : TEXCOORD5,
  out float2 p4 : TEXCOORD6,
  out float2 o5 : TEXCOORD7)
{
  o0.xyzw = v0.xyzw * float4(2,-2,1,1) + float4(-1,1,0,0);
  o1.xy = PI_psOffsets[0].xy + v1.xy;
  o1.zw = v1.xy;
  o2.xy = PI_psOffsets[1].xy + v1.xy;
  p2.xy = PI_psOffsets[2].xy + v1.xy;
  o3.xy = PI_psOffsets[3].xy + v1.xy;
  p3.xy = PI_psOffsets[4].xy + v1.xy;
  o4.xy = PI_psOffsets[5].xy + v1.xy;
  p4.xy = PI_psOffsets[6].xy + v1.xy;
  o5.xy = PI_psOffsets[7].xy + v1.xy;
  return;
}