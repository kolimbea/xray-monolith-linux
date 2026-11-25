// State objects of the Vulkan backend: blend, depth-stencil, rasterizer, sampler and input layout. The
// context turns them into dynamic state at draw time (vkContext.cpp).
#include "vkObjects.h"

#include <cstring>

#include "vkFormat.h"

namespace xrvk
{
    bool BlendState::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(ID3D11BlendState)});
    }
    bool DepthStencilState::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(ID3D11DepthStencilState)});
    }
    bool RasterizerState::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(ID3D11RasterizerState)});
    }
    bool SamplerState::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(ID3D11SamplerState)});
    }
    bool InputLayout::has_interface(REFIID r) const
    {
        return iid_in(r, {__uuidof(ID3D11DeviceChild), __uuidof(ID3D11InputLayout)});
    }

    SamplerState::~SamplerState()
    {
        if (Core *c = core(); c && sampler)
        {
            VkSampler s = sampler;
            c->defer([c, s]
                     {
                         vkDestroySampler(c->device, s, nullptr);
                     });
        }
    }

    VkCompareOp compare_op(D3D11_COMPARISON_FUNC f)
    {
        // D3D11_COMPARISON_NEVER = 1 ... ALWAYS = 8, VkCompareOp NEVER = 0 ... ALWAYS = 7
        return f >= D3D11_COMPARISON_NEVER && f <= D3D11_COMPARISON_ALWAYS ? VkCompareOp(f - 1) : VK_COMPARE_OP_ALWAYS;
    }

    VkStencilOp stencil_op(D3D11_STENCIL_OP op)
    {
        switch (op)
        {
        case D3D11_STENCIL_OP_ZERO:
            return VK_STENCIL_OP_ZERO;
        case D3D11_STENCIL_OP_REPLACE:
            return VK_STENCIL_OP_REPLACE;
        case D3D11_STENCIL_OP_INCR_SAT:
            return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case D3D11_STENCIL_OP_DECR_SAT:
            return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case D3D11_STENCIL_OP_INVERT:
            return VK_STENCIL_OP_INVERT;
        case D3D11_STENCIL_OP_INCR:
            return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case D3D11_STENCIL_OP_DECR:
            return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        default:
            return VK_STENCIL_OP_KEEP;
        }
    }

    namespace
    {
        VkBlendFactor blend_factor(D3D11_BLEND b)
        {
            switch (b)
            {
            case D3D11_BLEND_ZERO:
                return VK_BLEND_FACTOR_ZERO;
            case D3D11_BLEND_ONE:
                return VK_BLEND_FACTOR_ONE;
            case D3D11_BLEND_SRC_COLOR:
                return VK_BLEND_FACTOR_SRC_COLOR;
            case D3D11_BLEND_INV_SRC_COLOR:
                return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            case D3D11_BLEND_SRC_ALPHA:
                return VK_BLEND_FACTOR_SRC_ALPHA;
            case D3D11_BLEND_INV_SRC_ALPHA:
                return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            case D3D11_BLEND_DEST_ALPHA:
                return VK_BLEND_FACTOR_DST_ALPHA;
            case D3D11_BLEND_INV_DEST_ALPHA:
                return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            case D3D11_BLEND_DEST_COLOR:
                return VK_BLEND_FACTOR_DST_COLOR;
            case D3D11_BLEND_INV_DEST_COLOR:
                return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
            case D3D11_BLEND_SRC_ALPHA_SAT:
                return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
            case D3D11_BLEND_BLEND_FACTOR:
                return VK_BLEND_FACTOR_CONSTANT_COLOR;
            case D3D11_BLEND_INV_BLEND_FACTOR:
                return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
            case D3D11_BLEND_SRC1_COLOR:
                return VK_BLEND_FACTOR_SRC1_COLOR;
            case D3D11_BLEND_INV_SRC1_COLOR:
                return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
            case D3D11_BLEND_SRC1_ALPHA:
                return VK_BLEND_FACTOR_SRC1_ALPHA;
            case D3D11_BLEND_INV_SRC1_ALPHA:
                return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
            default:
                return VK_BLEND_FACTOR_ONE;
            }
        }
        // In the alpha equation D3D11 reads *_COLOR factors as their alpha counterparts.
        VkBlendFactor alpha_factor(D3D11_BLEND b)
        {
            switch (b)
            {
            case D3D11_BLEND_SRC_COLOR:
                return VK_BLEND_FACTOR_SRC_ALPHA;
            case D3D11_BLEND_INV_SRC_COLOR:
                return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            case D3D11_BLEND_DEST_COLOR:
                return VK_BLEND_FACTOR_DST_ALPHA;
            case D3D11_BLEND_INV_DEST_COLOR:
                return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            case D3D11_BLEND_SRC1_COLOR:
                return VK_BLEND_FACTOR_SRC1_ALPHA;
            case D3D11_BLEND_INV_SRC1_COLOR:
                return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
            case D3D11_BLEND_BLEND_FACTOR:
                return VK_BLEND_FACTOR_CONSTANT_ALPHA;
            case D3D11_BLEND_INV_BLEND_FACTOR:
                return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
            default:
                return blend_factor(b);
            }
        }
        VkBlendOp blend_op(D3D11_BLEND_OP op)
        {
            switch (op)
            {
            case D3D11_BLEND_OP_SUBTRACT:
                return VK_BLEND_OP_SUBTRACT;
            case D3D11_BLEND_OP_REV_SUBTRACT:
                return VK_BLEND_OP_REVERSE_SUBTRACT;
            case D3D11_BLEND_OP_MIN:
                return VK_BLEND_OP_MIN;
            case D3D11_BLEND_OP_MAX:
                return VK_BLEND_OP_MAX;
            default:
                return VK_BLEND_OP_ADD;
            }
        }
        VkSamplerAddressMode address_mode(D3D11_TEXTURE_ADDRESS_MODE m)
        {
            switch (m)
            {
            case D3D11_TEXTURE_ADDRESS_MIRROR:
                return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case D3D11_TEXTURE_ADDRESS_CLAMP:
                return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case D3D11_TEXTURE_ADDRESS_BORDER:
                return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            case D3D11_TEXTURE_ADDRESS_MIRROR_ONCE:
                return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
            default:
                return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            }
        }
        template <class T>
        HRESULT hand_out(T *v, T **out)
        {
            if (out)
                *out = v;
            else
                v->Release();
            return S_OK;
        }
    } // namespace
} // namespace xrvk

using namespace xrvk;

HRESULT ID3D11Device::CreateBlendState(const D3D11_BLEND_DESC *pBlendStateDesc, ID3D11BlendState **ppBlendState)
{
    if (ppBlendState)
        *ppBlendState = nullptr; // D3D11 clears the output, so a failed call never hands out garbage
    if (!pBlendStateDesc)
        return E_INVALIDARG;
    auto *s = new BlendState();
    s->xrvk_device = this;
    s->desc = *pBlendStateDesc;
    for (uint32_t i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        const D3D11_RENDER_TARGET_BLEND_DESC &r = s->desc.RenderTarget[s->desc.IndependentBlendEnable ? i : 0];
        s->enable[i] = r.BlendEnable ? VK_TRUE : VK_FALSE;
        s->equation[i] = {blend_factor(r.SrcBlend), blend_factor(r.DestBlend), blend_op(r.BlendOp), alpha_factor(r.SrcBlendAlpha),
                          alpha_factor(r.DestBlendAlpha), blend_op(r.BlendOpAlpha)};
        s->write_mask[i] = r.RenderTargetWriteMask & 0xf; // D3D11 RGBA bits = VK R|G|B|A bits
    }
    return hand_out<ID3D11BlendState>(s, ppBlendState);
}

HRESULT ID3D11Device::CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC *pDepthStencilDesc, ID3D11DepthStencilState **ppDepthStencilState)
{
    if (ppDepthStencilState)
        *ppDepthStencilState = nullptr; // D3D11 clears the output, so a failed call never hands out garbage
    if (!pDepthStencilDesc)
        return E_INVALIDARG;
    auto *s = new DepthStencilState();
    s->xrvk_device = this;
    s->desc = *pDepthStencilDesc;
    return hand_out<ID3D11DepthStencilState>(s, ppDepthStencilState);
}

HRESULT ID3D11Device::CreateRasterizerState(const D3D11_RASTERIZER_DESC *pRasterizerDesc, ID3D11RasterizerState **ppRasterizerState)
{
    if (ppRasterizerState)
        *ppRasterizerState = nullptr; // D3D11 clears the output, so a failed call never hands out garbage
    if (!pRasterizerDesc)
        return E_INVALIDARG;
    auto *s = new RasterizerState();
    s->xrvk_device = this;
    s->desc = *pRasterizerDesc;
    return hand_out<ID3D11RasterizerState>(s, ppRasterizerState);
}

HRESULT ID3D11Device::CreateSamplerState(const D3D11_SAMPLER_DESC *pSamplerDesc, ID3D11SamplerState **ppSamplerState)
{
    if (ppSamplerState)
        *ppSamplerState = nullptr; // D3D11 clears the output, so a failed call never hands out garbage
    if (!pSamplerDesc)
        return E_INVALIDARG;
    const D3D11_SAMPLER_DESC &d = *pSamplerDesc;
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    const UINT f = UINT(d.Filter);
    // D3D11_FILTER bits: mip linear 0x1, mag linear 0x4, min linear 0x10, anisotropic 0x40, comparison 0x80
    const bool aniso = (f & 0x40) != 0;
    si.minFilter = aniso || (f & 0x10) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    si.magFilter = aniso || (f & 0x04) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    si.mipmapMode = aniso || (f & 0x01) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = address_mode(d.AddressU);
    si.addressModeV = address_mode(d.AddressV);
    si.addressModeW = address_mode(d.AddressW);
    si.mipLodBias = d.MipLODBias;
    si.anisotropyEnable = aniso && d.MaxAnisotropy > 1 ? VK_TRUE : VK_FALSE;
    const float max_aniso = core()->props.limits.maxSamplerAnisotropy;
    si.maxAnisotropy = si.anisotropyEnable ? (float(d.MaxAnisotropy) < max_aniso ? float(d.MaxAnisotropy) : max_aniso) : 1.f;
    si.compareEnable = (f & 0x80) ? VK_TRUE : VK_FALSE;
    si.compareOp = compare_op(d.ComparisonFunc);
    si.minLod = d.MinLOD;
    si.maxLod = d.MaxLOD >= 3.0e38f ? VK_LOD_CLAMP_NONE : d.MaxLOD;
    // custom border colours need VK_EXT_custom_border_color; X-Ray uses the three standard ones
    const float *bc = d.BorderColor;
    if (bc[3] == 0.f)
        si.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    else if (bc[0] == 0.f && bc[1] == 0.f && bc[2] == 0.f)
        si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    else
        si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    auto *s = new SamplerState();
    s->xrvk_device = this;
    s->desc = d;
    XRVK_CHECK(vkCreateSampler(core()->device, &si, nullptr, &s->sampler));
    return hand_out<ID3D11SamplerState>(s, ppSamplerState);
}

HRESULT ID3D11Device::CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs, UINT NumElements,
                                        const void *pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, ID3D11InputLayout **ppInputLayout)
{
    if (ppInputLayout)
        *ppInputLayout = nullptr; // D3D11 clears the output, so a failed call never hands out garbage
    // D3D11 takes either the input signature blob or the whole vertex shader (imgui passes the latter)
    std::vector<ShaderParameter> sig;
    if (is_spirv(pShaderBytecodeWithInputSignature, BytecodeLength))
    {
        ShaderInfo info;
        std::string error;
        if (!reflect_spirv(pShaderBytecodeWithInputSignature, BytecodeLength, info, error))
        {
            log("! vk: CreateInputLayout: %s", error.c_str());
            return E_INVALIDARG;
        }
        for (const ShaderParameter &p : info.inputs)
            if (!p.builtin)
                sig.push_back(p);
    }
    else if (!parse_input_signature(pShaderBytecodeWithInputSignature, BytecodeLength, sig))
    {
        log("! vk: CreateInputLayout: neither a vertex shader nor a signature of this backend");
        return E_INVALIDARG;
    }
    auto *l = new InputLayout();
    l->xrvk_device = this;
    uint32_t next_offset[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
    std::vector<bool> fed(sig.size(), false);
    for (UINT i = 0; i < NumElements; ++i)
    {
        const D3D11_INPUT_ELEMENT_DESC &e = pInputElementDescs[i];
        const VkFormat fmt = resource_format(e.Format, false);
        if (fmt == VK_FORMAT_UNDEFINED || e.InputSlot >= D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)
        {
            log("! vk: CreateInputLayout: element %s%u: unsupported format %u or slot %u", e.SemanticName, e.SemanticIndex, unsigned(e.Format),
                e.InputSlot);
            l->Release();
            return E_INVALIDARG;
        }
        const uint32_t offset = e.AlignedByteOffset == D3D11_APPEND_ALIGNED_ELEMENT ? next_offset[e.InputSlot] : e.AlignedByteOffset;
        uint32_t bw;
        (void)bw;
        // element size, for D3D11_APPEND_ALIGNED_ELEMENT of the next element
        uint32_t size = 4;
        switch (e.Format)
        {
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_UINT:
        case DXGI_FORMAT_R32G32B32A32_SINT:
            size = 16;
            break;
        case DXGI_FORMAT_R32G32B32_FLOAT:
        case DXGI_FORMAT_R32G32B32_UINT:
        case DXGI_FORMAT_R32G32B32_SINT:
            size = 12;
            break;
        case DXGI_FORMAT_R32G32_FLOAT:
        case DXGI_FORMAT_R32G32_UINT:
        case DXGI_FORMAT_R32G32_SINT:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
        case DXGI_FORMAT_R16G16B16A16_SNORM:
        case DXGI_FORMAT_R16G16B16A16_UINT:
        case DXGI_FORMAT_R16G16B16A16_SINT:
            size = 8;
            break;
        case DXGI_FORMAT_R16_FLOAT:
        case DXGI_FORMAT_R16_UNORM:
        case DXGI_FORMAT_R16_SNORM:
        case DXGI_FORMAT_R16_UINT:
        case DXGI_FORMAT_R16_SINT:
        case DXGI_FORMAT_R8G8_UNORM:
        case DXGI_FORMAT_R8G8_SNORM:
        case DXGI_FORMAT_R8G8_UINT:
        case DXGI_FORMAT_R8G8_SINT:
            size = 2;
            break;
        case DXGI_FORMAT_R8_UNORM:
        case DXGI_FORMAT_R8_SNORM:
        case DXGI_FORMAT_R8_UINT:
        case DXGI_FORMAT_R8_SINT:
            size = 1;
            break;
        default:
            break;
        }
        next_offset[e.InputSlot] = offset + size;
        for (size_t k = 0; k < sig.size(); ++k)
            if (sig[k].index == e.SemanticIndex && !strcasecmp(sig[k].semantic.c_str(), e.SemanticName))
            {
                l->attributes.push_back({sig[k].location, e.InputSlot, offset, fmt, e.InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA,
                                         e.InstanceDataStepRate});
                fed[k] = true;
            }
    }
    for (size_t k = 0; k < sig.size(); ++k)
        if (!fed[k])
        {
            log("! vk: CreateInputLayout: the vertex shader reads %s%u, which the layout does not provide", sig[k].semantic.c_str(),
                sig[k].index);
            l->Release();
            return E_INVALIDARG;
        }
    return hand_out<ID3D11InputLayout>(l, ppInputLayout);
}

void ID3D11BlendState::GetDesc(D3D11_BLEND_DESC *pDesc)
{
    *pDesc = static_cast<BlendState *>(this)->desc;
}
void ID3D11DepthStencilState::GetDesc(D3D11_DEPTH_STENCIL_DESC *pDesc)
{
    *pDesc = static_cast<DepthStencilState *>(this)->desc;
}
void ID3D11RasterizerState::GetDesc(D3D11_RASTERIZER_DESC *pDesc)
{
    *pDesc = static_cast<RasterizerState *>(this)->desc;
}
void ID3D11SamplerState::GetDesc(D3D11_SAMPLER_DESC *pDesc)
{
    *pDesc = static_cast<SamplerState *>(this)->desc;
}
