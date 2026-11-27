#include "vkPresent.h"

#include "vkCore.h"
#include "vkDxc.h"
#include "vkFsr.h"
#include "vkHeap.h"
#include "vkObjects.h"

#include "fsr1_hlsl.h" // generated: kFfxAHeader, kFfxFsr1Header (AMD FidelityFX FSR 1, MIT)

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace xrvk
{
    namespace
    {
        // Bindings shared by every pass: 0 input (sampled), 1 linear-clamp sampler, 2 output (storage),
        // 3 gamma curve (uniform). Push constants: up to four uint4.
        constexpr uint32_t kPushSize = 64;

        // The engine's curve (CGammaControl::GenLUT) sampled at kGammaPoints, linearly interpolated.
        const char *const kGammaHlsl = R"hlsl(
[[vk::binding(0)]] Texture2D<float4> src;
[[vk::binding(2)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> dst;
[[vk::binding(3)]] cbuffer Lut { float4 lut[256]; };

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

        // FSR 1 as in AMD's sample (FP32 path, 64 threads cover 16x16 pixels); EASU or RCAS by EASU.
        const char *const kFsrHlsl = R"hlsl(
#define A_GPU 1
#define A_HLSL 1
#include "ffx_a.h"
[[vk::binding(0)]] Texture2D<float4> InputTexture;
[[vk::binding(1)]] SamplerState samLinearClamp;
[[vk::binding(2)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> OutputTexture;
struct Consts { uint4 c0; uint4 c1; uint4 c2; uint4 c3; };
[[vk::push_constant]] Consts pc;
#if EASU
#define FSR_EASU_F 1
AF4 FsrEasuRF(AF2 p) { return InputTexture.GatherRed(samLinearClamp, p, int2(0, 0)); }
AF4 FsrEasuGF(AF2 p) { return InputTexture.GatherGreen(samLinearClamp, p, int2(0, 0)); }
AF4 FsrEasuBF(AF2 p) { return InputTexture.GatherBlue(samLinearClamp, p, int2(0, 0)); }
#else
#define FSR_RCAS_F 1
AF4 FsrRcasLoadF(ASU2 p) { return InputTexture.Load(int3(ASU2(p), 0)); }
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
#endif
#include "ffx_fsr1.h"

void Filter(AU2 pos)
{
    uint w, h;
    OutputTexture.GetDimensions(w, h);
    if (pos.x >= w || pos.y >= h)
        return;
    AF3 c;
#if EASU
    FsrEasuF(c, pos, pc.c0, pc.c1, pc.c2, pc.c3);
#else
    FsrRcasF(c.r, c.g, c.b, pos, pc.c0);
#endif
    OutputTexture[pos] = float4(c, 1);
}

[numthreads(64, 1, 1)]
void main(uint3 LocalThreadId : SV_GroupThreadID, uint3 WorkGroupId : SV_GroupID)
{
    AU2 gxy = ARmp8x8(LocalThreadId.x) + AU2(WorkGroupId.x << 4u, WorkGroupId.y << 4u);
    Filter(gxy);
    gxy.x += 8u;
    Filter(gxy);
    gxy.y += 8u;
    Filter(gxy);
    gxy.x -= 8u;
    Filter(gxy);
}
)hlsl";

        VkMemoryBarrier2 memory_barrier(VkPipelineStageFlags2 src_stage, VkAccessFlags2 src, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst)
        {
            VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
            mb.srcStageMask = src_stage;
            mb.srcAccessMask = src;
            mb.dstStageMask = dst_stage;
            mb.dstAccessMask = dst;
            return mb;
        }
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

    VkPipeline PresentPass::compute_pipeline(const char *hlsl, const char *what, bool fsr)
    {
        Core &c = *core();
        std::string pre, errors;
        const dxc::IncludeFn include = [](const std::string &name, std::string &text)
        {
            if (name == "ffx_a.h")
                text = kFfxAHeader;
            else if (name == "ffx_fsr1.h")
                text = kFfxFsr1Header;
            else
                return false;
            return true;
        };
        std::vector<dxc::Define> defines;
        if (fsr)
            defines.push_back({"EASU", std::strcmp(what, "EASU") == 0 ? "1" : "0"});
        std::vector<uint32_t> spirv;
        if (!dxc::preprocess(hlsl, defines, include, pre, errors) ||
            !dxc::compile(pre, {"-spirv", "-fspv-target-env=vulkan1.3", "-HV", "2018", "-T", "cs_6_0", "-E", "main", "-O3"}, spirv, errors))
        {
            log("! vk: present pass %s: shader: %s", what, errors.c_str());
            return VK_NULL_HANDLE;
        }
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
        VkPipeline p = VK_NULL_HANDLE;
        const VkResult r = vkCreateComputePipelines(c.device, VK_NULL_HANDLE, 1, &ci, nullptr, &p);
        vkDestroyShaderModule(c.device, module, nullptr);
        XRVK_CHECK(r);
        return p;
    }

    bool PresentPass::init()
    {
        Core &c = *core();
        if (const char *u = getenv("XR_VK_UPSCALE"))
            m_fsr = std::strcmp(u, "linear") != 0;
        if (const char *s = getenv("XR_VK_FSR_SHARPNESS"))
            m_sharpness = float(atof(s));
        VkDescriptorSetLayoutBinding b[4] = {
            {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dl.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        dl.bindingCount = 4;
        dl.pBindings = b;
        XRVK_CHECK(vkCreateDescriptorSetLayout(c.device, &dl, nullptr, &m_set_layout));
        const VkPushConstantRange pr{VK_SHADER_STAGE_COMPUTE_BIT, 0, kPushSize};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &m_set_layout;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &pr;
        XRVK_CHECK(vkCreatePipelineLayout(c.device, &pl, nullptr, &m_layout));
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 0.f;
        XRVK_CHECK(vkCreateSampler(c.device, &si, nullptr, &m_sampler));
        m_gamma = compute_pipeline(kGammaHlsl, "gamma", false);
        if (m_fsr)
        {
            m_easu = compute_pipeline(kFsrHlsl, "EASU", true);
            m_rcas = compute_pipeline(kFsrHlsl, "RCAS", true);
            if (!m_easu || !m_rcas)
                m_fsr = false; // logged; the blit scales bilinearly
        }
        log("* vk: present passes ready (gamma %s, upscaling %s, sharpness %.2f)", m_gamma ? "ok" : "unavailable",
            m_fsr ? "FSR 1" : "bilinear", m_sharpness);
        return true;
    }

    void PresentPass::destroy_target(Target &t)
    {
        Core &c = *core();
        if (t.view)
            vkDestroyImageView(c.device, t.view, nullptr);
        if (t.image)
            vmaDestroyImage(c.vma, t.image, t.alloc);
        t = Target();
    }

    void PresentPass::ensure_target(Target &t, uint32_t w, uint32_t h)
    {
        if (t.image && t.w == w && t.h == h)
            return;
        Core &c = *core();
        if (t.image)
        {
            vkDeviceWaitIdle(c.device); // frames in flight may still read it
            destroy_target(t);
        }
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = VK_FORMAT_R8G8B8A8_UNORM;
        ii.extent = {w, h, 1};
        ii.mipLevels = ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        ai.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        XRVK_CHECK(vmaCreateImage(c.vma, &ii, &ai, &t.image, &t.alloc, nullptr));
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = t.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R8G8B8A8_UNORM;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        XRVK_CHECK(vkCreateImageView(c.device, &vi, nullptr, &t.view));
        t.w = w;
        t.h = h;
        t.fresh = true;
    }

    VkImageView PresentPass::source_view(const Image &src)
    {
        if (m_src_image == src.image && m_src_view)
            return m_src_view;
        Core &c = *core();
        if (m_src_view)
        {
            vkDeviceWaitIdle(c.device);
            vkDestroyImageView(c.device, m_src_view, nullptr);
        }
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = src.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = src.format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        XRVK_CHECK(vkCreateImageView(c.device, &vi, nullptr, &m_src_view));
        m_src_image = src.image;
        return m_src_view;
    }

    void PresentPass::dispatch(VkCommandBuffer cb, VkPipeline p, VkImageView in, Target &out, const void *push, uint32_t push_size,
                               const VkDescriptorBufferInfo *lut, uint32_t groups_x, uint32_t groups_y)
    {
        Core &c = *core();
        // the input was written by the engine or the previous pass; the output may still be read by the
        // previous frame's blit (same queue, so a memory dependency is enough)
        VkImageMemoryBarrier2 ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        ib.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        ib.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        ib.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        ib.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        ib.oldLayout = out.fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
        ib.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib.image = out.image;
        ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        const VkMemoryBarrier2 mb = memory_barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT);
        VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        d.memoryBarrierCount = 1;
        d.pMemoryBarriers = &mb;
        d.imageMemoryBarrierCount = 1;
        d.pImageMemoryBarriers = &ib;
        vkCmdPipelineBarrier2(cb, &d);
        out.fresh = false;

        VkDescriptorImageInfo input{VK_NULL_HANDLE, in, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo sampler{m_sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        VkDescriptorImageInfo output{VK_NULL_HANDLE, out.view, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet w[4] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
        for (uint32_t i = 0; i < 4; ++i)
        {
            w[i].dstBinding = i;
            w[i].descriptorCount = 1;
        }
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        w[0].pImageInfo = &input;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        w[1].pImageInfo = &sampler;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[2].pImageInfo = &output;
        w[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[3].pBufferInfo = lut;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p);
        c.ext.push_descriptor_set(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, lut ? 4 : 3, w);
        if (push_size)
            vkCmdPushConstants(cb, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, push_size, push);
        vkCmdDispatch(cb, groups_x, groups_y, 1);
    }

    PresentPass::Result PresentPass::record(VkCommandBuffer cb, const Image &src, VkExtent2D out)
    {
        Result r{src.image, src.width, src.height};
        const bool upscale = m_fsr && (out.width > src.width || out.height > src.height);
        if (!m_gamma_active && !upscale)
            return r;
        if (m_state == 0)
            m_state = init() ? 1 : -1;
        if (m_state < 0)
            return r;
        Core &c = *core();
        VkImageView in = source_view(src);

        if (m_gamma_active && m_gamma)
        {
            ensure_target(m_graded, src.width, src.height);
            const Slice lut = c.heap->alloc(sizeof(m_lut));
            memcpy(lut.ptr, m_lut.data(), sizeof(m_lut));
            c.heap->release(lut, c.recording_value());
            const VkDescriptorBufferInfo lb{lut.buffer, lut.offset, sizeof(m_lut)};
            dispatch(cb, m_gamma, in, m_graded, nullptr, 0, &lb, (src.width + 7) / 8, (src.height + 7) / 8);
            in = m_graded.view;
            r = {m_graded.image, m_graded.w, m_graded.h};
        }
        if (upscale && m_fsr) // m_fsr is cleared by init() when the FSR shaders are unavailable
        {
            ensure_target(m_scaled, out.width, out.height);
            ensure_target(m_sharpened, out.width, out.height);
            uint32_t easu[16], rcas[16] = {};
            fsr::easu_constants(easu, float(src.width), float(src.height), float(out.width), float(out.height));
            fsr::rcas_constants(rcas, m_sharpness);
            const uint32_t gx = (out.width + 15) / 16, gy = (out.height + 15) / 16;
            dispatch(cb, m_easu, in, m_scaled, easu, sizeof(easu), nullptr, gx, gy);
            dispatch(cb, m_rcas, m_scaled.view, m_sharpened, rcas, sizeof(rcas), nullptr, gx, gy);
            r = {m_sharpened.image, m_sharpened.w, m_sharpened.h};
        }
        // the blit (and the debug screenshot copy) read the result
        const VkMemoryBarrier2 mb = memory_barrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                                   VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT);
        VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        d.memoryBarrierCount = 1;
        d.pMemoryBarriers = &mb;
        vkCmdPipelineBarrier2(cb, &d);
        return r;
    }

    void PresentPass::destroy()
    {
        Core *c = core();
        if (!c)
            return;
        vkDeviceWaitIdle(c->device);
        destroy_target(m_graded);
        destroy_target(m_scaled);
        destroy_target(m_sharpened);
        if (m_src_view)
            vkDestroyImageView(c->device, m_src_view, nullptr);
        for (VkPipeline p : {m_gamma, m_easu, m_rcas})
            if (p)
                vkDestroyPipeline(c->device, p, nullptr);
        if (m_sampler)
            vkDestroySampler(c->device, m_sampler, nullptr);
        if (m_layout)
            vkDestroyPipelineLayout(c->device, m_layout, nullptr);
        if (m_set_layout)
            vkDestroyDescriptorSetLayout(c->device, m_set_layout, nullptr);
        *this = PresentPass();
    }
} // namespace xrvk
