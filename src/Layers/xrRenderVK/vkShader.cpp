// D3D shader-compiler entry points of the Vulkan build (D3DCompile, D3DCreateBlob,
// D3DGetInputSignatureBlob) and SPIR-V reflection into ShaderInfo. See vkShader.h for the bytecode and
// binding conventions; D3DReflect/D3DDisassemble are in vkReflection.cpp.
#include "vkShader.h"

#include <spirv_reflect.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>

#include "vkDxc.h"
#include "vkHlslRewrite.h"
#include "vk_d3dcompiler.h"

namespace xrvk
{
    struct Blob final : public ID3D10Blob
    {
        std::vector<uint8_t> bytes;
        bool has_interface(REFIID r) const override
        {
            return r == __uuidof(ID3D10Blob);
        }
    };

    ID3D10Blob *make_blob(const void *data, size_t size)
    {
        auto *b = new Blob;
        b->bytes.resize(size);
        if (data && size)
            memcpy(b->bytes.data(), data, size);
        return b;
    }

    bool decode_binding(uint32_t binding, Stage &s, RegClass &c, uint32_t &reg)
    {
        const uint32_t stage = binding / kBindingsPerStage, local = binding % kBindingsPerStage;
        if (stage >= uint32_t(Stage::Count))
            return false;
        for (uint32_t k = 0; k < 4; ++k)
            if (local >= kClassBase[k] && local < kClassBase[k] + kClassSlots[k])
            {
                s = Stage(stage);
                c = RegClass(k);
                reg = local - kClassBase[k];
                return true;
            }
        return false;
    }

    VkShaderStageFlagBits vk_stage(Stage s)
    {
        switch (s)
        {
        case Stage::Vertex:
            return VK_SHADER_STAGE_VERTEX_BIT;
        case Stage::Hull:
            return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case Stage::Domain:
            return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        case Stage::Geometry:
            return VK_SHADER_STAGE_GEOMETRY_BIT;
        case Stage::Pixel:
            return VK_SHADER_STAGE_FRAGMENT_BIT;
        default:
            return VK_SHADER_STAGE_COMPUTE_BIT;
        }
    }

    bool is_spirv(const void *data, size_t size)
    {
        uint32_t magic = 0;
        if (!data || size < 20 || size % 4)
            return false;
        memcpy(&magic, data, 4);
        return magic == SpvMagicNumber;
    }

    void split_semantic(const std::string &semantic, std::string &name, uint32_t &index)
    {
        size_t e = semantic.size();
        while (e > 0 && semantic[e - 1] >= '0' && semantic[e - 1] <= '9')
            --e;
        name = semantic.substr(0, e);
        index = e < semantic.size() ? uint32_t(std::stoul(semantic.substr(e))) : 0;
    }

    namespace
    {
        // ---- reflection ---------------------------------------------------------------------------
        struct Module
        {
            SpvReflectShaderModule m{};
            bool ok = false;
            Module(const void *data, size_t size, SpvReflectModuleFlags flags = SPV_REFLECT_MODULE_FLAG_NONE)
            {
                ok = spvReflectCreateShaderModule2(flags, size, data, &m) == SPV_REFLECT_RESULT_SUCCESS;
            }
            ~Module()
            {
                if (ok)
                    spvReflectDestroyShaderModule(&m);
            }
            std::vector<SpvReflectDescriptorBinding *> bindings() const
            {
                uint32_t n = 0;
                spvReflectEnumerateDescriptorBindings(&m, &n, nullptr);
                std::vector<SpvReflectDescriptorBinding *> v(n);
                spvReflectEnumerateDescriptorBindings(&m, &n, v.data());
                return v;
            }
            std::vector<SpvReflectInterfaceVariable *> io_variables(bool input) const
            {
                uint32_t n = 0;
                if (input)
                    spvReflectEnumerateInputVariables(&m, &n, nullptr);
                else
                    spvReflectEnumerateOutputVariables(&m, &n, nullptr);
                std::vector<SpvReflectInterfaceVariable *> v(n);
                if (input)
                    spvReflectEnumerateInputVariables(&m, &n, v.data());
                else
                    spvReflectEnumerateOutputVariables(&m, &n, v.data());
                return v;
            }
        };

        bool stage_of(SpvReflectShaderStageFlagBits s, Stage &out)
        {
            switch (s)
            {
            case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT:
                out = Stage::Vertex;
                return true;
            case SPV_REFLECT_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
                out = Stage::Hull;
                return true;
            case SPV_REFLECT_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
                out = Stage::Domain;
                return true;
            case SPV_REFLECT_SHADER_STAGE_GEOMETRY_BIT:
                out = Stage::Geometry;
                return true;
            case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT:
                out = Stage::Pixel;
                return true;
            case SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT:
                out = Stage::Compute;
                return true;
            default:
                return false;
            }
        }

        D3D_SHADER_VARIABLE_TYPE scalar_type(SpvReflectTypeFlags f, const SpvReflectNumericTraits &n)
        {
            if (f & SPV_REFLECT_TYPE_FLAG_BOOL)
                return D3D_SVT_BOOL;
            if (f & SPV_REFLECT_TYPE_FLAG_FLOAT)
                return n.scalar.width == 64 ? D3D_SVT_DOUBLE : D3D_SVT_FLOAT;
            if (f & SPV_REFLECT_TYPE_FLAG_INT)
                return n.scalar.signedness ? D3D_SVT_INT : D3D_SVT_UINT;
            return D3D_SVT_VOID;
        }

        const char *scalar_name(D3D_SHADER_VARIABLE_TYPE t)
        {
            switch (t)
            {
            case D3D_SVT_BOOL:
                return "bool";
            case D3D_SVT_INT:
                return "int";
            case D3D_SVT_UINT:
                return "uint";
            case D3D_SVT_DOUBLE:
                return "double";
            default:
                return "float";
            }
        }

        // One cbuffer member in D3D terms. DXC maps an HLSL RxC matrix to a SPIR-V matrix of R columns of C
        // components and flips the majorness decoration (HLSL row_major = SPIR-V ColMajor).
        ShaderVariable variable(const SpvReflectBlockVariable &b)
        {
            ShaderVariable v;
            v.name = b.name ? b.name : "";
            v.offset = b.offset;
            const SpvReflectTypeDescription *td = b.type_description;
            const SpvReflectTypeFlags f = td ? td->type_flags : 0;
            uint32_t elem_size = 0;
            if (f & SPV_REFLECT_TYPE_FLAG_STRUCT)
            {
                v.cls = D3D_SVC_STRUCT;
                v.type = D3D_SVT_VOID;
                v.members = b.member_count;
                v.rows = v.columns = 1;
                v.type_name = td && td->type_name ? td->type_name : "struct";
                elem_size = b.array.dims_count ? b.array.stride : b.size;
            }
            else
            {
                v.type = scalar_type(f, b.numeric);
                v.type_name = scalar_name(v.type);
                if (f & SPV_REFLECT_TYPE_FLAG_MATRIX)
                {
                    v.rows = b.numeric.matrix.column_count;
                    v.columns = b.numeric.matrix.row_count;
                    v.cls = (b.decoration_flags & SPV_REFLECT_DECORATION_ROW_MAJOR) ? D3D_SVC_MATRIX_COLUMNS : D3D_SVC_MATRIX_ROWS;
                    elem_size = v.cls == D3D_SVC_MATRIX_ROWS ? (v.rows - 1) * 16 + v.columns * 4 : (v.columns - 1) * 16 + v.rows * 4;
                    v.type_name += std::to_string(v.rows) + "x" + std::to_string(v.columns);
                }
                else if (f & SPV_REFLECT_TYPE_FLAG_VECTOR)
                {
                    v.cls = D3D_SVC_VECTOR;
                    v.rows = 1;
                    v.columns = b.numeric.vector.component_count;
                    elem_size = v.columns * 4;
                    v.type_name += std::to_string(v.columns);
                }
                else
                {
                    v.cls = D3D_SVC_SCALAR;
                    v.rows = v.columns = 1;
                    elem_size = 4;
                }
                if (v.type == D3D_SVT_DOUBLE)
                    elem_size *= 2;
            }
            if (b.array.dims_count)
            {
                v.elements = 1;
                for (uint32_t d = 0; d < b.array.dims_count; ++d)
                    v.elements *= b.array.dims[d];
                v.size = (v.elements - 1) * b.array.stride + elem_size;
            }
            else
                v.size = elem_size;
            return v;
        }

        D3D_SRV_DIMENSION dimension(const SpvReflectImageTraits &im)
        {
            switch (im.dim)
            {
            case SpvDim1D:
                return im.arrayed ? D3D_SRV_DIMENSION_TEXTURE1DARRAY : D3D_SRV_DIMENSION_TEXTURE1D;
            case SpvDim2D:
                if (im.ms)
                    return im.arrayed ? D3D_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D_SRV_DIMENSION_TEXTURE2DMS;
                return im.arrayed ? D3D_SRV_DIMENSION_TEXTURE2DARRAY : D3D_SRV_DIMENSION_TEXTURE2D;
            case SpvDim3D:
                return D3D_SRV_DIMENSION_TEXTURE3D;
            case SpvDimCube:
                return im.arrayed ? D3D_SRV_DIMENSION_TEXTURECUBEARRAY : D3D_SRV_DIMENSION_TEXTURECUBE;
            case SpvDimBuffer:
                return D3D_SRV_DIMENSION_BUFFER;
            default:
                return D3D_SRV_DIMENSION_UNKNOWN;
            }
        }

        D3D_SHADER_INPUT_TYPE storage_buffer_type(const SpvReflectDescriptorBinding &b, RegClass c)
        {
            switch (b.user_type)
            {
            case SPV_REFLECT_USER_TYPE_BYTE_ADDRESS_BUFFER:
                return D3D_SIT_BYTEADDRESS;
            case SPV_REFLECT_USER_TYPE_STRUCTURED_BUFFER:
                return D3D_SIT_STRUCTURED;
            case SPV_REFLECT_USER_TYPE_RW_BYTE_ADDRESS_BUFFER:
                return D3D_SIT_UAV_RWBYTEADDRESS;
            case SPV_REFLECT_USER_TYPE_RW_STRUCTURED_BUFFER:
                return D3D_SIT_UAV_RWSTRUCTURED;
            case SPV_REFLECT_USER_TYPE_APPEND_STRUCTURED_BUFFER:
                return D3D_SIT_UAV_APPEND_STRUCTURED;
            case SPV_REFLECT_USER_TYPE_CONSUME_STRUCTURED_BUFFER:
                return D3D_SIT_UAV_CONSUME_STRUCTURED;
            default:
                return c == RegClass::UAV ? D3D_SIT_UAV_RWSTRUCTURED : D3D_SIT_STRUCTURED;
            }
        }

        D3D_NAME system_value(const std::string &semantic)
        {
            static const struct
            {
                const char *name;
                D3D_NAME value;
            } table[] = {{"SV_POSITION", D3D_NAME_POSITION}, {"SV_CLIPDISTANCE", D3D_NAME_CLIP_DISTANCE}, {"SV_CULLDISTANCE", D3D_NAME_CULL_DISTANCE}, {"SV_RENDERTARGETARRAYINDEX", D3D_NAME_RENDER_TARGET_ARRAY_INDEX}, {"SV_VIEWPORTARRAYINDEX", D3D_NAME_VIEWPORT_ARRAY_INDEX}, {"SV_VERTEXID", D3D_NAME_VERTEX_ID}, {"SV_PRIMITIVEID", D3D_NAME_PRIMITIVE_ID}, {"SV_INSTANCEID", D3D_NAME_INSTANCE_ID}, {"SV_ISFRONTFACE", D3D_NAME_IS_FRONT_FACE}, {"SV_SAMPLEINDEX", D3D_NAME_SAMPLE_INDEX}, {"SV_TARGET", D3D_NAME_TARGET}, {"SV_DEPTH", D3D_NAME_DEPTH}, {"SV_COVERAGE", D3D_NAME_COVERAGE}};
            std::string up = semantic;
            for (char &ch : up)
                ch = char(toupper(static_cast<unsigned char>(ch)));
            for (const auto &e : table)
                if (up == e.name)
                    return e.value;
            return D3D_NAME_UNDEFINED;
        }

        ShaderParameter parameter(const SpvReflectInterfaceVariable &v)
        {
            ShaderParameter p;
            split_semantic(v.semantic ? v.semantic : "", p.semantic, p.index);
            p.builtin = v.built_in != -1;
            p.location = p.builtin ? UINT(-1) : v.location;
            p.components = v.numeric.vector.component_count ? v.numeric.vector.component_count : 1;
            const SpvReflectTypeFlags f = v.type_description ? v.type_description->type_flags : 0;
            if (f & SPV_REFLECT_TYPE_FLAG_FLOAT)
                p.component_type = D3D_REGISTER_COMPONENT_FLOAT32;
            else if (f & (SPV_REFLECT_TYPE_FLAG_INT | SPV_REFLECT_TYPE_FLAG_BOOL))
                p.component_type = v.numeric.scalar.signedness ? D3D_REGISTER_COMPONENT_SINT32 : D3D_REGISTER_COMPONENT_UINT32;
            p.system_value = system_value(p.semantic);
            return p;
        }

        // ---- D3DCompile ---------------------------------------------------------------------------
        bool stage_of_target(const char *target, Stage &s)
        {
            if (!target || strlen(target) < 6 || target[1] != 's' || target[2] != '_')
                return false;
            switch (target[0])
            {
            case 'v':
                s = Stage::Vertex;
                return true;
            case 'h':
                s = Stage::Hull;
                return true;
            case 'd':
                s = Stage::Domain;
                return true;
            case 'g':
                s = Stage::Geometry;
                return true;
            case 'p':
                s = Stage::Pixel;
                return true;
            case 'c':
                s = Stage::Compute;
                return true;
            default:
                return false;
            }
        }

        // Register class of a binding from its descriptor type (not from the binding number, which DXC may
        // have pushed past the class range).
        RegClass class_of(const SpvReflectDescriptorBinding &b)
        {
            switch (b.descriptor_type)
            {
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                return RegClass::CBuffer;
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
                return RegClass::Sampler;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                return RegClass::UAV;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                return b.user_type == SPV_REFLECT_USER_TYPE_STRUCTURED_BUFFER || b.user_type == SPV_REFLECT_USER_TYPE_BYTE_ADDRESS_BUFFER
                           ? RegClass::SRV
                           : RegClass::UAV;
            default:
                return RegClass::SRV;
            }
        }

        // FXC numbers implicit registers over the resources a shader uses; DXC (-fvk-auto-shift-bindings)
        // over all it declares, so e.g. one used texture of a common header ends up at t17, and a shader
        // that declares more than 16 samplers would leave the sampler range. Renumber each class densely,
        // keeping DXC's order. The engine takes every slot from the reflection, and binds cbuffer i of the
        // reflection at slot i (R_constant_table::parse), which this makes true as well.
        bool compact_bindings(std::vector<uint32_t> &code, Stage stage, std::string &error)
        {
            Module mod(code.data(), code.size() * 4, SPV_REFLECT_MODULE_FLAG_NO_COPY);
            if (!mod.ok)
            {
                error = "SPIR-V reflection failed";
                return false;
            }
            struct B
            {
                uint32_t binding, count, word;
            };
            std::vector<B> by_class[4];
            for (const SpvReflectDescriptorBinding *b : mod.bindings())
            {
                if (b->set != 0)
                {
                    error = std::string("descriptor set ") + std::to_string(b->set) + " used by " + (b->name ? b->name : "?");
                    return false;
                }
                by_class[uint32_t(class_of(*b))].push_back({b->binding, b->count ? b->count : 1, b->word_offset.binding});
            }
            static const char *const class_name[4] = {"constant buffers", "shader resources", "samplers", "UAVs"};
            for (uint32_t c = 0; c < 4; ++c)
            {
                auto &v = by_class[c];
                std::sort(v.begin(), v.end(), [](const B &a, const B &b) { return a.binding < b.binding; });
                uint32_t reg = 0;
                for (const B &b : v)
                {
                    if (reg + b.count > kClassSlots[c])
                    {
                        error = std::string("more ") + class_name[c] + " than D3D11 has slots";
                        return false;
                    }
                    code[b.word] = binding_of(stage, RegClass(c), reg);
                    reg += b.count;
                }
            }
            return true;
        }

        // D3D links stages by semantic, Vulkan by location, and DXC numbers locations per shader in
        // declaration order. So every inter-stage variable gets the location of its semantic from one
        // fixed table (the semantics the R4 shaders use; tests/vk_shaders prints them). An array or matrix
        // takes consecutive locations, like the consecutive semantic indices D3D gives it.
        bool interstage_location(const std::string &semantic, uint32_t index, uint32_t &location)
        {
            static const struct
            {
                const char *name;
                uint32_t base, count;
            } table[] = {{"TEXCOORD", 0, 16}, {"COLOR", 16, 2}, {"POSITION", 18, 2}, {"NORMAL", 20, 2}, {"TANGENT", 22, 2}, {"BINORMAL", 24, 2}, {"FOG", 26, 1}, {"SSS_EXTRA", 27, 1}, {"VEL_CURRENT", 28, 1}, {"VEL_PREVIOUS", 29, 1}};
            std::string up = semantic;
            for (char &ch : up)
                ch = char(toupper(static_cast<unsigned char>(ch)));
            for (const auto &e : table)
                if (up == e.name && index < e.count)
                {
                    location = e.base + index;
                    return true;
                }
            return false;
        }

        bool assign_interstage_locations(std::vector<uint32_t> &code, Stage stage, std::string &error)
        {
            Module mod(code.data(), code.size() * 4, SPV_REFLECT_MODULE_FLAG_NO_COPY);
            if (!mod.ok)
            {
                error = "SPIR-V reflection failed";
                return false;
            }
            // vertex inputs follow the input layout, pixel outputs are SV_Target<n> = location n already
            for (bool input : {true, false})
            {
                if ((input && (stage == Stage::Vertex || stage == Stage::Compute)) || (!input && (stage == Stage::Pixel || stage == Stage::Compute)))
                    continue;
                for (const SpvReflectInterfaceVariable *v : mod.io_variables(input))
                {
                    if (v->built_in != -1)
                        continue;
                    if (v->decoration_flags & SPV_REFLECT_DECORATION_PATCH)
                    {
                        error = "patch constants (tessellation) are not supported yet";
                        return false;
                    }
                    std::string name;
                    uint32_t index = 0, location = 0;
                    split_semantic(v->semantic ? v->semantic : "", name, index);
                    if (!interstage_location(name, index, location))
                    {
                        error = std::string("inter-stage semantic without a Vulkan location: ") + (v->semantic ? v->semantic : "(none)") +
                                " (add it to interstage_location in vkShader.cpp)";
                        return false;
                    }
                    code[v->word_offset.location] = location;
                }
            }
            return true;
        }

        HRESULT set_error(ID3DBlob **out, const std::string &text)
        {
            if (out)
                *out = text.empty() ? nullptr : make_blob(text.c_str(), text.size() + 1);
            return E_FAIL;
        }
    } // namespace

    bool reflect_spirv(const void *data, size_t size, ShaderInfo &info, std::string &error)
    {
        info = {};
        if (!is_spirv(data, size))
        {
            error = "not a SPIR-V module";
            return false;
        }
        Module mod(data, size);
        if (!mod.ok || !stage_of(mod.m.shader_stage, info.stage))
        {
            error = "SPIR-V reflection failed";
            return false;
        }
        for (const SpvReflectDescriptorBinding *b : mod.bindings())
        {
            Stage s;
            ShaderResource r;
            RegClass c;
            if (b->set != 0 || !decode_binding(b->binding, s, c, r.slot) || s != info.stage)
            {
                error = std::string("binding outside the backend's layout: ") + (b->name ? b->name : "?");
                return false;
            }
            r.name = b->name ? b->name : "";
            r.binding = b->binding;
            r.count = b->count ? b->count : 1;
            r.descriptor = VkDescriptorType(b->descriptor_type);
            switch (b->descriptor_type)
            {
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
                r.type = D3D_SIT_CBUFFER;
                ShaderCBuffer cb;
                cb.name = r.name;
                cb.slot = r.slot;
                cb.size = (b->block.size + 15) & ~15u;
                for (uint32_t i = 0; i < b->block.member_count; ++i)
                    cb.vars.push_back(variable(b->block.members[i]));
                info.cbuffers.push_back(std::move(cb));
                break;
            }
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
                r.type = D3D_SIT_SAMPLER;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                r.type = D3D_SIT_TEXTURE;
                r.dimension = dimension(b->image);
                r.return_type = D3D_RETURN_TYPE_FLOAT;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                r.type = D3D_SIT_UAV_RWTYPED;
                r.dimension = dimension(b->image);
                r.return_type = D3D_RETURN_TYPE_FLOAT;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                if (r.name.compare(0, 12, "counter.var.") == 0)
                {
                    error = "UAV counters (Append/Consume buffers, IncrementCounter) are not supported yet: " + r.name;
                    return false;
                }
                r.type = storage_buffer_type(*b, c);
                r.dimension = D3D_SRV_DIMENSION_BUFFER;
                r.return_type = D3D_RETURN_TYPE_MIXED;
                break;
            default:
                error = "unsupported descriptor type of " + r.name;
                return false;
            }
            info.resources.push_back(std::move(r));
        }
        std::sort(info.cbuffers.begin(), info.cbuffers.end(), [](const ShaderCBuffer &a, const ShaderCBuffer &b)
                  {
                      return a.slot < b.slot;
                  });
        std::sort(info.resources.begin(), info.resources.end(), [](const ShaderResource &a, const ShaderResource &b)
                  {
                      return a.binding < b.binding;
                  });
        for (bool input : {true, false})
            for (const SpvReflectInterfaceVariable *v : mod.io_variables(input))
            {
                if (!v->semantic && v->built_in != -1)
                    continue; // e.g. gl_PerVertex members DXC adds without an HLSL semantic
                (input ? info.inputs : info.outputs).push_back(parameter(*v));
            }
        auto by_location = [](const ShaderParameter &a, const ShaderParameter &b)
        {
            return a.location < b.location;
        };
        std::stable_sort(info.inputs.begin(), info.inputs.end(), by_location);
        std::stable_sort(info.outputs.begin(), info.outputs.end(), by_location);
        if (info.stage == Stage::Compute && mod.m.entry_point_count)
        {
            info.threads[0] = mod.m.entry_points[0].local_size.x;
            info.threads[1] = mod.m.entry_points[0].local_size.y;
            info.threads[2] = mod.m.entry_points[0].local_size.z;
        }
        return true;
    }

    std::vector<uint32_t> strip_reflection_info(const void *data, size_t size)
    {
        std::vector<uint32_t> in(size / 4), out;
        memcpy(in.data(), data, in.size() * 4);
        if (in.size() < 5)
            return in;
        out.assign(in.begin(), in.begin() + 5);
        for (size_t i = 5; i < in.size();)
        {
            const uint32_t count = in[i] >> 16, op = in[i] & 0xffff;
            if (count == 0 || i + count > in.size())
                return in; // malformed: leave it to the driver/validation to report
            bool drop = false;
            if (op == SpvOpExtension)
            {
                const char *name = reinterpret_cast<const char *>(&in[i + 1]);
                drop = !strcmp(name, "SPV_GOOGLE_hlsl_functionality1") || !strcmp(name, "SPV_GOOGLE_user_type");
            }
            else if (op == SpvOpDecorateString && count >= 3)
                drop = in[i + 2] == SpvDecorationUserSemantic || in[i + 2] == SpvDecorationUserTypeGOOGLE;
            else if (op == SpvOpMemberDecorateString && count >= 4)
                drop = in[i + 3] == SpvDecorationUserSemantic || in[i + 3] == SpvDecorationUserTypeGOOGLE;
            else if (op == SpvOpDecorateId && count >= 3)
                drop = in[i + 2] == SpvDecorationCounterBuffer;
            if (!drop)
                out.insert(out.end(), in.begin() + i, in.begin() + i + count);
            i += count;
        }
        return out;
    }

    // Signature blob: "XVIS", version 1, count, then per input: location, index, components, component
    // type, name length, name bytes padded to 4.
    static constexpr uint32_t kSignatureMagic = 0x53495658, kSignatureVersion = 1;

    bool parse_input_signature(const void *data, size_t size, std::vector<ShaderParameter> &inputs)
    {
        inputs.clear();
        const auto *p = static_cast<const uint8_t *>(data);
        size_t at = 0;
        auto u32 = [&](uint32_t &v)
        {
            if (at + 4 > size)
                return false;
            memcpy(&v, p + at, 4);
            at += 4;
            return true;
        };
        uint32_t magic, version, count;
        if (!data || !u32(magic) || !u32(version) || !u32(count) || magic != kSignatureMagic || version != kSignatureVersion)
            return false;
        for (uint32_t i = 0; i < count; ++i)
        {
            ShaderParameter s;
            uint32_t ct, len;
            if (!u32(s.location) || !u32(s.index) || !u32(s.components) || !u32(ct) || !u32(len) || at + len > size)
                return false;
            s.component_type = D3D_REGISTER_COMPONENT_TYPE(ct);
            s.semantic.assign(reinterpret_cast<const char *>(p + at), len);
            at += (len + 3) & ~size_t(3);
            inputs.push_back(std::move(s));
        }
        return true;
    }
} // namespace xrvk

using namespace xrvk;

void *ID3D10Blob::GetBufferPointer()
{
    return static_cast<Blob *>(this)->bytes.data();
}

SIZE_T ID3D10Blob::GetBufferSize()
{
    return static_cast<Blob *>(this)->bytes.size();
}

extern "C" HRESULT WINAPI D3DCreateBlob(SIZE_T data_size, ID3DBlob **blob)
{
    if (!blob)
        return E_INVALIDARG;
    *blob = make_blob(nullptr, data_size);
    return S_OK;
}

extern "C" HRESULT WINAPI D3DCompile(const void *data, SIZE_T data_size, const char *, const D3D_SHADER_MACRO *defines,
                                     ID3DInclude *include, const char *entrypoint, const char *target, UINT sflags, UINT, ID3DBlob **shader,
                                     ID3DBlob **error_messages)
{
    if (shader)
        *shader = nullptr;
    if (error_messages)
        *error_messages = nullptr;
    Stage stage;
    if (!data || !shader || !entrypoint || !stage_of_target(target, stage))
        return set_error(error_messages, std::string("D3DCompile: bad arguments (target ") + (target ? target : "null") + ")");

    std::vector<dxc::Define> defs;
    for (const D3D_SHADER_MACRO *d = defines; d && d->Name; ++d)
        defs.push_back({d->Name, d->Definition ? d->Definition : ""});

    // D3D_COMPILE_STANDARD_FILE_INCLUDE is ((ID3DInclude*)1): files relative to the working directory
    const bool standard = include == reinterpret_cast<ID3DInclude *>(uintptr_t(1));
    const dxc::IncludeFn open = [include, standard](const std::string &name, std::string &text)
    {
        if (standard)
        {
            std::ifstream f(name, std::ios::binary);
            if (!f)
                return false;
            text.assign(std::istreambuf_iterator<char>(f), {});
            return true;
        }
        const void *p = nullptr;
        UINT n = 0;
        if (!include || FAILED(include->Open(D3D_INCLUDE_LOCAL, name.c_str(), nullptr, &p, &n)))
            return false;
        text.assign(static_cast<const char *>(p), n);
        include->Close(p);
        return true;
    };

    std::string pre, errors;
    if (!dxc::preprocess(std::string(static_cast<const char *>(data), data_size), defs, open, pre, errors))
        return set_error(error_messages, errors);
    const std::string src = hlsl::rewrite_compat(pre);

    std::vector<std::string> args{"-spirv", "-fspv-target-env=vulkan1.3", "-HV", "2018", "-fvk-use-dx-layout",
                                  "-fspv-reflect", "-fvk-use-dx-position-w", "-fvk-auto-shift-bindings", "-E", entrypoint,
                                  "-fspv-entrypoint-name=main", "-T", std::string(target, 2) + "_6_0"};
    static const char *const shift_option[4] = {"-fvk-b-shift", "-fvk-t-shift", "-fvk-s-shift", "-fvk-u-shift"};
    for (uint32_t c = 0; c < 4; ++c)
    {
        args.push_back(shift_option[c]);
        args.push_back(std::to_string(binding_of(stage, RegClass(c), 0)));
        args.push_back("0");
    }
    args.push_back((sflags & D3DCOMPILE_PACK_MATRIX_ROW_MAJOR) ? "-Zpr" : "-Zpc");
    const bool no_opt = (sflags & D3DCOMPILE_SKIP_OPTIMIZATION) ||
                        (sflags & D3DCOMPILE_OPTIMIZATION_LEVEL2) == D3DCOMPILE_OPTIMIZATION_LEVEL0;
    args.push_back(no_opt ? "-O0" : "-O3");
    if (sflags & D3DCOMPILE_WARNINGS_ARE_ERRORS)
        args.push_back("-WX");

    std::vector<uint32_t> code;
    std::string compile_errors;
    if (!dxc::compile(src, args, code, compile_errors))
        return set_error(error_messages, errors + compile_errors);
    errors += compile_errors;
    std::string fix_error;
    if (!compact_bindings(code, stage, fix_error) || !assign_interstage_locations(code, stage, fix_error))
        return set_error(error_messages, "D3DCompile: " + fix_error);

    *shader = make_blob(code.data(), code.size() * 4);
    if (error_messages && !errors.empty())
        *error_messages = make_blob(errors.c_str(), errors.size() + 1);
    return S_OK;
}

extern "C" HRESULT WINAPI D3DGetInputSignatureBlob(const void *data, SIZE_T data_size, ID3DBlob **blob)
{
    if (!blob)
        return E_INVALIDARG;
    *blob = nullptr;
    ShaderInfo info;
    std::string error;
    if (!reflect_spirv(data, data_size, info, error))
        return E_FAIL;
    std::vector<uint8_t> out;
    auto put = [&out](uint32_t v)
    {
        const size_t at = out.size();
        out.resize(at + 4);
        memcpy(out.data() + at, &v, 4);
    };
    uint32_t count = 0;
    for (const auto &p : info.inputs)
        count += !p.builtin;
    put(kSignatureMagic);
    put(kSignatureVersion);
    put(count);
    for (const auto &p : info.inputs)
    {
        if (p.builtin)
            continue;
        put(p.location);
        put(p.index);
        put(p.components);
        put(uint32_t(p.component_type));
        put(uint32_t(p.semantic.size()));
        out.insert(out.end(), p.semantic.begin(), p.semantic.end());
        out.resize((out.size() + 3) & ~size_t(3), 0);
    }
    *blob = make_blob(out.data(), out.size());
    return S_OK;
}
