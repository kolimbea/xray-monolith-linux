// Shader-compiler API of the Vulkan build: same names/signatures as d3dcompiler.h, implemented by the
// backend (HLSL -> SPIR-V with DXC, reflection from SPIR-V; xrRenderVK/vkShader.cpp, vkReflection.cpp).
#pragma once
#include "vkCommonTypes.h"

#define D3DCOMPILE_DEBUG 0x00000001
#define D3DCOMPILE_SKIP_VALIDATION 0x00000002
#define D3DCOMPILE_SKIP_OPTIMIZATION 0x00000004
#define D3DCOMPILE_PACK_MATRIX_ROW_MAJOR 0x00000008
#define D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR 0x00000010
#define D3DCOMPILE_PARTIAL_PRECISION 0x00000020
#define D3DCOMPILE_AVOID_FLOW_CONTROL 0x00000200
#define D3DCOMPILE_PREFER_FLOW_CONTROL 0x00000400
#define D3DCOMPILE_ENABLE_STRICTNESS 0x00000800
#define D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY 0x00001000
#define D3DCOMPILE_IEEE_STRICTNESS 0x00002000
#define D3DCOMPILE_OPTIMIZATION_LEVEL0 0x00004000
#define D3DCOMPILE_OPTIMIZATION_LEVEL1 0x00000000
#define D3DCOMPILE_OPTIMIZATION_LEVEL2 0x0000c000
#define D3DCOMPILE_OPTIMIZATION_LEVEL3 0x00008000
#define D3DCOMPILE_WARNINGS_ARE_ERRORS 0x00040000
#define D3D10_SHADER_PACK_MATRIX_ROW_MAJOR D3DCOMPILE_PACK_MATRIX_ROW_MAJOR
#define D3D10_SHADER_DEBUG D3DCOMPILE_DEBUG
#define D3D10_SHADER_SKIP_OPTIMIZATION D3DCOMPILE_SKIP_OPTIMIZATION

extern "C" {
HRESULT WINAPI D3DCompile(const void* data, SIZE_T data_size, const char* filename, const D3D_SHADER_MACRO* defines,
    ID3DInclude* include, const char* entrypoint, const char* target, UINT sflags, UINT eflags, ID3DBlob** shader,
    ID3DBlob** error_messages);
HRESULT WINAPI D3DDisassemble(const void* data, SIZE_T data_size, UINT flags, const char* comments, ID3DBlob** disassembly);
HRESULT WINAPI D3DGetInputSignatureBlob(const void* data, SIZE_T data_size, ID3DBlob** blob);
HRESULT WINAPI D3DReflect(const void* data, SIZE_T data_size, REFIID riid, void** reflector);
HRESULT WINAPI D3DCreateBlob(SIZE_T data_size, ID3DBlob** blob);
}
