// Win32/MSVC CRT pieces used by Monolith but not covered by the OpenXRay shim.
#pragma once
#include <cmath>

#ifndef _MAX_PATH
#define _MAX_PATH MAX_PATH
#endif

#include "fp_compat.h"

#include <cerrno>
#include <cctype>
#include <cstring>
#include <type_traits>

// typedef uint8_t BYTE;  -- provided by windows_base.h
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
    out->QuadPart = (LONGLONG)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return TRUE;
}
inline BOOL QueryPerformanceFrequency(PLARGE_INTEGER out)
{
    out->QuadPart = 1000000000LL;
    return TRUE;
}

// All C stdio opens go through the case-insensitive resolver (std headers are already included).
#define fopen xr_linux_fopen
#define _unlink xr_linux_unlink

// Opaque Win32 handle and fixed-width integer typedefs used in engine headers.
// typedef void* HMONITOR;  -- provided by windows_base.h
// typedef void* HINSTANCE;  -- provided by windows_base.h
typedef void* HICON;
typedef void* HCURSOR;
// typedef uint32_t UINT32;  -- provided by windows_base.h
typedef int32_t INT32;
typedef uint64_t DWORD64;
// typedef uint64_t ULONG_PTR;  -- provided by windows_base.h
// typedef unsigned long ULONG;  -- provided by windows_base.h
// typedef unsigned short USHORT;  -- provided by windows_base.h
typedef unsigned char UCHAR;
#ifndef VOID
#define VOID void
#endif
typedef BYTE* LPBYTE;
typedef BYTE* PBYTE;
typedef WORD* LPWORD;
// typedef DWORD* LPDWORD;  -- provided by windows_base.h
#define sscanf_s sscanf // engine uses it with numeric conversions only (no %s/%c size arguments)
#include <unistd.h>
inline DWORD GetCurrentThreadId() { return (DWORD)gettid(); }
#include "win32_window.h"

#ifndef GUID_DEFINED
// #define GUID_DEFINED  -- defined by windows_base.h
typedef struct _GUID // Windows layout
{
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} GUID;
#endif
#ifndef E_NOTIMPL
// #define E_NOTIMPL ((HRESULT)0x80004001L)  -- defined by windows_base.h
#endif

// typedef int INT;  -- provided by windows_base.h
#include "include/mmsystem.h"
// ms since an arbitrary start, wraps after ~49 days like Win32
inline DWORD GetTickCount() { return timeGetTime(); }

// COM's IUnknown as dxvk declares it (D3D objects derive from it).
#include <unknwn.h>

#define SEM_FAILCRITICALERRORS 0x0001
inline UINT SetErrorMode(UINT) { return 0; } // no Windows error dialogs to suppress

// D3DXGetFVFVertexSize without D3DX9: size in bytes of a fixed-function vertex format
// (bit layout per the D3D9 FVF documentation / d3d9types.h).
inline UINT xr_fvf_vertex_size(DWORD fvf)
{
    UINT size = 0;
    switch (fvf & 0x400E) // D3DFVF_POSITION_MASK
    {
    case 0x002: size += 12; break;  // XYZ
    case 0x004: size += 16; break;  // XYZRHW
    case 0x006: size += 16; break;  // XYZB1
    case 0x008: size += 20; break;  // XYZB2
    case 0x00A: size += 24; break;  // XYZB3
    case 0x00C: size += 28; break;  // XYZB4
    case 0x00E: size += 32; break;  // XYZB5
    case 0x4002: size += 16; break; // XYZW
    default: break;
    }
    if (fvf & 0x010) size += 12; // NORMAL
    if (fvf & 0x020) size += 4;  // PSIZE
    if (fvf & 0x040) size += 4;  // DIFFUSE
    if (fvf & 0x080) size += 4;  // SPECULAR
    const UINT tex = (fvf & 0xF00) >> 8; // TEXCOUNT
    for (UINT i = 0; i < tex; ++i)
    {
        switch ((fvf >> (16 + i * 2)) & 3) // TEXCOORDSIZEn: 0 -> 2, 1 -> 3, 2 -> 4, 3 -> 1 floats
        {
        case 0: size += 8; break;
        case 1: size += 12; break;
        case 2: size += 16; break;
        case 3: size += 4; break;
        }
    }
    return size;
}

typedef DWORD* PDWORD;
typedef GUID CLSID;
typedef void* HFONT;
typedef void* HBRUSH;
typedef void* HBITMAP;
typedef void* HGDIOBJ;
typedef void* HKL;

// COM activation: no COM classes exist on Linux (DirectPlay is only created for multiplayer, which the
// port does not support), so every request fails the way Windows does for an unregistered class.
#define CLSCTX_INPROC_SERVER 0x1
#define REGDB_E_CLASSNOTREG ((HRESULT)0x80040154L)
inline HRESULT CoCreateInstance(const GUID&, void*, DWORD, const GUID&, void** out)
{
    if (out)
        *out = nullptr;
    return REGDB_E_CLASSNOTREG;
}

// Structured exception handling does not exist on Linux; SEH blocks are #ifdef'ed at their sites
// (a global __try macro would collide with libstdc++'s own __try). Faults reach xrDebug's signal
// handlers instead.
#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION (-1)
#define EXCEPTION_STACK_OVERFLOW ((DWORD)0xC00000FDL)
inline int _resetstkoflw() { return 1; }

#define _localtime64 localtime
#define _time64 time
typedef time_t __time64_t;

// SYSTEMTIME / GetLocalTime (used for screenshot and log file names).
typedef struct _SYSTEMTIME
{
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *LPSYSTEMTIME;
inline void GetLocalTime(LPSYSTEMTIME st)
{
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    tm t;
    localtime_r(&ts.tv_sec, &t);
    st->wYear = (WORD)(t.tm_year + 1900);
    st->wMonth = (WORD)(t.tm_mon + 1);
    st->wDayOfWeek = (WORD)t.tm_wday;
    st->wDay = (WORD)t.tm_mday;
    st->wHour = (WORD)t.tm_hour;
    st->wMinute = (WORD)t.tm_min;
    st->wSecond = (WORD)t.tm_sec;
    st->wMilliseconds = (WORD)(ts.tv_nsec / 1000000);
}
inline char* ultoa(unsigned long v, char* buf, int radix) { return xr_linux_itoa((long long)(unsigned long long)v, buf, 33, radix); }
#define _ultoa ultoa

typedef unsigned char boolean; // rpcndr.h
typedef int32_t __time32_t;    // MSVC 32-bit time_t

// Windows registry: there is none on Linux. Every key is "not found", so callers use their defaults.
typedef void* HKEY;
typedef HKEY* PHKEY;
#define HKEY_CLASSES_ROOT ((HKEY)(uintptr_t)0x80000000)
#define HKEY_CURRENT_USER ((HKEY)(uintptr_t)0x80000001)
#define HKEY_LOCAL_MACHINE ((HKEY)(uintptr_t)0x80000002)
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#define KEY_ALL_ACCESS 0xF003F
#define REG_SZ 1
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_OPTION_NON_VOLATILE 0
#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0L
#endif
#define ERROR_FILE_NOT_FOUND 2L
inline LONG RegOpenKeyEx(HKEY, const char*, DWORD, DWORD, PHKEY out) { if (out) *out = nullptr; return ERROR_FILE_NOT_FOUND; }
inline LONG RegCreateKeyEx(HKEY, const char*, DWORD, char*, DWORD, DWORD, void*, PHKEY out, DWORD*) { if (out) *out = nullptr; return ERROR_FILE_NOT_FOUND; }
inline LONG RegQueryValueEx(HKEY, const char*, DWORD*, DWORD*, BYTE*, DWORD*) { return ERROR_FILE_NOT_FOUND; }
inline LONG RegSetValueEx(HKEY, const char*, DWORD, DWORD, const BYTE*, DWORD) { return ERROR_FILE_NOT_FOUND; }
inline LONG RegCloseKey(HKEY) { return ERROR_SUCCESS; }
#define RegOpenKeyExA RegOpenKeyEx
#define RegCreateKeyExA RegCreateKeyEx
#define RegQueryValueExA RegQueryValueEx
#define RegSetValueExA RegSetValueEx

// GetFullPathName via realpath semantics (path need not exist: then it is made absolute textually).
inline DWORD GetFullPathName(const char* path, DWORD size, char* out, char** file_part)
{
    std::string p = xr_resolve_path(path);
    if (p.empty() || p[0] != '/')
    {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)))
            p = std::string(cwd) + "/" + p;
    }
    if (!out || size <= p.size())
        return (DWORD)p.size() + 1;
    memcpy(out, p.c_str(), p.size() + 1);
    if (file_part)
    {
        char* slash = strrchr(out, '/');
        *file_part = slash ? slash + 1 : out;
    }
    return (DWORD)p.size();
}
#define GetFullPathNameA GetFullPathName

typedef uintptr_t DWORD_PTR;
inline HANDLE GetCurrentProcess() { return (HANDLE)(intptr_t)-1; } // pseudo-handle, as on Windows
// Affinity of this process (first 64 CPUs) for both masks.
inline BOOL GetProcessAffinityMask(HANDLE, DWORD_PTR* process_mask, DWORD_PTR* system_mask)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) != 0)
        return FALSE;
    DWORD_PTR m = 0;
    for (int i = 0; i < 64 && i < CPU_SETSIZE; ++i)
        if (CPU_ISSET(i, &set))
            m |= DWORD_PTR(1) << i;
    if (process_mask) *process_mask = m;
    if (system_mask) *system_mask = m;
    return TRUE;
}
#define FILE_ATTRIBUTE_HIDDEN 0x2u
inline __time32_t _time32(__time32_t* t) { const __time32_t v = (__time32_t)time(nullptr); if (t) *t = v; return v; }
inline errno_t _strupr_s(char* s, size_t) { strupr(s); return 0; }

// Address of a temporary for the rest of the full-expression (MSVC allows &T(args)).
template <class T> inline T* xr_tmp_addr(T&& t) { return &t; }
typedef struct _OVERLAPPED* LPOVERLAPPED;
template <size_t N> inline errno_t _strupr_s(char (&s)[N]) { strupr(s); return 0; }
typedef struct _OVERLAPPED { uintptr_t Internal, InternalHigh; DWORD Offset, OffsetHigh; HANDLE hEvent; } OVERLAPPED;
// TCHAR is char in this build (no _UNICODE)
#ifndef _tfopen
#define _tfopen fopen
#endif
