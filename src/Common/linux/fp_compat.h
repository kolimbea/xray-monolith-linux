// MSVC _fpclass() for the engine and bundled ODE.
#pragma once
#include <cmath>

// MSVC _fpclass() bit flags (float.h), used by _valid() checks.
#define _FPCLASS_SNAN 0x0001
#define _FPCLASS_QNAN 0x0002
#define _FPCLASS_NINF 0x0004
#define _FPCLASS_NN   0x0008
#define _FPCLASS_ND   0x0010
#define _FPCLASS_NZ   0x0020
#define _FPCLASS_PZ   0x0040
#define _FPCLASS_PD   0x0080
#define _FPCLASS_PN   0x0100
#define _FPCLASS_PINF 0x0200

inline int _fpclass(double x)
{
    const bool neg = std::signbit(x);
    switch (std::fpclassify(x))
    {
    case FP_NAN: return _FPCLASS_QNAN; // glibc does not distinguish signalling NaN here
    case FP_INFINITE: return neg ? _FPCLASS_NINF : _FPCLASS_PINF;
    case FP_SUBNORMAL: return neg ? _FPCLASS_ND : _FPCLASS_PD;
    case FP_ZERO: return neg ? _FPCLASS_NZ : _FPCLASS_PZ;
    default: return neg ? _FPCLASS_NN : _FPCLASS_PN;
    }
}
