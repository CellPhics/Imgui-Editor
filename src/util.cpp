#include "util.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace fs = std::filesystem;

// ---------------------------------------------------------------- files

bool FileExists(const std::string& path)
{
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

long long FileMTime(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
}

std::string ReadFile(const std::string& path, bool* ok)
{
    std::ifstream f(path, std::ios::binary);
    if (ok) *ok = (bool)f;
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool WriteFileAtomic(const std::string& path, const std::string& data)
{
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << data;
        if (!f) return false;
    }
    fs::rename(tmp, path, ec);
    return !ec;
}

std::string HomeDir()
{
    const char* h = getenv("HOME");
    return h ? h : ".";
}

std::string ExpandHome(std::string p)
{
    while (!p.empty() && (p.back() == ' ' || p.back() == '\n' || p.back() == '\r')) p.pop_back();
    if (p == "~" || p.rfind("~/", 0) == 0) p = HomeDir() + p.substr(1);
    return p;
}

// ---------------------------------------------------------------- shell

std::string RunCommand(const std::string& cmd, int* exit_status)
{
    std::string out;
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) {
        if (exit_status) *exit_status = -1;
        return out;
    }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    const int st = pclose(f);
    if (exit_status) *exit_status = st;
    return out;
}

bool HaveCommand(const char* name)
{
    return system((std::string("command -v ") + name + " >/dev/null 2>&1").c_str()) == 0;
}

std::string ShellQuote(const std::string& s)
{
    // wrap in single quotes; a ' inside becomes '\''
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

bool OnWayland() { return getenv("WAYLAND_DISPLAY") != nullptr; }

// ---------------------------------------------------------------- strings

std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

bool ContainsNoCase(const std::string& haystack, const std::string& needle)
{
    return needle.empty() || ToLower(haystack).find(ToLower(needle)) != std::string::npos;
}

bool HasExtension(const std::string& path, std::initializer_list<const char*> exts)
{
    const std::string e = ToLower(fs::path(path).extension().string());
    for (const char* x : exts)
        if (e == x) return true;
    return false;
}

bool IsImageFile(const std::string& path)
{
    return HasExtension(path, {".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif", ".tga"});
}

std::vector<std::string> SplitLines(const std::string& s)
{
    std::vector<std::string> lines;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { lines.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

std::string Utf8(unsigned c)
{
    std::string s;
    if (c < 0x80) s += (char)c;
    else if (c < 0x800) { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { s += (char)(0xE0 | (c >> 12)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
    else { s += (char)(0xF0 | (c >> 18)); s += (char)(0x80 | ((c >> 12) & 0x3F)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
    return s;
}

// ---------------------------------------------------------------- time

double Now()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
