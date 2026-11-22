// Linux stand-in for MSVC <direct.h> (_getcwd/_mkdir/_rmdir come from PlatformLinux.inl).
#pragma once
#include <sys/stat.h>
#include <unistd.h>
