cbuffer PER_BATCH : register(b0)
{
  float4 ImageGhostingParamsPS : packoffset(c0);
}

SamplerState _tex0_s : register(s0);
Texture2D<float4> _tex0 : register(t0);

// LUMA: Unchanged.
// Used to create a ghosting "trail" with the previous frame (e.g. Prey uses it when throwing some kind of bombs).
// This runs after PostAAComposites and upscaling.
//TODO LUMA: test if it's adjusted by frame rate (it seems to be?).
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.x = ImageGhostingParamsPS.z + ImageGhostingParamsPS.w;
  o0.w = saturate(-r0.x * 4.125 + 1);
  r0.xyz = _tex0.Sample(_tex0_s, v1.xy).xyz;
  o0.xyz = r0.xyz;
  return;
}