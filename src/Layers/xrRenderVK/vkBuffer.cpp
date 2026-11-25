// Buffers of the Vulkan backend (see Buffer in vkObjects.h): creation, Map of dynamic and staging
// buffers, UpdateSubresource of default buffers.
#include "vkObjects.h"

#include <cstring>

namespace xrvk
{
    void transfer_barrier(VkCommandBuffer cb, bool before)
    {
        VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        if (before)
        {
            mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            mb.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            mb.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            mb.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
        }
        else
        {
            mb.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            mb.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        }
        VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        d.memoryBarrierCount = 1;
        d.pMemoryBarriers = &mb;
        vkCmdPipelineBarrier2(cb, &d);
    }

    bool Buffer::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(ID3D11Resource), __uuidof(ID3D11Buffer)});
    }

    Buffer::~Buffer()
    {
        Core *c = core();
        if (!c)
            return;
        if (slice)
            c->heap->release(slice, c->recording_value());
        if (buffer)
        {
            VkBuffer b = buffer;
            VmaAllocation a = alloc;
            c->track(mem_kind, a, -1);
            c->defer([c, b, a]
                     {
                         vmaDestroyBuffer(c->vma, b, a);
                     });
        }
    }

    Buffer *create_buffer(ID3D11Device *dev, const D3D11_BUFFER_DESC &d, const D3D11_SUBRESOURCE_DATA *init)
    {
        Core *c = core();
        if (d.ByteWidth == 0)
            return nullptr;
        if (d.Usage == D3D11_USAGE_IMMUTABLE && !(init && init->pSysMem))
            return nullptr; // D3D11: immutable buffers need their data at creation
        auto *b = new Buffer();
        b->xrvk_device = dev;
        b->xrvk_dimension = D3D11_RESOURCE_DIMENSION_BUFFER;
        b->desc = d;
        const void *data = init ? init->pSysMem : nullptr;
        switch (d.Usage)
        {
        case D3D11_USAGE_DYNAMIC:
            b->slice = c->heap->alloc(d.ByteWidth);
            if (data)
                memcpy(b->slice.ptr, data, d.ByteWidth);
            return b;
        case D3D11_USAGE_STAGING: {
            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bi.size = d.ByteWidth;
            bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            VmaAllocationInfo info{};
            XRVK_CHECK(vmaCreateBuffer(c->vma, &bi, &ai, &b->buffer, &b->alloc, &info));
            b->mem_kind = Core::kMemStagingBuffer;
            c->track(b->mem_kind, b->alloc, 1);
            b->mapped = static_cast<uint8_t *>(info.pMappedData);
            if (data)
                memcpy(b->mapped, data, d.ByteWidth);
            return b;
        }
        default: {
            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bi.size = d.ByteWidth;
            bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            if (d.BindFlags & D3D11_BIND_VERTEX_BUFFER)
                bi.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            if (d.BindFlags & D3D11_BIND_INDEX_BUFFER)
                bi.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            if (d.BindFlags & D3D11_BIND_CONSTANT_BUFFER)
                bi.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            if (d.BindFlags & (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS))
                bi.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
                            VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
            if (d.MiscFlags & D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS)
                bi.usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            XRVK_CHECK(vmaCreateBuffer(c->vma, &bi, &ai, &b->buffer, &b->alloc, nullptr));
            b->mem_kind = Core::kMemBuffer;
            c->track(b->mem_kind, b->alloc, 1);
            if (data)
            {
                const Slice s = c->heap->alloc(d.ByteWidth);
                memcpy(s.ptr, data, d.ByteWidth);
                {
                    std::lock_guard<std::mutex> g(c->upload_lock);
                    VkBufferCopy r{s.offset, 0, d.ByteWidth};
                    vkCmdCopyBuffer(c->upload_cmd(), s.buffer, b->buffer, 1, &r);
                }
                c->heap->release(s, c->recording_value());
            }
            return b;
        }
        }
    }

    HRESULT map_buffer(Context *ctx, Buffer *b, D3D11_MAP type, D3D11_MAPPED_SUBRESOURCE *out)
    {
        Core *c = core();
        switch (type)
        {
        case D3D11_MAP_WRITE_DISCARD:
            if (!b->dynamic())
                return E_INVALIDARG;
            c->heap->release(b->slice, c->recording_value());
            b->slice = c->heap->alloc(b->desc.ByteWidth);
            ++b->version;
            out->pData = b->slice.ptr;
            break;
        case D3D11_MAP_WRITE_NO_OVERWRITE:
            if (!b->dynamic())
                return E_INVALIDARG;
            out->pData = b->slice.ptr; // the caller promises not to touch data in use
            break;
        default: // READ / WRITE / READ_WRITE: staging buffers only
            if (!b->mapped)
                return E_INVALIDARG;
            if (type != D3D11_MAP_WRITE)
                c->flush_and_wait(); // D3D11 Map blocks until the GPU wrote the data
            out->pData = b->mapped;
            break;
        }
        (void)ctx;
        out->RowPitch = out->DepthPitch = b->desc.ByteWidth;
        return S_OK;
    }

    void update_buffer(Context *ctx, Buffer *b, const D3D11_BOX *box, const void *data)
    {
        Core *c = core();
        const UINT begin = box ? box->left : 0, end = box ? box->right : b->desc.ByteWidth;
        if (!data || begin >= end || end > b->desc.ByteWidth)
            return;
        if (b->dynamic() || b->mapped)
        {
            log("! vk: UpdateSubresource on a dynamic/staging buffer (D3D11 forbids it)");
            return;
        }
        const Slice s = c->heap->alloc(end - begin);
        memcpy(s.ptr, data, end - begin);
        VkCommandBuffer cb = ctx->outside_rendering();
        transfer_barrier(cb, true);
        VkBufferCopy r{s.offset, begin, end - begin};
        vkCmdCopyBuffer(cb, s.buffer, b->buffer, 1, &r);
        transfer_barrier(cb, false);
        c->heap->release(s, c->recording_value());
    }
} // namespace xrvk

using namespace xrvk;

HRESULT ID3D11Device::CreateBuffer(const D3D11_BUFFER_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Buffer **ppBuffer)
{
    if (ppBuffer)
        *ppBuffer = nullptr; // D3D11 clears the output, so a failed call never hands out garbage
    if (!pDesc)
        return E_INVALIDARG;
    if (!ppBuffer)
        return S_FALSE;
    *ppBuffer = create_buffer(this, *pDesc, pInitialData);
    return *ppBuffer ? S_OK : E_INVALIDARG;
}

void ID3D11Buffer::GetDesc(D3D11_BUFFER_DESC *pDesc)
{
    if (pDesc)
        *pDesc = static_cast<Buffer *>(this)->desc;
}
