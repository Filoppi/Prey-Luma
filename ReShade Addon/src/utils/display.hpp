
// Returns false if failed or if HDR is not enaged (but the white luminance can still be used).
bool GetHDRMaxLuminance(IDXGISwapChain3* swapChain, float& maxLuminance, float defaultMaxLuminance = 80.f)
{
	maxLuminance = defaultMaxLuminance;
    
    IDXGIOutput* output = nullptr;
    if (FAILED(swapChain->GetContainingOutput(&output))) {
        return false;
    }

    IDXGIOutput6* output6 = nullptr;
    if (FAILED(output->QueryInterface(&output6))) {
        return false;
    }

    DXGI_OUTPUT_DESC1 desc1;
    if (FAILED(output6->GetDesc1(&desc1))) {
        return false;
    }

    // Note: this might end up being outdated if a new display is added/removed,
    // or if HDR is toggled on them after swapchain creation (though it seems to be consistent between SDR and HDR).
    maxLuminance = desc1.MaxLuminance;

    // HDR is not supported (this only works if HDR is enaged on the monitor that currently contains the swapchain)
    if (desc1.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
        && desc1.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)
    {
        return false;
    }

    return true;
}

bool GetDisplayConfigPathInfo(HWND hwnd, DISPLAYCONFIG_PATH_INFO& outPathInfo)
{
	uint32_t pathCount, modeCount;
	if (ERROR_SUCCESS != GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount)) {
		return false;
	}

	std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
	std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
	if (ERROR_SUCCESS != QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr)) {
		return false;
	}

	const HMONITOR monitorFromWindow = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
	for (auto& pathInfo : paths) {
		if (pathInfo.flags & DISPLAYCONFIG_PATH_ACTIVE && pathInfo.sourceInfo.statusFlags & DISPLAYCONFIG_SOURCE_IN_USE) {
			const bool bVirtual = pathInfo.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE;
			const DISPLAYCONFIG_SOURCE_MODE& sourceMode = modes[bVirtual ? pathInfo.sourceInfo.sourceModeInfoIdx : pathInfo.sourceInfo.modeInfoIdx].sourceMode;

			RECT rect { sourceMode.position.x, sourceMode.position.y, sourceMode.position.x + sourceMode.width, sourceMode.position.y + sourceMode.height };
			if (!IsRectEmpty(&rect)) {
				const HMONITOR monitorFromMode = MonitorFromRect(&rect, MONITOR_DEFAULTTONULL);
				if (monitorFromMode != nullptr && monitorFromMode == monitorFromWindow) {
					outPathInfo = pathInfo;
					return true;
				}
			}
		}
	}

	return false;
}

bool GetColorInfo(HWND hwnd, DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO& outColorInfo)
{
	DISPLAYCONFIG_PATH_INFO pathInfo{};
	if (GetDisplayConfigPathInfo(hwnd, pathInfo)) {
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
		colorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
		colorInfo.header.size = sizeof(colorInfo);
		colorInfo.header.adapterId = pathInfo.targetInfo.adapterId;
		colorInfo.header.id = pathInfo.targetInfo.id;
		auto result = DisplayConfigGetDeviceInfo(&colorInfo.header);
		if (result == ERROR_SUCCESS) {
			outColorInfo = colorInfo;
			return true;
		}
	}
	return false;
}

bool IsHDRSupported(HWND hwnd)
{
	DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
	if (GetColorInfo(hwnd, colorInfo)) {
		return colorInfo.advancedColorSupported;
	}
	return false;
}

bool IsHDREnabled(HWND hwnd)
{
	DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
	if (GetColorInfo(hwnd, colorInfo)) {
		return colorInfo.advancedColorEnabled;
	}
	return false;
}

bool SetHDREnabled(HWND hwnd)
{
	DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
	if (GetColorInfo(hwnd, colorInfo)) {
		if (colorInfo.advancedColorSupported && !colorInfo.advancedColorEnabled) {
			DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE setColorState{};
			setColorState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
			setColorState.header.size = sizeof(setColorState);
			setColorState.header.adapterId = colorInfo.header.adapterId;
			setColorState.header.id = colorInfo.header.id;
			setColorState.enableAdvancedColor = true;
			return ERROR_SUCCESS == DisplayConfigSetDeviceInfo(&setColorState.header);
		}

		return colorInfo.advancedColorEnabled;
	}
	return false;
}