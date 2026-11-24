// Base-interface methods, textures and views of the Vulkan backend (see vkObjects.h).
#include "vkObjects.h"

#include <cstring>

#include <vk_mem_alloc.h>

#include "vkFormat.h"

namespace xrvk
{
    bool iid_in(REFIID riid, std::initializer_list<GUID> ids)
    {
        for (const GUID &g : ids)
            if (riid == g)
                return true;
        return false;
    }

    HRESULT ComBase::QueryInterface(REFIID riid, void **out)
    {
        if (!out)
            return E_POINTER;
        if (riid == __uuidof(IUnknown) || has_interface(riid))
        {
            AddRef();
            *out = static_cast<IUnknown *>(this); // single inheritance: every interface shares this address
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

} // namespace xrvk

using namespace xrvk;

void ID3D11DeviceChild::GetDevice(ID3D11Device **ppDevice)
{
    if (xrvk_device)
        xrvk_device->AddRef();
    *ppDevice = xrvk_device;
}

void ID3D11Resource::GetType(D3D11_RESOURCE_DIMENSION *pResourceDimension)
{
    *pResourceDimension = xrvk_dimension;
}

void ID3D11View::GetResource(ID3D11Resource **ppResource)
{
    if (xrvk_resource)
        xrvk_resource->AddRef();
    *ppResource = xrvk_resource;
}
