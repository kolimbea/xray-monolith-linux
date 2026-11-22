#include "win32_system.h"

#include <cfenv>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <pwd.h>
#include <string>
#include <unistd.h>

const char* GetCommandLine()
{
    static std::string line;
    static std::once_flag once;
    std::call_once(once, [] {
        FILE* f = fopen("/proc/self/cmdline", "rb");
        if (!f)
            return;
        std::string arg;
        bool first = true;
        auto flush = [&] {
            if (!first)
                line += ' ';
            first = false;
            const bool quote = arg.find(' ') != std::string::npos;
            line += quote ? "\"" + arg + "\"" : arg;
            arg.clear();
        };
        int c;
        while ((c = fgetc(f)) != EOF)
        {
            if (c == 0)
                flush();
            else
                arg += (char)c;
        }
        if (!arg.empty())
            flush();
        fclose(f);
    });
    return line.c_str();
}

void* GetModuleHandle(const char*) { return (void*)GetModuleHandle; }

uint32_t GetModuleFileName(void*, char* out, uint32_t size)
{
    if (!out || size == 0)
        return 0;
    const ssize_t n = readlink("/proc/self/exe", out, size - 1);
    if (n < 0)
    {
        out[0] = 0;
        return 0;
    }
    out[n] = 0;
    return (uint32_t)n;
}

uint32_t GetCurrentDirectory(uint32_t size, char* out)
{
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof(buf)))
        return 0;
    std::string s(buf);
    if (s.back() != '/')
        s += '/'; // engine concatenates file names directly onto it
    if (!out || size <= s.size())
        return (uint32_t)s.size() + 1; // Win32: required size including terminator
    memcpy(out, s.c_str(), s.size() + 1);
    return (uint32_t)s.size();
}

int SetCurrentDirectory(const char* path) { return chdir(path) == 0; }

static int copy_name(const char* name, char* out, uint32_t* size)
{
    const size_t n = strlen(name);
    if (!out || !size || *size <= n)
    {
        if (size)
            *size = (uint32_t)n + 1;
        return 0;
    }
    memcpy(out, name, n + 1);
    *size = (uint32_t)n + 1; // GetUserName counts the terminator
    return 1;
}

int GetUserName(char* out, uint32_t* size)
{
    const char* name = getenv("USER");
    if (!name || !*name)
    {
        const passwd* pw = getpwuid(getuid());
        name = pw ? pw->pw_name : "user";
    }
    return copy_name(name, out, size);
}

int GetComputerName(char* out, uint32_t* size)
{
    char host[256] = "localhost";
    gethostname(host, sizeof(host) - 1);
    const int ok = copy_name(host, out, size);
    if (ok)
        *size -= 1; // GetComputerName does not count the terminator
    return ok;
}

unsigned _clear87()
{
    const int raised = fetestexcept(FE_ALL_EXCEPT);
    feclearexcept(FE_ALL_EXCEPT);
    return (unsigned)raised;
}
