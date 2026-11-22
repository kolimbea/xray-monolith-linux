#include "io_compat.h"

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
    *fd = open(path, oflag, pmode);
    return *fd < 0 ? errno : 0;
}

long _filelength(int fd)
{
    struct stat sb;
    return fstat(fd, &sb) == 0 ? (long)sb.st_size : -1L;
}
