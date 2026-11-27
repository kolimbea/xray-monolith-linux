#include "path_resolve.h"
#include <cstdio>
#include <unistd.h>

#include <cctype>
#include <dirent.h>
#include <mutex>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

namespace
{
using Listing = std::unordered_map<std::string, std::string>; // lower-case name -> real name

// Never destroyed: engine threads (the freeze watchdog flushing the log) still resolve paths while
// static destructors run at exit; a destroyed map crashed there (SIGSEGV in xr_resolve_invalidate).
std::mutex& g_lock = *new std::mutex;
std::unordered_map<std::string, Listing>& g_dirs = *new std::unordered_map<std::string, Listing>; // real dir -> listing

std::string lower(std::string s)
{
    for (char& c : s)
        c = (char)tolower((unsigned char)c);
    return s;
}

bool exists(const std::string& p)
{
    struct stat sb;
    return lstat(p.c_str(), &sb) == 0;
}

// Caller holds g_lock.
const Listing* listing(const std::string& dir, bool refresh)
{
    auto it = g_dirs.find(dir);
    if (it != g_dirs.end() && !refresh)
        return &it->second;
    DIR* d = opendir(dir.empty() ? "/" : dir.c_str());
    if (!d)
        return nullptr;
    Listing l;
    while (dirent* e = readdir(d))
        l.emplace(lower(e->d_name), e->d_name); // first spelling wins if names differ only by case
    closedir(d);
    return &(g_dirs[dir] = std::move(l));
}

std::string normalize(const char* path)
{
    std::string out;
    out.reserve(256);
    for (const char* p = path; *p; ++p)
    {
        const char c = *p == '\\' ? '/' : *p;
        if (c == '/' && !out.empty() && out.back() == '/')
            continue;
        out += c;
    }
    return out;
}
} // namespace

std::string xr_resolve_path(const char* path)
{
    if (!path || !*path)
        return std::string();
    const std::string norm = normalize(path);
    if (exists(norm))
        return norm;

    const bool absolute = norm[0] == '/';
    const bool trailing = norm.size() > 1 && norm.back() == '/';
    std::vector<std::string> parts;
    for (size_t pos = absolute ? 1 : 0; pos < norm.size();)
    {
        size_t next = norm.find('/', pos);
        if (next == std::string::npos)
            next = norm.size();
        if (next > pos)
            parts.push_back(norm.substr(pos, next - pos));
        pos = next + 1;
    }

    std::lock_guard<std::mutex> g(g_lock);
    std::string dir = absolute ? "" : "."; // real path of the directory resolved so far
    std::string out = absolute ? "" : ".";
    size_t i = 0;
    for (; i < parts.size(); ++i)
    {
        const std::string& part = parts[i];
        std::string real = part;
        if (part != "." && part != ".." && !exists(dir + "/" + part))
        {
            const std::string key = lower(part);
            const Listing* l = listing(dir.empty() ? "/" : dir, false);
            const std::string* hit = nullptr;
            if (l)
            {
                auto it = l->find(key);
                if (it == l->end())
                {
                    l = listing(dir.empty() ? "/" : dir, true); // directory may have changed since cached
                    it = l ? l->find(key) : it;
                }
                if (l && it != l->end())
                    hit = &it->second;
            }
            if (!hit)
                break; // nothing matches: keep this and the remaining parts verbatim
            real = *hit;
        }
        dir += "/" + real;
        out += "/" + real;
    }
    for (; i < parts.size(); ++i)
        out += "/" + parts[i];
    if (!absolute)
        out.erase(0, 2); // drop the "./" prefix used while walking
    if (trailing)
        out += '/';
    return out;
}

void xr_resolve_invalidate(const char* path)
{
    std::lock_guard<std::mutex> g(g_lock);
    if (!path)
    {
        g_dirs.clear();
        return;
    }
    std::string norm = normalize(path);
    const size_t slash = norm.rfind('/');
    g_dirs.erase(slash == std::string::npos ? "." : norm.substr(0, slash));
}

void xr_detach_for_write(const std::string& real, bool keep_content)
{
    struct stat sb;
    if (lstat(real.c_str(), &sb) != 0 || !S_ISLNK(sb.st_mode))
        return;
    std::string content;
    if (keep_content)
        if (FILE* in = ::fopen(real.c_str(), "rb"))
        {
            char buf[65536];
            for (size_t n; (n = fread(buf, 1, sizeof(buf), in)) > 0;)
                content.append(buf, n);
            fclose(in);
        }
    if (unlink(real.c_str()) != 0)
        return;
    if (FILE* out = ::fopen(real.c_str(), "wb"))
    {
        fwrite(content.data(), 1, content.size(), out);
        fclose(out);
    }
}
