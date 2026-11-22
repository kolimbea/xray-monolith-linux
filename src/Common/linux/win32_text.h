// Win32 MultiByteToWideChar / WideCharToMultiByte over iconv.
// Supported code pages: CP_UTF8 (65001), 1251, and CP_ACP which the game assumes is 1251
// (Anomaly/GAMMA texts are Cyrillic CP1251). Other code pages fail with 0, as Win32 does.
#pragma once
#include <cwchar>

#define CP_ACP 0
#define CP_UTF8 65001

int MultiByteToWideChar(unsigned codepage, unsigned long flags, const char* src, int srclen, wchar_t* dst, int dstlen);
int WideCharToMultiByte(unsigned codepage, unsigned long flags, const wchar_t* src, int srclen, char* dst, int dstlen,
                        const char* defchar, int* used_default);
