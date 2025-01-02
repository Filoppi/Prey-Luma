#pragma once

#include "math.h"
#include "matrix.h"

namespace
{
   typedef uint32_t uint;

   struct uint2
   {
      uint32_t x;
      uint32_t y;

      friend bool operator==(const uint2& lhs, const uint2& rhs)
      {
         return lhs.x == rhs.x && lhs.y == rhs.y;
      }
   };

   struct float2
   {
      float x;
      float y;

      friend bool operator==(const float2& lhs, const float2& rhs)
      {
         return lhs.x == rhs.x && lhs.y == rhs.y;
      }
   };

   struct float3
   {
      float x;
      float y;
      float z;

      friend bool operator==(const float3& lhs, const float3& rhs)
      {
         return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
      }
   };

   struct float4
   {
      float x;
      float y;
      float z;
      float w;

      friend bool operator==(const float4& lhs, const float4& rhs)
      {
         return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
      }
   };

   // See shaders for comments on these
   struct CBPerViewGlobal
   {
      Matrix44A  CV_ViewProjZeroMatr;
      float4    CV_AnimGenParams;

      Matrix44A  CV_ViewProjMatr;
      Matrix44A  CV_ViewProjNearestMatr;
      Matrix44A  CV_InvViewProj;
      Matrix44A  CV_PrevViewProjMatr;
      Matrix44A  CV_PrevViewProjNearestMatr;
      /*Matrix34A*/ float4 CV_ScreenToWorldBasis[3];
      float4    CV_TessInfo;
      float4    CV_CameraRightVector;
      float4    CV_CameraFrontVector;
      float4    CV_CameraUpVector;

      float4    CV_ScreenSize;
      float4    CV_HPosScale;
      float4    CV_HPosClamp;
      float4    CV_ProjRatio;
      float4    CV_NearestScaled;
      float4    CV_NearFarClipDist;

      float4    CV_SunLightDir;
      float4    CV_SunColor;
      float4    CV_SkyColor;
      float4    CV_FogColor;
      float4    CV_TerrainInfo;

      float4    CV_DecalZFightingRemedy;

      Matrix44A  CV_FrustumPlaneEquation;

      float4    CV_WindGridOffset;

      Matrix44A  CV_ViewMatr;
      Matrix44A  CV_InvViewMatr;

      float     CV_LookingGlass_SunSelector;
      float     CV_LookingGlass_DepthScalar;

      float     CV_PADDING0;
      float     CV_PADDING1;
   };
   constexpr uint32_t CBPerViewGlobal_buffer_size = 1024; // This is how much CryEngine allocates for buffers that hold this (it doesn't use "sizeof(CBPerViewGlobal)")
   static_assert(CBPerViewGlobal_buffer_size > sizeof(CBPerViewGlobal));

   struct LumaFrameDevSettings
   {
      static constexpr size_t SettingsNum = 10;

      LumaFrameDevSettings(float Value = 0.f)
      {
         for (size_t i = 0; i < SettingsNum; i++)
         {
            Settings[i] = Value;
         }
      }
      float& operator[](const size_t i)
      {
         return Settings[i];
      }
      float Settings[SettingsNum];
   };

   struct LumaFrameSettings
   {
      uint32_t DisplayMode;
      float ScenePeakWhite;
      float ScenePaperWhite;
      float UIPaperWhite;
      uint32_t DLSS;
      uint32_t LensDistortion;
#if DEVELOPMENT // In case we disabled the "DEVELOPMENT" shader define while the code is compiled in "DEVELOPMENT" mode, we'll simply push values that aren't read by shaders
      LumaFrameDevSettings DevSettings;
#else
      float2 Padding = { 0, 0 };
#endif
   };
   static_assert(sizeof(LumaFrameSettings) % sizeof(uint32_t) == 0); // ReShade limitation, we probably don't depend on these anymore, still, it's not bad to have 4 bytes alignment, even if cbuffers are seemengly 8 byte aligned?
   static_assert(sizeof(LumaFrameSettings) % (sizeof(uint32_t) * 4) == 0); // Needed by DX?
   static_assert(sizeof(LumaFrameSettings) >= 16); // Needed by DX (there's a minimum size of 16 byte)

   //TODOFT: rename to PassData or something
   struct LumaFrameData
   {
      uint32_t PostEarlyUpscaling;
      uint32_t CustomData; // Per call data
      uint32_t Padding;
      uint32_t FrameIndex;
      float2 CameraJitters;
      float2 PreviousCameraJitters;
      float2 RenderResolutionScale;
      float2 PreviousRenderResolutionScale;
#if 0 // Disabled in shaders too as they are currently unused
      Matrix44A ViewProjectionMatrix;
      Matrix44A PreviousViewProjectionMatrix;
#endif
      Matrix44A ReprojectionMatrix;
   };
   static_assert(sizeof(LumaFrameData) % sizeof(uint32_t) == 0);
   static_assert(sizeof(LumaFrameData) % (sizeof(uint32_t) * 4) == 0);
   static_assert(sizeof(LumaFrameData) >= 16);

   struct LumaUIData
   {
      uint32_t drawing_on_swapchain = 0;
      uint32_t blend_mode = 0;
      float background_tonemapping_amount = 0.f;
      uint32_t padding = 0;
   };
   static_assert(sizeof(LumaUIData) % sizeof(uint32_t) == 0);
   static_assert(sizeof(LumaUIData) % (sizeof(uint32_t) * 4) == 0);
   static_assert(sizeof(LumaUIData) >= 16);
}