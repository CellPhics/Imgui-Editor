#include "theme.h"
#include <cmath>
#include <cstddef>

// ---------------------------------------------------------------- field tables

#define STYLE_VAR(field, floats, min, max) {#field, offsetof(ImGuiStyle, field), floats, min, max}
const std::vector<StyleVarInfo>& StyleVars()
{
    static const std::vector<StyleVarInfo> vars = {
        STYLE_VAR(Alpha, 1, 0.1f, 1.0f),
        STYLE_VAR(WindowPadding, 2, 0, 40),
        STYLE_VAR(WindowRounding, 1, 0, 30),
        STYLE_VAR(WindowBorderSize, 1, 0, 4),
        STYLE_VAR(WindowTitleAlign, 2, 0, 1),
        STYLE_VAR(ChildRounding, 1, 0, 30),
        STYLE_VAR(ChildBorderSize, 1, 0, 4),
        STYLE_VAR(PopupRounding, 1, 0, 30),
        STYLE_VAR(PopupBorderSize, 1, 0, 4),
        STYLE_VAR(FramePadding, 2, 0, 30),
        STYLE_VAR(FrameRounding, 1, 0, 20),
        STYLE_VAR(FrameBorderSize, 1, 0, 4),
        STYLE_VAR(ItemSpacing, 2, 0, 30),
        STYLE_VAR(ItemInnerSpacing, 2, 0, 30),
        STYLE_VAR(CellPadding, 2, 0, 30),
        STYLE_VAR(IndentSpacing, 1, 0, 40),
        STYLE_VAR(ScrollbarSize, 1, 2, 30),
        STYLE_VAR(ScrollbarRounding, 1, 0, 20),
        STYLE_VAR(GrabMinSize, 1, 2, 30),
        STYLE_VAR(GrabRounding, 1, 0, 20),
        STYLE_VAR(TabRounding, 1, 0, 20),
        STYLE_VAR(TabBorderSize, 1, 0, 4),
        STYLE_VAR(ButtonTextAlign, 2, 0, 1),
        STYLE_VAR(SelectableTextAlign, 2, 0, 1),
        STYLE_VAR(SeparatorTextBorderSize, 1, 0, 10),
        STYLE_VAR(SeparatorTextAlign, 2, 0, 1),
        STYLE_VAR(SeparatorTextPadding, 2, 0, 40),
    };
    return vars;
}
#undef STYLE_VAR

#define THEME_FIELD(field, kind, min, max) {#field, FieldKind::kind, offsetof(mm::Theme, field), min, max}
const std::vector<ThemeFieldInfo>& ThemeFields()
{
    static const std::vector<ThemeFieldInfo> fields = {
        THEME_FIELD(accent, Color, 0, 0),
        THEME_FIELD(sidebar_bg, Color, 0, 0),
        THEME_FIELD(toggle_off, Color, 0, 0),
        THEME_FIELD(text_dim, Color, 0, 0),
        THEME_FIELD(group_title, Color, 0, 0),
        THEME_FIELD(anim_speed, Float, 1, 40),
        THEME_FIELD(animations, Bool, 0, 0),
        THEME_FIELD(tab_height, Float, 20, 80),
        THEME_FIELD(tab_rounding, Float, 0, 30),
        THEME_FIELD(toggle_right, Bool, 0, 0),
        THEME_FIELD(labels_on_top, Bool, 0, 0),
    };
    return fields;
}
#undef THEME_FIELD

// ---------------------------------------------------------------- json <-> style

static float Round3(float v) { return std::round(v * 1000.0f) / 1000.0f; }

void ApplyStyleOverride(const json& o, ImGuiStyle& style, mm::Theme& mt)
{
    if (!o.is_object()) return;
    try {
        if (o.contains("vars") && o["vars"].is_object()) {
            const json& vars = o["vars"];
            for (auto& v : StyleVars()) {
                if (!vars.contains(v.name)) continue;
                const json& j = vars[v.name];
                float* f = (float*)((char*)&style + v.offset);
                if (v.floats == 1 && j.is_number()) f[0] = j.get<float>();
                else if (v.floats == 2 && j.is_array() && j.size() == 2) { f[0] = j[0].get<float>(); f[1] = j[1].get<float>(); }
            }
        }
        if (o.contains("colors") && o["colors"].is_object()) {
            for (int i = 0; i < ImGuiCol_COUNT; i++) {
                const char* name = ImGui::GetStyleColorName(i);
                if (o["colors"].contains(name)) ParseColor(o["colors"][name], style.Colors[i]);
            }
        }
        if (o.contains("mm") && o["mm"].is_object()) {
            const json& m = o["mm"];
            for (auto& f : ThemeFields()) {
                if (!m.contains(f.name)) continue;
                char* p = (char*)&mt + f.offset;
                const json& j = m[f.name];
                if (f.kind == FieldKind::Color) ParseColor(j, *(ImVec4*)p);
                else if (f.kind == FieldKind::Float && j.is_number()) *(float*)p = j.get<float>();
                else if (f.kind == FieldKind::Bool && j.is_boolean()) *(bool*)p = j.get<bool>();
            }
        }
    } catch (...) {
        // one bad value shouldn't kill the whole theme, keep what we've got
    }
}

void ThemeFromJson(const json& theme, ImGuiStyle& style, mm::Theme& mt)
{
    style = ImGuiStyle();
    ImGui::StyleColorsDark(&style);
    mt = mm::Theme();
    ApplyStyleOverride(theme, style, mt);
}

json ThemeToJson(const ImGuiStyle& style, const mm::Theme& mt)
{
    json mm_json = json::object();
    for (auto& f : ThemeFields()) {
        const char* p = (const char*)&mt + f.offset;
        if (f.kind == FieldKind::Color) mm_json[f.name] = ColorToJson(*(const ImVec4*)p);
        else if (f.kind == FieldKind::Float) mm_json[f.name] = Round3(*(const float*)p);
        else mm_json[f.name] = *(const bool*)p;
    }
    json vars = json::object();
    for (auto& v : StyleVars()) {
        const float* f = (const float*)((const char*)&style + v.offset);
        vars[v.name] = v.floats == 1 ? json(Round3(f[0])) : json::array({Round3(f[0]), Round3(f[1])});
    }
    json colors = json::object();
    for (int i = 0; i < ImGuiCol_COUNT; i++) colors[ImGui::GetStyleColorName(i)] = ColorToJson(style.Colors[i]);

    return json{{"mm", mm_json}, {"vars", vars}, {"colors", colors}};
}

// ---------------------------------------------------------------- presets

static ImVec4 Mix(ImVec4 a, ImVec4 b, float t) { return mm::LerpColor(a, b, t); }
static ImVec4 WithAlpha(ImVec4 c, float a) { c.w = a; return c; }

// Every preset is generated from an accent + background so they all feel
// consistent. Tweaking the numbers here changes all of them at once.
json ThemeFromAccent(ImVec4 accent, ImVec4 bg, bool light)
{
    ImGuiStyle s;
    mm::Theme mt;
    if (light) ImGui::StyleColorsLight(&s);
    else ImGui::StyleColorsDark(&s);

    const ImVec4 white(1, 1, 1, 1), black(0, 0, 0, 1);
    const ImVec4 brighter = light ? black : white; // which way "more contrast" goes
    const ImVec4 panel = Mix(bg, brighter, 0.035f);
    const ImVec4 frame = Mix(bg, brighter, 0.09f);
    const ImVec4 text = light ? ImVec4(0.10f, 0.11f, 0.14f, 1) : ImVec4(0.93f, 0.93f, 0.96f, 1);

    s.WindowPadding = ImVec2(14, 14);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 8);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.WindowRounding = 10;
    s.ChildRounding = 8;
    s.FrameRounding = 6;
    s.PopupRounding = 6;
    s.ScrollbarRounding = 6;
    s.GrabRounding = 6;
    s.TabRounding = 6;
    s.WindowBorderSize = 1;
    s.ChildBorderSize = 1;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.GrabMinSize = 10;
    s.ScrollbarSize = 10;
    s.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    s.SeparatorTextBorderSize = 2;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = Mix(text, bg, 0.55f);
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = panel;
    c[ImGuiCol_PopupBg] = Mix(bg, brighter, 0.05f);
    c[ImGuiCol_Border] = Mix(bg, brighter, 0.14f);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = Mix(frame, accent, 0.18f);
    c[ImGuiCol_FrameBgActive] = Mix(frame, accent, 0.30f);
    c[ImGuiCol_TitleBg] = bg;
    c[ImGuiCol_TitleBgActive] = Mix(bg, accent, 0.15f);
    c[ImGuiCol_TitleBgCollapsed] = bg;
    c[ImGuiCol_MenuBarBg] = panel;
    c[ImGuiCol_ScrollbarBg] = WithAlpha(bg, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = Mix(frame, brighter, 0.08f);
    c[ImGuiCol_ScrollbarGrabHovered] = Mix(frame, accent, 0.5f);
    c[ImGuiCol_ScrollbarGrabActive] = accent;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = Mix(accent, white, 0.25f);
    c[ImGuiCol_Button] = frame;
    c[ImGuiCol_ButtonHovered] = Mix(frame, accent, 0.45f);
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_Header] = WithAlpha(accent, 0.30f);
    c[ImGuiCol_HeaderHovered] = WithAlpha(accent, 0.45f);
    c[ImGuiCol_HeaderActive] = WithAlpha(accent, 0.65f);
    c[ImGuiCol_Separator] = Mix(bg, brighter, 0.12f);
    c[ImGuiCol_SeparatorHovered] = WithAlpha(accent, 0.7f);
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = WithAlpha(accent, 0.15f);
    c[ImGuiCol_ResizeGripHovered] = WithAlpha(accent, 0.6f);
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = frame;
    c[ImGuiCol_TabHovered] = Mix(frame, accent, 0.5f);
    c[ImGuiCol_TabSelected] = Mix(frame, accent, 0.7f);
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_PlotLines] = accent;
    c[ImGuiCol_TextSelectedBg] = WithAlpha(accent, 0.35f);
    c[ImGuiCol_NavCursor] = accent;

    mt.accent = accent;
    mt.sidebar_bg = Mix(bg, light ? white : black, 0.25f);
    mt.toggle_off = Mix(frame, brighter, 0.06f);
    mt.text_dim = Mix(text, bg, 0.45f);
    mt.group_title = Mix(text, bg, 0.12f);
    return ThemeToJson(s, mt);
}

std::vector<std::string> ThemePresetNames()
{
    return {"Midnight", "Crimson", "Cyber", "Emerald", "Gold", "Sakura", "Frost (light)"};
}

json ThemePreset(const std::string& name)
{
    if (name == "Crimson") return ThemeFromAccent(ImVec4(0.92f, 0.22f, 0.32f, 1), ImVec4(0.075f, 0.065f, 0.075f, 1), false);
    if (name == "Cyber") return ThemeFromAccent(ImVec4(0.00f, 0.84f, 0.95f, 1), ImVec4(0.045f, 0.065f, 0.095f, 1), false);
    if (name == "Emerald") return ThemeFromAccent(ImVec4(0.18f, 0.85f, 0.52f, 1), ImVec4(0.065f, 0.085f, 0.075f, 1), false);
    if (name == "Gold") return ThemeFromAccent(ImVec4(0.96f, 0.74f, 0.25f, 1), ImVec4(0.085f, 0.078f, 0.065f, 1), false);
    if (name == "Sakura") return ThemeFromAccent(ImVec4(1.00f, 0.50f, 0.72f, 1), ImVec4(0.095f, 0.075f, 0.09f, 1), false);
    if (name == "Frost (light)") return ThemeFromAccent(ImVec4(0.22f, 0.48f, 0.98f, 1), ImVec4(0.95f, 0.96f, 0.98f, 1), true);
    return ThemeFromAccent(ImVec4(0.55f, 0.36f, 0.96f, 1), ImVec4(0.085f, 0.085f, 0.105f, 1), false); // Midnight
}
