// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/RedtapeWindows.h"
#include "common/RedtapeWilCom.h"

#include "GS/GS.h"

#include <d3dcommon.h>
#include <dxgi1_5.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace DXGI
{
	// create a dxgi factory
	wil::com_ptr_nothrow<IDXGIFactory5> CreateFactory(bool debug);

	// returns a list of all adapter information
	std::vector<GSAdapterInfo> GetAdapterInfo(IDXGIFactory5* factory);

	// returns the fullscreen mode to use for the specified dimensions
	bool GetRequestedExclusiveFullscreenModeDesc(IDXGIFactory5* factory, HWND window_hwnd, u32 width, u32 height,
		float refresh_rate, DXGI_FORMAT format, DXGI_MODE_DESC* fullscreen_mode, IDXGIOutput** output);

	// get an adapter based on name
	wil::com_ptr_nothrow<IDXGIAdapter1> GetAdapterByName(IDXGIFactory5* factory, const std::string_view name);

	// returns the first adapter in the system
	wil::com_ptr_nothrow<IDXGIAdapter1> GetFirstAdapter(IDXGIFactory5* factory);

	// returns the adapter specified in the configuration, or the default
	wil::com_ptr_nothrow<IDXGIAdapter1> GetChosenOrFirstAdapter(IDXGIFactory5* factory, const std::string_view name);

	// returns a utf-8 string of the specified adapter's name
	std::string GetAdapterName(IDXGIAdapter1* adapter);

	// returns the driver version from the registry as a string
	std::string GetDriverVersionFromLUID(const LUID& luid);

	// Identifies the vendor of a DXGI adapter.
	enum class VendorID
	{
		Unknown,
		Nvidia,
		AMD,
		Intel
	};

	VendorID GetVendorID(IDXGIAdapter1* adapter);

	// Queries an adapter's feature level using D3D11, without retaining a device.
	std::optional<D3D_FEATURE_LEVEL> GetD3D11FeatureLevel(IDXGIAdapter1* adapter);
} // namespace DXGI
