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

    void transition(VkCommandBuffer cb, Texture2D *t, VkImageLayout to)
    {
        if (!t->image || t->layout == to)
            return;
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        b.oldLayout = t->layout;
        b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = t->image;
        b.subresourceRange = {aspect_of(t->format), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
        VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        d.imageMemoryBarrierCount = 1;
        d.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cb, &d);
        t->layout = to;
    }

    namespace
    {
        uint32_t block_size(VkFormat f, uint32_t &block_w)
        {
            block_w = 1;
            switch (f)
            {
            case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
            case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            case VK_FORMAT_BC4_UNORM_BLOCK:
            case VK_FORMAT_BC4_SNORM_BLOCK:
                block_w = 4;
                return 8;
            case VK_FORMAT_BC2_UNORM_BLOCK:
            case VK_FORMAT_BC2_SRGB_BLOCK:
            case VK_FORMAT_BC3_UNORM_BLOCK:
            case VK_FORMAT_BC3_SRGB_BLOCK:
            case VK_FORMAT_BC5_UNORM_BLOCK:
            case VK_FORMAT_BC5_SNORM_BLOCK:
            case VK_FORMAT_BC6H_UFLOAT_BLOCK:
            case VK_FORMAT_BC6H_SFLOAT_BLOCK:
            case VK_FORMAT_BC7_UNORM_BLOCK:
            case VK_FORMAT_BC7_SRGB_BLOCK:
                block_w = 4;
                return 16;
            case VK_FORMAT_R8_UNORM:
            case VK_FORMAT_R8_SNORM:
            case VK_FORMAT_R8_UINT:
            case VK_FORMAT_R8_SINT:
                return 1;
            case VK_FORMAT_R8G8_UNORM:
            case VK_FORMAT_R8G8_SNORM:
            case VK_FORMAT_R8G8_UINT:
            case VK_FORMAT_R8G8_SINT:
            case VK_FORMAT_R16_SFLOAT:
            case VK_FORMAT_R16_UNORM:
            case VK_FORMAT_R16_SNORM:
            case VK_FORMAT_R16_UINT:
            case VK_FORMAT_R16_SINT:
            case VK_FORMAT_D16_UNORM:
            case VK_FORMAT_R5G6B5_UNORM_PACK16:
            case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
            case VK_FORMAT_A4R4G4B4_UNORM_PACK16:
                return 2;
            case VK_FORMAT_R16G16B16A16_SFLOAT:
            case VK_FORMAT_R16G16B16A16_UNORM:
            case VK_FORMAT_R16G16B16A16_SNORM:
            case VK_FORMAT_R16G16B16A16_UINT:
            case VK_FORMAT_R16G16B16A16_SINT:
            case VK_FORMAT_R32G32_SFLOAT:
            case VK_FORMAT_R32G32_UINT:
            case VK_FORMAT_R32G32_SINT:
            case VK_FORMAT_D32_SFLOAT_S8_UINT:
                return 8;
            case VK_FORMAT_R32G32B32_SFLOAT:
            case VK_FORMAT_R32G32B32_UINT:
            case VK_FORMAT_R32G32B32_SINT:
                return 12;
            case VK_FORMAT_R32G32B32A32_SFLOAT:
            case VK_FORMAT_R32G32B32A32_UINT:
            case VK_FORMAT_R32G32B32A32_SINT:
                return 16;
            default:
                return 4; // all remaining supported formats are 32-bit texels
            }
        }

        VkImageUsageFlags usage_of(const D3D11_TEXTURE2D_DESC &d)
        {
            VkImageUsageFlags u = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            if (d.BindFlags & D3D11_BIND_SHADER_RESOURCE)
                u |= VK_IMAGE_USAGE_SAMPLED_BIT;
            if (d.BindFlags & D3D11_BIND_RENDER_TARGET)
                u |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            if (d.BindFlags & D3D11_BIND_DEPTH_STENCIL)
                u |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            if (d.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
                u |= VK_IMAGE_USAGE_STORAGE_BIT;
            return u;
        }
    } // namespace

    Texture2D *create_texture2d(Device *dev, const D3D11_TEXTURE2D_DESC &in, const D3D11_SUBRESOURCE_DATA *init)
    {
        Core &c = *core();
        D3D11_TEXTURE2D_DESC d = in;
        if (!d.MipLevels) // 0 = full chain
        {
            uint32_t m = 1;
            for (uint32_t s = d.Width > d.Height ? d.Width : d.Height; s > 1; s >>= 1)
                ++m;
            d.MipLevels = m;
        }
        bool typeless = false;
        const VkFormat fmt = resource_format(d.Format, (d.BindFlags & D3D11_BIND_DEPTH_STENCIL) != 0, &typeless);
        if (fmt == VK_FORMAT_UNDEFINED)
        {
            log("! vk: CreateTexture2D: unsupported DXGI format %u", unsigned(d.Format));
            return nullptr;
        }
        auto *t = new Texture2D();
        t->xrvk_device = dev;
        t->xrvk_dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
        t->desc = d;
        t->format = fmt;
        uint32_t bw;
        const uint32_t bs = block_size(fmt, bw);

        // per-subresource packing (staging layout and upload source layout)
        VkDeviceSize total = 0;
        for (uint32_t a = 0; a < d.ArraySize; ++a)
            for (uint32_t m = 0; m < d.MipLevels; ++m)
            {
                const uint32_t w = (d.Width >> m) ? (d.Width >> m) : 1, h = (d.Height >> m) ? (d.Height >> m) : 1;
                const uint32_t cols = (w + bw - 1) / bw, rows = (h + bw - 1) / bw;
                t->sub_offset.push_back(total);
                t->sub_pitch.push_back(cols * bs);
                t->sub_rows.push_back(rows);
                total += VkDeviceSize(cols) * bs * rows;
                total = (total + 15) & ~VkDeviceSize(15);
            }

        if (d.Usage == D3D11_USAGE_STAGING)
        {
            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bi.size = total ? total : 16;
            bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            VmaAllocationInfo info{};
            XRVK_CHECK(vmaCreateBuffer(c.vma, &bi, &ai, &t->staging, &t->alloc, &info));
            t->mapped = info.pMappedData;
            if (init)
                for (uint32_t s = 0; s < t->subresources(); ++s)
                    for (uint32_t r = 0; r < t->sub_rows[s]; ++r)
                        memcpy(static_cast<uint8_t *>(t->mapped) + t->sub_offset[s] + VkDeviceSize(r) * t->sub_pitch[s],
                               static_cast<const uint8_t *>(init[s].pSysMem) + size_t(r) * init[s].SysMemPitch, t->sub_pitch[s]);
            return t;
        }

        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = fmt;
        ii.extent = {d.Width, d.Height, 1};
        ii.mipLevels = d.MipLevels;
        ii.arrayLayers = d.ArraySize;
        ii.samples = VkSampleCountFlagBits(d.SampleDesc.Count ? d.SampleDesc.Count : 1);
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = usage_of(d);
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (typeless && !is_depth(fmt))
            ii.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        if (d.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE)
            ii.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (d.BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL))
            ai.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        XRVK_CHECK(vmaCreateImage(c.vma, &ii, &ai, &t->image, &t->alloc, nullptr));

        if (init)
        {
            // upload through a temporary staging buffer; freed once the GPU is done (flush_and_wait for now)
            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bi.size = total;
            bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            VmaAllocationCreateInfo sai{};
            sai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            sai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            VkBuffer buf;
            VmaAllocation balloc;
            VmaAllocationInfo info{};
            XRVK_CHECK(vmaCreateBuffer(c.vma, &bi, &sai, &buf, &balloc, &info));
            std::vector<VkBufferImageCopy> regions;
            for (uint32_t a = 0, s = 0; a < d.ArraySize; ++a)
                for (uint32_t m = 0; m < d.MipLevels; ++m, ++s)
                {
                    for (uint32_t r = 0; r < t->sub_rows[s]; ++r)
                        memcpy(static_cast<uint8_t *>(info.pMappedData) + t->sub_offset[s] + VkDeviceSize(r) * t->sub_pitch[s],
                               static_cast<const uint8_t *>(init[s].pSysMem) + size_t(r) * init[s].SysMemPitch, t->sub_pitch[s]);
                    VkBufferImageCopy rc{};
                    rc.bufferOffset = t->sub_offset[s];
                    rc.imageSubresource = {aspect_of(fmt) & ~VK_IMAGE_ASPECT_STENCIL_BIT, m, a, 1};
                    rc.imageExtent = {(d.Width >> m) ? (d.Width >> m) : 1, (d.Height >> m) ? (d.Height >> m) : 1, 1};
                    regions.push_back(rc);
                }
            VkCommandBuffer cb = c.cmd();
            transition(cb, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            vkCmdCopyBufferToImage(cb, buf, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, uint32_t(regions.size()), regions.data());
            c.flush_and_wait();
            vmaDestroyBuffer(c.vma, buf, balloc);
        }
        return t;
    }

    Texture2D::~Texture2D()
    {
        Core *c = core();
        if (!c)
            return;
        // Waits for the GPU before freeing: correct but slow. Per-frame deferred destruction is part of
    // the performance track (PLAN.md).
        if (image || staging)
            vkQueueWaitIdle(c->queue);
        if (image)
            vmaDestroyImage(c->vma, image, alloc);
        if (staging)
            vmaDestroyBuffer(c->vma, staging, alloc);
    }

    RenderTargetView::~RenderTargetView()
    {
        if (Core *c = core())
        {
            vkQueueWaitIdle(c->queue);
            vkDestroyImageView(c->device, view, nullptr);
        }
        if (xrvk_resource)
            xrvk_resource->Release();
    }

    DepthStencilView::~DepthStencilView()
    {
        if (Core *c = core())
        {
            vkQueueWaitIdle(c->queue);
            vkDestroyImageView(c->device, view, nullptr);
        }
        if (xrvk_resource)
            xrvk_resource->Release();
    }

    bool Texture2D::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(ID3D11Resource), __uuidof(ID3D11Texture2D), __uuidof(ID3D10Texture2D)});
    }
    bool RenderTargetView::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(ID3D11View), __uuidof(ID3D11RenderTargetView)});
    }
    bool DepthStencilView::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(ID3D11View), __uuidof(ID3D11DepthStencilView)});
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

void ID3D11Texture2D::GetDesc(D3D11_TEXTURE2D_DESC *pDesc)
{
    *pDesc = static_cast<Texture2D *>(this)->desc;
}
void ID3D11RenderTargetView::GetDesc(D3D11_RENDER_TARGET_VIEW_DESC *pDesc)
{
    *pDesc = static_cast<RenderTargetView *>(this)->desc;
}
void ID3D11DepthStencilView::GetDesc(D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc)
{
    *pDesc = static_cast<DepthStencilView *>(this)->desc;
}
