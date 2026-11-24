// The D3DX11 texture functions the renderer calls (d3dx11.lib is Windows-only), on top of the
// D3D11 API (dxvk-native today, our own renderer's D3D11-shaped backend later) and dds_codec.
//
// Covered: DDS files (every texture of Anomaly/GAMMA, see tests/compat_dds.cpp), copies between
// uncompressed RGBA8-class textures and into BC1 (save-game thumbnails), saving to DDS/BMP/PNG/JPG
// and mip generation. Not covered, reported with E_NOTIMPL/E_FAIL and a log line: other source
// image containers (the few PNGs renamed to .dds in mods), BC decoding, filters other than box.
#include "../../xrCore/xrCore_platform.h" // Win32 base types and SAL macros, as everywhere in the engine
#include <d3d11.h>
#include <d3dcompiler.h>
#include <D3DX11tex.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "dds_codec.h"
#include "jpeg/jpeglib.h"

namespace
{
    using namespace xr_dds;

    void log_error(const char *fn, const char *what)
    {
        fprintf(stderr, "! %s: %s\n", fn, what);
    }

    template <typename T>
    void release(T *&p)
    {
        if (p)
            p->Release();
        p = nullptr;
    }

    HRESULT set_hr(HRESULT *out, HRESULT hr)
    {
        if (out)
            *out = hr;
        return hr;
    }

    D3D11_RESOURCE_DIMENSION to_dimension(Dim d)
    {
        switch (d)
        {
        case Dim::Tex1D:
            return D3D11_RESOURCE_DIMENSION_TEXTURE1D;
        case Dim::Tex3D:
            return D3D11_RESOURCE_DIMENSION_TEXTURE3D;
        case Dim::Tex2D:
            break;
        }
        return D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    }

    void fill_info(const Image &img, D3DX11_IMAGE_INFO *info)
    {
        info->Width = img.width;
        info->Height = img.height;
        info->Depth = img.depth;
        info->ArraySize = img.array_size;
        info->MipLevels = img.mip_levels;
        info->MiscFlags = img.cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
        info->Format = DXGI_FORMAT(img.format);
        info->ResourceDimension = to_dimension(img.dim);
        info->ImageFileFormat = D3DX11_IFF_DDS;
    }

    UINT or_default(UINT v, UINT def)
    {
        return v == D3DX11_DEFAULT || v == D3DX11_FROM_FILE ? def : v;
    }

    // Reads every subresource of a 2D texture through a staging copy; tightly packed rows.
    HRESULT read_texture2d(ID3D11DeviceContext *ctx, ID3D11Texture2D *tex, D3D11_TEXTURE2D_DESC &desc,
                           std::vector<std::vector<uint8_t>> &levels)
    {
        tex->GetDesc(&desc);
        D3D11_TEXTURE2D_DESC sd = desc;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.MiscFlags &= D3D11_RESOURCE_MISC_TEXTURECUBE;
        sd.SampleDesc = {1, 0};
        if (desc.SampleDesc.Count > 1)
            return E_NOTIMPL; // multisampled sources would need a resolve first
        ID3D11Device *dev = nullptr;
        ctx->GetDevice(&dev);
        ID3D11Texture2D *staging = nullptr;
        HRESULT hr = dev->CreateTexture2D(&sd, nullptr, &staging);
        dev->Release();
        if (FAILED(hr))
            return hr;
        ctx->CopyResource(staging, tex);
        levels.clear();
        for (UINT e = 0; e < desc.ArraySize && SUCCEEDED(hr); ++e)
            for (UINT m = 0; m < desc.MipLevels; ++m)
            {
                const UINT sub = D3D11CalcSubresource(m, e, desc.MipLevels);
                uint32_t pitch, rows;
                if (!surface_info(Format(desc.Format), std::max(1u, desc.Width >> m), std::max(1u, desc.Height >> m), pitch, rows))
                {
                    hr = E_NOTIMPL;
                    break;
                }
                D3D11_MAPPED_SUBRESOURCE map;
                hr = ctx->Map(staging, sub, D3D11_MAP_READ, 0, &map);
                if (FAILED(hr))
                    break;
                std::vector<uint8_t> data(size_t(pitch) * rows);
                for (uint32_t r = 0; r < rows; ++r)
                    memcpy(data.data() + size_t(r) * pitch, static_cast<const uint8_t *>(map.pData) + size_t(r) * map.RowPitch, pitch);
                ctx->Unmap(staging, sub);
                levels.push_back(std::move(data));
            }
        staging->Release();
        return hr;
    }

    // Mip 0 of an uncompressed texture as RGBA8.
    bool to_rgba8(Format f, const std::vector<uint8_t> &level, uint32_t w, uint32_t h, std::vector<uint8_t> &rgba)
    {
        uint32_t pitch, rows;
        if (!surface_info(f, w, h, pitch, rows) || is_block_compressed(f))
            return false;
        rgba.resize(size_t(w) * h * 4);
        for (uint32_t y = 0; y < h; ++y)
            if (!decode_row_rgba8(f, level.data() + size_t(y) * pitch, w, rgba.data() + size_t(y) * w * 4))
                return false;
        return true;
    }

    // RGBA8 -> one subresource of `f` (tightly packed); false for formats we cannot encode.
    bool from_rgba8(Format f, const uint8_t *rgba, uint32_t w, uint32_t h, std::vector<uint8_t> &out, uint32_t &pitch)
    {
        if (f == FMT_BC1_UNORM || f == FMT_BC1_UNORM_SRGB)
        {
            out = encode_bc1(rgba, w, h);
            pitch = std::max(1u, (w + 3) / 4) * 8;
            return true;
        }
        uint32_t rows;
        if (!surface_info(f, w, h, pitch, rows))
            return false;
        out.resize(size_t(pitch) * rows);
        for (uint32_t y = 0; y < h; ++y)
            if (!encode_row_rgba8(f, rgba + size_t(y) * w * 4, w, out.data() + size_t(y) * pitch))
                return false;
        return true;
    }

    std::vector<uint8_t> write_jpg(const uint8_t *rgba, uint32_t w, uint32_t h)
    {
        jpeg_compress_struct cinfo;
        jpeg_error_mgr jerr;
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_compress(&cinfo);
        unsigned char *mem = nullptr;
        unsigned long mem_size = 0;
        jpeg_mem_dest(&cinfo, &mem, &mem_size);
        cinfo.image_width = w;
        cinfo.image_height = h;
        cinfo.input_components = 3;
        cinfo.in_color_space = JCS_RGB;
        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, 95, TRUE);
        jpeg_start_compress(&cinfo, TRUE);
        std::vector<uint8_t> row(size_t(w) * 3);
        while (cinfo.next_scanline < h)
        {
            const uint8_t *s = rgba + size_t(cinfo.next_scanline) * w * 4;
            for (uint32_t x = 0; x < w; ++x)
                memcpy(&row[x * 3], s + x * 4, 3);
            JSAMPROW rp = row.data();
            jpeg_write_scanlines(&cinfo, &rp, 1);
        }
        jpeg_finish_compress(&cinfo);
        std::vector<uint8_t> out(mem, mem + mem_size);
        jpeg_destroy_compress(&cinfo);
        free(mem);
        return out;
    }

    HRESULT save_to_bytes(ID3D11DeviceContext *ctx, ID3D11Resource *res, D3DX11_IMAGE_FILE_FORMAT fmt, std::vector<uint8_t> &out)
    {
        D3D11_RESOURCE_DIMENSION dim;
        res->GetType(&dim);
        if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
        {
            log_error("D3DX11SaveTexture", "only 2D textures are supported");
            return E_NOTIMPL;
        }
        D3D11_TEXTURE2D_DESC desc;
        std::vector<std::vector<uint8_t>> levels;
        HRESULT hr = read_texture2d(ctx, static_cast<ID3D11Texture2D *>(res), desc, levels);
        if (FAILED(hr))
            return hr;
        if (fmt == D3DX11_IFF_DDS)
        {
            out = write_dds_2d(Format(desc.Format), desc.Width, desc.Height, desc.MipLevels, desc.ArraySize,
                               (desc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0, levels);
            return S_OK;
        }
        std::vector<uint8_t> rgba;
        if (!to_rgba8(Format(desc.Format), levels[0], desc.Width, desc.Height, rgba))
        {
            log_error("D3DX11SaveTexture", "source format cannot be converted to an image file");
            return E_NOTIMPL;
        }
        switch (fmt)
        {
        case D3DX11_IFF_BMP:
            out = write_bmp(rgba.data(), desc.Width, desc.Height);
            return S_OK;
        case D3DX11_IFF_PNG:
            out = write_png(rgba.data(), desc.Width, desc.Height);
            return S_OK;
        case D3DX11_IFF_JPG:
            out = write_jpg(rgba.data(), desc.Width, desc.Height);
            return S_OK;
        default:
            log_error("D3DX11SaveTexture", "unsupported file format");
            return E_NOTIMPL;
        }
    }
} // namespace

extern "C"
{

    HRESULT WINAPI D3DX11GetImageInfoFromMemory(
        LPCVOID data, SIZE_T size, ID3DX11ThreadPump *, D3DX11_IMAGE_INFO *info, HRESULT *out_hr)
    {
        Image img;
        std::string err;
        if (!info || !parse(data, size, img, err))
        {
            if (info)
                log_error("D3DX11GetImageInfoFromMemory", err.c_str());
            return set_hr(out_hr, E_FAIL);
        }
        fill_info(img, info);
        return set_hr(out_hr, S_OK);
    }

    HRESULT WINAPI D3DX11CreateTextureFromMemory(ID3D11Device *dev, LPCVOID data, SIZE_T size, D3DX11_IMAGE_LOAD_INFO *li,
                                                 ID3DX11ThreadPump *, ID3D11Resource **out, HRESULT *out_hr)
    {
        if (!dev || !out)
            return set_hr(out_hr, E_INVALIDARG);
        *out = nullptr;
        Image img;
        std::string err;
        if (!parse(data, size, img, err))
        {
            log_error("D3DX11CreateTextureFromMemory", err.c_str());
            return set_hr(out_hr, E_FAIL);
        }
        D3DX11_IMAGE_LOAD_INFO defaults;
        if (!li)
            li = &defaults;
        if (li->pSrcInfo)
            fill_info(img, li->pSrcInfo);

        // Mip range: skip FirstMipLevel, keep at most MipLevels (no mip generation: files carry theirs).
        const UINT first = std::min(or_default(li->FirstMipLevel, 0), img.mip_levels - 1);
        UINT mips = img.mip_levels - first;
        if (li->MipLevels != D3DX11_DEFAULT && li->MipLevels != D3DX11_FROM_FILE && li->MipLevels != 0)
            mips = std::min(mips, li->MipLevels);
        if (li->Format != DXGI_FORMAT_FROM_FILE && li->Format != DXGI_FORMAT(D3DX11_DEFAULT) && li->Format != DXGI_FORMAT(img.format))
            log_error("D3DX11CreateTextureFromMemory", "format conversion on load is not supported; keeping the file format");

        const D3D11_USAGE usage = D3D11_USAGE(or_default(UINT(li->Usage), D3D11_USAGE_DEFAULT));
        const UINT bind = or_default(li->BindFlags, usage == D3D11_USAGE_STAGING ? 0 : D3D11_BIND_SHADER_RESOURCE);
        const UINT cpu = or_default(li->CpuAccessFlags, 0);
        const UINT misc = (or_default(li->MiscFlags, 0) & ~D3D11_RESOURCE_MISC_TEXTURECUBE) | (img.cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0);

        // Converted copies must stay alive until the create call returns.
        std::vector<std::vector<uint8_t>> converted;
        std::vector<D3D11_SUBRESOURCE_DATA> init;
        converted.reserve(size_t(img.array_size) * mips);
        for (UINT e = 0; e < img.array_size; ++e)
            for (UINT m = first; m < first + mips; ++m)
            {
                const Subresource &s = img.subresources[size_t(e) * img.mip_levels + m];
                D3D11_SUBRESOURCE_DATA d{};
                if (img.convert == Convert::None)
                {
                    d.pSysMem = s.data;
                    d.SysMemPitch = s.row_pitch;
                    d.SysMemSlicePitch = s.slice_pitch;
                }
                else
                {
                    uint32_t pitch;
                    converted.push_back(convert_subresource(img, s, pitch));
                    d.pSysMem = converted.back().data();
                    d.SysMemPitch = pitch;
                    d.SysMemSlicePitch = pitch * s.height;
                }
                init.push_back(d);
            }
        const Subresource &top = img.subresources[first];
        HRESULT hr;
        switch (img.dim)
        {
        case Dim::Tex1D: {
            D3D11_TEXTURE1D_DESC d{top.width, mips, img.array_size, DXGI_FORMAT(img.format), usage, bind, cpu, misc};
            ID3D11Texture1D *t = nullptr;
            hr = dev->CreateTexture1D(&d, init.data(), &t);
            *out = t;
            break;
        }
        case Dim::Tex3D: {
            D3D11_TEXTURE3D_DESC d{top.width, top.height, top.depth, mips, DXGI_FORMAT(img.format), usage, bind, cpu, misc};
            ID3D11Texture3D *t = nullptr;
            hr = dev->CreateTexture3D(&d, init.data(), &t);
            *out = t;
            break;
        }
        case Dim::Tex2D:
        default: {
            D3D11_TEXTURE2D_DESC d{top.width, top.height, mips, img.array_size, DXGI_FORMAT(img.format), {1, 0}, usage, bind, cpu, misc};
            ID3D11Texture2D *t = nullptr;
            hr = dev->CreateTexture2D(&d, init.data(), &t);
            *out = t;
            break;
        }
        }
        if (FAILED(hr))
            log_error("D3DX11CreateTextureFromMemory", "texture creation failed");
        return set_hr(out_hr, hr);
    }

    HRESULT WINAPI D3DX11LoadTextureFromTexture(
        ID3D11DeviceContext *ctx, ID3D11Resource *src, D3DX11_TEXTURE_LOAD_INFO *li, ID3D11Resource *dst)
    {
        if (!ctx || !src || !dst)
            return E_INVALIDARG;
        if (li && (li->pSrcBox || li->pDstBox))
        {
            log_error("D3DX11LoadTextureFromTexture", "boxes are not supported");
            return E_NOTIMPL;
        }
        D3D11_RESOURCE_DIMENSION sdim, ddim;
        src->GetType(&sdim);
        dst->GetType(&ddim);
        if (sdim != D3D11_RESOURCE_DIMENSION_TEXTURE2D || ddim != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
        {
            log_error("D3DX11LoadTextureFromTexture", "only 2D textures are supported");
            return E_NOTIMPL;
        }
        D3D11_TEXTURE2D_DESC sd, dd;
        static_cast<ID3D11Texture2D *>(dst)->GetDesc(&dd);
        static_cast<ID3D11Texture2D *>(src)->GetDesc(&sd);
        if (sd.Width == dd.Width && sd.Height == dd.Height && sd.Format == dd.Format && sd.MipLevels == dd.MipLevels &&
            sd.ArraySize == dd.ArraySize)
        {
            ctx->CopyResource(dst, src);
            return S_OK;
        }
        std::vector<std::vector<uint8_t>> levels;
        HRESULT hr = read_texture2d(ctx, static_cast<ID3D11Texture2D *>(src), sd, levels);
        if (FAILED(hr))
            return hr;
        std::vector<uint8_t> rgba;
        if (!to_rgba8(Format(sd.Format), levels[0], sd.Width, sd.Height, rgba))
        {
            log_error("D3DX11LoadTextureFromTexture", "source format not supported");
            return E_NOTIMPL;
        }
        if (dd.Usage != D3D11_USAGE_DEFAULT)
        {
            log_error("D3DX11LoadTextureFromTexture", "destination must have D3D11_USAGE_DEFAULT");
            return E_NOTIMPL;
        }
        // Every destination mip of element 0 from the box-filtered source.
        for (UINT m = 0; m < dd.MipLevels; ++m)
        {
            const uint32_t w = std::max(1u, dd.Width >> m), h = std::max(1u, dd.Height >> m);
            std::vector<uint8_t> scaled(size_t(w) * h * 4), packed;
            resize_rgba8(rgba.data(), sd.Width, sd.Height, scaled.data(), w, h);
            uint32_t pitch;
            if (!from_rgba8(Format(dd.Format), scaled.data(), w, h, packed, pitch))
            {
                log_error("D3DX11LoadTextureFromTexture", "destination format not supported");
                return E_NOTIMPL;
            }
            ctx->UpdateSubresource(dst, D3D11CalcSubresource(m, 0, dd.MipLevels), nullptr, packed.data(), pitch, 0);
        }
        return S_OK;
    }

    HRESULT WINAPI D3DX11FilterTexture(ID3D11DeviceContext *ctx, ID3D11Resource *tex, UINT src_level, UINT)
    {
        // The mip filter is ignored: levels are always box-filtered (D3DX11's default).
        if (!ctx || !tex)
            return E_INVALIDARG;
        D3D11_RESOURCE_DIMENSION dim;
        tex->GetType(&dim);
        if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
            return E_NOTIMPL;
        D3D11_TEXTURE2D_DESC d;
        std::vector<std::vector<uint8_t>> levels;
        HRESULT hr = read_texture2d(ctx, static_cast<ID3D11Texture2D *>(tex), d, levels);
        if (FAILED(hr))
            return hr;
        if (d.Usage != D3D11_USAGE_DEFAULT || src_level >= d.MipLevels)
            return E_INVALIDARG;
        for (UINT e = 0; e < d.ArraySize; ++e)
        {
            std::vector<uint8_t> rgba;
            const uint32_t sw = std::max(1u, d.Width >> src_level), sh = std::max(1u, d.Height >> src_level);
            if (!to_rgba8(Format(d.Format), levels[size_t(e) * d.MipLevels + src_level], sw, sh, rgba))
            {
                log_error("D3DX11FilterTexture", "format not supported");
                return E_NOTIMPL;
            }
            for (UINT m = src_level + 1; m < d.MipLevels; ++m)
            {
                const uint32_t w = std::max(1u, d.Width >> m), h = std::max(1u, d.Height >> m);
                std::vector<uint8_t> scaled(size_t(w) * h * 4), packed;
                resize_rgba8(rgba.data(), sw, sh, scaled.data(), w, h);
                uint32_t pitch;
                if (!from_rgba8(Format(d.Format), scaled.data(), w, h, packed, pitch))
                    return E_NOTIMPL;
                ctx->UpdateSubresource(tex, D3D11CalcSubresource(m, e, d.MipLevels), nullptr, packed.data(), pitch, 0);
            }
        }
        return S_OK;
    }

    HRESULT WINAPI D3DX11SaveTextureToMemory(
        ID3D11DeviceContext *ctx, ID3D11Resource *tex, D3DX11_IMAGE_FILE_FORMAT fmt, ID3D10Blob **out, UINT)
    {
        if (!ctx || !tex || !out)
            return E_INVALIDARG;
        *out = nullptr;
        std::vector<uint8_t> bytes;
        HRESULT hr = save_to_bytes(ctx, tex, fmt, bytes);
        if (FAILED(hr))
            return hr;
        hr = D3DCreateBlob(bytes.size(), out);
        if (FAILED(hr))
            return hr;
        memcpy((*out)->GetBufferPointer(), bytes.data(), bytes.size());
        return S_OK;
    }

    HRESULT WINAPI D3DX11SaveTextureToFileA(ID3D11DeviceContext *ctx, ID3D11Resource *tex, D3DX11_IMAGE_FILE_FORMAT fmt, LPCSTR path)
    {
        if (!ctx || !tex || !path)
            return E_INVALIDARG;
        std::vector<uint8_t> bytes;
        HRESULT hr = save_to_bytes(ctx, tex, fmt, bytes);
        if (FAILED(hr))
            return hr;
        FILE *f = fopen(path, "wb");
        if (!f)
        {
            log_error("D3DX11SaveTextureToFileA", path);
            return E_FAIL;
        }
        const bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
        return fclose(f) == 0 && ok ? S_OK : E_FAIL;
    }

} // extern "C"
