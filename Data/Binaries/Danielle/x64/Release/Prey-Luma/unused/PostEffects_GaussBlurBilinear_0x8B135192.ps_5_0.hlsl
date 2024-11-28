cbuffer PER_BATCH : register(b0)
{
  float4 clampTC : packoffset(c0);
  float4 psWeights[16] : packoffset(c1);
}

SamplerState _tex0_s : register(s0);
Texture2D<float4> _tex0 : register(t0);

// LUMA: Unchanged.
// This blurs the image. It's run after upscaling (and usually after TexToTexSampledPS), so it supports DLSS fine and doesn't need any "CV_HPosScale"/"MapViewportToRaster()" adjustments.
// It seems like it generally handles different aspect ratios correctly.
void main(
  float4 HPosition : SV_Position0,
  float4 tc0 : TEXCOORD0,
  float4 tc1 : TEXCOORD1,
  float4 tc2 : TEXCOORD2,
  float4 tc3 : TEXCOORD3,
  float4 tc4 : TEXCOORD4,
  out float4 outColor : SV_Target0)
{
  float4 sum = 0;
  
  // Perform downscaling clamp post-interpolation
  tc0.xyzw = clamp(tc0.xyzw, clampTC.xzxz, clampTC.ywyw);
  tc1.xyzw = clamp(tc1.xyzw, clampTC.xzxz, clampTC.ywyw);
  tc2.xyzw = clamp(tc2.xyzw, clampTC.xzxz, clampTC.ywyw);
  tc3.xyzw = clamp(tc3.xyzw, clampTC.xzxz, clampTC.ywyw);

	float4 col = _tex0.Sample(_tex0_s, tc0.xy);
	sum += col * psWeights[0].x;  

	col = _tex0.Sample(_tex0_s, tc0.zw);
	sum += col * psWeights[1].x;  
	
  col = _tex0.Sample(_tex0_s, tc1.xy);
	sum += col * psWeights[2].x;  

	col = _tex0.Sample(_tex0_s, tc1.zw);
	sum += col * psWeights[3].x;

	col = _tex0.Sample(_tex0_s, tc2.xy);
	sum += col * psWeights[4].x;  
	
	col = _tex0.Sample(_tex0_s, tc2.zw);
	sum += col * psWeights[5].x;  
	
	col = _tex0.Sample(_tex0_s, tc3.xy);
	sum += col * psWeights[6].x;  
	
	col = _tex0.Sample(_tex0_s, tc3.zw);
	sum += col * psWeights[7].x;

  // LUMA FT: this seems to already be acknowledging the aspect ratio and thus it blurs equally (in screen space) for ultrawide or 16:9 or any aspect ratio. This is used in menus backgrounds and other scene texture operations in Prey anyways
  // LUMA FT: note that this can cause invalid luminances in the downscaled image
  outColor = sum;
  return;
}