// Vulkan core of xrRender_VK (ADR 0001): instance, GPU, device, VMA, per-frame command recording.
// Independent of the engine (xrVK_core library) so it can be tested on its own (tests/vk_smoke.cpp).
#pragma once
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

struct VmaAllocator_T;
struct VmaAllocation_T;
typedef VmaAllocator_T *VmaAllocator;
typedef VmaAllocation_T *VmaAllocation;

namespace xrvk
{
    // Log sink; the renderer points it at the engine log (Msg), the default writes to stderr.
    using LogFn = void (*)(const char *line);
    void set_log(LogFn fn);
    void log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
    // Unrecoverable backend error (bad VkResult, missing hardware feature): logs and aborts.
    [[noreturn]] void fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#define XRVK_CHECK(expr)                                                                        \
    do                                                                                          \
    {                                                                                           \
        const VkResult r_ = (expr);                                                             \
        if (r_ != VK_SUCCESS)                                                                   \
            ::xrvk::fail("%s failed: VkResult %d (%s:%d)", #expr, int(r_), __FILE__, __LINE__); \
    } while (0)

    constexpr uint32_t kFramesInFlight = 3;

    struct Frame
    {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        uint64_t done_value = 0; // timeline value signalled when this frame's work completed
    };

    struct Core
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
        VkPhysicalDevice phys = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties props{};
        VkPhysicalDeviceMemoryProperties mem_props{};
        VkDevice device = VK_NULL_HANDLE;
        uint32_t queue_family = 0;
        VkQueue queue = VK_NULL_HANDLE;
        VmaAllocator vma = nullptr;
        bool validation = false;
        bool memory_budget = false;
        uint32_t max_push_descriptors = 0; // VK_KHR_push_descriptor limit per set

        // device-level entry points of the required extensions (the loader exports core functions only)
        struct
        {
            PFN_vkCreateShadersEXT create_shaders = nullptr;
            PFN_vkDestroyShaderEXT destroy_shader = nullptr;
            PFN_vkCmdBindShadersEXT bind_shaders = nullptr;
            PFN_vkCmdPushDescriptorSetKHR push_descriptor_set = nullptr;
        } ext;

        VkSemaphore timeline = VK_NULL_HANDLE;
        uint64_t timeline_value = 0; // last value submitted
        Frame frames[kFramesInFlight];
        uint32_t frame = 0; // index into frames
        bool recording = false;

        // Command buffer of the current frame, begun on first use.
        VkCommandBuffer cmd();
        // Ends and submits the current frame's commands; the waits/signals are for the swapchain.
        void submit(VkSemaphore wait = VK_NULL_HANDLE, VkPipelineStageFlags2 wait_stage = 0,
                    VkSemaphore signal = VK_NULL_HANDLE);
        // Advances to the next frame slot, waiting until its previous use has finished on the GPU.
        void next_frame();
        // Submits pending work and waits for the GPU to finish everything (readbacks, teardown).
        void flush_and_wait();
    };

    // The process-wide Vulkan core; null until create_device() ran.
    Core *core();
    // Instance with the window-system extensions SDL reports; idempotent.
    // XR_VK_VALIDATION=1 enables VK_LAYER_KHRONOS_validation.
    VkInstance create_instance();
    // GPUs in enumeration order (DXGI adapters map onto these) and the preferred one: a discrete GPU with
    // the most device-local memory, overridable with XR_VK_GPU=<index>.
    const std::vector<VkPhysicalDevice> &physical_devices();
    VkPhysicalDevice preferred_physical_device();
    // Device, queue, VMA and frame resources on `phys`; fails if a required feature is missing.
    Core &create_device(VkPhysicalDevice phys);
    void destroy_core();
} // namespace xrvk
