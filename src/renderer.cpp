#include "renderer.h"
#include "mm_widgets.h"
#include "theme.h"
#include <cfloat>
#include <cstring>
#include <strings.h>

// ---------------------------------------------------------------- keys / enums

int KeyFromName(const std::string& name)
{
    if (name.empty() || name == "None") return ImGuiKey_None;
    // a few names people actually type that ImGui spells differently
    static const struct { const char* typed; const char* imgui; } aliases[] = {
        {"Mouse1", "MouseLeft"}, {"Mouse2", "MouseRight"}, {"Mouse3", "MouseMiddle"}, {"Mouse4", "MouseX1"}, {"Mouse5", "MouseX2"},
        {"Ins", "Insert"}, {"Del", "Delete"}, {"Esc", "Escape"}, {"Shift", "LeftShift"}, {"Ctrl", "LeftCtrl"}, {"Alt", "LeftAlt"},
    };
    std::string n = name;
    for (auto& a : aliases)
        if (n == a.typed) n = a.imgui;
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++)
        if (strcasecmp(ImGui::GetKeyName((ImGuiKey)k), n.c_str()) == 0) return k;
    return ImGuiKey_None;
}

std::string KeyName(int key) { return key == ImGuiKey_None ? "None" : ImGui::GetKeyName((ImGuiKey)key); }

int CheckboxLook(const json& n)
{
    const std::string look = GetStr(n, "look", "default");
    return look == "filled" ? mm::CheckboxStyle_Filled : look == "circle" ? mm::CheckboxStyle_Circle : mm::CheckboxStyle_Default;
}

int SliderLook(const json& n)
{
    const std::string look = GetStr(n, "look", "default");
    if (look == "thin") return mm::SliderStyle_Thin;
    if (look == "filled") return mm::SliderStyle_Filled;
    if (look == "line") return mm::SliderStyle_Line;
    return mm::SliderStyle_Default;
}

int GroupTitleStyle(const json& n)
{
    const std::string s = GetStr(n, "title_style", "inside");
    return s == "border" ? mm::GroupTitle_Border : s == "plain" ? mm::GroupTitle_Plain : mm::GroupTitle_Inside;
}

int TabsStyle(const json& n)
{
    const std::string s = GetStr(n, "style", "sidebar");
    return s == "top" ? mm::TabsStyle_Top : s == "pills" ? mm::TabsStyle_Pills : mm::TabsStyle_Sidebar;
}

int TabHighlight(const json& n)
{
    const std::string s = GetStr(n, "highlight", "bar");
    if (s == "box") return mm::TabHighlight_Box;
    if (s == "fill") return mm::TabHighlight_Fill;
    if (s == "none") return mm::TabHighlight_None;
    return mm::TabHighlight_Bar;
}

int TextAlign(const json& n)
{
    const std::string a = GetStr(n, "align", "left");
    return a == "center" ? 1 : a == "right" ? 2 : 0;
}

// ---------------------------------------------------------------- rendering

namespace {

// Applies a node's "style" override while it's alive, then puts the old style back.
// ImGui reads the style while drawing each item, so swapping the whole struct
// around a node is enough to recolor just that node (and its kids).
struct StyleOverride {
    bool active = false;
    ImGuiStyle saved_style;
    mm::Theme saved_theme;

    explicit StyleOverride(const json& n)
    {
        auto it = n.find("style");
        if (it == n.end() || !it->is_object() || it->empty()) return;
        active = true;
        saved_style = ImGui::GetStyle();
        saved_theme = mm::GetTheme();
        ApplyStyleOverride(*it, ImGui::GetStyle(), mm::GetTheme());
    }
    ~StyleOverride()
    {
        if (!active) return;
        ImGui::GetStyle() = saved_style;
        mm::GetTheme() = saved_theme;
    }
};

// pos wins over everything, otherwise same_line + offset
void PlaceItem(const json& n)
{
    if (Has(n, "pos")) {
        ImGui::SetCursorPos(GetVec2(n, "pos", ImVec2(0, 0)));
        return;
    }
    if (GetBool(n, "same_line", false)) ImGui::SameLine();
    if (Has(n, "offset")) ImGui::SetCursorPos(ImGui::GetCursorPos() + GetVec2(n, "offset", ImVec2(0, 0)));
}

class MenuRenderer {
public:
    MenuRenderer(Assets& assets, PreviewState& state) : assets_(assets), state_(state) {}

    void Window(json& w)
    {
        using namespace ImGui;
        const std::string id = GetStr(w, "id");
        const ImVec2 pos = state_.origin + GetVec2(w, "pos", ImVec2(60, 60));
        const ImVec2 size = GetVec2(w, "size", ImVec2(720, 460));

        // Only force pos/size when the JSON changed. If we forced it every
        // frame you couldn't drag or resize the window with the mouse.
        auto ap = state_.applied_pos.find(id);
        if (ap == state_.applied_pos.end() || ap->second.x != pos.x || ap->second.y != pos.y) {
            SetNextWindowPos(pos, ImGuiCond_Always);
            state_.applied_pos[id] = pos;
        }
        auto as = state_.applied_size.find(id);
        if (as == state_.applied_size.end() || as->second.x != size.x || as->second.y != size.y) {
            SetNextWindowSize(size, ImGuiCond_Always);
            state_.applied_size[id] = size;
        }

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
        if (GetBool(w, "no_title_bar", true)) flags |= ImGuiWindowFlags_NoTitleBar;
        if (GetBool(w, "no_resize", false)) flags |= ImGuiWindowFlags_NoResize;
        if (GetBool(w, "no_move", false)) flags |= ImGuiWindowFlags_NoMove;
        if (GetBool(w, "no_collapse", true)) flags |= ImGuiWindowFlags_NoCollapse;
        if (GetBool(w, "no_scrollbar", true)) flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        if (GetBool(w, "no_background", false)) flags |= ImGuiWindowFlags_NoBackground;

        StyleOverride style(w);
        ImFont* font = assets_.Font(GetStr(w, "font"));
        float font_size = assets_.FontSize(GetStr(w, "font"));
        if (!font) {
            font = assets_.DefaultFont();
            font_size = assets_.DefaultFontSize();
        }
        PushFont(font, font_size);
        const bool fade = GetBool(w, "fade", true);
        if (fade) mm::PushWindowFade(id.c_str());
        const bool custom_padding = Has(w, "padding");
        if (custom_padding) PushStyleVar(ImGuiStyleVar_WindowPadding, GetVec2(w, "padding", ImVec2(0, 0)));
        const std::string title = GetStr(w, "title", "Menu") + "###mm_" + id; // ### so renaming doesn't reset the window
        Begin(title.c_str(), nullptr, flags);
        if (custom_padding) PopStyleVar();

        Children(w);

        Track(id, GetCurrentWindow()->Rect());
        const ImVec2 now_pos = GetWindowPos(), now_size = GetWindowSize();
        End();
        if (fade) mm::PopWindowFade();
        PopFont();

        // write mouse moves/resizes back into the doc
        if (now_pos.x != state_.applied_pos[id].x || now_pos.y != state_.applied_pos[id].y) {
            state_.applied_pos[id] = now_pos;
            const ImVec2 rel = now_pos - state_.origin;
            w["pos"] = json::array({(int)rel.x, (int)rel.y});
            state_.doc_changed = true;
        }
        if (now_size.x != state_.applied_size[id].x || now_size.y != state_.applied_size[id].y) {
            state_.applied_size[id] = now_size;
            w["size"] = json::array({(int)now_size.x, (int)now_size.y});
            state_.doc_changed = true;
        }
    }

private:
    Assets& assets_;
    PreviewState& state_;

    // "label##id" with the icon in front
    std::string Label(const json& n) const
    {
        const std::string label = GetStr(n, "label");
        return std::string(mm::IconLabel(IconUtf8(GetStr(n, "icon")), label.c_str())) + "##" + GetStr(n, "id");
    }

    // The live value for a widget. Gets reset when its default changes so
    // editing the default in the inspector shows up right away.
    PreviewValue& Value(const json& n)
    {
        const std::string type = GetStr(n, "type");
        const std::string signature = type + "|" + (n.contains("default") ? n["default"].dump() : "") + "|" +
                                      std::to_string(GetItems(n, "items").size()) + "|" + std::to_string(GetInt(n, "max_length", 64));
        PreviewValue& v = state_.values[GetStr(n, "id")];
        if (v.signature == signature && !state_.reset_values) return v;

        v = PreviewValue();
        v.signature = signature;
        if (type == "checkbox" || type == "toggle") v.b = GetBool(n, "default", false);
        else if (type == "slider_float") v.f = GetFloat(n, "default", 0);
        else if (type == "progress") v.f = GetFloat(n, "default", 0.5f);
        else if (type == "slider_int" || type == "combo" || type == "radio") v.i = GetInt(n, "default", 0);
        else if (type == "keybind") v.key = KeyFromName(GetStr(n, "default", "None"));
        else if (type == "color") {
            ImVec4 c(1, 1, 1, 1);
            GetColor(n, "default", c);
            memcpy(v.color, &c, sizeof(v.color));
        } else if (type == "multi_combo") {
            if (n.contains("default") && n["default"].is_array())
                for (size_t i = 0; i < n["default"].size() && i < 64; i++) v.flags[i] = n["default"][i].is_boolean() && n["default"][i].get<bool>();
        } else if (type == "input_text") {
            const int len = std::max(1, GetInt(n, "max_length", 64));
            v.text.assign(len + 1, 0);
            strncpy(v.text.data(), GetStr(n, "default").c_str(), len);
        }
        return v;
    }

    // Remembers where a node ended up on screen (for the selection outline)
    // and whether the mouse is on it. Kids get tracked before their parents,
    // so the first hit is always the innermost thing under the mouse.
    void Track(const std::string& id, const ImRect& rect)
    {
        state_.rects[id] = {rect, ImGui::GetCurrentWindow()->ClipRect};
        if (!state_.edit_mode || !state_.hovered.empty()) return;
        if (ImGui::IsMouseHoveringRect(rect.Min, rect.Max) &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            state_.hovered = id;
    }
    void TrackLastItem(const std::string& id) { Track(id, ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax())); }

    void Children(json& n)
    {
        if (!n.contains("children") || !n["children"].is_array()) return;
        for (auto& c : n["children"]) Node(c);
    }

    void Node(json& n)
    {
        if (!n.is_object() || GetBool(n, "hidden", false)) return;
        const std::string type = GetStr(n, "type");
        const std::string id = GetStr(n, "id");
        ImGui::PushID(id.c_str());
        StyleOverride style(n);
        if (type == "group") Group(n);
        else if (type == "columns") Columns(n);
        else if (type == "tabs") Tabs(n);
        else if (type == "tab") Children(n); // a tab outside a tab bar, just draw its stuff
        else Widget(n, type, id);
        ImGui::PopID();
    }

    void Group(json& n)
    {
        PlaceItem(n);
        const std::string id = GetStr(n, "id");
        const std::string title = GetStr(n, "title");
        mm::BeginGroup(id.c_str(), title.c_str(), GetVec2(n, "size", ImVec2(0, 0)), GetBool(n, "border", true), GroupTitleStyle(n));
        Children(n);
        mm::EndGroup();
        TrackLastItem(id);
    }

    void Columns(json& n)
    {
        const std::string id = GetStr(n, "id");
        const int count = std::clamp(GetInt(n, "count", 2), 1, 16);
        if (ImGui::BeginTable(id.c_str(), count, ImGuiTableFlags_SizingStretchSame)) {
            if (n.contains("children") && n["children"].is_array())
                for (auto& c : n["children"]) {
                    if (GetBool(c, "hidden", false)) continue;
                    ImGui::TableNextColumn();
                    Node(c);
                }
            ImGui::EndTable();
        }
        TrackLastItem(id);
    }

    void Tabs(json& n)
    {
        const std::string id = GetStr(n, "id");
        std::vector<json*> tabs;
        if (n.contains("children") && n["children"].is_array())
            for (auto& c : n["children"])
                if (c.is_object() && !GetBool(c, "hidden", false)) tabs.push_back(&c);

        // mm::BeginTabs wants plain C arrays, so keep the strings alive here
        std::vector<std::string> labels, icons;
        std::vector<const char*> label_ptrs, icon_ptrs;
        std::vector<ImTextureID> images;
        bool any_image = false;
        for (json* t : tabs) {
            labels.push_back(GetStr(*t, "label", "Tab"));
            icons.push_back(IconUtf8(GetStr(*t, "icon")));
            images.push_back(assets_.TexId(GetStr(*t, "image")));
            any_image |= images.back() != ImTextureID_Invalid;
        }
        for (size_t i = 0; i < tabs.size(); i++) {
            label_ptrs.push_back(labels[i].c_str());
            icon_ptrs.push_back(icons[i].c_str());
        }

        auto sel_it = state_.selected_tab.find(id);
        if (sel_it == state_.selected_tab.end()) sel_it = state_.selected_tab.emplace(id, GetInt(n, "default", 0)).first;
        int& selected = sel_it->second;

        const std::string header = GetStr(n, "header");
        mm::TabsOptions o;
        o.style = TabsStyle(n);
        o.highlight = TabHighlight(n);
        o.layout = GetStr(n, "layout", "row") == "stacked" ? mm::TabLayout_Stacked : mm::TabLayout_Row;
        o.icon_only = GetBool(n, "icon_only", false);
        o.icon_size = GetFloat(n, "icon_size", 0);
        o.images = any_image ? images.data() : nullptr;
        o.image_size = GetVec2(n, "image_size", ImVec2(0, 0));
        o.tint_images = GetBool(n, "tint_images", true);
        o.tab_height = GetFloat(n, "tab_height", 0);
        o.tab_gap = GetFloat(n, "tab_gap", 4);
        o.separators = GetBool(n, "separators", false);
        o.sidebar_border = GetBool(n, "sidebar_border", false);
        o.sidebar_padding = GetVec2(n, "sidebar_padding", ImVec2(10, 12));
        o.sidebar_width = GetFloat(n, "sidebar_width", 170);
        o.logo = assets_.TexId(GetStr(n, "logo"));
        o.logo_size = GetVec2(n, "logo_size", ImVec2(64, 64));
        o.header = header.c_str();
        o.header_font = assets_.Font(GetStr(n, "header_font"));
        o.header_size = GetFloat(n, "header_size", 0);

        const ImVec2 start = ImGui::GetCursorScreenPos();
        const int clicked = mm::BeginTabs(id.c_str(), label_ptrs.data(), icon_ptrs.data(), (int)tabs.size(), &selected, o);
        if (clicked >= 0 && state_.edit_mode) state_.clicked = GetStr(*tabs[clicked], "id");
        if (selected >= 0 && selected < (int)tabs.size()) {
            Children(*tabs[selected]);
            Track(GetStr(*tabs[selected], "id"), ImGui::GetCurrentWindow()->Rect());
        }
        mm::EndTabs();
        Track(id, ImRect(start, ImGui::GetItemRectMax()));
    }

    void Widget(json& n, const std::string& type, const std::string& id)
    {
        using namespace ImGui;
        PlaceItem(n);
        const bool disabled = GetBool(n, "disabled", false);
        const float width = GetFloat(n, "width", 0);
        if (disabled) BeginDisabled();
        if (width != 0) PushItemWidth(width < 0 ? -FLT_MIN : width);

        // The group makes multi-part widgets (label + control) count as one
        // item, so selection and tooltips cover the whole thing.
        BeginGroup();
        if (type == "text") TextWidget(n);
        else if (type == "separator") {
            const std::string label = GetStr(n, "label");
            if (label.empty()) Separator();
            else SeparatorText(label.c_str());
        } else if (type == "spacing") {
            const float h = GetFloat(n, "height", 8);
            Dummy(GetBool(n, "same_line", false) ? ImVec2(h, 0) : ImVec2(0, h));
        } else if (type == "button") {
            Button(Label(n).c_str(), GetVec2(n, "size", ImVec2(0, 0)));
        } else if (type == "checkbox") {
            mm::CheckboxEx(Label(n).c_str(), &Value(n).b, CheckboxLook(n), GetFloat(n, "box_size", 0));
        } else if (type == "toggle") {
            mm::Toggle(Label(n).c_str(), &Value(n).b);
        } else if (type == "slider_float" || type == "slider_int") {
            SliderWidget(n, type, width);
        } else if (type == "combo" || type == "multi_combo" || type == "radio") {
            ChoiceWidget(n, type, width);
        } else if (type == "color") {
            ColorWidget(n, id, width);
        } else if (type == "keybind") {
            mm::Keybind(Label(n).c_str(), &Value(n).key);
        } else if (type == "input_text") {
            PreviewValue& v = Value(n);
            const std::string label = Label(n);
            InputTextWithHint(mm::LabelAbove(label.c_str(), width == 0), GetStr(n, "hint").c_str(), v.text.data(), v.text.size(),
                              GetBool(n, "password", false) ? ImGuiInputTextFlags_Password : 0);
            mm::LabelAboveEnd();
        } else if (type == "rect") {
            ImVec4 c(0.55f, 0.36f, 0.96f, 1), c2(0, 0, 0, -1); // alpha -1 = no gradient
            GetColor(n, "color", c);
            GetColor(n, "color2", c2);
            mm::Rect(GetVec2(n, "size", ImVec2(-1, 2)), c, c2, GetBool(n, "vertical", false), GetFloat(n, "rounding", 0));
        } else if (type == "image") {
            ImageWidget(n);
        } else if (type == "progress") {
            const std::string overlay = GetStr(n, "overlay");
            ProgressBar(Value(n).f, GetVec2(n, "size", ImVec2(-1, 0)), overlay.empty() ? nullptr : overlay.c_str());
        } else {
            TextDisabled("[unknown widget '%s']", type.c_str());
        }
        EndGroup();

        if (width != 0) PopItemWidth();
        const std::string tooltip = GetStr(n, "tooltip");
        if (!tooltip.empty()) SetItemTooltip("%s", tooltip.c_str());
        if (disabled) EndDisabled();
        TrackLastItem(id);
    }

    void TextWidget(const json& n)
    {
        ImVec4 color;
        const bool has_color = GetColor(n, "color", color);
        const std::string text = GetStr(n, "text");
        mm::TextEx(text.c_str(), has_color ? &color : nullptr, assets_.Font(GetStr(n, "font")), GetFloat(n, "size", 0), TextAlign(n),
                   GetBool(n, "wrap", false));
    }

    void SliderWidget(const json& n, const std::string& type, float width)
    {
        const std::string label = Label(n);
        const int look = SliderLook(n);
        // the thin look draws its own label, every other look puts it above (or beside)
        const bool own_label = look == mm::SliderStyle_Thin;
        if (own_label && width == 0) ImGui::PushItemWidth(-FLT_MIN);
        const char* l = own_label ? label.c_str() : mm::LabelAbove(label.c_str(), width == 0);
        const float track = GetFloat(n, "track_height", 0);
        const bool markers = GetBool(n, "markers", false);
        if (type == "slider_float")
            mm::SliderFloatEx(l, &Value(n).f, GetFloat(n, "min", 0), GetFloat(n, "max", 100), GetStr(n, "format", "%.1f").c_str(), look, track, markers);
        else
            mm::SliderIntEx(l, &Value(n).i, GetInt(n, "min", 0), GetInt(n, "max", 100), GetStr(n, "format", "%d").c_str(), look, track, markers);
        if (own_label && width == 0) ImGui::PopItemWidth();
        if (!own_label) mm::LabelAboveEnd();
    }

    void ChoiceWidget(const json& n, const std::string& type, float width)
    {
        const auto items = GetItems(n, "items");
        std::vector<const char*> item_ptrs;
        for (auto& s : items) item_ptrs.push_back(s.c_str());
        PreviewValue& v = Value(n);

        if (type == "radio") {
            const std::string label = GetStr(n, "label");
            if (!label.empty()) ImGui::TextUnformatted(mm::IconLabel(IconUtf8(GetStr(n, "icon")), label.c_str()));
            const bool horizontal = GetBool(n, "horizontal", true);
            for (int i = 0; i < (int)item_ptrs.size(); i++) {
                if (i > 0 && horizontal) ImGui::SameLine();
                ImGui::PushID(i);
                ImGui::RadioButton(item_ptrs[i], &v.i, i);
                ImGui::PopID();
            }
            return;
        }
        const std::string label = Label(n);
        const char* l = mm::LabelAbove(label.c_str(), width == 0);
        if (type == "combo") ImGui::Combo(l, &v.i, item_ptrs.data(), (int)item_ptrs.size());
        else mm::MultiCombo(l, v.flags, item_ptrs.data(), (int)std::min<size_t>(item_ptrs.size(), 64));
        mm::LabelAboveEnd();
    }

    void ColorWidget(const json& n, const std::string& id, float width)
    {
        using namespace ImGui;
        PreviewValue& v = Value(n);
        const ImGuiColorEditFlags flags = GetBool(n, "alpha", true) ? ImGuiColorEditFlags_AlphaBar : ImGuiColorEditFlags_NoAlpha;
        if (GetBool(n, "inline", false)) {
            // label on the left, little color square pushed to the right edge
            const std::string label = GetStr(n, "label");
            AlignTextToFramePadding();
            TextUnformatted(mm::IconLabel(IconUtf8(GetStr(n, "icon")), label.c_str()));
            SameLine();
            const float square = GetFrameHeight();
            const float avail = GetContentRegionAvail().x;
            if (avail > square) SetCursorPosX(GetCursorPosX() + avail - square);
            ColorEdit4(("##" + id).c_str(), v.color, flags | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            return;
        }
        const std::string label = Label(n);
        ColorEdit4(mm::LabelAbove(label.c_str(), width == 0), v.color, flags);
        mm::LabelAboveEnd();
    }

    void ImageWidget(const json& n)
    {
        using namespace ImGui;
        const ImVec2 size = GetVec2(n, "size", ImVec2(64, 64));
        const int align = TextAlign(n);
        const float avail = GetContentRegionAvail().x;
        if (align != 0 && avail > size.x) SetCursorPosX(GetCursorPosX() + (align == 1 ? (avail - size.x) * 0.5f : avail - size.x));

        Dummy(size);
        ImDrawList* dl = GetWindowDrawList();
        const ImTextureID tex = assets_.TexId(GetStr(n, "image"));
        const float rounding = GetFloat(n, "rounding", 0);
        if (tex != ImTextureID_Invalid) {
            dl->AddImageRounded(tex, GetItemRectMin(), GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding);
        } else {
            // placeholder so you can still see and click it
            dl->AddRect(GetItemRectMin(), GetItemRectMax(), GetColorU32(ImGuiCol_Border), rounding);
            const char* icon = IconUtf8("image");
            const ImVec2 ts = CalcTextSize(icon);
            dl->AddText((GetItemRectMin() + GetItemRectMax() - ts) * 0.5f, GetColorU32(ImGuiCol_TextDisabled), icon);
        }
    }
};

// Switches tab bars so the selected node is actually on screen.
void RevealSelection(json& doc, PreviewState& state)
{
    const auto path = PathTo(doc, state.selected);
    for (size_t i = 0; i + 1 < path.size(); i++) {
        NodeRef r = FindNode(doc, path[i]);
        if (!r || GetStr(*r.node, "type") != "tabs") continue;
        int index = 0;
        for (auto& tab : (*r.node)["children"]) {
            if (GetBool(tab, "hidden", false)) continue;
            if (GetStr(tab, "id") == path[i + 1]) {
                state.selected_tab[path[i]] = index;
                break;
            }
            index++;
        }
    }
}

} // namespace

void RenderPreview(json& doc, Assets& assets, PreviewState& state)
{
    state.hovered.clear();
    state.clicked.clear();
    state.doc_changed = false;
    state.rects.clear();
    if (!doc.contains("windows") || !doc["windows"].is_array()) return;

    if (state.selection_changed && !state.selected.empty()) RevealSelection(doc, state);
    state.selection_changed = false;

    // Swap the editor's style for the menu's theme while we draw the menu.
    // Keep the editor's font scale though, otherwise HiDPI breaks.
    const ImGuiStyle editor_style = ImGui::GetStyle();
    ImGuiStyle menu_style;
    ThemeFromJson(ActiveTheme(doc), menu_style, mm::GetTheme());
    menu_style.FontSizeBase = editor_style.FontSizeBase;
    menu_style.FontScaleMain = editor_style.FontScaleMain;
    menu_style.FontScaleDpi = editor_style.FontScaleDpi;
    ImGui::GetStyle() = menu_style;

    MenuRenderer renderer(assets, state);
    for (auto& w : doc["windows"])
        if (w.is_object() && GetStr(w, "type") == "window" && !GetBool(w, "hidden", false)) renderer.Window(w);

    ImGui::GetStyle() = editor_style;
    state.reset_values = false;

    if (state.edit_mode && state.clicked.empty() && !state.hovered.empty() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        state.clicked = state.hovered;
}

void DrawSelectionOverlay(const PreviewState& state, ImU32 color)
{
    if (!state.edit_mode) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    auto outline = [&](const std::string& id, ImU32 col, float thickness) {
        auto it = state.rects.find(id);
        if (it == state.rects.end()) return;
        const auto& hit = it->second;
        dl->PushClipRect(hit.clip.Min - ImVec2(2, 2), hit.clip.Max + ImVec2(2, 2), false);
        dl->AddRect(hit.rect.Min - ImVec2(2, 2), hit.rect.Max + ImVec2(2, 2), col, 3.0f, 0, thickness);
        dl->PopClipRect();
    };
    if (!state.hovered.empty() && state.hovered != state.selected) outline(state.hovered, (color & 0x00FFFFFF) | 0x90000000, 1.0f);
    if (!state.selected.empty()) outline(state.selected, color, 2.0f);
}
