// Shaders of the Vulkan backend: the D3D shader-compiler API on DXC/SPIR-V (vkShader.cpp), reflection
// (vkReflection.cpp) and the shader objects behind ID3D11*Shader.
//
// Bytecode = SPIR-V as DXC emits it with -fspv-reflect (keeps HLSL semantics for reflection), after the
// fix-ups of D3DCompile. Every D3D register maps to one binding of a single push-descriptor set:
//   binding = stage * kBindingsPerStage + class base + register
// (classes: b = cbuffers, t = SRVs, s = samplers, u = UAVs). A program (the shader stages bound for a
// draw) gets the union of its stages' bindings as one set layout.
#pragma once
#include "vkCommonTypes.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace xrvk
{
    enum class Stage : uint8_t
    {
        Vertex,
        Hull,
        Domain,
        Geometry,
        Pixel,
        Compute,
        Count
    };
    enum class RegClass : uint8_t
    {
        CBuffer,
        SRV,
        Sampler,
        UAV
    };

    constexpr uint32_t kBindingsPerStage = 256;
    constexpr uint32_t kClassBase[4] = {0, 16, 16 + D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,
                                        16 + D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT + D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT};
    constexpr uint32_t kClassSlots[4] = {D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
                                         D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, D3D11_1_UAV_SLOT_COUNT};
    static_assert(kClassBase[3] + kClassSlots[3] <= kBindingsPerStage, "binding classes overlap the next stage");

    constexpr uint32_t binding_of(Stage s, RegClass c, uint32_t reg)
    {
        return uint32_t(s) * kBindingsPerStage + kClassBase[uint32_t(c)] + reg;
    }
    // inverse of binding_of; false if `binding` is not in any class range
    bool decode_binding(uint32_t binding, Stage &s, RegClass &c, uint32_t &reg);
    VkShaderStageFlagBits vk_stage(Stage s);

    struct ShaderVariable
    {
        std::string name, type_name;
        uint32_t offset = 0, size = 0;
        D3D_SHADER_VARIABLE_CLASS cls = D3D_SVC_SCALAR;
        D3D_SHADER_VARIABLE_TYPE type = D3D_SVT_VOID;
        uint32_t rows = 0, columns = 0, elements = 0, members = 0;
    };
    struct ShaderCBuffer
    {
        std::string name;
        uint32_t slot = 0, size = 0;
        std::vector<ShaderVariable> vars;
    };
    struct ShaderResource
    {
        std::string name;
        D3D_SHADER_INPUT_TYPE type = D3D_SIT_CBUFFER;
        uint32_t slot = 0, count = 1, binding = 0;
        D3D_SRV_DIMENSION dimension = D3D_SRV_DIMENSION_UNKNOWN;
        D3D_RESOURCE_RETURN_TYPE return_type = D3D_RESOURCE_RETURN_TYPE(0);
        VkDescriptorType descriptor = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    };
    struct ShaderParameter
    {
        std::string semantic; // without the index: "TEXCOORD", "SV_Position"
        uint32_t index = 0, location = 0, components = 0;
        D3D_NAME system_value = D3D_NAME_UNDEFINED;
        D3D_REGISTER_COMPONENT_TYPE component_type = D3D_REGISTER_COMPONENT_UNKNOWN;
        bool builtin = false; // a SPIR-V built-in (no location)
    };
    struct ShaderInfo
    {
        Stage stage = Stage::Vertex;
        std::vector<ShaderCBuffer> cbuffers;   // ordered by slot
        std::vector<ShaderResource> resources; // all bindings, cbuffers included (D3D's BoundResources)
        std::vector<ShaderParameter> inputs, outputs;
        uint32_t threads[3] = {};
    };

    // An ID3DBlob holding a copy of `data` (zero-filled if null), reference count 1.
    ID3D10Blob *make_blob(const void *data, size_t size);

    bool is_spirv(const void *data, size_t size);
    bool reflect_spirv(const void *data, size_t size, ShaderInfo &info, std::string &error);
    // The module for vkCreateShadersEXT: the reflection-only SPV_GOOGLE decorations removed.
    std::vector<uint32_t> strip_reflection_info(const void *data, size_t size);

    // The "input signature blob" of D3DGetInputSignatureBlob in this backend: the vertex shader's
    // non-built-in inputs, read back by CreateInputLayout.
    bool parse_input_signature(const void *data, size_t size, std::vector<ShaderParameter> &inputs);

    // `semantic` like "TEXCOORD3" split into name and index
    void split_semantic(const std::string &semantic, std::string &name, uint32_t &index);
} // namespace xrvk
