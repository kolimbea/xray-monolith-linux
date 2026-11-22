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
#define _S_IREAD S_IRUSR

struct _finddata_t
{
    unsigned attrib;
    time_t time_create;
    time_t time_access;
    time_t time_write;
    uint64_t size;
    char name[260];
};

// pattern is "<dir>/<mask>"; masks are matched case-insensitively (fnmatch), as on Windows,
// and "*.*" matches every name. "." and ".." are reported like on Windows.
intptr_t _findfirst(const char* pattern, _finddata_t* out);
int _findnext(intptr_t handle, _finddata_t* out);
int _findclose(intptr_t handle);

int _sopen_s(int* fd, const char* path, int oflag, int shflag, int pmode);
long _filelength(int fd);
