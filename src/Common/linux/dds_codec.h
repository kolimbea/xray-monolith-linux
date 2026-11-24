// DDS container parsing/writing and the few pixel conversions D3DX11 performed for the renderer.
// Independent of D3D objects so it can be unit-tested (tests/compat_dds.cpp).
//
// References: "DDS_HEADER", "DDS_PIXELFORMAT", "DDS_HEADER_DXT10" and "Map Direct3D 9 formats to
// Direct3D 10" (Microsoft Learn), BC1 block layout (Microsoft Learn, "Block compression").
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace xr_dds
{
    // DXGI_FORMAT values used here (dxgiformat.h numbering).
    enum Format : uint32_t
    {
        FMT_UNKNOWN = 0,
        FMT_R32G32B32A32_FLOAT = 2,
        FMT_R16G16B16A16_FLOAT = 10,
        FMT_R16G16B16A16_UNORM = 11,
        FMT_R16G16B16A16_SNORM = 13,
        FMT_R32G32_FLOAT = 16,
        FMT_R10G10B10A2_UNORM = 24,
        FMT_R11G11B10_FLOAT = 26,
        FMT_R8G8B8A8_UNORM = 28,
        FMT_R8G8B8A8_UNORM_SRGB = 29,
        FMT_R8G8B8A8_SNORM = 31,
        FMT_R16G16_FLOAT = 34,
        FMT_R16G16_UNORM = 35,
        FMT_R16G16_SNORM = 37,
        FMT_R32_FLOAT = 41,
        FMT_R8G8_UNORM = 49,
        FMT_R8G8_SNORM = 51,
        FMT_R16_FLOAT = 54,
        FMT_R16_UNORM = 56,
        FMT_R8_UNORM = 61,
        FMT_A8_UNORM = 65,
        FMT_R8G8_B8G8_UNORM = 68,
        FMT_G8R8_G8B8_UNORM = 69,
        FMT_BC1_UNORM = 71,
        FMT_BC1_UNORM_SRGB = 72,
        FMT_BC2_UNORM = 74,
        FMT_BC2_UNORM_SRGB = 75,
        FMT_BC3_UNORM = 77,
        FMT_BC3_UNORM_SRGB = 78,
        FMT_BC4_UNORM = 80,
        FMT_BC4_SNORM = 81,
        FMT_BC5_UNORM = 83,
        FMT_BC5_SNORM = 84,
        FMT_B8G8R8A8_UNORM = 87,
        FMT_B8G8R8X8_UNORM = 88,
        FMT_B8G8R8A8_UNORM_SRGB = 91,
        FMT_B8G8R8X8_UNORM_SRGB = 93,
        FMT_BC6H_UF16 = 95,
        FMT_BC6H_SF16 = 96,
        FMT_BC7_UNORM = 98,
        FMT_BC7_UNORM_SRGB = 99,
    };

    // How file pixels become texture pixels (formats DXGI lacks are expanded like D3DX did).
    enum class Convert
    {
        None,
        BGR24_to_BGRA32, // D3DFMT_R8G8B8
        L8_to_RGBA8,     // D3DFMT_L8: (L, L, L, 1), as D3D9 sampled it
        A8L8_to_RGBA8,   // D3DFMT_A8L8
        A4L4_to_RGBA8,
        B5G6R5_to_BGRA8,
        B5G5R5A1_to_BGRA8, // also X1R5G5B5 (force_opaque)
        B4G4R4A4_to_BGRA8, // also X4R4G4B4 (force_opaque)
    };

    enum class Dim
    {
        Tex1D,
        Tex2D,
        Tex3D,
    };

    struct Subresource
    {
        const uint8_t *data; // file data (before conversion)
        size_t size;
        uint32_t width, height, depth;
        uint32_t row_pitch, slice_pitch; // of the file data
    };

    struct Image
    {
        Dim dim = Dim::Tex2D;
        bool cube = false;
        uint32_t width = 0, height = 0, depth = 1;
        uint32_t array_size = 1; // faces included for cubes (6 per cube)
        uint32_t mip_levels = 1;
        Format format = FMT_UNKNOWN; // texture format after conversion
        Convert convert = Convert::None;
        bool force_opaque = false;
        // array_size * mip_levels entries in D3D11 subresource order: index = mip + element * mip_levels.
        std::vector<Subresource> subresources;
    };

    bool is_block_compressed(Format f);
    uint32_t block_bytes(Format f);    // 8 or 16 for BC formats, 0 otherwise
    uint32_t bits_per_pixel(Format f); // 0 for unknown/unsupported formats
    // Row pitch and row count of one mip level (rows of 4x4 blocks for BC formats).
    bool surface_info(Format f, uint32_t width, uint32_t height, uint32_t &row_pitch, uint32_t &rows);

    // Parses a DDS file (the data must outlive `out`). On failure returns false and sets `error`.
    bool parse(const void *file, size_t size, Image &out, std::string &error);

    // Converts one subresource's file pixels to the texture format (Convert::None copies).
    std::vector<uint8_t> convert_subresource(const Image &img, const Subresource &s, uint32_t &dst_row_pitch);

    // Decodes/encodes `count` texels of an uncompressed format from/to RGBA8; false if unsupported.
    bool decode_row_rgba8(Format f, const uint8_t *src, uint32_t count, uint8_t *rgba);
    bool encode_row_rgba8(Format f, const uint8_t *rgba, uint32_t count, uint8_t *dst);
    // BC1 (DXT1) encoding of a 4x4 RGBA8 block (16 texels, row-major), opaque 4-colour mode.
    void encode_bc1_block(const uint8_t rgba[64], uint8_t out[8]);
    // Encodes a whole RGBA8 image to BC1 (edge blocks replicate the last row/column).
    std::vector<uint8_t> encode_bc1(const uint8_t *rgba, uint32_t w, uint32_t h);
    // Box-filtered resize of an RGBA8 image (each destination texel averages its source footprint).
    void resize_rgba8(const uint8_t *src, uint32_t sw, uint32_t sh, uint8_t *dst, uint32_t dw, uint32_t dh);

    // Writes a DDS file for a 2D texture; `levels` holds every subresource tightly packed, in D3D11
    // order. Uses a legacy header where D3D9 had the format, otherwise the DX10 extension header.
    std::vector<uint8_t> write_dds_2d(Format f, uint32_t width, uint32_t height, uint32_t mips, uint32_t array_size,
                                      bool cube, const std::vector<std::vector<uint8_t>> &levels);

    // Image files from RGBA8 pixels (top row first).
    std::vector<uint8_t> write_bmp(const uint8_t *rgba, uint32_t w, uint32_t h);
    std::vector<uint8_t> write_png(const uint8_t *rgba, uint32_t w, uint32_t h); // stored (uncompressed) deflate
} // namespace xr_dds
