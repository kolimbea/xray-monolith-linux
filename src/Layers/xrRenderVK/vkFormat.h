// DXGI_FORMAT -> VkFormat for the Vulkan backend.
#pragma once
#include <cstdint>

#include <vulkan/vulkan.h>

namespace xrvk
{
    // Format of a resource (texture/buffer). `depth_usage`: the resource has D3D11_BIND_DEPTH_STENCIL, so a
    // typeless format resolves to its depth form (R24G8_TYPELESS -> D24_UNORM_S8_UINT, R32_TYPELESS -> D32).
    // Typeless colour formats resolve to a concrete member of their family; the image is then created
    // mutable so views can reinterpret it. VK_FORMAT_UNDEFINED for formats the backend does not support.
    VkFormat resource_format(uint32_t dxgi, bool depth_usage, bool *typeless = nullptr);
    // Format of a view (SRV/RTV/DSV/UAV) on an image whose resource format is `image_format`;
    // DXGI_FORMAT_UNKNOWN means "the resource's format".
    VkFormat view_format(uint32_t dxgi, VkFormat image_format);
    // Component mapping of a shader-resource view in `dxgi` format: formats stored as a wider or other
    // Vulkan format read back like D3D11 (A8_UNORM from R8 -> (0,0,0,r); B8G8R8X8 -> alpha 1).
    VkComponentMapping view_swizzle(uint32_t dxgi);
    VkImageAspectFlags aspect_of(VkFormat f);
    bool is_depth(VkFormat f);
    // D24_UNORM_S8_UINT is optional in Vulkan; the backend falls back to D32_SFLOAT_S8_UINT when the GPU
    // lacks it (checked once at device creation).
    void set_depth24_supported(bool supported);
} // namespace xrvk
