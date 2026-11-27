// Present-time image processing between the engine's back buffer and the swapchain: the engine's
// gamma/brightness/contrast curve (DXGI gamma control, which a non-exclusive Vulkan swapchain has no
// hardware ramp for) applied by a compute pass. The output image replaces the back buffer as the
// source of the final blit. Further passes (upscaling) belong here too.
#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <array>
#include <cstdint>

namespace xrvk
{
    struct Image;

    class PresentPass
    {
    public:
        static constexpr uint32_t kGammaPoints = 256; // control points offered to the engine

        // Stores the curve (kGammaPoints RGB values in [0,1]); an identity curve disables the pass.
        void set_gamma(const float (*rgb)[3]);
        // Records the passes for `src` (GENERAL layout) and returns the image the blit must read
        // (GENERAL), or `src_image` itself when no pass is active. `cb` is outside any rendering scope.
        VkImage record(VkCommandBuffer cb, const Image &src);
        void destroy();

    private:
        bool init();
        bool resize(const Image &src);

        std::array<float, kGammaPoints * 4> m_lut{}; // RGBA per point (A unused), std140-friendly
        bool m_gamma_active = false;
        int m_state = 0; // 0 not created, 1 ready, -1 failed (logged; the pass is skipped)
        VkDescriptorSetLayout m_set_layout = VK_NULL_HANDLE;
        VkPipelineLayout m_layout = VK_NULL_HANDLE;
        VkPipeline m_gamma = VK_NULL_HANDLE;
        VkImage m_out = VK_NULL_HANDLE;
        VmaAllocation m_out_alloc = nullptr;
        VkImageView m_out_view = VK_NULL_HANDLE;
        VkImageView m_src_view = VK_NULL_HANDLE;
        VkImage m_src_image = VK_NULL_HANDLE;
        uint32_t m_w = 0, m_h = 0;
        bool m_first_use = true; // m_out still in UNDEFINED layout
    };

    // The process-wide present pass (one swapchain).
    PresentPass &present_pass();
} // namespace xrvk
