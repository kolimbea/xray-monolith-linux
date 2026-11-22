// Linux stand-in for <d3d9types.h>: the engine (outside the renderer) only uses the colour macros.
#pragma once
#include <cstdint>
typedef uint32_t D3DCOLOR;
#define D3DCOLOR_ARGB(a, r, g, b) \
    ((D3DCOLOR)((((a) & 0xff) << 24) | (((r) & 0xff) << 16) | (((g) & 0xff) << 8) | ((b) & 0xff)))
#define D3DCOLOR_RGBA(r, g, b, a) D3DCOLOR_ARGB(a, r, g, b)
#define D3DCOLOR_XRGB(r, g, b) D3DCOLOR_ARGB(0xff, r, g, b)
#define D3DCOLOR_COLORVALUE(r, g, b, a) \
    D3DCOLOR_RGBA((uint32_t)((r)*255.f), (uint32_t)((g)*255.f), (uint32_t)((b)*255.f), (uint32_t)((a)*255.f))
