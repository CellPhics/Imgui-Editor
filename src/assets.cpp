#include "assets.h"
#include "util.h"
#include <GL/gl.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <unordered_map>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

namespace fs = std::filesystem;

// ---------------------------------------------------------------- icons

// Generated from IconsFontAwesome6.h (name -> codepoint).
static const IconEntry kIcons[] = {
#include "icons_map.inc"
};

const std::vector<IconEntry>& AllIcons()
{
    static const std::vector<IconEntry> icons(std::begin(kIcons), std::end(kIcons));
    return icons;
}

const char* IconUtf8(const std::string& name)
{
    static std::unordered_map<std::string, std::string> by_name;
    if (by_name.empty())
        for (auto& e : kIcons) by_name[e.name] = Utf8(e.codepoint);
    if (name.empty()) return "";

    auto it = by_name.find(name);
    if (it == by_name.end()) {
        // people (and Claude) write these a bunch of ways: "fa-gear", "ICON_FA_GEAR", "gear_solid"...
        std::string n = ToLower(name);
        for (const char* prefix : {"icon_fa_", "fa-", "fa_"})
            if (n.rfind(prefix, 0) == 0) n = n.substr(strlen(prefix));
        std::replace(n.begin(), n.end(), '_', '-');
        it = by_name.find(n);
        if (it == by_name.end()) return "";
    }
    return it->second.c_str();
}

// ---------------------------------------------------------------- textures

bool LoadTexture(Texture& t)
{
    int w, h, channels;
    unsigned char* pixels = stbi_load(t.path.c_str(), &w, &h, &channels, 4);
    if (!pixels) {
        t.error = stbi_failure_reason() ? stbi_failure_reason() : "couldn't load image";
        return false;
    }
    if (!t.gl) glGenTextures(1, &t.gl);
    glBindTexture(GL_TEXTURE_2D, t.gl);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    t.w = w;
    t.h = h;
    t.error.clear();
    return true;
}

void FreeTexture(Texture& t)
{
    if (t.gl) glDeleteTextures(1, &t.gl);
    t.gl = 0;
}

// ---------------------------------------------------------------- paths

std::string Assets::Resolve(const std::string& path) const
{
    if (path.empty()) return path;
    const fs::path p(path);
    if (p.is_absolute()) return path;
    std::error_code ec;
    const fs::path in_project = fs::path(project_dir) / p;
    if (fs::exists(in_project, ec)) return in_project.string();
    const fs::path in_data = fs::path(data_dir) / p;
    if (fs::exists(in_data, ec)) return in_data.string();
    return in_project.string();
}

std::string Assets::IconFontPath() const { return (fs::path(data_dir) / "fonts" / "fa-solid-900.ttf").string(); }

std::vector<std::string> Assets::ListFiles(const std::string& subdir, std::initializer_list<const char*> exts) const
{
    std::vector<std::string> files;
    std::error_code ec;
    const fs::path dir = fs::path(project_dir) / subdir;
    if (!fs::is_directory(dir, ec)) return files;
    for (auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (e.is_regular_file() && HasExtension(e.path().string(), exts))
            files.push_back(fs::relative(e.path(), project_dir, ec).string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

// ---------------------------------------------------------------- fonts

void Assets::SyncFonts(const json& doc)
{
    // cheap way to tell if anything font related changed
    const std::string signature = (doc.contains("fonts") ? doc["fonts"].dump() : "[]") + "|" + project_dir;
    if (signature == fonts_signature_) return;
    fonts_signature_ = signature;
    font_error.clear();

    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    for (auto& [id, font] : fonts_)
        if (font) atlas->RemoveFont(font);
    fonts_.clear();
    font_sizes_.clear();
    default_font_ = nullptr;
    default_size_ = 0.0f;

    if (!doc.contains("fonts") || !doc["fonts"].is_array()) return;
    const std::string icon_font = IconFontPath();
    const bool have_icons = FileExists(icon_font);

    for (auto& f : doc["fonts"]) {
        const std::string id = GetStr(f, "id");
        if (id.empty()) continue;
        const std::string path = Resolve(GetStr(f, "path"));
        const float size = GetFloat(f, "size", 16.0f);

        ImFontConfig cfg;
        snprintf(cfg.Name, sizeof(cfg.Name), "menu:%s", id.c_str());
        ImFont* font = nullptr;
        if (FileExists(path)) {
            font = atlas->AddFontFromFileTTF(path.c_str(), size, &cfg);
        } else {
            font_error += "Font '" + id + "': can't find " + GetStr(f, "path") + "\n";
            font = atlas->AddFontDefaultVector(&cfg); // at least show something
        }
        if (!font) {
            font_error += "Font '" + id + "' failed to load\n";
            continue;
        }
        // merge Font Awesome into every font so icons work everywhere
        if (have_icons) {
            ImFontConfig icons;
            icons.MergeMode = true;
            icons.GlyphMinAdvanceX = size;
            icons.PixelSnapH = true;
            atlas->AddFontFromFileTTF(icon_font.c_str(), size * 0.9f, &icons);
        }
        fonts_[id] = font;
        font_sizes_[id] = size;
        if (!default_font_) {
            default_font_ = font;
            default_size_ = size;
        }
    }
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

void Assets::SyncImages(const json& doc)
{
    std::map<std::string, Texture> loaded;
    if (doc.contains("images") && doc["images"].is_array()) {
        for (auto& im : doc["images"]) {
            const std::string id = GetStr(im, "id");
            if (id.empty()) continue;
            const std::string path = Resolve(GetStr(im, "path"));

            // reuse what we already have so we only hit the disk when the file changed
            Texture t;
            auto it = images_.find(id);
            if (it != images_.end()) {
                t = it->second;
                images_.erase(it);
            }
            const long long mtime = FileMTime(path);
            if (t.path != path || t.mtime != mtime) {
                t.path = path;
                t.mtime = mtime;
                if (mtime == 0) t.error = "file not found";
                else LoadTexture(t);
            }
            loaded[id] = t;
        }
    }
    for (auto& [id, t] : images_) FreeTexture(t); // whatever's left got removed from the doc
    images_ = std::move(loaded);
}

const Texture* Assets::Image(const std::string& id) const
{
    auto it = images_.find(id);
    return it == images_.end() ? nullptr : &it->second;
}

ImTextureID Assets::TexId(const std::string& id) const
{
    const Texture* t = Image(id);
    return t ? t->id() : ImTextureID_Invalid;
}
