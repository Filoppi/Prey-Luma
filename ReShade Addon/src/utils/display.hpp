
// Returns false if failed or if HDR is not engaged (but the white luminance can still be used).
bool GetHDRMaxLuminance(IDXGISwapChain3* swapChain, float& maxLuminance, float defaultMaxLuminance = 80.f /*Windows sRGB standard luminance*/)
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
        && desc1.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709) {
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

			RECT rect { sourceMode.position.x, sourceMode.position.y, sourceMode.position.x + (LONG)sourceMode.width, sourceMode.position.y + (LONG)sourceMode.height };
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

// Only available from Windows 11 SDK 10.0.26100.0
#if NTDDI_VERSION >= NTDDI_WIN11_GA
#else
// If c++ had "static warning" this would have been one.
static_assert(false, "Your Windows SDK is too old and lacks some features to check/engage for HDR on the display. Either upgrade to \"Windows 11 SDK 10.0.26100.0\" or disable this assert locally (the code will fall back on older features that might not work as well).");
#endif

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

#if NTDDI_VERSION >= NTDDI_WIN11_GA
bool GetColorInfo2(HWND hwnd, DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2& outColorInfo)
{
	DISPLAYCONFIG_PATH_INFO pathInfo{};
	if (GetDisplayConfigPathInfo(hwnd, pathInfo)) {
		DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 colorInfo{};
		colorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2;
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
#endif

// If HRR is enabled, it's automatically also supported
bool IsHDRSupportedAndEnabled(HWND hwnd, bool& supported, bool& enabled)
{
#if NTDDI_VERSION >= NTDDI_WIN11_GA
	// This will only succeed from Windows 11 24H2
	DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 colorInfo2{};
	if (GetColorInfo2(hwnd, colorInfo2)) {
		// Note: we don't currently consider "DISPLAYCONFIG_ADVANCED_COLOR_MODE_WCG" as an HDR mode.
		// WCG seemengly allows for a wider color range and bit depth, without a higher brightness peak,
		// but the concept seems to have mostly been deprecated (?),
		// their documentation also mentions its display referred and automatically color managed.
		// WCG displays could still benefit from running games in HDR mode, but it's
		// unclear if there's actually any out there (that support WCG but not HDR),
		// assuming its even a separate state/mode in Windows anymore.
		// Note that this variable can have a small amount of lag compared to the other ones ("DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2::highDynamicRangeUserEnabled" in particular).
		enabled = colorInfo2.activeColorMode == DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR;
#ifndef NDEBUG
		// Verify all other related states are set consistently.
		assert(!enabled || (colorInfo2.advancedColorSupported && !colorInfo2.advancedColorLimitedByPolicy && colorInfo2.highDynamicRangeSupported));
#endif
		// "HDR" falls under the umbrella of "Advanced Color" in Windows, thus if advanced color is "blocked" so is HDR (and WCG).
		// This implies we don't need to check for "DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2::advancedColorSupported" as checking for HDR support is enough.
		// The "DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2::highDynamicRangeUserEnabled" flag, while theoretically should only be true if a user manually enabled HDR on the display,
		// it's actually set to true even when HDR is enabled by an app through these functions, so theoretically we could check that too, but it wouldn't be reliable enough and it might change in the future.
		supported = enabled || (colorInfo2.highDynamicRangeSupported && !colorInfo2.advancedColorLimitedByPolicy);
		return true;
	}
#endif

	// Older Windows versions need to fall back to a simpler implementation.
	DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
	if (GetColorInfo(hwnd, colorInfo)) {
		enabled = colorInfo.advancedColorEnabled;
		assert(!enabled || (colorInfo.advancedColorSupported && !colorInfo.advancedColorForceDisabled));
		supported = enabled || (colorInfo.advancedColorSupported && !colorInfo.advancedColorForceDisabled);
		return true;
	}

	// Unknown/failed state, default to not supported
	supported = false;
	enabled = false;
	return false;
}

// Returns true if the display has been succesfully set to an HDR mode, or if it already was.
// Returns false in case of an unknown error.
bool SetHDREnabled(HWND hwnd)
{
#if NTDDI_VERSION >= NTDDI_WIN11_GA
	// This will only succeed from Windows 11 24H2
	DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 colorInfo2{};
	if (GetColorInfo2(hwnd, colorInfo2)) {
		if (colorInfo2.highDynamicRangeSupported && !colorInfo2.advancedColorLimitedByPolicy && colorInfo2.activeColorMode != DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR) {
			DISPLAYCONFIG_SET_HDR_STATE setHDRState{};
			setHDRState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_HDR_STATE;
			setHDRState.header.size = sizeof(setHDRState);
			setHDRState.header.adapterId = colorInfo2.header.adapterId;
			setHDRState.header.id = colorInfo2.header.id;
			setHDRState.enableHdr = true;
			bool enabled = (ERROR_SUCCESS == DisplayConfigSetDeviceInfo(&setHDRState.header));
#ifndef NDEBUG
			// Verify that Windows reports HDR as enabled by the user, even if it was an application to enable it.
			// The function above seemengly turns on "DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2::highDynamicRangeUserEnabled" too.
			assert(!enabled || !GetColorInfo2(hwnd, colorInfo2) || colorInfo2.activeColorMode == DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR);
#endif
			return enabled;
		}
		return colorInfo2.activeColorMode == DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR;
	}
#endif

	// Note: older Windows versions didn't allow to distinguish between HDR and "Advanced Color",
	// so it seems like this possibly has a small chance of breaking your display state until you manually toggle HDR again or change resolution etc.
	// It's not clear if that was a separate issue or if it was caused by a mismatch between HDR and WCG modes.
	DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
	if (GetColorInfo(hwnd, colorInfo)) {
		if (colorInfo.advancedColorSupported && !colorInfo.advancedColorForceDisabled && !colorInfo.advancedColorEnabled) {
			DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE setAdvancedColorState{};
			setAdvancedColorState.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
			setAdvancedColorState.header.size = sizeof(setAdvancedColorState);
			setAdvancedColorState.header.adapterId = colorInfo.header.adapterId;
			setAdvancedColorState.header.id = colorInfo.header.id;
			setAdvancedColorState.enableAdvancedColor = true;
			bool enabled = (ERROR_SUCCESS == DisplayConfigSetDeviceInfo(&setAdvancedColorState.header));
			return enabled;
		}
		return colorInfo.advancedColorEnabled;
	}

	return false;
}