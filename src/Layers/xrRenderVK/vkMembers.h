// Backend members of the D3D11-named classes (XRVK_EXTRA_<Interface>), injected by the generated
// vk_d3d_interfaces.gen.h. Filled in as the Vulkan backend implements each object (xrRenderVK/*.cpp).
#pragma once

// Data the base interfaces need for their own methods (GetDevice, GetType, GetResource).
#define XRVK_EXTRA_ID3D11DeviceChild ID3D11Device* xrvk_device = nullptr;
#define XRVK_EXTRA_ID3D11Resource D3D11_RESOURCE_DIMENSION xrvk_dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
#define XRVK_EXTRA_ID3D11View ID3D11Resource* xrvk_resource = nullptr; // holds a reference
