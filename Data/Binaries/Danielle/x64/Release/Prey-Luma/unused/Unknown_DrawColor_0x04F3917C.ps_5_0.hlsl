#include "include/UI.hlsl"

// Used to clean the render target to black at the beginning of the frame (though it doesn't seem to affect anything, so maybe it's cleaning other textures), and maybe for other stuff.
void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  out float4 outColor : SV_Target0)
{
  outColor.xyzw = v1.xyzw;

#if POST_PROCESS_SPACE_TYPE == 1
  if (LumaUIData.WritingOnSwapchain)
  {
	  const float paperWhite = UIPaperWhiteNits / sRGB_WhiteLevelNits;
    outColor.rgb = game_gamma_to_linear(outColor.rgb);
    outColor.rgb *= paperWhite;
  }
#endif // POST_PROCESS_SPACE_TYPE == 1

#if TEST_UI //TODOFT: if this ever happened, we might need to add alpha handling. Otherwise, linearization is already done above.
  if (any(outColor.xyz != 0) || outColor.w != 1)
  {
    outColor.xyzw = float4(0, 1, 2, 1);
  }
#endif
  return;
}