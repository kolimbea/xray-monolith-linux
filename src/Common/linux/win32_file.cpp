#include "win32_file.h"
#include "path_resolve.h"

#include <cerrno>
#include <fcntl.h>
#include <mutex>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <unordered_map>

namespace
{
enum class Kind { File, Mapping, Thread };
struct Handle
{
    Kind kind;
    int fd;        // File: owned; Mapping: dup of the file's fd, owned; Thread: unused (-1)
    bool writable;
    pthread_t tid{};
    bool joined = false;
    LPTHREAD_START_ROUTINE fn = nullptr;
    void* arg = nullptr;
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
    const std::string real = xr_resolve_path(path);
    const int fd = open(real.c_str(), flags | O_CLOEXEC, 0644);
    if (fd >= 0 && (flags & O_CREAT))
        xr_resolve_invalidate(real.c_str());
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
    if (h->kind == Kind::Thread)
    {
        if (!h->joined)
            pthread_detach(h->tid); // Win32 lets a thread outlive its closed handle
    }
    else
        close(h->fd);
    delete h;
    return 1;
}

HANDLE CreateThread(_SECURITY_ATTRIBUTES*, size_t stack, LPTHREAD_START_ROUTINE fn, void* arg, uint32_t, uint32_t* id)
{
    auto* h = new Handle{Kind::Thread, -1, false};
    h->fn = fn;
    h->arg = arg;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (stack)
        pthread_attr_setstacksize(&attr, stack < PTHREAD_STACK_MIN ? PTHREAD_STACK_MIN : stack);
    const int err = pthread_create(&h->tid, &attr, [](void* p) -> void* {
        auto* self = (Handle*)p;
        self->fn(self->arg);
        return nullptr;
    }, h);
    pthread_attr_destroy(&attr);
    if (err)
    {
        delete h;
        return nullptr; // CreateThread returns NULL on failure
    }
    if (id)
        *id = (uint32_t)(uintptr_t)h->tid;
    return h;
}

uint32_t WaitForSingleObject(HANDLE handle, uint32_t ms)
{
    Handle* h = as(handle);
    if (!h || h->kind != Kind::Thread || ms != INFINITE)
        return WAIT_FAILED;
    if (!h->joined)
    {
        pthread_join(h->tid, nullptr);
        h->joined = true;
    }
    return WAIT_OBJECT_0;
}

uint32_t WaitForMultipleObjects(uint32_t n, const HANDLE* hs, int wait_all, uint32_t ms)
{
    if (!wait_all || ms != INFINITE)
        return WAIT_FAILED;
    for (uint32_t i = 0; i < n; ++i)
        if (WaitForSingleObject(hs[i], ms) != WAIT_OBJECT_0)
            return WAIT_FAILED;
    return WAIT_OBJECT_0;
}

int ReadFile(HANDLE file, void* buf, uint32_t n, uint32_t* read, _OVERLAPPED* overlapped)
{
    Handle* h = as(file);
    if (read)
        *read = 0;
    if (!h || h->kind != Kind::File || overlapped)
        return 0;
    size_t done = 0;
    while (done < n) // read() may return short counts; Win32 ReadFile on a file does not
    {
        const ssize_t r = ::read(h->fd, (char*)buf + done, n - done);
        if (r < 0 && errno == EINTR)
            continue;
        if (r < 0)
            return 0;
        if (r == 0)
            break; // EOF: success with fewer bytes, as on Windows
        done += (size_t)r;
    }
    if (read)
        *read = (uint32_t)done;
    return 1;
}

int WriteFile(HANDLE file, const void* buf, uint32_t n, uint32_t* written, _OVERLAPPED* overlapped)
{
    Handle* h = as(file);
    if (written)
        *written = 0;
    if (!h || h->kind != Kind::File || overlapped)
        return 0;
    size_t done = 0;
    while (done < n)
    {
        const ssize_t r = ::write(h->fd, (const char*)buf + done, n - done);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return 0;
        done += (size_t)r;
    }
    if (written)
        *written = (uint32_t)done;
    return 1;
}

uint32_t SetFilePointer(HANDLE file, int32_t distance, int32_t* distance_high, uint32_t method)
{
    Handle* h = as(file);
    if (!h || h->kind != Kind::File)
        return INVALID_SET_FILE_POINTER;
    const int whence = method == FILE_BEGIN ? SEEK_SET : method == FILE_CURRENT ? SEEK_CUR : SEEK_END;
    // Without distance_high the 32-bit distance is signed; with it, the pair forms a signed 64-bit value.
    const off_t dist = distance_high ? (off_t)(((uint64_t)(uint32_t)*distance_high << 32) | (uint32_t)distance)
                                     : (off_t)distance;
    const off_t pos = lseek(h->fd, dist, whence);
    if (pos < 0)
        return INVALID_SET_FILE_POINTER;
    if (distance_high)
        *distance_high = (int32_t)((uint64_t)pos >> 32);
    return (uint32_t)pos;
}

namespace
{
thread_local unsigned long g_last_error = 0;
}
unsigned long GetLastError() { return g_last_error; }
void SetLastError(unsigned long code) { g_last_error = code; }

HANDLE CreateMutex(_SECURITY_ATTRIBUTES*, int, const char* name)
{
    if (!name || !*name)
        return nullptr;
    std::string file;
    for (const char* p = name; *p; ++p)
        file += (*p == '/' || *p == '\\') ? '_' : *p; // "Local\\X" -> "Local_X"
    const char* dir = getenv("XDG_RUNTIME_DIR");
    const std::string path = std::string(dir && *dir ? dir : "/tmp") + "/" + file + ".lock";
    const int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0)
        return nullptr;
    SetLastError(flock(fd, LOCK_EX | LOCK_NB) == 0 ? 0 : ERROR_ALREADY_EXISTS);
    return new Handle{Kind::File, fd, false};
}
