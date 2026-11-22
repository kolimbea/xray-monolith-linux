// Win32/MSVC CRT pieces used by Monolith but not covered by the OpenXRay shim.
#pragma once
#include <cmath>

#ifndef _MAX_PATH
#define _MAX_PATH MAX_PATH
#endif

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

#include <cerrno>
#include <cctype>
#include <cstring>
#include <type_traits>

typedef uint8_t BYTE;
typedef int errno_t;

inline char* strlwr(char* s) { for (char* p = s; *p; ++p) *p = (char)tolower((unsigned char)*p); return s; }
inline char* strupr(char* s) { for (char* p = s; *p; ++p) *p = (char)toupper((unsigned char)*p); return s; }

// Win32 lock API over the standard library. CRITICAL_SECTION is recursive on Windows, so it maps to
// std::recursive_mutex. Storage may come from raw xr_alloc, hence placement-new in Initialize*.
#include <mutex>
#include <new>
#include <shared_mutex>

typedef std::recursive_mutex CRITICAL_SECTION;
inline void InitializeCriticalSection(CRITICAL_SECTION* cs) { new (cs) std::recursive_mutex(); }
inline void DeleteCriticalSection(CRITICAL_SECTION* cs) { cs->~recursive_mutex(); }
inline void EnterCriticalSection(CRITICAL_SECTION* cs) { cs->lock(); }
inline void LeaveCriticalSection(CRITICAL_SECTION* cs) { cs->unlock(); }
inline BOOL TryEnterCriticalSection(CRITICAL_SECTION* cs) { return cs->try_lock() ? TRUE : FALSE; }

// SRWLOCK is a member (constructed with its owner), so no placement-new is needed.
typedef std::shared_mutex SRWLOCK;
inline void InitializeSRWLock(SRWLOCK*) {}
inline void AcquireSRWLockExclusive(SRWLOCK* l) { l->lock(); }
inline void ReleaseSRWLockExclusive(SRWLOCK* l) { l->unlock(); }
inline void AcquireSRWLockShared(SRWLOCK* l) { l->lock_shared(); }
inline void ReleaseSRWLockShared(SRWLOCK* l) { l->unlock_shared(); }
inline BOOL TryAcquireSRWLockExclusive(SRWLOCK* l) { return l->try_lock() ? TRUE : FALSE; }
inline BOOL TryAcquireSRWLockShared(SRWLOCK* l) { return l->try_lock_shared() ? TRUE : FALSE; }
