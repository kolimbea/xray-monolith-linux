// The concrete objects behind the D3D11/DXGI-named interfaces (ADR 0001). Each derives from the most
// derived interface it stands for; the non-virtual interface methods static_cast `this` to it.
#pragma once
#include "../../xrCore/xrCore_platform.h"
#include "vkCommonTypes.h"

#include <initializer_list>
#include <memory>
#include <mutex>
#include <vector>

#include "vkCore.h"
#include "vkHeap.h"
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

    // The Vulkan side of a D3D11 texture of any dimension: a device image, or for D3D11_USAGE_STAGING a
    // host buffer with the subresources packed in D3D11 order (subresource = mip + layer * mips).
    struct Image
    {
        D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
        DXGI_FORMAT dxgi = DXGI_FORMAT_UNKNOWN;
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint32_t width = 1, height = 1, depth = 1, mips = 1, layers = 1, samples = 1;
        D3D11_USAGE usage = D3D11_USAGE_DEFAULT;
        UINT bind = 0, misc = 0;
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = nullptr;
        // Whole-image layout. Engine images go to GENERAL on first use and stay there; hazards between
        // writes and reads are handled with barriers by the context (vkContext.cpp).
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkBuffer staging = VK_NULL_HANDLE;
        uint8_t *mapped = nullptr;
        std::vector<VkDeviceSize> sub_offset; // packing of staging memory / upload sources
        std::vector<uint32_t> sub_pitch, sub_rows;
        Slice dynamic;                     // DYNAMIC textures: data of the Map in progress
        uint32_t dynamic_sub = UINT32_MAX; // its subresource
        uint64_t written = 0, read = 0;    // context hazard epochs (vkContext.cpp)

        uint32_t subresources() const
        {
            return mips * layers;
        }
        uint32_t mip_width(uint32_t m) const
        {
            return (width >> m) ? (width >> m) : 1;
        }
        uint32_t mip_height(uint32_t m) const
        {
            return (height >> m) ? (height >> m) : 1;
        }
        uint32_t mip_depth(uint32_t m) const
        {
            return (depth >> m) ? (depth >> m) : 1;
        }
        VkImageAspectFlags copy_aspect() const; // depth for depth-stencil formats (D3D copies depth+stencil)
        ~Image();
    };

    struct Texture1D final : public ID3D11Texture1D
    {
        D3D11_TEXTURE1D_DESC desc{};
        Image img;
        bool has_interface(REFIID riid) const override;
    };
    struct Texture2D final : public ID3D11Texture2D
    {
        D3D11_TEXTURE2D_DESC desc{};
        Image img;
        bool has_interface(REFIID riid) const override;
    };
    struct Texture3D final : public ID3D11Texture3D
    {
        D3D11_TEXTURE3D_DESC desc{};
        Image img;
        bool has_interface(REFIID riid) const override;
    };
    // The image of a texture resource; null for buffers.
    Image *image_of(ID3D11Resource *r);

    struct Buffer final : public ID3D11Buffer
    {
        D3D11_BUFFER_DESC desc{};
        // DEFAULT/IMMUTABLE: device-local; STAGING: host memory (mapped); DYNAMIC: the current version is
        // a slice of the host heap, replaced on every Map(WRITE_DISCARD)
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation alloc = nullptr;
        uint8_t *mapped = nullptr; // STAGING
        Slice slice;               // DYNAMIC
        uint64_t version = 0;      // bumped when `slice` changes (contexts re-bind)

        bool dynamic() const
        {
            return desc.Usage == D3D11_USAGE_DYNAMIC;
        }
        VkBuffer vk() const
        {
            return dynamic() ? slice.buffer : buffer;
        }
        VkDeviceSize base() const
        {
            return dynamic() ? slice.offset : 0;
        }
        ~Buffer() override;
        bool has_interface(REFIID riid) const override;
    };
    Buffer *create_buffer(ID3D11Device *dev, const D3D11_BUFFER_DESC &desc, const D3D11_SUBRESOURCE_DATA *init);
    // A coarse barrier around transfers recorded in the frame command buffer (all prior work -> transfer,
    // or transfer -> all later work).
    void transfer_barrier(VkCommandBuffer cb, bool before);

    // What every view keeps: the Vulkan image view (or buffer range) and the image it shows.
    struct ViewData
    {
        Image *img = nullptr; // null for buffer views
        VkImageView view = VK_NULL_HANDLE;
        VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
        VkImageSubresourceRange range{};
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent2D extent{}; // of the viewed mip (attachments)
        // buffer views
        Buffer *buffer = nullptr;
        VkBufferView texel = VK_NULL_HANDLE; // typed buffer views
        VkDeviceSize offset = 0, size = 0;
        ~ViewData();
    };

    struct RenderTargetView final : public ID3D11RenderTargetView
    {
        D3D11_RENDER_TARGET_VIEW_DESC desc{};
        ViewData v;
        ~RenderTargetView() override;
        bool has_interface(REFIID riid) const override;
    };

    struct DepthStencilView final : public ID3D11DepthStencilView
    {
        D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
        ViewData v;
        ~DepthStencilView() override;
        bool has_interface(REFIID riid) const override;
    };

    struct ShaderResourceView final : public ID3D11ShaderResourceView
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
        ViewData v;
        ~ShaderResourceView() override;
        bool has_interface(REFIID riid) const override;
    };

    struct UnorderedAccessView final : public ID3D11UnorderedAccessView
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
        ViewData v;
        ~UnorderedAccessView() override;
        bool has_interface(REFIID riid) const override;
    };

    // ---- states (vkStates.cpp): the D3D11 description and its Vulkan dynamic-state form ----------------
    struct BlendState final : public ID3D11BlendState
    {
        D3D11_BLEND_DESC desc{};
        VkBool32 enable[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        VkColorBlendEquationEXT equation[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        VkColorComponentFlags write_mask[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        bool has_interface(REFIID riid) const override;
    };
    struct DepthStencilState final : public ID3D11DepthStencilState
    {
        D3D11_DEPTH_STENCIL_DESC desc{};
        bool has_interface(REFIID riid) const override;
    };
    struct RasterizerState final : public ID3D11RasterizerState
    {
        D3D11_RASTERIZER_DESC desc{};
        bool has_interface(REFIID riid) const override;
    };
    struct SamplerState final : public ID3D11SamplerState
    {
        D3D11_SAMPLER_DESC desc{};
        VkSampler sampler = VK_NULL_HANDLE;
        ~SamplerState() override;
        bool has_interface(REFIID riid) const override;
    };
    // Vertex attributes of an input layout, resolved against the vertex shader signature it was created
    // with (D3D11 matches by semantic, Vulkan by location).
    struct InputLayout final : public ID3D11InputLayout
    {
        struct Attribute
        {
            uint32_t location, slot, offset;
            VkFormat format;
            bool per_instance;
            uint32_t step_rate;
        };
        std::vector<Attribute> attributes;
        bool has_interface(REFIID riid) const override;
    };
    // D3D11_QUERY_EVENT and D3D11_QUERY_OCCLUSION (vkQuery.cpp). An occlusion query spans rendering
    // scopes: each scope it is active in uses one slot of its pool, the result is their sum.
    struct Query final : public ID3D11Query
    {
        static constexpr uint32_t kSlots = 16;
        D3D11_QUERY_DESC desc{};
        VkQueryPool pool = VK_NULL_HANDLE;
        uint32_t used = 0;       // slots written since Begin
        bool active = false;     // between Begin and End
        bool slot_open = false;  // vkCmdBeginQuery recorded in the current rendering scope
        uint64_t end_value = 0;  // timeline value of the frame that recorded End (0: never ended)
        ~Query() override;
        bool has_interface(REFIID riid) const override;
    };

    VkCompareOp compare_op(D3D11_COMPARISON_FUNC f);
    VkStencilOp stencil_op(D3D11_STENCIL_OP op);

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
        std::vector<const ShaderResource *> resources; // the reflection of each binding (owned by `modules`)
        ~Program();
    };
    struct ProgramCache;

    // IDXGIDevice of a D3D11 device (a separate object here); its parent is the adapter.
    struct DXGIDevice final : public IDXGIDevice
    {
        bool has_interface(REFIID riid) const override;
    };

    struct Device final : public ID3D11Device2
    {
        ID3D11DeviceContext *immediate = nullptr;
        std::unique_ptr<ProgramCache> programs;
        DXGIDevice *dxgi = nullptr;
        bool has_interface(REFIID riid) const override;
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
        Device();
        ~Device() override;
        // The program of these stages (null where a stage is unbound; compute alone or graphics without
        // compute), created on first use; null with a logged reason if Vulkan rejects it.
        const Program *program(const std::shared_ptr<const ShaderModule> (&stages)[size_t(Stage::Count)]);
    };

    // Resources bound to one shader stage (D3D11 keeps a reference to every bound object).
    struct StageState
    {
        ID3D11DeviceChild *shader = nullptr;
        std::shared_ptr<const ShaderModule> module;
        Buffer *cb[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
        ShaderResourceView *srv[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        SamplerState *sampler[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    };

    // Placeholders for unbound slots (D3D11 reads zeros from them): one zero image per view type, a
    // zero buffer and a default sampler.
    struct NullResources
    {
        ID3D11Resource *res[5] = {}; // 1D, 2D, cube, 3D, 2D multisampled
        VkImageView views[7] = {};   // indexed by VkImageViewType
        VkImageView ms_view = VK_NULL_HANDLE;
        Buffer *buffer = nullptr; // 64 KB of zeros
        VkSampler sampler = VK_NULL_HANDLE;
    };

    struct Context final : public ID3D11DeviceContext2
    {
        // D3D11 serialises the immediate context (multithread protection; dxvk does the same): the
        // engine calls it from worker threads, e.g. texture loads in CResourceManager::DeferredUpload
        // (tbb::parallel_for_each -> UpdateSubresource / Map). Every context method holds this lock.
        std::recursive_mutex lock;
        // draws after which the command buffer is submitted at the next rendering scope (begin_rendering)
        static constexpr uint32_t kPartDraws = 512;
        std::unique_lock<std::recursive_mutex> guard()
        {
            return std::unique_lock<std::recursive_mutex>(lock);
        }
        // ---- D3D11 state ----
        InputLayout *layout = nullptr;
        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        Buffer *vb[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
        UINT vb_stride[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {}, vb_offset[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
        Buffer *ib = nullptr;
        DXGI_FORMAT ib_format = DXGI_FORMAT_R16_UINT;
        UINT ib_offset = 0;
        StageState stage[size_t(Stage::Count)];
        UnorderedAccessView *cs_uav[D3D11_1_UAV_SLOT_COUNT] = {};
        BlendState *blend = nullptr;
        float blend_factor[4] = {1, 1, 1, 1};
        UINT sample_mask = 0xffffffff;
        DepthStencilState *depth_stencil = nullptr;
        UINT stencil_ref = 0;
        RasterizerState *rasterizer = nullptr;
        D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
        UINT num_viewports = 0;
        D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
        UINT num_scissors = 0;
        RenderTargetView *rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        DepthStencilView *dsv = nullptr;
        struct Annotation *annotation = nullptr; // ID3DUserDefinedAnnotation of this context

        // ---- recording state (vkContext.cpp) ----
        bool rendering = false;        // inside vkCmdBeginRendering (begun lazily by draws)
        bool targets_changed = true;   // OMSetRenderTargets since the current rendering began
        VkCommandBuffer state_cb = VK_NULL_HANDLE; // command buffer the dynamic state below was set in
        uint32_t dirty = ~0u;          // which dynamic state groups need setting (vkContext.cpp)
        const Program *bound_program = nullptr;
        VkBuffer bound_vb[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
        VkDeviceSize bound_vb_offset[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
        VkBuffer bound_ib = VK_NULL_HANDLE;
        VkDeviceSize bound_ib_offset = 0;
        VkIndexType bound_ib_type = VK_INDEX_TYPE_UINT16;
        uint32_t render_samples = 1; // of the current rendering's attachments
        VkExtent2D render_area{1, 1};
        uint32_t color_count = 0;
        RenderTargetView *current_rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {}; // attachments of the
        DepthStencilView *current_dsv = nullptr; // current rendering (no references: the bindings hold them)
        const Program *bound_compute = nullptr;
        std::vector<VkWriteDescriptorSet> writes; // push_descriptors scratch
        std::vector<VkDescriptorImageInfo> images;
        std::vector<VkDescriptorBufferInfo> buffers;
        // the graphics descriptors last pushed (in state_cb): an identical push is skipped
        const Program *pushed_program = nullptr;
        std::vector<VkDescriptorImageInfo> pushed_images;
        std::vector<VkDescriptorBufferInfo> pushed_buffers;
        NullResources null;

        std::vector<Query *> active_queries; // occlusion queries between Begin and End (no references)

        // per-frame statistics (logged by Present every kStatFrames frames)
        struct Stats
        {
            uint64_t draws = 0, renderings = 0, program_binds = 0, dispatches = 0, transfers = 0;
            double prepare_ms = 0; // CPU time in prepare_draw (state, descriptors, vertex input)
        } stats;

        bool is_attachment(const Image *img) const;
        void begin_rendering();
        void end_rendering(VkCommandBuffer cb); // closes open query slots, then vkCmdEndRendering
        void begin_query_slot(VkCommandBuffer cb, Query *q);
        const Program *bind_program(VkCommandBuffer cb, bool compute);
        bool push_descriptors(VkCommandBuffer cb, const Program *p, VkPipelineBindPoint bind_point);
        void bind_vertex_input(VkCommandBuffer cb);
        void set_dynamic_state(VkCommandBuffer cb);
        bool prepare_draw();
        bool prepare_draw_impl();

        // The frame command buffer with any dynamic-rendering scope ended (for copies, clears, barriers).
        VkCommandBuffer outside_rendering();
        void init_null_resources();
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
        uint64_t presents = 0;          // frames presented (XR_VK_SCREENSHOT)

        ~SwapChain() override;
        bool has_interface(REFIID riid) const override;
        void create_swapchain(uint32_t width, uint32_t height);
        void destroy_swapchain();
        void create_back_buffer();
    };

    // Records a layout transition of the whole image (conservative ALL_COMMANDS barrier).
    void transition(VkCommandBuffer cb, Image *img, VkImageLayout to);
    Texture1D *create_texture1d(Device *dev, const D3D11_TEXTURE1D_DESC &desc, const D3D11_SUBRESOURCE_DATA *init);
    Texture2D *create_texture2d(Device *dev, const D3D11_TEXTURE2D_DESC &desc, const D3D11_SUBRESOURCE_DATA *init);
    Texture3D *create_texture3d(Device *dev, const D3D11_TEXTURE3D_DESC &desc, const D3D11_SUBRESOURCE_DATA *init);

    HRESULT map_texture(Context *ctx, Image *img, UINT sub, D3D11_MAP type, D3D11_MAPPED_SUBRESOURCE *out);
    void unmap_texture(Context *ctx, Image *img, UINT sub);
    void update_texture(Context *ctx, Image *img, UINT sub, const D3D11_BOX *box, const void *data, UINT row_pitch, UINT depth_pitch);
    HRESULT map_buffer(Context *ctx, Buffer *b, D3D11_MAP type, D3D11_MAPPED_SUBRESOURCE *out);
    void update_buffer(Context *ctx, Buffer *b, const D3D11_BOX *box, const void *data);

    inline Device *impl(ID3D11Device *p)
    {
        return static_cast<Device *>(p);
    }
    inline Context *impl(ID3D11DeviceContext *p)
    {
        return static_cast<Context *>(p);
    }
} // namespace xrvk
