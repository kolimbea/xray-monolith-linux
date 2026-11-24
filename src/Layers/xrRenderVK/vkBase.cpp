#include "stdafx.h"
#include "vkBase.h"

namespace xrvk
{
void not_implemented(const char* what)
{
    Msg("! vk: %s not implemented", what);
    FlushLog();
    Debug.fatal(DEBUG_INFO, "Vulkan renderer: %s is not implemented yet", what);
    abort(); // fatal does not return; keeps [[noreturn]] honest if it ever did
}

HRESULT ComBase::QueryInterface(REFIID riid, void** out)
{
    if (!out)
        return E_POINTER;
    if (riid == __uuidof(IUnknown))
    {
        AddRef();
        *out = static_cast<IUnknown*>(this);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE; // typed lookups are done by the backend classes that support them
}
} // namespace xrvk

// D3D compiler API of the Vulkan build (DXC + SPIR-V reflection, milestone M3).
extern "C" {
HRESULT WINAPI D3DCompile(const void*, SIZE_T, const char*, const D3D_SHADER_MACRO*, ID3DInclude*, const char*,
    const char*, UINT, UINT, ID3DBlob**, ID3DBlob**)
{
    xrvk::not_implemented("D3DCompile");
}
HRESULT WINAPI D3DDisassemble(const void*, SIZE_T, UINT, const char*, ID3DBlob**) { xrvk::not_implemented("D3DDisassemble"); }
HRESULT WINAPI D3DGetInputSignatureBlob(const void*, SIZE_T, ID3DBlob**) { xrvk::not_implemented("D3DGetInputSignatureBlob"); }
HRESULT WINAPI D3DReflect(const void*, SIZE_T, REFIID, void**) { xrvk::not_implemented("D3DReflect"); }
HRESULT WINAPI D3DCreateBlob(SIZE_T, ID3DBlob**) { xrvk::not_implemented("D3DCreateBlob"); }
}
