#include "assets.h"
#include <GL/gl.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <sys/stat.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

namespace fs = std::filesystem;

// ---------------------------------------------------------------- files

long long FileMTime(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
}

bool FileExists(const std::string& path)
{
    std::error_code ec;
    return fs::is_regular_file(path, ec);
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
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << data;
        if (!f) return false;
    }
    fs::rename(tmp, path, ec);
    return !ec;
}

// ---------------------------------------------------------------- icons

static const IconEntry kIcons[] = {
#include "icons_map.inc"
};

const std::vector<IconEntry>& AllIcons()
{
    static std::vector<IconEntry> v(std::begin(kIcons), std::end(kIcons));
    return v;
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

const char* IconUtf8(const std::string& name)
{
    static std::unordered_map<std::string, std::string> map;
    if (map.empty())
        for (auto& e : kIcons) map[e.name] = Utf8(e.codepoint);
    if (name.empty()) return "";
    auto it = map.find(name);
    if (it == map.end()) {
        // Accept "fa-gear", "ICON_FA_GEAR", "gear_solid"-ish spellings.
        std::string n = name;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        for (const char* pre : {"icon_fa_", "fa-", "fa_"})
            if (n.rfind(pre, 0) == 0) n = n.substr(strlen(pre));
        std::replace(n.begin(), n.end(), '_', '-');
        it = map.find(n);
        if (it == map.end()) return "";
    }
    return it->second.c_str();
}

// ---------------------------------------------------------------- paths

std::string Assets::Resolve(const std::string& path) const
{
    if (path.empty()) return path;
    fs::path p(path);
    if (p.is_absolute()) return path;
    fs::path in_project = fs::path(project_dir) / p;
    if (fs::exists(in_project)) return in_project.string();
    fs::path in_data = fs::path(data_dir) / p;
    if (fs::exists(in_data)) return in_data.string();
    return in_project.string();
}

std::string Assets::IconFontPath() const { return (fs::path(data_dir) / "fonts" / "fa-solid-900.ttf").string(); }

std::vector<std::string> Assets::ListFiles(const std::string& subdir, const std::vector<std::string>& exts) const
{
    std::vector<std::string> out;
    std::error_code ec;
    fs::path dir = fs::path(project_dir) / subdir;
    if (!fs::is_directory(dir, ec)) return out;
    for (auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (std::find(exts.begin(), exts.end(), ext) == exts.end()) continue;
        out.push_back(fs::relative(e.path(), project_dir, ec).string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ---------------------------------------------------------------- fonts

bool Assets::SyncFonts(const json& doc)
{
    std::string sig = (doc.contains("fonts") ? doc["fonts"].dump() : "[]") + "|" + project_dir;
    if (sig == fonts_sig_) return false;
    fonts_sig_ = sig;
    font_error.clear();

    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    for (auto& [id, f] : fonts_) if (f) atlas->RemoveFont(f);
    fonts_.clear();
    font_sizes_.clear();
    default_font_ = nullptr;
    default_size_ = 0.0f;

    const std::string icon_path = IconFontPath();
    const bool have_icons = FileExists(icon_path);
    if (!doc.contains("fonts") || !doc["fonts"].is_array()) return true;
    for (auto& f : doc["fonts"]) {
        if (!f.is_object()) continue;
        std::string id = f.value("id", "");
        std::string path = Resolve(f.value("path", ""));
        float size = f.contains("size") && f["size"].is_number() ? f["size"].get<float>() : 16.0f;
        if (id.empty()) continue;
        ImFontConfig cfg;
        snprintf(cfg.Name, sizeof(cfg.Name), "menu:%s", id.c_str());
        ImFont* font = nullptr;
        if (FileExists(path)) font = atlas->AddFontFromFileTTF(path.c_str(), size, &cfg);
        else {
            font_error += "Font '" + id + "': file not found: " + f.value("path", "") + "\n";
            font = atlas->AddFontDefaultVector(&cfg);
        }
        if (!font) { font_error += "Font '" + id + "' failed to load\n"; continue; }
        if (have_icons) {
            ImFontConfig ic;
            ic.MergeMode = true;
            ic.GlyphMinAdvanceX = size;
            ic.PixelSnapH = true;
            atlas->AddFontFromFileTTF(icon_path.c_str(), size * 0.9f, &ic);
        }
        fonts_[id] = font;
        font_sizes_[id] = size;
        if (!default_font_) { default_font_ = font; default_size_ = size; }
    }
    return true;
}

ImFont* Assets::Font(const std::string& id) const
{
    auto it = fonts_.find(id);
    return it == fonts_.end() ? nullptr : it->second;
}

float Assets::FontSize(const std::string& id) const
{
    auto it = font_sizes_.find(id);
    return it == font_sizes_.end() ? 0.0f : it->second;
}

// ---------------------------------------------------------------- images

static bool Upload(Texture& t)
{
    int w, h, n;
    unsigned char* px = stbi_load(t.path.c_str(), &w, &h, &n, 4);
    if (!px) { t.error = stbi_failure_reason() ? stbi_failure_reason() : "load failed"; return false; }
    if (!t.gl) glGenTextures(1, &t.gl);
    glBindTexture(GL_TEXTURE_2D, t.gl);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);
    t.w = w; t.h = h; t.error.clear();
    return true;
}

bool LoadTexture(Texture& t) { return Upload(t); }
void FreeTexture(Texture& t)
{
    if (t.gl) glDeleteTextures(1, &t.gl);
    t.gl = 0;
}

void Assets::SyncImages(const json& doc)
{
    std::map<std::string, Texture> next;
    if (doc.contains("images") && doc["images"].is_array()) {
        for (auto& im : doc["images"]) {
            if (!im.is_object()) continue;
            std::string id = im.value("id", "");
            if (id.empty()) continue;
            std::string path = Resolve(im.value("path", ""));
            Texture t;
            auto it = images_.find(id);
            if (it != images_.end()) { t = it->second; images_.erase(it); }
            long long mt = FileMTime(path);
            if (t.path != path || t.mtime != mt) {
                t.path = path;
                t.mtime = mt;
                if (mt == 0) t.error = "file not found";
                else Upload(t);
            }
            next[id] = t;
        }
    }
    for (auto& [id, t] : images_) if (t.gl) glDeleteTextures(1, &t.gl);
    images_ = std::move(next);
}

const Texture* Assets::Image(const std::string& id) const
{
    auto it = images_.find(id);
    return it == images_.end() ? nullptr : &it->second;
}

ImTextureID Assets::TexId(const std::string& id) const
{
    const Texture* t = Image(id);
    return t && t->gl && t->error.empty() ? (ImTextureID)(intptr_t)t->gl : ImTextureID_Invalid;
}
