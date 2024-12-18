// LUMA: Unchanged.
// OcclCheckPS
// See "OcclCheckVS", the value returned here doesn't seem to matter.
void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  o0.xyzw = float4(0,0,0,0);
  return;
}