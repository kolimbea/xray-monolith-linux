// Vulkan core of xrRender_VK (ADR 0001): instance, GPU, device, VMA, per-frame command recording.
// Independent of the engine (xrVK_core library) so it can be tested on its own (tests/vk_smoke.cpp).
#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

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

    // A frame slot. A frame's commands may be submitted in several parts (flush() mid-frame lets the GPU
    // start early): each part is one command buffer of the slot's pool, reset together in next_frame().
    struct Frame
    {
        static constexpr uint32_t kTimedParts = 32; // parts with GPU timestamps (the rest is not timed)
        VkCommandPool pool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> cmds; // allocated on demand, reused every use of the slot
        uint32_t used = 0;                 // command buffers begun since the slot was reset
        uint64_t done_value = 0;           // timeline value signalled by the slot's last submitted part
        VkQueryPool timestamps = VK_NULL_HANDLE; // (start, end) per timed part
        uint32_t timed = 0;                      // parts whose two timestamps were written
    };
    class HostHeap;

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
            PFN_vkCmdSetVertexInputEXT set_vertex_input = nullptr;
            PFN_vkCmdSetPolygonModeEXT set_polygon_mode = nullptr;
            PFN_vkCmdSetRasterizationSamplesEXT set_rasterization_samples = nullptr;
            PFN_vkCmdSetSampleMaskEXT set_sample_mask = nullptr;
            PFN_vkCmdSetAlphaToCoverageEnableEXT set_alpha_to_coverage = nullptr;
            PFN_vkCmdSetAlphaToOneEnableEXT set_alpha_to_one = nullptr;
            PFN_vkCmdSetColorBlendEnableEXT set_color_blend_enable = nullptr;
            PFN_vkCmdSetColorBlendEquationEXT set_color_blend_equation = nullptr;
            PFN_vkCmdSetColorWriteMaskEXT set_color_write_mask = nullptr;
            PFN_vkCmdSetDepthClampEnableEXT set_depth_clamp = nullptr;
            PFN_vkCmdSetTessellationDomainOriginEXT set_tess_domain_origin = nullptr;
        } ext;
        VkPhysicalDeviceFeatures features{}; // the base features enabled on the device

        VkSemaphore timeline = VK_NULL_HANDLE;
        uint64_t timeline_value = 0; // last value submitted
        Frame frames[kFramesInFlight];
        uint32_t frame = 0; // index into frames
        bool recording = false;

        HostHeap *heap = nullptr; // dynamic buffer versions and upload staging (vkHeap.h)
        // Called with the frame command buffer right before submit() ends it (the immediate context
        // closes its rendering scope there).
        std::function<void(VkCommandBuffer)> before_submit;
        // Uploads of resource creation are recorded on any thread into command buffers that belong to no
        // frame slot: submit() takes the open one (submitted before the frame's commands) and it is reused
        // once the GPU passed that submission. upload_lock guards all of it (and the pool).
        std::mutex upload_lock;
        VkCommandPool upload_pool = VK_NULL_HANDLE;
        VkCommandBuffer upload_open = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> upload_free;
        std::deque<std::pair<uint64_t, VkCommandBuffer>> upload_pending;

        // Command buffer of the current frame, begun on first use (after a flush(), a new one).
        VkCommandBuffer cmd();
        // Submits the recorded part when it holds `draws` draws or more (keeps the GPU busy mid-frame).
        uint32_t part_draws = 0;
        // Upload command buffer of the current frame: initial data of new resources. Runs before the
        // frame's cmd() in the same submit and ends with a barrier making its writes visible to it.
        // The caller holds upload_lock.
        VkCommandBuffer upload_cmd();
        // The timeline value the frame being recorded will signal; and the value the GPU has reached.
        uint64_t recording_value() const
        {
            return timeline_value + 1;
        }
        uint64_t completed_value();
        // live VMA allocations of engine resources by kind (frame statistics; the host heap is separate)
        enum MemKind
        {
            kMemTexture,        // images only sampled (textures)
            kMemTarget,         // render targets, depth buffers, UAV images
            kMemStagingTexture, // D3D11_USAGE_STAGING textures (host buffers)
            kMemBuffer,         // default/immutable buffers
            kMemStagingBuffer,  // D3D11_USAGE_STAGING buffers
            kMemKinds
        };
        std::atomic<int64_t> mem_bytes[kMemKinds] = {};
        std::atomic<int32_t> mem_count[kMemKinds] = {};
        // adds (sign 1) or removes (-1) a live allocation of `kind`
        void track(int kind, VmaAllocation a, int sign);
        // performance counters, accumulated until read (frame statistics)
        double gpu_ms = 0, wait_ms = 0;
        uint32_t gpu_samples = 0, submits = 0, query_polls = 0; // query_polls: GetData answered "not ready"
        // Runs `fn` (destroying Vulkan objects) once the GPU finished every frame recorded so far.
        // Thread-safe.
        void defer(std::function<void()> fn);
        std::mutex deferred_lock;
        std::deque<std::pair<uint64_t, std::function<void()>>> deferred;
        // Ends and submits the current frame's commands; the waits/signals are for the swapchain.
        void submit(VkSemaphore wait = VK_NULL_HANDLE, VkPipelineStageFlags2 wait_stage = 0,
                    VkSemaphore signal = VK_NULL_HANDLE);
        // Advances to the next frame slot, waiting until its previous use has finished on the GPU.
        void next_frame();
        // Submits pending work and waits for the GPU to finish everything (readbacks, teardown).
        void flush_and_wait();
        // Submits the work recorded so far without waiting and stays in the frame slot (D3D11's implicit
        // flush in GetData; a driver submitting large frames in parts).
        void flush();
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
