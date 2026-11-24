// The D3D shader-compiler API the renderer uses (d3dcompiler_47.dll is Windows-only), on top of the
// vkd3d-shader C library (HLSL -> DXBC TPF).
//
// Why not vkd3d-utils' D3DCompile/D3DReflect: on x86-64 Linux vkd3d declares WINAPI and COM methods
// as __attribute__((ms_abi)) (vkd3d_windows.h), while dxvk-native and this engine use the System V
// ABI, so every call through them receives garbage arguments. vkd3d-shader's C API has no COM.
//
// D3DReflect parses the RDEF / ISGN / OSGN chunks of the DXBC container itself. Layouts follow the
// D3D11 shader reflection structures in d3d11shader.h and the container format documented by the
// Wine and vkd3d projects (dxbc.c / reflection.c).
#include "../../xrCore/xrCore_platform.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>

#include <vkd3d_shader.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{
    uint32_t rd32(const uint8_t *p)
    {
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }
    uint16_t rd16(const uint8_t *p)
    {
        return uint16_t(p[0] | p[1] << 8);
    }
    constexpr uint32_t tag(char a, char b, char c, char d)
    {
        return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24;
    }

    // ---------------------------------------------------------------------------------------------
    class Blob final : public ID3D10Blob
    {
    public:
        explicit Blob(size_t n) : m_data(n)
        {
        }
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override
        {
            if (!out)
                return E_POINTER;
            if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D10Blob))
            {
                AddRef();
                *out = this;
                return S_OK;
            }
            *out = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return ++m_refs;
        }
        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG r = --m_refs;
            if (!r)
                delete this;
            return r;
        }
        void *STDMETHODCALLTYPE GetBufferPointer() override
        {
            return m_data.data();
        }
        SIZE_T STDMETHODCALLTYPE GetBufferSize() override
        {
            return m_data.size();
        }

    private:
        std::vector<uint8_t> m_data;
        std::atomic<ULONG> m_refs{1};
    };

    HRESULT make_blob(const void *data, size_t n, ID3DBlob **out)
    {
        if (!out)
            return E_POINTER;
        Blob *b = new Blob(n);
        if (n)
            memcpy(b->GetBufferPointer(), data, n);
        *out = b;
        return S_OK;
    }

    // ---------------------------------------------------------------------------------------------
    // DXBC container
    struct Chunk
    {
        const uint8_t *data = nullptr;
        uint32_t size = 0;
    };

    bool find_chunk(const uint8_t *p, size_t n, uint32_t fourcc, Chunk &out)
    {
        if (n < 32 || rd32(p) != tag('D', 'X', 'B', 'C'))
            return false;
        const uint32_t count = rd32(p + 28);
        if (32 + size_t(count) * 4 > n)
            return false;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t off = rd32(p + 32 + i * 4);
            if (size_t(off) + 8 > n)
                return false;
            const uint32_t sz = rd32(p + off + 4);
            if (size_t(off) + 8 + sz > n)
                return false;
            if (rd32(p + off) == fourcc)
            {
                out.data = p + off + 8;
                out.size = sz;
                return true;
            }
        }
        return false;
    }

    // ---------------------------------------------------------------------------------------------
    // Reflection objects. All of them live inside one Reflection; only that one is reference counted,
    // matching d3dcompiler (child interfaces are not IUnknown and die with the parent).
    class RType final : public ID3D11ShaderReflectionType
    {
    public:
        D3D11_SHADER_TYPE_DESC desc{};
        std::vector<RType *> members;
        std::vector<const char *> member_names;
        bool valid = false;

        HRESULT STDMETHODCALLTYPE GetDesc(D3D11_SHADER_TYPE_DESC *d) override
        {
            if (!d || !valid)
                return E_FAIL;
            *d = desc;
            return S_OK;
        }
        ID3D11ShaderReflectionType *STDMETHODCALLTYPE GetMemberTypeByIndex(UINT i) override;
        ID3D11ShaderReflectionType *STDMETHODCALLTYPE GetMemberTypeByName(const char *name) override;
        const char *STDMETHODCALLTYPE GetMemberTypeName(UINT i) override
        {
            return i < member_names.size() ? member_names[i] : nullptr;
        }
        HRESULT STDMETHODCALLTYPE IsEqual(ID3D11ShaderReflectionType *t) override
        {
            return t == this ? S_OK : S_FALSE;
        }
        ID3D11ShaderReflectionType *STDMETHODCALLTYPE GetSubType() override
        {
            return nullptr;
        }
        ID3D11ShaderReflectionType *STDMETHODCALLTYPE GetBaseClass() override
        {
            return nullptr;
        }
        UINT STDMETHODCALLTYPE GetNumInterfaces() override
        {
            return 0;
        }
        ID3D11ShaderReflectionType *STDMETHODCALLTYPE GetInterfaceByIndex(UINT) override
        {
            return nullptr;
        }
        HRESULT STDMETHODCALLTYPE IsOfType(ID3D11ShaderReflectionType *t) override
        {
            return t == this ? S_OK : S_FALSE;
        }
        HRESULT STDMETHODCALLTYPE ImplementsInterface(ID3D11ShaderReflectionType *) override
        {
            return S_FALSE;
        }
    };

    RType g_null_type; // returned for invalid lookups, like d3dcompiler's "null" objects (GetDesc fails)

    ID3D11ShaderReflectionType *RType::GetMemberTypeByIndex(UINT i)
    {
        return i < members.size() ? members[i] : &g_null_type;
    }
    ID3D11ShaderReflectionType *RType::GetMemberTypeByName(const char *name)
    {
        for (size_t i = 0; name && i < member_names.size(); ++i)
            if (!strcmp(member_names[i], name))
                return members[i];
        return &g_null_type;
    }

    class RVariable final : public ID3D11ShaderReflectionVariable
    {
    public:
        D3D11_SHADER_VARIABLE_DESC desc{};
        RType *type = &g_null_type;
        ID3D11ShaderReflectionConstantBuffer *buffer = nullptr;
        bool valid = false;

        HRESULT STDMETHODCALLTYPE GetDesc(D3D11_SHADER_VARIABLE_DESC *d) override
        {
            if (!d || !valid)
                return E_FAIL;
            *d = desc;
            return S_OK;
        }
        ID3D11ShaderReflectionType *STDMETHODCALLTYPE GetType() override
        {
            return type;
        }
        ID3D11ShaderReflectionConstantBuffer *STDMETHODCALLTYPE GetBuffer() override
        {
            return buffer;
        }
        UINT STDMETHODCALLTYPE GetInterfaceSlot(UINT) override
        {
            return UINT(-1);
        }
    };

    RVariable g_null_variable;

    class RCBuffer final : public ID3D11ShaderReflectionConstantBuffer
    {
    public:
        D3D11_SHADER_BUFFER_DESC desc{};
        std::vector<std::unique_ptr<RVariable>> vars;
        bool valid = false;

        HRESULT STDMETHODCALLTYPE GetDesc(D3D11_SHADER_BUFFER_DESC *d) override
        {
            if (!d || !valid)
                return E_FAIL;
            *d = desc;
            return S_OK;
        }
        ID3D11ShaderReflectionVariable *STDMETHODCALLTYPE GetVariableByIndex(UINT i) override
        {
            return i < vars.size() ? vars[i].get() : &g_null_variable;
        }
        ID3D11ShaderReflectionVariable *STDMETHODCALLTYPE GetVariableByName(const char *name) override
        {
            for (auto &v : vars)
                if (name && !strcmp(v->desc.Name, name))
                    return v.get();
            return &g_null_variable;
        }
    };

    RCBuffer g_null_cbuffer;

    class Reflection final : public ID3D11ShaderReflection
    {
    public:
        bool init(const void *code, size_t size, std::string &error);

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override
        {
            if (!out)
                return E_POINTER;
            if (riid == __uuidof(IUnknown) || riid == IID_ID3D11ShaderReflection)
            {
                AddRef();
                *out = this;
                return S_OK;
            }
            *out = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return ++m_refs;
        }
        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG r = --m_refs;
            if (!r)
                delete this;
            return r;
        }
        HRESULT STDMETHODCALLTYPE GetDesc(D3D11_SHADER_DESC *d) override
        {
            if (!d)
                return E_FAIL;
            *d = m_desc;
            return S_OK;
        }
        ID3D11ShaderReflectionConstantBuffer *STDMETHODCALLTYPE GetConstantBufferByIndex(UINT i) override
        {
            return i < m_cbuffers.size() ? m_cbuffers[i].get() : &g_null_cbuffer;
        }
        ID3D11ShaderReflectionConstantBuffer *STDMETHODCALLTYPE GetConstantBufferByName(const char *name) override
        {
            for (auto &c : m_cbuffers)
                if (name && !strcmp(c->desc.Name, name))
                    return c.get();
            return &g_null_cbuffer;
        }
        HRESULT STDMETHODCALLTYPE GetResourceBindingDesc(UINT i, D3D11_SHADER_INPUT_BIND_DESC *d) override
        {
            if (!d || i >= m_bindings.size())
                return E_INVALIDARG;
            *d = m_bindings[i];
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetInputParameterDesc(UINT i, D3D11_SIGNATURE_PARAMETER_DESC *d) override
        {
            return sig(m_inputs, i, d);
        }
        HRESULT STDMETHODCALLTYPE GetOutputParameterDesc(UINT i, D3D11_SIGNATURE_PARAMETER_DESC *d) override
        {
            return sig(m_outputs, i, d);
        }
        HRESULT STDMETHODCALLTYPE GetPatchConstantParameterDesc(UINT i, D3D11_SIGNATURE_PARAMETER_DESC *d) override
        {
            return sig(m_patch, i, d);
        }
        ID3D11ShaderReflectionVariable *STDMETHODCALLTYPE GetVariableByName(const char *name) override
        {
            for (auto &c : m_cbuffers)
                for (auto &v : c->vars)
                    if (name && !strcmp(v->desc.Name, name))
                        return v.get();
            return &g_null_variable;
        }
        HRESULT STDMETHODCALLTYPE GetResourceBindingDescByName(const char *name, D3D11_SHADER_INPUT_BIND_DESC *d) override
        {
            for (auto &b : m_bindings)
                if (name && d && !strcmp(b.Name, name))
                {
                    *d = b;
                    return S_OK;
                }
            return E_INVALIDARG;
        }
        // Instruction statistics (STAT chunk) are not parsed; the engine does not read them.
        UINT STDMETHODCALLTYPE GetMovInstructionCount() override
        {
            return 0;
        }
        UINT STDMETHODCALLTYPE GetMovcInstructionCount() override
        {
            return 0;
        }
        UINT STDMETHODCALLTYPE GetConversionInstructionCount() override
        {
            return 0;
        }
        UINT STDMETHODCALLTYPE GetBitwiseInstructionCount() override
        {
            return 0;
        }
        D3D_PRIMITIVE STDMETHODCALLTYPE GetGSInputPrimitive() override
        {
            return D3D_PRIMITIVE_UNDEFINED;
        }
        WINBOOL STDMETHODCALLTYPE IsSampleFrequencyShader() override
        {
            return FALSE;
        }
        UINT STDMETHODCALLTYPE GetNumInterfaceSlots() override
        {
            return 0;
        }
        HRESULT STDMETHODCALLTYPE GetMinFeatureLevel(enum D3D_FEATURE_LEVEL *level) override
        {
            if (!level)
                return E_INVALIDARG;
            *level = D3D_FEATURE_LEVEL_11_0;
            return S_OK;
        }
        UINT STDMETHODCALLTYPE GetThreadGroupSize(UINT *x, UINT *y, UINT *z) override
        {
            if (x)
                *x = 0;
            if (y)
                *y = 0;
            if (z)
                *z = 0;
            return 0;
        }
        UINT64 STDMETHODCALLTYPE GetRequiresFlags() override
        {
            return 0;
        }

    private:
        static HRESULT sig(const std::vector<D3D11_SIGNATURE_PARAMETER_DESC> &v, UINT i, D3D11_SIGNATURE_PARAMETER_DESC *d)
        {
            if (!d || i >= v.size())
                return E_INVALIDARG;
            *d = v[i];
            return S_OK;
        }
        bool parse_rdef(const Chunk &c, std::string &error);
        RType *parse_type(const Chunk &c, uint32_t offset, bool sm5, std::string &error, int depth);
        bool parse_signature(const Chunk &c, bool has_stream, bool has_min_precision, std::vector<D3D11_SIGNATURE_PARAMETER_DESC> &out);
        static const char *str(const Chunk &c, uint32_t off)
        {
            if (off >= c.size || !memchr(c.data + off, 0, c.size - off))
                return "";
            return reinterpret_cast<const char *>(c.data + off);
        }

        std::vector<uint8_t> m_code; // the chunks point into this copy
        D3D11_SHADER_DESC m_desc{};
        std::vector<std::unique_ptr<RCBuffer>> m_cbuffers;
        std::vector<std::unique_ptr<RType>> m_types;
        std::vector<D3D11_SHADER_INPUT_BIND_DESC> m_bindings;
        std::vector<D3D11_SIGNATURE_PARAMETER_DESC> m_inputs, m_outputs, m_patch;
        std::atomic<ULONG> m_refs{1};
    };

    RType *Reflection::parse_type(const Chunk &c, uint32_t off, bool sm5, std::string &error, int depth)
    {
        const uint32_t type_size = sm5 ? 36 : 16;
        if (depth > 32 || size_t(off) + type_size > c.size)
        {
            error = "bad type offset";
            return nullptr;
        }
        const uint8_t *p = c.data + off;
        auto t = std::make_unique<RType>();
        t->valid = true;
        t->desc.Class = D3D_SHADER_VARIABLE_CLASS(rd16(p));
        t->desc.Type = D3D_SHADER_VARIABLE_TYPE(rd16(p + 2));
        t->desc.Rows = rd16(p + 4);
        t->desc.Columns = rd16(p + 6);
        t->desc.Elements = rd16(p + 8);
        t->desc.Members = rd16(p + 10);
        const uint32_t member_off = rd32(p + 12);
        t->desc.Name = sm5 ? str(c, rd32(p + 32)) : nullptr;
        RType *raw = t.get();
        m_types.push_back(std::move(t));
        for (UINT i = 0; i < raw->desc.Members; ++i)
        {
            const uint32_t mo = member_off + i * 12;
            if (size_t(mo) + 12 > c.size)
            {
                error = "bad member offset";
                return nullptr;
            }
            RType *mt = parse_type(c, rd32(c.data + mo + 4), sm5, error, depth + 1);
            if (!mt)
                return nullptr;
            mt->desc.Offset = rd32(c.data + mo + 8);
            raw->members.push_back(mt);
            raw->member_names.push_back(str(c, rd32(c.data + mo)));
        }
        return raw;
    }

    bool Reflection::parse_rdef(const Chunk &c, std::string &error)
    {
        if (c.size < 28)
        {
            error = "RDEF too small";
            return false;
        }
        const uint8_t *p = c.data;
        const uint32_t cb_count = rd32(p), cb_off = rd32(p + 4), bind_count = rd32(p + 8), bind_off = rd32(p + 12);
        const uint8_t minor = p[16], major = p[17];
        const uint16_t program_type = rd16(p + 18);
        m_desc.Version = uint32_t(program_type) << 16 | uint32_t(major) << 4 | minor;
        m_desc.Flags = rd32(p + 20);
        m_desc.Creator = str(c, rd32(p + 24));
        const bool sm5 = major >= 5;
        uint32_t var_size = 24;
        if (sm5)
        {
            // RD11 header: magic, then sizes of the RDEF header, cbuffer, binding, variable, type, member
            if (c.size < 60 || rd32(p + 28) != tag('R', 'D', '1', '1'))
            {
                error = "missing RD11 header";
                return false;
            }
            var_size = rd32(p + 28 + 4 * 4);
            if (var_size < 24)
                var_size = 40;
        }
        for (uint32_t i = 0; i < bind_count; ++i)
        {
            const uint32_t o = bind_off + i * 32;
            if (size_t(o) + 32 > c.size)
            {
                error = "bad binding table";
                return false;
            }
            const uint8_t *b = c.data + o;
            D3D11_SHADER_INPUT_BIND_DESC d{};
            d.Name = str(c, rd32(b));
            d.Type = D3D_SHADER_INPUT_TYPE(rd32(b + 4));
            d.ReturnType = D3D_RESOURCE_RETURN_TYPE(rd32(b + 8));
            d.Dimension = D3D_SRV_DIMENSION(rd32(b + 12));
            d.NumSamples = rd32(b + 16);
            d.BindPoint = rd32(b + 20);
            d.BindCount = rd32(b + 24);
            d.uFlags = rd32(b + 28);
            m_bindings.push_back(d);
        }
        for (uint32_t i = 0; i < cb_count; ++i)
        {
            const uint32_t o = cb_off + i * 24;
            if (size_t(o) + 24 > c.size)
            {
                error = "bad constant buffer table";
                return false;
            }
            const uint8_t *b = c.data + o;
            auto cb = std::make_unique<RCBuffer>();
            cb->valid = true;
            cb->desc.Name = str(c, rd32(b));
            cb->desc.Variables = rd32(b + 4);
            const uint32_t vars_off = rd32(b + 8);
            cb->desc.Size = rd32(b + 12);
            cb->desc.uFlags = rd32(b + 16);
            cb->desc.Type = D3D_CBUFFER_TYPE(rd32(b + 20));
            for (UINT v = 0; v < cb->desc.Variables; ++v)
            {
                const uint32_t vo = vars_off + v * var_size;
                if (size_t(vo) + var_size > c.size)
                {
                    error = "bad variable table";
                    return false;
                }
                const uint8_t *q = c.data + vo;
                auto var = std::make_unique<RVariable>();
                var->valid = true;
                var->buffer = cb.get();
                var->desc.Name = str(c, rd32(q));
                var->desc.StartOffset = rd32(q + 4);
                var->desc.Size = rd32(q + 8);
                var->desc.uFlags = rd32(q + 12);
                const uint32_t type_off = rd32(q + 16), default_off = rd32(q + 20);
                var->desc.DefaultValue =
                    default_off && size_t(default_off) + var->desc.Size <= c.size ? const_cast<uint8_t *>(c.data + default_off) : nullptr;
                if (sm5 && var_size >= 40)
                {
                    var->desc.StartTexture = rd32(q + 24);
                    var->desc.TextureSize = rd32(q + 28);
                    var->desc.StartSampler = rd32(q + 32);
                    var->desc.SamplerSize = rd32(q + 36);
                }
                else
                {
                    var->desc.StartTexture = var->desc.StartSampler = UINT(-1);
                }
                RType *t = parse_type(c, type_off, sm5, error, 0);
                if (!t)
                    return false;
                var->type = t;
                cb->vars.push_back(std::move(var));
            }
            m_cbuffers.push_back(std::move(cb));
        }
        m_desc.ConstantBuffers = UINT(m_cbuffers.size());
        m_desc.BoundResources = UINT(m_bindings.size());
        return true;
    }

    bool Reflection::parse_signature(const Chunk &c, bool has_stream, bool has_min_precision, std::vector<D3D11_SIGNATURE_PARAMETER_DESC> &out)
    {
        if (c.size < 8)
            return false;
        const uint32_t count = rd32(c.data);
        const uint32_t elem = 24 + (has_stream ? 4 : 0) + (has_min_precision ? 4 : 0);
        const uint32_t base = rd32(c.data + 4); // offset of the first element (8)
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t o = base + i * elem;
            if (size_t(o) + elem > c.size)
                return false;
            const uint8_t *e = c.data + o;
            D3D11_SIGNATURE_PARAMETER_DESC d{};
            if (has_stream)
            {
                d.Stream = rd32(e);
                e += 4;
            }
            d.SemanticName = str(c, rd32(e));
            d.SemanticIndex = rd32(e + 4);
            d.SystemValueType = D3D_NAME(rd32(e + 8));
            d.ComponentType = D3D_REGISTER_COMPONENT_TYPE(rd32(e + 12));
            d.Register = rd32(e + 16);
            d.Mask = e[20];
            d.ReadWriteMask = e[21];
#if D3D_COMPILER_VERSION >= 46
            if (has_min_precision)
                d.MinPrecision = D3D_MIN_PRECISION(rd32(e + 24));
#endif
            out.push_back(d);
        }
        return true;
    }

    bool Reflection::init(const void *code, size_t size, std::string &error)
    {
        m_code.assign(static_cast<const uint8_t *>(code), static_cast<const uint8_t *>(code) + size);
        const uint8_t *p = m_code.data();
        Chunk c;
        if (!find_chunk(p, size, tag('R', 'D', 'E', 'F'), c))
        {
            error = "no RDEF chunk";
            return false;
        }
        if (!parse_rdef(c, error))
            return false;
        // Input/output signatures: plain, stream (OSG5) or min-precision (ISG1/OSG1) variants.
        if (find_chunk(p, size, tag('I', 'S', 'G', 'N'), c))
            parse_signature(c, false, false, m_inputs);
        else if (find_chunk(p, size, tag('I', 'S', 'G', '1'), c))
            parse_signature(c, true, true, m_inputs);
        if (find_chunk(p, size, tag('O', 'S', 'G', 'N'), c))
            parse_signature(c, false, false, m_outputs);
        else if (find_chunk(p, size, tag('O', 'S', 'G', '5'), c))
            parse_signature(c, true, false, m_outputs);
        else if (find_chunk(p, size, tag('O', 'S', 'G', '1'), c))
            parse_signature(c, true, true, m_outputs);
        if (find_chunk(p, size, tag('P', 'C', 'S', 'G'), c))
            parse_signature(c, false, false, m_patch);
        m_desc.InputParameters = UINT(m_inputs.size());
        m_desc.OutputParameters = UINT(m_outputs.size());
        m_desc.PatchConstantParameters = UINT(m_patch.size());
        return true;
    }

    // ---------------------------------------------------------------------------------------------
    // Include bridge: vkd3d-shader callbacks -> the engine's ID3DInclude.
    struct IncludeContext
    {
        ID3DInclude *include;
    };

    int open_include(const char *filename, bool local, const char *parent_data, void *context, vkd3d_shader_code *out)
    {
        auto *ctx = static_cast<IncludeContext *>(context);
        if (!ctx->include)
            return VKD3D_ERROR;
        LPCVOID data = nullptr;
        UINT bytes = 0;
        if (FAILED(ctx->include->Open(local ? D3D_INCLUDE_LOCAL : D3D_INCLUDE_SYSTEM, filename, parent_data, &data, &bytes)))
            return VKD3D_ERROR;
        out->code = data;
        out->size = bytes;
        return VKD3D_OK;
    }

    void close_include(const vkd3d_shader_code *code, void *context)
    {
        auto *ctx = static_cast<IncludeContext *>(context);
        if (ctx->include)
            ctx->include->Close(code->code);
    }

    // Debug aid: XR_DUMP_FAILED_SHADERS=<dir> writes the (preprocessed, rewritten) source and the
    // compiler messages of every shader that fails to compile; the sources live in .db archives.
    void dump_failed_shader(const std::string &source, const char *entry, const char *target, const char *messages)
    {
        const char *dir = getenv("XR_DUMP_FAILED_SHADERS");
        if (!dir || !*dir)
            return;
        static std::atomic<unsigned> counter{0};
        const std::string base = std::string(dir) + "/" + std::to_string(counter++) + "_" + (entry ? entry : "main") + "_" + target;
        if (FILE *f = fopen((base + ".hlsl").c_str(), "wb"))
        {
            fwrite(source.data(), 1, source.size(), f);
            fclose(f);
        }
        if (FILE *f = fopen((base + ".log").c_str(), "wb"))
        {
            fputs(messages ? messages : "", f);
            fclose(f);
        }
    }

    bool is_ident(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

    // Source-level compatibility with d3dcompiler for constructs vkd3d-shader 1.17 does not implement,
    // applied to preprocessed text so the game's shader files stay untouched:
    //  * register(<stage>[_x_y], <reg>) ("Reservation shader target", E5017): the binding applies only
    //    when compiling for that stage. It becomes register(<reg>) for the current stage, and is
    //    removed (with its ':') for the other stages - exactly the d3dcompiler semantics.
    std::string rewrite_for_vkd3d(const char *text, size_t size, const char *target)
    {
        const std::string in(text, size);
        const std::string stage = target ? std::string(target).substr(0, 2) : std::string();
        std::string out;
        out.reserve(in.size());
        size_t pos = 0;
        for (;;)
        {
            size_t r = in.find("register", pos);
            if (r == std::string::npos)
                break;
            const size_t word_end = r + 8;
            if ((r > 0 && is_ident(in[r - 1])) || (word_end < in.size() && is_ident(in[word_end])))
            {
                out.append(in, pos, word_end - pos);
                pos = word_end;
                continue;
            }
            size_t k = word_end;
            while (k < in.size() && isspace(uint8_t(in[k])))
                ++k;
            if (k >= in.size() || in[k] != '(')
            {
                out.append(in, pos, word_end - pos);
                pos = word_end;
                continue;
            }
            size_t id = k + 1;
            while (id < in.size() && isspace(uint8_t(in[id])))
                ++id;
            size_t id_end = id;
            while (id_end < in.size() && is_ident(in[id_end]))
                ++id_end;
            const std::string name = in.substr(id, id_end - id);
            size_t comma = id_end;
            while (comma < in.size() && isspace(uint8_t(in[comma])))
                ++comma;
            const bool is_stage = name.size() >= 2 && (name.size() == 2 || name[2] == '_') &&
                (!name.compare(0, 2, "vs") || !name.compare(0, 2, "ps") || !name.compare(0, 2, "gs") || !name.compare(0, 2, "hs") ||
                 !name.compare(0, 2, "ds") || !name.compare(0, 2, "cs"));
            const size_t close = in.find(')', comma);
            if (!is_stage || comma >= in.size() || in[comma] != ',' || close == std::string::npos)
            {
                out.append(in, pos, word_end - pos);
                pos = word_end;
                continue;
            }
            if (!name.compare(0, 2, stage))
            {
                out.append(in, pos, r - pos);
                out += "register(";
                out.append(in, comma + 1, close + 1 - (comma + 1));
            }
            else
            {
                // drop ": register(...)" - remove the ':' already copied to `out`
                size_t back = r;
                while (back > pos && isspace(uint8_t(in[back - 1])))
                    --back;
                if (back > pos && in[back - 1] == ':')
                    out.append(in, pos, back - 1 - pos);
                else
                    out.append(in, pos, r - pos);
            }
            pos = close + 1;
        }
        out.append(in, pos, std::string::npos);
        return out;
    }
} // namespace

extern "C"
{

    HRESULT WINAPI D3DCreateBlob(SIZE_T size, ID3DBlob **blob)
    {
        if (!blob)
            return E_POINTER;
        *blob = new Blob(size);
        return S_OK;
    }

    HRESULT WINAPI D3DCompile(const void *data, SIZE_T data_size, const char *filename, const D3D_SHADER_MACRO *defines,
                              ID3DInclude *include, const char *entrypoint, const char *target, UINT flags1, UINT /*flags2: effects only*/,
                              ID3DBlob **shader, ID3DBlob **error_messages)
    {
        if (shader)
            *shader = nullptr;
        if (error_messages)
            *error_messages = nullptr;
        if (!data || !target || !shader)
            return E_INVALIDARG;

        std::vector<vkd3d_shader_macro> macros;
        for (const D3D_SHADER_MACRO *d = defines; d && d->Name; ++d)
            macros.push_back(vkd3d_shader_macro{d->Name, d->Definition ? d->Definition : ""});
        // D3DCOMPILE_STANDARD_FILE_INCLUDE is the pointer value 1, not an object; not supported here.
        IncludeContext inc{include == reinterpret_cast<ID3DInclude *>(uintptr_t(1)) ? nullptr : include};

        std::vector<vkd3d_shader_compile_option> options;
        options.push_back({VKD3D_SHADER_COMPILE_OPTION_API_VERSION, VKD3D_SHADER_API_VERSION_1_17});
        if (flags1 & D3DCOMPILE_PACK_MATRIX_ROW_MAJOR)
            options.push_back({VKD3D_SHADER_COMPILE_OPTION_PACK_MATRIX_ORDER, VKD3D_SHADER_COMPILE_OPTION_PACK_MATRIX_ROW_MAJOR});
        if (flags1 & D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY)
            options.push_back({VKD3D_SHADER_COMPILE_OPTION_BACKWARD_COMPATIBILITY, VKD3D_SHADER_COMPILE_OPTION_BACKCOMPAT_MAP_SEMANTIC_NAMES});

        vkd3d_shader_preprocess_info pp{};
        pp.type = VKD3D_SHADER_STRUCTURE_TYPE_PREPROCESS_INFO;
        pp.macros = macros.data();
        pp.macro_count = unsigned(macros.size());
        pp.pfn_open_include = open_include;
        pp.pfn_close_include = close_include;
        pp.include_context = &inc;

        vkd3d_shader_hlsl_source_info hlsl{};
        hlsl.type = VKD3D_SHADER_STRUCTURE_TYPE_HLSL_SOURCE_INFO;
        hlsl.next = &pp;
        hlsl.entry_point = entrypoint ? entrypoint : "main";
        hlsl.profile = target;

        vkd3d_shader_compile_info info{};
        info.type = VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO;
        info.next = &hlsl;
        info.source.code = data;
        info.source.size = data_size;
        info.source_type = VKD3D_SHADER_SOURCE_HLSL;
        info.target_type = VKD3D_SHADER_TARGET_DXBC_TPF;
        info.options = options.data();
        info.option_count = unsigned(options.size());
        info.log_level = VKD3D_SHADER_LOG_WARNING;
        info.source_name = filename && *filename ? filename : "shader";

        // Two steps: preprocess (macros, includes), rewrite what vkd3d's HLSL front end lacks, compile.
        vkd3d_shader_code pre{};
        char *messages = nullptr;
        if (vkd3d_shader_preprocess(&info, &pre, &messages) < 0)
        {
            if (messages)
            {
                if (error_messages && *messages)
                    make_blob(messages, strlen(messages) + 1, error_messages);
                vkd3d_shader_free_messages(messages);
            }
            return E_FAIL;
        }
        if (messages)
            vkd3d_shader_free_messages(messages);
        messages = nullptr;
        const std::string source = rewrite_for_vkd3d(static_cast<const char *>(pre.code), pre.size, target);
        vkd3d_shader_free_shader_code(&pre);
        hlsl.next = nullptr; // already preprocessed
        info.source.code = source.data();
        info.source.size = source.size();

        vkd3d_shader_code out{};
        const int rc = vkd3d_shader_compile(&info, &out, &messages);
        if (rc < 0)
            dump_failed_shader(source, entrypoint, target, messages);
        if (messages)
        {
            if (error_messages && *messages)
                make_blob(messages, strlen(messages) + 1, error_messages);
            vkd3d_shader_free_messages(messages);
        }
        if (rc < 0)
            return E_FAIL;
        const HRESULT hr = make_blob(out.code, out.size, shader);
        vkd3d_shader_free_shader_code(&out);
        return hr;
    }

    HRESULT WINAPI D3DReflect(const void *data, SIZE_T data_size, REFIID riid, void **reflector)
    {
        if (!reflector)
            return E_INVALIDARG;
        *reflector = nullptr;
        if (riid != IID_ID3D11ShaderReflection)
            return E_NOINTERFACE;
        auto *r = new Reflection();
        std::string error;
        if (!r->init(data, data_size, error))
        {
            fprintf(stderr, "! D3DReflect: %s\n", error.c_str());
            r->Release();
            return E_FAIL;
        }
        *reflector = static_cast<ID3D11ShaderReflection *>(r);
        return S_OK;
    }

    HRESULT WINAPI D3DGetInputSignatureBlob(const void *data, SIZE_T data_size, ID3DBlob **blob)
    {
        // The whole bytecode is a valid input signature for CreateInputLayout (it contains ISGN).
        Chunk c;
        if (!blob || !find_chunk(static_cast<const uint8_t *>(data), data_size, tag('I', 'S', 'G', 'N'), c))
            return E_FAIL;
        return make_blob(data, data_size, blob);
    }

    HRESULT WINAPI D3DDisassemble(const void *data, SIZE_T data_size, UINT, const char *comments, ID3DBlob **disassembly)
    {
        if (!disassembly)
            return E_INVALIDARG;
        *disassembly = nullptr;
        vkd3d_shader_compile_info info{};
        info.type = VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO;
        info.source.code = data;
        info.source.size = data_size;
        info.source_type = VKD3D_SHADER_SOURCE_DXBC_TPF;
        info.target_type = VKD3D_SHADER_TARGET_D3D_ASM;
        info.log_level = VKD3D_SHADER_LOG_ERROR;
        vkd3d_shader_code out{};
        const int rc = vkd3d_shader_compile(&info, &out, nullptr);
        if (rc < 0)
            return E_FAIL;
        std::string text = comments ? std::string(comments) + "\n" : std::string();
        text.append(static_cast<const char *>(out.code), out.size);
        vkd3d_shader_free_shader_code(&out);
        return make_blob(text.c_str(), text.size() + 1, disassembly);
    }

} // extern "C"
