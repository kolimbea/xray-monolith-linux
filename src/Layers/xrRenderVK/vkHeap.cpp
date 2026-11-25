#include "vkHeap.h"

#include "vkCore.h"

namespace xrvk
{
    namespace
    {
        constexpr VkDeviceSize kPageSize = 32ull << 20;
        constexpr VkDeviceSize kLinearPageSize = 8ull << 20;
        constexpr VkDeviceSize kLinearMax = 256ull << 10; // larger slices use the virtual blocks
    }

    bool HostHeap::create_buffer(VkDeviceSize size, VkBuffer &buffer, VmaAllocation &alloc, uint8_t *&ptr)
    {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = size;
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        if (vmaCreateBuffer(m_vma, &bi, &ai, &buffer, &alloc, &info) != VK_SUCCESS)
            return false;
        ptr = static_cast<uint8_t *>(info.pMappedData);
        m_total += size;
        return true;
    }

    bool HostHeap::add_page(VkDeviceSize size)
    {
        Page p;
        p.size = size;
        if (!create_buffer(size, p.buffer, p.alloc, p.ptr))
            return false;
        VmaVirtualBlockCreateInfo vbi{};
        vbi.size = size;
        if (vmaCreateVirtualBlock(&vbi, &p.block) != VK_SUCCESS)
        {
            vmaDestroyBuffer(m_vma, p.buffer, p.alloc);
            m_total -= size;
            return false;
        }
        m_pages.push_back(p);
        return true;
    }

    Slice HostHeap::alloc_linear(VkDeviceSize size, VkDeviceSize align)
    {
        auto fits = [&](const Linear &l)
        {
            return ((l.head + align - 1) & ~(align - 1)) + size <= kLinearPageSize;
        };
        if (m_linear.empty() || !fits(m_linear[m_current]))
        {
            // the next page all of whose slices the GPU is done with, in round-robin order
            uint32_t next = UINT32_MAX;
            for (uint32_t k = 1; k <= m_linear.size() && next == UINT32_MAX; ++k)
            {
                const uint32_t i = (m_current + k) % m_linear.size();
                if (m_linear[i].live == 0)
                    next = i;
            }
            if (next == UINT32_MAX)
            {
                Linear l;
                if (!create_buffer(kLinearPageSize, l.buffer, l.alloc, l.ptr))
                    fail("vk: out of host-visible memory (%llu MB in use)", (unsigned long long)(m_total >> 20));
                m_linear.push_back(l);
                next = uint32_t(m_linear.size() - 1);
            }
            m_current = next;
            m_linear[m_current].head = 0;
        }
        Linear &l = m_linear[m_current];
        Slice s;
        s.offset = (l.head + align - 1) & ~(align - 1);
        l.head = s.offset + size;
        ++l.live;
        s.buffer = l.buffer;
        s.size = size;
        s.ptr = l.ptr + s.offset;
        s.page = m_current;
        return s;
    }

    Slice HostHeap::alloc(VkDeviceSize size, VkDeviceSize align)
    {
        std::lock_guard<std::mutex> g(m_lock);
        if (size <= kLinearMax)
        {
            Slice s = alloc_linear(size ? size : 1, align); // an empty slice still gets a distinct offset
            s.size = size;
            return s;
        }
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
            if (s.va)
                vmaVirtualFree(m_pages[s.page].block, s.va);
            else
                --m_linear[s.page].live;
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
        for (Linear &l : m_linear)
            vmaDestroyBuffer(vma, l.buffer, l.alloc);
        m_linear.clear();
        m_current = 0;
        m_released.clear();
        m_total = 0;
    }
} // namespace xrvk
