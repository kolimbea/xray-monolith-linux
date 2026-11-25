// ID3D11Device::Create*Shader and the programs built from them (see vkObjects.h, vkShader.h).
#include "vkObjects.h"

#include <spirv_reflect.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <mutex>

namespace xrvk
{
    // Entries (and the modules they hold) live as long as the device: X-Ray creates its shaders once at
    // load time, so the set of programs stays bounded by the shader combinations the game uses.
    struct ProgramCache
    {
        std::mutex lock;
        std::map<std::array<uint64_t, size_t(Stage::Count)>, std::unique_ptr<Program>> programs;
    };

    Device::Device() : programs(std::make_unique<ProgramCache>())
    {
    }

    Device::~Device()
    {
        if (dxgi)
            dxgi->Release();
        if (Core *c = core(); c && !programs->programs.empty())
            vkQueueWaitIdle(c->queue); // programs may still be referenced by recorded work
    }

    Program::~Program()
    {
        Core *c = core();
        if (!c)
            return;
        for (VkShaderEXT s : shaders)
            if (s)
                c->ext.destroy_shader(c->device, s, nullptr);
        if (layout)
            vkDestroyPipelineLayout(c->device, layout, nullptr);
        if (set_layout)
            vkDestroyDescriptorSetLayout(c->device, set_layout, nullptr);
    }

    namespace
    {
        const char *stage_name(Stage s)
        {
            static const char *const n[] = {"vertex", "hull", "domain", "geometry", "pixel", "compute"};
            return n[size_t(s)];
        }

        // Inter-stage variables of one side of a stage interface: where the location word is, the canonical
        // location D3DCompile gave the semantic, and how many locations the variable takes.
        struct IoVar
        {
            size_t word;
            uint32_t location, slots;
        };

        bool io_vars(const std::vector<uint32_t> &code, bool input, bool per_vertex, std::vector<IoVar> &out)
        {
            SpvReflectShaderModule m{};
            if (spvReflectCreateShaderModule2(SPV_REFLECT_MODULE_FLAG_NO_COPY, code.size() * 4, code.data(), &m) != SPV_REFLECT_RESULT_SUCCESS)
                return false;
            uint32_t n = 0;
            std::vector<SpvReflectInterfaceVariable *> vars;
            if (input)
            {
                spvReflectEnumerateInputVariables(&m, &n, nullptr);
                vars.resize(n);
                spvReflectEnumerateInputVariables(&m, &n, vars.data());
            }
            else
            {
                spvReflectEnumerateOutputVariables(&m, &n, nullptr);
                vars.resize(n);
                spvReflectEnumerateOutputVariables(&m, &n, vars.data());
            }
            for (const SpvReflectInterfaceVariable *v : vars)
            {
                if (v->built_in != -1)
                    continue;
                uint32_t slots = 1;
                // the outer array of hull/domain/geometry inputs (and hull outputs) is per vertex, not locations
                for (uint32_t d = per_vertex ? 1 : 0; d < v->array.dims_count; ++d)
                    slots *= v->array.dims[d];
                if (v->type_description && (v->type_description->type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX))
                    slots *= v->numeric.matrix.column_count;
                out.push_back({v->word_offset.location, v->location, slots});
            }
            spvReflectDestroyShaderModule(&m);
            return true;
        }

        // D3DCompile gives each semantic a fixed canonical location (vkShader.cpp), up to 29, which is more
        // than the 16 locations Vulkan guarantees per interface (RADV: maxGeometryInputComponents = 64).
        // A program knows both sides of each interface, so it renumbers the locations densely there, keeping
        // the canonical order (consecutive locations of an array stay consecutive).
        bool pack_interstage_locations(std::vector<uint32_t> (&code)[size_t(Stage::Count)])
        {
            std::vector<size_t> chain;
            for (Stage s : {Stage::Vertex, Stage::Hull, Stage::Domain, Stage::Geometry, Stage::Pixel})
                if (!code[size_t(s)].empty())
                    chain.push_back(size_t(s));
            auto per_vertex_in = [](size_t s) { return s == size_t(Stage::Hull) || s == size_t(Stage::Domain) || s == size_t(Stage::Geometry); };
            // interface k sits in front of chain[k]; interface 0 has no producer unless the chain starts
            // with the vertex shader (whose inputs follow the input layout instead)
            for (size_t k = chain.empty() || chain[0] == size_t(Stage::Vertex) ? 1 : 0; k <= chain.size(); ++k)
            {
                const size_t prod = k > 0 ? chain[k - 1] : SIZE_MAX;
                const size_t cons = k < chain.size() ? chain[k] : SIZE_MAX;
                if (prod == size_t(Stage::Pixel))
                    break;
                std::vector<IoVar> outs, ins;
                if (prod != SIZE_MAX && !io_vars(code[prod], false, prod == size_t(Stage::Hull), outs))
                    return false;
                if (cons != SIZE_MAX && !io_vars(code[cons], true, per_vertex_in(cons), ins))
                    return false;
                std::vector<uint32_t> used;
                for (const auto *list : {&outs, &ins})
                    for (const IoVar &v : *list)
                        for (uint32_t i = 0; i < v.slots; ++i)
                            used.push_back(v.location + i);
                std::sort(used.begin(), used.end());
                used.erase(std::unique(used.begin(), used.end()), used.end());
                auto dense = [&used](uint32_t loc) { return uint32_t(std::lower_bound(used.begin(), used.end(), loc) - used.begin()); };
                for (const IoVar &v : outs)
                    code[prod][v.word] = dense(v.location);
                for (const IoVar &v : ins)
                    code[cons][v.word] = dense(v.location);
            }
            return true;
        }

        std::unique_ptr<Program> create_program(const std::shared_ptr<const ShaderModule> (&stages)[size_t(Stage::Count)])
        {
            Core *c = core();
            auto p = std::make_unique<Program>();
            uint32_t descriptors = 0;
            for (size_t s = 0; s < size_t(Stage::Count); ++s)
            {
                p->modules[s] = stages[s];
                if (!stages[s])
                    continue;
                for (const ShaderResource &r : stages[s]->info.resources)
                {
                    VkDescriptorSetLayoutBinding b{};
                    b.binding = r.binding;
                    b.descriptorType = r.descriptor;
                    b.descriptorCount = r.count;
                    b.stageFlags = vk_stage(Stage(s));
                    p->bindings.push_back(b);
                    p->resources.push_back(&r);
                    descriptors += r.count;
                }
            }
            if (descriptors > c->max_push_descriptors)
            {
                log("! vk: program needs %u descriptors, the push-descriptor limit is %u", descriptors, c->max_push_descriptors);
                return nullptr;
            }
            VkDescriptorSetLayoutCreateInfo sli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            sli.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
            sli.bindingCount = uint32_t(p->bindings.size());
            sli.pBindings = p->bindings.data();
            XRVK_CHECK(vkCreateDescriptorSetLayout(c->device, &sli, nullptr, &p->set_layout));
            VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            pli.setLayoutCount = 1;
            pli.pSetLayouts = &p->set_layout;
            XRVK_CHECK(vkCreatePipelineLayout(c->device, &pli, nullptr, &p->layout));

            std::vector<uint32_t> code[size_t(Stage::Count)]; // this program's copies (locations packed)
            for (size_t s = 0; s < size_t(Stage::Count); ++s)
                if (stages[s])
                    code[s] = stages[s]->code;
            if (!pack_interstage_locations(code))
            {
                log("! vk: program: SPIR-V reflection of the stage interfaces failed");
                return nullptr;
            }
            std::vector<VkShaderCreateInfoEXT> infos;
            std::vector<size_t> which;
            for (size_t s = 0; s < size_t(Stage::Count); ++s)
            {
                if (!stages[s])
                    continue;
                VkShaderCreateInfoEXT i{VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT};
                i.stage = vk_stage(Stage(s));
                i.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
                i.codeSize = code[s].size() * 4;
                i.pCode = code[s].data();
                i.pName = "main";
                i.setLayoutCount = 1;
                i.pSetLayouts = &p->set_layout;
                infos.push_back(i);
                which.push_back(s);
            }
            if (infos.empty())
                return nullptr;
            for (size_t k = 0; k < infos.size(); ++k)
            {
                // the next stage of this program; a last pre-rasterization stage can be followed by a pixel shader
                if (which[k] == size_t(Stage::Pixel) || which[k] == size_t(Stage::Compute))
                    continue;
                infos[k].nextStage = k + 1 < infos.size() ? infos[k + 1].stage : VK_SHADER_STAGE_FRAGMENT_BIT;
                if (infos.size() > 1)
                    infos[k].flags |= VK_SHADER_CREATE_LINK_STAGE_BIT_EXT;
            }
            if (infos.size() > 1)
                infos.back().flags |= VK_SHADER_CREATE_LINK_STAGE_BIT_EXT;
            std::vector<VkShaderEXT> out(infos.size());
            const VkResult r = c->ext.create_shaders(c->device, uint32_t(infos.size()), infos.data(), nullptr, out.data());
            if (r != VK_SUCCESS)
            {
                std::string names;
                for (size_t s : which)
                    names += std::string(names.empty() ? "" : "+") + stage_name(Stage(s));
                log("! vk: vkCreateShadersEXT (%s) failed: %d", names.c_str(), int(r));
                for (VkShaderEXT s : out)
                    if (s)
                        c->ext.destroy_shader(c->device, s, nullptr);
                return nullptr;
            }
            for (size_t k = 0; k < which.size(); ++k)
                p->shaders[which[k]] = out[k];
            return p;
        }

        template <class T>
        HRESULT create_shader(ID3D11Device *dev, Stage stage, const void *code, SIZE_T size, T **out)
        {
            static std::atomic<uint64_t> next_id{1};
            if (!out)
                return S_FALSE; // D3D11: validate only
            *out = nullptr;
            // anything else (DXBC from the R4 renderer's cache) is rejected so the engine recompiles
            if (!is_spirv(code, size))
                return E_INVALIDARG;
            auto m = std::make_shared<ShaderModule>();
            std::string error;
            if (!reflect_spirv(code, size, m->info, error))
            {
                log("! vk: Create%sShader: %s", stage_name(stage), error.c_str());
                return E_INVALIDARG;
            }
            if (m->info.stage != stage)
            {
                log("! vk: Create%sShader: the bytecode is a %s shader", stage_name(stage), stage_name(m->info.stage));
                return E_INVALIDARG;
            }
            m->code = strip_reflection_info(code, size);
            m->id = next_id++;
            auto *s = new Shader<T>();
            s->xrvk_device = dev;
            s->module = std::move(m);
            *out = s;
            return S_OK;
        }
    } // namespace

    const Program *Device::program(const std::shared_ptr<const ShaderModule> (&stages)[size_t(Stage::Count)])
    {
        std::array<uint64_t, size_t(Stage::Count)> key{};
        for (size_t s = 0; s < key.size(); ++s)
            key[s] = stages[s] ? stages[s]->id : 0;
        std::lock_guard<std::mutex> g(programs->lock);
        auto it = programs->programs.find(key);
        if (it == programs->programs.end())
            it = programs->programs.emplace(key, create_program(stages)).first; // a failure is cached as null
        return it->second.get();
    }
} // namespace xrvk

using namespace xrvk;

HRESULT ID3D11Device::CreateVertexShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *, ID3D11VertexShader **ppVertexShader)
{
    return create_shader(this, Stage::Vertex, pShaderBytecode, BytecodeLength, ppVertexShader);
}
HRESULT ID3D11Device::CreateHullShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *, ID3D11HullShader **ppHullShader)
{
    return create_shader(this, Stage::Hull, pShaderBytecode, BytecodeLength, ppHullShader);
}
HRESULT ID3D11Device::CreateDomainShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *, ID3D11DomainShader **ppDomainShader)
{
    return create_shader(this, Stage::Domain, pShaderBytecode, BytecodeLength, ppDomainShader);
}
HRESULT ID3D11Device::CreateGeometryShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *, ID3D11GeometryShader **ppGeometryShader)
{
    return create_shader(this, Stage::Geometry, pShaderBytecode, BytecodeLength, ppGeometryShader);
}
HRESULT ID3D11Device::CreatePixelShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *, ID3D11PixelShader **ppPixelShader)
{
    return create_shader(this, Stage::Pixel, pShaderBytecode, BytecodeLength, ppPixelShader);
}
HRESULT ID3D11Device::CreateComputeShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *, ID3D11ComputeShader **ppComputeShader)
{
    return create_shader(this, Stage::Compute, pShaderBytecode, BytecodeLength, ppComputeShader);
}
