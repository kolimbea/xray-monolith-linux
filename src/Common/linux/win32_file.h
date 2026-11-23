// Win32 file handles, file mappings and views over POSIX open/mmap — just the subset the
// locator and stream readers use. Handles are owned objects; CloseHandle releases either kind.
#pragma once
#include <cstddef>
#include <windows_base.h> // HANDLE, INVALID_HANDLE_VALUE, WAIT_* (dxvk-native)
#include <cstdint>

typedef void* HANDLE;
// #define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)  -- defined by windows_base.h

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x1u
#define FILE_SHARE_WRITE 0x2u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define FILE_ATTRIBUTE_READONLY 0x1u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define FILE_FLAG_NO_BUFFERING 0x20000000u
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000u
#define PAGE_READONLY 0x02u
#define PAGE_READWRITE 0x04u
#define FILE_MAP_READ 0x4u
#define FILE_MAP_WRITE 0x2u
#define FILE_MAP_ALL_ACCESS 0xF001Fu

struct _SECURITY_ATTRIBUTES;
typedef struct
{
    uint32_t dwPageSize;
    uint32_t dwAllocationGranularity;
    uint32_t dwNumberOfProcessors;
} SYSTEM_INFO;

void GetSystemInfo(SYSTEM_INFO* si);
HANDLE CreateFile(const char* path, uint32_t access, uint32_t share, _SECURITY_ATTRIBUTES*, uint32_t disposition,
                  uint32_t flags, HANDLE templ);
#define CreateFileA CreateFile
uint32_t GetFileSize(HANDLE file, uint32_t* high);
HANDLE CreateFileMapping(HANDLE file, _SECURITY_ATTRIBUTES*, uint32_t protect, uint32_t size_high, uint32_t size_low,
                         const char* name);
#define CreateFileMappingA CreateFileMapping
void* MapViewOfFile(HANDLE mapping, uint32_t access, uint32_t off_high, uint32_t off_low, size_t bytes);
int UnmapViewOfFile(const void* view);
int CloseHandle(HANDLE h);

// Threads (CreateThread / WaitFor*Object(s)) share the handle type so CloseHandle works on them.
typedef HANDLE* LPHANDLE;
typedef uint32_t (*LPTHREAD_START_ROUTINE)(void*);
#define INFINITE 0xFFFFFFFFu
// #define WAIT_OBJECT_0 0u  -- defined by windows_base.h
// #define WAIT_TIMEOUT 0x102u  -- defined by windows_base.h
// #define WAIT_FAILED 0xFFFFFFFFu  -- defined by windows_base.h
HANDLE CreateThread(_SECURITY_ATTRIBUTES*, size_t stack, LPTHREAD_START_ROUTINE fn, void* arg, uint32_t flags,
                    uint32_t* thread_id);
// Only INFINITE waits on thread handles are supported (all the engine uses); others fail.
uint32_t WaitForSingleObject(HANDLE h, uint32_t ms);
uint32_t WaitForMultipleObjects(uint32_t n, const HANDLE* hs, int wait_all, uint32_t ms);

#define FILE_BEGIN 0u
#define FILE_CURRENT 1u
#define FILE_END 2u
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFu
struct _OVERLAPPED;
// Synchronous only: overlapped must be NULL.
int ReadFile(HANDLE file, void* buf, uint32_t n, uint32_t* read, _OVERLAPPED* overlapped);
int WriteFile(HANDLE file, const void* buf, uint32_t n, uint32_t* written, _OVERLAPPED* overlapped);
uint32_t SetFilePointer(HANDLE file, int32_t distance, int32_t* distance_high, uint32_t method);
