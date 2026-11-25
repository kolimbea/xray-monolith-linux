// Views of the Vulkan backend: render-target, depth-stencil, shader-resource and unordered-access views
// on textures of every dimension and on buffers (ViewData in vkObjects.h).
#include "vkObjects.h"

#include "vkFormat.h"

namespace xrvk
{
    ViewData::~ViewData()
    {
        Core *c = core();
        if (!c)
            return;
        VkImageView v = view;
        VkBufferView b = texel;
        if (v || b)
            c->defer([c, v, b]
                     {
                         if (v)
                             vkDestroyImageView(c->device, v, nullptr);
                         if (b)
                             vkDestroyBufferView(c->device, b, nullptr);
                     });
    }

    RenderTargetView::~RenderTargetView()
    {
        if (xrvk_resource)
            xrvk_resource->Release();
    }
    DepthStencilView::~DepthStencilView()
    {
        if (xrvk_resource)
            xrvk_resource->Release();
    }
    ShaderResourceView::~ShaderResourceView()
    {
        if (xrvk_resource)
            xrvk_resource->Release();
    }
    UnorderedAccessView::~UnorderedAccessView()
    {
        if (xrvk_resource)
            xrvk_resource->Release();
    }

    bool RenderTargetView::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(ID3D11View), __uuidof(ID3D11RenderTargetView)});
    }
    bool DepthStencilView::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(ID3D11View), __uuidof(ID3D11DepthStencilView)});
    }
    bool ShaderResourceView::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(ID3D11View), __uuidof(ID3D11ShaderResourceView)});
    }
    bool UnorderedAccessView::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(ID3D11View), __uuidof(ID3D11UnorderedAccessView)});
    }

    namespace
    {
        // The subset of a view description common to all view kinds, in Vulkan terms.
        struct Subset
        {
            VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
            uint32_t mip = 0, mips = 1, layer = 0, layers = 1;
        };

        bool image_view(ViewData &v, Image *img, const Subset &s, VkFormat fmt, VkImageAspectFlags aspect, VkComponentMapping swizzle = {})
        {
            if (!img || !img->image)
                return false;
            v.img = img;
            v.format = fmt;
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image = img->image;
            vi.viewType = s.type;
            vi.format = fmt;
            vi.components = swizzle;
            const uint32_t mips = s.mips == UINT32_MAX || s.mip + s.mips > img->mips ? img->mips - s.mip : s.mips;
            const uint32_t layers = s.layers == UINT32_MAX || s.layer + s.layers > img->layers ? img->layers - s.layer : s.layers;
            vi.subresourceRange = {aspect, s.mip, mips, s.layer, layers};
            if (s.type == VK_IMAGE_VIEW_TYPE_3D)
                vi.subresourceRange.layerCount = 1;
            if (vkCreateImageView(core()->device, &vi, nullptr, &v.view) != VK_SUCCESS)
                return false;
            v.range = vi.subresourceRange;
            v.type = s.type;
            v.extent = {img->mip_width(s.mip), img->mip_height(s.mip)};
            return true;
        }

        // SRVs of depth formats show one aspect: depth for R24_UNORM_X8 / R32_FLOAT_X8X24 / R16_UNORM /
        // R32_FLOAT views, stencil for X24_TYPELESS_G8_UINT / X32_TYPELESS_G8X24_UINT.
        VkImageAspectFlags read_aspect(DXGI_FORMAT view, VkFormat image)
        {
            const VkImageAspectFlags a = aspect_of(image);
            if (!(a & VK_IMAGE_ASPECT_DEPTH_BIT))
                return a;
            if (view == DXGI_FORMAT_X24_TYPELESS_G8_UINT || view == DXGI_FORMAT_X32_TYPELESS_G8X24_UINT)
                return VK_IMAGE_ASPECT_STENCIL_BIT;
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        }

        Subset srv_subset(const D3D11_SHADER_RESOURCE_VIEW_DESC &d)
        {
            Subset s;
            switch (d.ViewDimension)
            {
            case D3D11_SRV_DIMENSION_TEXTURE1D:
                s = {VK_IMAGE_VIEW_TYPE_1D, d.Texture1D.MostDetailedMip, d.Texture1D.MipLevels, 0, 1};
                break;
            case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
                s = {VK_IMAGE_VIEW_TYPE_1D_ARRAY, d.Texture1DArray.MostDetailedMip, d.Texture1DArray.MipLevels,
                     d.Texture1DArray.FirstArraySlice, d.Texture1DArray.ArraySize};
                break;
            case D3D11_SRV_DIMENSION_TEXTURE2D:
                s = {VK_IMAGE_VIEW_TYPE_2D, d.Texture2D.MostDetailedMip, d.Texture2D.MipLevels, 0, 1};
                break;
            case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
                s = {VK_IMAGE_VIEW_TYPE_2D_ARRAY, d.Texture2DArray.MostDetailedMip, d.Texture2DArray.MipLevels,
                     d.Texture2DArray.FirstArraySlice, d.Texture2DArray.ArraySize};
                break;
            case D3D11_SRV_DIMENSION_TEXTURE2DMS:
                s = {VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1};
                break;
            case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
                s = {VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, d.Texture2DMSArray.FirstArraySlice, d.Texture2DMSArray.ArraySize};
                break;
            case D3D11_SRV_DIMENSION_TEXTURE3D:
                s = {VK_IMAGE_VIEW_TYPE_3D, d.Texture3D.MostDetailedMip, d.Texture3D.MipLevels, 0, 1};
                break;
            case D3D11_SRV_DIMENSION_TEXTURECUBE:
                s = {VK_IMAGE_VIEW_TYPE_CUBE, d.TextureCube.MostDetailedMip, d.TextureCube.MipLevels, 0, 6};
                break;
            case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
                s = {VK_IMAGE_VIEW_TYPE_CUBE_ARRAY, d.TextureCubeArray.MostDetailedMip, d.TextureCubeArray.MipLevels,
                     d.TextureCubeArray.First2DArrayFace, d.TextureCubeArray.NumCubes * 6};
                break;
            default:
                break;
            }
            return s;
        }

        // The view description D3D11 derives when the caller passes none.
        D3D11_SHADER_RESOURCE_VIEW_DESC default_srv(ID3D11Resource *r)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC d{};
            switch (r->xrvk_dimension)
            {
            case D3D11_RESOURCE_DIMENSION_TEXTURE1D: {
                const auto &t = static_cast<Texture1D *>(r)->desc;
                d.Format = t.Format;
                if (t.ArraySize > 1)
                {
                    d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
                    d.Texture1DArray = {0, UINT(-1), 0, t.ArraySize};
                }
                else
                {
                    d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
                    d.Texture1D = {0, UINT(-1)};
                }
                break;
            }
            case D3D11_RESOURCE_DIMENSION_TEXTURE2D: {
                const auto &t = static_cast<Texture2D *>(r)->desc;
                d.Format = t.Format;
                if (t.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE)
                {
                    d.ViewDimension = t.ArraySize > 6 ? D3D11_SRV_DIMENSION_TEXTURECUBEARRAY : D3D11_SRV_DIMENSION_TEXTURECUBE;
                    if (t.ArraySize > 6)
                        d.TextureCubeArray = {0, UINT(-1), 0, t.ArraySize / 6};
                    else
                        d.TextureCube = {0, UINT(-1)};
                }
                else if (t.SampleDesc.Count > 1)
                {
                    d.ViewDimension = t.ArraySize > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D11_SRV_DIMENSION_TEXTURE2DMS;
                    if (t.ArraySize > 1)
                        d.Texture2DMSArray = {0, t.ArraySize};
                }
                else if (t.ArraySize > 1)
                {
                    d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                    d.Texture2DArray = {0, UINT(-1), 0, t.ArraySize};
                }
                else
                {
                    d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                    d.Texture2D = {0, UINT(-1)};
                }
                break;
            }
            case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
                d.Format = static_cast<Texture3D *>(r)->desc.Format;
                d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
                d.Texture3D = {0, UINT(-1)};
                break;
            default:
                break;
            }
            return d;
        }

        template <class T>
        HRESULT hand_out(T *v, T **out)
        {
            if (out)
                *out = v;
            else
                v->Release();
            return S_OK;
        }
    } // namespace
} // namespace xrvk

using namespace xrvk;

HRESULT ID3D11Device::CreateRenderTargetView(ID3D11Resource *pResource, const D3D11_RENDER_TARGET_VIEW_DESC *pDesc, ID3D11RenderTargetView **ppRTView)
{
    if (ppRTView)
        *ppRTView = nullptr; // D3D11 clears the output, so a failed call never hands out garbage
    Image *img = image_of(pResource);
    if (!img || !img->image)
    {
        log("! vk: CreateRenderTargetView: needs a GPU texture");
        return E_INVALIDARG;
    }
    D3D11_RENDER_TARGET_VIEW_DESC d{};
    if (pDesc)
        d = *pDesc;
    else
    {
        d.Format = img->dxgi;
        d.ViewDimension = img->dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D ? D3D11_RTV_DIMENSION_TEXTURE3D
                          : img->samples > 1                             ? D3D11_RTV_DIMENSION_TEXTURE2DMS
                          : img->layers > 1                              ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY
                                                                         : D3D11_RTV_DIMENSION_TEXTURE2D;
        if (d.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DARRAY)
            d.Texture2DArray.ArraySize = img->layers;
        if (d.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE3D)
            d.Texture3D.WSize = img->depth;
    }
    Subset s;
    switch (d.ViewDimension)
    {
    case D3D11_RTV_DIMENSION_TEXTURE2D:
        s.mip = d.Texture2D.MipSlice;
        break;
    case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
        s = {VK_IMAGE_VIEW_TYPE_2D_ARRAY, d.Texture2DArray.MipSlice, 1, d.Texture2DArray.FirstArraySlice, d.Texture2DArray.ArraySize};
        break;
    case D3D11_RTV_DIMENSION_TEXTURE2DMS:
        break;
    case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
        s = {VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, d.Texture2DMSArray.FirstArraySlice, d.Texture2DMSArray.ArraySize};
        break;
    default:
        log("! vk: CreateRenderTargetView: view dimension %d is not supported", int(d.ViewDimension));
        return E_INVALIDARG;
    }
    auto *v = new RenderTargetView();
    v->desc = d;
    if (!image_view(v->v, img, s, view_format(d.Format, img->format), VK_IMAGE_ASPECT_COLOR_BIT))
    {
        delete v;
        return E_INVALIDARG;
    }
    pResource->AddRef();
    v->xrvk_resource = pResource;
    v->xrvk_device = this;
    return hand_out<ID3D11RenderTargetView>(v, ppRTView);
}

HRESULT ID3D11Device::CreateDepthStencilView(ID3D11Resource *pResource, const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc, ID3D11DepthStencilView **ppDepthStencilView)
{
    if (ppDepthStencilView)
        *ppDepthStencilView = nullptr; // D3D11 clears the output, so a failed call never hands out garbage
    Image *img = image_of(pResource);
    if (!img || !img->image || !is_depth(img->format))
    {
        log("! vk: CreateDepthStencilView: needs a depth texture");
        return E_INVALIDARG;
    }
    D3D11_DEPTH_STENCIL_VIEW_DESC d{};
    if (pDesc)
        d = *pDesc;
    else
    {
        d.Format = img->dxgi;
        d.ViewDimension = img->samples > 1  ? D3D11_DSV_DIMENSION_TEXTURE2DMS
                          : img->layers > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY
                                            : D3D11_DSV_DIMENSION_TEXTURE2D;
        if (d.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2DARRAY)
            d.Texture2DArray.ArraySize = img->layers;
    }
    Subset s;
    switch (d.ViewDimension)
    {
    case D3D11_DSV_DIMENSION_TEXTURE2D:
        s.mip = d.Texture2D.MipSlice;
        break;
    case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
        s = {VK_IMAGE_VIEW_TYPE_2D_ARRAY, d.Texture2DArray.MipSlice, 1, d.Texture2DArray.FirstArraySlice, d.Texture2DArray.ArraySize};
        break;
    case D3D11_DSV_DIMENSION_TEXTURE2DMS:
        break;
    default:
        log("! vk: CreateDepthStencilView: view dimension %d is not supported", int(d.ViewDimension));
        return E_INVALIDARG;
    }
    auto *v = new DepthStencilView();
    v->desc = d;
    if (!image_view(v->v, img, s, img->format, aspect_of(img->format)))
    {
        delete v;
        return E_INVALIDARG;
    }
    pResource->AddRef();
    v->xrvk_resource = pResource;
    v->xrvk_device = this;
    return hand_out<ID3D11DepthStencilView>(v, ppDepthStencilView);
}

HRESULT ID3D11Device::CreateShaderResourceView(ID3D11Resource *pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc, ID3D11ShaderResourceView **ppSRView)
{
    if (ppSRView)
        *ppSRView = nullptr; // D3D11 clears the output, so a failed call never hands out garbage
    if (!pResource)
        return E_INVALIDARG;
    auto *v = new ShaderResourceView();
    if (pResource->xrvk_dimension == D3D11_RESOURCE_DIMENSION_BUFFER)
    {
        auto *b = static_cast<Buffer *>(pResource);
        if (!pDesc)
        {
            delete v;
            log("! vk: CreateShaderResourceView: a buffer view needs a description");
            return E_INVALIDARG;
        }
        v->desc = *pDesc;
        const UINT first = pDesc->ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX ? pDesc->BufferEx.FirstElement : pDesc->Buffer.FirstElement;
        const UINT count = pDesc->ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX ? pDesc->BufferEx.NumElements : pDesc->Buffer.NumElements;
        const UINT stride = b->desc.StructureByteStride ? b->desc.StructureByteStride : 4;
        v->v.buffer = b;
        v->v.offset = VkDeviceSize(first) * stride;
        v->v.size = VkDeviceSize(count) * stride;
        if (pDesc->Format != DXGI_FORMAT_UNKNOWN) // typed buffer: a texel buffer view
        {
            log("! vk: CreateShaderResourceView: typed buffer views are not supported yet");
            delete v;
            return E_INVALIDARG;
        }
    }
    else
    {
        Image *img = image_of(pResource);
        if (!img || !img->image)
        {
            delete v;
            log("! vk: CreateShaderResourceView: needs a GPU texture");
            return E_INVALIDARG;
        }
        v->desc = pDesc ? *pDesc : default_srv(pResource);
        if (v->desc.Format == DXGI_FORMAT_UNKNOWN)
            v->desc.Format = img->dxgi;
        if (!image_view(v->v, img, srv_subset(v->desc), view_format(v->desc.Format, img->format), read_aspect(v->desc.Format, img->format),
                        view_swizzle(v->desc.Format)))
        {
            log("! vk: CreateShaderResourceView: view dimension %d, format %u failed", int(v->desc.ViewDimension), unsigned(v->desc.Format));
            delete v;
            return E_INVALIDARG;
        }
    }
    pResource->AddRef();
    v->xrvk_resource = pResource;
    v->xrvk_device = this;
    return hand_out<ID3D11ShaderResourceView>(v, ppSRView);
}

HRESULT ID3D11Device::CreateUnorderedAccessView(ID3D11Resource *pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC *pDesc, ID3D11UnorderedAccessView **ppUAView)
{
    if (ppUAView)
        *ppUAView = nullptr; // D3D11 clears the output, so a failed call never hands out garbage
    if (!pResource || !pDesc)
        return E_INVALIDARG;
    auto *v = new UnorderedAccessView();
    v->desc = *pDesc;
    if (pResource->xrvk_dimension == D3D11_RESOURCE_DIMENSION_BUFFER)
    {
        auto *b = static_cast<Buffer *>(pResource);
        if (pDesc->Format != DXGI_FORMAT_UNKNOWN)
        {
            log("! vk: CreateUnorderedAccessView: typed buffer views are not supported yet");
            delete v;
            return E_INVALIDARG;
        }
        const UINT stride = b->desc.StructureByteStride ? b->desc.StructureByteStride : 4;
        v->v.buffer = b;
        v->v.offset = VkDeviceSize(pDesc->Buffer.FirstElement) * stride;
        v->v.size = VkDeviceSize(pDesc->Buffer.NumElements) * stride;
    }
    else
    {
        Image *img = image_of(pResource);
        Subset s;
        switch (pDesc->ViewDimension)
        {
        case D3D11_UAV_DIMENSION_TEXTURE1D:
            s = {VK_IMAGE_VIEW_TYPE_1D, pDesc->Texture1D.MipSlice, 1, 0, 1};
            break;
        case D3D11_UAV_DIMENSION_TEXTURE2D:
            s = {VK_IMAGE_VIEW_TYPE_2D, pDesc->Texture2D.MipSlice, 1, 0, 1};
            break;
        case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
            s = {VK_IMAGE_VIEW_TYPE_2D_ARRAY, pDesc->Texture2DArray.MipSlice, 1, pDesc->Texture2DArray.FirstArraySlice,
                 pDesc->Texture2DArray.ArraySize};
            break;
        case D3D11_UAV_DIMENSION_TEXTURE3D:
            s = {VK_IMAGE_VIEW_TYPE_3D, pDesc->Texture3D.MipSlice, 1, 0, 1};
            break;
        default:
            log("! vk: CreateUnorderedAccessView: view dimension %d is not supported", int(pDesc->ViewDimension));
            delete v;
            return E_INVALIDARG;
        }
        const DXGI_FORMAT f = pDesc->Format != DXGI_FORMAT_UNKNOWN ? pDesc->Format : (img ? img->dxgi : DXGI_FORMAT_UNKNOWN);
        if (!img || !image_view(v->v, img, s, view_format(f, img->format), VK_IMAGE_ASPECT_COLOR_BIT))
        {
            log("! vk: CreateUnorderedAccessView: failed for format %u", unsigned(f));
            delete v;
            return E_INVALIDARG;
        }
    }
    pResource->AddRef();
    v->xrvk_resource = pResource;
    v->xrvk_device = this;
    return hand_out<ID3D11UnorderedAccessView>(v, ppUAView);
}

void ID3D11RenderTargetView::GetDesc(D3D11_RENDER_TARGET_VIEW_DESC *pDesc)
{
    *pDesc = static_cast<RenderTargetView *>(this)->desc;
}
void ID3D11DepthStencilView::GetDesc(D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc)
{
    *pDesc = static_cast<DepthStencilView *>(this)->desc;
}
void ID3D11ShaderResourceView::GetDesc(D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc)
{
    *pDesc = static_cast<ShaderResourceView *>(this)->desc;
}
void ID3D11UnorderedAccessView::GetDesc(D3D11_UNORDERED_ACCESS_VIEW_DESC *pDesc)
{
    *pDesc = static_cast<UnorderedAccessView *>(this)->desc;
}
