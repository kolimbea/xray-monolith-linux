// Host-visible memory the CPU writes and the GPU reads: versions of D3D11_USAGE_DYNAMIC buffers
// (every Map(WRITE_DISCARD) takes a fresh slice) and staging data of uploads. Large persistently mapped
// pages, sub-allocated with VMA virtual blocks. A released slice is reused only after the GPU passed
// the timeline value given on release, so data a recorded frame still reads is never overwritten.
#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

#include <vk_mem_alloc.h>

namespace xrvk
{
    struct Slice
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0, size = 0;
        uint8_t *ptr = nullptr;
        uint32_t page = 0;
        VmaVirtualAllocation va = VK_NULL_HANDLE;
        explicit operator bool() const
        {
            return buffer != VK_NULL_HANDLE;
        }
    };

    class HostHeap
    {
    public:
        // Thread-safe. Alignment 256 covers every buffer offset rule (uniform, storage, vertex, copy).
        Slice alloc(VkDeviceSize size, VkDeviceSize align = 256);
        // The slice becomes free once the timeline reaches `after_value` (see collect).
        void release(const Slice &s, uint64_t after_value);
        // Frees released slices the GPU is done with; `completed` = timeline value reached.
        void collect(uint64_t completed);
        void destroy(VmaAllocator vma);
        void init(VmaAllocator vma)
        {
            m_vma = vma;
        }
        VkDeviceSize bytes_in_pages() const
        {
            return m_total;
        }

    private:
        struct Page
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VmaAllocation alloc = nullptr;
            uint8_t *ptr = nullptr;
            VmaVirtualBlock block = VK_NULL_HANDLE;
            VkDeviceSize size = 0;
        };
        bool add_page(VkDeviceSize size);
        std::mutex m_lock;
        VmaAllocator m_vma = nullptr;
        std::vector<Page> m_pages;
        std::deque<std::pair<uint64_t, Slice>> m_released; // in release order = timeline order
        VkDeviceSize m_total = 0;
    };
} // namespace xrvk
