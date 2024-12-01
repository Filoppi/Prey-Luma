#include "include/Common.hlsl"

#include "include/CBuffer_PerViewGlobal.hlsl"

SamplerState _tex0_s : register(s0);
SamplerState _tex1_s : register(s1);
Texture2D<float4> _tex0 : register(t0);
Texture2D<float4> _tex1 : register(t1);

#define SCALE_PIXELSIZE 2
#define PS_ScreenSize CV_ScreenSize

// LUMA FT: the actual SMAA implementation (this applies SMAA (the edge AA) on the color buffer)
void main(
  float4 WPos : SV_Position0,
  float4 inBaseTC : TEXCOORD0,
  out float4 outColor : SV_Target0)
{
  // SMAA ruins DLSS and is simply useless when DLSS is running. We can't really detect whether the user has set AA to TAA or SMAA 2TX in game so we have to skip the pass on the spot instead.
  // If we wanted, we could also disable the previous two passes for SMAA edge detection when DLSS is engaged.
  bool skipSMAA = LumaSettings.DLSS;
#if !ENABLE_AA || !ENABLE_SMAA
  skipSMAA = true;
#endif
  if (skipSMAA)
  {
#if 1 // Optimized
    outColor = _tex1.Load(WPos.xyz);
#else
    outColor = _tex1.SampleLevel(_tex1_s, inBaseTC.xy, 0);
#endif
    return;
  }

  // LUMA FT: fixed code trying to directly edit cbuffer values (it works but it's not easy to read)
	float2 scaledInverseScreenSize = PS_ScreenSize.zw * SCALE_PIXELSIZE;

  // Fetch the blending weights for current pixel:
  float4 topLeft = _tex0.Sample(_tex0_s, inBaseTC.xy);
  float bottom = _tex0.Sample(_tex0_s, inBaseTC.xy + float2(0, 1) * scaledInverseScreenSize).g;
  float right = _tex0.Sample(_tex0_s, inBaseTC.xy + float2(1, 0) * scaledInverseScreenSize).a;
  float4 a = float4(topLeft.r, bottom, topLeft.b, right);
  
#if TEST_SMAA_EDGES
    outColor = float4(0, 0, 0, 1);
		outColor.xy = topLeft.xy + topLeft.zw;
    outColor.rgb = SDRToHDR(outColor.rgb, false);
    return;
#endif

  if (dot(a, 1.0) < 1e-5) // LUMA FT: theoretically we should adjust this by gamma/linear but we don't need to
	{
		outColor = _tex1.SampleLevel(_tex1_s, inBaseTC.xy, 0);
	}
  else 
	{
      float4 color = 0.0;

      // Up to 4 lines can be crossing a pixel (one through each edge). We
      // favor blending by choosing the line with the maximum weight for each
      // direction:
      float2 offset;
      offset.x = a.a > a.b? a.a : -a.b; // left vs. right 
      offset.y = a.g > a.r? a.g : -a.r; // top vs. bottom

      // Then we go in the direction that has the maximum weight:
      if (abs(offset.x) > abs(offset.y)) // horizontal vs. vertical
          offset.y = 0.0;
      else
          offset.x = 0.0;

      // Fetch the opposite color and lerp by hand:
      float4 C = _tex1.SampleLevel(_tex1_s, inBaseTC.xy, 0);
      inBaseTC.xy += sign(offset) * scaledInverseScreenSize;
      float4 Cop = _tex1.SampleLevel(_tex1_s, inBaseTC.xy, 0);
			
			// convert to linear
      // LUMA FT: improved linearization and gammification code
			C.rgb = DecodeBackBufferToLinearSDRRange(C.rgb);
			Cop.rgb = DecodeBackBufferToLinearSDRRange(Cop.rgb);

      float s = abs(offset.x) > abs(offset.y) ? abs(offset.x) : abs(offset.y);
      outColor = lerp(C, Cop, s);

			// convert back to gamma
			outColor.rgb = EncodeBackBufferFromLinearSDRRange(outColor.rgb);
  }

	return;
}