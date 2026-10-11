// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/RedtapeWindows.h"
#include "common/RedtapeWilCom.h"

#include <d3dcommon.h>
#include <string_view>

namespace D3D
{
	// D3DCompiler wrapper.
	enum class ShaderType
	{
		Vertex,
		Pixel,
		Compute
	};

	enum class ShaderModel
	{
		SM40 = 0x40, // DX11 FL 10_0
		SM41 = 0x41, // DX11 FL 10_1
		SM50 = 0x50, // DX11 FL 11_0
		SM51 = 0x51, // DX12
		SM60 = 0x60,
		SM61 = 0x61,
		SM62 = 0x62,
		SM63 = 0x63,
		SM64 = 0x64,
		SM65 = 0x65,
	};

	const char* ShaderModelToCacheString(ShaderModel shader_model);

	wil::com_ptr_nothrow<ID3DBlob> CompileShader(ShaderType type, ShaderModel shader_model, bool debug,
		const std::string_view code, const D3D_SHADER_MACRO* macros = nullptr, const char* entry_point = "main");

	wil::com_ptr_nothrow<ID3DBlob> CompileShaderDXBC(ShaderType type, ShaderModel shader_model, bool debug,
		const std::string_view code, const D3D_SHADER_MACRO* macros = nullptr, const char* entry_point = "main");
	wil::com_ptr_nothrow<ID3DBlob> CompileShaderDXIL(ShaderType type, ShaderModel shader_model, bool debug,
		const std::string_view code, const D3D_SHADER_MACRO* macros = nullptr, const char* entry_point = "main");
}; // namespace D3D
