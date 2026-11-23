// Process/system queries the engine makes at startup, over /proc and POSIX.
#pragma once
#include <cstdint>

// Whole command line as one string, built once from /proc/self/cmdline. Arguments containing
// spaces are double-quoted, as Windows would present them.
const char* GetCommandLine();
#define GetCommandLineA GetCommandLine

// Only the main executable is a "module" here (all-in-one build), so any handle means the exe.
void* GetModuleHandle(const char* name);
uint32_t GetModuleFileName(void* module, char* out, uint32_t size);
uint32_t GetCurrentDirectory(uint32_t size, char* out);
int SetCurrentDirectory(const char* path);
int GetUserName(char* out, uint32_t* size);
int GetComputerName(char* out, uint32_t* size);
#define GetModuleHandleA GetModuleHandle
#define GetModuleFileNameA GetModuleFileName
#define GetCurrentDirectoryA GetCurrentDirectory
#define SetCurrentDirectoryA SetCurrentDirectory
#define GetUserNameA GetUserName
#define GetComputerNameA GetComputerName

// COM is not used on Linux.
#define COINIT_MULTITHREADED 0
#define COINIT_APARTMENTTHREADED 2
inline long CoInitializeEx(void*, unsigned) { return 0; }
inline long CoInitialize(void*) { return 0; }
inline void CoUninitialize() {}

#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1
#define DLL_THREAD_ATTACH 2
#define DLL_THREAD_DETACH 3

// x87 control word helpers; on x86-64 these map to the SSE rounding control (see _controlfp).
unsigned _controlfp(unsigned value, unsigned mask);
inline unsigned _control87(unsigned value, unsigned mask) { return _controlfp(value, mask); }
unsigned _clear87();
#define _clearfp _clear87

#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef PASCAL
#define PASCAL
#endif

// dlopen-based module loading. "name.dll" is looked up as "libname.so" (and "name.so") next to the
// executable first, then on the normal library path; missing modules return NULL like Win32.
void* LoadLibrary(const char* name);
#define LoadLibraryA LoadLibrary
void* GetProcAddress(void* module, const char* symbol);
int FreeLibrary(void* module);
