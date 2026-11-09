// Theme <-> JSON. A theme is { "colors": {ImGuiCol name: [r,g,b,a]},
// "vars": {style var name: number | [x,y]}, "mm": {accent, sidebar_bg, ...} }.
// Missing entries fall back to Dear ImGui's dark style / mm defaults.
#pragma once
#include "model.h"
#include "imgui.h"
#include "mm_widgets.h"

struct StyleVarInfo {
    const char* name;
    size_t offset;
    int count; // 1 = float, 2 = ImVec2
    float vmin, vmax;
};
const std::vector<StyleVarInfo>& StyleVars();

struct MmFieldInfo {
    const char* name;
    int kind; // 0 color, 1 float, 2 bool
    size_t offset;
    float vmin, vmax;
};
const std::vector<MmFieldInfo>& MmFields();

// Accepts [r,g,b(,a)] floats 0..1 or "#RRGGBB[AA]".
bool ParseColor(const json& j, ImVec4& out);
// Applies a partial {"colors","vars","mm"} object on top of existing values
// (used for themes and for per-widget "style" overrides).
void ApplyStyleOverride(const json& o, ImGuiStyle& style, mm::Theme& mt);
void ThemeFromJson(const json& theme, ImGuiStyle& style, mm::Theme& mt);
json ThemeToJson(const ImGuiStyle& style, const mm::Theme& mt);

// Builds a complete, consistent theme from an accent and a background color.
json ThemeFromAccent(ImVec4 accent, ImVec4 bg, bool light);
std::vector<std::string> ThemePresetNames();
json ThemePreset(const std::string& name);
