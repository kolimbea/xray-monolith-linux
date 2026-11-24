// ID3D11ShaderReflection over SPIR-V (ShaderInfo from vkShader.cpp), D3DReflect and D3DDisassemble.
// As in d3dcompiler, only the reflection object is reference counted; constant buffers, variables and
// types are owned by it, and lookups out of range return "null" objects whose GetDesc fails.
#include "vkShader.h"

#include <cstdio>
#include <deque>
#include <cstring>
#include <memory>

namespace xrvk
{
    struct RType final : public ID3D11ShaderReflectionType
    {
        D3D11_SHADER_TYPE_DESC desc{};
        bool valid = false;
    };
    struct RCBuffer;
    struct RVariable final : public ID3D11ShaderReflectionVariable
    {
        D3D11_SHADER_VARIABLE_DESC desc{};
        RType *type = nullptr;
        RCBuffer *buffer = nullptr;
        bool valid = false;
    };
    struct RCBuffer final : public ID3D11ShaderReflectionConstantBuffer
    {
        D3D11_SHADER_BUFFER_DESC desc{};
        std::vector<RVariable *> vars;
        bool valid = false;
    };

    RType g_null_type;
    RCBuffer g_null_cbuffer;
    RVariable g_null_variable;

    struct Reflection final : public ID3D11ShaderReflection
    {
        ShaderInfo info; // owns every string the descriptors point to
        D3D11_SHADER_DESC desc{};
        std::deque<RType> types; // deque: element addresses stay put
        std::deque<RVariable> variables;
        std::deque<RCBuffer> cbuffers;
        std::vector<D3D11_SHADER_INPUT_BIND_DESC> bindings;
        std::vector<D3D11_SIGNATURE_PARAMETER_DESC> inputs, outputs;

        bool has_interface(REFIID r) const override
        {
            return r == IID_ID3D11ShaderReflection;
        }
        void build();
    };

    namespace
    {
        // D3D11_SHADER_VERSION_TYPE (d3d11shader.h): pixel 0, vertex 1, geometry 2, hull 3, domain 4, compute 5
        uint32_t shader_version(Stage s)
        {
            static const uint32_t type[] = {1, 3, 4, 2, 0, 5}; // in Stage order
            return (type[uint32_t(s)] << 16) | (5 << 4) | 0;
        }

        void fill_signature(const std::vector<ShaderParameter> &in, std::vector<D3D11_SIGNATURE_PARAMETER_DESC> &out)
        {
            for (const auto &p : in)
            {
                D3D11_SIGNATURE_PARAMETER_DESC d{};
                d.SemanticName = p.semantic.c_str();
                d.SemanticIndex = p.index;
                d.Register = p.location;
                d.SystemValueType = p.system_value;
                d.ComponentType = p.component_type;
                d.Mask = d.ReadWriteMask = BYTE((1u << p.components) - 1);
                out.push_back(d);
            }
        }
    } // namespace

    void Reflection::build()
    {
        desc.Version = shader_version(info.stage);
        desc.Creator = "DXC (SPIR-V), X-Ray Vulkan backend";
        desc.ConstantBuffers = UINT(info.cbuffers.size());
        desc.BoundResources = UINT(info.resources.size());
        for (const ShaderCBuffer &cb : info.cbuffers)
        {
            RCBuffer &c = cbuffers.emplace_back();
            c.valid = true;
            c.desc.Name = cb.name.c_str();
            c.desc.Type = D3D_CT_CBUFFER;
            c.desc.Variables = UINT(cb.vars.size());
            c.desc.Size = cb.size;
            for (const ShaderVariable &sv : cb.vars)
            {
                RType &t = types.emplace_back();
                t.valid = true;
                t.desc.Class = sv.cls;
                t.desc.Type = sv.type;
                t.desc.Rows = sv.rows;
                t.desc.Columns = sv.columns;
                t.desc.Elements = sv.elements;
                t.desc.Members = sv.members;
                t.desc.Name = sv.type_name.c_str(); // per reflection, as with d3dcompiler (see dx10ConstantBuffer::Similar)
                RVariable &v = variables.emplace_back();
                v.valid = true;
                v.type = &t;
                v.buffer = &c;
                v.desc.Name = sv.name.c_str();
                v.desc.StartOffset = sv.offset;
                v.desc.Size = sv.size;
                v.desc.uFlags = D3D_SVF_USED;
                v.desc.StartTexture = v.desc.StartSampler = UINT(-1);
                c.vars.push_back(&v);
            }
        }
        for (const ShaderResource &r : info.resources)
        {
            D3D11_SHADER_INPUT_BIND_DESC b{};
            b.Name = r.name.c_str();
            b.Type = r.type;
            b.BindPoint = r.slot;
            b.BindCount = r.count;
            b.ReturnType = r.return_type;
            b.Dimension = r.dimension;
            b.NumSamples = r.type == D3D_SIT_TEXTURE ? UINT(-1) : 0;
            bindings.push_back(b);
        }
        fill_signature(info.inputs, inputs);
        fill_signature(info.outputs, outputs);
        desc.InputParameters = UINT(inputs.size());
        desc.OutputParameters = UINT(outputs.size());
    }

    namespace
    {
        Reflection *self(ID3D11ShaderReflection *p)
        {
            return static_cast<Reflection *>(p);
        }
        HRESULT signature(const std::vector<D3D11_SIGNATURE_PARAMETER_DESC> &v, UINT i, D3D11_SIGNATURE_PARAMETER_DESC *d)
        {
            if (!d || i >= v.size())
                return E_INVALIDARG;
            *d = v[i];
            return S_OK;
        }
    } // namespace
} // namespace xrvk

using namespace xrvk;

// ---- ID3D11ShaderReflectionType ------------------------------------------------------------------------
HRESULT ID3D11ShaderReflectionType::GetDesc(D3D11_SHADER_TYPE_DESC *d)
{
    const RType *t = static_cast<RType *>(this);
    if (!d || !t->valid)
        return E_FAIL;
    *d = t->desc;
    return S_OK;
}
// Struct members are not reflected (the engine rejects struct constants), so member lookups find nothing.
ID3D11ShaderReflectionType *ID3D11ShaderReflectionType::GetMemberTypeByIndex(UINT)
{
    return &g_null_type;
}
ID3D11ShaderReflectionType *ID3D11ShaderReflectionType::GetMemberTypeByName(const char *)
{
    return &g_null_type;
}
const char *ID3D11ShaderReflectionType::GetMemberTypeName(UINT)
{
    return nullptr;
}
HRESULT ID3D11ShaderReflectionType::IsEqual(ID3D11ShaderReflectionType *t)
{
    return t == this ? S_OK : S_FALSE;
}
ID3D11ShaderReflectionType *ID3D11ShaderReflectionType::GetSubType()
{
    return nullptr;
}
ID3D11ShaderReflectionType *ID3D11ShaderReflectionType::GetBaseClass()
{
    return nullptr;
}
UINT ID3D11ShaderReflectionType::GetNumInterfaces()
{
    return 0;
}
ID3D11ShaderReflectionType *ID3D11ShaderReflectionType::GetInterfaceByIndex(UINT)
{
    return nullptr;
}
HRESULT ID3D11ShaderReflectionType::IsOfType(ID3D11ShaderReflectionType *t)
{
    return t == this ? S_OK : S_FALSE;
}
HRESULT ID3D11ShaderReflectionType::ImplementsInterface(ID3D11ShaderReflectionType *)
{
    return S_FALSE;
}

// ---- ID3D11ShaderReflectionVariable --------------------------------------------------------------------
HRESULT ID3D11ShaderReflectionVariable::GetDesc(D3D11_SHADER_VARIABLE_DESC *d)
{
    const RVariable *v = static_cast<RVariable *>(this);
    if (!d || !v->valid)
        return E_FAIL;
    *d = v->desc;
    return S_OK;
}
ID3D11ShaderReflectionType *ID3D11ShaderReflectionVariable::GetType()
{
    RVariable *v = static_cast<RVariable *>(this);
    return v->type ? v->type : &g_null_type;
}
ID3D11ShaderReflectionConstantBuffer *ID3D11ShaderReflectionVariable::GetBuffer()
{
    RVariable *v = static_cast<RVariable *>(this);
    return v->buffer ? v->buffer : &g_null_cbuffer;
}
UINT ID3D11ShaderReflectionVariable::GetInterfaceSlot(UINT)
{
    return UINT(-1);
}

// ---- ID3D11ShaderReflectionConstantBuffer --------------------------------------------------------------
HRESULT ID3D11ShaderReflectionConstantBuffer::GetDesc(D3D11_SHADER_BUFFER_DESC *d)
{
    const RCBuffer *c = static_cast<RCBuffer *>(this);
    if (!d || !c->valid)
        return E_FAIL;
    *d = c->desc;
    return S_OK;
}
ID3D11ShaderReflectionVariable *ID3D11ShaderReflectionConstantBuffer::GetVariableByIndex(UINT i)
{
    RCBuffer *c = static_cast<RCBuffer *>(this);
    return i < c->vars.size() ? c->vars[i] : &g_null_variable;
}
ID3D11ShaderReflectionVariable *ID3D11ShaderReflectionConstantBuffer::GetVariableByName(const char *name)
{
    RCBuffer *c = static_cast<RCBuffer *>(this);
    for (RVariable *v : c->vars)
        if (name && !strcmp(v->desc.Name, name))
            return v;
    return &g_null_variable;
}

// ---- ID3D11ShaderReflection ----------------------------------------------------------------------------
HRESULT ID3D11ShaderReflection::GetDesc(D3D11_SHADER_DESC *d)
{
    if (!d)
        return E_FAIL;
    *d = self(this)->desc;
    return S_OK;
}
ID3D11ShaderReflectionConstantBuffer *ID3D11ShaderReflection::GetConstantBufferByIndex(UINT i)
{
    auto &c = self(this)->cbuffers;
    return i < c.size() ? &c[i] : &g_null_cbuffer;
}
ID3D11ShaderReflectionConstantBuffer *ID3D11ShaderReflection::GetConstantBufferByName(const char *name)
{
    for (RCBuffer &c : self(this)->cbuffers)
        if (name && !strcmp(c.desc.Name, name))
            return &c;
    return &g_null_cbuffer;
}
HRESULT ID3D11ShaderReflection::GetResourceBindingDesc(UINT i, D3D11_SHADER_INPUT_BIND_DESC *d)
{
    const auto &b = self(this)->bindings;
    if (!d || i >= b.size())
        return E_INVALIDARG;
    *d = b[i];
    return S_OK;
}
HRESULT ID3D11ShaderReflection::GetInputParameterDesc(UINT i, D3D11_SIGNATURE_PARAMETER_DESC *d)
{
    return signature(self(this)->inputs, i, d);
}
HRESULT ID3D11ShaderReflection::GetOutputParameterDesc(UINT i, D3D11_SIGNATURE_PARAMETER_DESC *d)
{
    return signature(self(this)->outputs, i, d);
}
HRESULT ID3D11ShaderReflection::GetPatchConstantParameterDesc(UINT, D3D11_SIGNATURE_PARAMETER_DESC *)
{
    return E_INVALIDARG; // patch constants are not reflected (no tessellation shaders in use)
}
ID3D11ShaderReflectionVariable *ID3D11ShaderReflection::GetVariableByName(const char *name)
{
    for (RVariable &v : self(this)->variables)
        if (name && !strcmp(v.desc.Name, name))
            return &v;
    return &g_null_variable;
}
HRESULT ID3D11ShaderReflection::GetResourceBindingDescByName(const char *name, D3D11_SHADER_INPUT_BIND_DESC *d)
{
    for (const auto &b : self(this)->bindings)
        if (name && d && !strcmp(b.Name, name))
        {
            *d = b;
            return S_OK;
        }
    return E_INVALIDARG;
}
// Instruction statistics have no SPIR-V counterpart; the engine does not read them.
UINT ID3D11ShaderReflection::GetMovInstructionCount()
{
    return 0;
}
UINT ID3D11ShaderReflection::GetMovcInstructionCount()
{
    return 0;
}
UINT ID3D11ShaderReflection::GetConversionInstructionCount()
{
    return 0;
}
UINT ID3D11ShaderReflection::GetBitwiseInstructionCount()
{
    return 0;
}
D3D_PRIMITIVE ID3D11ShaderReflection::GetGSInputPrimitive()
{
    return D3D_PRIMITIVE_UNDEFINED;
}
WINBOOL ID3D11ShaderReflection::IsSampleFrequencyShader()
{
    return FALSE;
}
UINT ID3D11ShaderReflection::GetNumInterfaceSlots()
{
    return 0;
}
HRESULT ID3D11ShaderReflection::GetMinFeatureLevel(D3D_FEATURE_LEVEL *level)
{
    if (!level)
        return E_INVALIDARG;
    *level = D3D_FEATURE_LEVEL_11_0;
    return S_OK;
}
UINT ID3D11ShaderReflection::GetThreadGroupSize(UINT *x, UINT *y, UINT *z)
{
    const ShaderInfo &i = self(this)->info;
    if (x)
        *x = i.threads[0];
    if (y)
        *y = i.threads[1];
    if (z)
        *z = i.threads[2];
    return i.threads[0] * i.threads[1] * i.threads[2];
}
UINT64 ID3D11ShaderReflection::GetRequiresFlags()
{
    return 0;
}

extern "C" HRESULT WINAPI D3DReflect(const void *data, SIZE_T data_size, REFIID riid, void **reflector)
{
    if (!reflector)
        return E_INVALIDARG;
    *reflector = nullptr;
    if (riid != IID_ID3D11ShaderReflection)
        return E_NOINTERFACE;
    auto r = std::make_unique<Reflection>();
    std::string error;
    if (!reflect_spirv(data, data_size, r->info, error))
    {
        fprintf(stderr, "! D3DReflect: %s\n", error.c_str());
        return E_FAIL;
    }
    r->build();
    *reflector = static_cast<ID3D11ShaderReflection *>(r.release());
    return S_OK;
}

// r__disasm: the engine writes this text next to its shader cache. A SPIR-V disassembler is not part of
// the backend, so the output is the reflection as the engine sees it (bindings, constants, signatures).
extern "C" HRESULT WINAPI D3DDisassemble(const void *data, SIZE_T data_size, UINT, const char *, ID3DBlob **disassembly)
{
    if (!disassembly)
        return E_INVALIDARG;
    *disassembly = nullptr;
    ShaderInfo info;
    std::string error;
    if (!reflect_spirv(data, data_size, info, error))
        return E_FAIL;
    std::string t = "// SPIR-V module, " + std::to_string(data_size / 4) + " words (reflection only)\n";
    char line[512];
    for (const auto &cb : info.cbuffers)
    {
        snprintf(line, sizeof line, "cbuffer %s : register(b%u) // %u bytes\n", cb.name.c_str(), cb.slot, cb.size);
        t += line;
        for (const auto &v : cb.vars)
        {
            snprintf(line, sizeof line, "  %-10s %-32s offset %4u size %4u%s\n", v.type_name.c_str(), v.name.c_str(), v.offset,
                     v.size, v.elements ? (" [" + std::to_string(v.elements) + "]").c_str() : "");
            t += line;
        }
    }
    for (const auto &r : info.resources)
    {
        snprintf(line, sizeof line, "resource %-32s type %d slot %u binding %u count %u\n", r.name.c_str(), int(r.type), r.slot,
                 r.binding, r.count);
        t += line;
    }
    for (const auto *list : {&info.inputs, &info.outputs})
        for (const auto &p : *list)
        {
            snprintf(line, sizeof line, "%s %s%u location %d components %u\n", list == &info.inputs ? "in " : "out",
                     p.semantic.c_str(), p.index, int(p.location), p.components);
            t += line;
        }
    *disassembly = make_blob(t.c_str(), t.size() + 1);
    return S_OK;
}
