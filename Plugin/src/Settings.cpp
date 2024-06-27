#include "Settings.h"

#define ICON_FK_UNDO reinterpret_cast<const char*>(u8"\uf0e2")

namespace Settings
{
	std::string IntSlider::GetSliderText() const
	{
		return std::format("{}{}", value.get_data(), suffix);
	}

    std::string FloatSlider::GetSliderText() const
    {
		return std::format("{:.0f}{}", value.get_data(), suffix);
    }

    void Main::RegisterReshadeOverlay()
    {
		if (!bReshadeSettingsOverlayRegistered) {
			HMODULE hModule = nullptr;
			GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(Main::GetSingleton()), &hModule);
			if (hModule) {
				if (reshade::register_addon(hModule)) {
					reshade::register_overlay("Luma Settings", DrawReshadeSettings);
					bReshadeSettingsOverlayRegistered = true;
				}
			}
		}
    }

    void Main::Load() noexcept
	{
		static std::once_flag ConfigInit;
		std::call_once(ConfigInit, [&]() {
			config.Bind(PeakBrightness.value, PeakBrightness.defaultValue);
			config.Bind(GamePaperWhite.value, GamePaperWhite.defaultValue);
			config.Bind(UILuminance.value, UILuminance.defaultValue);
			config.Bind(ExtendGamut.value, ExtendGamut.defaultValue);
			config.Bind(ExtendGamutTarget.value, ExtendGamutTarget.defaultValue);
		});

		config.Load();

		INFO("Config loaded"sv)
	}

    void Main::Save() noexcept
    {
		config.Generate();
		config.Write();
    }

    void Main::DrawReshadeSettings(reshade::api::effect_runtime*)
    {
        const auto settings = Settings::Main::GetSingleton();
		settings->DrawReshadeSettings();
    }

    void Main::DrawReshadeTooltip(const char* a_desc)
	{
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8, 8 });
			if (ImGui::BeginTooltip()) {
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 50.0f);
				ImGui::TextUnformatted(a_desc);
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			ImGui::PopStyleVar();
		}
	}

    bool Main::DrawReshadeIntSlider(IntSlider& a_slider)
    {
		bool result = false;
		int32_t tempValue = *a_slider.value;
		if (ImGui::SliderInt(a_slider.name.c_str(), &tempValue, a_slider.sliderMin, a_slider.sliderMax)) {
			*a_slider.value = tempValue;
			Save();
			result = true;
		}
		DrawReshadeTooltip(a_slider.description.c_str());
		if (DrawReshadeResetButton(a_slider)) {
			*a_slider.value = a_slider.defaultValue;
			Save();
			result = true;
		}
		return result;
    }

    bool Main::DrawReshadeFloatSlider(FloatSlider& a_slider)
    {
		bool result = false;
		float tempValue = *a_slider.value;
		if (ImGui::SliderFloat(a_slider.name.c_str(), &tempValue, a_slider.sliderMin, a_slider.sliderMax, "%.0f")) {
			*a_slider.value = tempValue;
			Save();
			result = true;
		}
		DrawReshadeTooltip(a_slider.description.c_str());
		if (DrawReshadeResetButton(a_slider)) {
			*a_slider.value = a_slider.defaultValue;
			Save();
			result = true;
		}
		return result;
    }

    bool Main::DrawReshadeResetButton(Setting& a_setting)
	{
		bool bResult = false;
		ImGui::SameLine();
		ImGui::PushID(&a_setting);
		if (!a_setting.IsDefault()) {
			if (ImGui::SmallButton(ICON_FK_UNDO)) {
				bResult = true;
			}
		} else {
			const auto& style = ImGui::GetStyle();
			const float width = ImGui::CalcTextSize(ICON_FK_UNDO).x + style.FramePadding.x * 2.f;
			ImGui::InvisibleButton("", ImVec2(width, 0));
		}
		ImGui::PopID();
		return bResult;
	}

    void Main::DrawReshadeSettings()
    {
		ImGui::SetWindowSize(ImVec2(0, 0));
		const auto& io = ImGui::GetIO();
		const auto currentPos = ImGui::GetWindowPos();
		ImGui::SetWindowPos(ImVec2(io.DisplaySize.x / 3, currentPos.y), ImGuiCond_FirstUseEver);

		DrawReshadeIntSlider(PeakBrightness);
		DrawReshadeIntSlider(GamePaperWhite);
		DrawReshadeIntSlider(UILuminance);
		DrawReshadeFloatSlider(ExtendGamut);
		DrawReshadeIntSlider(ExtendGamutTarget);
    }
}
