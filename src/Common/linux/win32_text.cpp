#include "win32_text.h"

#include <cerrno>
#include <cstring>
#include <iconv.h>
#include <vector>

namespace
{
const char* charset(unsigned cp)
{
    switch (cp)
    {
    case CP_UTF8: return "UTF-8";
    case CP_ACP:
    case 1251: return "CP1251";
    default: return nullptr;
    }
}

// Converts n bytes; returns produced bytes or -1. With out == nullptr only measures.
// Unconvertible input is replaced ('?' / U+003F) like Win32 does without WC_NO_BEST_FIT_CHARS.
long convert(const char* to, const char* from, const char* in, size_t n, char* out, size_t cap)
{
    iconv_t cd = iconv_open(to, from);
    if (cd == (iconv_t)-1)
        return -1;
    std::vector<char> scratch;
    if (!out)
    {
        scratch.resize(n * 4 + 16);
        out = scratch.data();
        cap = scratch.size();
    }
    char* ip = const_cast<char*>(in);
    char* op = out;
    size_t il = n, ol = cap;
    long result = 0;
    while (il > 0)
    {
        if (iconv(cd, &ip, &il, &op, &ol) != (size_t)-1)
            break;
        if (errno == EILSEQ || errno == EINVAL)
        {
            // skip one bad input unit, emit a replacement
            const size_t unit = std::strcmp(from, "WCHAR_T") == 0 ? sizeof(wchar_t) : 1;
            ip += unit;
            il = il > unit ? il - unit : 0;
            char q = '?';
            char* qp = &q;
            size_t ql = 1;
            iconv_t rep = iconv_open(to, "ASCII");
            if (rep == (iconv_t)-1 || iconv(rep, &qp, &ql, &op, &ol) == (size_t)-1)
            {
                if (rep != (iconv_t)-1)
                    iconv_close(rep);
                result = -1;
                break;
            }
            iconv_close(rep);
            continue;
        }
        result = -1; // E2BIG: destination too small
        break;
    }
    iconv_close(cd);
    return result < 0 ? -1 : (long)(op - out);
}
} // namespace

int MultiByteToWideChar(unsigned cp, unsigned long, const char* src, int srclen, wchar_t* dst, int dstlen)
{
    const char* cs = charset(cp);
    if (!cs || !src)
        return 0;
    const size_t n = srclen < 0 ? std::strlen(src) + 1 : (size_t)srclen; // -1 includes the terminator
    const long bytes = convert("WCHAR_T", cs, src, n, dstlen ? (char*)dst : nullptr, (size_t)dstlen * sizeof(wchar_t));
    return bytes < 0 ? 0 : (int)(bytes / sizeof(wchar_t));
}

int WideCharToMultiByte(unsigned cp, unsigned long, const wchar_t* src, int srclen, char* dst, int dstlen, const char*,
                        int* used_default)
{
    if (used_default)
        *used_default = 0;
    const char* cs = charset(cp);
    if (!cs || !src)
        return 0;
    const size_t n = (srclen < 0 ? std::wcslen(src) + 1 : (size_t)srclen) * sizeof(wchar_t);
    const long bytes = convert(cs, "WCHAR_T", (const char*)src, n, dstlen ? dst : nullptr, (size_t)dstlen);
    return bytes < 0 ? 0 : (int)bytes;
}
