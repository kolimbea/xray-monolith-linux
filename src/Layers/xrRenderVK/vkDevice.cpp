// D3D11CreateDevice, the device's resource/view creation and the immediate context (Vulkan backend).
#include "vkObjects.h"

#include <cstring>

#include "vkFormat.h"

namespace xrvk
{
    bool Device::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11Device), __uuidof(ID3D11Device1), __uuidof(ID3D11Device2)});
    }
    bool DXGIDevice::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(IDXGIObject), __uuidof(IDXGIDevice)});
    }

    HRESULT Device::QueryInterface(REFIID riid, void **out)
    {
        if (out && riid == __uuidof(IDXGIDevice))
        {
            if (!dxgi)
                dxgi = new DXGIDevice();
            dxgi->AddRef();
            *out = static_cast<IDXGIDevice *>(dxgi);
            return S_OK;
        }
        return ComBase::QueryInterface(riid, out);
    }

    bool Context::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(ID3D11DeviceContext), __uuidof(ID3D11DeviceContext1),
                          __uuidof(ID3D11DeviceContext2)});
    }
    bool Annotation::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3DUserDefinedAnnotation)});
    }

    HRESULT Context::QueryInterface(REFIID riid, void **out)
    {
        if (out && riid == __uuidof(ID3DUserDefinedAnnotation))
        {
            if (!annotation)
                annotation = new Annotation();
            annotation->AddRef();
            *out = static_cast<ID3DUserDefinedAnnotation *>(annotation);
            return S_OK;
        }
        return ComBase::QueryInterface(riid, out);
    }



} // namespace xrvk

using namespace xrvk;

extern "C" HRESULT WINAPI D3D11CreateDevice(IDXGIAdapter *adapter, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *levels,
                                            UINT count, UINT, ID3D11Device **device, D3D_FEATURE_LEVEL *obtained, ID3D11DeviceContext **context)
{
    // The engine asks for feature level 11_0 only; Vulkan 1.3 + the ADR 0001 extensions cover it.
    bool ok = !levels || !count;
    for (UINT i = 0; i < count && !ok; ++i)
        ok = levels[i] <= D3D_FEATURE_LEVEL_11_1;
    if (!ok)
        return E_INVALIDARG;
    VkPhysicalDevice phys = adapter ? static_cast<Adapter *>(static_cast<IDXGIAdapter4 *>(adapter))->phys : preferred_physical_device();
    Core& core_ = create_device(phys);
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(core_.phys, VK_FORMAT_D24_UNORM_S8_UINT, &fp);
    set_depth24_supported((fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0);
    auto *dev = new Device();
    auto *ctx = new Context();
    ctx->xrvk_device = dev;
    dev->immediate = ctx;
    ctx->init_null_resources();
    if (obtained)
        *obtained = D3D_FEATURE_LEVEL_11_0;
    if (device)
        *device = dev;
    else
        dev->Release();
    if (context)
        *context = ctx; // the device keeps its own pointer without a reference (as D3D11's context is part of it)
    return S_OK;
}

void ID3D11Device::GetImmediateContext(ID3D11DeviceContext **ppImmediateContext)
{
    ID3D11DeviceContext *c = static_cast<Device *>(this)->immediate;
    c->AddRef();
    *ppImmediateContext = c;
}

// The annotation interface is answered as "no capture tool attached" (what D3D11 does without one).
INT ID3DUserDefinedAnnotation::BeginEvent(LPCWSTR)
{
    return -1;
}
INT ID3DUserDefinedAnnotation::EndEvent()
{
    return -1;
}
void ID3DUserDefinedAnnotation::SetMarker(LPCWSTR)
{
}
WINBOOL ID3DUserDefinedAnnotation::GetStatus()
{
    return FALSE;
}

// Device loss is fatal in the backend (xrvk::fail), so a live device was never removed.
HRESULT ID3D11Device::GetDeviceRemovedReason()
{
    return S_OK;
}
