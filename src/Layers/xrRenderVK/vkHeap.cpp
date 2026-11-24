#include "vkHeap.h"

#include "vkCore.h"

namespace xrvk
{
    namespace
    {
        constexpr VkDeviceSize kPageSize = 32ull << 20;
    }

    bool HostHeap::add_page(VkDeviceSize size)
    {
        Page p;
        p.size = size;
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = size;
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        if (vmaCreateBuffer(m_vma, &bi, &ai, &p.buffer, &p.alloc, &info) != VK_SUCCESS)
            return false;
        p.ptr = static_cast<uint8_t *>(info.pMappedData);
        VmaVirtualBlockCreateInfo vbi{};
        vbi.size = size;
        if (vmaCreateVirtualBlock(&vbi, &p.block) != VK_SUCCESS)
        {
            vmaDestroyBuffer(m_vma, p.buffer, p.alloc);
            return false;
        }
        m_pages.push_back(p);
        m_total += size;
        return true;
    }

    Slice HostHeap::alloc(VkDeviceSize size, VkDeviceSize align)
    {
        std::lock_guard<std::mutex> g(m_lock);
        VmaVirtualAllocationCreateInfo ci{};
        ci.size = size ? size : 1;
        ci.alignment = align;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            for (uint32_t i = 0; i < m_pages.size(); ++i)
            {
                Slice s;
                if (vmaVirtualAllocate(m_pages[i].block, &ci, &s.va, &s.offset) == VK_SUCCESS)
                {
                    s.buffer = m_pages[i].buffer;
                    s.size = size;
                    s.ptr = m_pages[i].ptr + s.offset;
                    s.page = i;
                    return s;
                }
            }
            if (!add_page(size > kPageSize / 4 ? (size + align + 0xffff) & ~VkDeviceSize(0xffff) : kPageSize))
                fail("vk: out of host-visible memory (%llu MB in use, %llu more requested)", (unsigned long long)(m_total >> 20),
                     (unsigned long long)(size >> 20));
        }
        fail("vk: host heap allocation of %llu bytes failed", (unsigned long long)size);
    }

    void HostHeap::release(const Slice &s, uint64_t after_value)
    {
        if (!s)
            return;
        std::lock_guard<std::mutex> g(m_lock);
        m_released.emplace_back(after_value, s);
    }

    void HostHeap::collect(uint64_t completed)
    {
        std::lock_guard<std::mutex> g(m_lock);
        while (!m_released.empty() && m_released.front().first <= completed)
        {
            const Slice &s = m_released.front().second;
            vmaVirtualFree(m_pages[s.page].block, s.va);
            m_released.pop_front();
        }
    }

    void HostHeap::destroy(VmaAllocator vma)
    {
        std::lock_guard<std::mutex> g(m_lock);
        for (Page &p : m_pages)
        {
            vmaClearVirtualBlock(p.block);
            vmaDestroyVirtualBlock(p.block);
            vmaDestroyBuffer(vma, p.buffer, p.alloc);
        }
        m_pages.clear();
        m_released.clear();
        m_total = 0;
    }
} // namespace xrvk
