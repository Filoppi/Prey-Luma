#define GAME_SEKIRO 1

#define ENABLE_NGX 1
#define ENABLE_FIDELITY_SK 1

#define DISABLE_AUTO_DEBUGGER 1
// #define ENABLE_NVAPI 1
// #define DISABLE_SWAPCHAIN_FLIP_MODEL 1

#include "..\..\Core\core.hpp"

namespace
{
   void DrawColoredSubHeader(const char* label, const ImVec4& color = ImColor(128, 255, 255, 255))
   {
      ImGui::PushStyleColor(ImGuiCol_Text, color);
      ImGui::Text("[%s]", label);
      ImGui::PopStyleColor();
   }

   bool DrawCollapsingHeaderEnabledColored(const char* label, bool enabled, bool is_default_open = false)
   {
      if (enabled) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.4f, 0.8f, 1.f));
      bool is_open = ImGui::CollapsingHeader(label, is_default_open ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
      if (enabled) ImGui::PopStyleColor();
      return is_open;
   }
   
   float GetPulseMultiplier(float speed = 0.1f, float amount = 0.25f)
   {
      return (1.f-amount/2) + amount/2 * sinf(cb_luma_global_settings.FrameIndex * speed);
   }

   bool IsModEnabled() {return custom_shaders_enabled || !ignore_indirect_upgraded_textures || !ignore_upgraded_samplers;}

   namespace ShaderDefineInfo
   {
      constexpr uint32_t TONEMAP_BT2020 = char_ptr_crc32("TONEMAP_BT2020");

      void OnInit()
      {
         std::vector<ShaderDefineData> game_shader_defines_data = {
            {"TONEMAP_BT2020", '0', true, !DEVELOPMENT, "Do per-channel tonemap in BT2020 instead of BT709.", 1},
         };
         shader_defines_data.append_range(game_shader_defines_data);
         auto_recompile_defines = true; //force
         assert(shader_defines_data.size() < MAX_SHADER_DEFINES);
         
         // Default built-in
         GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1'); // 2.2
      }

      static char InvertCharBool(char b)
      {
         return b == '0' ? '1' : '0'; 
      }
      
      //This feels dumb O(n) everytime, but it is the most consistent.
      static int Get(uint32_t p)
      {
         auto* d = &GetShaderDefineData(p);
         return d->editable_data.value[0] - '0';
      }

      static bool GetB(uint32_t p)
      {
         return Get(p) > 0;
      }

      static void Set(uint32_t p, char c)
      {
         auto* d = &GetShaderDefineData(p);
         if (d->editable_data.value[0] == c) return;
         d->SetValue(c);
         defines_need_recompilation = true;
      }
      
      static void Set(uint32_t p, int i)
      {
         auto* d = &GetShaderDefineData(p);
         char c = static_cast<char>(i + '0');
         if (d->editable_data.value[0] == c) return;
         d->SetValue(c);
         defines_need_recompilation = true;
      }

      static void ToggleBool(uint32_t p)
      {
         auto* d = &GetShaderDefineData(p);
         d->SetValue(InvertCharBool(d->editable_data.value[0]));
         defines_need_recompilation = true;
      }

      static void UIResetButton(uint32_t p)
      {
         auto* d = &GetShaderDefineData(p);
         if (d->editable_data.value[0] != d->default_data.value[0]) {
            int id = static_cast<int>(reinterpret_cast<uintptr_t>(d));
            ImGui::PushID(id);
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FK_UNDO))
            {
               d->Reset();
               defines_need_recompilation = true;
            }
            ImGui::PopID();
         }
      }

      static bool UIToggleCheckmark(uint32_t d, const char* label, const char* tooltip)
      {
         bool def = GetB(d);
         
         ImGui::PushID(std::string(label).append("_").append(std::to_string(d)).c_str());
         bool c = ImGui::Checkbox(label, &def);
         ImGui::PopID();

         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(tooltip);
         
         if (c) ToggleBool(d);
         
         UIResetButton(d);
         return def;
      }
         
      int UIDropDown(uint32_t d, const char* label, const char* const items[], const char* tooltip)
      {
         int def = Get(d);
         bool c = ImGui::Combo(label, &def, items, IM_ARRAYSIZE(items));
         if (c) Set(d, def);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(tooltip);
         UIResetButton(d);
         return def;
      }

      // Overload: pass items inline as braced args, e.g. {"A", "B", "C"}
      int UIDropDown(uint32_t d, const char* label, std::initializer_list<const char*> items_list, const char* tooltip)
      {
         std::vector<const char*> items(items_list);
         int def = Get(d);
         bool c = ImGui::Combo(label, &def, items.data(), static_cast<int>(items.size()));
         if (c) Set(d, def);
         if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(tooltip);
         UIResetButton(d);
         return def;
      }
   }

   namespace ConstBuffer
   {
      void OnInit()
      {
         luma_settings_cbuffer_index = 13;
         luma_data_cbuffer_index = 12;
         
         cb_luma_global_settings.GameSettings.UIBrightnessRatio = default_luma_global_game_settings.UIBrightnessRatio = 1.0f;
      }

      void OnLoad()
      {
         reshade::get_config_value(nullptr, NAME, "UIBrightnessRatio", cb_luma_global_settings.GameSettings.UIBrightnessRatio);
      }
   }
   
   namespace ResourceGather
   {
      void OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t ps, uint32_t vs, uint32_t cs)
      {
         // AO Normals Precalc: 0xE7CB978C
         // SRV1: Normals, rgb10a2, full res
      
         // AO resolve: 0xAE3D13DB
         // SRV0: depth, r32g8x24, full res
         // RTV0: AO results, rgba8, full res

         // TAA0: 0xAD75DCC2
         // SRV2: depth, r32g8x24, full res (same as AO)
         
         // TAA1: 0x113E91AB
         // SRV0: current color, r11g11b10, full res
         
         // SRV3: motion vectors, rg16, full res
         // TAA2: 0xCC15C41B (copies to history or something whatever...)
      }
   }

   namespace SRImp
   {
      enum State : uint8_t
      {
         FirstPS, //0xAD75DCC2
         SecondPS, //0x113E91AB
         ThirdPS, //0xCC15C41B
      };
   }

   namespace MainColor16f
   {
      namespace Resources
      {
         ComPtr<ID3D11ShaderResourceView> srv0;
         ComPtr<ID3D11RenderTargetView> rtv0;

         ComPtr<ID3D11ShaderResourceView> srv1;
         ComPtr<ID3D11RenderTargetView> rtv1;

         bool IsValid() { return srv0.get(); }
         
         void Create(uint2 size, ID3D11Device* native_device)
         {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = size.x;
            desc.Height = size.y;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = 0;

            ComPtr<ID3D11Texture2D> tex;
            auto hr0 = native_device->CreateTexture2D(&desc, nullptr, tex.put());
            ASSERT_MSG(hr0 >= 0, "MainColor16f::Resources::Create hr0");
            auto hr1 = native_device->CreateShaderResourceView(tex.get(), nullptr, srv0.put());
            ASSERT_MSG(hr1 >= 0, "MainColor16f::Resources::Create hr1");
            auto hr2 = native_device->CreateRenderTargetView(tex.get(), nullptr, rtv0.put());
            ASSERT_MSG(hr2 >= 0, "MainColor16f::Resources::Create hr2");

            auto hr3 = native_device->CreateTexture2D(&desc, nullptr, tex.put());
            ASSERT_MSG(hr3 >= 0, "MainColor16f::Resources::Create hr3");
            auto hr4 = native_device->CreateShaderResourceView(tex.get(), nullptr, srv1.put());
            ASSERT_MSG(hr4 >= 0, "MainColor16f::Resources::Create hr4");
            auto hr5 = native_device->CreateRenderTargetView(tex.get(), nullptr, rtv1.put());
            ASSERT_MSG(hr5 >= 0, "MainColor16f::Resources::Create hr5");
         }

         void Reset()
         {
            srv0.reset();
            rtv0.reset();
            srv1.reset();
            rtv1.reset();
         }
      }

      void OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, uint32_t ps, uint32_t vs, uint32_t cs)
      {
         if (cb_luma_global_settings.DisplayMode != DisplayModeType::HDR) return;
         
         if (ps == 0x775D9A9E) //tonemap
         {
            [[unlikely]] if (!Resources::IsValid())
            {
               //get RTV0 for size and Create
               ComPtr<ID3D11RenderTargetView> rtv0;
               native_device_context->OMGetRenderTargets(1, rtv0.put(), nullptr);
               
               ID3D11Resource* rtv0_resource = nullptr;
               rtv0->GetResource(&rtv0_resource);
               
               ComPtr<ID3D11Texture2D> tex;
               auto hr = rtv0_resource->QueryInterface(IID_PPV_ARGS(tex.put()));
               ASSERT_MSG(hr >= 0, "MainColor16f::OnDrawOrDispatch QueryInterface");
               
               D3D11_TEXTURE2D_DESC desc = {};
               tex->GetDesc(&desc);
               
               Resources::Create(uint2(desc.Width, desc.Height), native_device);
            }

            // replace RTV0 with our own
            native_device_context->OMSetRenderTargets(1, &Resources::rtv0, nullptr);
         }
         else if (ps == 0xDB1689BD) //ui composite
         {
            [[unlikely]] if (!Resources::IsValid()) return;
            
            // replace SRV0 with our own
            native_device_context->PSSetShaderResources(0, 1, &Resources::srv0);
            
            // replace RTV0 with our own
            native_device_context->OMSetRenderTargets(1, &Resources::rtv1, nullptr);
         }
         else if (ps == 0x49231113) //to swapchain
         {
            [[unlikely]] if (!Resources::IsValid()) return;

            // replace SRV0 with our own
            native_device_context->PSSetShaderResources(0, 1, &Resources::srv1);
         }
      }

      void HardReset() 
      {
         Resources::Reset();
      }
   }
}

class GameSekiro final : public Game
{
public:
   void OnInit(bool async) override
   {
      // ConstBuffers
      ConstBuffer::OnInit();

      // Shader Defines
      ShaderDefineInfo::OnInit();
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      uint32_t ps = original_shader_hashes.pixel_shaders[0];
      uint32_t vs = original_shader_hashes.vertex_shaders[0];
      uint32_t cs = original_shader_hashes.compute_shaders[0];

      // MainColor16f
      MainColor16f::OnDrawOrDispatch(native_device, native_device_context, cmd_list_data, device_data, ps, vs, cs);

      // HDR Reinhard LUT Builder
      if (ps == 0xC0C87BF5)
      {
         if (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
         {
            // make viewport width of 3 only
            D3D11_VIEWPORT viewport = {};
            viewport.Width = 3.0f;
            viewport.Height = 1.0f;
            native_device_context->RSSetViewports(1, &viewport);
         }
      }

      // set DisplayMode based on UI Composite shader
      if (ps == 0xDB1689BD) cb_luma_global_settings.DisplayMode = DisplayModeType::HDR;
      else if (ps == 0xF38F291F) cb_luma_global_settings.DisplayMode = DisplayModeType::SDR;

      return DrawOrDispatchOverrideType::None;
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      // MainColor16f
      MainColor16f::HardReset();
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {

   }

   void LoadConfigs() override
   {
      // ConstBuffers
      ConstBuffer::OnLoad();
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      ImGui::Separator();

      ShaderDefineInfo::UIToggleCheckmark(ShaderDefineInfo::TONEMAP_BT2020, "Tonemap BT2020", "Do per-channel tonemap in BT2020 instead of BT709.");

      if (bool is_need_clamp_1 = cb_luma_global_settings.GameSettings.UIBrightnessRatio > 1.0f && cb_luma_global_settings.DisplayMode == DisplayModeType::SDR;
         ImGui::SliderFloat("UI Brightness Ratio", &cb_luma_global_settings.GameSettings.UIBrightnessRatio, 0.0f, cb_luma_global_settings.DisplayMode == DisplayModeType::HDR ? 2.0f : 1.0f, "%.2f") || is_need_clamp_1)
      {
         if (is_need_clamp_1) cb_luma_global_settings.GameSettings.UIBrightnessRatio = 1.0f;
         reshade::set_config_value(nullptr, NAME, "UIBrightnessRatio", cb_luma_global_settings.GameSettings.UIBrightnessRatio);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Simple brightness multiplier on UI.");
      DrawResetButton(cb_luma_global_settings.GameSettings.UIBrightnessRatio, default_luma_global_game_settings.UIBrightnessRatio, nullptr);
      
      if (DEVELOPMENT) ImGui::Separator();
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
      swapchain_upgrade_type         = SwapchainUpgradeType::scRGB;
      
      texture_format_upgrades_type = TextureFormatUpgradesType::None;
      
      game = new GameSekiro();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}