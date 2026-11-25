// The immediate context of the Vulkan backend: D3D11 state, draws and dispatches on shader objects with
// dynamic rendering and fully dynamic state, clears, copies.
//
// Synchronisation is deliberately coarse for now (performance track): a full memory barrier before every
// rendering scope begins (render targets changed), around transfers and around dispatches. Inside one
// rendering scope draws only write the attachments; an SRV of a current attachment is read as the null
// resource, which is what D3D11 does (binding a resource as output unbinds it as input).
#include "vkObjects.h"

#include <algorithm>
#include <cstring>

#include "vkFormat.h"

namespace xrvk
{
    namespace
    {
        enum Dirty : uint32_t
        {
            kViewport = 1,
            kRaster = 2,
            kDepth = 4,
            kBlend = 8,
            kTopology = 16,
            kVertexInput = 32,
            kSamples = 64,
            kAll = ~0u
        };

        template <class T>
        void set_ref(T *&slot, T *v)
        {
            if (slot == v)
                return;
            if (v)
                v->AddRef();
            if (slot)
                slot->Release();
            slot = v;
        }

        // the backend's object behind an interface pointer (single inheritance: same address)
        template <class Impl, class I>
        Impl *impl_of(I *p)
        {
            return static_cast<Impl *>(p);
        }

        bool is_strip(D3D11_PRIMITIVE_TOPOLOGY t)
        {
            return t == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP || t == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP ||
                   t == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ || t == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
        }

        VkPrimitiveTopology vk_topology(D3D11_PRIMITIVE_TOPOLOGY t)
        {
            switch (t)
            {
            case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST:
                return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            case D3D11_PRIMITIVE_TOPOLOGY_LINELIST:
                return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP:
                return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
                return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            case D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ:
                return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
            case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ:
                return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
            case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ:
                return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
            case D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ:
                return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
            default:
                if (t >= D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST && t <= D3D11_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST)
                    return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
                return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            }
        }

        VkImageViewType null_view_type(D3D_SRV_DIMENSION d)
        {
            switch (d)
            {
            case D3D_SRV_DIMENSION_TEXTURE1D:
                return VK_IMAGE_VIEW_TYPE_1D;
            case D3D_SRV_DIMENSION_TEXTURE1DARRAY:
                return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
            case D3D_SRV_DIMENSION_TEXTURE2DARRAY:
            case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY:
                return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            case D3D_SRV_DIMENSION_TEXTURE3D:
                return VK_IMAGE_VIEW_TYPE_3D;
            case D3D_SRV_DIMENSION_TEXTURECUBE:
                return VK_IMAGE_VIEW_TYPE_CUBE;
            case D3D_SRV_DIMENSION_TEXTURECUBEARRAY:
                return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
            default:
                return VK_IMAGE_VIEW_TYPE_2D;
            }
        }

        void full_barrier(VkCommandBuffer cb)
        {
            VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
            mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            mb.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
            mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            mb.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            d.memoryBarrierCount = 1;
            d.pMemoryBarriers = &mb;
            vkCmdPipelineBarrier2(cb, &d);
        }

        // logs a message the first time a given key is seen (per process)
        void log_once(const void *key, const char *fmt, const char *arg)
        {
            static std::vector<const void *> seen;
            if (std::find(seen.begin(), seen.end(), key) != seen.end())
                return;
            seen.push_back(key);
            log(fmt, arg);
        }

        const D3D11_RASTERIZER_DESC kDefaultRaster = {D3D11_FILL_SOLID, D3D11_CULL_BACK, FALSE, 0, 0.f, 0.f, TRUE, FALSE, FALSE, FALSE};
        const D3D11_DEPTH_STENCIL_DESC kDefaultDepth = {TRUE,
                                                        D3D11_DEPTH_WRITE_MASK_ALL,
                                                        D3D11_COMPARISON_LESS,
                                                        FALSE,
                                                        0xff,
                                                        0xff,
                                                        {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS},
                                                        {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS}};
    } // namespace

    // ---- placeholders for unbound slots ------------------------------------------------------------------

    void Context::init_null_resources()
    {
        Core *c = core();
        c->before_submit = [this](VkCommandBuffer cb) {
            end_rendering(cb);
            state_cb = VK_NULL_HANDLE; // the next command buffer starts without any state
        };
        auto *dev = static_cast<Device *>(xrvk_device);
        D3D11_SUBRESOURCE_DATA zero[6];
        static const uint32_t zeros[16] = {};
        for (auto &z : zero)
            z = {zeros, 4, 4};
        // 1D, 2D (2D and 2D array views), cube (cube and cube array views), 3D
        D3D11_TEXTURE1D_DESC d1{1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT,
                                D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0};
        D3D11_TEXTURE2D_DESC d2{1, 1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0};
        D3D11_TEXTURE2D_DESC dc = d2;
        dc.ArraySize = 6;
        dc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        dc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        D3D11_TEXTURE3D_DESC d3{1, 1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT,
                                D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0};
        D3D11_TEXTURE2D_DESC dm = d2;
        dm.SampleDesc.Count = 4;
        dm.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        Texture1D *t1 = create_texture1d(dev, d1, zero);
        Texture2D *t2 = create_texture2d(dev, d2, zero);
        Texture2D *tc = create_texture2d(dev, dc, zero);
        Texture3D *t3 = create_texture3d(dev, d3, zero);
        Texture2D *tm = create_texture2d(dev, dm, nullptr);
        if (!t1 || !t2 || !tc || !t3 || !tm)
            fail("vk: creating the null resources failed");
        null.res[0] = t1;
        null.res[1] = t2;
        null.res[2] = tc;
        null.res[3] = t3;
        null.res[4] = tm;
        auto view = [c](Image &img, VkImageViewType type, uint32_t layers)
        {
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image = img.image;
            vi.viewType = type;
            vi.format = img.format;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
            VkImageView v;
            XRVK_CHECK(vkCreateImageView(c->device, &vi, nullptr, &v));
            return v;
        };
        null.views[VK_IMAGE_VIEW_TYPE_1D] = view(t1->img, VK_IMAGE_VIEW_TYPE_1D, 1);
        null.views[VK_IMAGE_VIEW_TYPE_1D_ARRAY] = view(t1->img, VK_IMAGE_VIEW_TYPE_1D_ARRAY, 1);
        null.views[VK_IMAGE_VIEW_TYPE_2D] = view(t2->img, VK_IMAGE_VIEW_TYPE_2D, 1);
        null.views[VK_IMAGE_VIEW_TYPE_2D_ARRAY] = view(t2->img, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 1);
        null.views[VK_IMAGE_VIEW_TYPE_CUBE] = view(tc->img, VK_IMAGE_VIEW_TYPE_CUBE, 6);
        null.views[VK_IMAGE_VIEW_TYPE_CUBE_ARRAY] =
            c->features.imageCubeArray ? view(tc->img, VK_IMAGE_VIEW_TYPE_CUBE_ARRAY, 6) : VK_NULL_HANDLE;
        null.views[VK_IMAGE_VIEW_TYPE_3D] = view(t3->img, VK_IMAGE_VIEW_TYPE_3D, 1);
        null.ms_view = view(tm->img, VK_IMAGE_VIEW_TYPE_2D, 1);
        {
            // the multisampled one cannot be uploaded: clear it
            std::lock_guard<std::mutex> g(c->upload_lock);
            const VkClearColorValue cv{};
            const VkImageSubresourceRange r{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdClearColorImage(c->upload_cmd(), tm->img.image, VK_IMAGE_LAYOUT_GENERAL, &cv, 1, &r);
        }
        std::vector<uint8_t> zero_bytes(65536, 0);
        D3D11_BUFFER_DESC bd{65536, D3D11_USAGE_DEFAULT,
                             D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER | D3D11_BIND_CONSTANT_BUFFER | D3D11_BIND_SHADER_RESOURCE |
                                 D3D11_BIND_UNORDERED_ACCESS,
                             0, 0, 0};
        D3D11_SUBRESOURCE_DATA bz{zero_bytes.data(), 0, 0};
        null.buffer = create_buffer(dev, bd, &bz);
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.maxLod = VK_LOD_CLAMP_NONE;
        XRVK_CHECK(vkCreateSampler(c->device, &si, nullptr, &null.sampler));
    }

    Context::~Context()
    {
        if (annotation)
            annotation->Release();
        ClearState();
        Core *c = core();
        c->before_submit = nullptr;
        for (VkImageView v : null.views)
            if (v)
                vkDestroyImageView(c->device, v, nullptr);
        if (null.ms_view)
            vkDestroyImageView(c->device, null.ms_view, nullptr);
        if (null.sampler)
            vkDestroySampler(c->device, null.sampler, nullptr);
        for (ID3D11Resource *r : null.res)
            if (r)
                r->Release();
        if (null.buffer)
            null.buffer->Release();
    }

    // ---- recording ----------------------------------------------------------------------------------------

    void Context::end_rendering(VkCommandBuffer cb)
    {
        if (!rendering)
            return;
        for (Query *q : active_queries)
            if (q->slot_open)
            {
                vkCmdEndQuery(cb, q->pool, q->used - 1);
                q->slot_open = false;
            }
        vkCmdEndRendering(cb);
        rendering = false;
    }

    VkCommandBuffer Context::outside_rendering()
    {
        VkCommandBuffer cb = core()->cmd();
        end_rendering(cb);
        return cb;
    }

    bool Context::is_attachment(const Image *img) const
    {
        if (!rendering || !img)
            return false;
        for (const RenderTargetView *r : current_rtv)
            if (r && r->v.img == img)
                return true;
        return current_dsv && current_dsv->v.img == img;
    }

    void Context::begin_rendering()
    {
        VkCommandBuffer cb = outside_rendering();
        full_barrier(cb);
        VkRenderingAttachmentInfo color[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
        VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}, stencil{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        VkExtent2D area{UINT32_MAX, UINT32_MAX};
        uint32_t layers = UINT32_MAX, count = 0;
        render_samples = 0;
        auto fit = [&](const ViewData &v)
        {
            area.width = std::min(area.width, v.extent.width);
            area.height = std::min(area.height, v.extent.height);
            layers = std::min(layers, v.range.layerCount);
            render_samples = v.img->samples;
        };
        for (uint32_t i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        {
            current_rtv[i] = rtv[i];
            color[i] = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            if (rtv[i])
            {
                transition(cb, rtv[i]->v.img, VK_IMAGE_LAYOUT_GENERAL);
                color[i].imageView = rtv[i]->v.view;
                color[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                color[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                color[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                fit(rtv[i]->v);
                count = i + 1;
            }
        }
        current_dsv = dsv;
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        if (dsv)
        {
            transition(cb, dsv->v.img, VK_IMAGE_LAYOUT_GENERAL);
            const VkImageAspectFlags a = dsv->v.range.aspectMask;
            for (VkRenderingAttachmentInfo *att : {&depth, &stencil})
            {
                att->imageView = dsv->v.view;
                att->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                att->loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                att->storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            }
            if (a & VK_IMAGE_ASPECT_DEPTH_BIT)
                ri.pDepthAttachment = &depth;
            if (a & VK_IMAGE_ASPECT_STENCIL_BIT)
                ri.pStencilAttachment = &stencil;
            fit(dsv->v);
        }
        if (area.width == UINT32_MAX) // no attachments: the viewport decides
        {
            const D3D11_VIEWPORT &vp = viewports[0];
            area = {uint32_t(std::max(1.f, vp.TopLeftX + vp.Width)), uint32_t(std::max(1.f, vp.TopLeftY + vp.Height))};
            layers = 1;
            render_samples = 1;
        }
        render_area = area;
        ri.renderArea = {{0, 0}, area};
        ri.layerCount = layers ? layers : 1;
        ri.colorAttachmentCount = count;
        ri.pColorAttachments = color;
        color_count = count;
        vkCmdBeginRendering(cb, &ri);
        rendering = true;
        for (Query *q : active_queries) // occlusion queries continue in a new slot
            begin_query_slot(cb, q);
        targets_changed = false;
        dirty |= kViewport | kSamples | kBlend;
    }

    // The program of the bound graphics stages, bound to the command buffer; null if unusable.
    const Program *Context::bind_program(VkCommandBuffer cb, bool compute)
    {
        Core *c = core();
        std::shared_ptr<const ShaderModule> set[size_t(Stage::Count)];
        if (compute)
            set[size_t(Stage::Compute)] = stage[size_t(Stage::Compute)].module;
        else
            for (Stage s : {Stage::Vertex, Stage::Hull, Stage::Domain, Stage::Geometry, Stage::Pixel})
                set[size_t(s)] = stage[size_t(s)].module;
        if (!set[size_t(compute ? Stage::Compute : Stage::Vertex)])
            return nullptr;
        const Program *p = static_cast<Device *>(xrvk_device)->program(set);
        if (!p)
            return nullptr;
        if (compute)
        {
            if (p != bound_compute)
            {
                const VkShaderStageFlagBits st = VK_SHADER_STAGE_COMPUTE_BIT;
                c->ext.bind_shaders(cb, 1, &st, &p->shaders[size_t(Stage::Compute)]);
                bound_compute = p;
            }
            return p;
        }
        if (p != bound_program)
        {
            VkShaderStageFlagBits stages[5];
            VkShaderEXT shaders[5];
            uint32_t n = 0;
            auto add = [&](VkShaderStageFlagBits st, Stage s)
            {
                stages[n] = st;
                shaders[n++] = p->shaders[size_t(s)];
            };
            add(VK_SHADER_STAGE_VERTEX_BIT, Stage::Vertex);
            if (c->features.tessellationShader)
            {
                add(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, Stage::Hull);
                add(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, Stage::Domain);
            }
            if (c->features.geometryShader)
                add(VK_SHADER_STAGE_GEOMETRY_BIT, Stage::Geometry);
            add(VK_SHADER_STAGE_FRAGMENT_BIT, Stage::Pixel);
            c->ext.bind_shaders(cb, n, stages, shaders);
            bound_program = p;
        }
        return p;
    }

    // Writes every binding of the program into its push-descriptor set; false if a binding cannot be
    // expressed yet (logged once).
    bool Context::push_descriptors(VkCommandBuffer cb, const Program *p, VkPipelineBindPoint bind_point)
    {
        Core *c = core();
        const size_t n = p->bindings.size();
        if (!n)
            return true;
        writes.resize(n);
        images.resize(n);
        buffers.resize(n);
        for (size_t i = 0; i < n; ++i)
        {
            const VkDescriptorSetLayoutBinding &b = p->bindings[i];
            const ShaderResource &r = *p->resources[i];
            Stage s;
            RegClass cls;
            uint32_t slot;
            decode_binding(b.binding, s, cls, slot);
            const StageState &st = stage[size_t(s)];
            VkWriteDescriptorSet &w = writes[i];
            w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstBinding = b.binding;
            w.descriptorCount = 1;
            w.descriptorType = b.descriptorType;
            if (b.descriptorCount != 1)
            {
                log_once(p, "! vk: resource arrays in shaders are not supported yet (%s)", r.name.c_str());
                return false;
            }
            switch (b.descriptorType)
            {
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
                const Buffer *buf = slot < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT ? st.cb[slot] : nullptr;
                if (!buf)
                    buf = null.buffer;
                VkDeviceSize range = buf->desc.ByteWidth;
                if (range > c->props.limits.maxUniformBufferRange)
                    range = c->props.limits.maxUniformBufferRange;
                buffers[i] = {buf->vk(), buf->base(), range};
                w.pBufferInfo = &buffers[i];
                break;
            }
            case VK_DESCRIPTOR_TYPE_SAMPLER: {
                const SamplerState *sm = slot < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT ? st.sampler[slot] : nullptr;
                images[i] = {sm ? sm->sampler : null.sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
                w.pImageInfo = &images[i];
                break;
            }
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: {
                const ShaderResourceView *v = st.srv[slot];
                VkImageView view = VK_NULL_HANDLE;
                if (v && v->v.view && !is_attachment(v->v.img))
                    view = v->v.view;
                else if (r.dimension == D3D_SRV_DIMENSION_TEXTURE2DMS)
                    view = null.ms_view;
                else
                    view = null.views[null_view_type(r.dimension)];
                images[i] = {VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
                w.pImageInfo = &images[i];
                break;
            }
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: {
                const UnorderedAccessView *u = cls == RegClass::UAV && s == Stage::Compute ? cs_uav[slot] : nullptr;
                images[i] = {VK_NULL_HANDLE, u && u->v.view ? u->v.view : null.views[null_view_type(r.dimension)], VK_IMAGE_LAYOUT_GENERAL};
                w.pImageInfo = &images[i];
                break;
            }
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: {
                const ViewData *v = nullptr;
                if (cls == RegClass::UAV)
                    v = s == Stage::Compute && cs_uav[slot] ? &cs_uav[slot]->v : nullptr;
                else if (st.srv[slot])
                    v = &st.srv[slot]->v;
                if (v && v->buffer)
                    buffers[i] = {v->buffer->vk(), v->buffer->base() + v->offset, v->size};
                else
                    buffers[i] = {null.buffer->vk(), 0, VK_WHOLE_SIZE};
                w.pBufferInfo = &buffers[i];
                break;
            }
            default:
                log_once(p, "! vk: typed buffer views in shaders are not supported yet (%s)", r.name.c_str());
                return false;
            }
        }
        c->ext.push_descriptor_set(cb, bind_point, p->layout, 0, uint32_t(n), writes.data());
        return true;
    }

    void Context::bind_vertex_input(VkCommandBuffer cb)
    {
        Core *c = core();
        if (dirty & kVertexInput)
        {
            std::vector<VkVertexInputBindingDescription2EXT> bindings;
            std::vector<VkVertexInputAttributeDescription2EXT> attrs;
            if (layout)
                for (const InputLayout::Attribute &a : layout->attributes)
                {
                    attrs.push_back({VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT, nullptr, a.location, a.slot, a.format, a.offset});
                    const bool known = std::any_of(bindings.begin(), bindings.end(), [&](const auto &b)
                                                   {
                                                       return b.binding == a.slot;
                                                   });
                    if (!known)
                        bindings.push_back({VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT, nullptr, a.slot, vb_stride[a.slot],
                                            a.per_instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX, 1});
                }
            c->ext.set_vertex_input(cb, uint32_t(bindings.size()), bindings.data(), uint32_t(attrs.size()), attrs.data());
        }
        // vertex buffers: re-bound when the D3D binding or a dynamic buffer's current slice changed
        if (layout)
            for (const InputLayout::Attribute &a : layout->attributes)
            {
                const Buffer *b = vb[a.slot] ? vb[a.slot] : null.buffer;
                const VkBuffer vkb = b->vk();
                const VkDeviceSize off = b->base() + (vb[a.slot] ? vb_offset[a.slot] : 0);
                if (bound_vb[a.slot] != vkb || bound_vb_offset[a.slot] != off)
                {
                    vkCmdBindVertexBuffers(cb, a.slot, 1, &vkb, &off);
                    bound_vb[a.slot] = vkb;
                    bound_vb_offset[a.slot] = off;
                }
            }
    }

    void Context::set_dynamic_state(VkCommandBuffer cb)
    {
        Core *c = core();
        const D3D11_RASTERIZER_DESC &rs = rasterizer ? rasterizer->desc : kDefaultRaster;
        const D3D11_DEPTH_STENCIL_DESC &ds = depth_stencil ? depth_stencil->desc : kDefaultDepth;
        if (dirty & (kViewport | kRaster))
        {
            // D3D11 viewports with y down: a negative height flips Vulkan's clip space to match
            const UINT n = num_viewports ? num_viewports : 1;
            VkViewport vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
            VkRect2D sc[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
            for (UINT i = 0; i < n; ++i)
            {
                const D3D11_VIEWPORT v = num_viewports ? viewports[i] : D3D11_VIEWPORT{0, 0, float(render_area.width), float(render_area.height), 0, 1};
                vp[i] = {v.TopLeftX, v.TopLeftY + v.Height, v.Width, -v.Height, v.MinDepth, v.MaxDepth};
                if (rs.ScissorEnable && i < num_scissors)
                {
                    const D3D11_RECT &r = scissors[i];
                    const LONG l = std::max<LONG>(r.left, 0), t = std::max<LONG>(r.top, 0);
                    sc[i] = {{int32_t(l), int32_t(t)}, {uint32_t(std::max<LONG>(r.right - l, 0)), uint32_t(std::max<LONG>(r.bottom - t, 0))}};
                }
                else
                    sc[i] = {{0, 0}, render_area};
            }
            vkCmdSetViewportWithCount(cb, n, vp);
            vkCmdSetScissorWithCount(cb, n, sc);
        }
        if (dirty & kRaster)
        {
            vkCmdSetRasterizerDiscardEnable(cb, VK_FALSE);
            vkCmdSetCullMode(cb, rs.CullMode == D3D11_CULL_NONE    ? VK_CULL_MODE_NONE
                                 : rs.CullMode == D3D11_CULL_FRONT ? VK_CULL_MODE_FRONT_BIT
                                                                   : VK_CULL_MODE_BACK_BIT);
            vkCmdSetFrontFace(cb, rs.FrontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);
            c->ext.set_polygon_mode(cb, rs.FillMode == D3D11_FILL_WIREFRAME && c->features.fillModeNonSolid ? VK_POLYGON_MODE_LINE
                                                                                                            : VK_POLYGON_MODE_FILL);
            const bool bias = rs.DepthBias != 0 || rs.SlopeScaledDepthBias != 0.f;
            vkCmdSetDepthBiasEnable(cb, bias ? VK_TRUE : VK_FALSE);
            vkCmdSetDepthBias(cb, float(rs.DepthBias), c->features.depthBiasClamp ? rs.DepthBiasClamp : 0.f, rs.SlopeScaledDepthBias);
            if (c->features.depthClamp)
                c->ext.set_depth_clamp(cb, rs.DepthClipEnable ? VK_FALSE : VK_TRUE);
            vkCmdSetLineWidth(cb, 1.f);
        }
        if (dirty & kDepth)
        {
            vkCmdSetDepthTestEnable(cb, ds.DepthEnable ? VK_TRUE : VK_FALSE);
            vkCmdSetDepthWriteEnable(cb, ds.DepthEnable && ds.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL ? VK_TRUE : VK_FALSE);
            vkCmdSetDepthCompareOp(cb, compare_op(ds.DepthFunc));
            vkCmdSetDepthBoundsTestEnable(cb, VK_FALSE);
            vkCmdSetStencilTestEnable(cb, ds.StencilEnable ? VK_TRUE : VK_FALSE);
            vkCmdSetStencilOp(cb, VK_STENCIL_FACE_FRONT_BIT, stencil_op(ds.FrontFace.StencilFailOp), stencil_op(ds.FrontFace.StencilPassOp),
                              stencil_op(ds.FrontFace.StencilDepthFailOp), compare_op(ds.FrontFace.StencilFunc));
            vkCmdSetStencilOp(cb, VK_STENCIL_FACE_BACK_BIT, stencil_op(ds.BackFace.StencilFailOp), stencil_op(ds.BackFace.StencilPassOp),
                              stencil_op(ds.BackFace.StencilDepthFailOp), compare_op(ds.BackFace.StencilFunc));
            vkCmdSetStencilCompareMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK, ds.StencilReadMask);
            vkCmdSetStencilWriteMask(cb, VK_STENCIL_FACE_FRONT_AND_BACK, ds.StencilWriteMask);
            vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, stencil_ref);
        }
        if (dirty & kBlend)
        {
            VkBool32 enable[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
            VkColorBlendEquationEXT eq[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            VkColorComponentFlags mask[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            for (uint32_t i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
            {
                if (blend)
                {
                    enable[i] = blend->enable[i];
                    eq[i] = blend->equation[i];
                    mask[i] = blend->write_mask[i];
                }
                else
                {
                    eq[i] = {VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD};
                    mask[i] = 0xf;
                }
            }
            if (color_count)
            {
                c->ext.set_color_blend_enable(cb, 0, color_count, enable);
                c->ext.set_color_blend_equation(cb, 0, color_count, eq);
                c->ext.set_color_write_mask(cb, 0, color_count, mask);
            }
            c->ext.set_alpha_to_coverage(cb, blend && blend->desc.AlphaToCoverageEnable ? VK_TRUE : VK_FALSE);
            if (c->features.alphaToOne)
                c->ext.set_alpha_to_one(cb, VK_FALSE);
            vkCmdSetBlendConstants(cb, blend_factor);
        }
        if (dirty & kSamples)
        {
            c->ext.set_rasterization_samples(cb, VkSampleCountFlagBits(render_samples ? render_samples : 1));
            const VkSampleMask m = sample_mask;
            c->ext.set_sample_mask(cb, VkSampleCountFlagBits(render_samples ? render_samples : 1), &m);
        }
        if (dirty & kTopology)
        {
            vkCmdSetPrimitiveTopology(cb, vk_topology(topology));
            vkCmdSetPrimitiveRestartEnable(cb, is_strip(topology) ? VK_TRUE : VK_FALSE);
            if (c->features.tessellationShader)
                c->ext.set_tess_domain_origin(cb, VK_TESSELLATION_DOMAIN_ORIGIN_UPPER_LEFT);
        }
    }

    bool Context::prepare_draw()
    {
        if (!rendering || targets_changed)
            begin_rendering();
        VkCommandBuffer cb = core()->cmd();
        if (cb != state_cb)
        {
            state_cb = cb;
            dirty = kAll;
            bound_program = bound_compute = nullptr;
            std::fill(std::begin(bound_vb), std::end(bound_vb), VK_NULL_HANDLE);
            bound_ib = VK_NULL_HANDLE;
        }
        const Program *p = bind_program(cb, false);
        if (!p)
        {
            log_once(stage[0].module.get(), "! vk: draw skipped: no usable vertex shader program%s", "");
            return false;
        }
        if (!push_descriptors(cb, p, VK_PIPELINE_BIND_POINT_GRAPHICS))
            return false;
        bind_vertex_input(cb);
        set_dynamic_state(cb);
        dirty = 0;
        return true;
    }
} // namespace xrvk

using namespace xrvk;

// ---- input assembler ----------------------------------------------------------------------------------

void ID3D11DeviceContext::IASetInputLayout(ID3D11InputLayout *pInputLayout)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    InputLayout *l = impl_of<InputLayout>(pInputLayout);
    if (c->layout != l)
        c->dirty |= kVertexInput;
    set_ref(c->layout, l);
}

void ID3D11DeviceContext::IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppVertexBuffers, const UINT *pStrides,
                                             const UINT *pOffsets)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    for (UINT i = 0; i < NumBuffers && StartSlot + i < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++i)
    {
        const UINT s = StartSlot + i;
        set_ref(c->vb[s], ppVertexBuffers ? impl_of<Buffer>(ppVertexBuffers[i]) : nullptr);
        const UINT stride = pStrides ? pStrides[i] : 0;
        if (c->vb_stride[s] != stride)
            c->dirty |= kVertexInput;
        c->vb_stride[s] = stride;
        c->vb_offset[s] = pOffsets ? pOffsets[i] : 0;
    }
}

void ID3D11DeviceContext::IASetIndexBuffer(ID3D11Buffer *pIndexBuffer, DXGI_FORMAT Format, UINT Offset)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    set_ref(c->ib, impl_of<Buffer>(pIndexBuffer));
    c->ib_format = Format;
    c->ib_offset = Offset;
}

void ID3D11DeviceContext::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY Topology)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    if (c->topology != Topology)
        c->dirty |= kTopology;
    c->topology = Topology;
}

void ID3D11DeviceContext::IAGetInputLayout(ID3D11InputLayout **ppInputLayout)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    if (c->layout)
        c->layout->AddRef();
    *ppInputLayout = c->layout;
}

void ID3D11DeviceContext::IAGetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppVertexBuffers, UINT *pStrides, UINT *pOffsets)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    for (UINT i = 0; i < NumBuffers; ++i)
    {
        const UINT s = StartSlot + i;
        const bool ok = s < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
        if (ppVertexBuffers)
        {
            ppVertexBuffers[i] = ok ? c->vb[s] : nullptr;
            if (ppVertexBuffers[i])
                ppVertexBuffers[i]->AddRef();
        }
        if (pStrides)
            pStrides[i] = ok ? c->vb_stride[s] : 0;
        if (pOffsets)
            pOffsets[i] = ok ? c->vb_offset[s] : 0;
    }
}

void ID3D11DeviceContext::IAGetIndexBuffer(ID3D11Buffer **pIndexBuffer, DXGI_FORMAT *Format, UINT *Offset)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    if (pIndexBuffer)
    {
        *pIndexBuffer = c->ib;
        if (c->ib)
            c->ib->AddRef();
    }
    if (Format)
        *Format = c->ib_format;
    if (Offset)
        *Offset = c->ib_offset;
}

void ID3D11DeviceContext::IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY *pTopology)
{
    auto lk = static_cast<Context *>(this)->guard();
    *pTopology = impl(this)->topology;
}

// ---- shader stages ------------------------------------------------------------------------------------

namespace
{
    template <class I>
    void set_shader(Context *c, Stage s, I *shader)
    {
        auto lk = c->guard();
        StageState &st = c->stage[size_t(s)];
        ID3D11DeviceChild *child = shader;
        if (st.shader == child)
            return;
        if (child)
            child->AddRef();
        if (st.shader)
            st.shader->Release();
        st.shader = child;
        st.module = shader ? static_cast<Shader<I> *>(shader)->module : nullptr;
    }

    template <class I>
    void get_shader(Context *c, Stage s, I **out)
    {
        auto lk = c->guard();
        StageState &st = c->stage[size_t(s)];
        *out = static_cast<I *>(st.shader);
        if (*out)
            (*out)->AddRef();
    }

    void set_cbs(Context *c, Stage s, UINT start, UINT n, ID3D11Buffer *const *bufs)
    {
        auto lk = c->guard();
        StageState &st = c->stage[size_t(s)];
        for (UINT i = 0; i < n && start + i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i)
            set_ref(st.cb[start + i], bufs ? impl_of<Buffer>(bufs[i]) : nullptr);
    }

    void set_srvs(Context *c, Stage s, UINT start, UINT n, ID3D11ShaderResourceView *const *views)
    {
        auto lk = c->guard();
        StageState &st = c->stage[size_t(s)];
        for (UINT i = 0; i < n && start + i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++i)
            set_ref(st.srv[start + i], views ? impl_of<ShaderResourceView>(views[i]) : nullptr);
    }

    void set_samplers(Context *c, Stage s, UINT start, UINT n, ID3D11SamplerState *const *samplers)
    {
        auto lk = c->guard();
        StageState &st = c->stage[size_t(s)];
        for (UINT i = 0; i < n && start + i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i)
            set_ref(st.sampler[start + i], samplers ? impl_of<SamplerState>(samplers[i]) : nullptr);
    }
} // namespace

void ID3D11DeviceContext::VSSetShader(ID3D11VertexShader *p, ID3D11ClassInstance *const *, UINT)
{
    auto lk = static_cast<Context *>(this)->guard();
    set_shader(impl(this), Stage::Vertex, p);
}
void ID3D11DeviceContext::HSSetShader(ID3D11HullShader *p, ID3D11ClassInstance *const *, UINT)
{
    auto lk = static_cast<Context *>(this)->guard();
    set_shader(impl(this), Stage::Hull, p);
}
void ID3D11DeviceContext::DSSetShader(ID3D11DomainShader *p, ID3D11ClassInstance *const *, UINT)
{
    auto lk = static_cast<Context *>(this)->guard();
    set_shader(impl(this), Stage::Domain, p);
}
void ID3D11DeviceContext::GSSetShader(ID3D11GeometryShader *p, ID3D11ClassInstance *const *, UINT)
{
    auto lk = static_cast<Context *>(this)->guard();
    set_shader(impl(this), Stage::Geometry, p);
}
void ID3D11DeviceContext::PSSetShader(ID3D11PixelShader *p, ID3D11ClassInstance *const *, UINT)
{
    auto lk = static_cast<Context *>(this)->guard();
    set_shader(impl(this), Stage::Pixel, p);
}
void ID3D11DeviceContext::CSSetShader(ID3D11ComputeShader *p, ID3D11ClassInstance *const *, UINT)
{
    auto lk = static_cast<Context *>(this)->guard();
    set_shader(impl(this), Stage::Compute, p);
}
void ID3D11DeviceContext::VSGetShader(ID3D11VertexShader **pp, ID3D11ClassInstance **, UINT *n)
{
    auto lk = static_cast<Context *>(this)->guard();
    get_shader(impl(this), Stage::Vertex, pp);
    if (n)
        *n = 0;
}
void ID3D11DeviceContext::GSGetShader(ID3D11GeometryShader **pp, ID3D11ClassInstance **, UINT *n)
{
    auto lk = static_cast<Context *>(this)->guard();
    get_shader(impl(this), Stage::Geometry, pp);
    if (n)
        *n = 0;
}
void ID3D11DeviceContext::PSGetShader(ID3D11PixelShader **pp, ID3D11ClassInstance **, UINT *n)
{
    auto lk = static_cast<Context *>(this)->guard();
    get_shader(impl(this), Stage::Pixel, pp);
    if (n)
        *n = 0;
}

#define XRVK_STAGE_SETTERS(P, S)                                                                                        \
    void ID3D11DeviceContext::P##SetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *pp)           \
    {                                                                                                                   \
        set_cbs(impl(this), Stage::S, StartSlot, NumBuffers, pp);                                                       \
    }                                                                                                                   \
    void ID3D11DeviceContext::P##SetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *pp) \
    {                                                                                                                   \
        set_srvs(impl(this), Stage::S, StartSlot, NumViews, pp);                                                        \
    }                                                                                                                   \
    void ID3D11DeviceContext::P##SetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *pp)           \
    {                                                                                                                   \
        set_samplers(impl(this), Stage::S, StartSlot, NumSamplers, pp);                                                 \
    }
XRVK_STAGE_SETTERS(VS, Vertex)
XRVK_STAGE_SETTERS(HS, Hull)
XRVK_STAGE_SETTERS(DS, Domain)
XRVK_STAGE_SETTERS(GS, Geometry)
XRVK_STAGE_SETTERS(PS, Pixel)
XRVK_STAGE_SETTERS(CS, Compute)
#undef XRVK_STAGE_SETTERS

void ID3D11DeviceContext::VSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **pp)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    for (UINT i = 0; i < NumBuffers; ++i)
    {
        const UINT s = StartSlot + i;
        pp[i] = s < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT ? c->stage[0].cb[s] : nullptr;
        if (pp[i])
            pp[i]->AddRef();
    }
}

void ID3D11DeviceContext::PSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **pp)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    for (UINT i = 0; i < NumViews; ++i)
    {
        const UINT s = StartSlot + i;
        pp[i] = s < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT ? c->stage[size_t(Stage::Pixel)].srv[s] : nullptr;
        if (pp[i])
            pp[i]->AddRef();
    }
}

void ID3D11DeviceContext::PSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **pp)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    for (UINT i = 0; i < NumSamplers; ++i)
    {
        const UINT s = StartSlot + i;
        pp[i] = s < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT ? c->stage[size_t(Stage::Pixel)].sampler[s] : nullptr;
        if (pp[i])
            pp[i]->AddRef();
    }
}

void ID3D11DeviceContext::CSSetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView *const *pp, const UINT *pUAVInitialCounts)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    if (pUAVInitialCounts)
        for (UINT i = 0; i < NumUAVs; ++i)
            if (pUAVInitialCounts[i] != UINT(-1))
                log_once(c, "! vk: UAV counters are not supported yet%s", "");
    for (UINT i = 0; i < NumUAVs && StartSlot + i < D3D11_1_UAV_SLOT_COUNT; ++i)
        set_ref(c->cs_uav[StartSlot + i], pp ? impl_of<UnorderedAccessView>(pp[i]) : nullptr);
}

// ---- rasterizer and output merger -------------------------------------------------------------------

void ID3D11DeviceContext::RSSetState(ID3D11RasterizerState *pRasterizerState)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    set_ref(c->rasterizer, impl_of<RasterizerState>(pRasterizerState));
    c->dirty |= kRaster;
}

void ID3D11DeviceContext::RSGetState(ID3D11RasterizerState **ppRasterizerState)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    *ppRasterizerState = c->rasterizer;
    if (c->rasterizer)
        c->rasterizer->AddRef();
}

void ID3D11DeviceContext::RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT *pViewports)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    c->num_viewports = std::min<UINT>(NumViewports, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    for (UINT i = 0; i < c->num_viewports; ++i)
        c->viewports[i] = pViewports[i];
    c->dirty |= kViewport;
}

void ID3D11DeviceContext::RSGetViewports(UINT *pNumViewports, D3D11_VIEWPORT *pViewports)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    if (pViewports)
        for (UINT i = 0; i < *pNumViewports && i < c->num_viewports; ++i)
            pViewports[i] = c->viewports[i];
    *pNumViewports = c->num_viewports;
}

void ID3D11DeviceContext::RSSetScissorRects(UINT NumRects, const D3D11_RECT *pRects)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    c->num_scissors = std::min<UINT>(NumRects, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    for (UINT i = 0; i < c->num_scissors; ++i)
        c->scissors[i] = pRects[i];
    c->dirty |= kViewport;
}

void ID3D11DeviceContext::RSGetScissorRects(UINT *pNumRects, D3D11_RECT *pRects)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    if (pRects)
        for (UINT i = 0; i < *pNumRects && i < c->num_scissors; ++i)
            pRects[i] = c->scissors[i];
    *pNumRects = c->num_scissors;
}

void ID3D11DeviceContext::OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView *const *ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    bool changed = c->dsv != pDepthStencilView;
    for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        RenderTargetView *v = i < NumViews && ppRenderTargetViews ? impl_of<RenderTargetView>(ppRenderTargetViews[i]) : nullptr;
        changed |= c->rtv[i] != v;
        set_ref(c->rtv[i], v);
    }
    set_ref(c->dsv, impl_of<DepthStencilView>(pDepthStencilView));
    if (changed)
        c->targets_changed = true;
}

void ID3D11DeviceContext::OMGetRenderTargets(UINT NumViews, ID3D11RenderTargetView **ppRenderTargetViews, ID3D11DepthStencilView **ppDepthStencilView)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    if (ppRenderTargetViews)
        for (UINT i = 0; i < NumViews; ++i)
        {
            ppRenderTargetViews[i] = i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT ? c->rtv[i] : nullptr;
            if (ppRenderTargetViews[i])
                ppRenderTargetViews[i]->AddRef();
        }
    if (ppDepthStencilView)
    {
        *ppDepthStencilView = c->dsv;
        if (c->dsv)
            c->dsv->AddRef();
    }
}

void ID3D11DeviceContext::OMSetBlendState(ID3D11BlendState *pBlendState, const FLOAT BlendFactor[4], UINT SampleMask)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    set_ref(c->blend, impl_of<BlendState>(pBlendState));
    for (int i = 0; i < 4; ++i)
        c->blend_factor[i] = BlendFactor ? BlendFactor[i] : 1.f;
    c->sample_mask = SampleMask;
    c->dirty |= kBlend | kSamples;
}

void ID3D11DeviceContext::OMGetBlendState(ID3D11BlendState **ppBlendState, FLOAT BlendFactor[4], UINT *pSampleMask)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    if (ppBlendState)
    {
        *ppBlendState = c->blend;
        if (c->blend)
            c->blend->AddRef();
    }
    if (BlendFactor)
        memcpy(BlendFactor, c->blend_factor, sizeof(c->blend_factor));
    if (pSampleMask)
        *pSampleMask = c->sample_mask;
}

void ID3D11DeviceContext::OMSetDepthStencilState(ID3D11DepthStencilState *pDepthStencilState, UINT StencilRef)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    set_ref(c->depth_stencil, impl_of<DepthStencilState>(pDepthStencilState));
    c->stencil_ref = StencilRef;
    c->dirty |= kDepth;
}

void ID3D11DeviceContext::OMGetDepthStencilState(ID3D11DepthStencilState **ppDepthStencilState, UINT *pStencilRef)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    if (ppDepthStencilState)
    {
        *ppDepthStencilState = c->depth_stencil;
        if (c->depth_stencil)
            c->depth_stencil->AddRef();
    }
    if (pStencilRef)
        *pStencilRef = c->stencil_ref;
}

void ID3D11DeviceContext::ClearState()
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    c->outside_rendering();
    set_ref(c->layout, (InputLayout *)nullptr);
    for (auto &b : c->vb)
        set_ref(b, (Buffer *)nullptr);
    set_ref(c->ib, (Buffer *)nullptr);
    for (size_t s = 0; s < size_t(Stage::Count); ++s)
    {
        StageState &st = c->stage[s];
        if (st.shader)
            st.shader->Release();
        st.shader = nullptr;
        st.module = nullptr;
        for (auto &b : st.cb)
            set_ref(b, (Buffer *)nullptr);
        for (auto &v : st.srv)
            set_ref(v, (ShaderResourceView *)nullptr);
        for (auto &v : st.sampler)
            set_ref(v, (SamplerState *)nullptr);
    }
    for (auto &u : c->cs_uav)
        set_ref(u, (UnorderedAccessView *)nullptr);
    set_ref(c->blend, (BlendState *)nullptr);
    set_ref(c->depth_stencil, (DepthStencilState *)nullptr);
    set_ref(c->rasterizer, (RasterizerState *)nullptr);
    for (auto &r : c->rtv)
        set_ref(r, (RenderTargetView *)nullptr);
    set_ref(c->dsv, (DepthStencilView *)nullptr);
    for (int i = 0; i < 4; ++i)
        c->blend_factor[i] = 1.f;
    c->sample_mask = 0xffffffff;
    c->stencil_ref = 0;
    c->topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    c->num_viewports = c->num_scissors = 0;
    c->targets_changed = true;
    c->dirty = kAll;
}

// ---- draws and dispatches ---------------------------------------------------------------------------

void ID3D11DeviceContext::Draw(UINT VertexCount, UINT StartVertexLocation)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    if (!VertexCount || !c->prepare_draw())
        return;
    vkCmdDraw(core()->cmd(), VertexCount, 1, StartVertexLocation, 0);
}

void ID3D11DeviceContext::DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    if (!IndexCount || !c->ib || !c->prepare_draw())
        return;
    VkCommandBuffer cb = core()->cmd();
    const VkIndexType type = c->ib_format == DXGI_FORMAT_R32_UINT ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
    const VkBuffer b = c->ib->vk();
    const VkDeviceSize off = c->ib->base() + c->ib_offset;
    if (b != c->bound_ib || off != c->bound_ib_offset || type != c->bound_ib_type)
    {
        vkCmdBindIndexBuffer(cb, b, off, type);
        c->bound_ib = b;
        c->bound_ib_offset = off;
        c->bound_ib_type = type;
    }
    vkCmdDrawIndexed(cb, IndexCount, 1, StartIndexLocation, BaseVertexLocation, 0);
}

void ID3D11DeviceContext::Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    VkCommandBuffer cb = c->outside_rendering();
    if (cb != c->state_cb)
    {
        c->state_cb = cb;
        c->dirty = kAll;
        c->bound_program = c->bound_compute = nullptr;
        std::fill(std::begin(c->bound_vb), std::end(c->bound_vb), VK_NULL_HANDLE);
        c->bound_ib = VK_NULL_HANDLE;
    }
    const Program *p = c->bind_program(cb, true);
    if (!p)
        return;
    full_barrier(cb);
    if (!c->push_descriptors(cb, p, VK_PIPELINE_BIND_POINT_COMPUTE))
        return;
    vkCmdDispatch(cb, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
    full_barrier(cb);
}

// ---- clears ---------------------------------------------------------------------------------------------

void ID3D11DeviceContext::ClearRenderTargetView(ID3D11RenderTargetView *pRenderTargetView, const FLOAT ColorRGBA[4])
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    auto *v = impl_of<RenderTargetView>(pRenderTargetView);
    if (!v)
        return;
    VkClearColorValue cv;
    memcpy(cv.float32, ColorRGBA, sizeof(cv.float32));
    // inside the current rendering scope: clear the attachment without ending the scope
    for (uint32_t i = 0; c->rendering && !c->targets_changed && i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        if (c->current_rtv[i] == v)
        {
            VkClearAttachment a{VK_IMAGE_ASPECT_COLOR_BIT, i, {}};
            a.clearValue.color = cv;
            const VkClearRect r{{{0, 0}, v->v.extent}, 0, v->v.range.layerCount};
            vkCmdClearAttachments(core()->cmd(), 1, &a, 1, &r);
            return;
        }
    VkCommandBuffer cb = c->outside_rendering();
    transition(cb, v->v.img, VK_IMAGE_LAYOUT_GENERAL);
    transfer_barrier(cb, true);
    vkCmdClearColorImage(cb, v->v.img->image, VK_IMAGE_LAYOUT_GENERAL, &cv, 1, &v->v.range);
    transfer_barrier(cb, false);
}

void ID3D11DeviceContext::ClearDepthStencilView(ID3D11DepthStencilView *pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    auto *v = impl_of<DepthStencilView>(pDepthStencilView);
    if (!v)
        return;
    VkImageAspectFlags aspect = 0;
    if (ClearFlags & D3D11_CLEAR_DEPTH)
        aspect |= v->v.range.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT;
    if (ClearFlags & D3D11_CLEAR_STENCIL)
        aspect |= v->v.range.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT;
    if (!aspect)
        return;
    const VkClearDepthStencilValue dv{Depth, Stencil};
    if (c->rendering && !c->targets_changed && c->current_dsv == v)
    {
        VkClearAttachment a{aspect, 0, {}};
        a.clearValue.depthStencil = dv;
        const VkClearRect r{{{0, 0}, v->v.extent}, 0, v->v.range.layerCount};
        vkCmdClearAttachments(core()->cmd(), 1, &a, 1, &r);
        return;
    }
    VkCommandBuffer cb = c->outside_rendering();
    transition(cb, v->v.img, VK_IMAGE_LAYOUT_GENERAL);
    transfer_barrier(cb, true);
    VkImageSubresourceRange range = v->v.range;
    range.aspectMask = aspect;
    vkCmdClearDepthStencilImage(cb, v->v.img->image, VK_IMAGE_LAYOUT_GENERAL, &dv, 1, &range);
    transfer_barrier(cb, false);
}

// ---- copies -------------------------------------------------------------------------------------------

namespace
{
    // copies whole subresources src_sub -> dst_sub (or a box of it) between two images / staging memory
    void copy_image_region(VkCommandBuffer cb, Image *dst, UINT dst_sub, int32_t dx, int32_t dy, int32_t dz, Image *src, UINT src_sub,
                           const D3D11_BOX *box)
    {
        const uint32_t sm = src_sub % src->mips, sl = src_sub / src->mips, dm = dst_sub % dst->mips, dl = dst_sub / dst->mips;
        VkOffset3D so{0, 0, 0};
        VkExtent3D ext{src->mip_width(sm), src->mip_height(sm), src->mip_depth(sm)};
        if (box)
        {
            so = {int32_t(box->left), int32_t(box->top), int32_t(box->front)};
            ext = {box->right - box->left, box->bottom - box->top, box->back - box->front};
        }
        if (src->image && dst->image)
        {
            VkImageCopy r{{src->copy_aspect(), sm, sl, 1}, so, {dst->copy_aspect(), dm, dl, 1}, {dx, dy, dz}, ext};
            vkCmdCopyImage(cb, src->image, VK_IMAGE_LAYOUT_GENERAL, dst->image, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
        }
        else if (src->image && dst->staging) // GPU -> CPU readback (whole subresource)
        {
            VkBufferImageCopy r{};
            r.bufferOffset = dst->sub_offset[dst_sub];
            r.imageSubresource = {src->copy_aspect() & ~VK_IMAGE_ASPECT_STENCIL_BIT, sm, sl, 1};
            r.imageOffset = so;
            r.imageExtent = ext;
            vkCmdCopyImageToBuffer(cb, src->image, VK_IMAGE_LAYOUT_GENERAL, dst->staging, 1, &r);
        }
        else if (src->staging && dst->image) // CPU -> GPU
        {
            VkBufferImageCopy r{};
            r.bufferOffset = src->sub_offset[src_sub];
            r.imageSubresource = {dst->copy_aspect() & ~VK_IMAGE_ASPECT_STENCIL_BIT, dm, dl, 1};
            r.imageOffset = {dx, dy, dz};
            r.imageExtent = ext;
            vkCmdCopyBufferToImage(cb, src->staging, dst->image, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
        }
        else
            log("! vk: copy between two staging textures is not supported");
    }

    void copy_buffer_region(VkCommandBuffer cb, Buffer *dst, UINT dx, Buffer *src, const D3D11_BOX *box)
    {
        const UINT begin = box ? box->left : 0, end = box ? box->right : src->desc.ByteWidth;
        if (end <= begin || dx + (end - begin) > dst->desc.ByteWidth)
            return;
        const VkBuffer sb = src->dynamic() ? src->slice.buffer : src->buffer, db = dst->buffer;
        if (!db)
        {
            log("! vk: copies into dynamic buffers are not supported (D3D11 forbids them too)");
            return;
        }
        VkBufferCopy r{src->base() + begin, dx, end - begin};
        vkCmdCopyBuffer(cb, sb, db, 1, &r);
    }
} // namespace

void ID3D11DeviceContext::CopyResource(ID3D11Resource *pDstResource, ID3D11Resource *pSrcResource)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    if (!pDstResource || !pSrcResource || pDstResource->xrvk_dimension != pSrcResource->xrvk_dimension)
        return;
    VkCommandBuffer cb = c->outside_rendering();
    transfer_barrier(cb, true);
    if (pDstResource->xrvk_dimension == D3D11_RESOURCE_DIMENSION_BUFFER)
        copy_buffer_region(cb, static_cast<Buffer *>(pDstResource), 0, static_cast<Buffer *>(pSrcResource), nullptr);
    else
    {
        Image *dst = image_of(pDstResource), *src = image_of(pSrcResource);
        transition(cb, dst, VK_IMAGE_LAYOUT_GENERAL);
        transition(cb, src, VK_IMAGE_LAYOUT_GENERAL);
        const uint32_t n = std::min(dst->subresources(), src->subresources());
        for (uint32_t s = 0; s < n; ++s)
            copy_image_region(cb, dst, s, 0, 0, 0, src, s, nullptr);
    }
    transfer_barrier(cb, false);
}

void ID3D11DeviceContext::CopySubresourceRegion(ID3D11Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ,
                                                ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    if (!pDstResource || !pSrcResource)
        return;
    VkCommandBuffer cb = c->outside_rendering();
    transfer_barrier(cb, true);
    if (pDstResource->xrvk_dimension == D3D11_RESOURCE_DIMENSION_BUFFER && pSrcResource->xrvk_dimension == D3D11_RESOURCE_DIMENSION_BUFFER)
        copy_buffer_region(cb, static_cast<Buffer *>(pDstResource), DstX, static_cast<Buffer *>(pSrcResource), pSrcBox);
    else if (Image *dst = image_of(pDstResource), *src = image_of(pSrcResource); dst && src && DstSubresource < dst->subresources() &&
                                                                                 SrcSubresource < src->subresources())
    {
        transition(cb, dst, VK_IMAGE_LAYOUT_GENERAL);
        transition(cb, src, VK_IMAGE_LAYOUT_GENERAL);
        copy_image_region(cb, dst, DstSubresource, int32_t(DstX), int32_t(DstY), int32_t(DstZ), src, SrcSubresource, pSrcBox);
    }
    transfer_barrier(cb, false);
}

void ID3D11DeviceContext::ResolveSubresource(ID3D11Resource *pDstResource, UINT DstSubresource, ID3D11Resource *pSrcResource, UINT SrcSubresource,
                                             DXGI_FORMAT)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    Image *dst = image_of(pDstResource), *src = image_of(pSrcResource);
    if (!dst || !src || !dst->image || !src->image)
        return;
    VkCommandBuffer cb = c->outside_rendering();
    transfer_barrier(cb, true);
    transition(cb, dst, VK_IMAGE_LAYOUT_GENERAL);
    transition(cb, src, VK_IMAGE_LAYOUT_GENERAL);
    const uint32_t sm = SrcSubresource % src->mips, dm = DstSubresource % dst->mips;
    if (src->samples > 1)
    {
        VkImageResolve r{{VK_IMAGE_ASPECT_COLOR_BIT, sm, SrcSubresource / src->mips, 1}, {}, {VK_IMAGE_ASPECT_COLOR_BIT, dm, DstSubresource / dst->mips, 1}, {}, {src->mip_width(sm), src->mip_height(sm), 1}};
        vkCmdResolveImage(cb, src->image, VK_IMAGE_LAYOUT_GENERAL, dst->image, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
    }
    else // D3D11 also accepts a single-sampled source (plain copy)
        copy_image_region(cb, dst, DstSubresource, 0, 0, 0, src, SrcSubresource, nullptr);
    transfer_barrier(cb, false);
}

void ID3D11DeviceContext::UpdateSubresource(ID3D11Resource *pDstResource, UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData,
                                            UINT SrcRowPitch, UINT SrcDepthPitch)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    if (!pDstResource)
        return;
    if (pDstResource->xrvk_dimension == D3D11_RESOURCE_DIMENSION_BUFFER)
        update_buffer(c, static_cast<Buffer *>(pDstResource), pDstBox, pSrcData);
    else if (Image *img = image_of(pDstResource))
        update_texture(c, img, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
}

HRESULT ID3D11DeviceContext::Map(ID3D11Resource *pResource, UINT Subresource, D3D11_MAP MapType, UINT, D3D11_MAPPED_SUBRESOURCE *pMappedResource)
{
    auto lk = static_cast<Context *>(this)->guard();
    auto *c = impl(this);
    if (!pResource || !pMappedResource)
        return E_INVALIDARG;
    if (pResource->xrvk_dimension == D3D11_RESOURCE_DIMENSION_BUFFER)
        return Subresource == 0 ? map_buffer(c, static_cast<Buffer *>(pResource), MapType, pMappedResource) : E_INVALIDARG;
    Image *img = image_of(pResource);
    return img ? map_texture(c, img, Subresource, MapType, pMappedResource) : E_INVALIDARG;
}

void ID3D11DeviceContext::Unmap(ID3D11Resource *pResource, UINT Subresource)
{
    auto lk = static_cast<Context *>(this)->guard();
    if (Image *img = image_of(pResource))
        unmap_texture(impl(this), img, Subresource);
    // buffers and staging memory are persistently mapped and coherent
}
