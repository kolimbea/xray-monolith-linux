// Shared helpers of the Vulkan backend (xrRender_VK, ADR 0001).
#pragma once

namespace xrvk
{
// Stops the game: the renderer called a D3D11-named method the Vulkan backend does not implement yet.
[[noreturn]] void not_implemented(const char* what);
} // namespace xrvk
