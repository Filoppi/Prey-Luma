#include "include/UI.hlsl"

cbuffer _Globals : register(b0)
{
  float4 consta : packoffset(c0);
  float4 crc : packoffset(c1);
  float4 cbc : packoffset(c2);
  float4 adj : packoffset(c3);
  float4 yscale : packoffset(c4);
}

SamplerState samp0_s : register(s0);
Texture2D<float4> tex0 : register(t0);
Texture2D<float4> tex1 : register(t1);
Texture2D<float4> tex2 : register(t2);
Texture2D<float4> tex3 : register(t3);

// Draws videos with an alpha channel (transparency or mask). We don't apply "Auto HDR" on this one as it's likely used to draw videos on the 3D scene.
void main(
  float4 v0 : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
  float4 r0;
    
  r0.x = tex1.Sample(samp0_s, inBaseTC.zw).x;
  r0.xyz = crc.xyz * r0.xxx;
  r0.w = tex0.Sample(samp0_s, inBaseTC.xy).x;
  r0.xyz = r0.www * yscale.xyz + r0.xyz;
  r0.w = tex2.Sample(samp0_s, inBaseTC.zw).x;
  r0.xyz = r0.www * cbc.xyz + r0.xyz;
  r0.xyz = adj.xyz + r0.xyz;
  r0.w = tex3.Sample(samp0_s, inBaseTC.xy).x;
  outColor.xyzw = consta.xyzw * r0.xyzw; // LUMA FT: this is possibly pre-multiplying the video color by an alpha
  
  //TODOFT1: when's this shader used? Do we need to linearize it!? Should we apply AutoHDR to this (see description on top).
  //Do we need to apply the "ConditionalLinearizeUI()" workaround on the alpha/color to emulate gamma space blends?
  if (LumaUIData.WritingOnSwapchain)
  {
	  const float paperWhite = GamePaperWhiteNits / sRGB_WhiteLevelNits; // Use the game brightness, not the UI one, as these are usually videos that are seamless with gameplay (except the main menu background), or represent 3D graphics anyway
#if POST_PROCESS_SPACE_TYPE == 1 || AUTO_HDR_VIDEOS
    outColor.rgb = game_gamma_to_linear_mirrored(outColor.rgb);
#endif // POST_PROCESS_SPACE_TYPE == 1 || AUTO_HDR_VIDEOS

#if AUTO_HDR_VIDEOS

    outColor.rgb = PumboAutoHDR(outColor.rgb, BinkVideosAutoHDRPeakWhiteNits, GamePaperWhiteNits, BinkVideosAutoHDRShoulderPow); // This won't multiply the paper white in, it just uses it as a modifier for the AutoHDR logic

#if POST_PROCESS_SPACE_TYPE <= 0 || POST_PROCESS_SPACE_TYPE >= 2
    outColor.rgb = linear_to_game_gamma_mirrored(outColor.rgb);
#endif // POST_PROCESS_SPACE_TYPE <= 0 || POST_PROCESS_SPACE_TYPE >= 2

#endif // AUTO_HDR_VIDEOS

#if POST_PROCESS_SPACE_TYPE == 1
    outColor.rgb *= paperWhite;
#endif // POST_PROCESS_SPACE_TYPE == 1
  }

#if !ENABLE_UI // We treat videos as UI
	outColor = 0;
#endif // !ENABLE_UI

  return;
}