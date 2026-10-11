// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/Common/DXGI.h"
#include "GS/Renderers/Common/GSDevice.h"

#include "common/Console.h"
#include "common/StringUtil.h"

#include "fmt/format.h"

#include <algorithm>
#include <cmath>
#include <d3d11.h>
#include <iterator>

wil::com_ptr_nothrow<IDXGIFactory5> DXGI::CreateFactory(bool debug)
{
	UINT flags = 0;
	if (debug)
		flags |= DXGI_CREATE_FACTORY_DEBUG;

	wil::com_ptr_nothrow<IDXGIFactory5> factory;
	const HRESULT hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(factory.put()));
	if (FAILED(hr))
		Console.Error("D3D: Failed to create DXGI factory: %08X", hr);

	return factory;
}

static std::string FixupDuplicateAdapterNames(const std::vector<GSAdapterInfo>& adapters, std::string adapter_name)
{
	if (std::any_of(adapters.begin(), adapters.end(),
			[&adapter_name](const GSAdapterInfo& other) { return (adapter_name == other.name); }))
	{
		std::string original_adapter_name = std::move(adapter_name);

		u32 current_extra = 2;
		do
		{
			adapter_name = fmt::format("{} ({})", original_adapter_name.c_str(), current_extra);
			current_extra++;
		} while (std::any_of(adapters.begin(), adapters.end(),
			[&adapter_name](const GSAdapterInfo& other) { return (adapter_name == other.name); }));
	}

	return adapter_name;
}

std::vector<GSAdapterInfo> DXGI::GetAdapterInfo(IDXGIFactory5* factory)
{
	std::vector<GSAdapterInfo> adapters;

	wil::com_ptr_nothrow<IDXGIAdapter1> adapter;
	for (u32 index = 0;; index++)
	{
		HRESULT hr = factory->EnumAdapters1(index, adapter.put());
		if (hr == DXGI_ERROR_NOT_FOUND)
			break;

		if (FAILED(hr))
		{
			ERROR_LOG("IDXGIFactory2::EnumAdapters() returned {:08X}", static_cast<unsigned>(hr));
			continue;
		}

		GSAdapterInfo ai;
		ai.name = FixupDuplicateAdapterNames(adapters, GetAdapterName(adapter.get()));

		// Unfortunately we can't get any properties such as feature level without creating the device.
		// So just assume a max of the D3D11 max across the board.
		ai.max_texture_size = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
		ai.max_upscale_multiplier = GSGetMaxUpscaleMultiplier(ai.max_texture_size);

		wil::com_ptr_nothrow<IDXGIOutput> output;
		// Only check the first output, which would be the primary display (if any is connected)
		if (SUCCEEDED(hr = adapter->EnumOutputs(0, &output)))
		{
			UINT num_modes = 0;
			if (SUCCEEDED(hr = output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &num_modes, nullptr)))
			{
				std::vector<DXGI_MODE_DESC> dmodes(num_modes);
				if (SUCCEEDED(hr = output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &num_modes, dmodes.data())))
				{
					for (const DXGI_MODE_DESC& mode : dmodes)
					{
						ai.fullscreen_modes.push_back(GSDevice::GetFullscreenModeString(mode.Width, mode.Height,
							static_cast<float>(mode.RefreshRate.Numerator) / static_cast<float>(mode.RefreshRate.Denominator)));
					}
				}
				else
				{
					ERROR_LOG("GetDisplayModeList() (2) failed: {:08X}", static_cast<unsigned>(hr));
				}
			}
			else
			{
				ERROR_LOG("GetDisplayModeList() failed: {:08X}", static_cast<unsigned>(hr));
			}
		}
		else if (hr != DXGI_ERROR_NOT_FOUND)
		{
			ERROR_LOG("EnumOutputs() failed: {:08X}", static_cast<unsigned>(hr));
		}

		adapters.push_back(std::move(ai));
	}

	return adapters;
}

bool DXGI::GetRequestedExclusiveFullscreenModeDesc(IDXGIFactory5* factory, HWND window_hwnd, u32 width,
	u32 height, float refresh_rate, DXGI_FORMAT format, DXGI_MODE_DESC* fullscreen_mode, IDXGIOutput** output)
{
	// We need to find which monitor the window is located on.
	// DXGI seems to use the nearest monitor if the window is out of bounds.
	const auto* monitor = MonitorFromWindow(window_hwnd, MONITOR_DEFAULTTONEAREST);

	// The monitor might be on a different adapter to which we are rendering.. so we have to enumerate them all.
	HRESULT hr;
	wil::com_ptr_nothrow<IDXGIOutput> first_output, monitor_output;

	for (u32 adapter_index = 0; !monitor_output; adapter_index++)
	{
		wil::com_ptr_nothrow<IDXGIAdapter1> adapter;
		hr = factory->EnumAdapters1(adapter_index, adapter.put());
		if (hr == DXGI_ERROR_NOT_FOUND)
			break;
		else if (FAILED(hr))
			continue;

		for (u32 output_index = 0;; output_index++)
		{
			wil::com_ptr_nothrow<IDXGIOutput> this_output;
			DXGI_OUTPUT_DESC output_desc;
			hr = adapter->EnumOutputs(output_index, this_output.put());
			if (hr == DXGI_ERROR_NOT_FOUND)
				break;
			else if (FAILED(hr) || FAILED(this_output->GetDesc(&output_desc)))
				continue;

			if (output_desc.Monitor == monitor)
			{
				monitor_output = std::move(this_output);
				break;
			}

			// Fallback to the first monitor.
			if (!first_output)
				first_output = std::move(this_output);
		}
	}

	if (!monitor_output)
	{
		if (!first_output)
		{
			Console.Error("No DXGI output found. Can't use exclusive fullscreen.");
			return false;
		}

		Console.Warning("No DXGI output found for window, using first.");
		monitor_output = std::move(first_output);
	}

	DXGI_MODE_DESC request_mode = {};
	request_mode.Width = width;
	request_mode.Height = height;
	request_mode.Format = format;
	request_mode.RefreshRate.Numerator = static_cast<UINT>(std::floor(refresh_rate * 1000.0f));
	request_mode.RefreshRate.Denominator = 1000u;

	if (FAILED(hr = monitor_output->FindClosestMatchingMode(&request_mode, fullscreen_mode, nullptr)) ||
		request_mode.Format != format)
	{
		ERROR_LOG("Failed to find closest matching mode, hr={:08X}", static_cast<unsigned>(hr));
		return false;
	}

	*output = monitor_output.get();
	monitor_output->AddRef();
	return true;
}

wil::com_ptr_nothrow<IDXGIAdapter1> DXGI::GetAdapterByName(IDXGIFactory5* factory, const std::string_view name)
{
	if (name.empty() || name == GetDefaultAdapter())
		return {};

	// This might seem a bit odd to cache the names.. but there's a method to the madness.
	// We might have two GPUs with the same name... :)
	std::vector<GSAdapterInfo> adapter_names;

	wil::com_ptr_nothrow<IDXGIAdapter1> adapter;
	for (u32 index = 0;; index++)
	{
		const HRESULT hr = factory->EnumAdapters1(index, adapter.put());
		if (hr == DXGI_ERROR_NOT_FOUND)
			break;

		if (FAILED(hr))
		{
			ERROR_LOG("IDXGIFactory2::EnumAdapters() returned {:08X}", static_cast<unsigned>(hr));
			continue;
		}

		GSAdapterInfo ai;
		ai.name = FixupDuplicateAdapterNames(adapter_names, GetAdapterName(adapter.get()));
		if (ai.name == name)
		{
			INFO_LOG("D3D: Found adapter '{}'", ai.name);
			return adapter;
		}

		adapter_names.push_back(std::move(ai));
	}

	Console.Warning(fmt::format("Adapter '{}' not found.", name));
	return {};
}

wil::com_ptr_nothrow<IDXGIAdapter1> DXGI::GetFirstAdapter(IDXGIFactory5* factory)
{
	wil::com_ptr_nothrow<IDXGIAdapter1> adapter;
	HRESULT hr = factory->EnumAdapters1(0, adapter.put());
	if (FAILED(hr))
		Console.Error(fmt::format("IDXGIFactory2::EnumAdapters() for first adapter returned %08X", hr));

	return adapter;
}

wil::com_ptr_nothrow<IDXGIAdapter1> DXGI::GetChosenOrFirstAdapter(IDXGIFactory5* factory, const std::string_view name)
{
	wil::com_ptr_nothrow<IDXGIAdapter1> adapter = GetAdapterByName(factory, name);
	if (!adapter)
		adapter = GetFirstAdapter(factory);

	return adapter;
}

std::string DXGI::GetAdapterName(IDXGIAdapter1* adapter)
{
	std::string ret;

	DXGI_ADAPTER_DESC1 desc;
	HRESULT hr = adapter->GetDesc1(&desc);
	if (SUCCEEDED(hr))
	{
		ret = StringUtil::WideStringToUTF8String(desc.Description);
	}
	else
	{
		Console.Error(fmt::format("IDXGIAdapter1::GetDesc() returned {:08X}", hr));
	}

	if (ret.empty())
		ret = "(Unknown)";

	return ret;
}

std::string DXGI::GetDriverVersionFromLUID(const LUID& luid)
{
	std::string ret;

	HKEY hKey;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\DirectX", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
	{
		DWORD max_key_len = 0, adapter_count = 0;
		if (RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, &adapter_count, &max_key_len, nullptr, nullptr, nullptr,
				nullptr, nullptr, nullptr) == ERROR_SUCCESS)
		{
			std::vector<TCHAR> current_name(max_key_len + 1);
			for (DWORD i = 0; i < adapter_count; ++i)
			{
				DWORD subKeyLength = static_cast<DWORD>(current_name.size());
				if (RegEnumKeyExW(hKey, i, current_name.data(), &subKeyLength, nullptr, nullptr, nullptr, nullptr) ==
					ERROR_SUCCESS)
				{
					LUID current_luid = {};
					DWORD current_luid_size = sizeof(uint64_t);
					if (RegGetValueW(hKey, current_name.data(), L"AdapterLuid", RRF_RT_QWORD, nullptr, &current_luid,
							&current_luid_size) == ERROR_SUCCESS &&
						current_luid.HighPart == luid.HighPart && current_luid.LowPart == luid.LowPart)
					{
						LARGE_INTEGER driver_version = {};
						DWORD driver_version_size = sizeof(driver_version);
						if (RegGetValueW(hKey, current_name.data(), L"DriverVersion", RRF_RT_QWORD, nullptr,
								&driver_version, &driver_version_size) == ERROR_SUCCESS)
						{
							WORD nProduct = HIWORD(driver_version.HighPart);
							WORD nVersion = LOWORD(driver_version.HighPart);
							WORD nSubVersion = HIWORD(driver_version.LowPart);
							WORD nBuild = LOWORD(driver_version.LowPart);
							ret = fmt::format("{}.{}.{}.{}", nProduct, nVersion, nSubVersion, nBuild);
						}
					}
				}
			}
		}

		RegCloseKey(hKey);
	}

	return ret;
}

DXGI::VendorID DXGI::GetVendorID(IDXGIAdapter1* adapter)
{
	DXGI_ADAPTER_DESC1 desc;
	const HRESULT hr = adapter->GetDesc1(&desc);
	if (FAILED(hr))
	{
		Console.Error(fmt::format("IDXGIAdapter1::GetDesc() returned {:08X}", hr));
		return VendorID::Unknown;
	}

	switch (desc.VendorId)
	{
		case 0x10DE:
			return VendorID::Nvidia;
		case 0x1002:
		case 0x1022:
			return VendorID::AMD;
		case 0x163C:
		case 0x8086:
		case 0x8087:
			return VendorID::Intel;
		default:
			return VendorID::Unknown;
	}
}

std::optional<D3D_FEATURE_LEVEL> DXGI::GetD3D11FeatureLevel(IDXGIAdapter1* adapter)
{
	static const D3D_FEATURE_LEVEL check[] = {
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_0,
	};

	D3D_FEATURE_LEVEL feature_level;
	const HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, std::data(check),
		std::size(check), D3D11_SDK_VERSION, nullptr, &feature_level, nullptr);

	if (FAILED(hr))
	{
		Console.Error("D3D11CreateDevice() for automatic renderer failed: %08X", hr);
		return std::nullopt;
	}

	Console.WriteLn("D3D11 feature level for autodetection: %x", static_cast<unsigned>(feature_level));
	return feature_level;
}
