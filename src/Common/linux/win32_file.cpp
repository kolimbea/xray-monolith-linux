#include "win32_file.h"

#include <cerrno>
#include <fcntl.h>
#include <mutex>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>

namespace
{
enum class Kind { File, Mapping };
struct Handle
{
    Kind kind;
    int fd;        // File: owned; Mapping: dup of the file's fd, owned
    bool writable;
};

std::mutex g_views_lock;
std::unordered_map<const void*, size_t> g_views; // view base -> length, for munmap

Handle* as(HANDLE h) { return (h && h != INVALID_HANDLE_VALUE) ? (Handle*)h : nullptr; }
} // namespace

void GetSystemInfo(SYSTEM_INFO* si)
{
    const long page = sysconf(_SC_PAGESIZE);
    si->dwPageSize = (uint32_t)page;
    // Windows uses 64 KiB; the engine aligns view offsets to this, and it is a page multiple.
    si->dwAllocationGranularity = 64 * 1024;
    si->dwNumberOfProcessors = (uint32_t)sysconf(_SC_NPROCESSORS_ONLN);
}

HANDLE CreateFile(const char* path, uint32_t access, uint32_t, _SECURITY_ATTRIBUTES*, uint32_t disposition,
                  uint32_t, HANDLE)
{
    int flags = (access & GENERIC_WRITE) ? ((access & GENERIC_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
    if (disposition == CREATE_ALWAYS)
        flags |= O_CREAT | O_TRUNC;
    const int fd = open(path, flags | O_CLOEXEC, 0644);
    if (fd < 0)
        return INVALID_HANDLE_VALUE;
    struct stat sb;
    if (fstat(fd, &sb) != 0 || S_ISDIR(sb.st_mode)) // Win32 CreateFile fails on directories without backup semantics
    {
        close(fd);
        return INVALID_HANDLE_VALUE;
    }
    return new Handle{Kind::File, fd, (access & GENERIC_WRITE) != 0};
}

uint32_t GetFileSize(HANDLE file, uint32_t* high)
{
    Handle* h = as(file);
    struct stat sb;
    if (!h || fstat(h->fd, &sb) != 0)
        return 0xFFFFFFFFu; // INVALID_FILE_SIZE
    if (high)
        *high = (uint32_t)((uint64_t)sb.st_size >> 32);
    return (uint32_t)sb.st_size;
}

HANDLE CreateFileMapping(HANDLE file, _SECURITY_ATTRIBUTES*, uint32_t protect, uint32_t, uint32_t, const char*)
{
    Handle* h = as(file);
    if (!h || h->kind != Kind::File)
        return nullptr; // Win32 returns NULL (not INVALID_HANDLE_VALUE) on failure
    const int fd = dup(h->fd);
    if (fd < 0)
        return nullptr;
    return new Handle{Kind::Mapping, fd, protect == PAGE_READWRITE};
}

void* MapViewOfFile(HANDLE mapping, uint32_t access, uint32_t off_high, uint32_t off_low, size_t bytes)
{
    Handle* h = as(mapping);
    if (!h || h->kind != Kind::Mapping)
        return nullptr;
    const off_t off = (off_t)(((uint64_t)off_high << 32) | off_low);
    if (bytes == 0) // whole file from offset
    {
        struct stat sb;
        if (fstat(h->fd, &sb) != 0 || sb.st_size <= off)
            return nullptr;
        bytes = (size_t)(sb.st_size - off);
    }
    const bool write = (access & FILE_MAP_WRITE) && h->writable;
    void* p = mmap(nullptr, bytes, PROT_READ | (write ? PROT_WRITE : 0), MAP_SHARED, h->fd, off);
    if (p == MAP_FAILED)
        return nullptr;
    std::lock_guard<std::mutex> g(g_views_lock);
    g_views[p] = bytes;
    return p;
}

int UnmapViewOfFile(const void* view)
{
    size_t len;
    {
        std::lock_guard<std::mutex> g(g_views_lock);
        auto it = g_views.find(view);
        if (it == g_views.end())
            return 0;
        len = it->second;
        g_views.erase(it);
    }
    return munmap(const_cast<void*>(view), len) == 0;
}

int CloseHandle(HANDLE handle)
{
    Handle* h = as(handle);
    if (!h)
        return 0;
    close(h->fd);
    delete h;
    return 1;
}
