// Source-level compatibility rules applied to preprocessed HLSL before DXC compiles it to SPIR-V.
// Each rule turns something FXC accepted into an equivalent form DXC's SPIR-V backend accepts; results
// are unchanged. Ported from tools/hlsl_rewrite.py (the prototype that was checked on the GAMMA tree).
//
// Rule 1 (SPIR-V needs immediate texel offsets): `T.Load(loc, offset)` on a Texture2D/Texture2DArray with
//   a non-literal offset becomes `T.Load((loc) + int3/int4((offset), 0[, 0]))`. Load's offset is added to
//   the integer texel coordinate, so this is exact.
// Rule 2 (DXC accepts only integer literals in loop attributes): `[unroll(expr)]` / `[loop(expr)]` with a
//   non-literal count becomes `[unroll]` / `[loop]`. The count is only an unroll hint.
#pragma once
#include <string>

namespace xrvk::hlsl
{
    // Applies all rules. Lines starting with '#' are left alone (macro bodies, #line markers).
    std::string rewrite_compat(const std::string &text);
} // namespace xrvk::hlsl
