// Vulkan build stand-in: D3DX10 is only used in the USE_DX10 branches, which this build does not take.
#pragma once
#include "../vkCommonTypes.h"
#ifndef D3DX10_FILTER_POINT
#define D3DX10_FILTER_POINT (2 << 0) // = D3DX11_FILTER_POINT (D3DX11tex.h), passed to D3DX11FilterTexture
#endif
