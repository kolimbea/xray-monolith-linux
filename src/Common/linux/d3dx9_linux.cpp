// The D3DX9 math and vertex-declaration functions the renderer calls (d3dx9.lib is Windows-only).
// Semantics follow the D3DX9 documentation: row vectors, row-major matrices, left-handed projections.
// Declarations come from the DirectX SDK headers (sdk/include/dxsdk), so signatures match exactly.
#include "../../xrCore/xrCore_platform.h" // Win32 base types and SAL macros, as everywhere in the engine
#include <d3d9types.h>
#include <d3dx9.h>

#include <cmath>
#include <cstring>

#include "matrix_inverse.h"

namespace
{
    UINT decl_type_size(BYTE type)
    {
        switch (type)
        {
        case D3DDECLTYPE_FLOAT1:
            return 4;
        case D3DDECLTYPE_FLOAT2:
            return 8;
        case D3DDECLTYPE_FLOAT3:
            return 12;
        case D3DDECLTYPE_FLOAT4:
            return 16;
        case D3DDECLTYPE_D3DCOLOR:
        case D3DDECLTYPE_UBYTE4:
        case D3DDECLTYPE_SHORT2:
        case D3DDECLTYPE_UBYTE4N:
        case D3DDECLTYPE_SHORT2N:
        case D3DDECLTYPE_USHORT2N:
        case D3DDECLTYPE_UDEC3:
        case D3DDECLTYPE_DEC3N:
        case D3DDECLTYPE_FLOAT16_2:
            return 4;
        case D3DDECLTYPE_SHORT4:
        case D3DDECLTYPE_SHORT4N:
        case D3DDECLTYPE_USHORT4N:
        case D3DDECLTYPE_FLOAT16_4:
            return 8;
        default:
            return 0;
        }
    }

    uint16_t float_to_half(float f)
    {
        uint32_t x;
        memcpy(&x, &f, 4);
        const uint32_t sign = (x >> 16) & 0x8000;
        const int32_t exp = int32_t((x >> 23) & 0xff) - 127 + 15;
        uint32_t mant = x & 0x7fffff;
        if (((x >> 23) & 0xff) == 0xff) // Inf/NaN
            return uint16_t(sign | 0x7c00 | (mant ? 0x200 : 0));
        if (exp >= 31)
            return uint16_t(sign | 0x7c00); // overflow -> Inf
        if (exp <= 0)
        {
            if (exp < -10)
                return uint16_t(sign); // underflow -> signed zero
            mant |= 0x800000;          // denormal: shift in the implicit bit, round to nearest even
            const int shift = 14 - exp;
            uint32_t h = mant >> shift;
            const uint32_t rem = mant & ((1u << shift) - 1), half = 1u << (shift - 1);
            if (rem > half || (rem == half && (h & 1)))
                ++h;
            return uint16_t(sign | h);
        }
        uint32_t h = uint32_t(exp) << 10 | (mant >> 13);
        const uint32_t rem = mant & 0x1fff;
        if (rem > 0x1000 || (rem == 0x1000 && (h & 1)))
            ++h; // may carry into the exponent, which is the correct rounding (up to Inf)
        return uint16_t(sign | h);
    }

    void mul(D3DXMATRIX &out, const D3DXMATRIX &a, const D3DXMATRIX &b)
    {
        D3DXMATRIX r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
        out = r; // via a temporary: out may alias a or b
    }
} // namespace

extern "C"
{

    D3DXFLOAT16 *WINAPI D3DXFloat32To16Array(D3DXFLOAT16 *out, CONST FLOAT *in, UINT n)
    {
        for (UINT i = 0; i < n; ++i)
        {
            const uint16_t h = float_to_half(in[i]);
            memcpy(&out[i], &h, 2);
        }
        return out;
    }

    D3DXMATRIX *WINAPI D3DXMatrixMultiply(D3DXMATRIX *out, CONST D3DXMATRIX *m1, CONST D3DXMATRIX *m2)
    {
        mul(*out, *m1, *m2);
        return out;
    }

    D3DXMATRIX *WINAPI D3DXMatrixInverse(D3DXMATRIX *out, FLOAT *det, CONST D3DXMATRIX *m)
    {
        const float *t = &m->_11;
        D3DXMATRIX tmp;
        if (!xr_matrix_inverse4x4(&tmp._11, t)) // singular: D3DX returns NULL and leaves out alone
        {
            if (det)
                *det = 0.f;
            return nullptr;
        }
        if (det) // cofactor expansion along the first row
            *det = t[0] * (t[5] * (t[10] * t[15] - t[11] * t[14]) - t[6] * (t[9] * t[15] - t[11] * t[13]) + t[7] * (t[9] * t[14] - t[10] * t[13])) -
                   t[1] * (t[4] * (t[10] * t[15] - t[11] * t[14]) - t[6] * (t[8] * t[15] - t[11] * t[12]) + t[7] * (t[8] * t[14] - t[10] * t[12])) +
                   t[2] * (t[4] * (t[9] * t[15] - t[11] * t[13]) - t[5] * (t[8] * t[15] - t[11] * t[12]) + t[7] * (t[8] * t[13] - t[9] * t[12])) -
                   t[3] * (t[4] * (t[9] * t[14] - t[10] * t[13]) - t[5] * (t[8] * t[14] - t[10] * t[12]) + t[6] * (t[8] * t[13] - t[9] * t[12]));
        *out = tmp;
        return out;
    }

    D3DXMATRIX *WINAPI D3DXMatrixTranslation(D3DXMATRIX *out, FLOAT x, FLOAT y, FLOAT z)
    {
        memset(out, 0, sizeof(*out));
        out->_11 = out->_22 = out->_33 = out->_44 = 1.f;
        out->_41 = x;
        out->_42 = y;
        out->_43 = z;
        return out;
    }

    D3DXMATRIX *WINAPI D3DXMatrixScaling(D3DXMATRIX *out, FLOAT sx, FLOAT sy, FLOAT sz)
    {
        memset(out, 0, sizeof(*out));
        out->_11 = sx;
        out->_22 = sy;
        out->_33 = sz;
        out->_44 = 1.f;
        return out;
    }

    D3DXMATRIX *WINAPI D3DXMatrixOrthoOffCenterLH(D3DXMATRIX *out, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn, FLOAT zf)
    {
        memset(out, 0, sizeof(*out));
        out->_11 = 2.f / (r - l);
        out->_22 = 2.f / (t - b);
        out->_33 = 1.f / (zf - zn);
        out->_41 = (l + r) / (l - r);
        out->_42 = (t + b) / (b - t);
        out->_43 = zn / (zn - zf);
        out->_44 = 1.f;
        return out;
    }

    D3DXVECTOR4 *WINAPI D3DXVec3Transform(D3DXVECTOR4 *out, CONST D3DXVECTOR3 *v, CONST D3DXMATRIX *m)
    {
        const float x = v->x, y = v->y, z = v->z;
        out->x = x * m->_11 + y * m->_21 + z * m->_31 + m->_41;
        out->y = x * m->_12 + y * m->_22 + z * m->_32 + m->_42;
        out->z = x * m->_13 + y * m->_23 + z * m->_33 + m->_43;
        out->w = x * m->_14 + y * m->_24 + z * m->_34 + m->_44;
        return out;
    }

    D3DXVECTOR3 *WINAPI D3DXVec3TransformNormal(D3DXVECTOR3 *out, CONST D3DXVECTOR3 *v, CONST D3DXMATRIX *m)
    {
        const float x = v->x, y = v->y, z = v->z;
        out->x = x * m->_11 + y * m->_21 + z * m->_31;
        out->y = x * m->_12 + y * m->_22 + z * m->_32;
        out->z = x * m->_13 + y * m->_23 + z * m->_33;
        return out;
    }

    D3DXVECTOR3 *WINAPI D3DXVec3TransformCoordArray(
        D3DXVECTOR3 *out, UINT out_stride, CONST D3DXVECTOR3 *in, UINT in_stride, CONST D3DXMATRIX *m, UINT n)
    {
        for (UINT i = 0; i < n; ++i)
        {
            const D3DXVECTOR3 *v = reinterpret_cast<const D3DXVECTOR3 *>(reinterpret_cast<const BYTE *>(in) + size_t(i) * in_stride);
            D3DXVECTOR3 *o = reinterpret_cast<D3DXVECTOR3 *>(reinterpret_cast<BYTE *>(out) + size_t(i) * out_stride);
            D3DXVECTOR4 r;
            D3DXVec3Transform(&r, v, m);
            const float inv = r.w != 0.f ? 1.f / r.w : 0.f; // D3DX divides by w; w == 0 gives zero here
            o->x = r.x * inv;
            o->y = r.y * inv;
            o->z = r.z * inv;
        }
        return out;
    }

    D3DXVECTOR3 *WINAPI D3DXVec3Normalize(D3DXVECTOR3 *out, CONST D3DXVECTOR3 *v)
    {
        const float len = std::sqrt(v->x * v->x + v->y * v->y + v->z * v->z);
        if (len == 0.f) // D3DX returns a zero vector for zero length
        {
            out->x = out->y = out->z = 0.f;
            return out;
        }
        const float inv = 1.f / len;
        out->x = v->x * inv;
        out->y = v->y * inv;
        out->z = v->z * inv;
        return out;
    }

    UINT WINAPI D3DXGetFVFVertexSize(DWORD fvf)
    {
        return xr_fvf_vertex_size(fvf);
    }

    UINT WINAPI D3DXGetDeclLength(CONST D3DVERTEXELEMENT9 *decl)
    {
        UINT n = 0;
        while (decl[n].Stream != 0xFF && n < MAXD3DDECLLENGTH)
            ++n;
        return n;
    }

    UINT WINAPI D3DXGetDeclVertexSize(CONST D3DVERTEXELEMENT9 *decl, DWORD stream)
    {
        UINT size = 0;
        for (UINT i = 0; decl[i].Stream != 0xFF && i < MAXD3DDECLLENGTH; ++i)
            if (decl[i].Stream == stream)
            {
                const UINT end = decl[i].Offset + decl_type_size(decl[i].Type);
                if (end > size)
                    size = end;
            }
        return size;
    }

    HRESULT WINAPI D3DXDeclaratorFromFVF(DWORD fvf, D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE])
    {
        UINT n = 0;
        WORD offset = 0;
        auto add = [&](BYTE type, BYTE usage, BYTE index)
        {
            decl[n++] = D3DVERTEXELEMENT9{0, offset, type, D3DDECLMETHOD_DEFAULT, usage, index};
            offset = WORD(offset + decl_type_size(type));
        };
        const DWORD pos = fvf & D3DFVF_POSITION_MASK;
        const bool idx_ubyte4 = (fvf & D3DFVF_LASTBETA_UBYTE4) != 0, idx_color = (fvf & D3DFVF_LASTBETA_D3DCOLOR) != 0;
        const bool has_idx = idx_ubyte4 || idx_color;
        const BYTE idx_type = idx_ubyte4 ? BYTE(D3DDECLTYPE_UBYTE4) : BYTE(D3DDECLTYPE_D3DCOLOR);
        switch (pos)
        {
        case 0:
            break;
        case D3DFVF_XYZ:
            add(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0);
            break;
        case D3DFVF_XYZRHW:
            add(D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITIONT, 0);
            break;
        case D3DFVF_XYZW:
            add(D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITION, 0);
            break;
        case D3DFVF_XYZB1:
        case D3DFVF_XYZB2:
        case D3DFVF_XYZB3:
        case D3DFVF_XYZB4:
        case D3DFVF_XYZB5: {
            add(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0);
            const int betas = int((pos - D3DFVF_XYZB1) / 2) + 1; // XYZB1..5 are 6, 8, 10, 12, 14
            const int weights = has_idx ? betas - 1 : betas;     // the last beta holds the indices
            if (weights > 4)
                return D3DERR_INVALIDCALL;
            if (weights > 0)
                add(BYTE(D3DDECLTYPE_FLOAT1 + weights - 1), D3DDECLUSAGE_BLENDWEIGHT, 0);
            if (has_idx)
                add(idx_type, D3DDECLUSAGE_BLENDINDICES, 0);
            break;
        }
        default:
            return D3DERR_INVALIDCALL;
        }
        if (fvf & D3DFVF_NORMAL)
            add(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_NORMAL, 0);
        if (fvf & D3DFVF_PSIZE)
            add(D3DDECLTYPE_FLOAT1, D3DDECLUSAGE_PSIZE, 0);
        if (fvf & D3DFVF_DIFFUSE)
            add(D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 0);
        if (fvf & D3DFVF_SPECULAR)
            add(D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 1);
        const UINT tex = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
        if (tex > 8)
            return D3DERR_INVALIDCALL;
        for (UINT i = 0; i < tex; ++i)
        {
            static const BYTE types[4] = {D3DDECLTYPE_FLOAT2, D3DDECLTYPE_FLOAT3, D3DDECLTYPE_FLOAT4, D3DDECLTYPE_FLOAT1};
            add(types[(fvf >> (16 + i * 2)) & 3], D3DDECLUSAGE_TEXCOORD, BYTE(i));
        }
        decl[n] = D3DVERTEXELEMENT9 D3DDECL_END();
        return D3D_OK;
    }

} // extern "C"
