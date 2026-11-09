// The Hierarchy, Inspector, Theme, Assets and Code panels.
#include "editor.h"
#include "exporter.h"
#include "mm_widgets.h"
#include "theme.h"
#include "util.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;
using namespace ImGui;

static const ImVec4 kWarningColor(1, 0.6f, 0.4f, 1);

// What a node is called in the tree: its label/title/text, or the type name.
static std::string DisplayName(const json& n)
{
    for (const char* key : {"label", "title", "text", "header"}) {
        const std::string s = GetStr(n, key);
        if (!s.empty()) return s.size() > 40 ? s.substr(0, 40) + "..." : s;
    }
    const TypeDef* t = FindType(GetStr(n, "type"));
    return t ? t->name : GetStr(n, "type");
}

// ---------------------------------------------------------------- hierarchy

void Editor::HierarchyPanel()
{
    if (!Begin("Hierarchy")) {
        End();
        return;
    }

    if (Button(IconText("plus", "Add").c_str())) OpenPopup("add_menu");
    if (BeginPopup("add_menu")) {
        TextDisabled("Goes into the selection (or right after it)");
        for (auto& t : Types())
            if (Selectable(IconText(t.icon.c_str(), t.name).c_str())) AddNode(t.type);
        EndPopup();
    }

    BeginDisabled(selected_.empty());
    auto tool_button = [](const char* icon, const char* tip) {
        SameLine();
        const bool clicked = Button(IconUtf8(icon));
        SetItemTooltip("%s", tip);
        return clicked;
    };
    if (tool_button("clone", "Duplicate (Ctrl+D)")) DuplicateSelected();
    if (tool_button("trash", "Delete (Del)")) DeleteSelected();
    if (tool_button("arrow-up", "Move up")) MoveSelected(-1);
    if (tool_button("arrow-down", "Move down")) MoveSelected(1);
    if (json* n = Selected()) {
        const bool hidden = GetBool(*n, "hidden", false);
        if (tool_button(hidden ? "eye-slash" : "eye", hidden ? "Show" : "Hide")) {
            if (hidden) n->erase("hidden");
            else (*n)["hidden"] = true;
            MarkDirty();
        }
    }
    EndDisabled();
    Separator();

    BeginChild("tree");
    for (auto& w : doc_["windows"]) HierarchyNode(w);
    Dummy(ImVec2(0, 40)); // room to drop things after the last node
    EndChild();
    reveal_in_tree_.clear();
    scroll_tree_to_selection_ = false;
    End();
}

void Editor::HierarchyNode(json& n)
{
    if (!n.is_object()) return;
    const std::string id = GetStr(n, "id");
    const std::string type = GetStr(n, "type");
    const TypeDef* t = FindType(type);
    const bool container = t && t->container;
    const bool hidden = GetBool(n, "hidden", false);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_FramePadding;
    if (!container || !n.contains("children") || n["children"].empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (type == "window" || type == "tabs") flags |= ImGuiTreeNodeFlags_DefaultOpen;
    if (id == selected_) flags |= ImGuiTreeNodeFlags_Selected;
    if (reveal_in_tree_.count(id)) SetNextItemOpen(true);

    const std::string label = IconText(t ? t->icon.c_str() : "question", DisplayName(n));
    if (hidden) PushStyleColor(ImGuiCol_Text, GetStyle().Colors[ImGuiCol_TextDisabled]);
    PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    const bool open = TreeNodeEx(id.c_str(), flags, "%s", label.c_str());
    PopStyleVar();
    if (hidden) PopStyleColor();

    if (id == selected_ && scroll_tree_to_selection_) SetScrollHereY(0.4f);
    if (IsItemClicked() && !IsItemToggledOpen()) Select(id);
    if (IsItemHovered()) {
        SetItemTooltip("%s  (%s)", id.c_str(), type.c_str());
        tree_hovered_ = id;
    }

    // id in gray on the right, if there's room
    const float id_x = GetItemRectMax().x - CalcTextSize(id.c_str()).x - 6;
    if (id_x > GetItemRectMin().x + CalcTextSize(label.c_str()).x + 40)
        GetWindowDrawList()->AddText(ImVec2(id_x, GetItemRectMin().y + 2), GetColorU32(ImGuiCol_TextDisabled), id.c_str());

    if (BeginPopupContextItem()) {
        Select(id);
        if (container && BeginMenu(IconText("plus", "Add inside").c_str())) {
            for (auto& tt : Types()) {
                if (!CanContain(&n, tt.type) || !MenuItem(IconText(tt.icon.c_str(), tt.name).c_str())) continue;
                const json node = MakeNode(tt.type, doc_);
                Later([this, id, node] {
                    const NodeRef r = FindNode(doc_, id);
                    if (!r) return;
                    ChildArray(doc_, r.node)->push_back(node);
                    Select(GetStr(node, "id"));
                    MarkDirty();
                });
            }
            EndMenu();
        }
        if (MenuItem("Duplicate", "Ctrl+D")) Later([this] { DuplicateSelected(); });
        if (MenuItem("Delete", "Del")) Later([this] { DeleteSelected(); });
        if (MenuItem("Move up")) Later([this] { MoveSelected(-1); });
        if (MenuItem("Move down")) Later([this] { MoveSelected(1); });
        if (MenuItem(hidden ? "Show" : "Hide"))
            Later([this, id, hidden] {
                const NodeRef r = FindNode(doc_, id);
                if (!r) return;
                if (hidden) r.node->erase("hidden");
                else (*r.node)["hidden"] = true;
                MarkDirty();
            });
        if (MenuItem("Copy JSON", "Ctrl+C")) SetClipboardText(n.dump(2).c_str());
        if (MenuItem("Ask Claude about this...")) {
            prompt_ = "For \"" + id + "\": ";
            focus_claude_ = true;
        }
        EndPopup();
    }

    // drag a node onto a container to move it inside, onto anything else to move it after
    if (BeginDragDropSource()) {
        SetDragDropPayload("MM_NODE", id.c_str(), id.size() + 1);
        TextUnformatted(label.c_str());
        EndDragDropSource();
    }
    if (BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = AcceptDragDropPayload("MM_NODE")) {
            const std::string dragged = (const char*)payload->Data;
            const NodeRef d = FindNode(doc_, dragged);
            const bool into = d && container && CanContain(&n, GetStr(*d.node, "type"));
            Later([this, dragged, id, into] { MoveNode(dragged, id, into); });
        }
        EndDragDropTarget();
    }

    if (open) {
        if (container && n.contains("children"))
            for (auto& child : n["children"]) HierarchyNode(child);
        TreePop();
    }
}

// ---------------------------------------------------------------- inspector

void Editor::InspectorPanel()
{
    if (!Begin("Inspector")) {
        End();
        return;
    }
    json* n = Selected();
    if (!n) {
        TextDisabled("Nothing selected.");
        TextWrapped("Click a widget on the canvas or in the Hierarchy to edit it.");
        Spacing();
        SeparatorText("Menu");
        std::string name = GetStr(doc_, "name");
        SetNextItemWidth(-FLT_MIN);
        if (InputText("##name", &name)) {
            doc_["name"] = name;
            MarkDirty();
        }
        SetItemTooltip("Menu name");
        if (!load_warnings_.empty()) {
            SeparatorText("Fixed up when the file was loaded");
            for (auto& w : load_warnings_) TextColored(ImVec4(1, 0.8f, 0.4f, 1), "%s", w.c_str());
        }
        End();
        return;
    }

    const std::string type = GetStr(*n, "type");
    const TypeDef* t = FindType(type);
    PushFont(nullptr, GetStyle().FontSizeBase * 1.15f);
    TextUnformatted(IconText(t ? t->icon.c_str() : "question", t ? t->name : type).c_str());
    PopFont();

    // id field: renaming only applies when you press Enter or click away
    if (id_edit_for_ != selected_) {
        id_edit_ = selected_;
        id_edit_for_ = selected_;
    }
    AlignTextToFramePadding();
    TextDisabled("id");
    SameLine();
    SetNextItemWidth(-FLT_MIN);
    if (InputText("##id", &id_edit_, ImGuiInputTextFlags_EnterReturnsTrue) || (IsItemDeactivated() && id_edit_ != selected_)) {
        const std::string new_id = Slug(id_edit_);
        if (new_id != selected_) {
            if (FindNode(doc_, new_id)) {
                SetStatus("id '" + new_id + "' is already taken");
            } else {
                (*n)["id"] = new_id;
                selected_ = new_id;
                id_edit_for_.clear();
                MarkDirty();
            }
        }
        id_edit_ = selected_;
    }
    Spacing();

    if (!t) {
        TextColored(kWarningColor, "Unknown type '%s'", type.c_str());
    } else if (BeginTable("props", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
        TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, CalcTextSize("sidebar_width").x + 8);
        TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        for (auto& p : t->props) PropEditor(*n, p);
        EndTable();
    }

    Spacing();
    if (CollapsingHeader("Raw JSON")) {
        // the props table above can rename/replace the node, so look it up again
        n = Selected();
        if (n) {
            const std::string key = selected_ + "|" + committed_;
            if (raw_json_for_ != key && !IsAnyItemActive()) {
                json shown = *n;
                shown.erase("children"); // way too long to edit here
                raw_json_ = shown.dump(2);
                raw_json_for_ = key;
                raw_json_error_.clear();
            }
            TextDisabled("Every prop, including ones not listed above (children hidden)");
            InputTextMultiline("##raw", &raw_json_, ImVec2(-FLT_MIN, GetTextLineHeight() * 12));
            if (Button("Apply JSON")) {
                json edited = json::parse(raw_json_, nullptr, false);
                if (!edited.is_object()) {
                    raw_json_error_ = "That's not a JSON object";
                } else {
                    if (n->contains("children")) edited["children"] = (*n)["children"];
                    const std::string new_id = GetStr(edited, "id", selected_);
                    if (new_id != selected_ && FindNode(doc_, new_id)) {
                        raw_json_error_ = "That id is already taken";
                    } else {
                        *n = edited;
                        selected_ = new_id;
                        raw_json_for_.clear();
                        MarkDirty();
                    }
                }
            }
            if (!raw_json_error_.empty()) {
                SameLine();
                TextColored(kWarningColor, "%s", raw_json_error_.c_str());
            }
        }
    }
    End();
}

// One row of the inspector table: prop name on the left, an editor that fits
// its kind on the right, and an x to reset it if it's set.
void Editor::PropEditor(json& n, const PropDef& p)
{
    const bool is_set = n.contains(p.key);
    const json current = is_set ? n[p.key] : p.def;
    PushID(p.key.c_str());
    TableNextRow();
    TableNextColumn();
    AlignTextToFramePadding();
    if (is_set) TextUnformatted(p.key.c_str());
    else TextDisabled("%s", p.key.c_str()); // gray = using the default
    if (!p.help.empty()) SetItemTooltip("%s", p.help.c_str());

    TableNextColumn();
    const float reset_width = is_set ? GetFrameHeight() + GetStyle().ItemSpacing.x : 0;
    SetNextItemWidth(-FLT_MIN - reset_width);
    bool changed = false;
    json value = current;

    switch (p.kind) {
    case PropKind::Bool: {
        bool b = current.is_boolean() && current.get<bool>();
        if (Checkbox("##v", &b)) { value = b; changed = true; }
        break;
    }
    case PropKind::Int: {
        int v = current.is_number() ? (int)current.get<double>() : 0;
        const bool edited = p.max > p.min ? DragInt("##v", &v, 0.2f, (int)p.min, (int)p.max) : DragInt("##v", &v);
        if (edited) { value = v; changed = true; }
        break;
    }
    case PropKind::Float: {
        float v = current.is_number() ? current.get<float>() : 0.0f;
        const float speed = p.max > p.min ? (p.max - p.min) / 400.0f : 0.1f;
        if (DragFloat("##v", &v, speed, p.min, p.max, "%.3g")) { value = std::round(v * 1000.0f) / 1000.0f; changed = true; }
        break;
    }
    case PropKind::String:
    case PropKind::Key: {
        std::string s = current.is_string() ? current.get<std::string>() : (current.is_null() ? "" : current.dump());
        if (p.kind == PropKind::Key) {
            // click-to-bind button, plus the text field below for typing a name
            int key = KeyFromName(s);
            if (mm::Keybind("##bind", &key)) { value = KeyName(key); changed = true; }
            SameLine();
            SetNextItemWidth(-FLT_MIN - reset_width);
        }
        if (InputText("##v", &s)) { value = s; changed = true; }
        break;
    }
    case PropKind::Text: {
        std::string s = current.is_string() ? current.get<std::string>() : "";
        if (InputTextMultiline("##v", &s, ImVec2(-FLT_MIN - reset_width, GetTextLineHeight() * 3.5f))) { value = s; changed = true; }
        break;
    }
    case PropKind::Color: {
        ImVec4 c(1, 1, 1, 1);
        if (!ParseColor(current, c) && current.is_null()) {
            if (Button("Set color")) { value = json::array({1, 1, 1, 1}); changed = true; }
        } else if (ColorEdit4("##v", &c.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
            value = ColorToJson(c);
            changed = true;
        }
        break;
    }
    case PropKind::Vec2: {
        if (current.is_null()) {
            if (Button("Override")) { value = json::array({0, 0}); changed = true; }
            break;
        }
        ImVec2 v(0, 0);
        if (current.is_array() && current.size() == 2 && current[0].is_number() && current[1].is_number())
            v = ImVec2(current[0].get<float>(), current[1].get<float>());
        if (DragFloat2("##v", &v.x, 0.5f, -1, 4000, "%.0f")) { value = json::array({(int)v.x, (int)v.y}); changed = true; }
        break;
    }
    case PropKind::Items: {
        // one item per line is way faster to edit than a list widget
        std::string text;
        if (current.is_array())
            for (size_t i = 0; i < current.size(); i++) text += (i ? "\n" : "") + (current[i].is_string() ? current[i].get<std::string>() : current[i].dump());
        const float rows = (float)std::min<size_t>(current.size(), 6) + 1.5f;
        if (InputTextMultiline("##v", &text, ImVec2(-FLT_MIN - reset_width, GetTextLineHeight() * rows))) {
            json items = json::array();
            for (const std::string& line : SplitLines(text)) items.push_back(line);
            if (!text.empty() && text.back() == '\n') items.push_back(""); // keep a fresh empty line while typing
            value = items;
            changed = true;
        }
        SetItemTooltip("One item per line");
        break;
    }
    case PropKind::BoolList: {
        const auto items = GetItems(n, "items");
        json flags = current.is_array() ? current : json::array();
        for (size_t i = 0; i < items.size(); i++) {
            bool on = i < flags.size() && flags[i].is_boolean() && flags[i].get<bool>();
            PushID((int)i);
            if (Checkbox(items[i].c_str(), &on)) {
                while (flags.size() < items.size()) flags.push_back(false);
                flags[i] = on;
                value = flags;
                changed = true;
            }
            PopID();
        }
        break;
    }
    case PropKind::Enum: {
        const std::string s = current.is_string() ? current.get<std::string>() : "";
        if (BeginCombo("##v", s.c_str())) {
            for (auto& option : p.options)
                if (Selectable(option.c_str(), option == s)) { value = option; changed = true; }
            EndCombo();
        }
        break;
    }
    case PropKind::Image:
    case PropKind::Font: {
        const bool font = p.kind == PropKind::Font;
        const char* none_label = font ? "(default)" : "(none)";
        const std::string s = current.is_string() ? current.get<std::string>() : "";
        if (BeginCombo("##v", s.empty() ? none_label : s.c_str())) {
            if (Selectable(none_label, s.empty())) { value = ""; changed = true; }
            for (auto& entry : doc_[font ? "fonts" : "images"]) {
                const std::string id = GetStr(entry, "id");
                if (Selectable(id.c_str(), id == s)) { value = id; changed = true; }
            }
            EndCombo();
        }
        break;
    }
    case PropKind::Icon: {
        std::string s = current.is_string() ? current.get<std::string>() : "";
        const char* glyph = IconUtf8(s);
        if (Button(glyph[0] ? glyph : "...", ImVec2(GetFrameHeight() * 1.4f, 0))) OpenPopup("icon_picker");
        SetItemTooltip("Pick an icon");
        SameLine();
        SetNextItemWidth(-FLT_MIN - reset_width);
        if (InputText("##v", &s)) { value = s; changed = true; }
        if (IconPicker("icon_picker", s)) { value = s; changed = true; }
        break;
    }
    case PropKind::Style: {
        json overrides = current.is_object() ? current : json::object();
        if (StyleOverrideEditor(overrides)) {
            value = overrides.empty() ? json() : overrides;
            changed = true;
        }
        break;
    }
    }

    if (is_set) {
        SameLine();
        if (Button(IconUtf8("xmark"), ImVec2(GetFrameHeight(), 0))) {
            n.erase(p.key);
            MarkDirty();
        }
        SetItemTooltip("Reset to default");
    }
    if (changed) {
        // don't litter the file with empty strings / nulls that mean "default" anyway
        const bool empty_string = value.is_string() && value.get<std::string>().empty() && p.def.is_string() && p.def.get<std::string>().empty();
        if (value.is_null() || empty_string) n.erase(p.key);
        else n[p.key] = value;
        MarkDirty();
    }
    PopID();
}

// Editor for a node's "style" prop: lists the overrides it has, each with an
// x, plus a searchable popup to add more.
bool Editor::StyleOverrideEditor(json& o)
{
    // new overrides start from whatever the active theme has
    ImGuiStyle theme_style;
    mm::Theme theme_extras;
    ThemeFromJson(ActiveTheme(doc_), theme_style, theme_extras);

    bool changed = false;
    std::string remove_section, remove_key;
    auto remove_button = [&](const char* section, const std::string& key) {
        SameLine();
        PushID((std::string(section) + key).c_str());
        if (SmallButton(IconUtf8("xmark"))) {
            remove_section = section;
            remove_key = key;
        }
        PopID();
    };

    int count = 0;
    if (o.contains("colors") && o["colors"].is_object())
        for (auto& [key, v] : o["colors"].items()) {
            ImVec4 c(1, 1, 1, 1);
            ParseColor(v, c);
            PushID(("c" + key).c_str());
            if (ColorEdit4(key.c_str(), &c.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs)) {
                o["colors"][key] = ColorToJson(c);
                changed = true;
            }
            PopID();
            remove_button("colors", key);
            count++;
        }
    if (o.contains("vars") && o["vars"].is_object())
        for (auto& [key, v] : o["vars"].items()) {
            PushID(("v" + key).c_str());
            SetNextItemWidth(GetContentRegionAvail().x * 0.5f);
            if (v.is_array() && v.size() == 2) {
                float f[2] = {v[0].get<float>(), v[1].get<float>()};
                if (DragFloat2(key.c_str(), f, 0.1f, 0, 100, "%.1f")) { o["vars"][key] = json::array({f[0], f[1]}); changed = true; }
            } else if (v.is_number()) {
                float f = v.get<float>();
                if (DragFloat(key.c_str(), &f, 0.1f, 0, 100, "%.1f")) { o["vars"][key] = f; changed = true; }
            }
            PopID();
            remove_button("vars", key);
            count++;
        }
    if (o.contains("mm") && o["mm"].is_object())
        for (auto& [key, v] : o["mm"].items()) {
            PushID(("m" + key).c_str());
            ImVec4 c;
            if (v.is_boolean()) {
                bool b = v.get<bool>();
                if (Checkbox(key.c_str(), &b)) { o["mm"][key] = b; changed = true; }
            } else if (v.is_number()) {
                float f = v.get<float>();
                SetNextItemWidth(GetContentRegionAvail().x * 0.5f);
                if (DragFloat(key.c_str(), &f, 0.1f)) { o["mm"][key] = f; changed = true; }
            } else if (ParseColor(v, c) && ColorEdit4(key.c_str(), &c.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs)) {
                o["mm"][key] = ColorToJson(c);
                changed = true;
            }
            PopID();
            remove_button("mm", key);
            count++;
        }
    if (!remove_key.empty()) {
        o[remove_section].erase(remove_key);
        if (o[remove_section].empty()) o.erase(remove_section);
        changed = true;
    }

    if (SmallButton(IconText("plus", count ? "Add override" : "Override a color/size...").c_str())) {
        OpenPopup("add_override");
        override_filter_.clear();
    }
    SetNextWindowSizeConstraints(ImVec2(260, 0), ImVec2(400, 420));
    if (BeginPopup("add_override")) {
        if (IsWindowAppearing()) SetKeyboardFocusHere();
        InputTextWithHint("##filter", "Filter", &override_filter_);
        SeparatorText("Colors");
        for (int i = 0; i < ImGuiCol_COUNT; i++) {
            const char* name = GetStyleColorName(i);
            if (ContainsNoCase(name, override_filter_) && Selectable(name)) {
                o["colors"][name] = ColorToJson(theme_style.Colors[i]);
                changed = true;
            }
        }
        SeparatorText("Sizes");
        for (auto& v : StyleVars()) {
            if (!ContainsNoCase(v.name, override_filter_) || !Selectable(v.name)) continue;
            const float* f = (const float*)((const char*)&theme_style + v.offset);
            o["vars"][v.name] = v.floats == 1 ? json(f[0]) : json::array({f[0], f[1]});
            changed = true;
        }
        SeparatorText("Menu widgets");
        for (auto& f : ThemeFields()) {
            if (!ContainsNoCase(f.name, override_filter_) || !Selectable(f.name)) continue;
            const char* p = (const char*)&theme_extras + f.offset;
            if (f.kind == FieldKind::Color) o["mm"][f.name] = ColorToJson(*(const ImVec4*)p);
            else if (f.kind == FieldKind::Float) o["mm"][f.name] = *(const float*)p;
            else o["mm"][f.name] = *(const bool*)p;
            changed = true;
        }
        EndPopup();
    }
    return changed;
}

// Grid of every Font Awesome icon with a search box. Returns true when one gets picked.
bool Editor::IconPicker(const char* popup_id, std::string& icon)
{
    bool picked = false;
    SetNextWindowSize(ImVec2(420, 360) * content_scale * settings_.ui_scale, ImGuiCond_Appearing);
    if (BeginPopup(popup_id)) {
        if (IsWindowAppearing()) SetKeyboardFocusHere();
        SetNextItemWidth(-FLT_MIN);
        InputTextWithHint("##filter", "Search icons (gun, eye, shield...)", &icon_filter_);
        BeginChild("grid", ImVec2(0, GetTextLineHeight() * 18));
        const float cell = GetFrameHeight() * 1.6f;
        const int columns = std::max(1, (int)(GetContentRegionAvail().x / (cell + GetStyle().ItemSpacing.x)));
        if (Button("none", ImVec2(cell * 2 + GetStyle().ItemSpacing.x, cell))) {
            icon.clear();
            picked = true;
            CloseCurrentPopup();
        }
        int i = 2; // "none" takes two cells
        for (auto& e : AllIcons()) {
            if (strlen(e.name) <= 1) continue; // skip the plain 0-9/a-z glyphs
            if (!icon_filter_.empty() && !strstr(e.name, icon_filter_.c_str())) continue;
            if (i % columns != 0) SameLine();
            PushID(e.name);
            if (Button(Utf8(e.codepoint).c_str(), ImVec2(cell, cell))) {
                icon = e.name;
                picked = true;
                CloseCurrentPopup();
            }
            SetItemTooltip("%s", e.name);
            PopID();
            i++;
        }
        EndChild();
        EndPopup();
    }
    return picked;
}

// ---------------------------------------------------------------- theme

void Editor::ThemePanel()
{
    if (!Begin("Theme")) {
        End();
        return;
    }
    const std::string current = GetStr(doc_, "theme");
    SetNextItemWidth(-FLT_MIN);
    if (BeginCombo("##active", IconText("palette", current).c_str())) {
        for (auto& [name, theme] : doc_["themes"].items())
            if (Selectable(name.c_str(), name == current)) {
                doc_["theme"] = name;
                MarkDirty();
            }
        EndCombo();
    }

    auto unique_name = [&](const std::string& base) {
        std::string name = base;
        for (int i = 2; doc_["themes"].contains(name); i++) name = base + " " + std::to_string(i);
        return name;
    };
    auto add_theme = [&](const std::string& name, const json& theme) {
        doc_["themes"][name] = theme;
        doc_["theme"] = name;
        MarkDirty();
    };

    if (Button(IconText("plus", "From preset").c_str())) OpenPopup("presets");
    if (BeginPopup("presets")) {
        for (auto& preset : ThemePresetNames())
            if (Selectable(preset.c_str())) add_theme(unique_name(preset), ThemePreset(preset));
        EndPopup();
    }
    SameLine();
    if (Button(IconText("clone", "Duplicate").c_str())) add_theme(unique_name(current + " copy"), doc_["themes"][current]);
    SameLine();
    if (Button(IconText("pen", "Rename").c_str())) {
        rename_theme_to_ = current;
        OpenPopup("rename_theme");
    }
    if (BeginPopup("rename_theme")) {
        if (IsWindowAppearing()) SetKeyboardFocusHere();
        const bool entered = InputText("##name", &rename_theme_to_, ImGuiInputTextFlags_EnterReturnsTrue);
        if (entered && !rename_theme_to_.empty() && !doc_["themes"].contains(rename_theme_to_)) {
            // rebuild the object so the theme keeps its spot in the list
            json renamed = json::object();
            for (auto& [name, theme] : doc_["themes"].items()) renamed[name == current ? rename_theme_to_ : name] = theme;
            doc_["themes"] = renamed;
            doc_["theme"] = rename_theme_to_;
            MarkDirty();
            CloseCurrentPopup();
        }
        EndPopup();
    }
    SameLine();
    BeginDisabled(doc_["themes"].size() <= 1);
    if (Button(IconUtf8("trash"))) {
        doc_["themes"].erase(current);
        doc_["theme"] = doc_["themes"].begin().key();
        MarkDirty();
    }
    SetItemTooltip("Delete this theme");
    EndDisabled();

    json& theme = ActiveTheme(doc_);
    ImGuiStyle style;
    mm::Theme extras;
    ThemeFromJson(theme, style, extras);

    if (CollapsingHeader("Quick recolor", ImGuiTreeNodeFlags_DefaultOpen)) {
        TextDisabled("Rebuilds every color from an accent + background");
        ImVec4 accent = extras.accent;
        bool edited = ColorEdit3("Accent", &accent.x, ImGuiColorEditFlags_NoInputs);
        SameLine();
        edited |= ColorEdit3("Background", &recolor_bg_.x, ImGuiColorEditFlags_NoInputs);
        SameLine();
        edited |= Checkbox("Light", &recolor_light_);
        if (edited) {
            const json fresh = ThemeFromAccent(accent, recolor_bg_, recolor_light_);
            theme["colors"] = fresh["colors"];
            for (const char* key : {"accent", "sidebar_bg", "toggle_off", "text_dim", "group_title"}) theme["mm"][key] = fresh["mm"][key];
            MarkDirty();
        }
    }

    if (CollapsingHeader("Menu widgets & animation", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (auto& f : ThemeFields()) {
            char* p = (char*)&extras + f.offset;
            bool edited = false;
            if (f.kind == FieldKind::Color) edited = ColorEdit4(f.name, (float*)p, ImGuiColorEditFlags_AlphaBar);
            else if (f.kind == FieldKind::Float) edited = SliderFloat(f.name, (float*)p, f.min, f.max, "%.1f");
            else edited = Checkbox(f.name, (bool*)p);
            if (!edited) continue;
            if (f.kind == FieldKind::Color) theme["mm"][f.name] = ColorToJson(*(ImVec4*)p);
            else if (f.kind == FieldKind::Float) theme["mm"][f.name] = std::round(*(float*)p * 100) / 100;
            else theme["mm"][f.name] = *(bool*)p;
            MarkDirty();
        }
    }

    if (CollapsingHeader("Sizes & rounding")) {
        for (auto& v : StyleVars()) {
            float* f = (float*)((char*)&style + v.offset);
            const bool edited = v.floats == 1 ? SliderFloat(v.name, f, v.min, v.max, "%.1f") : SliderFloat2(v.name, f, v.min, v.max, "%.1f");
            if (!edited) continue;
            auto round1 = [](float x) { return std::round(x * 10) / 10; };
            theme["vars"][v.name] = v.floats == 1 ? json(round1(f[0])) : json::array({round1(f[0]), round1(f[1])});
            MarkDirty();
        }
    }

    if (CollapsingHeader("All colors")) {
        SetNextItemWidth(-FLT_MIN);
        InputTextWithHint("##filter", "Filter (Button, Frame...)", &color_filter_);
        for (int i = 0; i < ImGuiCol_COUNT; i++) {
            const char* name = GetStyleColorName(i);
            if (!ContainsNoCase(name, color_filter_)) continue;
            ImVec4 c = style.Colors[i];
            if (ColorEdit4(name, &c.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
                theme["colors"][name] = ColorToJson(c);
                MarkDirty();
            }
        }
    }
    End();
}

// ---------------------------------------------------------------- assets

// When a font or image id changes, everything pointing at the old id follows.
static void RenameReferences(json& nodes, const char* key, const std::string& from, const std::string& to)
{
    if (!nodes.is_array()) return;
    for (auto& n : nodes) {
        if (!n.is_object()) continue;
        if (GetStr(n, key) == from) n[key] = to;
        if (n.contains("children")) RenameReferences(n["children"], key, from, to);
    }
}

// Copies a file into <project>/assets/<subdir>/ and returns the project-relative path ("" on failure).
std::string Editor::CopyIntoAssets(const std::string& src_in, const char* subdir)
{
    const fs::path src(ExpandHome(src_in));
    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) {
        SetStatus("File not found: " + src.string());
        return "";
    }
    const fs::path dst = fs::path(project_dir_) / "assets" / subdir / src.filename();
    fs::create_directories(dst.parent_path(), ec);
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        SetStatus(ec.message());
        return "";
    }
    return (fs::path("assets") / subdir / src.filename()).string();
}

bool Editor::ImportFont(const std::string& src)
{
    const std::string rel = CopyIntoAssets(src, "fonts");
    if (rel.empty()) return false;
    const std::string base = Slug(fs::path(src).stem().string());
    std::string id = base;
    auto taken = [&](const std::string& candidate) {
        return std::any_of(doc_["fonts"].begin(), doc_["fonts"].end(), [&](const json& f) { return GetStr(f, "id") == candidate; });
    };
    for (int i = 2; taken(id); i++) id = base + "_" + std::to_string(i);
    doc_["fonts"].push_back({{"id", id}, {"path", rel}, {"size", 16}});
    MarkDirty();
    SetStatus("Added font '" + id + "'");
    return true;
}

void Editor::AssetsPanel()
{
    if (!Begin("Assets")) {
        End();
        return;
    }
    if (BeginTabBar("assets_tabs")) {
        if (BeginTabItem(IconText("font", "Fonts").c_str())) {
            FontsTab();
            EndTabItem();
        }
        if (BeginTabItem(IconText("image", "Images").c_str())) {
            ImagesTab();
            EndTabItem();
        }
        if (BeginTabItem(IconText("icons", "Icons").c_str())) {
            IconsTab();
            EndTabItem();
        }
        EndTabBar();
    }
    End();
}

void Editor::FontsTab()
{
    const auto files = assets_.ListFiles("assets/fonts", {".ttf", ".otf"});
    json& fonts = doc_["fonts"];
    int remove = -1;
    for (int i = 0; i < (int)fonts.size(); i++) {
        json& f = fonts[i];
        PushID(i);
        std::string id = GetStr(f, "id");
        SetNextItemWidth(GetContentRegionAvail().x * 0.3f);
        if (InputText("##id", &id, ImGuiInputTextFlags_EnterReturnsTrue) && !id.empty()) {
            const std::string old_id = GetStr(f, "id");
            f["id"] = Slug(id);
            RenameReferences(doc_["windows"], "font", old_id, Slug(id));
            RenameReferences(doc_["windows"], "header_font", old_id, Slug(id));
            MarkDirty();
        }
        SetItemTooltip(i == 0 ? "Font id (the first font is the default)" : "Font id");
        SameLine();
        const std::string path = GetStr(f, "path");
        SetNextItemWidth(GetContentRegionAvail().x - 110);
        if (BeginCombo("##path", fs::path(path).filename().string().c_str())) {
            for (auto& file : files)
                if (Selectable(fs::path(file).filename().string().c_str(), file == path)) {
                    f["path"] = file;
                    MarkDirty();
                }
            EndCombo();
        }
        SetItemTooltip("%s", path.c_str());
        SameLine();
        float size = GetFloat(f, "size", 16);
        SetNextItemWidth(60);
        if (DragFloat("##size", &size, 0.2f, 6, 96, "%.0f")) {
            f["size"] = (int)size;
            MarkDirty();
        }
        SameLine();
        if (Button(IconUtf8("trash"))) remove = i;
        PopID();
    }
    if (remove >= 0) {
        fonts.erase(fonts.begin() + remove);
        MarkDirty();
    }
    if (Button(IconText("plus", "Add font").c_str())) {
        fonts.push_back({{"id", "font_" + std::to_string(fonts.size() + 1)}, {"path", files.empty() ? "" : files[0]}, {"size", 16}});
        MarkDirty();
    }
    Separator();
    SetNextItemWidth(-90);
    InputTextWithHint("##import", "/path/to/font.ttf (or drop it on the window)", &import_path_);
    SameLine();
    if (Button("Import") && ImportFont(import_path_)) import_path_.clear();
    if (!assets_.font_error.empty()) TextColored(kWarningColor, "%s", assets_.font_error.c_str());
}

void Editor::ImagesTab()
{
    const auto files = assets_.ListFiles("assets/images", {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif"});
    json& images = doc_["images"];
    int remove = -1;
    for (int i = 0; i < (int)images.size(); i++) {
        json& image = images[i];
        PushID(i);
        std::string id = GetStr(image, "id");
        const Texture* tex = assets_.Image(id);
        const float thumb = GetFrameHeight() * 2;
        if (tex && tex->id() != ImTextureID_Invalid) ImageFit(tex->id(), tex->w, tex->h, thumb, thumb);
        else Dummy(ImVec2(thumb, thumb));
        SameLine();

        BeginGroup();
        SetNextItemWidth(GetContentRegionAvail().x - 40);
        if (InputText("##id", &id, ImGuiInputTextFlags_EnterReturnsTrue) && !id.empty()) {
            const std::string old_id = GetStr(image, "id");
            image["id"] = Slug(id);
            RenameReferences(doc_["windows"], "image", old_id, Slug(id));
            RenameReferences(doc_["windows"], "logo", old_id, Slug(id));
            MarkDirty();
        }
        SameLine();
        if (Button(IconUtf8("trash"))) remove = i;
        const std::string path = GetStr(image, "path");
        SetNextItemWidth(-FLT_MIN);
        if (BeginCombo("##path", path.c_str())) {
            for (auto& file : files)
                if (Selectable(file.c_str(), file == path)) {
                    image["path"] = file;
                    MarkDirty();
                }
            EndCombo();
        }
        if (tex && !tex->error.empty()) TextColored(kWarningColor, "%s", tex->error.c_str());
        EndGroup();
        PopID();
    }
    if (remove >= 0) {
        images.erase(images.begin() + remove);
        MarkDirty();
    }
    if (images.empty()) TextDisabled("Put images in assets/images, or import one below.");

    auto add_image = [&](const std::string& rel) {
        const std::string base = Slug(fs::path(rel).stem().string());
        std::string id = base;
        auto taken = [&](const std::string& candidate) {
            return std::any_of(images.begin(), images.end(), [&](const json& e) { return GetStr(e, "id") == candidate; });
        };
        for (int k = 2; taken(id); k++) id = base + "_" + std::to_string(k);
        images.push_back({{"id", id}, {"path", rel}});
        MarkDirty();
    };
    BeginDisabled(files.empty());
    if (Button(IconText("plus", "Add image").c_str())) add_image(files[0]);
    EndDisabled();
    Separator();
    SetNextItemWidth(-90);
    InputTextWithHint("##import", "/path/to/logo.png", &import_path_);
    SameLine();
    if (Button("Import")) {
        const std::string rel = CopyIntoAssets(import_path_, "images");
        if (!rel.empty()) {
            add_image(rel);
            import_path_.clear();
        }
    }
}

void Editor::IconsTab()
{
    json* sel = Selected();
    const TypeDef* t = sel ? FindType(GetStr(*sel, "type")) : nullptr;
    const bool can_set = t && FindProp(*t, "icon");
    TextDisabled(can_set ? "Click to set the selected widget's icon" : "Click to copy an icon's name");
    SetNextItemWidth(-FLT_MIN);
    InputTextWithHint("##filter", "Search icons", &icon_filter_);
    BeginChild("icons");
    const float cell = GetFrameHeight() * 1.6f;
    const int columns = std::max(1, (int)(GetContentRegionAvail().x / (cell + GetStyle().ItemSpacing.x)));
    int i = 0;
    for (auto& e : AllIcons()) {
        if (strlen(e.name) <= 1) continue;
        if (!icon_filter_.empty() && !strstr(e.name, icon_filter_.c_str())) continue;
        if (i % columns != 0) SameLine();
        PushID(e.name);
        if (Button(Utf8(e.codepoint).c_str(), ImVec2(cell, cell))) {
            if (can_set) {
                (*sel)["icon"] = e.name;
                MarkDirty();
            } else {
                SetClipboardText(e.name);
                SetStatus(std::string("Copied icon name: ") + e.name);
            }
        }
        SetItemTooltip("%s", e.name);
        PopID();
        i++;
    }
    EndChild();
}

// ---------------------------------------------------------------- code

void Editor::CodePanel()
{
    if (!Begin("Code")) {
        End();
        return;
    }
    if (code_cache_for_ != committed_) { // only regenerate after a real change
        code_cache_ = GenerateMenuHeader(doc_);
        code_cache_for_ = committed_;
    }
    if (Button(IconText("copy", "Copy").c_str())) SetClipboardText(code_cache_.c_str());
    SameLine();
    if (Button(IconText("file-export", "Export project...").c_str())) open_export_popup_ = true;
    SameLine();
    TextDisabled("menu_generated.h - live preview of the exported code");
    InputTextMultiline("##code", &code_cache_, ImVec2(-FLT_MIN, -FLT_MIN), ImGuiInputTextFlags_ReadOnly);
    End();
}
