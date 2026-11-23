// MSVC <io.h> directory scan and file-open API used by the locator, over POSIX.
#pragma once
#include <cstdint>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_SUBDIR 0x10
#define _A_ARCH 0x20

#ifndef _O_RDONLY
#define _O_RDONLY O_RDONLY
#endif
#define _O_BINARY 0 // POSIX has no text mode
#define _SH_DENYNO 0

// Named like MSVC's 64-bit-time variant because the locator forward-declares that struct tag.
struct _finddata64i32_t
{
    unsigned attrib;
    time_t time_create;
    time_t time_access;
    time_t time_write;
    uint64_t size;
    char name[260];
};
typedef _finddata64i32_t _finddata_t;

#define _findfirst64i32 _findfirst
#define _findnext64i32 _findnext
// "struct _stat st; _stat(path, &st);" — a struct and a function may share a name in C++.
struct _stat : stat
{
};
int _stat(const char* path, struct _stat* out);
#define _stat64i32 _stat

// pattern is "<dir>/<mask>"; masks are matched case-insensitively (fnmatch), as on Windows,
// and "*.*" matches every name. "." and ".." are reported like on Windows.
intptr_t _findfirst(const char* pattern, _finddata_t* out);
int _findnext(intptr_t handle, _finddata_t* out);
int _findclose(intptr_t handle);

#include <cstdio>
// Path-resolving (case-insensitive, '\\'-tolerant) versions of the POSIX file calls.
FILE* xr_linux_fopen(const char* path, const char* mode);
int xr_linux_open(const char* path, int flags, int mode = 0644);
int xr_linux_access(const char* path, int mode);
int xr_linux_unlink(const char* path);
int xr_linux_mkdir(const char* path);
int xr_linux_rmdir(const char* path);
int xr_linux_rename(const char* from, const char* to);

int _sopen_s(int* fd, const char* path, int oflag, int shflag, int pmode);
long _filelength(int fd);
