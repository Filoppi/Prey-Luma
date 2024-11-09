#include "include/ColorGradingLUT.hlsl"

cbuffer PER_BATCH : register(b0)
{
  float4 ColorGradingParams3 : packoffset(c0);
  float4 ColorGradingParams2 : packoffset(c1);
  float4 ColorGradingParams4 : packoffset(c2);
  float4 mColorGradingMatrix[3] : packoffset(c3);
  float4 ColorGradingParams0 : packoffset(c6);
  float4 ColorGradingParams1 : packoffset(c7);
}

Texture2D<float4> mergedChartTex : register(t0);
SamplerState ssMergedChart : register(s0);

void AdjustLevels( inout float4 cImage )
{
  float fMinInput = ColorGradingParams0.x;
  float fGammaInput = 1.0 / ColorGradingParams0.y; // User secondary gamma (brightness adjustments, defaulting to 1), this is for the Vanilla SDR game, it's best avoided with Luma HDR
  float fMaxInput = ColorGradingParams0.z;
  float fMinOutput = ColorGradingParams0.w;
  float fMaxOutput = ColorGradingParams1.x;

  cImage.xyz *= 255.0;
  cImage.xyz -= fMinInput;
  cImage.xyz /= fMaxInput - fMinInput;
#if ENABLE_HDR_COLOR_GRADING_LUT // LUMA FT: allow negative colors here, in case the levels made it go below zero, hopefully it will allow more HDR colors to pass through without changing the white level
  cImage.xyz = pow(abs(cImage.xyz), fGammaInput) * sign(cImage.xyz); // LUMA FT: this is usually centered around 1 as the min/max input range is usually set 255
#else
  cImage.xyz = pow(max(cImage.xyz, 0.0f), fGammaInput);
#endif
  cImage.xyz *= fMaxOutput - fMinOutput;
  cImage.xyz += fMinOutput;
  cImage.xyz /= 255.0;
}

void ApplyPhotoFilter( inout float4 cImage )
{
  float3 cFilterColor = ColorGradingParams2.xyz;
  float fFilterColorDensity = ColorGradingParams2.w;

  float fLum = GetLuminance(cImage.xyz);

  float3 cMin = 0;
  float3 cMed = cFilterColor;
  float3 cMax = 1.0;

  float3 cColor = lerp(cMin, cMed , saturate( fLum * 2.0 ));
  cColor = lerp( cColor, cMax, saturate( fLum - 0.5 ) * 2.0 );

  cImage.xyz = lerp( cImage.xyz, cColor.xyz, saturate( fLum * fFilterColorDensity ) );
}

void AdjustColor( inout float4 cImage )
{
  // do a dp4 instead, saves 3 adds // LUMA FT: the 4th element is actually used to manipulate the average brightness
  cImage.xyz = float3(dot(cImage, mColorGradingMatrix[0]), 
                      dot(cImage, mColorGradingMatrix[1]), 
                      dot(cImage, mColorGradingMatrix[2]));
}

float4 RGBtoCMYK( float3 rgb )
{
  float4 cmyk = 0.0;
  cmyk.xyz = 1.0 - rgb;

#if ENABLE_HDR_COLOR_GRADING_LUT //TODOFT0: test this, if we ever met it ("selectiveColorAdjustment" is always off now). The code seems fine.
  cmyk.w = min3(cmyk.xyz);
  cmyk.xyz = safeDivision(cmyk.xyz - cmyk.w, 1.0 - cmyk.w, 0);
#else
  cmyk.w = saturate( min( min(cmyk.x, cmyk.y), cmyk.z ) );
  cmyk.xyz = saturate( (cmyk.xyz - cmyk.w) / (1.0 - cmyk.w) );
#endif

  return cmyk;
}

float3 CMYKtoRGB( float4 cmyk )
{
  float3 rgb = 0.0;
#if ENABLE_HDR_COLOR_GRADING_LUT
  rgb = 1.0 - (cmyk.xyz * (1.0 - cmyk.w) + cmyk.w);
#else
  rgb = 1.0 - min(1.0, cmyk.xyz * (1.0 - cmyk.w) + cmyk.w);
#endif
  return rgb;
}

void SelectiveColor( inout float3 cImage )
{
  float fColorPickRange = saturate(1.0 - length(cImage.xyz - ColorGradingParams3.xyz));

  float4 cmyk = RGBtoCMYK( cImage.xyz );
  cmyk = lerp( cmyk, clamp(cmyk+ColorGradingParams4, -1, 1), fColorPickRange );
  cImage = lerp( cImage, CMYKtoRGB( cmyk ), fColorPickRange ); // LUMA FT: not sure why the "fColorPickRange" lerp is applied twice (acts as pow 2 basically)
}

float3 CombineColorGradingWithColorChartPS(float3 Color, bool adjustLevels, bool photoFilter, bool selectiveColorAdjustment)
{
  // LUMA FT: the input coordinates are generated from a vertex shader and are exactly in the 0-1 range
	float4 col = float4(Color, 1.f);
  
#if 0 // Like "FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE" but directly on the LUT
#if ENABLE_LINEAR_COLOR_GRADING_LUT
  col.rgb = gamma_sRGB_to_linear(col.rgb);
#endif
  return col.rgb;
#endif

  // LUMA FT: we do these adjustments in gamma space independently of "ENABLE_LINEAR_COLOR_GRADING_LUT" or "ENABLE_HDR_COLOR_GRADING_LUT", as they are meant to be

  if (adjustLevels) // _RT_SAMPLE0
  {
    AdjustLevels(col);
  }

  if (photoFilter) // _RT_SAMPLE4
  {
    ApplyPhotoFilter(col);
  }

  // General color adjustment
  AdjustColor(col);
  
  if (selectiveColorAdjustment) // _RT_SAMPLE2
  {
    SelectiveColor(col.xyz);
  }
  
#if 0 // This is already done in the LUT extrapolation now
  // LUMA FT: "AdjustColor()" can result in negative colors with negative luminances (invalid colors).
  // We might leave them in, almost all of the game's post process is done per channel,
  // as nothing really breaks if there's invalid luminances, but some specific scene end up looking too dark (the negative color components bring the luminance down).
  // Correcting them at any point in the pipeline does theoretically reduce the output range and quality though.
  // The alternative would be to clip negative luminance out before the final output, but again, that would not maintan the vanilla SDR look.
  FixColorGradingLUTNegativeLuminance(col.xyz);
#endif

  LUTExtrapolationData extrapolationData = DefaultLUTExtrapolationData();
  extrapolationData.inputColor = col.xyz;

  LUTExtrapolationSettings extrapolationSettings = DefaultLUTExtrapolationSettings();
  extrapolationSettings.enableExtrapolation = bool(ENABLE_LUT_EXTRAPOLATION);
  extrapolationSettings.extrapolationQuality = LUT_EXTRAPOLATION_QUALITY;
  extrapolationSettings.inputLinear = false;
  extrapolationSettings.lutInputLinear = false;
  extrapolationSettings.lutOutputLinear = false;
  extrapolationSettings.outputLinear = bool(ENABLE_LINEAR_COLOR_GRADING_LUT);
  extrapolationSettings.transferFunctionIn = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB;
  // We don't do gamma correction here (like, baking it in the LUT) as LUTs are 16x so they are very low in resolution and correcting gamma with such low precision would greatly shift the black level (more than it should)
  extrapolationSettings.transferFunctionOut = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB;
  // We don't want tetrahedral interpolation here as this is an intermediary LUT step that re-stores the LUT on itself, so we'd shift colors if we did that.
  extrapolationSettings.samplingQuality = 0;

  static const bool gammaCorrection = false; 
	col.xyz = SampleLUTWithExtrapolation(mergedChartTex, ssMergedChart, extrapolationData, extrapolationSettings); // LUMA FT: replaced from "TexColorChart2D()"
  // LUMA FT: we allow values beyond 0-1 even if "ENABLE_LINEAR_COLOR_GRADING_LUT" is false, see "ENABLE_HDR_COLOR_GRADING_LUT".

  //TODOFT3: ... Why does this happen!? Could we store the last clipped texel in the alpha channel, to re-use it later in sampling?
  //TODO LUMA: Prey LUTs are occasionally clipped, as in, for example, the last two or three texels on the red axis are all 255, so this will both make the HDR look clipped in general, and particularly break the LUT extrapolation logic.
  //Fortunately it only really happens in one or two LUTs, so it's not a massive problem (they could also be fixed by modifying the assets).
#if TEST_LUT && 0
  if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX, LUT_MAX, LUT_MAX))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX - 1, LUT_MAX - 1, LUT_MAX - 1))).rgb))
  {
    col.xyz = 0;
  }
  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX, LUT_MAX, LUT_MAX))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX - 1, LUT_MAX, LUT_MAX))).rgb))
  {
    col.xyz = 0;
  }
  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX, LUT_MAX, LUT_MAX))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX, LUT_MAX - 1, LUT_MAX))).rgb))
  {
    col.xyz = 0;
  }
  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX, LUT_MAX, LUT_MAX))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX, LUT_MAX, LUT_MAX - 1))).rgb))
  {
    col.xyz = 0;
  }

  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX, LUT_MAX, 0))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX - 1, LUT_MAX - 1, 0))).rgb))
  {
    col.xyz = 0;
  }
  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX, LUT_MAX, 0))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX - 1, LUT_MAX, 0))).rgb))
  {
    col.xyz = 0;
  }
  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX, LUT_MAX, 0))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX, LUT_MAX - 1, 0))).rgb))
  {
    col.xyz = 0;
  }

  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX, 0, LUT_MAX))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX - 1, 0, LUT_MAX - 1))).rgb))
  {
    col.xyz = 0;
  }
  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX, 0, LUT_MAX))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX - 1, 0, LUT_MAX))).rgb))
  {
    col.xyz = 0;
  }
  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX, 0, LUT_MAX))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX, 0, LUT_MAX - 1))).rgb))
  {
    col.xyz = 0;
  }

  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(0, LUT_MAX, LUT_MAX))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(0, LUT_MAX - 1, LUT_MAX - 1))).rgb))
  {
    col.xyz = 0;
  }
  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(0, LUT_MAX, LUT_MAX))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(0, LUT_MAX - 1, LUT_MAX))).rgb))
  {
    col.xyz = 0;
  }
  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(0, LUT_MAX, LUT_MAX))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(0, LUT_MAX, LUT_MAX - 1))).rgb))
  {
    col.xyz = 0;
  }

  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX, 0, 0))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(LUT_MAX - 1, 0, 0))).rgb))
  {
    col.xyz = 0;
  }

  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(0, LUT_MAX, 0))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(0, LUT_MAX - 1, 0))).rgb))
  {
    col.xyz = 0;
  }

  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(0, 0, LUT_MAX))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(0, 0, LUT_MAX - 1))).rgb))
  {
    col.xyz = 0;
  }

  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(0, 0, 0))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(1, 1, 1))).rgb))
  {
    col.xyz = 0;
  }
  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(0, 0, 0))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(1, 0, 0))).rgb))
  {
    col.xyz = 0;
  }
  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(0, 0, 0))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(0, 1, 0))).rgb))
  {
    col.xyz = 0;
  }
  else if (any(mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(0, 0, 0))).rgb == mergedChartTex.Load(ConditionalConvert3DTo2DLUTCoordinates(int3(0, 0, 1))).rgb))
  {
    col.xyz = 0;
  }
#endif

  return col.rgb;
}