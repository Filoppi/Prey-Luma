#include "include/Common.hlsl"

// Applies exponential ("Photographic") luminance/luma compression.
// The pow can modulate the curve without changing the values around the edges.
// The max is the max possible range to compress from, to not lose any output range if the input range was limited.
float rangeCompress(float X, float Max = FLT_MAX)
{
  // Branches are for static parameters optimizations
  if (Max == FLT_MAX) {
    // This does e^X. We expect X to be between 0 and 1.
    return 1.f - exp(-X);
  }
  const float lostRange = exp(-Max);
  const float restoreRangeScale = 1.f / (1.f - lostRange);
  return (1.f - exp(-X)) * restoreRangeScale;
}

// Refurbished DICE HDR tonemapper (per channel or luminance).
// Expects "InValue" to be >= "ShoulderStart" and "OutMaxValue" to be > "ShoulderStart".
float luminanceCompress(
  float InValue,
  float OutMaxValue,
  float ShoulderStart = 0.f,
  bool ConsiderMaxValue = false,
  float InMaxValue = FLT_MAX)
{
  const float compressableValue = InValue - ShoulderStart;
  const float compressableRange = InMaxValue - ShoulderStart;
  const float compressedRange = OutMaxValue - ShoulderStart;
  const float possibleOutValue = ShoulderStart + compressedRange * rangeCompress(compressableValue / compressedRange, ConsiderMaxValue ? (compressableRange / compressedRange) : FLT_MAX);
#if 1
  return possibleOutValue;
#else // Enable this branch if "InValue" can be smaller than "ShoulderStart"
  return (InValue <= ShoulderStart) ? InValue : possibleOutValue;
#endif
}

#define DICE_TYPE_BY_LUMINANCE_RGB 0
// Doing the DICE compression in PQ (either on luminance or each color channel) produces a curve that is closer to our "perception" and leaves more detail highlights without overly compressing them
#define DICE_TYPE_BY_LUMINANCE_PQ 1
// Modern HDR displays clip individual rgb channels beyond their "white" peak brightness,
// like, if the peak brightness is 700 nits, any r g b color beyond a value of 700/80 will be clipped (not acknowledged, it won't make a difference).
// Tonemapping by luminance, is generally more perception accurate but can then generate rgb colors "out of range". This setting fixes them up,
// though it's optional as it's working based on assumptions on how current displays work, which might not be true anymore in the future.
// Note that this can create some steep (rough, quickly changing) gradients on very bright colors.
#define DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE 2
// This might look more like classic SDR tonemappers and is closer to how modern TVs and Monitors play back colors (usually they clip each individual channel to the peak brightness value, though in their native panel color space, or current SDR/HDR mode color space).
// Overall, this seems to handle bright gradients more smoothly, even if it shifts hues more (and generally desaturating).
#define DICE_TYPE_BY_CHANNEL_PQ 3

struct DICESettings
{
  uint Type;
  // Determines where the highlights curve (shoulder) starts.
  // Values between 0.25 and 0.5 are good with DICE by PQ (any type).
  // With linear/rgb DICE this barely makes a difference, zero is a good default but (e.g.) 0.5 would also work.
  // This should always be between 0 and 1.
  float ShoulderStart;

  // For "Type == DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE" only:
  // The sum of these needs to be <= 1, both within 0 and 1.
  // The closer the sum is to 1, the more each color channel will be containted within its peak range.
  float DesaturationAmount;
  float DarkeningAmount;
};

DICESettings DefaultDICESettings()
{
  DICESettings Settings;
  Settings.Type = DICE_TYPE_BY_CHANNEL_PQ;
  Settings.ShoulderStart = (Settings.Type > DICE_TYPE_BY_LUMINANCE_RGB) ? (1.f / 3.f) : 0.f; //TODOFT3: increase value!!! (did I already?)
  Settings.DesaturationAmount = 1.0 / 3.0;
  Settings.DarkeningAmount = 1.0 / 3.0;
  return Settings;
}

// Tonemapper inspired from DICE. Can work by luminance to maintain hue.
// Takes scRGB colors with a white level (the value of 1 1 1) of 80 nits (sRGB) (to not be confused with paper white).
// Paper white is expected to have already been multiplied in.
float3 DICETonemap(
  float3 Color,
  float PeakWhite,
  const DICESettings Settings /*= DefaultDICESettings()*/)
{
  const float sourceLuminance = GetLuminance(Color);

  if (Settings.Type != DICE_TYPE_BY_LUMINANCE_RGB)
  {
    static const float HDR10_MaxWhite = HDR10_MaxWhiteNits / sRGB_WhiteLevelNits;

    // We could first convert the peak white to PQ and then apply the "shoulder start" alpha to it (in PQ),
    // but tests showed scaling it in linear actually produces a better curve and more consistently follows the peak across different values
    const float shoulderStartPQ = Linear_to_PQ((Settings.ShoulderStart * PeakWhite) / HDR10_MaxWhite).x;
    if (Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ || Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE)
    {
      const float sourceLuminanceNormalized = sourceLuminance / HDR10_MaxWhite;
      const float sourceLuminancePQ = Linear_to_PQ(sourceLuminanceNormalized, 1).x;

      if (sourceLuminancePQ > shoulderStartPQ) // Luminance below the shoulder (or below zero) don't need to be adjusted
      {
        const float peakWhitePQ = Linear_to_PQ(PeakWhite / HDR10_MaxWhite).x;

        const float compressedLuminancePQ = luminanceCompress(sourceLuminancePQ, peakWhitePQ, shoulderStartPQ);
        const float compressedLuminanceNormalized = PQ_to_Linear(compressedLuminancePQ).x;
        Color *= compressedLuminanceNormalized / sourceLuminanceNormalized;

        if (Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE)
        {
          float3 Color_BT2020 = BT709_To_BT2020(Color);
          if (any(Color_BT2020 > PeakWhite)) // Optional "optimization" branch
          {
            float colorLuminance = GetLuminance(Color);
            float colorLuminanceInExcess = colorLuminance - PeakWhite;
            float maxColorInExcess = max3(Color_BT2020) - PeakWhite; // This is guaranteed to be >= "colorLuminanceInExcess"
            float brightnessReduction = saturate(safeDivision(PeakWhite, max3(Color_BT2020), 1)); // Fall back to one in case of division by zero
            float desaturateAlpha = saturate(safeDivision(maxColorInExcess, maxColorInExcess - colorLuminanceInExcess, 0)); // Fall back to zero in case of division by zero
            Color_BT2020 = lerp(Color_BT2020, colorLuminance, desaturateAlpha * Settings.DesaturationAmount);
            Color_BT2020 = lerp(Color_BT2020, Color_BT2020 * brightnessReduction, Settings.DarkeningAmount); // Also reduce the brightness to partially maintain the hue, at the cost of brightness
            Color = BT2020_To_BT709(Color_BT2020);
          }
        }
      }
    }
    else // DICE_TYPE_BY_CHANNEL_PQ
    {
      const float peakWhitePQ = Linear_to_PQ(PeakWhite / HDR10_MaxWhite).x;

      // Tonemap in BT.2020 to more closely match the primaries of modern displays
      const float3 sourceColorNormalized = BT709_To_BT2020(Color) / HDR10_MaxWhite;
      const float3 sourceColorPQ = Linear_to_PQ(sourceColorNormalized, 1);

      [unroll]
      for (uint i = 0; i < 3; i++) //TODO LUMA: optimize? will the shader compile already convert this to float3? Or should we already make a version with no branches that works in float3?
      {
        if (sourceColorPQ[i] > shoulderStartPQ) // Colors below the shoulder (or below zero) don't need to be adjusted
        {
          const float compressedColorPQ = luminanceCompress(sourceColorPQ[i], peakWhitePQ, shoulderStartPQ);
          const float compressedColorNormalized = PQ_to_Linear(compressedColorPQ).x;
          Color[i] = BT2020_To_BT709(Color[i] * (compressedColorNormalized / sourceColorNormalized[i])).x;
        }
      }
    }
  }
  else // DICE_TYPE_BY_LUMINANCE_RGB
  {
    const float shoulderStart = PeakWhite * Settings.ShoulderStart; // From alpha to linear range
    if (sourceLuminance > shoulderStart) // Luminances below the shoulder (or below zero) don't need to be adjusted
    {
      const float compressedLuminance = luminanceCompress(sourceLuminance, PeakWhite, shoulderStart);
      Color *= compressedLuminance / sourceLuminance;
    }
  }

  return Color;
}
