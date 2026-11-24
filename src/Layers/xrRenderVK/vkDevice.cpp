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

    Context::~Context()
    {
        if (annotation)
            annotation->Release();
    }

    namespace
    {
        template <typename View, typename Desc>
        View *make_view(ID3D11Resource *res, VkImageViewType default_type, uint32_t mip, uint32_t first_layer, uint32_t layers,
                        VkFormat fmt, VkImageAspectFlags aspect)
        {
            auto *t = static_cast<Texture2D *>(res);
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image = t->image;
            vi.viewType = default_type;
            vi.format = fmt;
            vi.subresourceRange = {aspect, mip, 1, first_layer, layers};
            auto *v = new View();
            XRVK_CHECK(vkCreateImageView(core()->device, &vi, nullptr, &v->view));
            v->range = vi.subresourceRange;
            res->AddRef();
            v->xrvk_resource = res;
            v->xrvk_device = t->xrvk_device;
            return v;
        }

        Texture2D *as_texture2d(ID3D11Resource *r)
        {
            D3D11_RESOURCE_DIMENSION d;
            r->GetType(&d);
            return d == D3D11_RESOURCE_DIMENSION_TEXTURE2D ? static_cast<Texture2D *>(r) : nullptr;
        }
    } // namespace
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

HRESULT ID3D11Device::CreateTexture2D(const D3D11_TEXTURE2D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture2D **ppTexture2D)
{
    if (!pDesc)
        return E_INVALIDARG;
    Texture2D *t = create_texture2d(static_cast<Device *>(this), *pDesc, pInitialData);
    if (!t)
        return E_INVALIDARG;
    if (ppTexture2D)
        *ppTexture2D = t;
    else
        t->Release(); // validation-only call
    return S_OK;
}

HRESULT ID3D11Device::CreateRenderTargetView(ID3D11Resource *pResource, const D3D11_RENDER_TARGET_VIEW_DESC *pDesc, ID3D11RenderTargetView **ppRTView)
{
    Texture2D *t = pResource ? as_texture2d(pResource) : nullptr;
    if (!t || !t->image)
    {
        log("! vk: CreateRenderTargetView: only 2D GPU textures are supported");
        return E_INVALIDARG;
    }
    D3D11_RENDER_TARGET_VIEW_DESC d{};
    if (pDesc)
        d = *pDesc;
    else
    {
        d.Format = t->desc.Format;
        d.ViewDimension = t->desc.ArraySize > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY : D3D11_RTV_DIMENSION_TEXTURE2D;
        if (d.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DARRAY)
            d.Texture2DArray.ArraySize = t->desc.ArraySize;
    }
    uint32_t mip = 0, first = 0, layers = 1;
    VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
    switch (d.ViewDimension)
    {
    case D3D11_RTV_DIMENSION_TEXTURE2D:
        mip = d.Texture2D.MipSlice;
        break;
    case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
        mip = d.Texture2DArray.MipSlice;
        first = d.Texture2DArray.FirstArraySlice;
        layers = d.Texture2DArray.ArraySize;
        type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        break;
    case D3D11_RTV_DIMENSION_TEXTURE2DMS:
        break;
    default:
        log("! vk: CreateRenderTargetView: view dimension %d not supported", int(d.ViewDimension));
        return E_INVALIDARG;
    }
    auto *v = make_view<RenderTargetView, D3D11_RENDER_TARGET_VIEW_DESC>(pResource, type, mip, first, layers,
                                                                         view_format(d.Format, t->format), VK_IMAGE_ASPECT_COLOR_BIT);
    v->desc = d;
    if (ppRTView)
        *ppRTView = v;
    else
        v->Release();
    return S_OK;
}

HRESULT ID3D11Device::CreateDepthStencilView(ID3D11Resource *pResource, const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc, ID3D11DepthStencilView **ppDepthStencilView)
{
    Texture2D *t = pResource ? as_texture2d(pResource) : nullptr;
    if (!t || !t->image || !is_depth(t->format))
    {
        log("! vk: CreateDepthStencilView: needs a 2D depth texture");
        return E_INVALIDARG;
    }
    D3D11_DEPTH_STENCIL_VIEW_DESC d{};
    if (pDesc)
        d = *pDesc;
    else
    {
        d.Format = t->desc.Format;
        d.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    }
    uint32_t mip = 0, first = 0, layers = 1;
    VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
    switch (d.ViewDimension)
    {
    case D3D11_DSV_DIMENSION_TEXTURE2D:
        mip = d.Texture2D.MipSlice;
        break;
    case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
        mip = d.Texture2DArray.MipSlice;
        first = d.Texture2DArray.FirstArraySlice;
        layers = d.Texture2DArray.ArraySize;
        type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        break;
    case D3D11_DSV_DIMENSION_TEXTURE2DMS:
        break;
    default:
        log("! vk: CreateDepthStencilView: view dimension %d not supported", int(d.ViewDimension));
        return E_INVALIDARG;
    }
    auto *v = make_view<DepthStencilView, D3D11_DEPTH_STENCIL_VIEW_DESC>(pResource, type, mip, first, layers, t->format,
                                                                         aspect_of(t->format));
    v->desc = d;
    if (ppDepthStencilView)
        *ppDepthStencilView = v;
    else
        v->Release();
    return S_OK;
}

void ID3D11Device::GetImmediateContext(ID3D11DeviceContext **ppImmediateContext)
{
    ID3D11DeviceContext *c = static_cast<Device *>(this)->immediate;
    c->AddRef();
    *ppImmediateContext = c;
}

// ---- immediate context ----------------------------------------------------------------------------

void ID3D11DeviceContext::ClearRenderTargetView(ID3D11RenderTargetView *pRenderTargetView, const FLOAT ColorRGBA[4])
{
    if (!pRenderTargetView)
        return;
    auto *v = static_cast<RenderTargetView *>(pRenderTargetView);
    auto *t = static_cast<Texture2D *>(v->xrvk_resource);
    VkCommandBuffer cb = core()->cmd();
    transition(cb, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue c;
    memcpy(c.float32, ColorRGBA, sizeof(c.float32));
    vkCmdClearColorImage(cb, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &c, 1, &v->range);
}

void ID3D11DeviceContext::ClearDepthStencilView(ID3D11DepthStencilView *pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil)
{
    if (!pDepthStencilView)
        return;
    auto *v = static_cast<DepthStencilView *>(pDepthStencilView);
    auto *t = static_cast<Texture2D *>(v->xrvk_resource);
    VkImageSubresourceRange r = v->range;
    r.aspectMask = 0;
    if (ClearFlags & D3D11_CLEAR_DEPTH)
        r.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if (ClearFlags & D3D11_CLEAR_STENCIL)
        r.aspectMask |= v->range.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT;
    if (!r.aspectMask)
        return;
    VkCommandBuffer cb = core()->cmd();
    transition(cb, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkClearDepthStencilValue c{Depth, Stencil};
    vkCmdClearDepthStencilImage(cb, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &c, 1, &r);
}

void ID3D11DeviceContext::OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView *const *ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView)
{
    // Bound targets are used by draws (milestone M4); D3D11 does not reference-count bindings here.
    auto *c = static_cast<Context *>(this);
    for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        c->rtv[i] = i < NumViews && ppRenderTargetViews ? ppRenderTargetViews[i] : nullptr;
    c->dsv = pDepthStencilView;
}

void ID3D11DeviceContext::CopyResource(ID3D11Resource *pDstResource, ID3D11Resource *pSrcResource)
{
    Texture2D *dst = pDstResource ? as_texture2d(pDstResource) : nullptr;
    Texture2D *src = pSrcResource ? as_texture2d(pSrcResource) : nullptr;
    if (!dst || !src || dst->desc.Width != src->desc.Width || dst->desc.Height != src->desc.Height ||
        dst->subresources() != src->subresources())
    {
        log("! vk: CopyResource: only identical 2D textures are supported so far");
        return;
    }
    VkCommandBuffer cb = core()->cmd();
    const VkImageAspectFlags aspect = aspect_of(src->image ? src->format : dst->format) & ~VK_IMAGE_ASPECT_STENCIL_BIT;
    auto extent = [&](uint32_t m)
    {
        return VkExtent3D{(src->desc.Width >> m) ? (src->desc.Width >> m) : 1, (src->desc.Height >> m) ? (src->desc.Height >> m) : 1, 1};
    };
    if (src->image && dst->image)
    {
        transition(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        transition(cb, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        std::vector<VkImageCopy> rc;
        for (uint32_t m = 0; m < src->desc.MipLevels; ++m)
            rc.push_back({{aspect, m, 0, src->desc.ArraySize}, {}, {aspect, m, 0, src->desc.ArraySize}, {}, extent(m)});
        vkCmdCopyImage(cb, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       uint32_t(rc.size()), rc.data());
    }
    else if (src->image && dst->staging) // GPU -> CPU readback
    {
        transition(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        std::vector<VkBufferImageCopy> rc;
        for (uint32_t a = 0, s = 0; a < src->desc.ArraySize; ++a)
            for (uint32_t m = 0; m < src->desc.MipLevels; ++m, ++s)
            {
                VkBufferImageCopy r{};
                r.bufferOffset = dst->sub_offset[s];
                r.imageSubresource = {aspect, m, a, 1};
                r.imageExtent = extent(m);
                rc.push_back(r);
            }
        vkCmdCopyImageToBuffer(cb, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->staging, uint32_t(rc.size()), rc.data());
    }
    else
        log("! vk: CopyResource: staging -> GPU copies are not supported yet");
}

HRESULT ID3D11DeviceContext::Map(ID3D11Resource *pResource, UINT Subresource, D3D11_MAP MapType, UINT, D3D11_MAPPED_SUBRESOURCE *pMappedResource)
{
    Texture2D *t = pResource ? as_texture2d(pResource) : nullptr;
    if (!t || !t->staging || Subresource >= t->subresources() || !pMappedResource)
    {
        log("! vk: Map: only staging 2D textures are supported so far");
        return E_INVALIDARG;
    }
    if (MapType == D3D11_MAP_READ || MapType == D3D11_MAP_READ_WRITE)
        core()->flush_and_wait(); // D3D11 Map blocks until the GPU wrote the data
    pMappedResource->pData = static_cast<uint8_t *>(t->mapped) + t->sub_offset[Subresource];
    pMappedResource->RowPitch = t->sub_pitch[Subresource];
    pMappedResource->DepthPitch = t->sub_pitch[Subresource] * t->sub_rows[Subresource];
    return S_OK;
}

void ID3D11DeviceContext::Unmap(ID3D11Resource *, UINT)
{
} // staging memory is persistently mapped and coherent

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
