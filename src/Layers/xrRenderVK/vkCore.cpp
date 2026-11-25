// See vkCore.h.
#include "vkCore.h"
#include "vkHeap.h"

#include <type_traits>

#include <SDL3/SDL_vulkan.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <vk_mem_alloc.h>

namespace xrvk
{
    namespace
    {
        void stderr_log(const char *line)
        {
            fprintf(stderr, "%s\n", line);
        }
        LogFn g_log = stderr_log;
        Core *g_core = nullptr;
        VkInstance g_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT g_messenger = VK_NULL_HANDLE;
        bool g_validation = false;
        std::vector<VkPhysicalDevice> g_phys;

        VKAPI_ATTR VkBool32 VKAPI_CALL on_debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                                const VkDebugUtilsMessengerCallbackDataEXT *data, void *)
        {
            const char *tag = severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "! vk validation error" : "~ vk validation";
            log("%s: %s", tag, data && data->pMessage ? data->pMessage : "");
            return VK_FALSE;
        }

        bool has_extension(const std::vector<VkExtensionProperties> &list, const char *name)
        {
            for (const auto &e : list)
                if (!strcmp(e.extensionName, name))
                    return true;
            return false;
        }

        std::vector<VkExtensionProperties> device_extensions(VkPhysicalDevice p)
        {
            uint32_t n = 0;
            vkEnumerateDeviceExtensionProperties(p, nullptr, &n, nullptr);
            std::vector<VkExtensionProperties> v(n);
            vkEnumerateDeviceExtensionProperties(p, nullptr, &n, v.data());
            return v;
        }
    } // namespace

    void set_log(LogFn fn)
    {
        g_log = fn ? fn : stderr_log;
    }

    void log(const char *fmt, ...)
    {
        char buf[2048];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        g_log(buf);
    }

    void fail(const char *fmt, ...)
    {
        char buf[2048];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        log("! vk: FATAL: %s", buf);
        abort();
    }

    Core *core()
    {
        return g_core;
    }

    VkInstance create_instance()
    {
        if (g_instance)
            return g_instance;
        if (!SDL_Vulkan_LoadLibrary(nullptr))
            fail("SDL_Vulkan_LoadLibrary: %s", SDL_GetError());
        Uint32 n_sdl = 0;
        const char *const *sdl_ext = SDL_Vulkan_GetInstanceExtensions(&n_sdl);
        if (!sdl_ext)
            fail("SDL_Vulkan_GetInstanceExtensions: %s", SDL_GetError());
        std::vector<const char *> ext(sdl_ext, sdl_ext + n_sdl);

        const char *v = getenv("XR_VK_VALIDATION");
        g_validation = v && *v == '1';
        std::vector<const char *> layers;
        if (g_validation)
        {
            uint32_t n = 0;
            vkEnumerateInstanceLayerProperties(&n, nullptr);
            std::vector<VkLayerProperties> avail(n);
            vkEnumerateInstanceLayerProperties(&n, avail.data());
            bool found = false;
            for (const auto &l : avail)
                found |= !strcmp(l.layerName, "VK_LAYER_KHRONOS_validation");
            if (found)
            {
                layers.push_back("VK_LAYER_KHRONOS_validation");
                ext.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            }
            else
            {
                log("! vk: XR_VK_VALIDATION=1 but VK_LAYER_KHRONOS_validation is not installed");
                g_validation = false;
            }
        }

        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "X-Ray Monolith";
        app.pEngineName = "xrRender_VK";
        app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = uint32_t(ext.size());
        ci.ppEnabledExtensionNames = ext.data();
        ci.enabledLayerCount = uint32_t(layers.size());
        ci.ppEnabledLayerNames = layers.data();
        XRVK_CHECK(vkCreateInstance(&ci, nullptr, &g_instance));

        if (g_validation)
        {
            auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(g_instance, "vkCreateDebugUtilsMessengerEXT"));
            VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            mi.pfnUserCallback = on_debug;
            if (create)
                XRVK_CHECK(create(g_instance, &mi, nullptr, &g_messenger));
        }

        uint32_t n = 0;
        XRVK_CHECK(vkEnumeratePhysicalDevices(g_instance, &n, nullptr));
        g_phys.resize(n);
        XRVK_CHECK(vkEnumeratePhysicalDevices(g_instance, &n, g_phys.data()));
        log("* vk: instance (API 1.3)%s, %u GPU(s)", g_validation ? " with validation" : "", n);
        return g_instance;
    }

    const std::vector<VkPhysicalDevice> &physical_devices()
    {
        create_instance();
        return g_phys;
    }

    VkPhysicalDevice preferred_physical_device()
    {
        const auto &list = physical_devices();
        if (list.empty())
            fail("no Vulkan GPU");
        if (const char *s = getenv("XR_VK_GPU"))
        {
            const unsigned i = unsigned(atoi(s));
            if (i < list.size())
                return list[i];
            log("! vk: XR_VK_GPU=%s out of range, ignored", s);
        }
        VkPhysicalDevice best = list[0];
        uint64_t best_score = 0;
        for (VkPhysicalDevice p : list)
        {
            VkPhysicalDeviceProperties pr;
            vkGetPhysicalDeviceProperties(p, &pr);
            VkPhysicalDeviceMemoryProperties mp;
            vkGetPhysicalDeviceMemoryProperties(p, &mp);
            uint64_t local = 0;
            for (uint32_t h = 0; h < mp.memoryHeapCount; ++h)
                if (mp.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                    local += mp.memoryHeaps[h].size;
            // discrete first (hybrid laptops: the NVIDIA GPU, not the integrated one), then memory
            const uint64_t score = (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? (1ull << 62) : 0) + local;
            if (score > best_score)
            {
                best_score = score;
                best = p;
            }
        }
        return best;
    }

    Core &create_device(VkPhysicalDevice phys)
    {
        if (g_core)
            return *g_core;
        create_instance();
        Core &c = *(g_core = new Core());
        c.instance = g_instance;
        c.messenger = g_messenger;
        c.validation = g_validation;
        c.phys = phys;
        vkGetPhysicalDeviceProperties(phys, &c.props);
        vkGetPhysicalDeviceMemoryProperties(phys, &c.mem_props);
        if (c.props.apiVersion < VK_API_VERSION_1_3)
            fail("%s supports Vulkan %u.%u; 1.3 is required", c.props.deviceName, VK_API_VERSION_MAJOR(c.props.apiVersion),
                 VK_API_VERSION_MINOR(c.props.apiVersion));

        // one graphics+compute queue (also used for transfers and present)
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(phys, &nq, nullptr);
        std::vector<VkQueueFamilyProperties> qf(nq);
        vkGetPhysicalDeviceQueueFamilyProperties(phys, &nq, qf.data());
        c.queue_family = UINT32_MAX;
        for (uint32_t i = 0; i < nq; ++i)
            if ((qf[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
            {
                c.queue_family = i;
                break;
            }
        if (c.queue_family == UINT32_MAX)
            fail("%s has no graphics+compute queue", c.props.deviceName);

        // extensions: ADR 0001 requires shader objects and fully dynamic state
        const auto avail = device_extensions(phys);
        const char *required[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_SHADER_OBJECT_EXTENSION_NAME,
                                  VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME, VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
                                  VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
        std::vector<const char *> ext;
        for (const char *r : required)
        {
            if (!has_extension(avail, r))
                fail("%s lacks %s (required, see ADR 0001)", c.props.deviceName, r);
            ext.push_back(r);
        }
        c.memory_budget = has_extension(avail, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
        if (c.memory_budget)
            ext.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);

        // features: query what the GPU has, then enable the subset the renderer uses
        VkPhysicalDeviceExtendedDynamicState3FeaturesEXT eds3{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT};
        VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT vids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT, &eds3};
        VkPhysicalDeviceShaderObjectFeaturesEXT so{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT, &vids};
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &so};
        VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &f13};
        VkPhysicalDeviceVulkan11Features f11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &f12};
        VkPhysicalDeviceFeatures2 have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f11};
        vkGetPhysicalDeviceFeatures2(phys, &have);
        auto need = [&](VkBool32 v, const char *what)
        {
            if (!v)
                fail("%s lacks the %s feature (required)", c.props.deviceName, what);
        };
        need(f13.dynamicRendering, "dynamicRendering");
        need(f13.synchronization2, "synchronization2");
        need(f12.timelineSemaphore, "timelineSemaphore");
        need(so.shaderObject, "shaderObject");
        need(f12.hostQueryReset, "hostQueryReset");
        need(vids.vertexInputDynamicState, "vertexInputDynamicState");
        // keep the queried values for the optional extended-dynamic-state-3 bits, the 1.1/1.2/1.3 sets and
        // the base features the D3D11 feature level 11_0 implies; drop what is not used to save driver work
        VkPhysicalDeviceFeatures &b = have.features;
        VkPhysicalDeviceFeatures base{};
        base.geometryShader = b.geometryShader;
        base.tessellationShader = b.tessellationShader;
        base.samplerAnisotropy = b.samplerAnisotropy;
        base.textureCompressionBC = b.textureCompressionBC;
        base.depthClamp = b.depthClamp;
        base.depthBiasClamp = b.depthBiasClamp;
        base.fillModeNonSolid = b.fillModeNonSolid;
        base.independentBlend = b.independentBlend;
        base.dualSrcBlend = b.dualSrcBlend;
        base.imageCubeArray = b.imageCubeArray;
        base.shaderClipDistance = b.shaderClipDistance;
        base.shaderCullDistance = b.shaderCullDistance;
        base.occlusionQueryPrecise = b.occlusionQueryPrecise;
        base.fragmentStoresAndAtomics = b.fragmentStoresAndAtomics;
        base.shaderImageGatherExtended = b.shaderImageGatherExtended;
        base.sampleRateShading = b.sampleRateShading;
        base.alphaToOne = b.alphaToOne;
        need(base.textureCompressionBC, "textureCompressionBC");
        have.features = base;

        const float prio = 1.f;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = c.queue_family;
        qci.queueCount = 1;
        qci.pQueuePriorities = &prio;
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &have};
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        dci.enabledExtensionCount = uint32_t(ext.size());
        dci.ppEnabledExtensionNames = ext.data();
        XRVK_CHECK(vkCreateDevice(phys, &dci, nullptr, &c.device));
        vkGetDeviceQueue(c.device, c.queue_family, 0, &c.queue);
        auto load = [&c](auto &fn, const char *name)
        {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(vkGetDeviceProcAddr(c.device, name));
            if (!fn)
                fail("vkGetDeviceProcAddr(%s) failed", name);
        };
        load(c.ext.create_shaders, "vkCreateShadersEXT");
        load(c.ext.destroy_shader, "vkDestroyShaderEXT");
        load(c.ext.bind_shaders, "vkCmdBindShadersEXT");
        load(c.ext.push_descriptor_set, "vkCmdPushDescriptorSetKHR");
        load(c.ext.set_vertex_input, "vkCmdSetVertexInputEXT");
        load(c.ext.set_polygon_mode, "vkCmdSetPolygonModeEXT");
        load(c.ext.set_rasterization_samples, "vkCmdSetRasterizationSamplesEXT");
        load(c.ext.set_sample_mask, "vkCmdSetSampleMaskEXT");
        load(c.ext.set_alpha_to_coverage, "vkCmdSetAlphaToCoverageEnableEXT");
        load(c.ext.set_alpha_to_one, "vkCmdSetAlphaToOneEnableEXT");
        load(c.ext.set_color_blend_enable, "vkCmdSetColorBlendEnableEXT");
        load(c.ext.set_color_blend_equation, "vkCmdSetColorBlendEquationEXT");
        load(c.ext.set_color_write_mask, "vkCmdSetColorWriteMaskEXT");
        load(c.ext.set_depth_clamp, "vkCmdSetDepthClampEnableEXT");
        load(c.ext.set_tess_domain_origin, "vkCmdSetTessellationDomainOriginEXT");
        c.features = have.features;
        VkPhysicalDevicePushDescriptorPropertiesKHR pdp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR};
        VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &pdp};
        vkGetPhysicalDeviceProperties2(phys, &p2);
        c.max_push_descriptors = pdp.maxPushDescriptors;

        VmaAllocatorCreateInfo ai{};
        ai.physicalDevice = phys;
        ai.device = c.device;
        ai.instance = g_instance;
        ai.vulkanApiVersion = VK_API_VERSION_1_3;
        if (c.memory_budget)
            ai.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
        XRVK_CHECK(vmaCreateAllocator(&ai, &c.vma));
        c.heap = new HostHeap();
        c.heap->init(c.vma);

        VkSemaphoreTypeCreateInfo tci{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        tci.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &tci};
        XRVK_CHECK(vkCreateSemaphore(c.device, &sci, nullptr, &c.timeline));
        for (Frame &f : c.frames)
        {
            VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            pci.queueFamilyIndex = c.queue_family;
            XRVK_CHECK(vkCreateCommandPool(c.device, &pci, nullptr, &f.pool));
            VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qi.queryCount = 2 * Frame::kTimedParts;
            XRVK_CHECK(vkCreateQueryPool(c.device, &qi, nullptr, &f.timestamps));
            vkResetQueryPool(c.device, f.timestamps, 0, qi.queryCount);
        }
        {
            VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pci.queueFamilyIndex = c.queue_family;
            XRVK_CHECK(vkCreateCommandPool(c.device, &pci, nullptr, &c.upload_pool));
        }
        // NVIDIA packs its driver version as 10.8.8.6 bits; others use the Vulkan encoding
        const uint32_t dv = c.props.driverVersion;
        const bool nv = c.props.vendorID == 0x10de;
        log("* vk: device %s (driver %u.%u.%u), %s", c.props.deviceName, nv ? dv >> 22 : VK_API_VERSION_MAJOR(dv),
            nv ? (dv >> 14) & 0xff : VK_API_VERSION_MINOR(dv), nv ? (dv >> 6) & 0xff : VK_API_VERSION_PATCH(dv),
            c.memory_budget ? "memory budget on" : "no memory budget extension");
        return c;
    }

    VkCommandBuffer Core::cmd()
    {
        Frame &f = frames[frame];
        if (!recording)
        {
            if (f.used == f.cmds.size())
            {
                VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
                cai.commandPool = f.pool;
                cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                cai.commandBufferCount = 1;
                VkCommandBuffer cb;
                XRVK_CHECK(vkAllocateCommandBuffers(device, &cai, &cb));
                f.cmds.push_back(cb);
            }
            VkCommandBuffer cb = f.cmds[f.used++];
            VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            XRVK_CHECK(vkBeginCommandBuffer(cb, &bi));
            if (f.timed < Frame::kTimedParts)
                vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, f.timestamps, 2 * f.timed);
            recording = true;
            part_draws = 0;
        }
        return f.cmds[f.used - 1];
    }

    VkCommandBuffer Core::upload_cmd()
    {
        if (!upload_open)
        {
            const uint64_t done = completed_value();
            while (!upload_pending.empty() && upload_pending.front().first <= done)
            {
                XRVK_CHECK(vkResetCommandBuffer(upload_pending.front().second, 0));
                upload_free.push_back(upload_pending.front().second);
                upload_pending.pop_front();
            }
            if (upload_free.empty())
            {
                VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
                cai.commandPool = upload_pool;
                cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                cai.commandBufferCount = 1;
                VkCommandBuffer cb;
                XRVK_CHECK(vkAllocateCommandBuffers(device, &cai, &cb));
                upload_free.push_back(cb);
            }
            upload_open = upload_free.back();
            upload_free.pop_back();
            VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            XRVK_CHECK(vkBeginCommandBuffer(upload_open, &bi));
        }
        return upload_open;
    }

    void Core::defer(std::function<void()> fn)
    {
        std::lock_guard<std::mutex> g(deferred_lock);
        deferred.emplace_back(recording_value(), std::move(fn));
    }

    uint64_t Core::completed_value()
    {
        uint64_t v = 0;
        XRVK_CHECK(vkGetSemaphoreCounterValue(device, timeline, &v));
        return v;
    }

    void Core::submit(VkSemaphore wait, VkPipelineStageFlags2 wait_stage, VkSemaphore signal)
    {
        VkCommandBuffer cb = cmd(); // an empty frame still submits, so the timeline keeps advancing
        if (before_submit)
            before_submit(cb);
        if (frames[frame].timed < Frame::kTimedParts)
        {
            vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, frames[frame].timestamps, 2 * frames[frame].timed + 1);
            ++frames[frame].timed;
        }
        XRVK_CHECK(vkEndCommandBuffer(cb));
        recording = false;
        Frame &f = frames[frame];
        VkCommandBufferSubmitInfo cbi[2] = {{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO}, {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO}};
        uint32_t ncb = 0;
        {
            std::lock_guard<std::mutex> g(upload_lock);
            if (upload_open)
            {
                VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
                mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                mb.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                mb.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
                VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                d.memoryBarrierCount = 1;
                d.pMemoryBarriers = &mb;
                vkCmdPipelineBarrier2(upload_open, &d);
                XRVK_CHECK(vkEndCommandBuffer(upload_open));
                cbi[ncb++].commandBuffer = upload_open;
                upload_pending.emplace_back(timeline_value + 1, upload_open); // this submission's value
                upload_open = VK_NULL_HANDLE;
            }
        }
        cbi[ncb++].commandBuffer = cb;
        f.done_value = ++timeline_value;
        ++submits;

        VkSemaphoreSubmitInfo waits{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        waits.semaphore = wait;
        waits.stageMask = wait_stage;
        VkSemaphoreSubmitInfo signals[2] = {{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO}, {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO}};
        signals[0].semaphore = timeline;
        signals[0].value = f.done_value;
        signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signals[1].semaphore = signal;
        signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        si.waitSemaphoreInfoCount = wait ? 1 : 0;
        si.pWaitSemaphoreInfos = &waits;
        si.commandBufferInfoCount = ncb;
        si.pCommandBufferInfos = cbi;
        si.signalSemaphoreInfoCount = signal ? 2 : 1;
        si.pSignalSemaphoreInfos = signals;
        XRVK_CHECK(vkQueueSubmit2(queue, 1, &si, VK_NULL_HANDLE));
    }

    void Core::next_frame()
    {
        frame = (frame + 1) % kFramesInFlight;
        Frame &f = frames[frame];
        if (f.done_value)
        {
            VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
            wi.semaphoreCount = 1;
            wi.pSemaphores = &timeline;
            wi.pValues = &f.done_value;
            const auto t0 = std::chrono::steady_clock::now();
            XRVK_CHECK(vkWaitSemaphores(device, &wi, UINT64_MAX));
            wait_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        }
        if (f.timed)
        {
            uint64_t ts[2 * Frame::kTimedParts] = {};
            if (vkGetQueryPoolResults(device, f.timestamps, 0, 2 * f.timed, sizeof(ts), ts, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
            {
                for (uint32_t i = 0; i < f.timed; ++i)
                    gpu_ms += double(ts[2 * i + 1] - ts[2 * i]) * props.limits.timestampPeriod * 1e-6;
                ++gpu_samples;
            }
            vkResetQueryPool(device, f.timestamps, 0, 2 * f.timed);
            f.timed = 0;
        }
        XRVK_CHECK(vkResetCommandPool(device, f.pool, 0));
        f.used = 0;
        const uint64_t done = completed_value();
        heap->collect(done);
        std::deque<std::function<void()>> run;
        {
            std::lock_guard<std::mutex> d(deferred_lock);
            while (!deferred.empty() && deferred.front().first <= done)
            {
                run.push_back(std::move(deferred.front().second));
                deferred.pop_front();
            }
        }
        for (auto &fn : run)
            fn();
    }

    void Core::track(int kind, VmaAllocation a, int sign)
    {
        if (!a)
            return;
        VmaAllocationInfo info{};
        vmaGetAllocationInfo(vma, a, &info);
        mem_bytes[kind] += sign * int64_t(info.size);
        mem_count[kind] += sign;
    }

    void Core::flush()
    {
        if (recording)
            submit();
    }

    void Core::flush_and_wait()
    {
        bool upload;
        {
            std::lock_guard<std::mutex> g(upload_lock);
            upload = upload_open != VK_NULL_HANDLE;
        }
        if (recording || upload)
        {
            submit();
            next_frame();
        }
        XRVK_CHECK(vkQueueWaitIdle(queue));
    }

    void destroy_core()
    {
        if (g_core)
        {
            Core &c = *g_core;
            vkDeviceWaitIdle(c.device);
            for (auto &d : c.deferred)
                d.second();
            c.deferred.clear();
            for (Frame &f : c.frames)
            {
                vkDestroyQueryPool(c.device, f.timestamps, nullptr);
                vkDestroyCommandPool(c.device, f.pool, nullptr);

            }
            vkDestroySemaphore(c.device, c.timeline, nullptr);
            vkDestroyCommandPool(c.device, c.upload_pool, nullptr);
            c.heap->destroy(c.vma);
            delete c.heap;
            vmaDestroyAllocator(c.vma);
            vkDestroyDevice(c.device, nullptr);
            delete g_core;
            g_core = nullptr;
        }
        if (g_instance)
        {
            if (g_messenger)
            {
                auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(g_instance, "vkDestroyDebugUtilsMessengerEXT"));
                if (destroy)
                    destroy(g_instance, g_messenger, nullptr);
            }
            vkDestroyInstance(g_instance, nullptr);
            g_instance = VK_NULL_HANDLE;
            g_messenger = VK_NULL_HANDLE;
            g_phys.clear();
        }
    }
} // namespace xrvk
