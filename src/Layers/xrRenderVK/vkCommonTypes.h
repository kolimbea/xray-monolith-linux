// Types the renderer sees as D3D11/DXGI in the Vulkan build (xrRender_VK, ADR 0001). The stand-in
// headers in xrRenderVK/include (d3d11.h, dxgi1_4.h, d3dcompiler.h, ...) all lead here, so the shared
// layer, the R4 pipeline and xrRenderDX10's logic compile unchanged against the Vulkan backend.
#pragma once
#include <atomic>
#include <unknwn.h> // the engine-wide IUnknown (MonolithCompat.inl)

#include "vk_d3d_types.gen.h"

#ifndef DXGI_ERROR_DEVICE_REMOVED // winerror.h values
#define DXGI_ERROR_DEVICE_REMOVED ((HRESULT)0x887A0005)
#define DXGI_ERROR_DEVICE_RESET ((HRESULT)0x887A0007)
#endif

namespace xrvk
{
    // Root of every backend object: reference counted, destroyed through the virtual destructor.
    struct ComBase : public IUnknown
    {
        virtual ~ComBase() = default;
    using IUnknown::QueryInterface; // keep the QueryInterface(T**) convenience overload visible
    // The leaf classes (xrRenderVK/vkObjects.h) list the interfaces they implement.
    virtual bool has_interface(REFIID) const { return false; }
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return ++m_refs;
        }
        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG r = --m_refs;
            if (!r)
                delete this;
            return r;
        }

    private:
        std::atomic<ULONG> m_refs{1};
    };
} // namespace xrvk

#include "vkMembers.h"
#include "vk_d3d_interfaces.gen.h"

// Short names the D3D headers define next to the interfaces.
typedef ID3DInclude ID3D10Include;
typedef ID3D10Include *LPD3D10INCLUDE;
typedef ID3DInclude *LPD3DINCLUDE;
typedef ID3D10Blob ID3DBlob;
typedef ID3D10Blob *LPD3D10BLOB;
typedef ID3DBlob *LPD3DBLOB;
typedef D3D_SHADER_MACRO D3D10_SHADER_MACRO;
typedef D3D_INCLUDE_TYPE D3D10_INCLUDE_TYPE;

// D3D11 inline helper (d3d11.h defines it with the CD3D11 helpers, which this build leaves out).
inline UINT D3D11CalcSubresource(UINT mip_slice, UINT array_slice, UINT mip_levels)
{
    return mip_slice + array_slice * mip_levels;
}

// D3D10 names used in D3D11 code paths; the values are the same as D3D11's.
#define D3D10_BIND_SHADER_RESOURCE D3D11_BIND_SHADER_RESOURCE
#define D3D10_BIND_RENDER_TARGET D3D11_BIND_RENDER_TARGET
#define D3D10_BIND_DEPTH_STENCIL D3D11_BIND_DEPTH_STENCIL
#define D3D10_PRIMITIVE_TOPOLOGY_UNDEFINED D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED
// r4_rendertarget_phase_combine.cpp asks the swap chain for its buffer with this IID (the backend's
// GetBuffer accepts it as well as ID3D11Texture2D's).
struct ID3D10Texture2D;
DECLARE_UUIDOF_HELPER(ID3D10Texture2D, 0x9b7e4c04, 0x342c, 0x4106, 0xa1, 0x9f, 0x4f, 0x27, 0x04, 0xf6, 0x89, 0xf0)

// Entry points dx10HW.cpp calls; implemented on Vulkan in vkDXGI.cpp / vkDevice.cpp.
extern "C" {
HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** factory);
HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** factory);
HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID riid, void** factory);
HRESULT WINAPI D3D11CreateDevice(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driver_type, HMODULE swrast, UINT flags,
    const D3D_FEATURE_LEVEL* feature_levels, UINT levels, UINT sdk_version, ID3D11Device** device,
    D3D_FEATURE_LEVEL* obtained_feature_level, ID3D11DeviceContext** immediate_context);
}
#ifndef D3D11_SDK_VERSION
#define D3D11_SDK_VERSION 7
#endif
