// Themes in menu.json look like:
//   { "colors": { "WindowBg": [r,g,b,a], ... },   ImGuiCol_ names without the prefix
//     "vars":   { "FrameRounding": 4, "ItemSpacing": [8,6], ... },   ImGuiStyle field names
//     "mm":     { "accent": [...], "anim_speed": 12, ... } }   our extra widget settings
// Anything missing just falls back to ImGui's dark style / mm::Theme defaults.
#pragma once
#include "imgui.h"
#include "mm_widgets.h"
#include "model.h"

// Table of the ImGuiStyle fields we expose, so the theme editor, the JSON
// loader and the exporter can all loop over the same list.
struct StyleVarInfo {
    const char* name;
    size_t offset;   // offsetof(ImGuiStyle, ...)
    int floats;      // 1 = float, 2 = ImVec2
    float min, max;  // slider range in the theme editor
};
const std::vector<StyleVarInfo>& StyleVars();

// Same idea for mm::Theme.
enum class FieldKind { Color, Float, Bool };
struct ThemeFieldInfo {
    const char* name;
    FieldKind kind;
    size_t offset; // offsetof(mm::Theme, ...)
    float min, max;
};
const std::vector<ThemeFieldInfo>& ThemeFields();

// Applies a partial {"colors","vars","mm"} object on top of whatever's in
// style/mt. Used for whole themes and for per-widget "style" overrides.
void ApplyStyleOverride(const json& overrides, ImGuiStyle& style, mm::Theme& mt);
void ThemeFromJson(const json& theme, ImGuiStyle& style, mm::Theme& mt);
json ThemeToJson(const ImGuiStyle& style, const mm::Theme& mt);

// Builds a full, consistent theme from just an accent and a background.
json ThemeFromAccent(ImVec4 accent, ImVec4 bg, bool light);
std::vector<std::string> ThemePresetNames();
json ThemePreset(const std::string& name);
