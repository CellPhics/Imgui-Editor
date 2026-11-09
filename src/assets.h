// Fonts, images and Font Awesome icons for the menu being edited.
#pragma once
#include "imgui.h"
#include "model.h"
#include <map>
#include <string>
#include <vector>

// ---- icons
// "crosshairs" -> UTF-8 string of the glyph, "" if we don't know it.
const char* IconUtf8(const std::string& name);
struct IconEntry {
    const char* name;
    unsigned codepoint;
};
const std::vector<IconEntry>& AllIcons();

// ---- textures
struct Texture {
    unsigned gl = 0;
    int w = 0, h = 0;
    std::string path;
    long long mtime = 0;
    std::string error;
    ImTextureID id() const { return gl && error.empty() ? (ImTextureID)(intptr_t)gl : ImTextureID_Invalid; }
};
bool LoadTexture(Texture& t); // loads t.path into t.gl (creates or updates it)
void FreeTexture(Texture& t);

class Assets {
public:
    std::string data_dir;    // the editor's own data (bundled fonts, icon font)
    std::string project_dir; // the menu project we're editing

    // Rebuilds the menu fonts if the doc's font list changed. Has to be called
    // between frames (before NewFrame), ImGui doesn't like fonts changing mid-frame.
    void SyncFonts(const json& doc);
    ImFont* Font(const std::string& id) const;
    float FontSize(const std::string& id) const;
    ImFont* DefaultFont() const { return default_font_; }
    float DefaultFontSize() const { return default_size_; }
    std::string font_error;

    // (Re)loads images from the doc. Only touches files whose mtime changed.
    void SyncImages(const json& doc);
    const Texture* Image(const std::string& id) const;
    ImTextureID TexId(const std::string& id) const;

    // "assets/x.png" -> absolute path (checks the project first, then data_dir).
    std::string Resolve(const std::string& path) const;
    std::string IconFontPath() const;
    std::vector<std::string> ListFiles(const std::string& subdir, std::initializer_list<const char*> exts) const;

private:
    std::string fonts_signature_;
    std::map<std::string, ImFont*> fonts_;
    std::map<std::string, float> font_sizes_;
    ImFont* default_font_ = nullptr;
    float default_size_ = 0.0f;
    std::map<std::string, Texture> images_;
};
