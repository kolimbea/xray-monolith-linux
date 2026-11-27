// Source-level compatibility rules applied to preprocessed HLSL before DXC compiles it to SPIR-V.
// Each rule turns something FXC accepted into an equivalent form DXC's SPIR-V backend accepts; results
// are unchanged. Ported from tools/hlsl_rewrite.py (the prototype that was checked on the GAMMA tree).
//
// Rule 1 (SPIR-V needs immediate texel offsets): `T.Load(loc, offset)` on a Texture2D/Texture2DArray with
//   a non-literal offset becomes `T.Load((loc) + int3/int4((int2)(offset), 0[, 0]))` (the cast also splats a scalar). Load's offset is added to
//   the integer texel coordinate, so this is exact.
// Rule 2 (DXC accepts only integer literals in loop attributes): `[unroll(expr)]` / `[loop(expr)]` with a
//   non-literal count becomes `[unroll]` / `[loop]`. The count is only an unroll hint.
// Rule 4 (DX9-era lowercase object types): `texture2D` / `texture3D` / `textureCube` become `Texture2D` /
//   `Texture3D` / `TextureCube`, which FXC treated them as.
// Rule 5 (no pow2 tessellation in Vulkan): `[partitioning("pow2")]` becomes `[partitioning("integer")]`
//   (same integer levels, not rounded up to a power of two).
#pragma once
#include <string>

namespace xrvk::hlsl
{
    // Applies all rules. Lines starting with '#' are left alone (macro bodies, #line markers).
    std::string rewrite_compat(const std::string &text);

    // Rule 3 (DXC rejects an input semantic used twice; FXC lets both read the same register): given
    // DXC's diagnostics for preprocessed `text` (with `#line N "file"` markers), removes a function
    // parameter reported as "input semantic 'X' used more than once" if the function body never reads
    // it. Returns true if the text changed (then compile again).
    bool drop_unused_duplicate_input(std::string &text, const std::string &diagnostics);
} // namespace xrvk::hlsl
