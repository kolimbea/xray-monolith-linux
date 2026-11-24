// Textures of the Vulkan backend (Image and Texture1D/2D/3D in vkObjects.h): creation with initial data
// (recorded in the upload command buffer), staging and dynamic Map/Unmap, UpdateSubresource.
#include "vkObjects.h"

#include <cstring>

#include "vkFormat.h"

namespace xrvk
{
    namespace
    {
        // bytes per texel (or per 4x4 block for BC formats, block_w = 4)
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
            case VK_FORMAT_S8_UINT:
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
            case VK_FORMAT_B5G6R5_UNORM_PACK16:
            case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
            case VK_FORMAT_A4R4G4B4_UNORM_PACK16:
            case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
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

        VkImageUsageFlags usage_of(UINT bind)
        {
            VkImageUsageFlags u = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            if (bind & D3D11_BIND_SHADER_RESOURCE)
                u |= VK_IMAGE_USAGE_SAMPLED_BIT;
            if (bind & D3D11_BIND_RENDER_TARGET)
                u |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            if (bind & D3D11_BIND_DEPTH_STENCIL)
                u |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            if (bind & D3D11_BIND_UNORDERED_ACCESS)
                u |= VK_IMAGE_USAGE_STORAGE_BIT;
            return u;
        }

        uint32_t full_chain(uint32_t w, uint32_t h, uint32_t d)
        {
            uint32_t m = 1, s = w > h ? w : h;
            if (d > s)
                s = d;
            for (; s > 1; s >>= 1)
                ++m;
            return m;
        }

        // Copies D3D11_SUBRESOURCE_DATA rows/slices into the packed layout at `dst`.
        void pack(const Image &img, uint32_t s, uint8_t *dst, const void *src, UINT row_pitch, UINT depth_pitch)
        {
            const uint32_t m = s % img.mips, slices = img.mip_depth(m);
            for (uint32_t z = 0; z < slices; ++z)
                for (uint32_t r = 0; r < img.sub_rows[s]; ++r)
                    memcpy(dst + (VkDeviceSize(z) * img.sub_rows[s] + r) * img.sub_pitch[s],
                           static_cast<const uint8_t *>(src) + size_t(z) * depth_pitch + size_t(r) * row_pitch, img.sub_pitch[s]);
        }

        VkBufferImageCopy region(const Image &img, uint32_t s, VkDeviceSize offset)
        {
            const uint32_t m = s % img.mips, layer = s / img.mips;
            VkBufferImageCopy r{};
            r.bufferOffset = offset;
            r.imageSubresource = {img.copy_aspect(), m, layer, 1};
            r.imageExtent = {img.mip_width(m), img.mip_height(m), img.mip_depth(m)};
            return r;
        }

        // Creates the image (or staging memory) and records the initial data upload.
        bool init_image(Image &img, const D3D11_SUBRESOURCE_DATA *init, VkImageCreateFlags flags)
        {
            Core &c = *core();
            bool typeless = false;
            img.format = resource_format(img.dxgi, (img.bind & D3D11_BIND_DEPTH_STENCIL) != 0, &typeless);
            if (img.format == VK_FORMAT_UNDEFINED)
            {
                log("! vk: CreateTexture: unsupported DXGI format %u", unsigned(img.dxgi));
                return false;
            }
            uint32_t bw;
            const uint32_t bs = block_size(img.format, bw);
            VkDeviceSize total = 0;
            for (uint32_t a = 0; a < img.layers; ++a)
                for (uint32_t m = 0; m < img.mips; ++m)
                {
                    const uint32_t cols = (img.mip_width(m) + bw - 1) / bw, rows = (img.mip_height(m) + bw - 1) / bw;
                    img.sub_offset.push_back(total);
                    img.sub_pitch.push_back(cols * bs);
                    img.sub_rows.push_back(rows);
                    total += VkDeviceSize(cols) * bs * rows * img.mip_depth(m);
                    total = (total + 15) & ~VkDeviceSize(15);
                }

            if (img.usage == D3D11_USAGE_STAGING)
            {
                VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                bi.size = total ? total : 16;
                bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                VmaAllocationCreateInfo ai{};
                ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
                ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
                VmaAllocationInfo info{};
                XRVK_CHECK(vmaCreateBuffer(c.vma, &bi, &ai, &img.staging, &img.alloc, &info));
                img.mapped = static_cast<uint8_t *>(info.pMappedData);
                if (init)
                    for (uint32_t s = 0; s < img.subresources(); ++s)
                        pack(img, s, img.mapped + img.sub_offset[s], init[s].pSysMem, init[s].SysMemPitch, init[s].SysMemSlicePitch);
                return true;
            }

            VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            ii.imageType = img.dim == D3D11_RESOURCE_DIMENSION_TEXTURE1D   ? VK_IMAGE_TYPE_1D
                           : img.dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D ? VK_IMAGE_TYPE_3D
                                                                           : VK_IMAGE_TYPE_2D;
            ii.format = img.format;
            ii.extent = {img.width, img.height, img.depth};
            ii.mipLevels = img.mips;
            ii.arrayLayers = img.layers;
            ii.samples = VkSampleCountFlagBits(img.samples);
            ii.tiling = VK_IMAGE_TILING_OPTIMAL;
            ii.usage = usage_of(img.bind);
            ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            ii.flags = flags;
            if (typeless && !is_depth(img.format))
                ii.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
            if (img.misc & D3D11_RESOURCE_MISC_TEXTURECUBE)
                ii.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            if (ii.imageType == VK_IMAGE_TYPE_3D && (img.bind & D3D11_BIND_RENDER_TARGET))
                ii.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT; // render targets of 3D slices
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            if (img.bind & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL))
                ai.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            XRVK_CHECK(vmaCreateImage(c.vma, &ii, &ai, &img.image, &img.alloc, nullptr));

            std::lock_guard<std::mutex> g(c.upload_lock);
            VkCommandBuffer cb = c.upload_cmd();
            transition(cb, &img, VK_IMAGE_LAYOUT_GENERAL);
            if (init)
            {
                const Slice s = c.heap->alloc(total ? total : 16);
                std::vector<VkBufferImageCopy> regions;
                for (uint32_t i = 0; i < img.subresources(); ++i)
                {
                    pack(img, i, s.ptr + img.sub_offset[i], init[i].pSysMem, init[i].SysMemPitch, init[i].SysMemSlicePitch);
                    regions.push_back(region(img, i, s.offset + img.sub_offset[i]));
                }
                vkCmdCopyBufferToImage(cb, s.buffer, img.image, VK_IMAGE_LAYOUT_GENERAL, uint32_t(regions.size()), regions.data());
                c.heap->release(s, c.recording_value());
            }
            return true;
        }

        template <class T>
        T *finish(T *t, bool ok)
        {
            if (!ok)
            {
                delete t;
                return nullptr;
            }
            return t;
        }
    } // namespace

    VkImageAspectFlags Image::copy_aspect() const
    {
        const VkImageAspectFlags a = aspect_of(format);
        return (a & VK_IMAGE_ASPECT_DEPTH_BIT) ? (a & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) : a;
    }

    Image::~Image()
    {
        Core *c = core();
        if (!c)
            return;
        if (dynamic)
            c->heap->release(dynamic, c->recording_value());
        VkImage i = image;
        VkBuffer b = staging;
        VmaAllocation a = alloc;
        if (i || b)
            c->defer([c, i, b, a]
                     {
                         if (i)
                             vmaDestroyImage(c->vma, i, a);
                         if (b)
                             vmaDestroyBuffer(c->vma, b, a);
                     });
    }

    void transition(VkCommandBuffer cb, Image *t, VkImageLayout to)
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

    Image *image_of(ID3D11Resource *r)
    {
        if (!r)
            return nullptr;
        switch (r->xrvk_dimension)
        {
        case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
            return &static_cast<Texture1D *>(r)->img;
        case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
            return &static_cast<Texture2D *>(r)->img;
        case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
            return &static_cast<Texture3D *>(r)->img;
        default:
            return nullptr;
        }
    }

    bool Texture1D::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(ID3D11Resource), __uuidof(ID3D11Texture1D)});
    }
    bool Texture2D::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(ID3D11Resource), __uuidof(ID3D11Texture2D), __uuidof(ID3D10Texture2D)});
    }
    bool Texture3D::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(ID3D11Resource), __uuidof(ID3D11Texture3D)});
    }

    Texture1D *create_texture1d(Device *dev, const D3D11_TEXTURE1D_DESC &d, const D3D11_SUBRESOURCE_DATA *init)
    {
        auto *t = new Texture1D();
        t->xrvk_device = dev;
        t->xrvk_dimension = D3D11_RESOURCE_DIMENSION_TEXTURE1D;
        t->desc = d;
        if (!t->desc.MipLevels)
            t->desc.MipLevels = full_chain(d.Width, 1, 1);
        Image &i = t->img;
        i.dim = t->xrvk_dimension;
        i.dxgi = d.Format;
        i.width = d.Width;
        i.mips = t->desc.MipLevels;
        i.layers = d.ArraySize ? d.ArraySize : 1;
        i.usage = d.Usage;
        i.bind = d.BindFlags;
        i.misc = d.MiscFlags;
        return finish(t, init_image(i, init, 0));
    }

    Texture2D *create_texture2d(Device *dev, const D3D11_TEXTURE2D_DESC &d, const D3D11_SUBRESOURCE_DATA *init)
    {
        auto *t = new Texture2D();
        t->xrvk_device = dev;
        t->xrvk_dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
        t->desc = d;
        if (!t->desc.MipLevels)
            t->desc.MipLevels = full_chain(d.Width, d.Height, 1);
        Image &i = t->img;
        i.dim = t->xrvk_dimension;
        i.dxgi = d.Format;
        i.width = d.Width;
        i.height = d.Height;
        i.mips = t->desc.MipLevels;
        i.layers = d.ArraySize ? d.ArraySize : 1;
        i.samples = d.SampleDesc.Count ? d.SampleDesc.Count : 1;
        i.usage = d.Usage;
        i.bind = d.BindFlags;
        i.misc = d.MiscFlags;
        return finish(t, init_image(i, init, 0));
    }

    Texture3D *create_texture3d(Device *dev, const D3D11_TEXTURE3D_DESC &d, const D3D11_SUBRESOURCE_DATA *init)
    {
        auto *t = new Texture3D();
        t->xrvk_device = dev;
        t->xrvk_dimension = D3D11_RESOURCE_DIMENSION_TEXTURE3D;
        t->desc = d;
        if (!t->desc.MipLevels)
            t->desc.MipLevels = full_chain(d.Width, d.Height, d.Depth);
        Image &i = t->img;
        i.dim = t->xrvk_dimension;
        i.dxgi = d.Format;
        i.width = d.Width;
        i.height = d.Height;
        i.depth = d.Depth;
        i.mips = t->desc.MipLevels;
        i.usage = d.Usage;
        i.bind = d.BindFlags;
        i.misc = d.MiscFlags;
        return finish(t, init_image(i, init, 0));
    }

    // ---- Map / Unmap / UpdateSubresource of textures --------------------------------------------------

    HRESULT map_texture(Context *, Image *img, UINT sub, D3D11_MAP type, D3D11_MAPPED_SUBRESOURCE *out)
    {
        Core *c = core();
        if (sub >= img->subresources())
            return E_INVALIDARG;
        const uint32_t m = sub % img->mips;
        if (img->mapped) // staging
        {
            if (type == D3D11_MAP_READ || type == D3D11_MAP_READ_WRITE)
                c->flush_and_wait(); // D3D11 Map blocks until the GPU wrote the data
            out->pData = img->mapped + img->sub_offset[sub];
        }
        else if (img->usage == D3D11_USAGE_DYNAMIC && type == D3D11_MAP_WRITE_DISCARD)
        {
            if (img->dynamic)
                c->heap->release(img->dynamic, c->recording_value());
            const VkDeviceSize size = VkDeviceSize(img->sub_pitch[sub]) * img->sub_rows[sub] * img->mip_depth(m);
            img->dynamic = c->heap->alloc(size);
            img->dynamic_sub = sub;
            out->pData = img->dynamic.ptr;
        }
        else
        {
            log("! vk: Map: map type %d is not supported for this texture (usage %d)", int(type), int(img->usage));
            return E_INVALIDARG;
        }
        out->RowPitch = img->sub_pitch[sub];
        out->DepthPitch = img->sub_pitch[sub] * img->sub_rows[sub];
        return S_OK;
    }

    // DYNAMIC textures: the data written since Map goes to the image, in order with the frame's commands.
    void unmap_texture(Context *ctx, Image *img, UINT sub)
    {
        Core *c = core();
        if (!img->dynamic || img->dynamic_sub != sub)
            return;
        VkCommandBuffer cb = ctx->outside_rendering();
        transition(cb, img, VK_IMAGE_LAYOUT_GENERAL);
        transfer_barrier(cb, true);
        const VkBufferImageCopy r = region(*img, sub, img->dynamic.offset);
        vkCmdCopyBufferToImage(cb, img->dynamic.buffer, img->image, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
        transfer_barrier(cb, false);
        c->heap->release(img->dynamic, c->recording_value());
        img->dynamic = {};
        img->dynamic_sub = UINT32_MAX;
    }

    void update_texture(Context *ctx, Image *img, UINT sub, const D3D11_BOX *box, const void *data, UINT row_pitch, UINT depth_pitch)
    {
        Core *c = core();
        if (!data || sub >= img->subresources() || !img->image)
            return;
        const uint32_t m = sub % img->mips, layer = sub / img->mips;
        uint32_t bw;
        const uint32_t bs = block_size(img->format, bw);
        const uint32_t x0 = box ? box->left : 0, y0 = box ? box->top : 0, z0 = box ? box->front : 0;
        const uint32_t x1 = box ? box->right : img->mip_width(m), y1 = box ? box->bottom : img->mip_height(m),
                       z1 = box ? box->back : img->mip_depth(m);
        if (x1 <= x0 || y1 <= y0 || z1 <= z0)
            return;
        const uint32_t cols = (x1 - x0 + bw - 1) / bw, rows = (y1 - y0 + bw - 1) / bw, pitch = cols * bs;
        const Slice s = c->heap->alloc(VkDeviceSize(pitch) * rows * (z1 - z0));
        for (uint32_t z = 0; z < z1 - z0; ++z)
            for (uint32_t r = 0; r < rows; ++r)
                memcpy(s.ptr + (VkDeviceSize(z) * rows + r) * pitch, static_cast<const uint8_t *>(data) + size_t(z) * depth_pitch + size_t(r) * row_pitch,
                       pitch);
        VkBufferImageCopy rc{};
        rc.bufferOffset = s.offset;
        rc.imageSubresource = {img->copy_aspect(), m, layer, 1};
        rc.imageOffset = {int32_t(x0), int32_t(y0), int32_t(z0)};
        // a block-compressed edge region may extend past the mip; Vulkan wants the texel extent clamped
        rc.imageExtent = {x1 - x0, y1 - y0, z1 - z0};
        VkCommandBuffer cb = ctx->outside_rendering();
        transition(cb, img, VK_IMAGE_LAYOUT_GENERAL);
        transfer_barrier(cb, true);
        vkCmdCopyBufferToImage(cb, s.buffer, img->image, VK_IMAGE_LAYOUT_GENERAL, 1, &rc);
        transfer_barrier(cb, false);
        c->heap->release(s, c->recording_value());
    }
} // namespace xrvk

using namespace xrvk;

HRESULT ID3D11Device::CreateTexture1D(const D3D11_TEXTURE1D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture1D **ppTexture1D)
{
    if (!pDesc)
        return E_INVALIDARG;
    if (!ppTexture1D)
        return S_FALSE;
    *ppTexture1D = create_texture1d(static_cast<Device *>(this), *pDesc, pInitialData);
    return *ppTexture1D ? S_OK : E_INVALIDARG;
}

HRESULT ID3D11Device::CreateTexture2D(const D3D11_TEXTURE2D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture2D **ppTexture2D)
{
    if (!pDesc)
        return E_INVALIDARG;
    if (!ppTexture2D)
        return S_FALSE;
    *ppTexture2D = create_texture2d(static_cast<Device *>(this), *pDesc, pInitialData);
    return *ppTexture2D ? S_OK : E_INVALIDARG;
}

HRESULT ID3D11Device::CreateTexture3D(const D3D11_TEXTURE3D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture3D **ppTexture3D)
{
    if (!pDesc)
        return E_INVALIDARG;
    if (!ppTexture3D)
        return S_FALSE;
    *ppTexture3D = create_texture3d(static_cast<Device *>(this), *pDesc, pInitialData);
    return *ppTexture3D ? S_OK : E_INVALIDARG;
}

void ID3D11Texture1D::GetDesc(D3D11_TEXTURE1D_DESC *pDesc)
{
    *pDesc = static_cast<Texture1D *>(this)->desc;
}
void ID3D11Texture2D::GetDesc(D3D11_TEXTURE2D_DESC *pDesc)
{
    *pDesc = static_cast<Texture2D *>(this)->desc;
}
void ID3D11Texture3D::GetDesc(D3D11_TEXTURE3D_DESC *pDesc)
{
    *pDesc = static_cast<Texture3D *>(this)->desc;
}
