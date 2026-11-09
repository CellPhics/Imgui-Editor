#include "exporter.h"
#include "assets.h"
#include "renderer.h"
#include "theme.h"
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

std::string CppStr(const std::string& s)
{
    std::string o = "\"";
    bool after_hex = false;
    for (unsigned char c : s) {
        if (after_hex && std::isxdigit(c)) o += "\"\"";
        after_hex = false;
        switch (c) {
            case '\\': o += "\\\\"; break;
            case '"':  o += "\\\""; break;
            case '\n': o += "\\n"; break;
            case '\t': o += "\\t"; break;
            case '\r': o += "\\r"; break;
            default:
                if (c < 0x20 || c >= 0x7F) {
                    char b[8];
                    snprintf(b, sizeof(b), "\\x%02x", c);
                    o += b;
                    after_hex = true;
                } else o += (char)c;
        }
    }
    return o + "\"";
}

std::string F(float v)
{
    char b[32];
    if (std::fabs(v - std::round(v)) < 1e-6f) snprintf(b, sizeof(b), "%.1ff", v);
    else snprintf(b, sizeof(b), "%.4gf", v);
    std::string s = b;
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos) s.insert(s.size() - 1, ".0");
    return s;
}
std::string V2(ImVec2 v) { return "ImVec2(" + F(v.x) + ", " + F(v.y) + ")"; }
std::string V4(const ImVec4& c) { return "ImVec4(" + F(c.x) + ", " + F(c.y) + ", " + F(c.z) + ", " + F(c.w) + ")"; }

std::string Ident(const std::string& s)
{
    std::string o;
    for (unsigned char c : s) o += std::isalnum(c) ? (char)c : '_';
    if (o.empty() || std::isdigit((unsigned char)o[0])) o = "v_" + o;
    static const std::set<std::string> kw = {"auto", "bool", "break", "case", "char", "class", "const", "continue", "default",
        "delete", "do", "double", "else", "enum", "float", "for", "if", "int", "long", "new", "private", "public", "return",
        "short", "signed", "static", "struct", "switch", "this", "union", "unsigned", "void", "while", "open", "s"};
    if (kw.count(o)) o += "_";
    return o;
}

std::string IconLabelStr(const json& n, const char* key = "label")
{
    std::string label = JS(n, key);
    std::string icon = IconUtf8(JS(n, "icon"));
    if (!icon.empty() && !label.empty()) return icon + "  " + label;
    if (!icon.empty()) return icon;
    return label;
}

std::string FileName(const std::string& p) { return fs::path(p).filename().string(); }

// Code that applies a {"colors","vars","mm"} override to the live style.
std::vector<std::string> OverrideLines(const json& o)
{
    std::vector<std::string> out;
    if (!o.is_object()) return out;
    if (o.contains("colors") && o["colors"].is_object())
        for (int i = 0; i < ImGuiCol_COUNT; i++) {
            const char* name = ImGui::GetStyleColorName(i);
            ImVec4 c;
            if (o["colors"].contains(name) && ParseColor(o["colors"][name], c))
                out.push_back(std::string("ImGui::GetStyle().Colors[ImGuiCol_") + name + "] = " + V4(c) + ";");
        }
    if (o.contains("vars") && o["vars"].is_object())
        for (auto& v : StyleVars()) {
            if (!o["vars"].contains(v.name)) continue;
            const json& j = o["vars"][v.name];
            if (v.count == 1 && j.is_number()) out.push_back(std::string("ImGui::GetStyle().") + v.name + " = " + F(j.get<float>()) + ";");
            else if (v.count == 2 && j.is_array() && j.size() == 2 && j[0].is_number() && j[1].is_number())
                out.push_back(std::string("ImGui::GetStyle().") + v.name + " = " + V2(ImVec2(j[0].get<float>(), j[1].get<float>())) + ";");
        }
    if (o.contains("mm") && o["mm"].is_object())
        for (auto& f : MmFields()) {
            if (!o["mm"].contains(f.name)) continue;
            const json& j = o["mm"][f.name];
            ImVec4 c;
            if (f.kind == 0 && ParseColor(j, c)) out.push_back(std::string("mm::GetTheme().") + f.name + " = " + V4(c) + ";");
            else if (f.kind == 1 && j.is_number()) out.push_back(std::string("mm::GetTheme().") + f.name + " = " + F(j.get<float>()) + ";");
            else if (f.kind == 2 && j.is_boolean()) out.push_back(std::string("mm::GetTheme().") + f.name + " = " + (j.get<bool>() ? "true;" : "false;"));
        }
    return out;
}

int LookOf(const json& n)
{
    const std::string l = JS(n, "look", "default");
    if (JS(n, "type") == "checkbox") return l == "filled" ? 1 : l == "circle" ? 2 : 0;
    return l == "thin" ? 1 : l == "filled" ? 2 : l == "line" ? 3 : 0;
}

struct Gen {
    const json& doc;
    std::ostringstream state, body;
    std::map<std::string, std::string> vars; // node id -> var name
    std::set<std::string> used;
    std::map<std::string, std::string> font_var, img_var;
    int ind = 1;
    int uniq = 0;

    std::string Pad() const { return std::string(ind * 4, ' '); }

    // Opens a scope that applies the node's style override; returns true if opened.
    bool BeginStyle(const json& n)
    {
        auto lines = n.contains("style") ? OverrideLines(n["style"]) : std::vector<std::string>();
        if (lines.empty()) return false;
        std::string sfx = std::to_string(uniq++);
        L("{ // style override: " + JS(n, "id"));
        ind++;
        L("const ImGuiStyle saved_style_" + sfx + " = ImGui::GetStyle();");
        L("const mm::Theme saved_theme_" + sfx + " = mm::GetTheme();");
        for (auto& l : lines) L(l);
        style_sfx.push_back(sfx);
        return true;
    }
    void EndStyle()
    {
        std::string sfx = style_sfx.back();
        style_sfx.pop_back();
        L("ImGui::GetStyle() = saved_style_" + sfx + ";");
        L("mm::GetTheme() = saved_theme_" + sfx + ";");
        ind--;
        L("}");
    }
    std::vector<std::string> style_sfx;

    void Place(const json& n)
    {
        if (n.contains("pos") && n["pos"].is_array()) { L("ImGui::SetCursorPos(" + V2(JV2(n, "pos", ImVec2(0, 0))) + ");"); return; }
        if (JB(n, "same_line", false)) L("ImGui::SameLine();");
        if (n.contains("offset") && n["offset"].is_array()) {
            ImVec2 o = JV2(n, "offset", ImVec2(0, 0));
            L("{ const ImVec2 c = ImGui::GetCursorPos(); ImGui::SetCursorPos(ImVec2(c.x + " + F(o.x) + ", c.y + " + F(o.y) + ")); }");
        }
    }
    void L(const std::string& s) { body << Pad() << s << "\n"; }

    std::string VarFor(const json& n)
    {
        const std::string id = JS(n, "id");
        auto it = vars.find(id);
        if (it != vars.end()) return it->second;
        std::string base = Ident(JS(n, "var").empty() ? id : JS(n, "var"));
        std::string v = base;
        for (int i = 2; used.count(v); i++) v = base + "_" + std::to_string(i);
        used.insert(v);
        vars[id] = v;
        return v;
    }

    std::string FontRef(const std::string& id) const
    {
        auto it = font_var.find(id);
        return it == font_var.end() ? "nullptr" : it->second;
    }
    std::string ImgRef(const std::string& id) const
    {
        auto it = img_var.find(id);
        return it == img_var.end() ? "ImTextureID_Invalid" : it->second;
    }

    // ---- state declarations
    void DeclareState(const json& arr)
    {
        if (!arr.is_array()) return;
        for (auto& n : arr) {
            if (!n.is_object()) continue;
            const std::string type = JS(n, "type");
            const std::string id = JS(n, "id");
            std::string comment = "  // " + id;
            if (type == "checkbox" || type == "toggle")
                state << "    bool " << VarFor(n) << " = " << (JB(n, "default", false) ? "true" : "false") << ";" << comment << "\n";
            else if (type == "slider_float")
                state << "    float " << VarFor(n) << " = " << F(JF(n, "default", 0)) << ";" << comment << "\n";
            else if (type == "progress")
                state << "    float " << VarFor(n) << " = " << F(JF(n, "default", 0.5f)) << ";" << comment << " (0..1)\n";
            else if (type == "slider_int" || type == "combo" || type == "radio" || type == "tabs")
                state << "    int " << VarFor(n) << " = " << JI(n, "default", 0) << ";" << comment << (type == "tabs" ? " (selected tab)" : "") << "\n";
            else if (type == "multi_combo") {
                auto items = JItems(n, "items");
                state << "    bool " << VarFor(n) << "[" << std::max<size_t>(1, items.size()) << "] = {";
                for (size_t i = 0; i < std::max<size_t>(1, items.size()); i++) {
                    bool b = n.contains("default") && n["default"].is_array() && i < n["default"].size() && n["default"][i].is_boolean() && n["default"][i].get<bool>();
                    state << (i ? ", " : "") << (b ? "true" : "false");
                }
                state << "};" << comment << "\n";
            } else if (type == "color") {
                ImVec4 c(1, 1, 1, 1);
                JCol(n, "default", c);
                state << "    float " << VarFor(n) << "[4] = {" << F(c.x) << ", " << F(c.y) << ", " << F(c.z) << ", " << F(c.w) << "};" << comment << "\n";
            } else if (type == "keybind") {
                int k = KeyFromName(JS(n, "default", "None"));
                state << "    int " << VarFor(n) << " = " << (k ? "ImGuiKey_" + KeyName(k) : std::string("ImGuiKey_None")) << ";" << comment << " (ImGuiKey)\n";
            } else if (type == "input_text") {
                int len = std::max(1, JI(n, "max_length", 64));
                state << "    char " << VarFor(n) << "[" << (len + 1) << "] = " << CppStr(JS(n, "default")) << ";" << comment << "\n";
            } else if (type == "button") {
                state << "    bool " << VarFor(n) << " = false;" << comment << " (true on the frame it was clicked)\n";
            }
            if (n.contains("children")) DeclareState(n["children"]);
        }
    }

    // ---- body
    void Children(const json& n)
    {
        if (!n.contains("children") || !n["children"].is_array()) return;
        for (auto& c : n["children"]) Node(c);
    }

    void Node(const json& n)
    {
        if (!n.is_object() || JB(n, "hidden", false)) return;
        const std::string type = JS(n, "type");
        const bool styled = BeginStyle(n);
        if (type == "group") Group(n);
        else if (type == "columns") Columns(n);
        else if (type == "tabs") Tabs(n);
        else if (type == "tab") Children(n);
        else Leaf(n, type);
        if (styled) EndStyle();
    }

    void Group(const json& n)
    {
        Place(n);
        L("// group: " + JS(n, "id"));
        const std::string ts = JS(n, "title_style", "inside");
        L("mm::BeginGroup(" + CppStr(JS(n, "id")) + ", " + CppStr(JS(n, "title")) + ", " + V2(JV2(n, "size", ImVec2(0, 0))) + ", " +
          (JB(n, "border", true) ? "true" : "false") + ", " +
          (ts == "border" ? "mm::GroupTitle_Border" : ts == "plain" ? "mm::GroupTitle_Plain" : "mm::GroupTitle_Inside") + ");");
        ind++;
        Children(n);
        ind--;
        L("mm::EndGroup();");
    }

    void Columns(const json& n)
    {
        int count = std::clamp(JI(n, "count", 2), 1, 16);
        L("if (ImGui::BeginTable(" + CppStr(JS(n, "id")) + ", " + std::to_string(count) + ", ImGuiTableFlags_SizingStretchSame)) {");
        ind++;
        if (n.contains("children") && n["children"].is_array())
            for (auto& c : n["children"]) {
                if (JB(c, "hidden", false)) continue;
                L("ImGui::TableNextColumn();");
                Node(c);
            }
        L("ImGui::EndTable();");
        ind--;
        L("}");
    }

    void Tabs(const json& n)
    {
        const std::string id = JS(n, "id");
        std::vector<const json*> tabs;
        if (n.contains("children") && n["children"].is_array())
            for (auto& c : n["children"])
                if (c.is_object() && !JB(c, "hidden", false)) tabs.push_back(&c);
        std::string var = VarFor(n);
        L("{ // tabs: " + id);
        ind++;
        std::string labels = "static const char* const labels[] = {", icons = "static const char* const icons[] = {";
        for (size_t i = 0; i < tabs.size(); i++) {
            labels += (i ? ", " : "") + CppStr(JS(*tabs[i], "label", "Tab"));
            icons += (i ? ", " : "") + CppStr(IconUtf8(JS(*tabs[i], "icon")));
        }
        if (tabs.empty()) { labels += "\"\""; icons += "\"\""; }
        L(labels + "};");
        L(icons + "};");
        bool any_image = false;
        for (auto* t : tabs) any_image |= img_var.count(JS(*t, "image")) > 0;
        if (any_image) {
            std::string imgs = "const ImTextureID images[] = {";
            for (size_t i = 0; i < tabs.size(); i++) imgs += (i ? ", " : "") + ImgRef(JS(*tabs[i], "image"));
            L(imgs + "};");
        }
        std::string style = JS(n, "style", "sidebar");
        L("mm::TabsOptions o;");
        L(std::string("o.style = ") + (style == "top" ? "mm::TabsStyle_Top" : style == "pills" ? "mm::TabsStyle_Pills" : "mm::TabsStyle_Sidebar") + ";");
        L("o.sidebar_width = " + F(JF(n, "sidebar_width", 170)) + ";");
        if (!JS(n, "logo").empty()) {
            L("o.logo = " + ImgRef(JS(n, "logo")) + ";");
            L("o.logo_size = " + V2(JV2(n, "logo_size", ImVec2(64, 64))) + ";");
        }
        if (!JS(n, "header").empty()) {
            L("o.header = " + CppStr(JS(n, "header")) + ";");
            L("o.header_font = " + FontRef(JS(n, "header_font")) + ";");
            L("o.header_size = " + F(JF(n, "header_size", 0)) + ";");
        }
        const std::string hl = JS(n, "highlight", "bar");
        if (hl != "bar") L(std::string("o.highlight = ") + (hl == "box" ? "mm::TabHighlight_Box" : hl == "fill" ? "mm::TabHighlight_Fill" : "mm::TabHighlight_None") + ";");
        if (JS(n, "layout", "row") == "stacked") L("o.layout = mm::TabLayout_Stacked;");
        if (JB(n, "icon_only", false)) L("o.icon_only = true;");
        if (JF(n, "icon_size", 0) > 0) L("o.icon_size = " + F(JF(n, "icon_size", 0)) + ";");
        if (any_image) L("o.images = images;");
        if (n.contains("image_size") && n["image_size"].is_array()) L("o.image_size = " + V2(JV2(n, "image_size", ImVec2(0, 0))) + ";");
        if (!JB(n, "tint_images", true)) L("o.tint_images = false;");
        if (JF(n, "tab_height", 0) > 0) L("o.tab_height = " + F(JF(n, "tab_height", 0)) + ";");
        if (JF(n, "tab_gap", 4) != 4) L("o.tab_gap = " + F(JF(n, "tab_gap", 4)) + ";");
        if (JB(n, "separators", false)) L("o.separators = true;");
        if (JB(n, "sidebar_border", false)) L("o.sidebar_border = true;");
        if (n.contains("sidebar_padding") && n["sidebar_padding"].is_array()) L("o.sidebar_padding = " + V2(JV2(n, "sidebar_padding", ImVec2(10, 12))) + ";");
        L("mm::BeginTabs(" + CppStr(id) + ", labels, icons, " + std::to_string(tabs.size()) + ", &s." + var + ", o);");
        L("switch (s." + var + ") {");
        for (size_t i = 0; i < tabs.size(); i++) {
            L("case " + std::to_string(i) + ": { // " + JS(*tabs[i], "label"));
            ind++;
            Children(*tabs[i]);
            L("break;");
            ind--;
            L("}");
        }
        L("default: break;");
        L("}");
        L("mm::EndTabs();");
        ind--;
        L("}");
    }

    static std::string FW(float width) { return width != 0 ? ", false" : ""; }
    std::string Lbl(const json& n) { return CppStr(IconLabelStr(n) + "##" + JS(n, "id")); }

    void Leaf(const json& n, const std::string& type)
    {
        const std::string id = JS(n, "id");
        Place(n);
        const bool disabled = JB(n, "disabled", false);
        const float width = JF(n, "width", 0);
        const std::string tip = JS(n, "tooltip");
        if (disabled) L("ImGui::BeginDisabled();");
        if (width != 0) L("ImGui::PushItemWidth(" + (width < 0 ? std::string("-FLT_MIN") : F(width)) + ");");
        if (!tip.empty()) L("ImGui::BeginGroup();");

        if (type == "text") {
            ImVec4 col;
            bool has = JCol(n, "color", col);
            std::string a = JS(n, "align", "left");
            std::string args = CppStr(JS(n, "text")) + ", " + (has ? "&col" : "nullptr") + ", " + FontRef(JS(n, "font")) + ", " +
                               F(JF(n, "size", 0)) + ", " + (a == "center" ? "1" : a == "right" ? "2" : "0") + ", " + (JB(n, "wrap", false) ? "true" : "false");
            if (has) L("{ const ImVec4 col = " + V4(col) + "; mm::TextEx(" + args + "); }");
            else L("mm::TextEx(" + args + ");");
        } else if (type == "separator") {
            std::string l = JS(n, "label");
            L(l.empty() ? "ImGui::Separator();" : "ImGui::SeparatorText(" + CppStr(l) + ");");
        } else if (type == "spacing") {
            float h = JF(n, "height", 8);
            L("ImGui::Dummy(" + (JB(n, "same_line", false) ? V2(ImVec2(h, 0)) : V2(ImVec2(0, h))) + ");");
        } else if (type == "button") {
            L("s." + VarFor(n) + " = ImGui::Button(" + Lbl(n) + ", " + V2(JV2(n, "size", ImVec2(0, 0))) + ");");
        } else if (type == "checkbox") {
            if (LookOf(n)) L("mm::CheckboxEx(" + Lbl(n) + ", &s." + VarFor(n) + ", " + std::to_string(LookOf(n)) + ", " + F(JF(n, "box_size", 0)) + ");");
            else L("ImGui::Checkbox(" + Lbl(n) + ", &s." + VarFor(n) + ");");
        } else if (type == "toggle") {
            L("mm::Toggle(" + Lbl(n) + ", &s." + VarFor(n) + ");");
        } else if (type == "slider_float" || type == "slider_int") {
            const int look = LookOf(n);
            const bool fl = type == "slider_float";
            std::string range = fl ? F(JF(n, "min", 0)) + ", " + F(JF(n, "max", 100)) : std::to_string(JI(n, "min", 0)) + ", " + std::to_string(JI(n, "max", 100));
            std::string fn = fl ? "mm::SliderFloatEx(" : "mm::SliderIntEx(";
            std::string args = "&s." + VarFor(n) + ", " + range + ", " + CppStr(JS(n, "format", fl ? "%.1f" : "%d")) + ", " + std::to_string(look) +
                               ", " + F(JF(n, "track_height", 0)) + ", " + (JB(n, "markers", false) ? "true" : "false") + ")";
            if (look == 1) {
                if (width == 0) L("ImGui::PushItemWidth(-FLT_MIN);");
                L(fn + Lbl(n) + ", " + args + ";");
                if (width == 0) L("ImGui::PopItemWidth();");
            } else {
                L(fn + "mm::LabelAbove(" + Lbl(n) + FW(width) + "), " + args + "; mm::LabelAboveEnd();");
            }
        } else if (type == "rect") {
            ImVec4 c(0.55f, 0.36f, 0.96f, 1), c2(0, 0, 0, -1);
            JCol(n, "color", c);
            JCol(n, "color2", c2);
            L("mm::Rect(" + V2(JV2(n, "size", ImVec2(-1, 2))) + ", " + V4(c) + ", " + V4(c2) + ", " + (JB(n, "vertical", false) ? "true" : "false") + ", " + F(JF(n, "rounding", 0)) + ");");
        } else if (type == "combo" || type == "multi_combo" || type == "radio") {
            auto items = JItems(n, "items");
            std::string arr = "static const char* const items[] = {";
            for (size_t i = 0; i < items.size(); i++) arr += (i ? ", " : "") + CppStr(items[i]);
            if (items.empty()) arr += "\"\"";
            arr += "};";
            const std::string cnt = std::to_string(items.size());
            L("{");
            ind++;
            L(arr);
            if (type == "combo")
                L("ImGui::Combo(mm::LabelAbove(" + Lbl(n) + FW(width) + "), &s." + VarFor(n) + ", items, " + cnt + "); mm::LabelAboveEnd();");
            else if (type == "multi_combo")
                L("mm::MultiCombo(mm::LabelAbove(" + Lbl(n) + FW(width) + "), s." + VarFor(n) + ", items, " + cnt + "); mm::LabelAboveEnd();");
            else {
                if (!JS(n, "label").empty()) L("ImGui::TextUnformatted(" + CppStr(IconLabelStr(n)) + ");");
                const bool horiz = JB(n, "horizontal", true);
                L("for (int i = 0; i < " + cnt + "; i++) {");
                ind++;
                if (horiz) L("if (i > 0) ImGui::SameLine();");
                L("ImGui::PushID(i); ImGui::RadioButton(items[i], &s." + VarFor(n) + ", i); ImGui::PopID();");
                ind--;
                L("}");
            }
            ind--;
            L("}");
        } else if (type == "color") {
            std::string flags = JB(n, "alpha", true) ? "ImGuiColorEditFlags_AlphaBar" : "ImGuiColorEditFlags_NoAlpha";
            if (JB(n, "inline", false)) {
                L("ImGui::AlignTextToFramePadding();");
                L("ImGui::TextUnformatted(" + CppStr(IconLabelStr(n)) + ");");
                L("ImGui::SameLine();");
                L("{ const float sq = ImGui::GetFrameHeight(), avail = ImGui::GetContentRegionAvail().x; if (avail > sq) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - sq); }");
                L("ImGui::ColorEdit4(" + CppStr("##" + id) + ", s." + VarFor(n) + ", " + flags + " | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);");
            } else {
                L("ImGui::ColorEdit4(mm::LabelAbove(" + Lbl(n) + FW(width) + "), s." + VarFor(n) + ", " + flags + "); mm::LabelAboveEnd();");
            }
        } else if (type == "keybind") {
            L("mm::Keybind(" + Lbl(n) + ", &s." + VarFor(n) + ");");
        } else if (type == "input_text") {
            L("ImGui::InputTextWithHint(mm::LabelAbove(" + Lbl(n) + FW(width) + "), " + CppStr(JS(n, "hint")) + ", s." + VarFor(n) + ", sizeof(s." + VarFor(n) + ")" +
              (JB(n, "password", false) ? ", ImGuiInputTextFlags_Password" : "") + "); mm::LabelAboveEnd();");
        } else if (type == "image") {
            ImVec2 size = JV2(n, "size", ImVec2(64, 64));
            std::string a = JS(n, "align", "left");
            L("{");
            ind++;
            L("const ImVec2 sz = " + V2(size) + ";");
            if (a != "left")
                L(std::string("const float avail = ImGui::GetContentRegionAvail().x; if (avail > sz.x) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ") +
                  (a == "center" ? "(avail - sz.x) * 0.5f" : "avail - sz.x") + ");");
            L("ImGui::Dummy(sz);");
            L("if (" + ImgRef(JS(n, "image")) + " != ImTextureID_Invalid) ImGui::GetWindowDrawList()->AddImageRounded(" + ImgRef(JS(n, "image")) +
              ", ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, " + F(JF(n, "rounding", 0)) + ");");
            ind--;
            L("}");
        } else if (type == "progress") {
            std::string ov = JS(n, "overlay");
            L("ImGui::ProgressBar(s." + VarFor(n) + ", " + V2(JV2(n, "size", ImVec2(-1, 0))) + ", " + (ov.empty() ? std::string("nullptr") : CppStr(ov)) + ");");
        } else {
            L("// unknown widget type '" + type + "' (" + id + ")");
        }

        if (!tip.empty()) { L("ImGui::EndGroup();"); L("ImGui::SetItemTooltip(\"%s\", " + CppStr(tip) + ");"); }
        if (width != 0) L("ImGui::PopItemWidth();");
        if (disabled) L("ImGui::EndDisabled();");
    }

    void Window(const json& w)
    {
        const std::string id = JS(w, "id");
        std::vector<std::string> fl;
        if (JB(w, "no_title_bar", true)) fl.push_back("ImGuiWindowFlags_NoTitleBar");
        if (JB(w, "no_resize", false)) fl.push_back("ImGuiWindowFlags_NoResize");
        if (JB(w, "no_move", false)) fl.push_back("ImGuiWindowFlags_NoMove");
        if (JB(w, "no_collapse", true)) fl.push_back("ImGuiWindowFlags_NoCollapse");
        if (JB(w, "no_scrollbar", true)) { fl.push_back("ImGuiWindowFlags_NoScrollbar"); fl.push_back("ImGuiWindowFlags_NoScrollWithMouse"); }
        if (JB(w, "no_background", false)) fl.push_back("ImGuiWindowFlags_NoBackground");
        std::string flags;
        for (size_t i = 0; i < fl.size(); i++) flags += (i ? " | " : "") + fl[i];
        if (flags.empty()) flags = "ImGuiWindowFlags_None";

        std::string font_id = JS(w, "font");
        if (!font_var.count(font_id) && doc["fonts"].is_array() && !doc["fonts"].empty()) font_id = JS(doc["fonts"][0], "id");
        float fsize = 0;
        for (auto& f : doc["fonts"]) if (JS(f, "id") == font_id) fsize = JF(f, "size", 16);

        L("// window: " + id);
        L("if (s.show_" + Ident(id) + ") {");
        ind++;
        const bool styled = BeginStyle(w);
        L("ImGui::SetNextWindowPos(" + V2(JV2(w, "pos", ImVec2(60, 60))) + ", ImGuiCond_FirstUseEver);");
        L("ImGui::SetNextWindowSize(" + V2(JV2(w, "size", ImVec2(720, 460))) + ", ImGuiCond_FirstUseEver);");
        L("ImGui::PushFont(" + FontRef(font_id) + ", " + F(fsize) + ");");
        const bool fade = JB(w, "fade", true);
        if (fade) L("mm::PushWindowFade(" + CppStr(id) + ");");
        const bool pad = w.contains("padding") && w["padding"].is_array();
        if (pad) L("ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, " + V2(JV2(w, "padding", ImVec2(0, 0))) + ");");
        L("const bool visible = ImGui::Begin(" + CppStr(JS(w, "title", "Menu") + "###mm_" + id) + ", nullptr, " + flags + ");");
        if (pad) L("ImGui::PopStyleVar();");
        L("if (visible) {");
        ind++;
        Children(w);
        ind--;
        L("}");
        L("ImGui::End();");
        if (fade) L("mm::PopWindowFade();");
        L("ImGui::PopFont();");
        if (styled) EndStyle();
        ind--;
        L("}");
    }
};

std::string ThemeFn(const std::string& fn, const json& theme)
{
    ImGuiStyle st;
    mm::Theme mt;
    ThemeFromJson(theme, st, mt);
    std::ostringstream o;
    o << "inline void " << fn << "()\n{\n    ImGuiStyle& st = ImGui::GetStyle();\n";
    for (auto& v : StyleVars()) {
        const float* f = (const float*)((const char*)&st + v.offset);
        if (v.count == 1) o << "    st." << v.name << " = " << F(f[0]) << ";\n";
        else o << "    st." << v.name << " = " << V2(ImVec2(f[0], f[1])) << ";\n";
    }
    for (int i = 0; i < ImGuiCol_COUNT; i++)
        o << "    st.Colors[ImGuiCol_" << ImGui::GetStyleColorName(i) << "] = " << V4(st.Colors[i]) << ";\n";
    o << "    mm::Theme& t = mm::GetTheme();\n";
    for (auto& f : MmFields()) {
        const char* p = (const char*)&mt + f.offset;
        if (f.kind == 0) o << "    t." << f.name << " = " << V4(*(const ImVec4*)p) << ";\n";
        else if (f.kind == 1) o << "    t." << f.name << " = " << F(*(const float*)p) << ";\n";
        else o << "    t." << f.name << " = " << (*(const bool*)p ? "true" : "false") << ";\n";
    }
    o << "}\n\n";
    return o.str();
}

// File names inside the exported assets folder (deduplicated).
std::map<std::string, std::string> ExportNames(const json& arr)
{
    std::map<std::string, std::string> out;
    std::set<std::string> used;
    if (!arr.is_array()) return out;
    for (auto& e : arr) {
        std::string id = JS(e, "id");
        if (id.empty()) continue;
        std::string name = FileName(JS(e, "path"));
        if (name.empty()) name = id;
        if (used.count(name)) name = Ident(id) + "_" + name;
        used.insert(name);
        out[id] = name;
    }
    return out;
}

} // namespace

std::string GenerateMenuHeader(const json& doc_in)
{
    json doc = doc_in;
    Normalize(doc);
    Gen g{doc};
    auto font_files = ExportNames(doc["fonts"]);
    auto image_files = ExportNames(doc["images"]);
    for (auto& [id, file] : font_files) { (void)file; g.font_var[id] = "font_" + Ident(id); }
    for (auto& [id, file] : image_files) { (void)file; g.img_var[id] = "image_" + Ident(id); }

    g.DeclareState(doc["windows"]);
    for (auto& w : doc["windows"])
        if (JS(w, "type") == "window" && !JB(w, "hidden", false)) g.Window(w);

    std::ostringstream o;
    o << "// " << JS(doc, "name", "Menu") << " — generated by ImGui Menu Editor.\n"
      << "// Re-export from the editor instead of editing this file by hand.\n"
      << "//\n// Usage (after ImGui::CreateContext() and backend init):\n"
      << "//   menu::LoadFonts(\"path/to/assets\");\n"
      << "//   menu::LoadImages(\"path/to/assets\", MyTextureLoader);   // only if the menu uses images\n"
      << "//   menu::ApplyStyle();\n"
      << "//   static menu::State state;\n"
      << "//   ... every frame, between ImGui::NewFrame() and ImGui::Render():\n"
      << "//   menu::Render(state);\n"
      << "#pragma once\n#include \"imgui.h\"\n#include \"mm_widgets.h\"\n#include <cfloat>\n#include <string>\n\nnamespace menu {\n\n";

    o << "struct State {\n";
    for (auto& w : doc["windows"])
        if (JS(w, "type") == "window") o << "    bool show_" << Ident(JS(w, "id")) << " = true; // window visibility\n";
    o << g.state.str() << "};\n\n";

    o << "// ---- fonts\n";
    for (auto& [id, v] : g.font_var) o << "inline ImFont* " << v << " = nullptr;\n";
    o << "\n// Loads the menu fonts (with Font Awesome icons merged in) from <asset_dir>/fonts.\n"
      << "inline void LoadFonts(const char* asset_dir)\n{\n    ImGuiIO& io = ImGui::GetIO();\n"
      << "    const std::string dir = std::string(asset_dir) + \"/fonts/\";\n";
    for (auto& f : doc["fonts"]) {
        std::string id = JS(f, "id");
        if (!font_files.count(id)) continue;
        float size = JF(f, "size", 16);
        o << "    " << g.font_var[id] << " = io.Fonts->AddFontFromFileTTF((dir + " << CppStr(font_files[id]) << ").c_str(), " << F(size) << ");\n"
          << "    if (" << g.font_var[id] << ") { ImFontConfig ic; ic.MergeMode = true; ic.GlyphMinAdvanceX = " << F(size) << "; ic.PixelSnapH = true;"
          << " io.Fonts->AddFontFromFileTTF((dir + \"fa-solid-900.ttf\").c_str(), " << F(size * 0.9f) << ", &ic); }\n";
    }
    o << "}\n\n";

    o << "// ---- images\n";
    for (auto& [id, v] : g.img_var) o << "inline ImTextureID " << v << " = ImTextureID_Invalid;\n";
    o << "\n// Your renderer's texture loader: returns a texture id (or ImTextureID_Invalid).\n"
      << "using TextureLoader = ImTextureID (*)(const char* path, int* width, int* height);\n"
      << "inline void LoadImages(const char* asset_dir, TextureLoader load)\n{\n"
      << "    const std::string dir = std::string(asset_dir) + \"/images/\";\n    int w = 0, h = 0;\n";
    for (auto& [id, file] : image_files)
        o << "    " << g.img_var[id] << " = load((dir + " << CppStr(file) << ").c_str(), &w, &h);\n";
    o << "    (void)w; (void)h; (void)load;\n}\n\n";

    o << "// ---- themes\n";
    std::vector<std::string> theme_names;
    for (auto& [name, t] : doc["themes"].items()) theme_names.push_back(name);
    for (size_t i = 0; i < theme_names.size(); i++)
        o << ThemeFn("ApplyTheme_" + Ident(theme_names[i]), doc["themes"][theme_names[i]]);
    o << "inline const char* const theme_names[] = {";
    for (size_t i = 0; i < theme_names.size(); i++) o << (i ? ", " : "") << CppStr(theme_names[i]);
    o << "};\ninline constexpr int theme_count = " << theme_names.size() << ";\n\n";
    o << "inline void ApplyTheme(int index)\n{\n    switch (index) {\n";
    for (size_t i = 0; i < theme_names.size(); i++)
        o << "    case " << i << ": ApplyTheme_" << Ident(theme_names[i]) << "(); break;\n";
    o << "    default: break;\n    }\n}\n\n";
    o << "// Applies the theme that was active in the editor (" << JS(doc, "theme") << ").\n"
      << "inline void ApplyStyle() { ApplyTheme_" << Ident(JS(doc, "theme")) << "(); }\n\n";

    o << "// ---- menu\n" << "inline void Render(State& s)\n{\n" << g.body.str() << "}\n\n} // namespace menu\n";
    return o.str();
}

static bool CopyFile(const fs::path& from, const fs::path& to, std::string& err)
{
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) { err += "Couldn't copy " + from.string() + ": " + ec.message() + "\n"; return false; }
    return true;
}

ExportResult ExportProject(const json& doc_in, const std::string& project_dir, const std::string& data_dir, const std::string& out_dir)
{
    ExportResult r;
    json doc = doc_in;
    Normalize(doc);
    std::string err;
    fs::path out(out_dir);
    std::error_code ec;
    fs::create_directories(out, ec);
    if (ec) { r.message = "Couldn't create " + out_dir + ": " + ec.message(); return r; }

    auto write = [&](const fs::path& p, const std::string& s) {
        if (WriteFileAtomic(p.string(), s)) r.files.push_back(fs::relative(p, out, ec).string());
        else err += "Couldn't write " + p.string() + "\n";
    };

    write(out / "menu_generated.h", GenerateMenuHeader(doc));

    fs::path data(data_dir);
    for (auto* f : {"mm_widgets.h", "mm_widgets.cpp"})
        if (CopyFile(data / "runtime" / f, out / f, err)) r.files.push_back(f);

    Assets resolver;
    resolver.project_dir = project_dir;
    resolver.data_dir = data_dir;
    auto font_files = ExportNames(doc["fonts"]);
    for (auto& f : doc["fonts"]) {
        std::string id = JS(f, "id");
        if (!font_files.count(id)) continue;
        if (CopyFile(resolver.Resolve(JS(f, "path")), out / "assets/fonts" / font_files[id], err)) r.files.push_back("assets/fonts/" + font_files[id]);
    }
    if (CopyFile(data / "fonts/fa-solid-900.ttf", out / "assets/fonts/fa-solid-900.ttf", err)) r.files.push_back("assets/fonts/fa-solid-900.ttf");
    auto image_files = ExportNames(doc["images"]);
    for (auto& im : doc["images"]) {
        std::string id = JS(im, "id");
        if (!image_files.count(id)) continue;
        if (CopyFile(resolver.Resolve(JS(im, "path")), out / "assets/images" / image_files[id], err)) r.files.push_back("assets/images/" + image_files[id]);
    }

    // Example host so the export can be built and tried on its own.
    for (auto* f : {"main.cpp", "CMakeLists.txt"})
        if (CopyFile(data / "example" / f, out / "example" / f, err)) r.files.push_back(std::string("example/") + f);
    if (CopyFile(data / "stb/stb_image.h", out / "example/stb_image.h", err)) r.files.push_back("example/stb_image.h");
    if (CopyFile(data / "example/README.md", out / "README.md", err)) r.files.push_back("README.md");

    r.ok = err.empty();
    r.message = r.ok ? "Exported " + std::to_string(r.files.size()) + " files to " + out_dir : err;
    return r;
}
