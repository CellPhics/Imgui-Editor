// Fonts, images and icons used by a menu document.
#pragma once
#include "model.h"
#include "imgui.h"
#include <map>
#include <string>
#include <vector>

// Font Awesome icon lookup by name ("crosshairs" -> UTF-8 string).
const char* IconUtf8(const std::string& name); // "" if unknown
struct IconEntry { const char* name; unsigned codepoint; };
const std::vector<IconEntry>& AllIcons();
std::string Utf8(unsigned codepoint);

struct Texture {
    unsigned gl = 0;
    int w = 0, h = 0;
    std::string path;
    long long mtime = 0;
    std::string error;
};

class Assets {
public:
    std::string data_dir;    // bundled editor data (fonts, icon font)
    std::string project_dir; // current project root

    // Call between frames (before NewFrame). Reloads menu fonts if the
    // document's font list changed. Returns true if fonts were rebuilt.
    bool SyncFonts(const json& doc);
    ImFont* Font(const std::string& id) const; // nullptr = not found
    ImFont* DefaultFont() const { return default_font_; }
    float DefaultFontSize() const { return default_size_; }
    float FontSize(const std::string& id) const;
    std::string font_error;

    // Loads/reloads images listed in the document (lazy, checks mtime).
    void SyncImages(const json& doc);
    const Texture* Image(const std::string& id) const;
    ImTextureID TexId(const std::string& id) const;

    std::string Resolve(const std::string& path) const; // project-relative -> absolute
    std::string IconFontPath() const;
    std::vector<std::string> ListFiles(const std::string& subdir, const std::vector<std::string>& exts) const;

private:
    std::string fonts_sig_;
    std::map<std::string, ImFont*> fonts_;
    std::map<std::string, float> font_sizes_;
    ImFont* default_font_ = nullptr;
    float default_size_ = 0.0f;
    std::map<std::string, Texture> images_;
};

// Loads t.path into an OpenGL texture (creates or updates t.gl).
bool LoadTexture(Texture& t);
void FreeTexture(Texture& t);

long long FileMTime(const std::string& path);
bool FileExists(const std::string& path);
std::string ReadFile(const std::string& path, bool* ok = nullptr);
bool WriteFileAtomic(const std::string& path, const std::string& data);
