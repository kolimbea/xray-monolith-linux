// See vkFormat.h. Mapping per the Vulkan spec's "Compatible formats" table and the DXGI_FORMAT docs.
#include "../../xrCore/xrCore_platform.h"
#include "vkCommonTypes.h"
#include "vkFormat.h"

namespace xrvk
{
    namespace
    {
        bool g_d24 = true;
        VkFormat d24s8()
        {
            return g_d24 ? VK_FORMAT_D24_UNORM_S8_UINT : VK_FORMAT_D32_SFLOAT_S8_UINT;
        }

        VkFormat concrete(uint32_t f)
        {
            switch (DXGI_FORMAT(f))
            {
            case DXGI_FORMAT_R32G32B32A32_FLOAT:
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            case DXGI_FORMAT_R32G32B32A32_UINT:
                return VK_FORMAT_R32G32B32A32_UINT;
            case DXGI_FORMAT_R32G32B32A32_SINT:
                return VK_FORMAT_R32G32B32A32_SINT;
            case DXGI_FORMAT_R32G32B32_FLOAT:
                return VK_FORMAT_R32G32B32_SFLOAT;
            case DXGI_FORMAT_R32G32B32_UINT:
                return VK_FORMAT_R32G32B32_UINT;
            case DXGI_FORMAT_R32G32B32_SINT:
                return VK_FORMAT_R32G32B32_SINT;
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
                return VK_FORMAT_R16G16B16A16_SFLOAT;
            case DXGI_FORMAT_R16G16B16A16_UNORM:
                return VK_FORMAT_R16G16B16A16_UNORM;
            case DXGI_FORMAT_R16G16B16A16_UINT:
                return VK_FORMAT_R16G16B16A16_UINT;
            case DXGI_FORMAT_R16G16B16A16_SNORM:
                return VK_FORMAT_R16G16B16A16_SNORM;
            case DXGI_FORMAT_R16G16B16A16_SINT:
                return VK_FORMAT_R16G16B16A16_SINT;
            case DXGI_FORMAT_R32G32_FLOAT:
                return VK_FORMAT_R32G32_SFLOAT;
            case DXGI_FORMAT_R32G32_UINT:
                return VK_FORMAT_R32G32_UINT;
            case DXGI_FORMAT_R32G32_SINT:
                return VK_FORMAT_R32G32_SINT;
            case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
                return VK_FORMAT_D32_SFLOAT_S8_UINT;
            case DXGI_FORMAT_R10G10B10A2_UNORM:
                return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
            case DXGI_FORMAT_R10G10B10A2_UINT:
                return VK_FORMAT_A2B10G10R10_UINT_PACK32;
            case DXGI_FORMAT_R11G11B10_FLOAT:
                return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
            case DXGI_FORMAT_R8G8B8A8_UNORM:
                return VK_FORMAT_R8G8B8A8_UNORM;
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                return VK_FORMAT_R8G8B8A8_SRGB;
            case DXGI_FORMAT_R8G8B8A8_UINT:
                return VK_FORMAT_R8G8B8A8_UINT;
            case DXGI_FORMAT_R8G8B8A8_SNORM:
                return VK_FORMAT_R8G8B8A8_SNORM;
            case DXGI_FORMAT_R8G8B8A8_SINT:
                return VK_FORMAT_R8G8B8A8_SINT;
            case DXGI_FORMAT_R16G16_FLOAT:
                return VK_FORMAT_R16G16_SFLOAT;
            case DXGI_FORMAT_R16G16_UNORM:
                return VK_FORMAT_R16G16_UNORM;
            case DXGI_FORMAT_R16G16_UINT:
                return VK_FORMAT_R16G16_UINT;
            case DXGI_FORMAT_R16G16_SNORM:
                return VK_FORMAT_R16G16_SNORM;
            case DXGI_FORMAT_R16G16_SINT:
                return VK_FORMAT_R16G16_SINT;
            case DXGI_FORMAT_D32_FLOAT:
                return VK_FORMAT_D32_SFLOAT;
            case DXGI_FORMAT_R32_FLOAT:
                return VK_FORMAT_R32_SFLOAT;
            case DXGI_FORMAT_R32_UINT:
                return VK_FORMAT_R32_UINT;
            case DXGI_FORMAT_R32_SINT:
                return VK_FORMAT_R32_SINT;
            case DXGI_FORMAT_D24_UNORM_S8_UINT:
                return d24s8();
            case DXGI_FORMAT_R8G8_UNORM:
                return VK_FORMAT_R8G8_UNORM;
            case DXGI_FORMAT_R8G8_UINT:
                return VK_FORMAT_R8G8_UINT;
            case DXGI_FORMAT_R8G8_SNORM:
                return VK_FORMAT_R8G8_SNORM;
            case DXGI_FORMAT_R8G8_SINT:
                return VK_FORMAT_R8G8_SINT;
            case DXGI_FORMAT_R16_FLOAT:
                return VK_FORMAT_R16_SFLOAT;
            case DXGI_FORMAT_D16_UNORM:
                return VK_FORMAT_D16_UNORM;
            case DXGI_FORMAT_R16_UNORM:
                return VK_FORMAT_R16_UNORM;
            case DXGI_FORMAT_R16_UINT:
                return VK_FORMAT_R16_UINT;
            case DXGI_FORMAT_R16_SNORM:
                return VK_FORMAT_R16_SNORM;
            case DXGI_FORMAT_R16_SINT:
                return VK_FORMAT_R16_SINT;
            case DXGI_FORMAT_R8_UNORM:
                return VK_FORMAT_R8_UNORM;
            case DXGI_FORMAT_R8_UINT:
                return VK_FORMAT_R8_UINT;
            case DXGI_FORMAT_R8_SNORM:
                return VK_FORMAT_R8_SNORM;
            case DXGI_FORMAT_R8_SINT:
                return VK_FORMAT_R8_SINT;
            // A8_UNORM: an R8 image; views swizzle red into alpha (see view creation)
            case DXGI_FORMAT_A8_UNORM:
                return VK_FORMAT_R8_UNORM;
            case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
                return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
            case DXGI_FORMAT_R8G8_B8G8_UNORM:
                return VK_FORMAT_B8G8R8G8_422_UNORM;
            case DXGI_FORMAT_G8R8_G8B8_UNORM:
                return VK_FORMAT_G8B8G8R8_422_UNORM;
            case DXGI_FORMAT_BC1_UNORM:
                return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
            case DXGI_FORMAT_BC1_UNORM_SRGB:
                return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
            case DXGI_FORMAT_BC2_UNORM:
                return VK_FORMAT_BC2_UNORM_BLOCK;
            case DXGI_FORMAT_BC2_UNORM_SRGB:
                return VK_FORMAT_BC2_SRGB_BLOCK;
            case DXGI_FORMAT_BC3_UNORM:
                return VK_FORMAT_BC3_UNORM_BLOCK;
            case DXGI_FORMAT_BC3_UNORM_SRGB:
                return VK_FORMAT_BC3_SRGB_BLOCK;
            case DXGI_FORMAT_BC4_UNORM:
                return VK_FORMAT_BC4_UNORM_BLOCK;
            case DXGI_FORMAT_BC4_SNORM:
                return VK_FORMAT_BC4_SNORM_BLOCK;
            case DXGI_FORMAT_BC5_UNORM:
                return VK_FORMAT_BC5_UNORM_BLOCK;
            case DXGI_FORMAT_BC5_SNORM:
                return VK_FORMAT_BC5_SNORM_BLOCK;
            case DXGI_FORMAT_B5G6R5_UNORM:
                return VK_FORMAT_R5G6B5_UNORM_PACK16;
            case DXGI_FORMAT_B5G5R5A1_UNORM:
                return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
            case DXGI_FORMAT_B8G8R8A8_UNORM:
                return VK_FORMAT_B8G8R8A8_UNORM;
            case DXGI_FORMAT_B8G8R8X8_UNORM:
                return VK_FORMAT_B8G8R8A8_UNORM; // alpha forced to 1 in views
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                return VK_FORMAT_B8G8R8A8_SRGB;
            case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
                return VK_FORMAT_B8G8R8A8_SRGB;
            case DXGI_FORMAT_BC6H_UF16:
                return VK_FORMAT_BC6H_UFLOAT_BLOCK;
            case DXGI_FORMAT_BC6H_SF16:
                return VK_FORMAT_BC6H_SFLOAT_BLOCK;
            case DXGI_FORMAT_BC7_UNORM:
                return VK_FORMAT_BC7_UNORM_BLOCK;
            case DXGI_FORMAT_BC7_UNORM_SRGB:
                return VK_FORMAT_BC7_SRGB_BLOCK;
            case DXGI_FORMAT_B4G4R4A4_UNORM:
                return VK_FORMAT_A4R4G4B4_UNORM_PACK16;
            default:
                return VK_FORMAT_UNDEFINED;
            }
        }
    } // namespace

    void set_depth24_supported(bool supported)
    {
        g_d24 = supported;
    }

    VkFormat resource_format(uint32_t f, bool depth_usage, bool *typeless)
    {
        if (typeless)
            *typeless = true;
        switch (DXGI_FORMAT(f))
        {
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case DXGI_FORMAT_R32G32B32_TYPELESS:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case DXGI_FORMAT_R32G32_TYPELESS:
            return VK_FORMAT_R32G32_SFLOAT;
        case DXGI_FORMAT_R32G8X24_TYPELESS:
            return VK_FORMAT_D32_SFLOAT_S8_UINT;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_R16G16_TYPELESS:
            return VK_FORMAT_R16G16_SFLOAT;
        case DXGI_FORMAT_R32_TYPELESS:
            return depth_usage ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_R32_SFLOAT;
        case DXGI_FORMAT_R24G8_TYPELESS:
            return d24s8();
        case DXGI_FORMAT_R8G8_TYPELESS:
            return VK_FORMAT_R8G8_UNORM;
        case DXGI_FORMAT_R16_TYPELESS:
            return depth_usage ? VK_FORMAT_D16_UNORM : VK_FORMAT_R16_UNORM;
        case DXGI_FORMAT_R8_TYPELESS:
            return VK_FORMAT_R8_UNORM;
        case DXGI_FORMAT_BC1_TYPELESS:
            return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case DXGI_FORMAT_BC2_TYPELESS:
            return VK_FORMAT_BC2_UNORM_BLOCK;
        case DXGI_FORMAT_BC3_TYPELESS:
            return VK_FORMAT_BC3_UNORM_BLOCK;
        case DXGI_FORMAT_BC4_TYPELESS:
            return VK_FORMAT_BC4_UNORM_BLOCK;
        case DXGI_FORMAT_BC5_TYPELESS:
            return VK_FORMAT_BC5_UNORM_BLOCK;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_BC6H_TYPELESS:
            return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        case DXGI_FORMAT_BC7_TYPELESS:
            return VK_FORMAT_BC7_UNORM_BLOCK;
        default:
            if (typeless)
                *typeless = false;
            return concrete(f);
        }
    }

    VkFormat view_format(uint32_t f, VkFormat image_format)
    {
        switch (DXGI_FORMAT(f))
        {
        case DXGI_FORMAT_UNKNOWN:
            return image_format;
        // depth images read as colour: the view keeps the depth format, the aspect selects the plane
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
            return d24s8();
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
            return VK_FORMAT_D32_SFLOAT_S8_UINT;
        case DXGI_FORMAT_R32_FLOAT:
            return is_depth(image_format) ? image_format : VK_FORMAT_R32_SFLOAT;
        case DXGI_FORMAT_R16_UNORM:
            return is_depth(image_format) ? image_format : VK_FORMAT_R16_UNORM;
        default:
            return resource_format(f, is_depth(image_format));
        }
    }

    bool is_depth(VkFormat f)
    {
        switch (f)
        {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D16_UNORM_S8_UINT:
            return true;
        default:
            return false;
        }
    }

    VkImageAspectFlags aspect_of(VkFormat f)
    {
        switch (f)
        {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
        case VK_FORMAT_D16_UNORM_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }
} // namespace xrvk
