#pragma once

#include "../../build_config_defines.h"

#include "lua.hpp"

#include <cstdlib>
#include <cctype>

#ifdef __linux__
#include <cstdint>
typedef uint32_t DWORD; // 32-bit as on Windows (unsigned long is 64-bit here)
#else
typedef unsigned long DWORD;
#endif
typedef unsigned char BYTE;

void open_additional_libs(lua_State*);