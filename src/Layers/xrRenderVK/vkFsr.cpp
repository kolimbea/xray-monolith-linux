// AMD's CPU-side setup of FSR 1 (MIT, build/deps/fsr1-a21ffb8, license.txt there).
#include "vkFsr.h"

// ffx_a.h's CPU path calls the C math/stdlib functions without including their headers
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define A_CPU 1
#include "ffx_a.h"
#include "ffx_fsr1.h"

namespace xrvk::fsr
{
    void easu_constants(uint32_t out[16], float in_w, float in_h, float out_w, float out_h)
    {
        FsrEasuCon(out, out + 4, out + 8, out + 12, in_w, in_h, in_w, in_h, out_w, out_h);
    }

    void rcas_constants(uint32_t out[4], float sharpness)
    {
        FsrRcasCon(out, sharpness);
    }
} // namespace xrvk::fsr
