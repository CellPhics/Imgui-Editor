#include "model.h"
#include "theme.h"
#include <cctype>
#include <set>

static std::vector<PropDef> Common(bool label, const char* label_def, bool value)
{
    std::vector<PropDef> p;
    if (label) {
        p.push_back({"label", PK::String, label_def, "Text shown next to the widget"});
        p.push_back({"icon", PK::Icon, "", "Font Awesome icon name, e.g. crosshairs"});
    }
    p.push_back({"tooltip", PK::String, "", "Shown when hovering"});
    p.push_back({"same_line", PK::Bool, false, "Place on the same line as the previous widget"});
    p.push_back({"pos", PK::Vec2, nullptr, "Exact position inside the parent (window/group local pixels)"});
    p.push_back({"offset", PK::Vec2, nullptr, "Nudge from the normal layout position, in pixels"});
    p.push_back({"width", PK::Float, 0.0f, "0 = default, -1 = fill, >0 pixels", -1, 800});
    p.push_back({"disabled", PK::Bool, false, "Greyed out and not interactive"});
    p.push_back({"hidden", PK::Bool, false, "Not drawn"});
    p.push_back({"style", PK::Style, nullptr, "Per-widget overrides: {colors, vars, mm}"});
    if (value) p.push_back({"var", PK::String, "", "C++ variable name in exported State (default: from id)"});
    return p;
}

template <class... T>
static std::vector<PropDef> Cat(std::vector<PropDef> a, T... rest)
{
    (a.insert(a.end(), rest.begin(), rest.end()), ...);
    return a;
}

static std::vector<TypeDef> BuildTypes()
{
    std::vector<TypeDef> t;
    t.push_back({"window", "Window", "window-maximize", true, "", false, {
        {"title", PK::String, "Menu", "Window title (also its ImGui id)"},
        {"pos", PK::Vec2, json::array({60, 60}), "Position (relative to canvas in the editor)"},
        {"size", PK::Vec2, json::array({720, 460}), "Size in pixels"},
        {"no_title_bar", PK::Bool, true, ""},
        {"no_resize", PK::Bool, false, ""},
        {"no_move", PK::Bool, false, ""},
        {"no_collapse", PK::Bool, true, ""},
        {"no_scrollbar", PK::Bool, true, ""},
        {"no_background", PK::Bool, false, ""},
        {"padding", PK::Vec2, nullptr, "Window padding override (null = theme). Use [0,0] for edge-to-edge sidebars"},
        {"fade", PK::Bool, true, "Fade in when opened"},
        {"font", PK::Font, "", "Default font id for this window (empty = first font)"},
        {"hidden", PK::Bool, false, ""},
        {"style", PK::Style, nullptr, "Overrides for this node and everything inside: {colors, vars, mm}"},
    }});
    t.push_back({"tabs", "Tab bar", "folder-tree", true, "tab", true, {
        {"style", PK::Enum, "sidebar", "sidebar | top | pills", 0, 0, {"sidebar", "top", "pills"}},
        {"sidebar_width", PK::Float, 170.0f, "Sidebar width", 60, 400},
        {"logo", PK::Image, "", "Image id shown at the top of the sidebar"},
        {"logo_size", PK::Vec2, json::array({64, 64}), ""},
        {"header", PK::String, "", "Text shown at the top of the sidebar (brand name)"},
        {"header_font", PK::Font, "", "Font id for the header"},
        {"header_size", PK::Float, 0.0f, "Header font size (0 = font default)", 0, 96},
        {"highlight", PK::Enum, "bar", "Selected tab: bar (accent tint + marker) | box (content-colored box with border lines) | fill (solid accent) | none", 0, 0, {"bar", "box", "fill", "none"}},
        {"layout", PK::Enum, "row", "row (icon beside label) | stacked (icon above label)", 0, 0, {"row", "stacked"}},
        {"icon_only", PK::Bool, false, "Hide labels, center the icon/image"},
        {"icon_size", PK::Float, 0.0f, "Icon size in px (0 = font size)", 0, 128},
        {"image_size", PK::Vec2, nullptr, "Size of tab images (default: icon_size square)"},
        {"tint_images", PK::Bool, true, "Color tab images like the tab text (dim when unselected)"},
        {"tab_height", PK::Float, 0.0f, "Tab height in px (0 = theme)", 0, 200},
        {"tab_gap", PK::Float, 4.0f, "Space between tabs", 0, 40},
        {"separators", PK::Bool, false, "Lines between tabs"},
        {"sidebar_border", PK::Bool, false, "Line on the sidebar's right edge"},
        {"sidebar_padding", PK::Vec2, json::array({10, 12}), "Padding inside the sidebar"},
        {"default", PK::Int, 0, "Initially selected tab", 0, 32},
        {"hidden", PK::Bool, false, ""},
        {"style", PK::Style, nullptr, "Overrides for this node and everything inside: {colors, vars, mm}"},
        {"var", PK::String, "", "C++ variable for the selected tab index"},
    }});
    t.push_back({"tab", "Tab", "folder", true, "", false, {
        {"label", PK::String, "Tab", ""},
        {"icon", PK::Icon, "", "Font Awesome icon name"},
        {"image", PK::Image, "", "Image id used instead of the icon (e.g. artwork cropped from a screenshot)"},
    }});
    t.push_back({"group", "Group box", "object-group", true, "", false, {
        {"title", PK::String, "Group", "Title drawn at the top (empty = none)"},
        {"title_style", PK::Enum, "inside", "inside (title + accent underline) | border (title set into the top border) | plain", 0, 0, {"inside", "border", "plain"}},
        {"size", PK::Vec2, json::array({0, 0}), "0 width = fill, 0 height = fit contents"},
        {"border", PK::Bool, true, ""},
        {"same_line", PK::Bool, false, ""},
        {"pos", PK::Vec2, nullptr, "Exact position inside the parent"},
        {"offset", PK::Vec2, nullptr, "Nudge from the normal layout position"},
        {"hidden", PK::Bool, false, ""},
        {"style", PK::Style, nullptr, "Overrides for this node and everything inside: {colors, vars, mm}"},
    }});
    t.push_back({"columns", "Columns", "table-columns", true, "", false, {
        {"count", PK::Int, 2, "Number of columns; children fill columns left to right", 1, 6},
        {"hidden", PK::Bool, false, ""},
        {"style", PK::Style, nullptr, "Overrides for this node and everything inside: {colors, vars, mm}"},
    }});
    t.push_back({"text", "Text", "font", false, "", false, Cat(std::vector<PropDef>{
        {"text", PK::Text, "Text", ""},
        {"color", PK::Color, nullptr, "null = theme text color"},
        {"font", PK::Font, "", "Font id (empty = window font)"},
        {"size", PK::Float, 0.0f, "Font size (0 = default)", 0, 96},
        {"align", PK::Enum, "left", "", 0, 0, {"left", "center", "right"}},
        {"wrap", PK::Bool, false, "Word wrap"},
    }, Common(false, "", false))});
    t.push_back({"separator", "Separator", "minus", false, "", false, {
        {"label", PK::String, "", "Optional text in the separator"},
        {"hidden", PK::Bool, false, ""},
    }});
    t.push_back({"spacing", "Spacing", "arrows-up-down", false, "", false, {
        {"height", PK::Float, 8.0f, "", 0, 200},
        {"same_line", PK::Bool, false, ""},
        {"hidden", PK::Bool, false, ""},
    }});
    t.push_back({"button", "Button", "hand-pointer", false, "", false, Cat(Common(true, "Button", false), std::vector<PropDef>{
        {"size", PK::Vec2, json::array({0, 0}), "0 = fit, -1 width = fill"},
    })});
    t.push_back({"checkbox", "Checkbox", "square-check", false, "", true, Cat(Common(true, "Checkbox", true), std::vector<PropDef>{
        {"default", PK::Bool, false, ""},
        {"look", PK::Enum, "default", "default (check mark) | filled (solid square) | circle", 0, 0, {"default", "filled", "circle"}},
        {"box_size", PK::Float, 0.0f, "Box size in px for filled/circle (0 = frame height)", 0, 64},
    })});
    t.push_back({"toggle", "Toggle switch", "toggle-on", false, "", true, Cat(Common(true, "Toggle", true), std::vector<PropDef>{
        {"default", PK::Bool, false, ""},
    })});
    t.push_back({"slider_float", "Slider (float)", "sliders", false, "", true, Cat(Common(true, "Slider", true), std::vector<PropDef>{
        {"min", PK::Float, 0.0f, "", -10000, 10000},
        {"max", PK::Float, 100.0f, "", -10000, 10000},
        {"default", PK::Float, 50.0f, "", -10000, 10000},
        {"format", PK::String, "%.1f", "printf format"},
        {"look", PK::Enum, "default", "default | thin (label+value row, thin track) | filled (bar fills up) | line (thin gradient bar, value under it)", 0, 0, {"default", "thin", "filled", "line"}},
        {"track_height", PK::Float, 0.0f, "Bar height for thin/line (0 = auto)", 0, 40},
        {"markers", PK::Bool, false, "Show - and + at the ends (thin/line)"},
    })});
    t.push_back({"slider_int", "Slider (int)", "sliders", false, "", true, Cat(Common(true, "Slider", true), std::vector<PropDef>{
        {"min", PK::Int, 0, "", -10000, 10000},
        {"max", PK::Int, 100, "", -10000, 10000},
        {"default", PK::Int, 50, "", -10000, 10000},
        {"format", PK::String, "%d", "printf format"},
        {"look", PK::Enum, "default", "default | thin (label+value row, thin track) | filled (bar fills up) | line (thin gradient bar, value under it)", 0, 0, {"default", "thin", "filled", "line"}},
        {"track_height", PK::Float, 0.0f, "Bar height for thin/line (0 = auto)", 0, 40},
        {"markers", PK::Bool, false, "Show - and + at the ends (thin/line)"},
    })});
    t.push_back({"combo", "Combo", "caret-down", false, "", true, Cat(Common(true, "Combo", true), std::vector<PropDef>{
        {"items", PK::Items, json::array({"Option A", "Option B", "Option C"}), ""},
        {"default", PK::Int, 0, "Selected index", 0, 64},
    })});
    t.push_back({"multi_combo", "Multi combo", "list-check", false, "", true, Cat(Common(true, "Multi", true), std::vector<PropDef>{
        {"items", PK::Items, json::array({"Head", "Chest", "Legs"}), ""},
        {"default", PK::BoolList, json::array({true, false, false}), "Enabled items"},
    })});
    t.push_back({"radio", "Radio buttons", "circle-dot", false, "", true, Cat(Common(true, "", true), std::vector<PropDef>{
        {"items", PK::Items, json::array({"Low", "Medium", "High"}), ""},
        {"default", PK::Int, 0, "Selected index", 0, 64},
        {"horizontal", PK::Bool, true, ""},
    })});
    t.push_back({"color", "Color picker", "palette", false, "", true, Cat(Common(true, "Color", true), std::vector<PropDef>{
        {"default", PK::Color, json::array({1, 1, 1, 1}), ""},
        {"alpha", PK::Bool, true, "Edit alpha"},
        {"inline", PK::Bool, false, "Only show the color square (label on the left)"},
    })});
    t.push_back({"keybind", "Keybind", "keyboard", false, "", true, Cat(Common(true, "Key", true), std::vector<PropDef>{
        {"default", PK::Key, "None", "ImGuiKey name: F, Insert, MouseX1, ..."},
    })});
    t.push_back({"input_text", "Text input", "i-cursor", false, "", true, Cat(Common(true, "Input", true), std::vector<PropDef>{
        {"hint", PK::String, "", "Placeholder"},
        {"default", PK::String, "", ""},
        {"max_length", PK::Int, 64, "", 1, 1024},
        {"password", PK::Bool, false, ""},
    })});
    t.push_back({"rect", "Rectangle / bar", "square-full", false, "", false, Cat(std::vector<PropDef>{
        {"size", PK::Vec2, json::array({-1, 2}), "Width -1 = fill"},
        {"color", PK::Color, json::array({0.55, 0.36, 0.96, 1}), ""},
        {"color2", PK::Color, nullptr, "Second color for a gradient (null = solid)"},
        {"vertical", PK::Bool, false, "Vertical gradient"},
        {"rounding", PK::Float, 0.0f, "", 0, 100},
    }, Common(false, "", false))});
    t.push_back({"image", "Image", "image", false, "", false, Cat(std::vector<PropDef>{
        {"image", PK::Image, "", "Image id from the Images list"},
        {"size", PK::Vec2, json::array({64, 64}), ""},
        {"rounding", PK::Float, 0.0f, "", 0, 200},
        {"align", PK::Enum, "left", "", 0, 0, {"left", "center", "right"}},
    }, Common(false, "", false))});
    t.push_back({"progress", "Progress bar", "bars-progress", false, "", true, Cat(Common(false, "", true), std::vector<PropDef>{
        {"default", PK::Float, 0.5f, "0..1", 0, 1},
        {"overlay", PK::String, "", "Text on the bar"},
        {"size", PK::Vec2, json::array({-1, 0}), ""},
    })});
    return t;
}

const std::vector<TypeDef>& Types()
{
    static std::vector<TypeDef> t = BuildTypes();
    return t;
}

const TypeDef* FindType(const std::string& type)
{
    for (auto& t : Types()) if (t.type == type) return &t;
    return nullptr;
}

const PropDef* FindProp(const TypeDef& t, const std::string& key)
{
    for (auto& p : t.props) if (p.key == key) return &p;
    return nullptr;
}

// ---------------------------------------------------------------- tree

static bool FindIn(json& arr, json* parent, const std::string& id, NodeRef& out)
{
    if (!arr.is_array()) return false;
    for (int i = 0; i < (int)arr.size(); i++) {
        json& n = arr[i];
        if (n.is_object() && n.value("id", "") == id) {
            out.node = &n; out.parent_array = &arr; out.parent = parent; out.index = i;
            return true;
        }
        if (n.is_object() && n.contains("children") && FindIn(n["children"], &n, id, out)) return true;
    }
    return false;
}

NodeRef FindNode(json& root, const std::string& id)
{
    NodeRef r;
    if (id.empty() || !root.contains("windows")) return r;
    FindIn(root["windows"], nullptr, id, r);
    return r;
}

json* ChildArray(json& root, json* container)
{
    if (!container) return &root["windows"];
    if (!container->contains("children")) (*container)["children"] = json::array();
    return &(*container)["children"];
}

static bool PathIn(json& arr, const std::string& id, std::vector<std::string>& path)
{
    if (!arr.is_array()) return false;
    for (auto& n : arr) {
        if (!n.is_object()) continue;
        path.push_back(n.value("id", ""));
        if (path.back() == id) return true;
        if (n.contains("children") && PathIn(n["children"], id, path)) return true;
        path.pop_back();
    }
    return false;
}

std::vector<std::string> PathTo(json& root, const std::string& id)
{
    std::vector<std::string> path;
    if (root.contains("windows")) PathIn(root["windows"], id, path);
    return path;
}

bool IsAncestor(json& root, const std::string& ancestor, const std::string& id)
{
    auto p = PathTo(root, id);
    for (size_t i = 0; i + 1 < p.size(); i++) if (p[i] == ancestor) return true;
    return false;
}

static void CollectIds(const json& arr, std::set<std::string>& ids)
{
    if (!arr.is_array()) return;
    for (auto& n : arr) {
        if (!n.is_object()) continue;
        if (n.contains("id") && n["id"].is_string()) ids.insert(n["id"].get<std::string>());
        if (n.contains("children")) CollectIds(n["children"], ids);
    }
}

std::string Slug(const std::string& s)
{
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c)) o += (char)std::tolower(c);
        else if (!o.empty() && o.back() != '_') o += '_';
    }
    while (!o.empty() && o.back() == '_') o.pop_back();
    if (o.empty()) o = "item";
    if (std::isdigit((unsigned char)o[0])) o = "n" + o;
    return o;
}

std::string UniqueId(const json& root, const std::string& base_in)
{
    std::set<std::string> ids;
    if (root.contains("windows")) CollectIds(root["windows"], ids);
    std::string base = Slug(base_in);
    if (!ids.count(base)) return base;
    for (int i = 2;; i++) {
        std::string c = base + "_" + std::to_string(i);
        if (!ids.count(c)) return c;
    }
}

json MakeNode(const std::string& type, const json& root, const std::string& label_hint)
{
    const TypeDef* t = FindType(type);
    json n = json::object();
    if (!t) return n;
    std::string hint = label_hint;
    for (auto& p : t->props) {
        if (p.def.is_null()) continue;
        // Keep new nodes small: only write the meaningful defaults.
        bool keep = p.key == "label" || p.key == "title" || p.key == "text" || p.key == "items" ||
                    p.key == "default" || p.key == "min" || p.key == "max" || p.key == "size" ||
                    p.key == "pos" || p.key == "style" || p.key == "count" || p.key == "height" ||
                    p.key == "format" || p.key == "border" || p.key == "image" || p.key == "color";
        if (type == "window" && (p.key.rfind("no_", 0) == 0)) keep = true;
        if (keep) n[p.key] = p.def;
    }
    if (!hint.empty()) {
        if (n.contains("label")) n["label"] = hint;
        else if (n.contains("title")) n["title"] = hint;
        else if (n.contains("text")) n["text"] = hint;
    }
    std::string base = hint.empty() ? type : hint;
    json out = json::object();
    out["id"] = UniqueId(root, base);
    out["type"] = type;
    for (auto& [k, v] : n.items()) out[k] = v;
    if (t->container) out["children"] = json::array();
    if (type == "tabs") {
        json tab = MakeNode("tab", root, "General");
        out["children"].push_back(tab);
    }
    return out;
}

static void ReId(json& n, json& scratch_root)
{
    // scratch_root accumulates ids so siblings don't collide
    std::string id = UniqueId(scratch_root, n.value("id", n.value("type", "item")));
    n["id"] = id;
    scratch_root["windows"].push_back(json{{"id", id}});
    if (n.contains("children"))
        for (auto& c : n["children"]) ReId(c, scratch_root);
}

json CloneWithNewIds(const json& node, const json& root)
{
    json copy = node;
    json scratch = root; // include existing ids
    ReId(copy, scratch);
    return copy;
}

bool CanContain(const json* container, const std::string& child_type)
{
    if (!container) return child_type == "window";
    if (child_type == "window") return false;
    const TypeDef* t = FindType(container->value("type", ""));
    if (!t || !t->container) return false;
    if (!t->child_type.empty()) return t->child_type == child_type;
    return child_type != "tab";
}

// ---------------------------------------------------------------- normalize

static void NormalizeArr(json& arr, std::set<std::string>& seen, std::vector<std::string>& warn, const std::string& where)
{
    if (!arr.is_array()) { arr = json::array(); return; }
    for (auto it = arr.begin(); it != arr.end();) {
        if (!it->is_object()) { warn.push_back("Removed non-object entry in " + where); it = arr.erase(it); continue; }
        json& n = *it;
        std::string type = n.value("type", "");
        const TypeDef* t = FindType(type);
        if (!t) warn.push_back("Unknown widget type '" + type + "' (id " + n.value("id", "?") + ")");
        std::string id = n.contains("id") && n["id"].is_string() ? n["id"].get<std::string>() : "";
        if (id.empty() || seen.count(id)) {
            std::string base = id.empty() ? (type.empty() ? "item" : type) : id;
            std::string nid = Slug(base);
            for (int i = 2; seen.count(nid); i++) nid = Slug(base) + "_" + std::to_string(i);
            if (!id.empty()) warn.push_back("Duplicate id '" + id + "' renamed to '" + nid + "'");
            n["id"] = nid;
            id = nid;
        }
        seen.insert(id);
        if (t && t->container && !n.contains("children")) n["children"] = json::array();
        if (n.contains("children")) NormalizeArr(n["children"], seen, warn, id);
        ++it;
    }
}

std::vector<std::string> Normalize(json& root)
{
    std::vector<std::string> warn;
    if (!root.is_object()) { root = DefaultDoc(); warn.push_back("Document was not an object; reset"); return warn; }
    if (!root.contains("version")) root["version"] = 1;
    if (!root.contains("name")) root["name"] = "My Menu";
    if (!root.contains("themes") || !root["themes"].is_object() || root["themes"].empty()) {
        root["themes"] = json::object();
        root["themes"]["Midnight"] = ThemePreset("Midnight");
    }
    if (!root.contains("theme") || !root["theme"].is_string() || !root["themes"].contains(root["theme"].get<std::string>()))
        root["theme"] = root["themes"].begin().key();
    if (!root.contains("fonts") || !root["fonts"].is_array()) root["fonts"] = json::array();
    if (!root.contains("images") || !root["images"].is_array()) root["images"] = json::array();
    if (!root.contains("windows")) root["windows"] = json::array();
    std::set<std::string> seen;
    NormalizeArr(root["windows"], seen, warn, "windows");
    return warn;
}

json& ActiveTheme(json& root)
{
    return root["themes"][root["theme"].get<std::string>()];
}

json DefaultDoc()
{
    json d = json::object();
    d["version"] = 1;
    d["name"] = "Nexus";
    d["theme"] = "Midnight";
    d["themes"] = json::object();
    for (const char* p : {"Midnight", "Crimson", "Cyber", "Emerald", "Gold"}) d["themes"][p] = ThemePreset(p);
    d["fonts"] = json::array({
        json{{"id", "main"}, {"path", "assets/fonts/Roboto-Medium.ttf"}, {"size", 15}},
        json{{"id", "title"}, {"path", "assets/fonts/Roboto-Medium.ttf"}, {"size", 24}},
    });
    d["images"] = json::array();
    d["windows"] = json::parse(R"([
  {
    "id": "main", "type": "window", "title": "Nexus", "pos": [80, 60], "size": [760, 480],
    "no_title_bar": true, "no_resize": false, "no_collapse": true, "no_scrollbar": true, "padding": [0, 0], "fade": true,
    "children": [
      {
        "id": "nav", "type": "tabs", "style": "sidebar", "sidebar_width": 170, "header": "NEXUS",
        "header_font": "title", "header_size": 26,
        "children": [
          { "id": "tab_aimbot", "type": "tab", "label": "Aimbot", "icon": "crosshairs", "children": [
            { "id": "aim_cols", "type": "columns", "count": 2, "children": [
              { "id": "grp_general", "type": "group", "title": "General", "children": [
                { "id": "aim_enabled", "type": "toggle", "label": "Enable aimbot", "default": true },
                { "id": "aim_key", "type": "keybind", "label": "Aim key", "default": "MouseX1" },
                { "id": "aim_fov", "type": "slider_float", "label": "FOV", "min": 1, "max": 180, "default": 90, "format": "%.0f deg" },
                { "id": "aim_smooth", "type": "slider_float", "label": "Smoothing", "min": 1, "max": 20, "default": 5 },
                { "id": "aim_bones", "type": "multi_combo", "label": "Bones", "items": ["Head", "Neck", "Chest", "Pelvis"], "default": [true, false, true, false] }
              ]},
              { "id": "grp_target", "type": "group", "title": "Targeting", "children": [
                { "id": "aim_vis_check", "type": "toggle", "label": "Visibility check", "default": true },
                { "id": "aim_team", "type": "toggle", "label": "Ignore teammates", "default": true },
                { "id": "aim_priority", "type": "combo", "label": "Priority", "items": ["Closest to crosshair", "Lowest health", "Distance"], "default": 0 },
                { "id": "aim_fov_circle", "type": "checkbox", "label": "Draw FOV circle", "default": false },
                { "id": "aim_fov_color", "type": "color", "label": "Circle color", "default": [0.55, 0.36, 0.96, 1.0], "inline": true, "same_line": false }
              ]}
            ]}
          ]},
          { "id": "tab_visuals", "type": "tab", "label": "Visuals", "icon": "eye", "children": [
            { "id": "vis_cols", "type": "columns", "count": 2, "children": [
              { "id": "grp_esp", "type": "group", "title": "ESP", "children": [
                { "id": "esp_boxes", "type": "toggle", "label": "Boxes", "default": true },
                { "id": "esp_box_color", "type": "color", "label": "Box color", "default": [1, 0.3, 0.4, 1], "inline": true },
                { "id": "esp_names", "type": "toggle", "label": "Names", "default": true },
                { "id": "esp_health", "type": "toggle", "label": "Health bars", "default": false },
                { "id": "esp_distance", "type": "slider_int", "label": "Max distance", "min": 50, "max": 1000, "default": 300, "format": "%d m" }
              ]},
              { "id": "grp_world", "type": "group", "title": "World", "children": [
                { "id": "world_nightmode", "type": "toggle", "label": "Night mode" },
                { "id": "world_fov", "type": "slider_int", "label": "Camera FOV", "min": 60, "max": 130, "default": 90 },
                { "id": "world_quality", "type": "radio", "items": ["Low", "Med", "High"], "default": 2 }
              ]}
            ]}
          ]},
          { "id": "tab_misc", "type": "tab", "label": "Misc", "icon": "bolt", "children": [
            { "id": "grp_misc", "type": "group", "title": "Movement", "children": [
              { "id": "misc_bhop", "type": "toggle", "label": "Bunny hop" },
              { "id": "misc_speed", "type": "slider_float", "label": "Speed multiplier", "min": 1, "max": 3, "default": 1, "format": "%.2fx" },
              { "id": "misc_name", "type": "input_text", "label": "Name", "hint": "Custom name..." }
            ]}
          ]},
          { "id": "tab_settings", "type": "tab", "label": "Settings", "icon": "gear", "children": [
            { "id": "grp_cfg", "type": "group", "title": "Config", "children": [
              { "id": "cfg_menu_key", "type": "keybind", "label": "Menu key", "default": "Insert" },
              { "id": "cfg_save", "type": "button", "label": "Save config", "icon": "floppy-disk", "size": [-1, 0] },
              { "id": "cfg_load", "type": "button", "label": "Load config", "icon": "folder-open", "size": [-1, 0] },
              { "id": "cfg_sep", "type": "separator" },
              { "id": "cfg_ver", "type": "text", "text": "v1.0 - made with ImGui Menu Editor", "align": "center" }
            ]}
          ]}
        ]
      }
    ]
  }
])");
    return d;
}
