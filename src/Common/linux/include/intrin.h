// Linux stand-in for MSVC <intrin.h>: __cpuid/__cpuidex via GCC/clang <cpuid.h>, plus x86 intrinsics.
#pragma once
#include <cpuid.h>
#undef __cpuid // <cpuid.h> defines a 5-argument macro of the same name
#include <x86intrin.h>
inline void __cpuid(int info[4], int leaf) { __cpuid_count(leaf, 0, info[0], info[1], info[2], info[3]); }
inline void __cpuidex(int info[4], int leaf, int sub) { __cpuid_count(leaf, sub, info[0], info[1], info[2], info[3]); }
