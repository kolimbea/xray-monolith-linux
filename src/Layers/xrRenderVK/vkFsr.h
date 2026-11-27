// Constants of AMD FidelityFX Super Resolution 1.0 (EASU, RCAS), computed by AMD's own CPU code
// (ffx_fsr1.h with A_CPU) in vkFsr.cpp, which is the only translation unit that sees AMD's macros.
#pragma once
#include <cstdint>

namespace xrvk::fsr
{
    // EASU: four uint4 (16 values) for scaling in_w x in_h to out_w x out_h.
    void easu_constants(uint32_t out[16], float in_w, float in_h, float out_w, float out_h);
    // RCAS: one uint4; sharpness in stops (0 = sharpest).
    void rcas_constants(uint32_t out[4], float sharpness);
} // namespace xrvk::fsr
