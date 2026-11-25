// DXGI on Vulkan + SDL3: factory, adapters (Vulkan GPUs), outputs (SDL displays), swap chain.
// The swap chain's buffer 0 is an own texture that Present blits into the acquired swapchain image,
// which also converts between the engine's format and the surface's (e.g. RGBA -> BGRA).
#include "vkObjects.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <cstring>
#include <string>

#include "vkFormat.h"
#include "../../Common/linux/dds_codec.h"

namespace xrvk
{
    bool Factory::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(IDXGIObject), __uuidof(IDXGIFactory), __uuidof(IDXGIFactory1), __uuidof(IDXGIFactory2),
                          __uuidof(IDXGIFactory3), __uuidof(IDXGIFactory4), __uuidof(IDXGIFactory5), __uuidof(IDXGIFactory6)});
    }
    bool Adapter::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(IDXGIObject), __uuidof(IDXGIAdapter), __uuidof(IDXGIAdapter1), __uuidof(IDXGIAdapter2),
                          __uuidof(IDXGIAdapter3), __uuidof(IDXGIAdapter4)});
    }
    bool Output::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(IDXGIObject), __uuidof(IDXGIOutput), __uuidof(IDXGIOutput1), __uuidof(IDXGIOutput2),
                          __uuidof(IDXGIOutput3), __uuidof(IDXGIOutput4), __uuidof(IDXGIOutput5), __uuidof(IDXGIOutput6)});
    }
    bool SwapChain::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(IDXGIObject), __uuidof(IDXGIDeviceSubObject), __uuidof(IDXGISwapChain), __uuidof(IDXGISwapChain1),
                          __uuidof(IDXGISwapChain2), __uuidof(IDXGISwapChain3), __uuidof(IDXGISwapChain4)});
    }

    namespace
    {
        // DXGI adapter order: the preferred GPU first (adapter 0 is the one D3D11CreateDevice(nullptr) uses).
        std::vector<VkPhysicalDevice> adapter_order()
        {
            std::vector<VkPhysicalDevice> v = physical_devices();
            const VkPhysicalDevice best = preferred_physical_device();
            for (size_t i = 0; i < v.size(); ++i)
                if (v[i] == best)
                {
                    v.erase(v.begin() + i);
                    break;
                }
            v.insert(v.begin(), best);
            return v;
        }

        void to_wide(WCHAR *dst, size_t n, const char *src)
        {
            size_t i = 0;
            for (; src && src[i] && i + 1 < n; ++i)
                dst[i] = WCHAR(uint8_t(src[i]));
            dst[i] = 0;
        }

        VkPresentModeKHR pick_mode(VkPhysicalDevice phys, VkSurfaceKHR surface, UINT sync_interval)
        {
            if (sync_interval)
                return VK_PRESENT_MODE_FIFO_KHR; // always available
            uint32_t n = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &n, nullptr);
            std::vector<VkPresentModeKHR> modes(n);
            vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &n, modes.data());
            for (VkPresentModeKHR want : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR})
                for (VkPresentModeKHR m : modes)
                    if (m == want)
                        return m;
            return VK_PRESENT_MODE_FIFO_KHR;
        }

        void window_pixels(SDL_Window *w, uint32_t &width, uint32_t &height)
        {
            int x = 0, y = 0;
            SDL_GetWindowSizeInPixels(w, &x, &y);
            width = x > 0 ? uint32_t(x) : 1;
            height = y > 0 ? uint32_t(y) : 1;
        }
    } // namespace

    void SwapChain::create_swapchain(uint32_t width, uint32_t height)
    {
        Core &c = *core();
        VkSurfaceCapabilitiesKHR caps;
        XRVK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(c.phys, surface, &caps));
        if (caps.currentExtent.width != UINT32_MAX)
            extent = caps.currentExtent;
        else
            extent = {width, height};
        if (!extent.width || !extent.height)
            return; // minimised; recreated on the next Present/ResizeBuffers

        uint32_t nf = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(c.phys, surface, &nf, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(nf);
        vkGetPhysicalDeviceSurfaceFormatsKHR(c.phys, surface, &nf, formats.data());
        VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : formats[0];
        for (const auto &f : formats) // UNORM: the engine writes already-encoded colour, like DXGI's R8G8B8A8_UNORM
            if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                chosen = f;
                break;
            }
        swap_format = chosen.format;

        VkBool32 present_ok = VK_FALSE;
        XRVK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(c.phys, c.queue_family, surface, &present_ok));
        if (!present_ok)
            fail("the chosen GPU cannot present to this window");

        uint32_t count = desc.BufferCount > caps.minImageCount ? desc.BufferCount : caps.minImageCount + 1;
        if (caps.maxImageCount && count > caps.maxImageCount)
            count = caps.maxImageCount;
        VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        ci.surface = surface;
        ci.minImageCount = count;
        ci.imageFormat = chosen.format;
        ci.imageColorSpace = chosen.colorSpace;
        ci.imageExtent = extent;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = mode;
        ci.clipped = VK_TRUE;
        ci.oldSwapchain = swapchain;
        VkSwapchainKHR next;
        XRVK_CHECK(vkCreateSwapchainKHR(c.device, &ci, nullptr, &next));
        if (swapchain)
            vkDestroySwapchainKHR(c.device, swapchain, nullptr);
        swapchain = next;
        uint32_t n = 0;
        vkGetSwapchainImagesKHR(c.device, swapchain, &n, nullptr);
        images.resize(n);
        vkGetSwapchainImagesKHR(c.device, swapchain, &n, images.data());
        for (VkSemaphore s : present_ready)
            vkDestroySemaphore(c.device, s, nullptr);
        present_ready.assign(n, VK_NULL_HANDLE);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (VkSemaphore &s : present_ready)
            XRVK_CHECK(vkCreateSemaphore(c.device, &si, nullptr, &s));
        for (VkSemaphore &s : acquired)
            if (!s)
                XRVK_CHECK(vkCreateSemaphore(c.device, &si, nullptr, &s));
        log("* vk: swapchain %ux%u, %u images, present mode %d", extent.width, extent.height, n, int(mode));
    }

    void SwapChain::destroy_swapchain()
    {
        Core &c = *core();
        vkDeviceWaitIdle(c.device);
        for (VkSemaphore s : present_ready)
            vkDestroySemaphore(c.device, s, nullptr);
        present_ready.clear();
        if (swapchain)
            vkDestroySwapchainKHR(c.device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
        images.clear();
    }

    void SwapChain::create_back_buffer()
    {
        if (back_buffer)
            back_buffer->Release();
        D3D11_TEXTURE2D_DESC d{};
        d.Width = desc.Width;
        d.Height = desc.Height;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = desc.Format;
        d.SampleDesc = {1, 0};
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        back_buffer = create_texture2d(static_cast<Device *>(device), d, nullptr);
        if (!back_buffer)
            fail("cannot create the %ux%u back buffer (format %u)", d.Width, d.Height, unsigned(d.Format));
    }

    SwapChain::~SwapChain()
    {
        if (!core())
            return;
        destroy_swapchain();
        Core &c = *core();
        for (VkSemaphore s : acquired)
            if (s)
                vkDestroySemaphore(c.device, s, nullptr);
        if (surface)
            SDL_Vulkan_DestroySurface(c.instance, surface, nullptr);
        if (back_buffer)
            back_buffer->Release();
        if (device)
            device->Release();
    }
} // namespace xrvk

namespace xrvk
{
    // XR_VK_SCREENSHOT=<file.png>: the back buffer of presented frame XR_VK_SCREENSHOT_FRAME (default 300)
    // is written as PNG (screenshot tests; works without looking at the window). With
    // XR_VK_SCREENSHOT_EVERY=<n> every n-th frame is written; "%d" in the path is the frame number.
    struct Screenshot
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation alloc = nullptr;
        void *ptr = nullptr;
        uint32_t w = 0, h = 0;

        void record(VkCommandBuffer cb, SwapChain *sc)
        {
            const char *path = getenv("XR_VK_SCREENSHOT");
            const char *at = getenv("XR_VK_SCREENSHOT_FRAME");
            const char *every = getenv("XR_VK_SCREENSHOT_EVERY");
            const uint64_t n = every ? uint64_t(atoll(every)) : 0;
            const bool due = n ? (sc->presents && sc->presents % n == 0) : sc->presents == uint64_t(at ? atoll(at) : 300);
            if (!path || !due)
                return;
            Core &c = *core();
            w = sc->back_buffer->desc.Width;
            h = sc->back_buffer->desc.Height;
            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bi.size = VkDeviceSize(w) * h * 4;
            bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            VmaAllocationCreateInfo ai{};
            ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            VmaAllocationInfo info{};
            XRVK_CHECK(vmaCreateBuffer(c.vma, &bi, &ai, &buffer, &alloc, &info));
            ptr = info.pMappedData;
            VkBufferImageCopy r{};
            r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            r.imageExtent = {w, h, 1};
            vkCmdCopyImageToBuffer(cb, sc->back_buffer->img.image, VK_IMAGE_LAYOUT_GENERAL, buffer, 1, &r);
        }

        void write(SwapChain *sc)
        {
            if (!buffer)
                return;
            Core &c = *core();
            vkQueueWaitIdle(c.queue);
            std::vector<uint8_t> rgba(size_t(w) * h * 4);
            memcpy(rgba.data(), ptr, rgba.size());
            const DXGI_FORMAT f = sc->back_buffer->desc.Format;
            if (f == DXGI_FORMAT_B8G8R8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
                for (size_t i = 0; i < rgba.size(); i += 4)
                    std::swap(rgba[i], rgba[i + 2]);
            for (size_t i = 3; i < rgba.size(); i += 4)
                rgba[i] = 255; // the alpha of a back buffer is not shown
            const std::vector<uint8_t> png = xr_dds::write_png(rgba.data(), w, h);
            // "%d" in the path: the frame number (XR_VK_SCREENSHOT_EVERY writes a series)
            char path[1024];
            snprintf(path, sizeof path, getenv("XR_VK_SCREENSHOT"), int(sc->presents));
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()));
            if (out.good())
            {
                log("* vk: screenshot of frame %llu written to %s", (unsigned long long)sc->presents, path);
            }
            else
                log("! vk: cannot write the screenshot %s", path);
            vmaDestroyBuffer(c.vma, buffer, alloc);
            buffer = VK_NULL_HANDLE;
        }
    };
} // namespace

namespace xrvk
{
    // Frame statistics in the log every 600 presents: frame time (average, 99th percentile), and per
    // frame the backend's draw count, rendering scopes (each one a full barrier), program binds and the
    // CPU time spent preparing draws. The baseline of the performance track.
    void frame_stats(SwapChain *, Context *ctx)
    {
        static constexpr uint32_t kStatFrames = 600;
        static std::vector<double> times;
        static auto last = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double, std::milli>(now - last).count());
        last = now;
        if (times.size() < kStatFrames)
            return;
        std::vector<double> sorted = times;
        std::sort(sorted.begin(), sorted.end());
        double sum = 0;
        for (double v : times)
            sum += v;
        const double avg = sum / times.size(), p99 = sorted[size_t(sorted.size() * 0.99)];
        const Context::Stats &s = ctx->stats;
        Core &c = *core();
        const double n = double(times.size());
        log("* vk stats: %.2f ms/frame (%.1f fps), p99 %.2f ms | per frame: GPU %.2f ms in %.1f submits, CPU waits for a frame slot %.2f ms, "
            "%.0f query polls not ready, %.0f draws, %.1f renderings, %.0f program binds, prepare %.2f ms | host heap %llu MB",
            avg, 1000.0 / avg, p99, c.gpu_ms / n, c.submits / n, c.wait_ms / n, c.query_polls / n, s.draws / n, s.renderings / n,
            s.program_binds / n, s.prepare_ms / n, (unsigned long long)(c.heap->bytes_in_pages() >> 20));
        // memory per Vulkan heap: what VMA allocated in it (usage) against what the driver offers (budget);
        // device-local heaps over budget spill resources into system memory
        VmaBudget budgets[VK_MAX_MEMORY_HEAPS] = {};
        vmaGetHeapBudgets(c.vma, budgets);
        std::string heaps;
        for (uint32_t h = 0; h < c.mem_props.memoryHeapCount; ++h)
        {
            char one[96];
            snprintf(one, sizeof(one), "%s%s %llu/%llu MB", heaps.empty() ? "" : ", ",
                     (c.mem_props.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? "VRAM" : "system",
                     (unsigned long long)(budgets[h].statistics.blockBytes >> 20), (unsigned long long)(budgets[h].budget >> 20));
            heaps += one;
        }
        auto mb = [&c](int k) { return (long long)(c.mem_bytes[k].load() >> 20); };
        auto n_ = [&c](int k) { return int(c.mem_count[k].load()); };
        log("* vk memory: %s (allocated/budget per heap) | textures %lld MB (%d), targets %lld MB (%d), staging textures %lld MB (%d), "
            "buffers %lld MB (%d), staging buffers %lld MB (%d)",
            heaps.c_str(), mb(Core::kMemTexture), n_(Core::kMemTexture), mb(Core::kMemTarget), n_(Core::kMemTarget),
            mb(Core::kMemStagingTexture), n_(Core::kMemStagingTexture), mb(Core::kMemBuffer), n_(Core::kMemBuffer),
            mb(Core::kMemStagingBuffer), n_(Core::kMemStagingBuffer));
        c.gpu_ms = c.wait_ms = 0;
        c.gpu_samples = c.submits = c.query_polls = 0;
        ctx->stats = {};
        times.clear();
    }
} // namespace xrvk

using namespace xrvk;

extern "C" HRESULT WINAPI CreateDXGIFactory2(UINT, REFIID riid, void **factory)
{
    if (!factory)
        return E_POINTER;
    create_instance();
    auto *f = new Factory();
    const HRESULT hr = f->QueryInterface(riid, factory);
    f->Release();
    return hr;
}
extern "C" HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void **factory)
{
    return CreateDXGIFactory2(0, riid, factory);
}
extern "C" HRESULT WINAPI CreateDXGIFactory(REFIID riid, void **factory)
{
    return CreateDXGIFactory2(0, riid, factory);
}

HRESULT IDXGIFactory1::EnumAdapters1(UINT Adapter, IDXGIAdapter1 **ppAdapter)
{
    const auto order = adapter_order();
    if (!ppAdapter)
        return E_INVALIDARG;
    if (Adapter >= order.size())
        return DXGI_ERROR_NOT_FOUND;
    auto *a = new xrvk::Adapter();
    a->phys = order[Adapter];
    *ppAdapter = a;
    return S_OK;
}

HRESULT IDXGIFactory::EnumAdapters(UINT adapter_idx, IDXGIAdapter **adapter)
{
    IDXGIAdapter1 *a = nullptr;
    const HRESULT hr = static_cast<IDXGIFactory1 *>(this)->EnumAdapters1(adapter_idx, &a);
    *adapter = a;
    return hr;
}

HRESULT IDXGIFactory5::CheckFeatureSupport(DXGI_FEATURE feature, void *support_data, UINT support_data_size)
{
    if (feature == DXGI_FEATURE_PRESENT_ALLOW_TEARING && support_data && support_data_size == sizeof(BOOL))
    {
        *static_cast<BOOL *>(support_data) = FALSE; // tearing/VRR present path not implemented yet
        return S_OK;
    }
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT IDXGIFactory2::CreateSwapChainForHwnd(IUnknown *pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
                                              const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *, IDXGIOutput *, IDXGISwapChain1 **ppSwapChain)
{
    if (ppSwapChain)
        *ppSwapChain = nullptr; // D3D11 clears the output, so a failed call never hands out garbage
    if (!pDevice || !hWnd || !pDesc || !ppSwapChain || !core())
        return E_INVALIDARG;
    auto *sc = new SwapChain();
    sc->window = static_cast<SDL_Window *>(hWnd); // HWND is the SDL_Window (Common/linux/win32_window)
    sc->desc = *pDesc;
    if (!sc->desc.Width || !sc->desc.Height)
        window_pixels(sc->window, sc->desc.Width, sc->desc.Height);
    ID3D11Device *dev = nullptr;
    if (FAILED(pDevice->QueryInterface(__uuidof(ID3D11Device), reinterpret_cast<void **>(&dev))))
    {
        sc->Release();
        return E_INVALIDARG;
    }
    sc->device = dev; // the swap chain keeps its device alive
    if (!SDL_Vulkan_CreateSurface(sc->window, core()->instance, nullptr, &sc->surface))
    {
        log("! vk: SDL_Vulkan_CreateSurface: %s", SDL_GetError());
        sc->Release();
        return E_FAIL;
    }
    sc->mode = pick_mode(core()->phys, sc->surface, 1);
    sc->create_swapchain(sc->desc.Width, sc->desc.Height);
    sc->create_back_buffer();
    *ppSwapChain = sc;
    return S_OK;
}

HRESULT IDXGIAdapter::GetDesc(DXGI_ADAPTER_DESC *desc)
{
    if (!desc)
        return E_INVALIDARG;
    auto *a = static_cast<xrvk::Adapter *>(static_cast<IDXGIAdapter4 *>(this));
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(a->phys, &p);
    VkPhysicalDeviceMemoryProperties m;
    vkGetPhysicalDeviceMemoryProperties(a->phys, &m);
    *desc = {};
    to_wide(desc->Description, 128, p.deviceName);
    desc->VendorId = p.vendorID;
    desc->DeviceId = p.deviceID;
    for (uint32_t h = 0; h < m.memoryHeapCount; ++h)
        if (m.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            desc->DedicatedVideoMemory += SIZE_T(m.memoryHeaps[h].size);
        else
            desc->SharedSystemMemory += SIZE_T(m.memoryHeaps[h].size);
    return S_OK;
}

HRESULT IDXGIAdapter1::GetDesc1(DXGI_ADAPTER_DESC1 *pDesc)
{
    if (!pDesc)
        return E_INVALIDARG;
    DXGI_ADAPTER_DESC d;
    static_cast<IDXGIAdapter *>(this)->GetDesc(&d);
    *pDesc = {};
    memcpy(pDesc->Description, d.Description, sizeof(d.Description));
    pDesc->VendorId = d.VendorId;
    pDesc->DeviceId = d.DeviceId;
    pDesc->DedicatedVideoMemory = d.DedicatedVideoMemory;
    pDesc->SharedSystemMemory = d.SharedSystemMemory;
    return S_OK;
}

HRESULT IDXGIAdapter::EnumOutputs(UINT output_idx, IDXGIOutput **output)
{
    if (!output)
        return E_INVALIDARG;
    int n = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&n);
    if (!ids || output_idx >= UINT(n))
    {
        SDL_free(ids);
        return DXGI_ERROR_NOT_FOUND;
    }
    auto *o = new Output();
    o->display = ids[output_idx];
    SDL_free(ids);
    *output = o;
    return S_OK;
}

HRESULT IDXGIOutput::GetDesc(DXGI_OUTPUT_DESC *desc)
{
    if (!desc)
        return E_INVALIDARG;
    auto *o = static_cast<Output *>(static_cast<IDXGIOutput6 *>(this));
    *desc = {};
    to_wide(desc->DeviceName, 32, SDL_GetDisplayName(o->display));
    SDL_Rect r{};
    SDL_GetDisplayBounds(o->display, &r);
    desc->DesktopCoordinates = RECT{r.x, r.y, r.x + r.w, r.y + r.h};
    desc->AttachedToDesktop = TRUE;
    desc->Rotation = DXGI_MODE_ROTATION_IDENTITY;
    desc->Monitor = reinterpret_cast<HMONITOR>(uintptr_t(o->display)); // the encoding win32_window uses
    return S_OK;
}

HRESULT IDXGIOutput::GetDisplayModeList(DXGI_FORMAT format, UINT, UINT *mode_count, DXGI_MODE_DESC *desc)
{
    if (!mode_count)
        return E_INVALIDARG;
    auto *o = static_cast<Output *>(static_cast<IDXGIOutput6 *>(this));
    int n = 0;
    SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(o->display, &n);
    if (!modes)
        n = 0;
    if (!desc)
    {
        *mode_count = UINT(n);
        SDL_free(modes);
        return S_OK;
    }
    const UINT k = *mode_count < UINT(n) ? *mode_count : UINT(n);
    for (UINT i = 0; i < k; ++i)
    {
        const SDL_DisplayMode *m = modes[i];
        desc[i] = {};
        desc[i].Width = UINT(m->w * (m->pixel_density > 0 ? m->pixel_density : 1.f));
        desc[i].Height = UINT(m->h * (m->pixel_density > 0 ? m->pixel_density : 1.f));
        desc[i].RefreshRate.Numerator = m->refresh_rate_numerator ? UINT(m->refresh_rate_numerator) : UINT(m->refresh_rate + 0.5f);
        desc[i].RefreshRate.Denominator = m->refresh_rate_numerator ? UINT(m->refresh_rate_denominator) : 1;
        desc[i].Format = format;
    }
    SDL_free(modes);
    *mode_count = k;
    return k < UINT(n) ? DXGI_ERROR_MORE_DATA : S_OK;
}

HRESULT IDXGISwapChain::GetBuffer(UINT buffer_idx, REFIID riid, void **surface)
{
    auto *sc = static_cast<SwapChain *>(static_cast<IDXGISwapChain4 *>(this));
    if (buffer_idx != 0 || !surface)
        return E_INVALIDARG; // flip model: only buffer 0 is accessible
    return sc->back_buffer->QueryInterface(riid, surface);
}

HRESULT IDXGISwapChain::Present(UINT sync_interval, UINT flags)
{
    auto *sc = static_cast<SwapChain *>(static_cast<IDXGISwapChain4 *>(this));
    // DXGI_PRESENT_TEST only asks whether presenting would be visible (the engine does it every frame);
    // nothing is presented. A minimised window answers "occluded" like DXGI.
    if (flags & DXGI_PRESENT_TEST)
        return (SDL_GetWindowFlags(sc->window) & SDL_WINDOW_MINIMIZED) ? DXGI_STATUS_OCCLUDED : S_OK;
    Core &c = *core();
    auto lk = impl(static_cast<Device *>(sc->device)->immediate)->guard(); // the frame's commands are the context's
    const VkPresentModeKHR want = pick_mode(c.phys, sc->surface, sync_interval);
    if (want != sc->mode || !sc->swapchain)
    {
        sc->mode = want;
        vkDeviceWaitIdle(c.device);
        sc->create_swapchain(sc->desc.Width, sc->desc.Height);
        if (!sc->swapchain)
        {
            c.submit(); // minimised: still execute the frame's work
            c.next_frame();
            return S_OK;
        }
    }
    uint32_t idx = 0;
    VkSemaphore acquire = sc->acquired[c.frame];
    VkResult r = vkAcquireNextImageKHR(c.device, sc->swapchain, UINT64_MAX, acquire, VK_NULL_HANDLE, &idx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR)
    {
        vkDeviceWaitIdle(c.device);
        sc->create_swapchain(sc->desc.Width, sc->desc.Height);
        r = vkAcquireNextImageKHR(c.device, sc->swapchain, UINT64_MAX, acquire, VK_NULL_HANDLE, &idx);
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        fail("vkAcquireNextImageKHR: %d", int(r));

    // end the engine's last rendering scope; the back buffer stays in GENERAL like every engine image
    VkCommandBuffer cb = impl(static_cast<Device *>(sc->device)->immediate)->outside_rendering();
    transition(cb, &sc->back_buffer->img, VK_IMAGE_LAYOUT_GENERAL);
    transfer_barrier(cb, true);
    Screenshot shot;
    shot.record(cb, sc);
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = sc->images[idx];
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cb, &dep);
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {int32_t(sc->back_buffer->desc.Width), int32_t(sc->back_buffer->desc.Height), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[1] = {int32_t(sc->extent.width), int32_t(sc->extent.height), 1};
    const bool same = sc->back_buffer->desc.Width == sc->extent.width && sc->back_buffer->desc.Height == sc->extent.height;
    vkCmdBlitImage(cb, sc->back_buffer->img.image, VK_IMAGE_LAYOUT_GENERAL, sc->images[idx],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, same ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
    b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    b.dstAccessMask = 0;
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier2(cb, &dep);

    c.submit(acquire, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, sc->present_ready[idx]);
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &sc->present_ready[idx];
    pi.swapchainCount = 1;
    pi.pSwapchains = &sc->swapchain;
    pi.pImageIndices = &idx;
    r = vkQueuePresentKHR(c.queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
    {
        vkDeviceWaitIdle(c.device);
        sc->create_swapchain(sc->desc.Width, sc->desc.Height);
    }
    else if (r != VK_SUCCESS)
        fail("vkQueuePresentKHR: %d", int(r));
    shot.write(sc);
    frame_stats(sc, impl(static_cast<Device *>(sc->device)->immediate));
    ++sc->presents;
    c.next_frame();
    return S_OK;
}

HRESULT IDXGISwapChain::ResizeBuffers(UINT buffer_count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
    auto *sc = static_cast<SwapChain *>(static_cast<IDXGISwapChain4 *>(this));
    if (buffer_count)
        sc->desc.BufferCount = buffer_count;
    if (format != DXGI_FORMAT_UNKNOWN)
        sc->desc.Format = format;
    sc->desc.Flags = flags;
    if (!width || !height)
        window_pixels(sc->window, width, height);
    sc->desc.Width = width;
    sc->desc.Height = height;
    core()->flush_and_wait();
    sc->create_swapchain(width, height);
    sc->create_back_buffer();
    return S_OK;
}

HRESULT IDXGISwapChain::ResizeTarget(const DXGI_MODE_DESC *target_mode_desc)
{
    auto *sc = static_cast<SwapChain *>(static_cast<IDXGISwapChain4 *>(this));
    if (!target_mode_desc)
        return E_INVALIDARG;
    if (target_mode_desc->Width && target_mode_desc->Height)
        SDL_SetWindowSize(sc->window, int(target_mode_desc->Width), int(target_mode_desc->Height));
    return S_OK;
}

HRESULT IDXGISwapChain::SetFullscreenState(WINBOOL fullscreen, IDXGIOutput *)
{
    auto *sc = static_cast<SwapChain *>(static_cast<IDXGISwapChain4 *>(this));
    return SDL_SetWindowFullscreen(sc->window, fullscreen != FALSE) ? S_OK : DXGI_ERROR_UNSUPPORTED;
}

HRESULT IDXGISwapChain::GetDesc(DXGI_SWAP_CHAIN_DESC *desc)
{
    auto *sc = static_cast<SwapChain *>(static_cast<IDXGISwapChain4 *>(this));
    if (!desc)
        return E_INVALIDARG;
    *desc = {};
    desc->BufferDesc.Width = sc->desc.Width;
    desc->BufferDesc.Height = sc->desc.Height;
    desc->BufferDesc.Format = sc->desc.Format;
    desc->SampleDesc = sc->desc.SampleDesc;
    desc->BufferUsage = sc->desc.BufferUsage;
    desc->BufferCount = sc->desc.BufferCount;
    desc->OutputWindow = static_cast<HWND>(sc->window);
    desc->Windowed = !(SDL_GetWindowFlags(sc->window) & SDL_WINDOW_FULLSCREEN);
    desc->SwapEffect = sc->desc.SwapEffect;
    desc->Flags = sc->desc.Flags;
    return S_OK;
}

HRESULT IDXGISwapChain1::GetDesc1(DXGI_SWAP_CHAIN_DESC1 *pDesc)
{
    if (!pDesc)
        return E_INVALIDARG;
    *pDesc = static_cast<SwapChain *>(static_cast<IDXGISwapChain4 *>(this))->desc;
    return S_OK;
}

HRESULT IDXGISwapChain3::CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE colour_space, UINT *colour_space_support)
{
    if (!colour_space_support)
        return E_INVALIDARG;
    // only SDR sRGB output so far (HDR10 needs an HDR10 surface colour space; not implemented)
    *colour_space_support = colour_space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 ? DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT : 0;
    return S_OK;
}

HRESULT IDXGISwapChain3::SetColorSpace1(DXGI_COLOR_SPACE_TYPE colour_space)
{
    return colour_space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 ? S_OK : E_INVALIDARG;
}

// ---- the rest of DXGI the engine and imgui touch -------------------------------------------------------

// Parents as in DXGI: device -> adapter -> factory; other objects have none here.
HRESULT IDXGIObject::GetParent(REFIID riid, void **parent)
{
    if (!parent)
        return E_INVALIDARG;
    *parent = nullptr;
    if (has_interface(__uuidof(IDXGIDevice))) // a device's parent: the adapter of its GPU
    {
        auto *a = new Adapter();
        a->phys = core() ? core()->phys : preferred_physical_device();
        const HRESULT hr = a->QueryInterface(riid, parent);
        a->Release();
        return hr;
    }
    if (!has_interface(__uuidof(IDXGIAdapter)))
        return E_NOINTERFACE;
    return CreateDXGIFactory2(0, riid, parent);
}

// The legacy swap-chain call (imgui's multi-viewport windows): same as CreateSwapChainForHwnd.
HRESULT IDXGIFactory::CreateSwapChain(IUnknown *device, DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **swapchain)
{
    if (swapchain)
        *swapchain = nullptr; // D3D11 clears the output, so a failed call never hands out garbage
    if (!desc || !swapchain)
        return E_INVALIDARG;
    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = desc->BufferDesc.Width;
    d.Height = desc->BufferDesc.Height;
    d.Format = desc->BufferDesc.Format;
    d.SampleDesc = desc->SampleDesc;
    d.BufferUsage = desc->BufferUsage;
    d.BufferCount = desc->BufferCount;
    d.SwapEffect = desc->SwapEffect;
    d.Flags = desc->Flags;
    IDXGISwapChain1 *sc = nullptr;
    const HRESULT hr = static_cast<Factory *>(this)->CreateSwapChainForHwnd(device, desc->OutputWindow, &d, nullptr, nullptr, &sc);
    *swapchain = sc;
    return hr;
}

// The display the window is on.
HRESULT IDXGISwapChain::GetContainingOutput(IDXGIOutput **output)
{
    if (!output)
        return E_INVALIDARG;
    auto *sc = static_cast<SwapChain *>(static_cast<IDXGISwapChain4 *>(this));
    const SDL_DisplayID id = SDL_GetDisplayForWindow(sc->window);
    if (!id)
        return DXGI_ERROR_NOT_FOUND;
    auto *o = new Output();
    o->display = id;
    *output = o;
    return S_OK;
}

// Hardware gamma ramps: DXGI offers them only to a full-screen exclusive swap chain and answers
// DXGI_ERROR_NOT_CURRENTLY_AVAILABLE otherwise, which the engine (CGammaControl) handles by skipping the
// ramp. The Vulkan swap chain is never exclusive, so that is the answer here too. (Applying the engine's
// gamma/brightness/contrast in the final blit is a later feature.)
HRESULT IDXGIOutput::GetGammaControlCapabilities(DXGI_GAMMA_CONTROL_CAPABILITIES *)
{
    return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
}
HRESULT IDXGIOutput::SetGammaControl(const DXGI_GAMMA_CONTROL *)
{
    return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
}
