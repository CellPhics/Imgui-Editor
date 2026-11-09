#include "exporter.h"
#include "assets.h"
#include "renderer.h"
#include "theme.h"
#include "util.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------- C++ literal helpers

// A C++ string literal. Non-ASCII bytes (like the Font Awesome glyphs) become
// \x escapes, and we split the literal after each one so the next character
// can't accidentally get eaten as more hex digits.
std::string CppString(const std::string& s)
{
    std::string out = "\"";
    bool last_was_hex = false;
    for (unsigned char c : s) {
        if (last_was_hex && std::isxdigit(c)) out += "\"\"";
        last_was_hex = false;
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (c < 0x20 || c >= 0x7F) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\x%02x", c);
                out += buf;
                last_was_hex = true;
            } else {
                out += (char)c;
            }
        }
    }
    return out + "\"";
}

std::string Float(float v)
{
    char buf[32];
    if (std::fabs(v - std::round(v)) < 1e-6f) snprintf(buf, sizeof(buf), "%.1ff", v);
    else snprintf(buf, sizeof(buf), "%.4gf", v);
    std::string s = buf;
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos) s.insert(s.size() - 1, ".0");
    return s;
}

std::string Vec2(ImVec2 v) { return "ImVec2(" + Float(v.x) + ", " + Float(v.y) + ")"; }
std::string Vec4(const ImVec4& c) { return "ImVec4(" + Float(c.x) + ", " + Float(c.y) + ", " + Float(c.z) + ", " + Float(c.w) + ")"; }
std::string Bool(bool b) { return b ? "true" : "false"; }

// Turns an id into a valid C++ identifier.
std::string Identifier(const std::string& s)
{
    std::string out;
    for (unsigned char c : s) out += std::isalnum(c) ? (char)c : '_';
    if (out.empty() || std::isdigit((unsigned char)out[0])) out = "v_" + out;
    static const std::set<std::string> reserved = {
        "auto", "bool", "break", "case", "char", "class", "const", "continue", "default", "delete", "do", "double", "else",
        "enum", "float", "for", "if", "int", "long", "new", "private", "public", "return", "short", "signed", "static",
        "struct", "switch", "this", "union", "unsigned", "void", "while",
        "s", "open"}; // "s" is the State param in Render()
    if (reserved.count(out)) out += "_";
    return out;
}

// "icon  label" like mm::IconLabel does at runtime, baked into the literal
std::string IconLabelText(const json& n)
{
    const std::string label = GetStr(n, "label");
    const std::string icon = IconUtf8(GetStr(n, "icon"));
    if (!icon.empty() && !label.empty()) return icon + "  " + label;
    return icon.empty() ? label : icon;
}

const char* const kCheckboxStyles[] = {"mm::CheckboxStyle_Default", "mm::CheckboxStyle_Filled", "mm::CheckboxStyle_Circle"};
const char* const kSliderStyles[] = {"mm::SliderStyle_Default", "mm::SliderStyle_Thin", "mm::SliderStyle_Filled", "mm::SliderStyle_Line"};
const char* const kGroupTitles[] = {"mm::GroupTitle_Inside", "mm::GroupTitle_Border", "mm::GroupTitle_Plain"};
const char* const kTabsStyles[] = {"mm::TabsStyle_Sidebar", "mm::TabsStyle_Top", "mm::TabsStyle_Pills"};
const char* const kTabHighlights[] = {"mm::TabHighlight_Bar", "mm::TabHighlight_Box", "mm::TabHighlight_Fill", "mm::TabHighlight_None"};

// The code that applies a {"colors","vars","mm"} override to the live style.
std::vector<std::string> StyleOverrideCode(const json& o)
{
    std::vector<std::string> lines;
    if (!o.is_object()) return lines;
    if (o.contains("colors") && o["colors"].is_object()) {
        for (int i = 0; i < ImGuiCol_COUNT; i++) {
            const char* name = ImGui::GetStyleColorName(i);
            ImVec4 c;
            if (o["colors"].contains(name) && ParseColor(o["colors"][name], c))
                lines.push_back(std::string("ImGui::GetStyle().Colors[ImGuiCol_") + name + "] = " + Vec4(c) + ";");
        }
    }
    if (o.contains("vars") && o["vars"].is_object()) {
        for (auto& v : StyleVars()) {
            if (!o["vars"].contains(v.name)) continue;
            const json& j = o["vars"][v.name];
            if (v.floats == 1 && j.is_number())
                lines.push_back(std::string("ImGui::GetStyle().") + v.name + " = " + Float(j.get<float>()) + ";");
            else if (v.floats == 2 && j.is_array() && j.size() == 2 && j[0].is_number() && j[1].is_number())
                lines.push_back(std::string("ImGui::GetStyle().") + v.name + " = " + Vec2(ImVec2(j[0].get<float>(), j[1].get<float>())) + ";");
        }
    }
    if (o.contains("mm") && o["mm"].is_object()) {
        for (auto& f : ThemeFields()) {
            if (!o["mm"].contains(f.name)) continue;
            const json& j = o["mm"][f.name];
            const std::string target = std::string("mm::GetTheme().") + f.name + " = ";
            ImVec4 c;
            if (f.kind == FieldKind::Color && ParseColor(j, c)) lines.push_back(target + Vec4(c) + ";");
            else if (f.kind == FieldKind::Float && j.is_number()) lines.push_back(target + Float(j.get<float>()) + ";");
            else if (f.kind == FieldKind::Bool && j.is_boolean()) lines.push_back(target + Bool(j.get<bool>()) + ";");
        }
    }
    return lines;
}

// Exported asset file names, deduplicated (two fonts called Font.ttf from
// different folders would clash otherwise).
std::map<std::string, std::string> ExportFileNames(const json& entries)
{
    std::map<std::string, std::string> names;
    std::set<std::string> used;
    if (!entries.is_array()) return names;
    for (auto& e : entries) {
        const std::string id = GetStr(e, "id");
        if (id.empty()) continue;
        std::string name = fs::path(GetStr(e, "path")).filename().string();
        if (name.empty()) name = id;
        if (used.count(name)) name = Identifier(id) + "_" + name;
        used.insert(name);
        names[id] = name;
    }
    return names;
}

// ---------------------------------------------------------------- code generator

// Walks the menu once to declare the State struct and once to write Render().
// The Render() code mirrors MenuRenderer in renderer.cpp call for call.
class CodeGen {
public:
    CodeGen(const json& doc) : doc_(doc) {}

    std::map<std::string, std::string> font_vars;  // font id -> C++ variable
    std::map<std::string, std::string> image_vars; // image id -> C++ variable
    std::ostringstream state;                      // fields of struct State
    std::ostringstream body;                       // body of Render()

    void DeclareState(const json& nodes)
    {
        if (!nodes.is_array()) return;
        for (auto& n : nodes) {
            if (!n.is_object()) continue;
            const std::string type = GetStr(n, "type");
            const std::string comment = "  // " + GetStr(n, "id");
            if (type == "checkbox" || type == "toggle") {
                state << "    bool " << Var(n) << " = " << Bool(GetBool(n, "default", false)) << ";" << comment << "\n";
            } else if (type == "slider_float") {
                state << "    float " << Var(n) << " = " << Float(GetFloat(n, "default", 0)) << ";" << comment << "\n";
            } else if (type == "progress") {
                state << "    float " << Var(n) << " = " << Float(GetFloat(n, "default", 0.5f)) << ";" << comment << " (0..1)\n";
            } else if (type == "slider_int" || type == "combo" || type == "radio" || type == "tabs") {
                state << "    int " << Var(n) << " = " << GetInt(n, "default", 0) << ";" << comment << (type == "tabs" ? " (selected tab)" : "") << "\n";
            } else if (type == "multi_combo") {
                const size_t count = std::max<size_t>(1, GetItems(n, "items").size());
                state << "    bool " << Var(n) << "[" << count << "] = {";
                for (size_t i = 0; i < count; i++) {
                    const bool on = n.contains("default") && n["default"].is_array() && i < n["default"].size() &&
                                    n["default"][i].is_boolean() && n["default"][i].get<bool>();
                    state << (i ? ", " : "") << Bool(on);
                }
                state << "};" << comment << "\n";
            } else if (type == "color") {
                ImVec4 c(1, 1, 1, 1);
                GetColor(n, "default", c);
                state << "    float " << Var(n) << "[4] = {" << Float(c.x) << ", " << Float(c.y) << ", " << Float(c.z) << ", " << Float(c.w) << "};" << comment << "\n";
            } else if (type == "keybind") {
                const int key = KeyFromName(GetStr(n, "default", "None"));
                state << "    int " << Var(n) << " = " << (key ? "ImGuiKey_" + KeyName(key) : std::string("ImGuiKey_None")) << ";" << comment << " (ImGuiKey)\n";
            } else if (type == "input_text") {
                const int len = std::max(1, GetInt(n, "max_length", 64));
                state << "    char " << Var(n) << "[" << (len + 1) << "] = " << CppString(GetStr(n, "default")) << ";" << comment << "\n";
            } else if (type == "button") {
                state << "    bool " << Var(n) << " = false;" << comment << " (true on the frame it was clicked)\n";
            }
            if (n.contains("children")) DeclareState(n["children"]);
        }
    }

    void Window(const json& w)
    {
        const std::string id = GetStr(w, "id");
        std::vector<std::string> flag_list;
        if (GetBool(w, "no_title_bar", true)) flag_list.push_back("ImGuiWindowFlags_NoTitleBar");
        if (GetBool(w, "no_resize", false)) flag_list.push_back("ImGuiWindowFlags_NoResize");
        if (GetBool(w, "no_move", false)) flag_list.push_back("ImGuiWindowFlags_NoMove");
        if (GetBool(w, "no_collapse", true)) flag_list.push_back("ImGuiWindowFlags_NoCollapse");
        if (GetBool(w, "no_scrollbar", true)) {
            flag_list.push_back("ImGuiWindowFlags_NoScrollbar");
            flag_list.push_back("ImGuiWindowFlags_NoScrollWithMouse");
        }
        if (GetBool(w, "no_background", false)) flag_list.push_back("ImGuiWindowFlags_NoBackground");
        std::string flags;
        for (auto& f : flag_list) flags += (flags.empty() ? "" : " | ") + f;
        if (flags.empty()) flags = "ImGuiWindowFlags_None";

        // window font: the one it asks for, otherwise the first font
        std::string font_id = GetStr(w, "font");
        if (!font_vars.count(font_id) && doc_["fonts"].is_array() && !doc_["fonts"].empty()) font_id = GetStr(doc_["fonts"][0], "id");
        float font_size = 0;
        for (auto& f : doc_["fonts"])
            if (GetStr(f, "id") == font_id) font_size = GetFloat(f, "size", 16);

        Line("// window: " + id);
        Line("if (s.show_" + Identifier(id) + ") {");
        indent_++;
        const bool styled = BeginStyleOverride(w);
        Line("ImGui::SetNextWindowPos(" + Vec2(GetVec2(w, "pos", ImVec2(60, 60))) + ", ImGuiCond_FirstUseEver);");
        Line("ImGui::SetNextWindowSize(" + Vec2(GetVec2(w, "size", ImVec2(720, 460))) + ", ImGuiCond_FirstUseEver);");
        Line("ImGui::PushFont(" + FontVar(font_id) + ", " + Float(font_size) + ");");
        const bool fade = GetBool(w, "fade", true);
        if (fade) Line("mm::PushWindowFade(" + CppString(id) + ");");
        const bool custom_padding = Has(w, "padding");
        if (custom_padding) Line("ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, " + Vec2(GetVec2(w, "padding", ImVec2(0, 0))) + ");");
        Line("const bool visible = ImGui::Begin(" + CppString(GetStr(w, "title", "Menu") + "###mm_" + id) + ", nullptr, " + flags + ");");
        if (custom_padding) Line("ImGui::PopStyleVar();");
        Line("if (visible) {");
        indent_++;
        Children(w);
        indent_--;
        Line("}");
        Line("ImGui::End();");
        if (fade) Line("mm::PopWindowFade();");
        Line("ImGui::PopFont();");
        if (styled) EndStyleOverride();
        indent_--;
        Line("}");
    }

private:
    const json& doc_;
    std::map<std::string, std::string> vars_; // node id -> State field
    std::set<std::string> used_vars_;
    std::vector<std::string> style_scopes_;
    int indent_ = 1;
    int next_scope_ = 0;

    void Line(const std::string& s) { body << std::string(indent_ * 4, ' ') << s << "\n"; }

    std::string Var(const json& n)
    {
        const std::string id = GetStr(n, "id");
        auto it = vars_.find(id);
        if (it != vars_.end()) return it->second;
        const std::string base = Identifier(GetStr(n, "var").empty() ? id : GetStr(n, "var"));
        std::string name = base;
        for (int i = 2; used_vars_.count(name); i++) name = base + "_" + std::to_string(i);
        used_vars_.insert(name);
        vars_[id] = name;
        return name;
    }

    std::string FontVar(const std::string& id) const
    {
        auto it = font_vars.find(id);
        return it == font_vars.end() ? "nullptr" : it->second;
    }
    std::string ImageVar(const std::string& id) const
    {
        auto it = image_vars.find(id);
        return it == image_vars.end() ? "ImTextureID_Invalid" : it->second;
    }

    std::string Label(const json& n) const { return CppString(IconLabelText(n) + "##" + GetStr(n, "id")); }
    // keeps the widget's own width when it has one set
    static std::string LabelAbove(const std::string& label, float width)
    {
        return "mm::LabelAbove(" + label + (width != 0 ? ", false" : "") + ")";
    }

    // Opens a { } block that saves the style, applies the override and restores
    // it at the end. Returns false (and writes nothing) if there's no override.
    bool BeginStyleOverride(const json& n)
    {
        const auto lines = n.contains("style") ? StyleOverrideCode(n["style"]) : std::vector<std::string>();
        if (lines.empty()) return false;
        const std::string suffix = std::to_string(next_scope_++);
        Line("{ // style override: " + GetStr(n, "id"));
        indent_++;
        Line("const ImGuiStyle saved_style_" + suffix + " = ImGui::GetStyle();");
        Line("const mm::Theme saved_theme_" + suffix + " = mm::GetTheme();");
        for (auto& l : lines) Line(l);
        style_scopes_.push_back(suffix);
        return true;
    }

    void EndStyleOverride()
    {
        const std::string suffix = style_scopes_.back();
        style_scopes_.pop_back();
        Line("ImGui::GetStyle() = saved_style_" + suffix + ";");
        Line("mm::GetTheme() = saved_theme_" + suffix + ";");
        indent_--;
        Line("}");
    }

    void Place(const json& n)
    {
        if (Has(n, "pos")) {
            Line("ImGui::SetCursorPos(" + Vec2(GetVec2(n, "pos", ImVec2(0, 0))) + ");");
            return;
        }
        if (GetBool(n, "same_line", false)) Line("ImGui::SameLine();");
        if (Has(n, "offset")) {
            const ImVec2 o = GetVec2(n, "offset", ImVec2(0, 0));
            Line("{ const ImVec2 c = ImGui::GetCursorPos(); ImGui::SetCursorPos(ImVec2(c.x + " + Float(o.x) + ", c.y + " + Float(o.y) + ")); }");
        }
    }

    void Children(const json& n)
    {
        if (!n.contains("children") || !n["children"].is_array()) return;
        for (auto& c : n["children"]) Node(c);
    }

    void Node(const json& n)
    {
        if (!n.is_object() || GetBool(n, "hidden", false)) return;
        const std::string type = GetStr(n, "type");
        const bool styled = BeginStyleOverride(n);
        if (type == "group") Group(n);
        else if (type == "columns") Columns(n);
        else if (type == "tabs") Tabs(n);
        else if (type == "tab") Children(n);
        else Widget(n, type);
        if (styled) EndStyleOverride();
    }

    void Group(const json& n)
    {
        Place(n);
        Line("// group: " + GetStr(n, "id"));
        Line("mm::BeginGroup(" + CppString(GetStr(n, "id")) + ", " + CppString(GetStr(n, "title")) + ", " + Vec2(GetVec2(n, "size", ImVec2(0, 0))) +
             ", " + Bool(GetBool(n, "border", true)) + ", " + kGroupTitles[GroupTitleStyle(n)] + ");");
        indent_++;
        Children(n);
        indent_--;
        Line("mm::EndGroup();");
    }

    void Columns(const json& n)
    {
        const int count = std::clamp(GetInt(n, "count", 2), 1, 16);
        Line("if (ImGui::BeginTable(" + CppString(GetStr(n, "id")) + ", " + std::to_string(count) + ", ImGuiTableFlags_SizingStretchSame)) {");
        indent_++;
        if (n.contains("children") && n["children"].is_array())
            for (auto& c : n["children"]) {
                if (GetBool(c, "hidden", false)) continue;
                Line("ImGui::TableNextColumn();");
                Node(c);
            }
        Line("ImGui::EndTable();");
        indent_--;
        Line("}");
    }

    void Tabs(const json& n)
    {
        const std::string id = GetStr(n, "id");
        std::vector<const json*> tabs;
        if (n.contains("children") && n["children"].is_array())
            for (auto& c : n["children"])
                if (c.is_object() && !GetBool(c, "hidden", false)) tabs.push_back(&c);
        const std::string var = Var(n);

        Line("{ // tabs: " + id);
        indent_++;
        std::string labels = "static const char* const labels[] = {", icons = "static const char* const icons[] = {";
        for (size_t i = 0; i < tabs.size(); i++) {
            labels += (i ? ", " : "") + CppString(GetStr(*tabs[i], "label", "Tab"));
            icons += (i ? ", " : "") + CppString(IconUtf8(GetStr(*tabs[i], "icon")));
        }
        if (tabs.empty()) {
            labels += "\"\"";
            icons += "\"\"";
        }
        Line(labels + "};");
        Line(icons + "};");
        bool any_image = false;
        for (const json* t : tabs) any_image |= image_vars.count(GetStr(*t, "image")) > 0;
        if (any_image) {
            std::string images = "const ImTextureID images[] = {";
            for (size_t i = 0; i < tabs.size(); i++) images += (i ? ", " : "") + ImageVar(GetStr(*tabs[i], "image"));
            Line(images + "};");
        }

        // only write the options that differ from mm::TabsOptions' defaults
        Line("mm::TabsOptions o;");
        Line(std::string("o.style = ") + kTabsStyles[TabsStyle(n)] + ";");
        Line("o.sidebar_width = " + Float(GetFloat(n, "sidebar_width", 170)) + ";");
        if (!GetStr(n, "logo").empty()) {
            Line("o.logo = " + ImageVar(GetStr(n, "logo")) + ";");
            Line("o.logo_size = " + Vec2(GetVec2(n, "logo_size", ImVec2(64, 64))) + ";");
        }
        if (!GetStr(n, "header").empty()) {
            Line("o.header = " + CppString(GetStr(n, "header")) + ";");
            Line("o.header_font = " + FontVar(GetStr(n, "header_font")) + ";");
            Line("o.header_size = " + Float(GetFloat(n, "header_size", 0)) + ";");
        }
        if (TabHighlight(n) != mm::TabHighlight_Bar) Line(std::string("o.highlight = ") + kTabHighlights[TabHighlight(n)] + ";");
        if (GetStr(n, "layout", "row") == "stacked") Line("o.layout = mm::TabLayout_Stacked;");
        if (GetBool(n, "icon_only", false)) Line("o.icon_only = true;");
        if (GetFloat(n, "icon_size", 0) > 0) Line("o.icon_size = " + Float(GetFloat(n, "icon_size", 0)) + ";");
        if (any_image) Line("o.images = images;");
        if (Has(n, "image_size")) Line("o.image_size = " + Vec2(GetVec2(n, "image_size", ImVec2(0, 0))) + ";");
        if (!GetBool(n, "tint_images", true)) Line("o.tint_images = false;");
        if (GetFloat(n, "tab_height", 0) > 0) Line("o.tab_height = " + Float(GetFloat(n, "tab_height", 0)) + ";");
        if (GetFloat(n, "tab_gap", 4) != 4) Line("o.tab_gap = " + Float(GetFloat(n, "tab_gap", 4)) + ";");
        if (GetBool(n, "separators", false)) Line("o.separators = true;");
        if (GetBool(n, "sidebar_border", false)) Line("o.sidebar_border = true;");
        if (Has(n, "sidebar_padding")) Line("o.sidebar_padding = " + Vec2(GetVec2(n, "sidebar_padding", ImVec2(10, 12))) + ";");

        Line("mm::BeginTabs(" + CppString(id) + ", labels, icons, " + std::to_string(tabs.size()) + ", &s." + var + ", o);");
        Line("switch (s." + var + ") {");
        for (size_t i = 0; i < tabs.size(); i++) {
            Line("case " + std::to_string(i) + ": { // " + GetStr(*tabs[i], "label"));
            indent_++;
            Children(*tabs[i]);
            Line("break;");
            indent_--;
            Line("}");
        }
        Line("default: break;");
        Line("}");
        Line("mm::EndTabs();");
        indent_--;
        Line("}");
    }

    void Widget(const json& n, const std::string& type)
    {
        const std::string id = GetStr(n, "id");
        Place(n);
        const bool disabled = GetBool(n, "disabled", false);
        const float width = GetFloat(n, "width", 0);
        const std::string tooltip = GetStr(n, "tooltip");
        if (disabled) Line("ImGui::BeginDisabled();");
        if (width != 0) Line("ImGui::PushItemWidth(" + (width < 0 ? std::string("-FLT_MIN") : Float(width)) + ");");
        if (!tooltip.empty()) Line("ImGui::BeginGroup();"); // so the tooltip covers label + control

        if (type == "text") {
            ImVec4 color;
            const bool has_color = GetColor(n, "color", color);
            const std::string args = CppString(GetStr(n, "text")) + ", " + (has_color ? "&color" : "nullptr") + ", " + FontVar(GetStr(n, "font")) + ", " +
                                     Float(GetFloat(n, "size", 0)) + ", " + std::to_string(TextAlign(n)) + ", " + Bool(GetBool(n, "wrap", false));
            if (has_color) Line("{ const ImVec4 color = " + Vec4(color) + "; mm::TextEx(" + args + "); }");
            else Line("mm::TextEx(" + args + ");");
        } else if (type == "separator") {
            const std::string label = GetStr(n, "label");
            Line(label.empty() ? "ImGui::Separator();" : "ImGui::SeparatorText(" + CppString(label) + ");");
        } else if (type == "spacing") {
            const float h = GetFloat(n, "height", 8);
            Line("ImGui::Dummy(" + Vec2(GetBool(n, "same_line", false) ? ImVec2(h, 0) : ImVec2(0, h)) + ");");
        } else if (type == "button") {
            Line("s." + Var(n) + " = ImGui::Button(" + Label(n) + ", " + Vec2(GetVec2(n, "size", ImVec2(0, 0))) + ");");
        } else if (type == "checkbox") {
            const int look = CheckboxLook(n);
            if (look == mm::CheckboxStyle_Default) Line("ImGui::Checkbox(" + Label(n) + ", &s." + Var(n) + ");");
            else Line("mm::CheckboxEx(" + Label(n) + ", &s." + Var(n) + ", " + kCheckboxStyles[look] + ", " + Float(GetFloat(n, "box_size", 0)) + ");");
        } else if (type == "toggle") {
            Line("mm::Toggle(" + Label(n) + ", &s." + Var(n) + ");");
        } else if (type == "slider_float" || type == "slider_int") {
            Slider(n, type, width);
        } else if (type == "rect") {
            ImVec4 c(0.55f, 0.36f, 0.96f, 1), c2(0, 0, 0, -1);
            GetColor(n, "color", c);
            GetColor(n, "color2", c2);
            Line("mm::Rect(" + Vec2(GetVec2(n, "size", ImVec2(-1, 2))) + ", " + Vec4(c) + ", " + Vec4(c2) + ", " + Bool(GetBool(n, "vertical", false)) +
                 ", " + Float(GetFloat(n, "rounding", 0)) + ");");
        } else if (type == "combo" || type == "multi_combo" || type == "radio") {
            Choice(n, type, width);
        } else if (type == "color") {
            const std::string flags = GetBool(n, "alpha", true) ? "ImGuiColorEditFlags_AlphaBar" : "ImGuiColorEditFlags_NoAlpha";
            if (GetBool(n, "inline", false)) {
                Line("ImGui::AlignTextToFramePadding();");
                Line("ImGui::TextUnformatted(" + CppString(IconLabelText(n)) + ");");
                Line("ImGui::SameLine();");
                Line("{ const float square = ImGui::GetFrameHeight(), avail = ImGui::GetContentRegionAvail().x; if (avail > square) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - square); }");
                Line("ImGui::ColorEdit4(" + CppString("##" + id) + ", s." + Var(n) + ", " + flags + " | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);");
            } else {
                Line("ImGui::ColorEdit4(" + LabelAbove(Label(n), width) + ", s." + Var(n) + ", " + flags + "); mm::LabelAboveEnd();");
            }
        } else if (type == "keybind") {
            Line("mm::Keybind(" + Label(n) + ", &s." + Var(n) + ");");
        } else if (type == "input_text") {
            Line("ImGui::InputTextWithHint(" + LabelAbove(Label(n), width) + ", " + CppString(GetStr(n, "hint")) + ", s." + Var(n) + ", sizeof(s." + Var(n) + ")" +
                 (GetBool(n, "password", false) ? ", ImGuiInputTextFlags_Password" : "") + "); mm::LabelAboveEnd();");
        } else if (type == "image") {
            Image(n);
        } else if (type == "progress") {
            const std::string overlay = GetStr(n, "overlay");
            Line("ImGui::ProgressBar(s." + Var(n) + ", " + Vec2(GetVec2(n, "size", ImVec2(-1, 0))) + ", " + (overlay.empty() ? std::string("nullptr") : CppString(overlay)) + ");");
        } else {
            Line("// unknown widget type '" + type + "' (" + id + ")");
        }

        if (!tooltip.empty()) {
            Line("ImGui::EndGroup();");
            Line("ImGui::SetItemTooltip(\"%s\", " + CppString(tooltip) + ");");
        }
        if (width != 0) Line("ImGui::PopItemWidth();");
        if (disabled) Line("ImGui::EndDisabled();");
    }

    void Slider(const json& n, const std::string& type, float width)
    {
        const int look = SliderLook(n);
        const bool is_float = type == "slider_float";
        const std::string range = is_float ? Float(GetFloat(n, "min", 0)) + ", " + Float(GetFloat(n, "max", 100))
                                           : std::to_string(GetInt(n, "min", 0)) + ", " + std::to_string(GetInt(n, "max", 100));
        const std::string func = is_float ? "mm::SliderFloatEx(" : "mm::SliderIntEx(";
        const std::string args = "&s." + Var(n) + ", " + range + ", " + CppString(GetStr(n, "format", is_float ? "%.1f" : "%d")) + ", " + kSliderStyles[look] +
                                 ", " + Float(GetFloat(n, "track_height", 0)) + ", " + Bool(GetBool(n, "markers", false)) + ")";
        if (look == mm::SliderStyle_Thin) { // draws its own label
            if (width == 0) Line("ImGui::PushItemWidth(-FLT_MIN);");
            Line(func + Label(n) + ", " + args + ";");
            if (width == 0) Line("ImGui::PopItemWidth();");
        } else {
            Line(func + LabelAbove(Label(n), width) + ", " + args + "; mm::LabelAboveEnd();");
        }
    }

    void Choice(const json& n, const std::string& type, float width)
    {
        const auto items = GetItems(n, "items");
        std::string array = "static const char* const items[] = {";
        for (size_t i = 0; i < items.size(); i++) array += (i ? ", " : "") + CppString(items[i]);
        if (items.empty()) array += "\"\"";
        array += "};";
        const std::string count = std::to_string(items.size());

        Line("{");
        indent_++;
        Line(array);
        if (type == "combo") {
            Line("ImGui::Combo(" + LabelAbove(Label(n), width) + ", &s." + Var(n) + ", items, " + count + "); mm::LabelAboveEnd();");
        } else if (type == "multi_combo") {
            Line("mm::MultiCombo(" + LabelAbove(Label(n), width) + ", s." + Var(n) + ", items, " + count + "); mm::LabelAboveEnd();");
        } else {
            if (!GetStr(n, "label").empty()) Line("ImGui::TextUnformatted(" + CppString(IconLabelText(n)) + ");");
            Line("for (int i = 0; i < " + count + "; i++) {");
            indent_++;
            if (GetBool(n, "horizontal", true)) Line("if (i > 0) ImGui::SameLine();");
            Line("ImGui::PushID(i); ImGui::RadioButton(items[i], &s." + Var(n) + ", i); ImGui::PopID();");
            indent_--;
            Line("}");
        }
        indent_--;
        Line("}");
    }

    void Image(const json& n)
    {
        const int align = TextAlign(n);
        const std::string tex = ImageVar(GetStr(n, "image"));
        Line("{");
        indent_++;
        Line("const ImVec2 size = " + Vec2(GetVec2(n, "size", ImVec2(64, 64))) + ";");
        if (align != 0)
            Line(std::string("const float avail = ImGui::GetContentRegionAvail().x; if (avail > size.x) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ") +
                 (align == 1 ? "(avail - size.x) * 0.5f" : "avail - size.x") + ");");
        Line("ImGui::Dummy(size);");
        Line("if (" + tex + " != ImTextureID_Invalid) ImGui::GetWindowDrawList()->AddImageRounded(" + tex +
             ", ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, " + Float(GetFloat(n, "rounding", 0)) + ");");
        indent_--;
        Line("}");
    }
};

// One ApplyTheme_<Name>() function per theme. Writes every value out, so the
// exported theme doesn't depend on ImGui's defaults staying the same.
std::string ThemeFunction(const std::string& name, const json& theme)
{
    ImGuiStyle st;
    mm::Theme mt;
    ThemeFromJson(theme, st, mt);
    std::ostringstream o;
    o << "inline void " << name << "()\n{\n    ImGuiStyle& st = ImGui::GetStyle();\n";
    for (auto& v : StyleVars()) {
        const float* f = (const float*)((const char*)&st + v.offset);
        o << "    st." << v.name << " = " << (v.floats == 1 ? Float(f[0]) : Vec2(ImVec2(f[0], f[1]))) << ";\n";
    }
    for (int i = 0; i < ImGuiCol_COUNT; i++)
        o << "    st.Colors[ImGuiCol_" << ImGui::GetStyleColorName(i) << "] = " << Vec4(st.Colors[i]) << ";\n";
    o << "    mm::Theme& t = mm::GetTheme();\n";
    for (auto& f : ThemeFields()) {
        const char* p = (const char*)&mt + f.offset;
        if (f.kind == FieldKind::Color) o << "    t." << f.name << " = " << Vec4(*(const ImVec4*)p) << ";\n";
        else if (f.kind == FieldKind::Float) o << "    t." << f.name << " = " << Float(*(const float*)p) << ";\n";
        else o << "    t." << f.name << " = " << Bool(*(const bool*)p) << ";\n";
    }
    o << "}\n\n";
    return o.str();
}

bool CopyInto(const fs::path& from, const fs::path& to, std::string& errors)
{
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        errors += "Couldn't copy " + from.string() + ": " + ec.message() + "\n";
        return false;
    }
    return true;
}

} // namespace

std::string GenerateMenuHeader(const json& input)
{
    json doc = input;
    Normalize(doc);

    CodeGen gen(doc);
    const auto font_files = ExportFileNames(doc["fonts"]);
    const auto image_files = ExportFileNames(doc["images"]);
    for (auto& [id, file] : font_files) gen.font_vars[id] = "font_" + Identifier(id);
    for (auto& [id, file] : image_files) gen.image_vars[id] = "image_" + Identifier(id);

    gen.DeclareState(doc["windows"]);
    for (auto& w : doc["windows"])
        if (GetStr(w, "type") == "window" && !GetBool(w, "hidden", false)) gen.Window(w);

    std::ostringstream o;
    o << "// " << GetStr(doc, "name", "Menu") << " - generated by ImGui Menu Editor.\n"
      << "// Re-export from the editor instead of editing this by hand.\n"
      << "//\n"
      << "// Usage (after ImGui::CreateContext() and your backend's init):\n"
      << "//   menu::LoadFonts(\"path/to/assets\");\n"
      << "//   menu::LoadImages(\"path/to/assets\", MyTextureLoader);   // only if the menu uses images\n"
      << "//   menu::ApplyStyle();\n"
      << "//   static menu::State state;\n"
      << "//   ...then every frame, between ImGui::NewFrame() and ImGui::Render():\n"
      << "//   menu::Render(state);\n"
      << "#pragma once\n#include \"imgui.h\"\n#include \"mm_widgets.h\"\n#include <cfloat>\n#include <string>\n\nnamespace menu {\n\n";

    o << "struct State {\n";
    for (auto& w : doc["windows"])
        if (GetStr(w, "type") == "window") o << "    bool show_" << Identifier(GetStr(w, "id")) << " = true; // window visibility\n";
    o << gen.state.str() << "};\n\n";

    // fonts
    o << "// ---- fonts\n";
    for (auto& [id, var] : gen.font_vars) o << "inline ImFont* " << var << " = nullptr;\n";
    o << "\n// Loads the menu fonts from <asset_dir>/fonts, with Font Awesome icons merged in.\n"
      << "inline void LoadFonts(const char* asset_dir)\n{\n    ImGuiIO& io = ImGui::GetIO();\n"
      << "    const std::string dir = std::string(asset_dir) + \"/fonts/\";\n";
    for (auto& f : doc["fonts"]) {
        const std::string id = GetStr(f, "id");
        if (!font_files.count(id)) continue;
        const float size = GetFloat(f, "size", 16);
        const std::string& var = gen.font_vars[id];
        o << "    " << var << " = io.Fonts->AddFontFromFileTTF((dir + " << CppString(font_files.at(id)) << ").c_str(), " << Float(size) << ");\n"
          << "    if (" << var << ") { ImFontConfig icons; icons.MergeMode = true; icons.GlyphMinAdvanceX = " << Float(size)
          << "; icons.PixelSnapH = true; io.Fonts->AddFontFromFileTTF((dir + \"fa-solid-900.ttf\").c_str(), " << Float(size * 0.9f) << ", &icons); }\n";
    }
    o << "}\n\n";

    // images
    o << "// ---- images\n";
    for (auto& [id, var] : gen.image_vars) o << "inline ImTextureID " << var << " = ImTextureID_Invalid;\n";
    o << "\n// Pass in your renderer's texture loader (return ImTextureID_Invalid if it fails).\n"
      << "using TextureLoader = ImTextureID (*)(const char* path, int* width, int* height);\n"
      << "inline void LoadImages(const char* asset_dir, TextureLoader load)\n{\n"
      << "    const std::string dir = std::string(asset_dir) + \"/images/\";\n    int w = 0, h = 0;\n";
    for (auto& [id, file] : image_files) o << "    " << gen.image_vars[id] << " = load((dir + " << CppString(file) << ").c_str(), &w, &h);\n";
    o << "    (void)w; (void)h; (void)load;\n}\n\n";

    // themes
    o << "// ---- themes\n";
    std::vector<std::string> theme_names;
    for (auto& [name, theme] : doc["themes"].items()) theme_names.push_back(name);
    for (auto& name : theme_names) o << ThemeFunction("ApplyTheme_" + Identifier(name), doc["themes"][name]);
    o << "inline const char* const theme_names[] = {";
    for (size_t i = 0; i < theme_names.size(); i++) o << (i ? ", " : "") << CppString(theme_names[i]);
    o << "};\ninline constexpr int theme_count = " << theme_names.size() << ";\n\n";
    o << "inline void ApplyTheme(int index)\n{\n    switch (index) {\n";
    for (size_t i = 0; i < theme_names.size(); i++) o << "    case " << i << ": ApplyTheme_" << Identifier(theme_names[i]) << "(); break;\n";
    o << "    default: break;\n    }\n}\n\n";
    o << "// The theme that was active in the editor (" << GetStr(doc, "theme") << ").\n"
      << "inline void ApplyStyle() { ApplyTheme_" << Identifier(GetStr(doc, "theme")) << "(); }\n\n";

    o << "// ---- menu\ninline void Render(State& s)\n{\n" << gen.body.str() << "}\n\n} // namespace menu\n";
    return o.str();
}

ExportResult ExportProject(const json& input, const std::string& project_dir, const std::string& data_dir, const std::string& out_dir)
{
    ExportResult result;
    json doc = input;
    Normalize(doc);

    const fs::path out(out_dir), data(data_dir);
    std::error_code ec;
    fs::create_directories(out, ec);
    if (ec) {
        result.message = "Couldn't create " + out_dir + ": " + ec.message();
        return result;
    }

    std::string errors;
    auto copy = [&](const fs::path& from, const std::string& to) {
        if (CopyInto(from, out / to, errors)) result.files.push_back(to);
    };

    if (WriteFileAtomic((out / "menu_generated.h").string(), GenerateMenuHeader(doc))) result.files.push_back("menu_generated.h");
    else errors += "Couldn't write menu_generated.h\n";

    copy(data / "runtime/mm_widgets.h", "mm_widgets.h");
    copy(data / "runtime/mm_widgets.cpp", "mm_widgets.cpp");

    Assets paths; // just for Resolve()
    paths.project_dir = project_dir;
    paths.data_dir = data_dir;
    const auto font_files = ExportFileNames(doc["fonts"]);
    for (auto& f : doc["fonts"]) {
        auto it = font_files.find(GetStr(f, "id"));
        if (it != font_files.end()) copy(paths.Resolve(GetStr(f, "path")), "assets/fonts/" + it->second);
    }
    copy(data / "fonts/fa-solid-900.ttf", "assets/fonts/fa-solid-900.ttf");
    const auto image_files = ExportFileNames(doc["images"]);
    for (auto& im : doc["images"]) {
        auto it = image_files.find(GetStr(im, "id"));
        if (it != image_files.end()) copy(paths.Resolve(GetStr(im, "path")), "assets/images/" + it->second);
    }

    // tiny example app so you can try the export on its own
    copy(data / "example/main.cpp", "example/main.cpp");
    copy(data / "example/CMakeLists.txt", "example/CMakeLists.txt");
    copy(data / "stb/stb_image.h", "example/stb_image.h");
    copy(data / "example/README.md", "README.md");

    result.ok = errors.empty();
    result.message = result.ok ? "Exported " + std::to_string(result.files.size()) + " files to " + out_dir : errors;
    return result;
}
