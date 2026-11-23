// Force-included into the bundled ODE sources on Linux (they do not see the engine compat layer).
#pragma once
#include <stdint.h>
#ifdef __cplusplus
#include "fp_compat.h"
#endif
#define _cdecl
#define __cdecl
#define _copysign copysign
