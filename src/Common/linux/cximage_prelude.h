// Force-included into cximage on Linux. cximage defines min/max as macros (ximadef.h); every standard
// header it (or the compat layer) pulls in must be parsed before that, or libstdc++ breaks.
#pragma once
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <windows_base.h>
#include <strings.h>
// TCHAR is char in this (non-UNICODE) build.
typedef const char* LPCTSTR;
typedef char* LPTSTR;
#define _tcsnicmp strncasecmp
#define _tfopen fopen
