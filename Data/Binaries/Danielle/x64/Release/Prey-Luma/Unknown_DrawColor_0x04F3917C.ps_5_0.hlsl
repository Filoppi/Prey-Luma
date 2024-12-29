#include "include/UI.hlsl"

// LUMA FT: we don't know the name of this shader as we can't find it in the source (it's too generic)
// Used to clean the render target to black at the beginning of the frame (though it doesn't seem to affect anything, so maybe it's cleaning other textures), and maybe for other stuff.
// This is also used to draw black screens on top of the scenes (e.g. fades to white or black etc).
void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  out float4 outColor : SV_Target0)
{
  outColor.xyzw = v1.xyzw;

  // LUMA FT: support any image encoding and blend type etc
	outColor = ConditionalLinearizeUI(outColor, false, false, true); // This will take care of any "POST_PROCESS_SPACE_TYPE" case
}