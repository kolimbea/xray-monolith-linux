// The concrete objects behind the D3D11/DXGI-named interfaces (ADR 0001). Each derives from the most
// derived interface it stands for; the non-virtual interface methods static_cast `this` to it.
#pragma once
#include "../../xrCore/xrCore_platform.h"
#include "vkCommonTypes.h"

#include <initializer_list>
#include <memory>
#include <vector>

#include "vkCore.h"
#include "vkShader.h"

struct SDL_Window;

namespace xrvk
{
    bool iid_in(REFIID riid, std::initializer_list<GUID> ids);

    struct Factory final : public IDXGIFactory6
    {
        bool has_interface(REFIID riid) const override;
    };

    struct Adapter final : public IDXGIAdapter4
    {
        VkPhysicalDevice phys = VK_NULL_HANDLE;
        bool has_interface(REFIID riid) const override;
    };

    struct Output final : public IDXGIOutput6
    {
        uint32_t display = 0; // SDL_DisplayID
        bool has_interface(REFIID riid) const override;
    };

    struct Texture2D final : public ID3D11Texture2D
    {
        D3D11_TEXTURE2D_DESC desc{};
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImage image = VK_NULL_HANDLE; // GPU textures
        VmaAllocation alloc = nullptr;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED; // whole-image layout tracking (all subresources alike)
        // D3D11_USAGE_STAGING: a host-visible buffer, subresources tightly packed in D3D11 order
        VkBuffer staging = VK_NULL_HANDLE;
        void *mapped = nullptr;
        std::vector<VkDeviceSize> sub_offset;
        std::vector<uint32_t> sub_pitch, sub_rows;

        ~Texture2D() override;
        bool has_interface(REFIID riid) const override;
        uint32_t subresources() const
        {
            return desc.MipLevels * desc.ArraySize;
        }
    };

    struct RenderTargetView final : public ID3D11RenderTargetView
    {
        D3D11_RENDER_TARGET_VIEW_DESC desc{};
        VkImageView view = VK_NULL_HANDLE;
        VkImageSubresourceRange range{};
        ~RenderTargetView() override;
        bool has_interface(REFIID riid) const override;
    };

    struct DepthStencilView final : public ID3D11DepthStencilView
    {
        D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
        VkImageView view = VK_NULL_HANDLE;
        VkImageSubresourceRange range{};
        ~DepthStencilView() override;
        bool has_interface(REFIID riid) const override;
    };

    // A compiled shader: its reflection and the module for vkCreateShadersEXT. Shared by the D3D shader
    // object and every program that uses it.
    struct ShaderModule
    {
        ShaderInfo info;
        std::vector<uint32_t> code; // SPIR-V without the reflection-only decorations
        uint64_t id = 0;            // unique per module; program cache key
    };

    template <class I> struct Shader final : public I
    {
        std::shared_ptr<const ShaderModule> module;
        bool has_interface(REFIID r) const override
        {
            return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(I)});
        }
    };
    using VertexShader = Shader<ID3D11VertexShader>;
    using HullShader = Shader<ID3D11HullShader>;
    using DomainShader = Shader<ID3D11DomainShader>;
    using GeometryShader = Shader<ID3D11GeometryShader>;
    using PixelShader = Shader<ID3D11PixelShader>;
    using ComputeShader = Shader<ID3D11ComputeShader>;

    // The shader stages bound for a draw (or one compute shader) as linked shader objects with one
    // push-descriptor set layout: the union of the stages' bindings (see vkShader.h).
    struct Program
    {
        std::shared_ptr<const ShaderModule> modules[size_t(Stage::Count)];
        VkShaderEXT shaders[size_t(Stage::Count)] = {};
        VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        ~Program();
    };
    struct ProgramCache;

    struct Device final : public ID3D11Device2
    {
        ID3D11DeviceContext *immediate = nullptr;
        std::unique_ptr<ProgramCache> programs;
        bool has_interface(REFIID riid) const override;
        Device();
        ~Device() override;
        // The program of these stages (null where a stage is unbound; compute alone or graphics without
        // compute), created on first use; null with a logged reason if Vulkan rejects it.
        const Program *program(const std::shared_ptr<const ShaderModule> (&stages)[size_t(Stage::Count)]);
    };

    struct Context final : public ID3D11DeviceContext2
    {
        ID3D11RenderTargetView *rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        ID3D11DepthStencilView *dsv = nullptr;
        struct Annotation *annotation = nullptr; // ID3DUserDefinedAnnotation of this context
        bool has_interface(REFIID riid) const override;
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
        ~Context() override;
    };

    // ID3DUserDefinedAnnotation of the immediate context (a separate object here).
    struct Annotation final : public ID3DUserDefinedAnnotation
    {
        bool has_interface(REFIID riid) const override;
    };

    struct SwapChain final : public IDXGISwapChain4
    {
        SDL_Window *window = nullptr;
        DXGI_SWAP_CHAIN_DESC1 desc{};
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkFormat swap_format = VK_FORMAT_UNDEFINED;
        VkExtent2D extent{};
        std::vector<VkImage> images;
        std::vector<VkSemaphore> present_ready; // one per swapchain image
        VkSemaphore acquired[kFramesInFlight] = {};
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        Texture2D *back_buffer = nullptr; // buffer 0 as the engine sees it; blitted on Present
        ID3D11Device *device = nullptr; // holds a reference

        ~SwapChain() override;
        bool has_interface(REFIID riid) const override;
        void create_swapchain(uint32_t width, uint32_t height);
        void destroy_swapchain();
        void create_back_buffer();
    };

    // Records a layout transition of the whole image (conservative ALL_COMMANDS barrier).
    void transition(VkCommandBuffer cb, Texture2D *t, VkImageLayout to);
    Texture2D *create_texture2d(Device *dev, const D3D11_TEXTURE2D_DESC &desc, const D3D11_SUBRESOURCE_DATA *init);

    inline Device *impl(ID3D11Device *p)
    {
        return static_cast<Device *>(p);
    }
    inline Context *impl(ID3D11DeviceContext *p)
    {
        return static_cast<Context *>(p);
    }
} // namespace xrvk
