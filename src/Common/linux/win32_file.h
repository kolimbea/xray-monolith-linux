// Win32 file handles, file mappings and views over POSIX open/mmap — just the subset the
// locator and stream readers use. Handles are owned objects; CloseHandle releases either kind.
#pragma once
#include <cstddef>
#include <cstdint>

typedef void* HANDLE;
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

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
