#include "theme.h"
#include <cmath>
#include <cstddef>

#define SV(n, cnt, lo, hi) {#n, offsetof(ImGuiStyle, n), cnt, lo, hi}
const std::vector<StyleVarInfo>& StyleVars()
{
    static std::vector<StyleVarInfo> v = {
        SV(Alpha, 1, 0.1f, 1.0f),
        SV(WindowPadding, 2, 0, 40),
        SV(WindowRounding, 1, 0, 30),
        SV(WindowBorderSize, 1, 0, 4),
        SV(WindowTitleAlign, 2, 0, 1),
        SV(ChildRounding, 1, 0, 30),
        SV(ChildBorderSize, 1, 0, 4),
        SV(PopupRounding, 1, 0, 30),
        SV(PopupBorderSize, 1, 0, 4),
        SV(FramePadding, 2, 0, 30),
        SV(FrameRounding, 1, 0, 20),
        SV(FrameBorderSize, 1, 0, 4),
        SV(ItemSpacing, 2, 0, 30),
        SV(ItemInnerSpacing, 2, 0, 30),
        SV(CellPadding, 2, 0, 30),
        SV(IndentSpacing, 1, 0, 40),
        SV(ScrollbarSize, 1, 2, 30),
        SV(ScrollbarRounding, 1, 0, 20),
        SV(GrabMinSize, 1, 2, 30),
        SV(GrabRounding, 1, 0, 20),
        SV(TabRounding, 1, 0, 20),
        SV(TabBorderSize, 1, 0, 4),
        SV(ButtonTextAlign, 2, 0, 1),
        SV(SelectableTextAlign, 2, 0, 1),
        SV(SeparatorTextBorderSize, 1, 0, 10),
        SV(SeparatorTextAlign, 2, 0, 1),
        SV(SeparatorTextPadding, 2, 0, 40),
    };
    return v;
}
#undef SV

#define MF(n, k, lo, hi) {#n, k, offsetof(mm::Theme, n), lo, hi}
const std::vector<MmFieldInfo>& MmFields()
{
    static std::vector<MmFieldInfo> v = {
        MF(accent, 0, 0, 0),
        MF(sidebar_bg, 0, 0, 0),
        MF(toggle_off, 0, 0, 0),
        MF(text_dim, 0, 0, 0),
        MF(group_title, 0, 0, 0),
        MF(anim_speed, 1, 1, 40),
        MF(animations, 2, 0, 0),
        MF(tab_height, 1, 20, 80),
        MF(tab_rounding, 1, 0, 30),
        MF(toggle_right, 2, 0, 0),
        MF(labels_on_top, 2, 0, 0),
    };
    return v;
}
#undef MF

static float R3(float v) { return std::round(v * 1000.0f) / 1000.0f; }

bool ParseColor(const json& j, ImVec4& out)
{
    try {
    if (j.is_array() && j.size() >= 3) {
        ImVec4 c(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j.size() > 3 ? j[3].get<float>() : 1.0f);
        // Accept 0..255 values too.
        if (c.x > 1.0f || c.y > 1.0f || c.z > 1.0f) { c.x /= 255.0f; c.y /= 255.0f; c.z /= 255.0f; if (c.w > 1.0f) c.w /= 255.0f; }
        out = c;
        return true;
    }
    if (j.is_string()) { // "#RRGGBB" or "#RRGGBBAA"
        std::string s = j.get<std::string>();
        if (!s.empty() && s[0] == '#') s = s.substr(1);
        if (s.size() != 6 && s.size() != 8) return false;
        unsigned v = std::stoul(s, nullptr, 16);
        if (s.size() == 6) v = (v << 8) | 0xFF;
        out = ImVec4(((v >> 24) & 255) / 255.0f, ((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f);
        return true;
    }
    } catch (...) {}
    return false;
}

void ApplyStyleOverride(const json& o, ImGuiStyle& style, mm::Theme& mt)
{
    if (!o.is_object()) return;
    try {
        if (o.contains("vars") && o["vars"].is_object()) {
            for (auto& v : StyleVars()) {
                if (!o["vars"].contains(v.name)) continue;
                const json& j = o["vars"][v.name];
                float* f = (float*)((char*)&style + v.offset);
                if (v.count == 1 && j.is_number()) *f = j.get<float>();
                else if (v.count == 2 && j.is_array() && j.size() == 2) { f[0] = j[0].get<float>(); f[1] = j[1].get<float>(); }
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
            for (auto& f : MmFields()) {
                if (!m.contains(f.name)) continue;
                char* p = (char*)&mt + f.offset;
                if (f.kind == 0) ParseColor(m[f.name], *(ImVec4*)p);
                else if (f.kind == 1 && m[f.name].is_number()) *(float*)p = m[f.name].get<float>();
                else if (f.kind == 2 && m[f.name].is_boolean()) *(bool*)p = m[f.name].get<bool>();
            }
        }
    } catch (...) {
        // A malformed value just leaves the remaining values in place.
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
    json t = json::object();
    json vars = json::object();
    for (auto& v : StyleVars()) {
        const float* f = (const float*)((const char*)&style + v.offset);
        if (v.count == 1) vars[v.name] = R3(f[0]);
        else vars[v.name] = json::array({R3(f[0]), R3(f[1])});
    }
    json cols = json::object();
    for (int i = 0; i < ImGuiCol_COUNT; i++) {
        const ImVec4& c = style.Colors[i];
        cols[ImGui::GetStyleColorName(i)] = json::array({R3(c.x), R3(c.y), R3(c.z), R3(c.w)});
    }
    json m = json::object();
    for (auto& f : MmFields()) {
        const char* p = (const char*)&mt + f.offset;
        if (f.kind == 0) { const ImVec4& c = *(const ImVec4*)p; m[f.name] = json::array({R3(c.x), R3(c.y), R3(c.z), R3(c.w)}); }
        else if (f.kind == 1) m[f.name] = R3(*(const float*)p);
        else m[f.name] = *(const bool*)p;
    }
    t["mm"] = m;
    t["vars"] = vars;
    t["colors"] = cols;
    return t;
}

// ---------------------------------------------------------------- presets

static ImVec4 Mix(ImVec4 a, ImVec4 b, float t) { return mm::LerpColor(a, b, t); }
static ImVec4 A(ImVec4 c, float a) { c.w = a; return c; }

static json Build(ImVec4 accent, ImVec4 bg, bool light = false)
{
    ImGuiStyle s;
    mm::Theme mt;
    if (light) ImGui::StyleColorsLight(&s); else ImGui::StyleColorsDark(&s);
    const ImVec4 white(1, 1, 1, 1), black(0, 0, 0, 1);
    const ImVec4 up = light ? black : white;          // direction of "brighter"
    const ImVec4 panel = Mix(bg, up, 0.035f);
    const ImVec4 frame = Mix(bg, up, 0.09f);
    const ImVec4 text = light ? ImVec4(0.10f, 0.11f, 0.14f, 1) : ImVec4(0.93f, 0.93f, 0.96f, 1);

    s.WindowPadding = ImVec2(14, 14);
    s.WindowRounding = 10; s.ChildRounding = 8; s.FrameRounding = 6; s.PopupRounding = 6;
    s.ScrollbarRounding = 6; s.GrabRounding = 6; s.TabRounding = 6;
    s.WindowBorderSize = 1; s.ChildBorderSize = 1; s.FrameBorderSize = 0; s.PopupBorderSize = 1;
    s.FramePadding = ImVec2(10, 6); s.ItemSpacing = ImVec2(10, 8); s.ItemInnerSpacing = ImVec2(8, 6);
    s.GrabMinSize = 10; s.ScrollbarSize = 10; s.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    s.SeparatorTextBorderSize = 2;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = Mix(text, bg, 0.55f);
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = panel;
    c[ImGuiCol_PopupBg] = Mix(bg, up, 0.05f);
    c[ImGuiCol_Border] = A(Mix(bg, up, 0.14f), 1.0f);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = Mix(frame, accent, 0.18f);
    c[ImGuiCol_FrameBgActive] = Mix(frame, accent, 0.30f);
    c[ImGuiCol_TitleBg] = bg;
    c[ImGuiCol_TitleBgActive] = Mix(bg, accent, 0.15f);
    c[ImGuiCol_TitleBgCollapsed] = bg;
    c[ImGuiCol_MenuBarBg] = panel;
    c[ImGuiCol_ScrollbarBg] = A(bg, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = Mix(frame, up, 0.08f);
    c[ImGuiCol_ScrollbarGrabHovered] = Mix(frame, accent, 0.5f);
    c[ImGuiCol_ScrollbarGrabActive] = accent;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = Mix(accent, white, 0.25f);
    c[ImGuiCol_Button] = frame;
    c[ImGuiCol_ButtonHovered] = Mix(frame, accent, 0.45f);
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_Header] = A(accent, 0.30f);
    c[ImGuiCol_HeaderHovered] = A(accent, 0.45f);
    c[ImGuiCol_HeaderActive] = A(accent, 0.65f);
    c[ImGuiCol_Separator] = Mix(bg, up, 0.12f);
    c[ImGuiCol_SeparatorHovered] = A(accent, 0.7f);
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = A(accent, 0.15f);
    c[ImGuiCol_ResizeGripHovered] = A(accent, 0.6f);
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = frame;
    c[ImGuiCol_TabHovered] = Mix(frame, accent, 0.5f);
    c[ImGuiCol_TabSelected] = Mix(frame, accent, 0.7f);
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_PlotLines] = accent;
    c[ImGuiCol_TextSelectedBg] = A(accent, 0.35f);
    c[ImGuiCol_NavCursor] = accent;

    mt.accent = accent;
    mt.sidebar_bg = Mix(bg, light ? white : black, 0.25f);
    mt.toggle_off = Mix(frame, up, 0.06f);
    mt.text_dim = Mix(text, bg, 0.45f);
    mt.group_title = Mix(text, bg, 0.12f);
    return ThemeToJson(s, mt);
}

json ThemeFromAccent(ImVec4 accent, ImVec4 bg, bool light) { return Build(accent, bg, light); }

std::vector<std::string> ThemePresetNames()
{
    return {"Midnight", "Crimson", "Cyber", "Emerald", "Gold", "Sakura", "Frost (light)"};
}

json ThemePreset(const std::string& name)
{
    if (name == "Crimson") return Build(ImVec4(0.92f, 0.22f, 0.32f, 1), ImVec4(0.075f, 0.065f, 0.075f, 1));
    if (name == "Cyber")   return Build(ImVec4(0.00f, 0.84f, 0.95f, 1), ImVec4(0.045f, 0.065f, 0.095f, 1));
    if (name == "Emerald") return Build(ImVec4(0.18f, 0.85f, 0.52f, 1), ImVec4(0.065f, 0.085f, 0.075f, 1));
    if (name == "Gold")    return Build(ImVec4(0.96f, 0.74f, 0.25f, 1), ImVec4(0.085f, 0.078f, 0.065f, 1));
    if (name == "Sakura")  return Build(ImVec4(1.00f, 0.50f, 0.72f, 1), ImVec4(0.095f, 0.075f, 0.09f, 1));
    if (name == "Frost (light)") return Build(ImVec4(0.22f, 0.48f, 0.98f, 1), ImVec4(0.95f, 0.96f, 0.98f, 1), true);
    return Build(ImVec4(0.55f, 0.36f, 0.96f, 1), ImVec4(0.085f, 0.085f, 0.105f, 1)); // Midnight
}
