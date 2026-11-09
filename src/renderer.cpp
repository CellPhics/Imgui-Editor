#include "renderer.h"
#include "mm_widgets.h"
#include "theme.h"
#include <cfloat>
#include <cstring>

// ---------------------------------------------------------------- json getters

float JF(const json& n, const char* k, float d)
{
    auto it = n.find(k);
    return it != n.end() && it->is_number() ? it->get<float>() : d;
}
int JI(const json& n, const char* k, int d)
{
    auto it = n.find(k);
    return it != n.end() && it->is_number() ? (int)it->get<double>() : d;
}
bool JB(const json& n, const char* k, bool d)
{
    auto it = n.find(k);
    return it != n.end() && it->is_boolean() ? it->get<bool>() : d;
}
std::string JS(const json& n, const char* k, const std::string& d)
{
    auto it = n.find(k);
    return it != n.end() && it->is_string() ? it->get<std::string>() : d;
}
ImVec2 JV2(const json& n, const char* k, ImVec2 d)
{
    auto it = n.find(k);
    if (it == n.end() || !it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number()) return d;
    return ImVec2((*it)[0].get<float>(), (*it)[1].get<float>());
}
bool JCol(const json& n, const char* k, ImVec4& out)
{
    auto it = n.find(k);
    return it != n.end() && ParseColor(*it, out);
}
std::vector<std::string> JItems(const json& n, const char* k)
{
    std::vector<std::string> v;
    auto it = n.find(k);
    if (it == n.end() || !it->is_array()) return v;
    for (auto& s : *it) v.push_back(s.is_string() ? s.get<std::string>() : s.dump());
    return v;
}

int KeyFromName(const std::string& name)
{
    if (name.empty() || name == "None") return ImGuiKey_None;
    static const struct { const char* a; const char* b; } alias[] = {
        {"Mouse1", "MouseLeft"}, {"Mouse2", "MouseRight"}, {"Mouse3", "MouseMiddle"}, {"Mouse4", "MouseX1"}, {"Mouse5", "MouseX2"},
        {"Ins", "Insert"}, {"Del", "Delete"}, {"Esc", "Escape"}, {"Shift", "LeftShift"}, {"Ctrl", "LeftCtrl"}, {"Alt", "LeftAlt"},
    };
    std::string n = name;
    for (auto& a : alias) if (n == a.a) n = a.b;
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++)
        if (strcasecmp(ImGui::GetKeyName((ImGuiKey)k), n.c_str()) == 0) return k;
    return ImGuiKey_None;
}

std::string KeyName(int key) { return key == ImGuiKey_None ? "None" : ImGui::GetKeyName((ImGuiKey)key); }

// ---------------------------------------------------------------- render

namespace {

// Applies a node's "style" override for the lifetime of the scope.
struct StyleScope {
    bool active = false;
    ImGuiStyle saved;
    mm::Theme saved_mm;
    explicit StyleScope(const json& n)
    {
        auto it = n.find("style");
        if (it == n.end() || !it->is_object() || it->empty()) return;
        active = true;
        saved = ImGui::GetStyle();
        saved_mm = mm::GetTheme();
        ApplyStyleOverride(*it, ImGui::GetStyle(), mm::GetTheme());
    }
    ~StyleScope()
    {
        if (!active) return;
        ImGui::GetStyle() = saved;
        mm::GetTheme() = saved_mm;
    }
};

int Look(const json& n)
{
    const std::string l = JS(n, "look", "default");
    if (JS(n, "type") == "checkbox") return l == "filled" ? mm::CheckboxStyle_Filled : l == "circle" ? mm::CheckboxStyle_Circle : 0;
    return l == "thin" ? mm::SliderStyle_Thin : l == "filled" ? mm::SliderStyle_Filled : l == "line" ? mm::SliderStyle_Line : 0;
}

void Place(const json& n)
{
    auto it = n.find("pos");
    if (it != n.end() && it->is_array()) { ImGui::SetCursorPos(JV2(n, "pos", ImVec2(0, 0))); return; }
    if (JB(n, "same_line", false)) ImGui::SameLine();
    it = n.find("offset");
    if (it != n.end() && it->is_array()) ImGui::SetCursorPos(ImGui::GetCursorPos() + JV2(n, "offset", ImVec2(0, 0)));
}

struct R {
    json& doc;
    Assets& assets;
    PreviewCtx& ctx;

    std::string Label(const json& n, const char* key = "label")
    {
        std::string label = JS(n, key);
        const char* icon = IconUtf8(JS(n, "icon"));
        return std::string(mm::IconLabel(icon, label.c_str())) + "##" + JS(n, "id");
    }

    PreviewValue& Value(const json& n)
    {
        const std::string type = JS(n, "type");
        std::string sig = type + "|" + (n.contains("default") ? n["default"].dump() : "") + "|" +
                          std::to_string(JItems(n, "items").size()) + "|" + std::to_string(JI(n, "max_length", 64));
        PreviewValue& v = ctx.values[JS(n, "id")];
        if (v.sig == sig && !ctx.reset_values) return v;
        v = PreviewValue();
        v.sig = sig;
        if (type == "checkbox" || type == "toggle") v.b = JB(n, "default", false);
        else if (type == "slider_float") v.f = JF(n, "default", 0);
        else if (type == "progress") v.f = JF(n, "default", 0.5f);
        else if (type == "slider_int" || type == "combo" || type == "radio") v.i = JI(n, "default", 0);
        else if (type == "color") { ImVec4 c(1, 1, 1, 1); JCol(n, "default", c); memcpy(v.col, &c, sizeof(v.col)); }
        else if (type == "keybind") v.key = KeyFromName(JS(n, "default", "None"));
        else if (type == "multi_combo") {
            if (n.contains("default") && n["default"].is_array())
                for (size_t i = 0; i < n["default"].size() && i < 64; i++) v.flags[i] = n["default"][i].is_boolean() && n["default"][i].get<bool>();
        } else if (type == "input_text") {
            int len = JI(n, "max_length", 64);
            if (len < 1) len = 1;
            v.text.assign(len + 1, 0);
            std::string d = JS(n, "default");
            strncpy(v.text.data(), d.c_str(), len);
        }
        return v;
    }

    void Hook(const std::string& id, const ImRect& rect)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        ctx.rects[id] = {rect, w->ClipRect};
        if (!ctx.edit_mode || !ctx.hovered.empty()) return;
        if (ImGui::IsMouseHoveringRect(rect.Min, rect.Max) &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            ctx.hovered = id;
    }
    void HookItem(const std::string& id) { Hook(id, ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax())); }

    void Children(json& n)
    {
        if (!n.contains("children") || !n["children"].is_array()) return;
        for (auto& c : n["children"]) Node(c);
    }

    void Node(json& n)
    {
        if (!n.is_object() || JB(n, "hidden", false)) return;
        const std::string type = JS(n, "type");
        const std::string id = JS(n, "id");
        ImGui::PushID(id.c_str());
        StyleScope style(n);
        if (type == "group") Group(n);
        else if (type == "columns") Columns(n);
        else if (type == "tabs") Tabs(n);
        else if (type == "tab") Children(n); // stray tab outside a tab bar
        else Leaf(n, type, id);
        ImGui::PopID();
    }

    void Group(json& n)
    {
        Place(n);
        ImVec2 size = JV2(n, "size", ImVec2(0, 0));
        std::string title = JS(n, "title");
        const std::string ts = JS(n, "title_style", "inside");
        mm::BeginGroup(JS(n, "id").c_str(), title.c_str(), size, JB(n, "border", true),
                       ts == "border" ? mm::GroupTitle_Border : ts == "plain" ? mm::GroupTitle_Plain : mm::GroupTitle_Inside);
        Children(n);
        mm::EndGroup();
        HookItem(JS(n, "id"));
    }

    void Columns(json& n)
    {
        int count = JI(n, "count", 2);
        if (count < 1) count = 1;
        if (count > 16) count = 16;
        if (ImGui::BeginTable(JS(n, "id").c_str(), count, ImGuiTableFlags_SizingStretchSame)) {
            if (n.contains("children") && n["children"].is_array())
                for (auto& c : n["children"]) {
                    if (JB(c, "hidden", false)) continue;
                    ImGui::TableNextColumn();
                    Node(c);
                }
            ImGui::EndTable();
        }
        HookItem(JS(n, "id"));
    }

    void Tabs(json& n)
    {
        const std::string id = JS(n, "id");
        std::vector<json*> tabs;
        if (n.contains("children") && n["children"].is_array())
            for (auto& c : n["children"])
                if (c.is_object() && !JB(c, "hidden", false)) tabs.push_back(&c);
        std::vector<std::string> labels, icons;
        for (auto* t : tabs) { labels.push_back(JS(*t, "label", "Tab")); icons.push_back(IconUtf8(JS(*t, "icon"))); }
        std::vector<const char*> lp, ip;
        std::vector<ImTextureID> imgs;
        bool any_image = false;
        for (size_t i = 0; i < tabs.size(); i++) {
            lp.push_back(labels[i].c_str());
            ip.push_back(icons[i].c_str());
            imgs.push_back(assets.TexId(JS(*tabs[i], "image")));
            any_image |= imgs.back() != ImTextureID_Invalid;
        }

        auto sel_it = ctx.tab_sel.find(id);
        if (sel_it == ctx.tab_sel.end()) sel_it = ctx.tab_sel.emplace(id, JI(n, "default", 0)).first;
        int& sel = sel_it->second;

        mm::TabsOptions o;
        std::string style = JS(n, "style", "sidebar");
        o.style = style == "top" ? mm::TabsStyle_Top : style == "pills" ? mm::TabsStyle_Pills : mm::TabsStyle_Sidebar;
        o.sidebar_width = JF(n, "sidebar_width", 170);
        o.logo = assets.TexId(JS(n, "logo"));
        o.logo_size = JV2(n, "logo_size", ImVec2(64, 64));
        std::string header = JS(n, "header");
        o.header = header.c_str();
        o.header_font = assets.Font(JS(n, "header_font"));
        o.header_size = JF(n, "header_size", 0);
        const std::string hl = JS(n, "highlight", "bar");
        o.highlight = hl == "box" ? mm::TabHighlight_Box : hl == "fill" ? mm::TabHighlight_Fill : hl == "none" ? mm::TabHighlight_None : mm::TabHighlight_Bar;
        o.layout = JS(n, "layout", "row") == "stacked" ? mm::TabLayout_Stacked : mm::TabLayout_Row;
        o.icon_only = JB(n, "icon_only", false);
        o.icon_size = JF(n, "icon_size", 0);
        o.images = any_image ? imgs.data() : nullptr;
        o.image_size = JV2(n, "image_size", ImVec2(0, 0));
        o.tint_images = JB(n, "tint_images", true);
        o.tab_height = JF(n, "tab_height", 0);
        o.tab_gap = JF(n, "tab_gap", 4);
        o.separators = JB(n, "separators", false);
        o.sidebar_border = JB(n, "sidebar_border", false);
        o.sidebar_padding = JV2(n, "sidebar_padding", ImVec2(10, 12));

        const ImVec2 start = ImGui::GetCursorScreenPos();
        int clicked = mm::BeginTabs(id.c_str(), lp.data(), ip.data(), (int)tabs.size(), &sel, o);
        if (clicked >= 0 && ctx.edit_mode) ctx.clicked = JS(*tabs[clicked], "id");
        if (sel >= 0 && sel < (int)tabs.size()) {
            Children(*tabs[sel]);
            ImGuiWindow* content = ImGui::GetCurrentWindow();
            Hook(JS(*tabs[sel], "id"), content->Rect());
        }
        mm::EndTabs();
        Hook(id, ImRect(start, ImGui::GetItemRectMax()));
    }

    void Leaf(json& n, const std::string& type, const std::string& id)
    {
        using namespace ImGui;
        Place(n);
        const bool disabled = JB(n, "disabled", false);
        if (disabled) BeginDisabled();
        const float width = JF(n, "width", 0);
        if (width != 0) PushItemWidth(width < 0 ? -FLT_MIN : width);

        BeginGroup();
        if (type == "text") {
            ImVec4 col;
            bool has_col = JCol(n, "color", col);
            std::string a = JS(n, "align", "left");
            std::string text = JS(n, "text");
            mm::TextEx(text.c_str(), has_col ? &col : nullptr, assets.Font(JS(n, "font")), JF(n, "size", 0),
                       a == "center" ? 1 : a == "right" ? 2 : 0, JB(n, "wrap", false));
        } else if (type == "separator") {
            std::string l = JS(n, "label");
            if (l.empty()) Separator(); else SeparatorText(l.c_str());
        } else if (type == "spacing") {
            float h = JF(n, "height", 8);
            Dummy(JB(n, "same_line", false) ? ImVec2(h, 0) : ImVec2(0, h));
        } else if (type == "button") {
            Button(Label(n).c_str(), JV2(n, "size", ImVec2(0, 0)));
        } else if (type == "checkbox") {
            mm::CheckboxEx(Label(n).c_str(), &Value(n).b, Look(n), JF(n, "box_size", 0));
        } else if (type == "toggle") {
            mm::Toggle(Label(n).c_str(), &Value(n).b);
        } else if (type == "slider_float" || type == "slider_int") {
            const std::string full = Label(n);
            const int look = Look(n);
            const bool thin = look == mm::SliderStyle_Thin;
            if (thin && width == 0) PushItemWidth(-FLT_MIN);
            const char* l = thin ? full.c_str() : mm::LabelAbove(full.c_str(), width == 0);
            const float trh = JF(n, "track_height", 0);
            const bool markers = JB(n, "markers", false);
            if (type == "slider_float") mm::SliderFloatEx(l, &Value(n).f, JF(n, "min", 0), JF(n, "max", 100), JS(n, "format", "%.1f").c_str(), look, trh, markers);
            else mm::SliderIntEx(l, &Value(n).i, JI(n, "min", 0), JI(n, "max", 100), JS(n, "format", "%d").c_str(), look, trh, markers);
            if (thin && width == 0) PopItemWidth();
            if (!thin) mm::LabelAboveEnd();
        } else if (type == "rect") {
            ImVec4 c(0.55f, 0.36f, 0.96f, 1), c2(0, 0, 0, -1);
            JCol(n, "color", c);
            JCol(n, "color2", c2);
            mm::Rect(JV2(n, "size", ImVec2(-1, 2)), c, c2, JB(n, "vertical", false), JF(n, "rounding", 0));
        } else if (type == "combo" || type == "multi_combo" || type == "radio") {
            auto items = JItems(n, "items");
            std::vector<const char*> ip;
            for (auto& s : items) ip.push_back(s.c_str());
            PreviewValue& v = Value(n);
            if (type == "combo") {
                const std::string full = Label(n);
            const char* l = mm::LabelAbove(full.c_str(), width == 0);
                Combo(l, &v.i, ip.data(), (int)ip.size());
                mm::LabelAboveEnd();
            } else if (type == "multi_combo") {
                const std::string full = Label(n);
            const char* l = mm::LabelAbove(full.c_str(), width == 0);
                mm::MultiCombo(l, v.flags, ip.data(), (int)ImMin<size_t>(ip.size(), 64));
                mm::LabelAboveEnd();
            } else {
                std::string label = JS(n, "label");
                if (!label.empty()) TextUnformatted(mm::IconLabel(IconUtf8(JS(n, "icon")), label.c_str()));
                const bool horiz = JB(n, "horizontal", true);
                for (int i = 0; i < (int)ip.size(); i++) {
                    if (i > 0 && horiz) SameLine();
                    PushID(i);
                    RadioButton(ip[i], &v.i, i);
                    PopID();
                }
            }
        } else if (type == "color") {
            PreviewValue& v = Value(n);
            ImGuiColorEditFlags f = JB(n, "alpha", true) ? ImGuiColorEditFlags_AlphaBar : ImGuiColorEditFlags_NoAlpha;
            if (JB(n, "inline", false)) {
                std::string label = JS(n, "label");
                AlignTextToFramePadding();
                TextUnformatted(mm::IconLabel(IconUtf8(JS(n, "icon")), label.c_str()));
                SameLine();
                const float sq = GetFrameHeight();
                const float avail = GetContentRegionAvail().x;
                if (avail > sq) SetCursorPosX(GetCursorPosX() + avail - sq);
                ColorEdit4(("##" + id).c_str(), v.col, f | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            } else {
                const std::string full = Label(n);
            const char* l = mm::LabelAbove(full.c_str(), width == 0);
                ColorEdit4(l, v.col, f);
                mm::LabelAboveEnd();
            }
        } else if (type == "keybind") {
            mm::Keybind(Label(n).c_str(), &Value(n).key);
        } else if (type == "input_text") {
            PreviewValue& v = Value(n);
            const std::string full = Label(n);
            const char* l = mm::LabelAbove(full.c_str(), width == 0);
            InputTextWithHint(l, JS(n, "hint").c_str(), v.text.data(), v.text.size(), JB(n, "password", false) ? ImGuiInputTextFlags_Password : 0);
            mm::LabelAboveEnd();
        } else if (type == "image") {
            ImVec2 size = JV2(n, "size", ImVec2(64, 64));
            std::string a = JS(n, "align", "left");
            float avail = GetContentRegionAvail().x;
            if (a != "left" && avail > size.x) SetCursorPosX(GetCursorPosX() + (a == "center" ? (avail - size.x) * 0.5f : avail - size.x));
            ImTextureID tex = assets.TexId(JS(n, "image"));
            const float rounding = JF(n, "rounding", 0);
            Dummy(size);
            ImDrawList* dl = GetWindowDrawList();
            if (tex != ImTextureID_Invalid) {
                dl->AddImageRounded(tex, GetItemRectMin(), GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), GetColorU32(ImVec4(1, 1, 1, 1)), rounding);
            } else {
                dl->AddRect(GetItemRectMin(), GetItemRectMax(), GetColorU32(ImGuiCol_Border), rounding);
                const char* q = IconUtf8("image");
                ImVec2 ts = CalcTextSize(q);
                dl->AddText((GetItemRectMin() + GetItemRectMax() - ts) * 0.5f, GetColorU32(ImGuiCol_TextDisabled), q);
            }
        } else if (type == "progress") {
            std::string ov = JS(n, "overlay");
            ProgressBar(Value(n).f, JV2(n, "size", ImVec2(-1, 0)), ov.empty() ? nullptr : ov.c_str());
        } else {
            TextDisabled("[unknown widget '%s']", type.c_str());
        }
        EndGroup();

        if (width != 0) PopItemWidth();
        std::string tip = JS(n, "tooltip");
        if (!tip.empty()) SetItemTooltip("%s", tip.c_str());
        if (disabled) EndDisabled();
        HookItem(id);
    }

    void Window(json& w)
    {
        using namespace ImGui;
        const std::string id = JS(w, "id");
        const ImVec2 pos = ctx.origin + JV2(w, "pos", ImVec2(60, 60));
        const ImVec2 size = JV2(w, "size", ImVec2(720, 460));

        auto ap = ctx.applied_pos.find(id);
        if (ap == ctx.applied_pos.end() || ap->second.x != pos.x || ap->second.y != pos.y) {
            SetNextWindowPos(pos, ImGuiCond_Always);
            ctx.applied_pos[id] = pos;
        }
        auto as = ctx.applied_size.find(id);
        if (as == ctx.applied_size.end() || as->second.x != size.x || as->second.y != size.y) {
            SetNextWindowSize(size, ImGuiCond_Always);
            ctx.applied_size[id] = size;
        }

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
        if (JB(w, "no_title_bar", true)) flags |= ImGuiWindowFlags_NoTitleBar;
        if (JB(w, "no_resize", false)) flags |= ImGuiWindowFlags_NoResize;
        if (JB(w, "no_move", false)) flags |= ImGuiWindowFlags_NoMove;
        if (JB(w, "no_collapse", true)) flags |= ImGuiWindowFlags_NoCollapse;
        if (JB(w, "no_scrollbar", true)) flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        if (JB(w, "no_background", false)) flags |= ImGuiWindowFlags_NoBackground;

        StyleScope style(w);
        ImFont* font = assets.Font(JS(w, "font"));
        float fsize = assets.FontSize(JS(w, "font"));
        if (!font) { font = assets.DefaultFont(); fsize = assets.DefaultFontSize(); }
        PushFont(font, fsize);
        const bool fade = JB(w, "fade", true);
        if (fade) mm::PushWindowFade(id.c_str());
        const bool has_pad = w.contains("padding") && w["padding"].is_array();
        if (has_pad) PushStyleVar(ImGuiStyleVar_WindowPadding, JV2(w, "padding", ImVec2(0, 0)));
        std::string title = JS(w, "title", "Menu") + "###mm_" + id;
        Begin(title.c_str(), nullptr, flags);
        if (has_pad) PopStyleVar();

        Children(w);

        ImGuiWindow* win = GetCurrentWindow();
        Hook(id, win->Rect());
        const ImVec2 now_pos = GetWindowPos(), now_size = GetWindowSize();
        End();
        if (fade) mm::PopWindowFade();
        PopFont();

        if (now_pos.x != ctx.applied_pos[id].x || now_pos.y != ctx.applied_pos[id].y) {
            ctx.applied_pos[id] = now_pos;
            ImVec2 rel = now_pos - ctx.origin;
            w["pos"] = json::array({(int)rel.x, (int)rel.y});
            ctx.doc_changed = true;
        }
        if (now_size.x != ctx.applied_size[id].x || now_size.y != ctx.applied_size[id].y) {
            ctx.applied_size[id] = now_size;
            w["size"] = json::array({(int)now_size.x, (int)now_size.y});
            ctx.doc_changed = true;
        }
    }
};

} // namespace

void RenderPreview(json& doc, Assets& assets, PreviewCtx& ctx)
{
    ctx.hovered.clear();
    ctx.clicked.clear();
    ctx.doc_changed = false;
    ctx.rects.clear();
    if (!doc.contains("windows") || !doc["windows"].is_array()) return;

    // Reveal the selection: switch every tab bar on its path to the right tab.
    if (ctx.selection_changed && !ctx.selected.empty()) {
        auto path = PathTo(doc, ctx.selected);
        for (size_t i = 0; i + 1 < path.size(); i++) {
            NodeRef r = FindNode(doc, path[i]);
            if (!r || JS(*r.node, "type") != "tabs") continue;
            int idx = 0;
            for (auto& c : (*r.node)["children"]) {
                if (JB(c, "hidden", false)) continue;
                if (JS(c, "id") == path[i + 1]) { ctx.tab_sel[path[i]] = idx; break; }
                idx++;
            }
        }
    }
    ctx.selection_changed = false;

    // Swap in the menu's theme while drawing it.
    ImGuiStyle editor_style = ImGui::GetStyle();
    ImGuiStyle menu_style;
    ThemeFromJson(ActiveTheme(doc), menu_style, mm::GetTheme());
    menu_style.FontSizeBase = editor_style.FontSizeBase;
    menu_style.FontScaleMain = editor_style.FontScaleMain;
    menu_style.FontScaleDpi = editor_style.FontScaleDpi;
    ImGui::GetStyle() = menu_style;

    R r{doc, assets, ctx};
    for (auto& w : doc["windows"])
        if (w.is_object() && JS(w, "type") == "window" && !JB(w, "hidden", false))
            r.Window(w);

    ImGui::GetStyle() = editor_style;
    ctx.reset_values = false;

    if (ctx.edit_mode && ctx.clicked.empty() && !ctx.hovered.empty() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        ctx.clicked = ctx.hovered;
}

void DrawSelectionOverlay(const PreviewCtx& ctx, ImU32 accent)
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    auto draw = [&](const std::string& id, ImU32 col, float thick) {
        auto it = ctx.rects.find(id);
        if (it == ctx.rects.end()) return;
        dl->PushClipRect(it->second.clip.Min - ImVec2(2, 2), it->second.clip.Max + ImVec2(2, 2), false);
        dl->AddRect(it->second.rect.Min - ImVec2(2, 2), it->second.rect.Max + ImVec2(2, 2), col, 3.0f, 0, thick);
        dl->PopClipRect();
    };
    if (!ctx.edit_mode) return;
    if (!ctx.hovered.empty() && ctx.hovered != ctx.selected) draw(ctx.hovered, (accent & 0x00FFFFFF) | 0x90000000, 1.0f);
    if (!ctx.selected.empty()) draw(ctx.selected, accent, 2.0f);
}
