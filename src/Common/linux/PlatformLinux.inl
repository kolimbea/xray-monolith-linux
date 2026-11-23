// Based on OpenXRay src/Common/PlatformLinux.inl (commit d1eb5d3), same GSC licence.
// Win32 base types come from dxvk-native's windows_base.h (Windows-accurate widths; the renderer's
// D3D11/DXGI headers need exactly these). This file and MonolithCompat.inl only add on top.
#include <windows_base.h>
#include <stdio.h>
#include "io_compat.h" // xr_linux_* path-resolving file calls used below
#include "path_resolve.h"
#include <cstring>
#include <stdlib.h> // for malloc
#include <unistd.h> // for rmdir
#ifndef __HAIKU__
#include <sys/fcntl.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>
#include <limits.h> // for PAGESIZE...
#include <math.h>
#include <sched.h>

#include <algorithm> // for min max

#include <string>
#include <alloca.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h> // for mmap / munmap
#include <dirent.h>
#include <utime.h>
#include <ctime>

#define _LINUX // for GameSpy

// #define MAX_PATH PATH_MAX + 1  -- defined by windows_base.h

// #define WINAPI  -- defined by windows_base.h

#define _copysign copysign

#define _cdecl //__attribute__((cdecl))

#define __cdecl _cdecl
// #define __stdcall  -- defined by windows_base.h
#define __fastcall

#define __pragma(...) _Pragma(#__VA_ARGS__)
#define CALLBACK

#define __except(X) catch(X)

#define GetCurrentProcessId getpid

inline void Sleep(int ms)
{
    usleep(ms * 1000);
}

inline void _splitpath(const char* path, // Path Input
        char* drive, // Drive     : Output
        char* dir, // Directory : Output
        char* fname, // Filename  : Output
        char* ext // Extension : Output
        )
{
    if(!path)
        return;

    const char *p, *end;

    if(drive)
        strcpy(drive, "");

    end = NULL;
    for(p = path; *p; p++)
        if(*p == '/' || *p == '\\')
            end = p + 1;

    if(end)
    {
        if(dir)
        {
            memcpy(dir, path, end - path);
            dir[end - path] = 0;
        }
        path = end;
    }
    else if(dir)
        dir[0] = 0;

    end = strchr(path, '.');

    if(!end)
        end = p;

    if(fname)
    {
        memcpy(fname, path, end - path);
        fname[end - path] = 0;
    }

    if(ext)
        strcpy(ext, end);
}

#include <iostream>
inline void OutputDebugString(const char *str) // for linux debugger
{
    std::cerr << str;
}

inline unsigned long GetLastError()
{
    return 0;
}

inline int GetExceptionCode()
{
    return 0;
}

inline void convert_path_separators(char * path);

#include <inttypes.h>
// typedef int32_t BOOL;  -- provided by windows_base.h
// typedef uint16_t WORD;  -- provided by windows_base.h
// typedef uint32_t DWORD;  -- provided by windows_base.h
// typedef int32_t LONG;  -- provided by windows_base.h
#ifndef _LIBRAW_TYPES_H
// typedef int64_t INT64;  -- provided by windows_base.h
// typedef uint64_t UINT64;  -- provided by windows_base.h
#endif

// typedef char* LPSTR;  -- provided by windows_base.h
typedef char* PSTR;
typedef char* LPTSTR;
typedef const char* LPCSTR;
typedef const char* LPCTSTR;
// typedef unsigned int UINT;  -- provided by windows_base.h
// typedef long long int LARGE_INTEGER;  -- provided by windows_base.h
typedef unsigned long long int ULARGE_INTEGER;

typedef struct _EXCEPTION_POINTERS {
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS;

#if defined(XR_ARCHITECTURE_X64) || defined(XR_ARCHITECTURE_ARM64) || defined(XR_ARCHITECTURE_E2K)
// typedef int64_t INT_PTR;  -- provided by windows_base.h
// typedef uint64_t UINT_PTR;  -- provided by windows_base.h
// typedef int64_t LONG_PTR;  -- provided by windows_base.h
#else
// typedef int INT_PTR;  -- provided by windows_base.h
// typedef unsigned int UINT_PTR;  -- provided by windows_base.h
// typedef long LONG_PTR;  -- provided by windows_base.h
#endif

// typedef void* HANDLE;  -- provided by windows_base.h
// typedef void* HMODULE;  -- provided by windows_base.h
// typedef void* PVOID;  -- provided by windows_base.h
// typedef void* LPVOID;  -- provided by windows_base.h
#if defined(XR_ARCHITECTURE_PPC64)
typedef LONG_PTR WPARAM;
#else
typedef UINT_PTR WPARAM;
#endif
typedef LONG_PTR LPARAM;
// typedef long HRESULT;  -- provided by windows_base.h
typedef long LRESULT;
// typedef void* HWND;  -- provided by windows_base.h
// typedef void* HDC;  -- provided by windows_base.h

// Win32 LONG is 32-bit; the engine casts Ivector2/Irect (int pairs) to POINT*/RECT*, so the
// layout must match Windows (OpenXRay used 64-bit long here).
// RECT/POINT come from windows_base.h (32-bit LONG, Windows layout)

// RECT/POINT come from windows_base.h (32-bit LONG, Windows layout)

#define WM_USER 0x0400

// #define TRUE true  -- defined by windows_base.h
// #define FALSE false  -- defined by windows_base.h
#define NONE 0
// #define CONST const  -- defined by windows_base.h

typedef dirent DirEntryType;

#define _O_WRONLY O_WRONLY
#define _O_RDONLY O_RDONLY
#define _O_TRUNC O_TRUNC
#define _O_CREAT O_CREAT
#define _S_IWRITE S_IWRITE
#define _S_IREAD S_IREAD
#define _O_BINARY 0
#define O_BINARY 0
#define O_SEQUENTIAL 0
#define SH_DENYWR 0

// itoa/_itoa_s: see MonolithCompat.inl (OpenXRay mapped _itoa_s to SDL_itoa, whose arity differs).

#define _stricmp stricmp
#define strcmpi stricmp
#define lstrcpy strcpy
#define stricmp strcasecmp
// sprintf_s/vsprintf_s: bounded versions in MonolithCompat.inl (OpenXRay ignored the size).
// error code numbers from original MS strcpy_s return value
inline int strcpy_s(char *dest, size_t num, const char *source)
{
    if(!dest)
        return EINVAL;

    if(0 == num)
    {
        dest[0] = '\0';
        return ERANGE;
    }

    if(!source)
    {
        dest[0] = '\0';
        return EINVAL;
    }

    size_t i;
    for(i = 0; i < num; i++)
    {
        if((dest[i] = source[i]) == '\0')
            return 0;
    }
    dest[0] = '\0';
    return ERANGE;
}

template <size_t num>
inline int strcpy_s(char (&dest)[num], const char *source) { return strcpy_s(dest, num, source); }

#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif
inline int strncpy_s(char * dest, size_t dst_size, const char * source, size_t num)
{
    if (!dest || (0 == dst_size))
        return EINVAL;

    if(0 == num)
    {
        dest[0] = '\0';
        return 0;
    }

    if (!source)
    {
        dest[0] = '\0';
        return EINVAL;
    }

    size_t i, end;
    if(num < dst_size)
        end = num;
    else
        end = dst_size - 1;

    for(i = 0; i < end && source[i]; i++)
        dest[i] = source[i];

    if(!source[i] || end == num)
    {
        dest[i] = '\0';
        return 0;
    }

    if (num == (size_t)-1) // _TRUNCATE: keep what fits (OpenXRay cleared the buffer instead)
    {
        dest[i] = '\0';
        return 80; // STRUNCATE
    }

    dest[0] = '\0';

    return EINVAL;
}

template <size_t dst_sz>
inline int strncpy_s(char (&dest)[dst_sz], const char * source, size_t num) { return strncpy_s(dest, dst_sz, source, num); }

inline int strcat_s(char * dest, size_t num, const char * source)
{
    if(!dest)
        return EINVAL;

    if(!source)
    {
        dest[0] = '\0';
        return EINVAL;
    }

    size_t i, j;
    for(i = 0; i < num; i++)
    {
        if(dest[i] == '\0')
        {
            for(j = 0; (j + i) < num; j++)
            {
                if((dest[j + i] = source[j]) == '\0')
                    return 0;
            }
        }
    }

    dest[0] = '\0';
    return ERANGE;
}

inline int strncat_s(char * dest, size_t num, const char * source, size_t count)
{
    if (!dest || !source)
        return EINVAL;

    size_t i, j;
    for(i = 0; i < num; i++)
    {
        if(dest[i] == '\0')
        {
            for(j = 0; (j + i) < num; j++)
            {
                if(j == count || (dest[j + i] = source[j]) == '\0')
                {
                    dest[j + i] = '\0';
                    return 0;
                }
            }
        }
    }

    return ERANGE;
}

#define _vsnprintf vsnprintf
inline int vsnprintf_s(char* buffer, size_t size, size_t, const char* format, va_list list)
{
    //TODO add bound check
    return vsnprintf(buffer, size, format, list);
}
#define _snprintf snprintf
//#define GetProcAddress(handle, name) dlsym(handle, name)
#define _chdir chdir
#define _strnicmp strnicmp
#define strnicmp strncasecmp
#define _getcwd getcwd
#define _snwprintf swprintf
#define swprintf_s swprintf
#define wcsicmp _wcsicmp
#define _wcsicmp wcscasecmp // OpenXRay used wcscmp, which is case-sensitive
#define _tempnam tempnam
#define _access xr_linux_access
#define _open xr_linux_open
#define _close close
inline int _sopen(const char* path, int flags, int /*share*/, int mode = 0644) { return xr_linux_open(path, flags, mode); }
#define _utime utime
#define _utimbuf utimbuf
// _sopen_s/_filelength: see io_compat.h (the OpenXRay macro never stored the handle).
#define _fdopen fdopen
inline int _rmdir(const char *path) { return xr_linux_rmdir(path); }
#define _write write
#define _strupr strupr
#define _read xr_read
#define _set_new_handler std::set_new_handler
#define _finite isfinite
inline int _mkdir(const char *dir) { return xr_linux_mkdir(dir); }

#define _wtoi(arg) wcstol(arg, NULL, 10)
#define _wtoi64(arg) wcstoll(arg, NULL, 10)
#undef min
#undef max
#define __max(a, b) std::max(a, b)
#define __min(a, b) std::min(a, b)

#define _locale_t locale_t
#define _isalpha_l isalpha_l
#define _create_locale(category, arg) newlocale(category, arg, (locale_t) 0)

// #define ZeroMemory(p, sz) memset((p), 0, (sz))  -- defined by windows_base.h
#define CopyMemory(d, s, n) memcpy(d, s, n)

// #define RGB(r,g,b) ( ((DWORD)(uint8_t)r)|((DWORD)((uint8_t)g)<<8)|((DWORD)((uint8_t)b)<<16) )  -- defined by windows_base.h
// #define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)  -- defined by windows_base.h
// #define FAILED(hr) (((HRESULT)(hr)) < 0)  -- defined by windows_base.h
// #define S_OK 0x00000000  -- defined by windows_base.h
// #define S_FALSE 0x10000000  -- defined by windows_base.h
// #define E_FAIL 0x80004005  -- defined by windows_base.h

#define _MAX_DRIVE	3
#define _MAX_DIR	256
#define _MAX_FNAME	256
#define _MAX_EXT	256

typedef DWORD           FOURCC;

typedef struct _AVIINDEXENTRY {
    DWORD	ckid;
    DWORD	dwFlags;
    DWORD	dwChunkOffset;
    DWORD	dwChunkLength;
} AVIINDEXENTRY;

typedef void *HIC;

inline BOOL SwitchToThread() { return (0 == sched_yield()); }

template <typename T>
decltype(auto) do_nothing(const T& obj)
{
    return obj;
}

#define xr_fs_strlwr(str) do_nothing(str)
#define xr_fs_nostrlwr(str) xr_strlwr(str)

/// For backward compability of FS, for real filesystem delimiter set to back
inline void restore_path_separators(char * path)
{
    while (char* sep = strchr(path, '/')) *sep = '\\'; //
}

inline void convert_path_separators(char * path)
{
    while (char* sep = strchr(path, '\\')) *sep = '/';
}

inline int xr_unlink(const char *path) { return xr_linux_unlink(path); }

inline tm* localtime_safe(const time_t *time, struct tm* result){ return localtime_r(time, result); }

#define xr_strerror(errno, buffer, bufferSize) strerror_r(errno, buffer, sizeof(buffer))

using xrpid_t = pid_t;

// This is a drop-in replacement for calls to linux 'read' function. We use this because unlike other OSes
// Linux 'read' function can return less then the requested number of bytes and might need to be called multiple times.
// Apart from this, it behaves exactly as you would expect and matches the other OSes implementation.
// See also: https://www.man7.org/linux/man-pages/man2/read.2.html
inline ssize_t xr_read(int file_handle, void *buffer, size_t count)
{
    ssize_t total_r_bytes = 0;
    do
    {
        const ssize_t r_bytes =
            read(file_handle, reinterpret_cast<unsigned char*>(buffer) + total_r_bytes, count - total_r_bytes);

        // Check for error
        if (r_bytes == -1)
            return -1;

        // Check for EOF otherwise we would loop indefinitely
        if (r_bytes == 0)
            return total_r_bytes;

        total_r_bytes += r_bytes;
    } while (static_cast<size_t>(total_r_bytes) < count);

    return total_r_bytes;
}
