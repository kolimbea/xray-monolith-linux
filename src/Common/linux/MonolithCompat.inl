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
    struct _stat sb;
    if (_stat(path, &sb) != 0)
        return INVALID_FILE_ATTRIBUTES;
    DWORD a = S_ISDIR(sb.st_mode) ? 0x10u : FILE_ATTRIBUTE_NORMAL;
    if (!(sb.st_mode & S_IWUSR))
        a |= FILE_ATTRIBUTE_READONLY;
    return a;
}
inline BOOL SetFileAttributes(const char* path, DWORD attr)
{
    struct _stat sb;
    if (_stat(path, &sb) != 0)
        return FALSE;
    const mode_t m = (attr & FILE_ATTRIBUTE_READONLY) ? (sb.st_mode & ~(mode_t)0222) : (sb.st_mode | S_IWUSR);
    return chmod(xr_resolve_path(path).c_str(), m) == 0 ? TRUE : FALSE;
}
#define GetFileAttributesA GetFileAttributes
#define SetFileAttributesA SetFileAttributes

// MessageBox over SDL3 (works without a window). Always logs to stderr too, since fatal paths call
// it right before abort() and the dialog may not be visible.
#include <SDL3/SDL_messagebox.h>
#define MB_OK 0x0u
#define MB_OKCANCEL 0x1u
#define MB_YESNO 0x4u
#define MB_ICONERROR 0x10u
#define MB_ICONHAND MB_ICONERROR
#define MB_ICONSTOP MB_ICONERROR
#define MB_ICONQUESTION 0x20u
#define MB_ICONWARNING 0x30u
#define MB_ICONEXCLAMATION MB_ICONWARNING
#define MB_ICONINFORMATION 0x40u
#define MB_TASKMODAL 0x2000u
#define MB_SYSTEMMODAL 0x1000u
#define MB_TOPMOST 0x40000u
#define IDOK 1
#define IDCANCEL 2
#define IDYES 6
#define IDNO 7
inline int MessageBox(void*, const char* text, const char* caption, unsigned type)
{
    fprintf(stderr, "[%s] %s\n", caption ? caption : "", text ? text : "");
    if (getenv("XR_NO_MSGBOX")) // headless runs and tests: report on stderr only
        return (type & 0xFu) == MB_YESNO ? IDNO : IDOK;
    const unsigned icon = type & 0xF0u;
    const SDL_MessageBoxFlags flags = icon == MB_ICONERROR ? SDL_MESSAGEBOX_ERROR
        : icon == MB_ICONWARNING ? SDL_MESSAGEBOX_WARNING : SDL_MESSAGEBOX_INFORMATION;
    const unsigned kind = type & 0xFu;
    if (kind != MB_OKCANCEL && kind != MB_YESNO)
    {
        SDL_ShowSimpleMessageBox(flags, caption, text, nullptr);
        return IDOK;
    }
    const bool yesno = kind == MB_YESNO;
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, yesno ? IDNO : IDCANCEL, yesno ? "No" : "Cancel"},
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, yesno ? IDYES : IDOK, yesno ? "Yes" : "OK"},
    };
    const SDL_MessageBoxData data = {flags, nullptr, caption, text, 2, buttons, nullptr};
    int pressed = yesno ? IDNO : IDCANCEL;
    if (!SDL_ShowMessageBox(&data, &pressed))
        return yesno ? IDNO : IDCANCEL;
    return pressed;
}
#define MessageBoxA MessageBox

// MSVC _controlfp. On x64 MSVC only the SSE rounding mode takes effect (precision control is
// x87-only and ignored there), so this maps the rounding bits onto fesetround and ignores the rest.
#include <cfenv>
#define _MCW_RC 0x00000300u
#define _RC_NEAR 0x00000000u
#define _RC_DOWN 0x00000100u
#define _RC_UP 0x00000200u
#define _RC_CHOP 0x00000300u
#define _MCW_PC 0x00030000u
#define _PC_64 0x00000000u
#define _PC_53 0x00010000u
#define _PC_24 0x00020000u
#define _MCW_EM 0x0008001Fu
#define MCW_RC _MCW_RC
#define MCW_PC _MCW_PC
#define MCW_EM _MCW_EM
#define RC_NEAR _RC_NEAR
#define RC_CHOP _RC_CHOP
#define PC_24 _PC_24
#define PC_53 _PC_53
#define PC_64 _PC_64
inline unsigned _controlfp(unsigned value, unsigned mask)
{
    if (mask & _MCW_RC)
    {
        const unsigned rc = value & _MCW_RC;
        fesetround(rc == _RC_DOWN ? FE_DOWNWARD : rc == _RC_UP ? FE_UPWARD : rc == _RC_CHOP ? FE_TOWARDZERO : FE_TONEAREST);
    }
    const int cur = fegetround();
    return cur == FE_DOWNWARD ? _RC_DOWN : cur == FE_UPWARD ? _RC_UP : cur == FE_TOWARDZERO ? _RC_CHOP : _RC_NEAR;
}
#include "win32_system.h"

// Bounded MSVC *printf_s. The CRT would invoke the invalid-parameter handler on truncation; here the
// output is truncated and terminated instead, which keeps the engine's log paths alive.
#include <cstdarg>
inline int vsprintf_s(char* dst, size_t size, const char* fmt, va_list args)
{
    if (!dst || size == 0)
        return -1;
    const int n = vsnprintf(dst, size, fmt, args);
    return n < 0 || (size_t)n >= size ? -1 : n;
}
template <size_t N> inline int vsprintf_s(char (&dst)[N], const char* fmt, va_list args) { return vsprintf_s(dst, N, fmt, args); }
inline int sprintf_s(char* dst, size_t size, const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    const int n = vsprintf_s(dst, size, fmt, a);
    va_end(a);
    return n;
}
template <size_t N> inline int sprintf_s(char (&dst)[N], const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    const int n = vsprintf_s(dst, N, fmt, a);
    va_end(a);
    return n;
}

#define filelength _filelength
// Cannot be probed cheaply on Linux; callers only check buffers they computed themselves.
inline BOOL IsBadReadPtr(const void* p, size_t) { return p == nullptr; }

// QueryPerformanceCounter in nanoseconds (frequency 1e9) from the monotonic clock.
typedef LARGE_INTEGER* PLARGE_INTEGER;
inline BOOL QueryPerformanceCounter(PLARGE_INTEGER out)
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    *out = (LARGE_INTEGER)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return TRUE;
}
inline BOOL QueryPerformanceFrequency(PLARGE_INTEGER out)
{
    *out = 1000000000LL;
    return TRUE;
}

// All C stdio opens go through the case-insensitive resolver (std headers are already included).
#define fopen xr_linux_fopen
#define _unlink xr_linux_unlink

// Opaque Win32 handle and fixed-width integer typedefs used in engine headers.
typedef void* HMONITOR;
typedef void* HINSTANCE;
typedef void* HICON;
typedef void* HCURSOR;
typedef uint32_t UINT32;
typedef int32_t INT32;
typedef uint64_t DWORD64;
typedef uint64_t ULONG_PTR;
typedef unsigned long ULONG;
typedef unsigned short USHORT;
typedef unsigned char UCHAR;
#ifndef VOID
#define VOID void
#endif
typedef BYTE* LPBYTE;
typedef BYTE* PBYTE;
typedef WORD* LPWORD;
typedef DWORD* LPDWORD;
#define sscanf_s sscanf // engine uses it with numeric conversions only (no %s/%c size arguments)
