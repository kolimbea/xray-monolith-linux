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
#include "win32_text.h"

// MSVC target macro the engine uses for "64-bit x86" code paths (selects C code over 32-bit asm).
#if defined(__x86_64__) && !defined(_M_AMD64)
#define _M_AMD64 100
#define _M_X64 100
#endif

// MSVC itoa family. Radix 2..36, negative values only for radix 10 (as the CRT does).
inline char* xr_linux_itoa(long long v, char* buf, size_t size, int radix)
{
    if (!buf || size == 0 || radix < 2 || radix > 36)
        return buf;
    char tmp[72];
    int n = 0;
    const bool neg = v < 0 && radix == 10;
    unsigned long long u = neg ? 0ull - (unsigned long long)v : (unsigned long long)(radix == 10 ? v : (unsigned)v);
    do
    {
        tmp[n++] = "0123456789abcdefghijklmnopqrstuvwxyz"[u % radix];
        u /= radix;
    } while (u);
    if (neg)
        tmp[n++] = '-';
    size_t i = 0;
    while (n && i + 1 < size)
        buf[i++] = tmp[--n];
    buf[i] = 0;
    return buf;
}
inline char* itoa(int v, char* buf, int radix) { return xr_linux_itoa(v, buf, 33, radix); }
inline char* _itoa(int v, char* buf, int radix) { return xr_linux_itoa(v, buf, 33, radix); }
inline errno_t _itoa_s(int v, char* buf, size_t size, int radix) { xr_linux_itoa(v, buf, size, radix); return 0; }
template <size_t N> inline errno_t _itoa_s(int v, char (&buf)[N], int radix) { xr_linux_itoa(v, buf, N, radix); return 0; }

// MSVC heap extensions over glibc. malloc_usable_size works for malloc and posix_memalign blocks.
#include <malloc.h>
#include <cstdlib>
inline size_t _msize(void* p) { return p ? malloc_usable_size(p) : 0; }
// _expand may legally fail; callers then fall back to malloc + copy.
inline void* _expand(void*, size_t) { return nullptr; }
inline void* _aligned_malloc(size_t size, size_t align)
{
    void* p = nullptr;
    if (align < sizeof(void*))
        align = sizeof(void*);
    return posix_memalign(&p, align, size ? size : 1) == 0 ? p : nullptr;
}
inline void _aligned_free(void* p) { free(p); }
inline size_t _aligned_msize(void* p, size_t, size_t) { return p ? malloc_usable_size(p) : 0; }
inline void* _aligned_realloc(void* p, size_t size, size_t align)
{
    if (!p)
        return _aligned_malloc(size, align);
    if (!size)
    {
        free(p);
        return nullptr;
    }
    void* n = _aligned_malloc(size, align);
    if (!n)
        return nullptr; // like the CRT: the old block stays valid
    const size_t old = malloc_usable_size(p);
    memcpy(n, p, old < size ? old : size);
    free(p);
    return n;
}
#include "win32_file.h"

// More MSVC CRT helpers.
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>

inline char* _strlwr(char* s) { return strlwr(s); }
inline errno_t _strlwr_s(char* s, size_t) { strlwr(s); return 0; }
inline long long _atoi64(const char* s) { return strtoll(s, nullptr, 10); }
inline unsigned long long _strtoui64(const char* s, char** end, int base) { return strtoull(s, end, base); }
inline errno_t _i64toa_s(long long v, char* buf, size_t size, int radix)
{
    if (radix != 10)
        return EINVAL; // engine only formats decimal; other radices are not needed
    return snprintf(buf, size, "%lld", v) < (int)size ? 0 : ERANGE;
}
inline errno_t _ui64toa_s(unsigned long long v, char* buf, size_t size, int radix)
{
    if (radix != 10)
        return EINVAL;
    return snprintf(buf, size, "%llu", v) < (int)size ? 0 : ERANGE;
}

// _splitpath_s: splits "dir/name.ext" (either slash). There are no drive letters on Linux, so
// drive is always empty. Returns EINVAL on a too-small buffer, like the CRT.
inline errno_t _splitpath_s(const char* path, char* drive, size_t drive_n, char* dir, size_t dir_n, char* fname,
                            size_t fname_n, char* ext, size_t ext_n)
{
    const char* slash = nullptr;
    for (const char* p = path; *p; ++p)
        if (*p == '/' || *p == '\\')
            slash = p;
    const char* name = slash ? slash + 1 : path;
    const char* dot = strrchr(name, '.');
    if (!dot)
        dot = name + strlen(name);
    auto put = [](char* dst, size_t n, const char* b, const char* e) -> bool {
        if (!dst)
            return true;
        const size_t len = (size_t)(e - b);
        if (len + 1 > n)
            return false;
        memcpy(dst, b, len);
        dst[len] = 0;
        return true;
    };
    const bool ok = put(drive, drive_n, path, path) && put(dir, dir_n, path, name) && put(fname, fname_n, name, dot) &&
                    put(ext, ext_n, dot, dot + strlen(dot));
    return ok ? 0 : EINVAL;
}

// _sys_errlist[errno] is used only to print messages.
struct xr_linux_errlist
{
    const char* operator[](int e) const { return strerror(e); }
};
inline constexpr xr_linux_errlist _sys_errlist{};

inline LONG InterlockedExchange(volatile LONG* target, LONG value)
{
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

// File attributes: only the read-only bit is meaningful (owner write permission).
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
inline DWORD GetFileAttributes(const char* path)
{
    struct stat sb;
    if (stat(path, &sb) != 0)
        return INVALID_FILE_ATTRIBUTES;
    DWORD a = S_ISDIR(sb.st_mode) ? 0x10u : FILE_ATTRIBUTE_NORMAL;
    if (!(sb.st_mode & S_IWUSR))
        a |= FILE_ATTRIBUTE_READONLY;
    return a;
}
inline BOOL SetFileAttributes(const char* path, DWORD attr)
{
    struct stat sb;
    if (stat(path, &sb) != 0)
        return FALSE;
    const mode_t m = (attr & FILE_ATTRIBUTE_READONLY) ? (sb.st_mode & ~(mode_t)0222) : (sb.st_mode | S_IWUSR);
    return chmod(path, m) == 0 ? TRUE : FALSE;
}
#define GetFileAttributesA GetFileAttributes
#define SetFileAttributesA SetFileAttributes
