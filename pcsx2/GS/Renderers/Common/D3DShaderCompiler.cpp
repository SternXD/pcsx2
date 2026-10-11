// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/Common/D3DShaderCompiler.h"
#include "GS/GSShaderCompileIndicator.h"
#include "Config.h"

#include "common/Assertions.h"
#include "common/Console.h"
#include "common/Path.h"
#include "common/StringUtil.h"

#include "fmt/format.h"

#include <array>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <fstream>
#include <vector>

static u32 s_next_bad_shader_id = 1;

const char* D3D::ShaderModelToCacheString(D3D::ShaderModel shader_model)
{
	switch (shader_model)
	{
		case ShaderModel::SM40:
			return "sm40";
		case ShaderModel::SM41:
			return "sm41";
		case ShaderModel::SM50:
			return "sm50";
		case ShaderModel::SM51:
		case ShaderModel::SM60:
		case ShaderModel::SM61:
		case ShaderModel::SM62:
		case ShaderModel::SM63:
		case ShaderModel::SM64:
			return "sm51";
		case ShaderModel::SM65:
			return "sm65";
		default:
			return "unk";
	}
}

wil::com_ptr_nothrow<ID3DBlob> D3D::CompileShaderDXBC(D3D::ShaderType type, D3D::ShaderModel shader_model, bool debug,
	const std::string_view code, const D3D_SHADER_MACRO* macros /* = nullptr */,
	const char* entry_point /* = "main" */)
{
	const char* target;
	switch (shader_model)
	{
		case ShaderModel::SM40:
		{
			static constexpr std::array<const char*, 4> targets = {{"vs_4_0", "ps_4_0", "cs_4_0"}};
			target = targets[static_cast<int>(type)];
		}
		break;

		case ShaderModel::SM41:
		{
			static constexpr std::array<const char*, 4> targets = {{"vs_4_1", "ps_4_1", "cs_4_1"}};
			target = targets[static_cast<int>(type)];
		}
		break;

		case ShaderModel::SM50:
		{
			static constexpr std::array<const char*, 4> targets = {{"vs_5_0", "ps_5_0", "cs_5_0"}};
			target = targets[static_cast<int>(type)];
		}
		break;

		case ShaderModel::SM51:
		default:
		{
			static constexpr std::array<const char*, 4> targets = {{"vs_5_1", "ps_5_1", "cs_5_1"}};
			target = targets[static_cast<int>(type)];
		}
		break;
	}

	static constexpr UINT flags_non_debug = D3DCOMPILE_OPTIMIZATION_LEVEL3;
	static constexpr UINT flags_debug = D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_DEBUG | D3DCOMPILE_DEBUG_NAME_FOR_SOURCE;

	wil::com_ptr_nothrow<ID3DBlob> blob;
	wil::com_ptr_nothrow<ID3DBlob> error_blob;
	const HRESULT hr = D3DCompile(code.data(), code.size(), "0", macros, nullptr, entry_point, target,
		debug ? flags_debug : flags_non_debug, 0, blob.put(), error_blob.put());

	std::string error_string;
	if (error_blob)
	{
		error_string.append(static_cast<const char*>(error_blob->GetBufferPointer()), error_blob->GetBufferSize());
		error_blob.reset();
	}

	if (FAILED(hr))
	{
		Console.WriteLn("D3D: Failed to compile '%s':\n%s", target, error_string.c_str());

		std::ofstream ofs(Path::Combine(EmuFolders::Logs, fmt::format("pcsx2_bad_shader_{}.txt", s_next_bad_shader_id++)),
			std::ofstream::out | std::ofstream::binary);
		if (ofs.is_open())
		{
			ofs << code;
			ofs << "\n\nCompile as " << target << " failed: " << hr << "\n";
			ofs.write(error_string.c_str(), error_string.size());
			ofs << "\n";
			if (macros)
			{
				for (const D3D_SHADER_MACRO* macro = macros; macro->Name != nullptr; macro++)
					ofs << "#define " << macro->Name << " " << macro->Definition << "\n";
			}
			ofs.close();
		}

		return {};
	}

	if (!error_string.empty())
		Console.Warning("'%s' compiled with warnings:\n%s", target, error_string.c_str());

	return blob;
}

wil::com_ptr_nothrow<ID3DBlob> D3D::CompileShaderDXIL(D3D::ShaderType type, D3D::ShaderModel shader_model, bool debug,
	const std::string_view code, const D3D_SHADER_MACRO* macros /* = nullptr */,
	const char* entry_point /* = "main" */)
{
	const wchar_t* target;
	switch (shader_model)
	{
		case ShaderModel::SM60:
		case ShaderModel::SM61:
		case ShaderModel::SM62:
		case ShaderModel::SM63:
		case ShaderModel::SM64:
			pxAssert(false);
			break;
		case ShaderModel::SM65:
		default:
		{
			static constexpr std::array<const wchar_t*, 4> targets = {{L"vs_6_5", L"ps_6_5", L"cs_6_5"}};
			target = targets[static_cast<int>(type)];
		}
		break;
	}

	// Build Args.
	std::vector<std::wstring> args;
	if (macros)
	{
		for (const D3D_SHADER_MACRO* macro = macros; macro->Name != nullptr; macro++)
			args.push_back(StringUtil::UTF8StringToWideString(fmt::format("-D{}={}", macro->Name, macro->Definition)));
	}

	if (entry_point)
	{
		args.push_back(L"-E");
		args.push_back(StringUtil::UTF8StringToWideString(entry_point));
	}

	args.push_back(L"-T");
	args.push_back(target);

	if (debug)
	{
		args.push_back(L"-Od");
		args.push_back(L"-Zi");
		args.push_back(L"-Zss");
		args.push_back(L"-Qembed_debug");
	}
	else
	{
		args.push_back(L"-O3");
		args.push_back(L"-Qstrip_reflect");
	}

	std::vector<const wchar_t*> arg_ptrs;
	arg_ptrs.reserve(args.size());
	for (const std::wstring& arg : args)
		arg_ptrs.push_back(arg.c_str());

	// Compile Shader.
	wil::com_ptr_nothrow<IDxcCompiler3> compiler;
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(compiler.put()));

	const DxcBuffer source{code.data(), code.length(), DXC_CP_UTF8};
	wil::com_ptr_nothrow<IDxcResult> results;
	HRESULT hr = compiler->Compile(&source, arg_ptrs.data(), arg_ptrs.size(), nullptr, IID_PPV_ARGS(results.put()));

	if (FAILED(hr))
	{
		Console.WriteLn("Compiler Failed");
		return {};
	}

	wil::com_ptr_nothrow<IDxcBlobUtf8> error_string = nullptr;
	results->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(error_string.put()), nullptr);

	results->GetStatus(&hr);
	if (FAILED(hr))
	{
		std::string target_utf8 = StringUtil::WideStringToUTF8String(target);
		Console.WriteLn("D3D: Failed to compile '%s':\n%s", target_utf8.c_str(), error_string->GetStringPointer());

		std::ofstream ofs(Path::Combine(EmuFolders::Logs, fmt::format("pcsx2_bad_shader_{}.txt", s_next_bad_shader_id++)),
			std::ofstream::out | std::ofstream::binary);
		if (ofs.is_open())
		{
			ofs << code;
			ofs << "\n\nCompile as " << target_utf8.c_str() << " failed: " << hr << "\n";
			ofs.write(error_string->GetStringPointer(), error_string->GetStringLength());
			ofs << "\n";
			if (macros)
			{
				for (const D3D_SHADER_MACRO* macro = macros; macro->Name != nullptr; macro++)
					ofs << "#define " << macro->Name << " " << macro->Definition << "\n";
			}
			ofs.close();
		}

		return {};
	}

	if (error_string->GetStringLength() != 0)
		Console.Warning("'%s' compiled with warnings:\n%s", StringUtil::WideStringToUTF8String(target).c_str(), error_string->GetStringPointer());

	wil::com_ptr_nothrow<ID3DBlob> blob = nullptr;
	results->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(blob.put()), nullptr);

	return blob;
}

wil::com_ptr_nothrow<ID3DBlob> D3D::CompileShader(D3D::ShaderType type, D3D::ShaderModel shader_model, bool debug,
	const std::string_view code, const D3D_SHADER_MACRO* macros /* = nullptr */,
	const char* entry_point /* = "main" */)
{
	const GSShaderCompileIndicator::CompileTimer compile_timer;

	if (static_cast<int>(shader_model) < 0x65)
		return CompileShaderDXBC(type, shader_model, debug, code, macros, entry_point);
	else
		return CompileShaderDXIL(type, shader_model, debug, code, macros, entry_point);
}
