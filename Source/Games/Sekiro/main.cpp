#define GAME_SEKIRO 1

#define ENABLE_NGX 1
#define ENABLE_FIDELITY_SK 1

#define DISABLE_AUTO_DEBUGGER 1
// #define ENABLE_NVAPI 1
// #define DISABLE_SWAPCHAIN_FLIP_MODEL 1

#include "..\..\Core\core.hpp"

class GameSekiro final : public Game
{
public:
   void OnInit(bool async) override
   {
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      auto_recompile_defines = true;
   }

   void PrintImGuiAbout() override
   {
      
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Sekiro - Luma");
      Globals::VERSION = 1;

      force_borderless = true;
      prevent_fullscreen_state = true;
      
      swapchain_format_upgrade_type  = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      
      texture_format_upgrades_type   = TextureFormatUpgradesType::None;

      // force_disable_display_composition = true;

      game = new GameSekiro();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}