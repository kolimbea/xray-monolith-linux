#include "vkPresent.h"

#include "vkCore.h"
#include "vkDxc.h"
#include "vkHeap.h"
#include "vkObjects.h"

#include <cmath>
#include <cstring>

namespace xrvk
{
    namespace
    {
        // The engine's curve (CGammaControl::GenLUT) sampled at kGammaPoints, linearly interpolated.
        const char *const kGammaHlsl = R"hlsl(
[[vk::binding(0)]] Texture2D<float4> src;
[[vk::binding(1)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> dst;
[[vk::binding(2)]] cbuffer Lut { float4 lut[256]; };

float curve(float x, uint channel)
{
    float p = saturate(x) * 255.0;
    uint i0 = (uint)floor(p);
    uint i1 = min(i0 + 1, 255u);
    return lerp(lut[i0][channel], lut[i1][channel], p - i0);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    src.GetDimensions(w, h);
    if (id.x >= w || id.y >= h)
        return;
    float4 c = src.Load(int3(id.xy, 0));
    dst[id.xy] = float4(curve(c.r, 0), curve(c.g, 1), curve(c.b, 2), 1.0);
}
)hlsl";
    } // namespace

    PresentPass &present_pass()
    {
        static PresentPass p;
        return p;
    }

    void PresentPass::set_gamma(const float (*rgb)[3])
    {
        bool identity = true;
        for (uint32_t i = 0; i < kGammaPoints; ++i)
        {
            const float x = float(i) / float(kGammaPoints - 1);
            for (int c = 0; c < 3; ++c)
            {
                m_lut[i * 4 + c] = rgb[i][c];
                if (std::fabs(rgb[i][c] - x) > 0.5f / 255.f)
                    identity = false;
            }
            m_lut[i * 4 + 3] = 1.f;
        }
        if (m_gamma_active != !identity)
            log("* vk: gamma curve %s", identity ? "identity (present pass off)" : "set (present pass on)");
        m_gamma_active = !identity;
    }

    bool PresentPass::init()
    {
        Core &c = *core();
        std::vector<uint32_t> spirv;
        std::string errors;
        if (!dxc::compile(kGammaHlsl, {"-spirv", "-fspv-target-env=vulkan1.3", "-T", "cs_6_0", "-E", "main", "-O3"}, spirv, errors))
        {
            log("! vk: present gamma shader: %s", errors.c_str());
            return false;
        }
        VkDescriptorSetLayoutBinding b[3] = {
            {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dl.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        dl.bindingCount = 3;
        dl.pBindings = b;
        XRVK_CHECK(vkCreateDescriptorSetLayout(c.device, &dl, nullptr, &m_set_layout));
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &m_set_layout;
        XRVK_CHECK(vkCreatePipelineLayout(c.device, &pl, nullptr, &m_layout));
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        sm.codeSize = spirv.size() * sizeof(uint32_t);
        sm.pCode = spirv.data();
        VkShaderModule module;
        XRVK_CHECK(vkCreateShaderModule(c.device, &sm, nullptr, &module));
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        ci.stage.module = module;
        ci.stage.pName = "main";
        ci.layout = m_layout;
        const VkResult r = vkCreateComputePipelines(c.device, VK_NULL_HANDLE, 1, &ci, nullptr, &m_gamma);
        vkDestroyShaderModule(c.device, module, nullptr);
        XRVK_CHECK(r);
        return true;
    }

    bool PresentPass::resize(const Image &src)
    {
        Core &c = *core();
        if (m_src_image == src.image && m_w == src.width && m_h == src.height)
            return true;
        vkDeviceWaitIdle(c.device); // the old views/images may still be read by frames in flight
        if (m_src_view)
            vkDestroyImageView(c.device, m_src_view, nullptr);
        if (m_out_view)
            vkDestroyImageView(c.device, m_out_view, nullptr);
        if (m_out)
            vmaDestroyImage(c.vma, m_out, m_out_alloc);
        m_src_view = m_out_view = VK_NULL_HANDLE;
        m_out = VK_NULL_HANDLE;

        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = src.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = src.format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        XRVK_CHECK(vkCreateImageView(c.device, &vi, nullptr, &m_src_view));

        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = VK_FORMAT_R8G8B8A8_UNORM;
        ii.extent = {src.width, src.height, 1};
        ii.mipLevels = ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        ai.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        XRVK_CHECK(vmaCreateImage(c.vma, &ii, &ai, &m_out, &m_out_alloc, nullptr));
        vi.image = m_out;
        vi.format = VK_FORMAT_R8G8B8A8_UNORM;
        XRVK_CHECK(vkCreateImageView(c.device, &vi, nullptr, &m_out_view));
        m_src_image = src.image;
        m_w = src.width;
        m_h = src.height;
        m_first_use = true;
        return true;
    }

    VkImage PresentPass::record(VkCommandBuffer cb, const Image &src)
    {
        if (!m_gamma_active)
            return src.image;
        if (m_state == 0)
            m_state = init() ? 1 : -1;
        if (m_state < 0 || !resize(src))
            return src.image;
        Core &c = *core();

        // the output image lives in GENERAL like every engine image; first use comes from UNDEFINED
        VkImageMemoryBarrier2 ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        ib.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        ib.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        ib.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        ib.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        ib.oldLayout = m_first_use ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
        ib.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib.image = m_out;
        ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2}; // the engine's writes to the back buffer
        mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        mb.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        mb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mb.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT;
        VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        d.memoryBarrierCount = 1;
        d.pMemoryBarriers = &mb;
        d.imageMemoryBarrierCount = 1;
        d.pImageMemoryBarriers = &ib;
        vkCmdPipelineBarrier2(cb, &d);
        m_first_use = false;

        const Slice lut = c.heap->alloc(sizeof(m_lut));
        memcpy(lut.ptr, m_lut.data(), sizeof(m_lut));
        c.heap->release(lut, c.recording_value());

        VkDescriptorImageInfo in{VK_NULL_HANDLE, m_src_view, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo out{VK_NULL_HANDLE, m_out_view, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorBufferInfo lb{lut.buffer, lut.offset, sizeof(m_lut)};
        VkWriteDescriptorSet w[3] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
        for (uint32_t i = 0; i < 3; ++i)
        {
            w[i].dstBinding = i;
            w[i].descriptorCount = 1;
        }
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        w[0].pImageInfo = &in;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[1].pImageInfo = &out;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[2].pBufferInfo = &lb;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_gamma);
        c.ext.push_descriptor_set(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 3, w);
        vkCmdDispatch(cb, (m_w + 7) / 8, (m_h + 7) / 8, 1);

        mb.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mb.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        mb.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
        d.imageMemoryBarrierCount = 0;
        vkCmdPipelineBarrier2(cb, &d);
        return m_out;
    }

    void PresentPass::destroy()
    {
        Core *c = core();
        if (!c)
            return;
        if (m_src_view)
            vkDestroyImageView(c->device, m_src_view, nullptr);
        if (m_out_view)
            vkDestroyImageView(c->device, m_out_view, nullptr);
        if (m_out)
            vmaDestroyImage(c->vma, m_out, m_out_alloc);
        if (m_gamma)
            vkDestroyPipeline(c->device, m_gamma, nullptr);
        if (m_layout)
            vkDestroyPipelineLayout(c->device, m_layout, nullptr);
        if (m_set_layout)
            vkDestroyDescriptorSetLayout(c->device, m_set_layout, nullptr);
        *this = PresentPass();
    }
} // namespace xrvk
