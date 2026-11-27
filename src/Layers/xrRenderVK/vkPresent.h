// Present-time image processing between the engine's back buffer and the swapchain:
//  1. the engine's gamma/brightness/contrast curve (DXGI gamma control, which a non-exclusive Vulkan
//     swapchain has no hardware ramp for), at render size;
//  2. upscaling to the window size when the engine renders smaller (a lower vid_mode in full screen):
//     AMD FidelityFX Super Resolution 1.0, EASU + RCAS (MIT, build/deps/fsr1-a21ffb8).
// Each pass is a compute dispatch; the result is copied to the swapchain by the final blit.
// Settings (environment, read once): XR_VK_UPSCALE = fsr (default) | linear (plain bilinear blit);
// XR_VK_FSR_SHARPNESS = RCAS attenuation in stops, 0 = sharpest (default 0.25).
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

        struct Result
        {
            VkImage image; // GENERAL layout; RGBA8 unless it is the back buffer itself
            uint32_t width, height;
        };

        // Stores the curve (kGammaPoints RGB values in [0,1]); an identity curve disables the pass.
        void set_gamma(const float (*rgb)[3]);
        // Records the active passes for `src` (GENERAL layout) towards an output of `out` pixels and
        // returns the image the final blit must read. `cb` is outside any rendering scope.
        Result record(VkCommandBuffer cb, const Image &src, VkExtent2D out);
        void destroy();

    private:
        struct Target
        {
            VkImage image = VK_NULL_HANDLE;
            VmaAllocation alloc = nullptr;
            VkImageView view = VK_NULL_HANDLE;
            uint32_t w = 0, h = 0;
            bool fresh = true; // still in UNDEFINED layout
        };
        bool init();
        VkPipeline compute_pipeline(const char *hlsl, const char *what, bool fsr);
        void ensure_target(Target &t, uint32_t w, uint32_t h);
        void destroy_target(Target &t);
        VkImageView source_view(const Image &src);
        void dispatch(VkCommandBuffer cb, VkPipeline p, VkImageView in, Target &out, const void *push, uint32_t push_size,
                      const VkDescriptorBufferInfo *lut, uint32_t groups_x, uint32_t groups_y);

        std::array<float, kGammaPoints * 4> m_lut{}; // RGBA per point (A unused), std140-friendly
        bool m_gamma_active = false;
        bool m_fsr = true;
        float m_sharpness = 0.25f;
        int m_state = 0; // 0 not created, 1 ready, -1 failed (logged; passes are skipped)
        VkDescriptorSetLayout m_set_layout = VK_NULL_HANDLE;
        VkPipelineLayout m_layout = VK_NULL_HANDLE;
        VkPipeline m_gamma = VK_NULL_HANDLE, m_easu = VK_NULL_HANDLE, m_rcas = VK_NULL_HANDLE;
        VkSampler m_sampler = VK_NULL_HANDLE;
        Target m_graded, m_scaled, m_sharpened;
        VkImageView m_src_view = VK_NULL_HANDLE;
        VkImage m_src_image = VK_NULL_HANDLE;
    };

    // The process-wide present pass (one swapchain).
    PresentPass &present_pass();
} // namespace xrvk
