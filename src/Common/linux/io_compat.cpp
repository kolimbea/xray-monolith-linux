#include "io_compat.h"
#include "path_resolve.h"

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fnmatch.h>
#include <string>

namespace
{
struct FindState
{
    DIR* dir;
    std::string base; // directory with trailing '/'
    std::string mask;
};

bool fill(FindState* st, _finddata_t* out)
{
    while (dirent* e = readdir(st->dir))
    {
        if (st->mask != "*.*" && st->mask != "*" && fnmatch(st->mask.c_str(), e->d_name, FNM_CASEFOLD) != 0)
            continue;
        struct stat sb;
        const std::string full = st->base + e->d_name;
        if (stat(full.c_str(), &sb) != 0)
            continue; // dangling symlink or raced deletion: skip like Windows never lists it
        std::memset(out, 0, sizeof(*out));
        std::strncpy(out->name, e->d_name, sizeof(out->name) - 1);
        out->attrib = S_ISDIR(sb.st_mode) ? _A_SUBDIR : _A_NORMAL;
        if (access(full.c_str(), W_OK) != 0)
            out->attrib |= _A_RDONLY;
        // Dot-files are hidden only by convention; "." and ".." must stay visible for Windows parity.
        if (e->d_name[0] == '.' && std::strcmp(e->d_name, ".") && std::strcmp(e->d_name, ".."))
            out->attrib |= _A_HIDDEN;
        out->time_create = sb.st_ctime;
        out->time_access = sb.st_atime;
        out->time_write = sb.st_mtime;
        out->size = S_ISDIR(sb.st_mode) ? 0 : (uint64_t)sb.st_size;
        return true;
    }
    return false;
}
} // namespace

intptr_t _findfirst(const char* pattern, _finddata_t* out)
{
    std::string p(pattern);
    for (char& c : p)
        if (c == '\\')
            c = '/';
    {
        // resolve the directory part case-insensitively; the mask is matched with FNM_CASEFOLD
        const size_t cut = p.rfind('/');
        if (cut != std::string::npos)
            p = xr_resolve_path(p.substr(0, cut + 1).c_str()) + p.substr(cut + 1);
    }
    const size_t slash = p.rfind('/');
    auto* st = new FindState{nullptr, slash == std::string::npos ? "./" : p.substr(0, slash + 1),
                             slash == std::string::npos ? p : p.substr(slash + 1)};
    st->dir = opendir(st->base.c_str());
    if (!st->dir || !fill(st, out))
    {
        if (st->dir)
            closedir(st->dir);
        delete st;
        errno = ENOENT;
        return -1;
    }
    return (intptr_t)st;
}

int _findnext(intptr_t handle, _finddata_t* out)
{
    if (handle == -1)
        return -1;
    return fill((FindState*)handle, out) ? 0 : -1;
}

int _findclose(intptr_t handle)
{
    if (handle == -1)
        return -1;
    auto* st = (FindState*)handle;
    closedir(st->dir);
    delete st;
    return 0;
}

int _sopen_s(int* fd, const char* path, int oflag, int, int pmode)
{
    const std::string real = xr_resolve_path(path);
    *fd = open(real.c_str(), oflag, pmode);
    if (*fd >= 0 && (oflag & O_CREAT))
        xr_resolve_invalidate(real.c_str());
    return *fd < 0 ? errno : 0;
}

long _filelength(int fd)
{
    struct stat sb;
    return fstat(fd, &sb) == 0 ? (long)sb.st_size : -1L;
}

FILE* xr_linux_fopen(const char* path, const char* mode)
{
    const std::string real = xr_resolve_path(path);
    FILE* f = ::fopen(real.c_str(), mode);
    if (f && (strchr(mode, 'w') || strchr(mode, 'a')))
        xr_resolve_invalidate(real.c_str());
    return f;
}

int xr_linux_open(const char* path, int flags, int mode)
{
    const std::string real = xr_resolve_path(path);
    const int fd = ::open(real.c_str(), flags, mode);
    if (fd >= 0 && (flags & O_CREAT))
        xr_resolve_invalidate(real.c_str());
    return fd;
}

int xr_linux_access(const char* path, int mode) { return ::access(xr_resolve_path(path).c_str(), mode); }

int xr_linux_unlink(const char* path)
{
    const std::string real = xr_resolve_path(path);
    const int r = ::unlink(real.c_str());
    xr_resolve_invalidate(real.c_str());
    return r;
}

int xr_linux_mkdir(const char* path)
{
    const std::string real = xr_resolve_path(path);
    const int r = ::mkdir(real.c_str(), 0755);
    xr_resolve_invalidate(real.c_str());
    return r;
}

int xr_linux_rmdir(const char* path)
{
    const std::string real = xr_resolve_path(path);
    const int r = ::rmdir(real.c_str());
    xr_resolve_invalidate(real.c_str());
    return r;
}

int xr_linux_rename(const char* from, const char* to)
{
    const std::string a = xr_resolve_path(from), b = xr_resolve_path(to);
    const int r = ::rename(a.c_str(), b.c_str());
    xr_resolve_invalidate(a.c_str());
    xr_resolve_invalidate(b.c_str());
    return r;
}

int _stat(const char* path, struct _stat* out) { return ::stat(xr_resolve_path(path).c_str(), out); }
