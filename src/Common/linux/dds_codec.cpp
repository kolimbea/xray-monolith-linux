// See dds_codec.h.
#include "dds_codec.h"

#include <algorithm>
#include <cmath>

namespace xr_dds
{
    namespace
    {
        constexpr uint32_t DDS_MAGIC = 0x20534444; // "DDS "
        constexpr uint32_t DDSD_CAPS = 0x1, DDSD_HEIGHT = 0x2, DDSD_WIDTH = 0x4, DDSD_PITCH = 0x8, DDSD_PIXELFORMAT = 0x1000,
                           DDSD_MIPMAPCOUNT = 0x20000, DDSD_LINEARSIZE = 0x80000, DDSD_DEPTH = 0x800000;
        constexpr uint32_t DDPF_ALPHAPIXELS = 0x1, DDPF_ALPHA = 0x2, DDPF_FOURCC = 0x4, DDPF_RGB = 0x40,
                           DDPF_LUMINANCE = 0x20000, DDPF_BUMPDUDV = 0x80000;
        constexpr uint32_t DDSCAPS_COMPLEX = 0x8, DDSCAPS_TEXTURE = 0x1000, DDSCAPS_MIPMAP = 0x400000;
        constexpr uint32_t DDSCAPS2_CUBEMAP = 0x200, DDSCAPS2_CUBEMAP_ALLFACES = 0xFC00, DDSCAPS2_VOLUME = 0x200000;
        constexpr uint32_t DX10_MISC_TEXTURECUBE = 0x4;
        constexpr size_t HEADER_SIZE = 124, DX10_SIZE = 20;

        constexpr uint32_t fourcc(char a, char b, char c, char d)
        {
            return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24;
        }

        uint32_t rd32(const uint8_t *p)
        {
            return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
        }
        void wr32(std::vector<uint8_t> &v, uint32_t x)
        {
            for (int i = 0; i < 4; ++i)
                v.push_back(uint8_t(x >> (8 * i)));
        }
        void wr16(std::vector<uint8_t> &v, uint32_t x)
        {
            v.push_back(uint8_t(x));
            v.push_back(uint8_t(x >> 8));
        }
        void wr32be(std::vector<uint8_t> &v, uint32_t x)
        {
            for (int i = 3; i >= 0; --i)
                v.push_back(uint8_t(x >> (8 * i)));
        }

        struct PixelFormat
        {
            uint32_t flags, fourcc, bits, r, g, b, a;
        };

        // Legacy pixel format -> texture format (+ conversion). Mirrors the D3D9 -> DXGI table.
        bool map_legacy(const PixelFormat &pf, Format &f, Convert &cv, bool &opaque)
        {
            cv = Convert::None;
            opaque = false;
            auto masks = [&](uint32_t r, uint32_t g, uint32_t b, uint32_t a)
            {
                return pf.r == r && pf.g == g && pf.b == b && pf.a == a;
            };
            if (pf.flags & DDPF_FOURCC)
            {
                switch (pf.fourcc)
                {
                case fourcc('D', 'X', 'T', '1'):
                    f = FMT_BC1_UNORM;
                    return true;
                case fourcc('D', 'X', 'T', '2'):
                case fourcc('D', 'X', 'T', '3'):
                    f = FMT_BC2_UNORM;
                    return true;
                case fourcc('D', 'X', 'T', '4'):
                case fourcc('D', 'X', 'T', '5'):
                    f = FMT_BC3_UNORM;
                    return true;
                case fourcc('A', 'T', 'I', '1'):
                case fourcc('B', 'C', '4', 'U'):
                    f = FMT_BC4_UNORM;
                    return true;
                case fourcc('B', 'C', '4', 'S'):
                    f = FMT_BC4_SNORM;
                    return true;
                case fourcc('A', 'T', 'I', '2'):
                case fourcc('B', 'C', '5', 'U'):
                    f = FMT_BC5_UNORM;
                    return true;
                case fourcc('B', 'C', '5', 'S'):
                    f = FMT_BC5_SNORM;
                    return true;
                case fourcc('R', 'G', 'B', 'G'):
                    f = FMT_R8G8_B8G8_UNORM;
                    return true;
                case fourcc('G', 'R', 'G', 'B'):
                    f = FMT_G8R8_G8B8_UNORM;
                    return true;
                // D3DFORMAT numbers stored in the FourCC field
                case 36:
                    f = FMT_R16G16B16A16_UNORM;
                    return true;
                case 110:
                    f = FMT_R16G16B16A16_SNORM;
                    return true;
                case 111:
                    f = FMT_R16_FLOAT;
                    return true;
                case 112:
                    f = FMT_R16G16_FLOAT;
                    return true;
                case 113:
                    f = FMT_R16G16B16A16_FLOAT;
                    return true;
                case 114:
                    f = FMT_R32_FLOAT;
                    return true;
                case 115:
                    f = FMT_R32G32_FLOAT;
                    return true;
                case 116:
                    f = FMT_R32G32B32A32_FLOAT;
                    return true;
                default:
                    return false;
                }
            }
            if (pf.flags & DDPF_RGB)
            {
                const uint32_t a = (pf.flags & DDPF_ALPHAPIXELS) ? pf.a : 0;
                switch (pf.bits)
                {
                case 32:
                    if (masks(0xff, 0xff00, 0xff0000, 0xff000000) && a)
                    {
                        f = FMT_R8G8B8A8_UNORM;
                        return true;
                    }
                    if (pf.r == 0xff0000 && pf.g == 0xff00 && pf.b == 0xff)
                    {
                        f = a ? FMT_B8G8R8A8_UNORM : FMT_B8G8R8X8_UNORM;
                        return true;
                    }
                    if (pf.r == 0xff && pf.g == 0xff00 && pf.b == 0xff0000)
                    {
                        f = FMT_R8G8B8A8_UNORM;
                        return true;
                    } // X8B8G8R8
                    if (pf.r == 0x3ff && pf.g == 0xffc00 && pf.b == 0x3ff00000)
                    {
                        f = FMT_R10G10B10A2_UNORM;
                        return true;
                    }
                    if (pf.r == 0xffff && pf.g == 0xffff0000 && !pf.b)
                    {
                        f = FMT_R16G16_UNORM;
                        return true;
                    }
                    if (pf.r == 0xffffffff && !pf.g && !pf.b)
                    {
                        f = FMT_R32_FLOAT;
                        return true;
                    }
                    return false;
                case 24:
                    if (pf.r == 0xff0000 && pf.g == 0xff00 && pf.b == 0xff)
                    {
                        f = FMT_B8G8R8A8_UNORM;
                        cv = Convert::BGR24_to_BGRA32;
                        return true;
                    }
                    return false;
                case 16:
                    // A8L8 written by some tools as DDPF_RGB with identical R/G/B masks
                    if (pf.r == 0xff && pf.g == 0xff && pf.b == 0xff && a == 0xff00)
                    {
                        f = FMT_R8G8B8A8_UNORM;
                        cv = Convert::A8L8_to_RGBA8;
                        return true;
                    }
                    if (pf.r == 0xf800 && pf.g == 0x7e0 && pf.b == 0x1f)
                    {
                        f = FMT_B8G8R8A8_UNORM;
                        cv = Convert::B5G6R5_to_BGRA8;
                        return true;
                    }
                    if (pf.r == 0x7c00 && pf.g == 0x3e0 && pf.b == 0x1f)
                    {
                        f = FMT_B8G8R8A8_UNORM;
                        cv = Convert::B5G5R5A1_to_BGRA8;
                        opaque = !a;
                        return true;
                    }
                    if (pf.r == 0xf00 && pf.g == 0xf0 && pf.b == 0xf)
                    {
                        f = FMT_B8G8R8A8_UNORM;
                        cv = Convert::B4G4R4A4_to_BGRA8;
                        opaque = !a;
                        return true;
                    }
                    return false;
                case 8:
                    if (pf.r == 0xff && pf.g == 0xff && pf.b == 0xff) // L8 written as DDPF_RGB
                    {
                        f = FMT_R8G8B8A8_UNORM;
                        cv = Convert::L8_to_RGBA8;
                        return true;
                    }
                    return false;
                default:
                    return false;
                }
            }
            if (pf.flags & DDPF_LUMINANCE)
            {
                if (pf.bits == 8 && pf.r == 0xff)
                {
                    f = FMT_R8G8B8A8_UNORM;
                    cv = Convert::L8_to_RGBA8;
                    return true;
                }
                if (pf.bits == 8 && pf.r == 0x0f && (pf.flags & DDPF_ALPHAPIXELS))
                {
                    f = FMT_R8G8B8A8_UNORM;
                    cv = Convert::A4L4_to_RGBA8;
                    return true;
                }
                if (pf.bits == 16 && pf.r == 0xff && (pf.flags & DDPF_ALPHAPIXELS))
                {
                    f = FMT_R8G8B8A8_UNORM;
                    cv = Convert::A8L8_to_RGBA8;
                    return true;
                }
                if (pf.bits == 16 && pf.r == 0xffff)
                {
                    f = FMT_R16_UNORM;
                    return true;
                }
                return false;
            }
            if (pf.flags & DDPF_ALPHA)
            {
                if (pf.bits == 8)
                {
                    f = FMT_A8_UNORM;
                    return true;
                }
                return false;
            }
            if (pf.flags & DDPF_BUMPDUDV)
            {
                if (pf.bits == 16 && pf.r == 0xff && pf.g == 0xff00)
                {
                    f = FMT_R8G8_SNORM;
                    return true;
                } // V8U8
                if (pf.bits == 32 && pf.r == 0xff && pf.g == 0xff00 && pf.b == 0xff0000)
                {
                    f = FMT_R8G8B8A8_SNORM;
                    return true;
                } // Q8W8V8U8
                if (pf.bits == 32 && pf.r == 0xffff && pf.g == 0xffff0000)
                {
                    f = FMT_R16G16_SNORM;
                    return true;
                } // V16U16
                return false;
            }
            return false;
        }

        // File-side bytes per texel for converted formats (the texture format differs).
        uint32_t file_bpp(const Image &img)
        {
            switch (img.convert)
            {
            case Convert::BGR24_to_BGRA32:
                return 24;
            case Convert::L8_to_RGBA8:
            case Convert::A4L4_to_RGBA8:
                return 8;
            case Convert::A8L8_to_RGBA8:
            case Convert::B5G6R5_to_BGRA8:
            case Convert::B5G5R5A1_to_BGRA8:
            case Convert::B4G4R4A4_to_BGRA8:
                return 16;
            case Convert::None:
                return 0;
            }
            return 0;
        }

        uint8_t expand(uint32_t v, int bits)
        {
            return uint8_t((v * 255 + ((1u << bits) - 1) / 2) / ((1u << bits) - 1));
        }

        float half_to_float(uint16_t h)
        {
            const uint32_t s = (h >> 15) & 1, e = (h >> 10) & 0x1f, m = h & 0x3ff;
            float v;
            if (e == 0)
                v = std::ldexp(float(m), -24);
            else if (e == 31)
                v = m ? NAN : INFINITY;
            else
                v = std::ldexp(float(m | 0x400), int(e) - 25);
            return s ? -v : v;
        }

        uint8_t unorm8(float v)
        {
            if (!(v > 0.f)) // also NaN
                return 0;
            if (v >= 1.f)
                return 255;
            return uint8_t(v * 255.f + 0.5f);
        }

        uint32_t crc32(const uint8_t *p, size_t n, uint32_t crc = 0)
        {
            static uint32_t table[256];
            static bool init = false;
            if (!init)
            {
                for (uint32_t i = 0; i < 256; ++i)
                {
                    uint32_t c = i;
                    for (int k = 0; k < 8; ++k)
                        c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                    table[i] = c;
                }
                init = true;
            }
            crc = ~crc;
            for (size_t i = 0; i < n; ++i)
                crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
            return ~crc;
        }
    } // namespace

    bool is_block_compressed(Format f)
    {
        return block_bytes(f) != 0;
    }

    uint32_t block_bytes(Format f)
    {
        switch (f)
        {
        case FMT_BC1_UNORM:
        case FMT_BC1_UNORM_SRGB:
        case FMT_BC4_UNORM:
        case FMT_BC4_SNORM:
            return 8;
        case FMT_BC2_UNORM:
        case FMT_BC2_UNORM_SRGB:
        case FMT_BC3_UNORM:
        case FMT_BC3_UNORM_SRGB:
        case FMT_BC5_UNORM:
        case FMT_BC5_SNORM:
        case FMT_BC6H_UF16:
        case FMT_BC6H_SF16:
        case FMT_BC7_UNORM:
        case FMT_BC7_UNORM_SRGB:
            return 16;
        default:
            return 0;
        }
    }

    uint32_t bits_per_pixel(Format f)
    {
        switch (f)
        {
        case FMT_R32G32B32A32_FLOAT:
            return 128;
        case FMT_R16G16B16A16_FLOAT:
        case FMT_R16G16B16A16_UNORM:
        case FMT_R16G16B16A16_SNORM:
        case FMT_R32G32_FLOAT:
            return 64;
        case FMT_R10G10B10A2_UNORM:
        case FMT_R11G11B10_FLOAT:
        case FMT_R8G8B8A8_UNORM:
        case FMT_R8G8B8A8_UNORM_SRGB:
        case FMT_R8G8B8A8_SNORM:
        case FMT_R16G16_FLOAT:
        case FMT_R16G16_UNORM:
        case FMT_R16G16_SNORM:
        case FMT_R32_FLOAT:
        case FMT_B8G8R8A8_UNORM:
        case FMT_B8G8R8X8_UNORM:
        case FMT_B8G8R8A8_UNORM_SRGB:
        case FMT_B8G8R8X8_UNORM_SRGB:
        case FMT_R8G8_B8G8_UNORM:
        case FMT_G8R8_G8B8_UNORM:
            return 32;
        case FMT_R8G8_UNORM:
        case FMT_R8G8_SNORM:
        case FMT_R16_FLOAT:
        case FMT_R16_UNORM:
            return 16;
        case FMT_R8_UNORM:
        case FMT_A8_UNORM:
            return 8;
        case FMT_BC1_UNORM:
        case FMT_BC1_UNORM_SRGB:
        case FMT_BC4_UNORM:
        case FMT_BC4_SNORM:
            return 4;
        default:
            return is_block_compressed(f) ? 8 : 0;
        }
    }

    bool surface_info(Format f, uint32_t width, uint32_t height, uint32_t &row_pitch, uint32_t &rows)
    {
        width = std::max(width, 1u);
        height = std::max(height, 1u);
        if (const uint32_t bb = block_bytes(f))
        {
            row_pitch = std::max(1u, (width + 3) / 4) * bb;
            rows = std::max(1u, (height + 3) / 4);
            return true;
        }
        if (f == FMT_R8G8_B8G8_UNORM || f == FMT_G8R8_G8B8_UNORM)
        {
            row_pitch = ((width + 1) / 2) * 4;
            rows = height;
            return true;
        }
        const uint32_t bpp = bits_per_pixel(f);
        if (!bpp)
            return false;
        row_pitch = (width * bpp + 7) / 8;
        rows = height;
        return true;
    }

    bool parse(const void *file, size_t size, Image &img, std::string &error)
    {
        const uint8_t *p = static_cast<const uint8_t *>(file);
        img = Image();
        if (size < 4 + HEADER_SIZE || rd32(p) != DDS_MAGIC || rd32(p + 4) != HEADER_SIZE)
        {
            error = "not a DDS file";
            return false;
        }
        const uint8_t *h = p + 4;
        const uint32_t flags = rd32(h + 4);
        img.height = rd32(h + 8);
        img.width = rd32(h + 12);
        const uint32_t depth = rd32(h + 20);
        const uint32_t mips = rd32(h + 24);
        const PixelFormat pf{rd32(h + 76), rd32(h + 80), rd32(h + 84), rd32(h + 88), rd32(h + 92), rd32(h + 96), rd32(h + 100)};
        const uint32_t caps2 = rd32(h + 108);
        size_t offset = 4 + HEADER_SIZE;
        img.mip_levels = (flags & DDSD_MIPMAPCOUNT) && mips ? mips : 1;

        if ((pf.flags & DDPF_FOURCC) && pf.fourcc == fourcc('D', 'X', '1', '0'))
        {
            if (size < offset + DX10_SIZE)
            {
                error = "truncated DX10 header";
                return false;
            }
            const uint8_t *x = p + offset;
            offset += DX10_SIZE;
            img.format = Format(rd32(x));
            const uint32_t dimension = rd32(x + 4), misc = rd32(x + 8);
            img.array_size = std::max(1u, rd32(x + 12));
            switch (dimension)
            {
            case 2:
                img.dim = Dim::Tex1D;
                img.height = 1;
                break;
            case 3:
                img.dim = Dim::Tex2D;
                if (misc & DX10_MISC_TEXTURECUBE)
                {
                    img.cube = true;
                    img.array_size *= 6;
                }
                break;
            case 4:
                img.dim = Dim::Tex3D;
                img.depth = std::max(1u, depth);
                img.array_size = 1;
                break;
            default:
                error = "unknown DX10 resource dimension";
                return false;
            }
            if (!bits_per_pixel(img.format))
            {
                error = "unsupported DXGI format " + std::to_string(img.format);
                return false;
            }
        }
        else
        {
            if (!map_legacy(pf, img.format, img.convert, img.force_opaque))
            {
                error = "unsupported legacy pixel format (flags " + std::to_string(pf.flags) + ", fourcc " +
                        std::to_string(pf.fourcc) + ", bits " + std::to_string(pf.bits) + ")";
                return false;
            }
            if (caps2 & DDSCAPS2_CUBEMAP)
            {
                if ((caps2 & DDSCAPS2_CUBEMAP_ALLFACES) != DDSCAPS2_CUBEMAP_ALLFACES)
                {
                    error = "partial cube maps are not supported"; // not allowed by D3D10+ either
                    return false;
                }
                img.cube = true;
                img.array_size = 6;
            }
            else if ((caps2 & DDSCAPS2_VOLUME) && (flags & DDSD_DEPTH) && depth > 1)
            {
                img.dim = Dim::Tex3D;
                img.depth = depth;
            }
        }
        if (!img.width || !img.height)
        {
            error = "zero-sized image";
            return false;
        }
        // Clamp a mip count that claims more levels than the chain has (seen in hand-edited files).
        const uint32_t max_dim = std::max({img.width, img.height, img.depth});
        uint32_t full_chain = 1;
        while ((max_dim >> full_chain) && full_chain < 32)
            ++full_chain;
        img.mip_levels = std::min(img.mip_levels, full_chain);

        const uint32_t fbpp = file_bpp(img);
        img.subresources.reserve(size_t(img.array_size) * img.mip_levels);
        for (uint32_t e = 0; e < img.array_size; ++e)
        {
            uint32_t w = img.width, hgt = img.height, d = img.depth;
            for (uint32_t m = 0; m < img.mip_levels; ++m)
            {
                uint32_t pitch, rows;
                if (fbpp)
                {
                    pitch = (w * fbpp + 7) / 8;
                    rows = hgt;
                }
                else if (!surface_info(img.format, w, hgt, pitch, rows))
                {
                    error = "cannot size format";
                    return false;
                }
                const size_t slice = size_t(pitch) * rows, bytes = slice * d;
                if (offset + bytes > size)
                {
                    error = "truncated pixel data";
                    return false;
                }
                img.subresources.push_back(Subresource{p + offset, bytes, w, hgt, d, pitch, uint32_t(slice)});
                offset += bytes;
                w = std::max(1u, w / 2);
                hgt = std::max(1u, hgt / 2);
                d = std::max(1u, d / 2);
            }
        }
        return true;
    }

    std::vector<uint8_t> convert_subresource(const Image &img, const Subresource &s, uint32_t &dst_pitch)
    {
        if (img.convert == Convert::None)
        {
            dst_pitch = s.row_pitch;
            return std::vector<uint8_t>(s.data, s.data + s.size);
        }
        dst_pitch = s.width * 4;
        const size_t rows = size_t(s.height) * s.depth;
        std::vector<uint8_t> out(size_t(dst_pitch) * rows);
        for (size_t r = 0; r < rows; ++r)
        {
            const uint8_t *src = s.data + r * s.row_pitch;
            uint8_t *dst = out.data() + r * dst_pitch;
            for (uint32_t x = 0; x < s.width; ++x, dst += 4)
            {
                switch (img.convert)
                {
                case Convert::BGR24_to_BGRA32:
                    dst[0] = src[x * 3];
                    dst[1] = src[x * 3 + 1];
                    dst[2] = src[x * 3 + 2];
                    dst[3] = 255;
                    break;
                case Convert::L8_to_RGBA8:
                    dst[0] = dst[1] = dst[2] = src[x];
                    dst[3] = 255;
                    break;
                case Convert::A4L4_to_RGBA8:
                    dst[0] = dst[1] = dst[2] = expand(src[x] & 0xf, 4);
                    dst[3] = expand(src[x] >> 4, 4);
                    break;
                case Convert::A8L8_to_RGBA8:
                    dst[0] = dst[1] = dst[2] = src[x * 2];
                    dst[3] = src[x * 2 + 1];
                    break;
                case Convert::B5G6R5_to_BGRA8: {
                    const uint32_t v = src[x * 2] | src[x * 2 + 1] << 8;
                    dst[0] = expand(v & 0x1f, 5);
                    dst[1] = expand((v >> 5) & 0x3f, 6);
                    dst[2] = expand(v >> 11, 5);
                    dst[3] = 255;
                    break;
                }
                case Convert::B5G5R5A1_to_BGRA8: {
                    const uint32_t v = src[x * 2] | src[x * 2 + 1] << 8;
                    dst[0] = expand(v & 0x1f, 5);
                    dst[1] = expand((v >> 5) & 0x1f, 5);
                    dst[2] = expand((v >> 10) & 0x1f, 5);
                    dst[3] = img.force_opaque || (v & 0x8000) ? 255 : 0;
                    break;
                }
                case Convert::B4G4R4A4_to_BGRA8: {
                    const uint32_t v = src[x * 2] | src[x * 2 + 1] << 8;
                    dst[0] = expand(v & 0xf, 4);
                    dst[1] = expand((v >> 4) & 0xf, 4);
                    dst[2] = expand((v >> 8) & 0xf, 4);
                    dst[3] = img.force_opaque ? 255 : expand(v >> 12, 4);
                    break;
                }
                case Convert::None:
                    break;
                }
            }
        }
        return out;
    }

    bool decode_row_rgba8(Format f, const uint8_t *s, uint32_t n, uint8_t *d)
    {
        switch (f)
        {
        case FMT_R8G8B8A8_UNORM:
        case FMT_R8G8B8A8_UNORM_SRGB:
            memcpy(d, s, size_t(n) * 4);
            return true;
        case FMT_B8G8R8A8_UNORM:
        case FMT_B8G8R8A8_UNORM_SRGB:
        case FMT_B8G8R8X8_UNORM:
        case FMT_B8G8R8X8_UNORM_SRGB: {
            const bool x = f == FMT_B8G8R8X8_UNORM || f == FMT_B8G8R8X8_UNORM_SRGB;
            for (uint32_t i = 0; i < n; ++i, s += 4, d += 4)
            {
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = x ? 255 : s[3];
            }
            return true;
        }
        case FMT_R10G10B10A2_UNORM:
            for (uint32_t i = 0; i < n; ++i, s += 4, d += 4)
            {
                const uint32_t v = rd32(s);
                d[0] = uint8_t((v & 0x3ff) >> 2);
                d[1] = uint8_t(((v >> 10) & 0x3ff) >> 2);
                d[2] = uint8_t(((v >> 20) & 0x3ff) >> 2);
                d[3] = expand(v >> 30, 2);
            }
            return true;
        case FMT_R16G16B16A16_FLOAT:
            for (uint32_t i = 0; i < n; ++i, s += 8, d += 4)
                for (int c = 0; c < 4; ++c)
                    d[c] = unorm8(half_to_float(uint16_t(s[c * 2] | s[c * 2 + 1] << 8)));
            return true;
        case FMT_R8_UNORM:
            for (uint32_t i = 0; i < n; ++i, d += 4)
            {
                d[0] = d[1] = d[2] = s[i];
                d[3] = 255;
            }
            return true;
        default:
            return false;
        }
    }

    bool encode_row_rgba8(Format f, const uint8_t *s, uint32_t n, uint8_t *d)
    {
        switch (f)
        {
        case FMT_R8G8B8A8_UNORM:
        case FMT_R8G8B8A8_UNORM_SRGB:
            memcpy(d, s, size_t(n) * 4);
            return true;
        case FMT_B8G8R8A8_UNORM:
        case FMT_B8G8R8A8_UNORM_SRGB:
        case FMT_B8G8R8X8_UNORM:
        case FMT_B8G8R8X8_UNORM_SRGB:
            for (uint32_t i = 0; i < n; ++i, s += 4, d += 4)
            {
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = s[3];
            }
            return true;
        default:
            return false;
        }
    }

    void encode_bc1_block(const uint8_t px[64], uint8_t out[8])
    {
        // Endpoints: the block's colour bounding box (exact for two-colour blocks such as UI art).
        int mn[3] = {255, 255, 255}, mx[3] = {0, 0, 0};
        for (int i = 0; i < 16; ++i)
            for (int c = 0; c < 3; ++c)
            {
                mn[c] = std::min(mn[c], int(px[i * 4 + c]));
                mx[c] = std::max(mx[c], int(px[i * 4 + c]));
            }
        auto to565 = [](const int *c)
        {
            return uint16_t((c[0] >> 3) << 11 | (c[1] >> 2) << 5 | (c[2] >> 3));
        };
        uint16_t c0 = to565(mx), c1 = to565(mn);
        if (c0 < c1)
            std::swap(c0, c1);
        auto from565 = [](uint16_t v, int *c)
        {
            c[0] = expand(v >> 11, 5);
            c[1] = expand((v >> 5) & 0x3f, 6);
            c[2] = expand(v & 0x1f, 5);
        };
        int pal[4][3];
        from565(c0, pal[0]);
        from565(c1, pal[1]);
        for (int c = 0; c < 3; ++c)
        {
            pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3;
            pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
        }
        uint32_t indices = 0;
        if (c0 != c1) // equal endpoints would select 3-colour mode; index 0 everywhere is exact then
            for (int i = 0; i < 16; ++i)
            {
                int best = 0, best_d = 1 << 30;
                for (int k = 0; k < 4; ++k)
                {
                    int dd = 0;
                    for (int c = 0; c < 3; ++c)
                    {
                        const int e = int(px[i * 4 + c]) - pal[k][c];
                        dd += e * e;
                    }
                    if (dd < best_d)
                    {
                        best_d = dd;
                        best = k;
                    }
                }
                indices |= uint32_t(best) << (2 * i);
            }
        out[0] = uint8_t(c0);
        out[1] = uint8_t(c0 >> 8);
        out[2] = uint8_t(c1);
        out[3] = uint8_t(c1 >> 8);
        for (int i = 0; i < 4; ++i)
            out[4 + i] = uint8_t(indices >> (8 * i));
    }

    std::vector<uint8_t> encode_bc1(const uint8_t *rgba, uint32_t w, uint32_t h)
    {
        const uint32_t bw = std::max(1u, (w + 3) / 4), bh = std::max(1u, (h + 3) / 4);
        std::vector<uint8_t> out(size_t(bw) * bh * 8);
        uint8_t block[64];
        for (uint32_t by = 0; by < bh; ++by)
            for (uint32_t bx = 0; bx < bw; ++bx)
            {
                for (uint32_t y = 0; y < 4; ++y)
                    for (uint32_t x = 0; x < 4; ++x)
                    {
                        const uint32_t sx = std::min(bx * 4 + x, w - 1), sy = std::min(by * 4 + y, h - 1);
                        memcpy(block + (y * 4 + x) * 4, rgba + (size_t(sy) * w + sx) * 4, 4);
                    }
                encode_bc1_block(block, out.data() + (size_t(by) * bw + bx) * 8);
            }
        return out;
    }

    void resize_rgba8(const uint8_t *src, uint32_t sw, uint32_t sh, uint8_t *dst, uint32_t dw, uint32_t dh)
    {
        for (uint32_t y = 0; y < dh; ++y)
        {
            const uint32_t y0 = uint32_t(uint64_t(y) * sh / dh), y1 = std::max(y0 + 1, uint32_t(uint64_t(y + 1) * sh / dh));
            for (uint32_t x = 0; x < dw; ++x)
            {
                const uint32_t x0 = uint32_t(uint64_t(x) * sw / dw), x1 = std::max(x0 + 1, uint32_t(uint64_t(x + 1) * sw / dw));
                uint32_t sum[4] = {0, 0, 0, 0};
                for (uint32_t yy = y0; yy < y1; ++yy)
                    for (uint32_t xx = x0; xx < x1; ++xx)
                        for (int c = 0; c < 4; ++c)
                            sum[c] += src[(size_t(yy) * sw + xx) * 4 + c];
                const uint32_t n = (y1 - y0) * (x1 - x0);
                for (int c = 0; c < 4; ++c)
                    dst[(size_t(y) * dw + x) * 4 + c] = uint8_t((sum[c] + n / 2) / n);
            }
        }
    }

    std::vector<uint8_t> write_dds_2d(Format f, uint32_t width, uint32_t height, uint32_t mips, uint32_t array_size,
                                      bool cube, const std::vector<std::vector<uint8_t>> &levels)
    {
        std::vector<uint8_t> v;
        wr32(v, DDS_MAGIC);
        PixelFormat pf{};
        bool dx10 = false;
        switch (f)
        {
        case FMT_BC1_UNORM:
            pf = {DDPF_FOURCC, fourcc('D', 'X', 'T', '1'), 0, 0, 0, 0, 0};
            break;
        case FMT_BC2_UNORM:
            pf = {DDPF_FOURCC, fourcc('D', 'X', 'T', '3'), 0, 0, 0, 0, 0};
            break;
        case FMT_BC3_UNORM:
            pf = {DDPF_FOURCC, fourcc('D', 'X', 'T', '5'), 0, 0, 0, 0, 0};
            break;
        case FMT_B8G8R8A8_UNORM:
            pf = {DDPF_RGB | DDPF_ALPHAPIXELS, 0, 32, 0xff0000, 0xff00, 0xff, 0xff000000};
            break;
        case FMT_B8G8R8X8_UNORM:
            pf = {DDPF_RGB, 0, 32, 0xff0000, 0xff00, 0xff, 0};
            break;
        case FMT_R8G8B8A8_UNORM:
            pf = {DDPF_RGB | DDPF_ALPHAPIXELS, 0, 32, 0xff, 0xff00, 0xff0000, 0xff000000};
            break;
        default:
            pf = {DDPF_FOURCC, fourcc('D', 'X', '1', '0'), 0, 0, 0, 0, 0};
            dx10 = true;
            break;
        }
        uint32_t pitch = 0, rows = 0;
        surface_info(f, width, height, pitch, rows);
        const bool bc = is_block_compressed(f);
        uint32_t flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | (bc ? DDSD_LINEARSIZE : DDSD_PITCH);
        if (mips > 1)
            flags |= DDSD_MIPMAPCOUNT;
        wr32(v, uint32_t(HEADER_SIZE));
        wr32(v, flags);
        wr32(v, height);
        wr32(v, width);
        wr32(v, bc ? pitch * rows : pitch);
        wr32(v, 0); // depth
        wr32(v, mips);
        for (int i = 0; i < 11; ++i)
            wr32(v, 0);
        wr32(v, 32);
        wr32(v, pf.flags);
        wr32(v, pf.fourcc);
        wr32(v, pf.bits);
        wr32(v, pf.r);
        wr32(v, pf.g);
        wr32(v, pf.b);
        wr32(v, pf.a);
        wr32(v, DDSCAPS_TEXTURE | (mips > 1 ? DDSCAPS_MIPMAP | DDSCAPS_COMPLEX : 0) | (cube ? DDSCAPS_COMPLEX : 0));
        wr32(v, cube ? DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_ALLFACES : 0);
        wr32(v, 0);
        wr32(v, 0);
        wr32(v, 0);
        if (dx10)
        {
            wr32(v, f);
            wr32(v, 3); // TEXTURE2D
            wr32(v, cube ? DX10_MISC_TEXTURECUBE : 0);
            wr32(v, cube ? array_size / 6 : array_size);
            wr32(v, 0);
        }
        for (const auto &l : levels)
            v.insert(v.end(), l.begin(), l.end());
        return v;
    }

    std::vector<uint8_t> write_bmp(const uint8_t *rgba, uint32_t w, uint32_t h)
    {
        const uint32_t row = (w * 3 + 3) & ~3u, image = row * h;
        std::vector<uint8_t> v;
        v.push_back('B');
        v.push_back('M');
        wr32(v, 54 + image);
        wr32(v, 0);
        wr32(v, 54);
        wr32(v, 40); // BITMAPINFOHEADER
        wr32(v, w);
        wr32(v, h); // positive: bottom-up rows
        wr16(v, 1);
        wr16(v, 24);
        wr32(v, 0); // BI_RGB
        wr32(v, image);
        wr32(v, 2835);
        wr32(v, 2835);
        wr32(v, 0);
        wr32(v, 0);
        for (uint32_t y = h; y-- > 0;)
        {
            const uint8_t *s = rgba + size_t(y) * w * 4;
            for (uint32_t x = 0; x < w; ++x, s += 4)
            {
                v.push_back(s[2]);
                v.push_back(s[1]);
                v.push_back(s[0]);
            }
            for (uint32_t p = w * 3; p < row; ++p)
                v.push_back(0);
        }
        return v;
    }

    std::vector<uint8_t> write_png(const uint8_t *rgba, uint32_t w, uint32_t h)
    {
        std::vector<uint8_t> v = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
        auto chunk = [&v](const char *type, const std::vector<uint8_t> &data)
        {
            wr32be(v, uint32_t(data.size()));
            const size_t start = v.size();
            v.insert(v.end(), type, type + 4);
            v.insert(v.end(), data.begin(), data.end());
            wr32be(v, crc32(v.data() + start, v.size() - start));
        };
        std::vector<uint8_t> ihdr;
        wr32be(ihdr, w);
        wr32be(ihdr, h);
        ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA, deflate, adaptive filtering, no interlace
        chunk("IHDR", ihdr);
        // Filtered scanlines (filter type 0) wrapped in zlib with stored deflate blocks.
        std::vector<uint8_t> raw;
        raw.reserve((size_t(w) * 4 + 1) * h);
        for (uint32_t y = 0; y < h; ++y)
        {
            raw.push_back(0);
            raw.insert(raw.end(), rgba + size_t(y) * w * 4, rgba + size_t(y + 1) * w * 4);
        }
        std::vector<uint8_t> z = {0x78, 0x01};
        size_t pos = 0;
        do
        {
            const size_t n = std::min<size_t>(65535, raw.size() - pos);
            z.push_back(pos + n == raw.size() ? 1 : 0);
            wr16(z, uint32_t(n));
            wr16(z, uint32_t(~n & 0xffff));
            z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
            pos += n;
        } while (pos < raw.size());
        uint32_t a = 1, b = 0; // Adler-32
        for (uint8_t c : raw)
        {
            a = (a + c) % 65521;
            b = (b + a) % 65521;
        }
        wr32be(z, b << 16 | a);
        chunk("IDAT", z);
        chunk("IEND", {});
        return v;
    }
} // namespace xr_dds
