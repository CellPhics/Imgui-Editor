#include "model.h"
#include "theme.h"
#include <cctype>
#include <cmath>
#include <set>

// ---------------------------------------------------------------- widget types

static const PropDef kStyleProp = {"style", PropKind::Style, nullptr, "Overrides for this node and everything inside it: {colors, vars, mm}"};

// Props almost every widget has. `label` adds label + icon, `value` adds the
// C++ variable name used by the exporter.
static std::vector<PropDef> CommonProps(bool label, const char* default_label, bool value)
{
    std::vector<PropDef> p;
    if (label) {
        p.push_back({"label", PropKind::String, default_label, "Text shown next to the widget"});
        p.push_back({"icon", PropKind::Icon, "", "Font Awesome icon name, e.g. crosshairs"});
    }
    p.push_back({"tooltip", PropKind::String, "", "Shown when hovering"});
    p.push_back({"same_line", PropKind::Bool, false, "Put it on the same row as the previous widget"});
    p.push_back({"pos", PropKind::Vec2, nullptr, "Exact position inside the parent, in pixels"});
    p.push_back({"offset", PropKind::Vec2, nullptr, "Nudge from where it would normally go, in pixels"});
    p.push_back({"width", PropKind::Float, 0.0f, "0 = default, -1 = fill, >0 = pixels", -1, 800});
    p.push_back({"disabled", PropKind::Bool, false, "Greyed out and not clickable"});
    p.push_back({"hidden", PropKind::Bool, false, "Not drawn"});
    p.push_back(kStyleProp);
    if (value) p.push_back({"var", PropKind::String, "", "Variable name in the exported State struct (default: the id)"});
    return p;
}

static std::vector<PropDef> Concat(std::vector<PropDef> a, const std::vector<PropDef>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

static std::vector<PropDef> SliderProps(bool is_float)
{
    const char* looks = "default | thin (label + value on one row, thin track) | filled (bar fills up) | line (thin gradient bar, value under it)";
    return {
        {"min", is_float ? PropKind::Float : PropKind::Int, is_float ? json(0.0f) : json(0), "", -10000, 10000},
        {"max", is_float ? PropKind::Float : PropKind::Int, is_float ? json(100.0f) : json(100), "", -10000, 10000},
        {"default", is_float ? PropKind::Float : PropKind::Int, is_float ? json(50.0f) : json(50), "", -10000, 10000},
        {"format", PropKind::String, is_float ? "%.1f" : "%d", "printf format, e.g. \"%.0f deg\""},
        {"look", PropKind::Enum, "default", looks, 0, 0, {"default", "thin", "filled", "line"}},
        {"track_height", PropKind::Float, 0.0f, "Bar height for thin/line (0 = auto)", 0, 40},
        {"markers", PropKind::Bool, false, "Draw - and + at the ends (thin/line)"},
    };
}

static std::vector<TypeDef> BuildTypes()
{
    std::vector<TypeDef> t;

    t.push_back({"window", "Window", "window-maximize", true, "", false, {
        {"title", PropKind::String, "Menu", "Window title"},
        {"pos", PropKind::Vec2, json::array({60, 60}), "Position (relative to the canvas in the editor)"},
        {"size", PropKind::Vec2, json::array({720, 460}), "Size in pixels"},
        {"no_title_bar", PropKind::Bool, true, ""},
        {"no_resize", PropKind::Bool, false, ""},
        {"no_move", PropKind::Bool, false, ""},
        {"no_collapse", PropKind::Bool, true, ""},
        {"no_scrollbar", PropKind::Bool, true, ""},
        {"no_background", PropKind::Bool, false, ""},
        {"padding", PropKind::Vec2, nullptr, "Window padding (null = theme). [0,0] for edge-to-edge sidebars"},
        {"fade", PropKind::Bool, true, "Fade in when it opens"},
        {"font", PropKind::Font, "", "Font for this window (empty = first font)"},
        {"hidden", PropKind::Bool, false, ""},
        kStyleProp,
    }});

    t.push_back({"tabs", "Tab bar", "folder-tree", true, "tab", true, {
        {"style", PropKind::Enum, "sidebar", "sidebar | top | pills", 0, 0, {"sidebar", "top", "pills"}},
        {"sidebar_width", PropKind::Float, 170.0f, "", 60, 400},
        {"logo", PropKind::Image, "", "Image shown at the top of the sidebar"},
        {"logo_size", PropKind::Vec2, json::array({64, 64}), ""},
        {"header", PropKind::String, "", "Text at the top of the sidebar (brand name)"},
        {"header_font", PropKind::Font, "", ""},
        {"header_size", PropKind::Float, 0.0f, "0 = font default", 0, 96},
        {"highlight", PropKind::Enum, "bar", "How the selected tab looks: bar | box | fill | none", 0, 0, {"bar", "box", "fill", "none"}},
        {"layout", PropKind::Enum, "row", "row (icon next to label) | stacked (icon above label)", 0, 0, {"row", "stacked"}},
        {"icon_only", PropKind::Bool, false, "Hide labels, center the icon/image"},
        {"icon_size", PropKind::Float, 0.0f, "Icon size in px (0 = font size)", 0, 128},
        {"image_size", PropKind::Vec2, nullptr, "Size of tab images (default: icon_size square)"},
        {"tint_images", PropKind::Bool, true, "Color tab images like the tab text (dimmer when not selected)"},
        {"tab_height", PropKind::Float, 0.0f, "0 = theme", 0, 200},
        {"tab_gap", PropKind::Float, 4.0f, "Space between tabs", 0, 40},
        {"separators", PropKind::Bool, false, "Lines between tabs"},
        {"sidebar_border", PropKind::Bool, false, "Line on the sidebar's right edge"},
        {"sidebar_padding", PropKind::Vec2, json::array({10, 12}), ""},
        {"default", PropKind::Int, 0, "Tab selected at start", 0, 32},
        {"hidden", PropKind::Bool, false, ""},
        kStyleProp,
        {"var", PropKind::String, "", "Variable name for the selected tab index"},
    }});

    t.push_back({"tab", "Tab", "folder", true, "", false, {
        {"label", PropKind::String, "Tab", ""},
        {"icon", PropKind::Icon, "", "Font Awesome icon name"},
        {"image", PropKind::Image, "", "Image used instead of the icon"},
    }});

    t.push_back({"group", "Group box", "object-group", true, "", false, {
        {"title", PropKind::String, "Group", "Empty = no title"},
        {"title_style", PropKind::Enum, "inside", "inside (with accent underline) | border (title sits on the border) | plain", 0, 0, {"inside", "border", "plain"}},
        {"size", PropKind::Vec2, json::array({0, 0}), "0 width = fill, 0 height = fit contents"},
        {"border", PropKind::Bool, true, ""},
        {"same_line", PropKind::Bool, false, ""},
        {"pos", PropKind::Vec2, nullptr, "Exact position inside the parent"},
        {"offset", PropKind::Vec2, nullptr, "Nudge from the normal position"},
        {"hidden", PropKind::Bool, false, ""},
        kStyleProp,
    }});

    t.push_back({"columns", "Columns", "table-columns", true, "", false, {
        {"count", PropKind::Int, 2, "Kids fill the columns left to right", 1, 6},
        {"hidden", PropKind::Bool, false, ""},
        kStyleProp,
    }});

    t.push_back({"text", "Text", "font", false, "", false, Concat({
        {"text", PropKind::Text, "Text", ""},
        {"color", PropKind::Color, nullptr, "null = theme text color"},
        {"font", PropKind::Font, "", "Empty = window font"},
        {"size", PropKind::Float, 0.0f, "0 = default", 0, 96},
        {"align", PropKind::Enum, "left", "", 0, 0, {"left", "center", "right"}},
        {"wrap", PropKind::Bool, false, ""},
    }, CommonProps(false, "", false))});

    t.push_back({"separator", "Separator", "minus", false, "", false, {
        {"label", PropKind::String, "", "Optional text in the line"},
        {"hidden", PropKind::Bool, false, ""},
    }});

    t.push_back({"spacing", "Spacing", "arrows-up-down", false, "", false, {
        {"height", PropKind::Float, 8.0f, "", 0, 200},
        {"same_line", PropKind::Bool, false, ""},
        {"hidden", PropKind::Bool, false, ""},
    }});

    t.push_back({"button", "Button", "hand-pointer", false, "", false, Concat(CommonProps(true, "Button", false), {
        {"size", PropKind::Vec2, json::array({0, 0}), "0 = fit, -1 width = fill"},
    })});

    t.push_back({"checkbox", "Checkbox", "square-check", false, "", true, Concat(CommonProps(true, "Checkbox", true), {
        {"default", PropKind::Bool, false, ""},
        {"look", PropKind::Enum, "default", "default (check mark) | filled (solid square) | circle", 0, 0, {"default", "filled", "circle"}},
        {"box_size", PropKind::Float, 0.0f, "Box size for filled/circle (0 = frame height)", 0, 64},
    })});

    t.push_back({"toggle", "Toggle switch", "toggle-on", false, "", true, Concat(CommonProps(true, "Toggle", true), {
        {"default", PropKind::Bool, false, ""},
    })});

    t.push_back({"slider_float", "Slider (float)", "sliders", false, "", true, Concat(CommonProps(true, "Slider", true), SliderProps(true))});
    t.push_back({"slider_int", "Slider (int)", "sliders", false, "", true, Concat(CommonProps(true, "Slider", true), SliderProps(false))});

    t.push_back({"combo", "Combo", "caret-down", false, "", true, Concat(CommonProps(true, "Combo", true), {
        {"items", PropKind::Items, json::array({"Option A", "Option B", "Option C"}), ""},
        {"default", PropKind::Int, 0, "Selected index", 0, 64},
    })});

    t.push_back({"multi_combo", "Multi combo", "list-check", false, "", true, Concat(CommonProps(true, "Multi", true), {
        {"items", PropKind::Items, json::array({"Head", "Chest", "Legs"}), ""},
        {"default", PropKind::BoolList, json::array({true, false, false}), "Which items start enabled"},
    })});

    t.push_back({"radio", "Radio buttons", "circle-dot", false, "", true, Concat(CommonProps(true, "", true), {
        {"items", PropKind::Items, json::array({"Low", "Medium", "High"}), ""},
        {"default", PropKind::Int, 0, "Selected index", 0, 64},
        {"horizontal", PropKind::Bool, true, ""},
    })});

    t.push_back({"color", "Color picker", "palette", false, "", true, Concat(CommonProps(true, "Color", true), {
        {"default", PropKind::Color, json::array({1, 1, 1, 1}), ""},
        {"alpha", PropKind::Bool, true, "Let the user edit alpha"},
        {"inline", PropKind::Bool, false, "Label on the left, just the color square on the right"},
    })});

    t.push_back({"keybind", "Keybind", "keyboard", false, "", true, Concat(CommonProps(true, "Key", true), {
        {"default", PropKind::Key, "None", "ImGuiKey name: F, Insert, MouseX1, ..."},
    })});

    t.push_back({"input_text", "Text input", "i-cursor", false, "", true, Concat(CommonProps(true, "Input", true), {
        {"hint", PropKind::String, "", "Placeholder text"},
        {"default", PropKind::String, "", ""},
        {"max_length", PropKind::Int, 64, "", 1, 1024},
        {"password", PropKind::Bool, false, ""},
    })});

    t.push_back({"rect", "Rectangle / bar", "square-full", false, "", false, Concat({
        {"size", PropKind::Vec2, json::array({-1, 2}), "Width -1 = fill"},
        {"color", PropKind::Color, json::array({0.55, 0.36, 0.96, 1}), ""},
        {"color2", PropKind::Color, nullptr, "Second color for a gradient (null = solid)"},
        {"vertical", PropKind::Bool, false, "Vertical gradient"},
        {"rounding", PropKind::Float, 0.0f, "", 0, 100},
    }, CommonProps(false, "", false))});

    t.push_back({"image", "Image", "image", false, "", false, Concat({
        {"image", PropKind::Image, "", "Which image (from the Images list)"},
        {"size", PropKind::Vec2, json::array({64, 64}), ""},
        {"rounding", PropKind::Float, 0.0f, "", 0, 200},
        {"align", PropKind::Enum, "left", "", 0, 0, {"left", "center", "right"}},
    }, CommonProps(false, "", false))});

    t.push_back({"progress", "Progress bar", "bars-progress", false, "", true, Concat(CommonProps(false, "", true), {
        {"default", PropKind::Float, 0.5f, "0..1", 0, 1},
        {"overlay", PropKind::String, "", "Text on the bar"},
        {"size", PropKind::Vec2, json::array({-1, 0}), ""},
    })});

    return t;
}

const std::vector<TypeDef>& Types()
{
    static const std::vector<TypeDef> types = BuildTypes();
    return types;
}

const TypeDef* FindType(const std::string& type)
{
    for (auto& t : Types())
        if (t.type == type) return &t;
    return nullptr;
}

const PropDef* FindProp(const TypeDef& t, const std::string& key)
{
    for (auto& p : t.props)
        if (p.key == key) return &p;
    return nullptr;
}

// ---------------------------------------------------------------- tree helpers

static bool FindIn(json& arr, json* parent, const std::string& id, NodeRef& out)
{
    if (!arr.is_array()) return false;
    for (int i = 0; i < (int)arr.size(); i++) {
        json& n = arr[i];
        if (!n.is_object()) continue;
        if (GetStr(n, "id") == id) {
            out = {&n, &arr, parent, i};
            return true;
        }
        if (n.contains("children") && FindIn(n["children"], &n, id, out)) return true;
    }
    return false;
}

NodeRef FindNode(json& doc, const std::string& id)
{
    NodeRef r;
    if (!id.empty() && doc.contains("windows")) FindIn(doc["windows"], nullptr, id, r);
    return r;
}

json* ChildArray(json& doc, json* container)
{
    if (!container) return &doc["windows"];
    if (!container->contains("children")) (*container)["children"] = json::array();
    return &(*container)["children"];
}

static bool PathIn(json& arr, const std::string& id, std::vector<std::string>& path)
{
    if (!arr.is_array()) return false;
    for (auto& n : arr) {
        if (!n.is_object()) continue;
        path.push_back(GetStr(n, "id"));
        if (path.back() == id) return true;
        if (n.contains("children") && PathIn(n["children"], id, path)) return true;
        path.pop_back();
    }
    return false;
}

std::vector<std::string> PathTo(json& doc, const std::string& id)
{
    std::vector<std::string> path;
    if (doc.contains("windows")) PathIn(doc["windows"], id, path);
    return path;
}

bool IsAncestor(json& doc, const std::string& ancestor, const std::string& id)
{
    const auto path = PathTo(doc, id);
    for (size_t i = 0; i + 1 < path.size(); i++)
        if (path[i] == ancestor) return true;
    return false;
}

bool CanContain(const json* container, const std::string& child_type)
{
    if (!container) return child_type == "window";
    if (child_type == "window") return false;
    const TypeDef* t = FindType(GetStr(*container, "type"));
    if (!t || !t->container) return false;
    if (!t->child_type.empty()) return t->child_type == child_type;
    return child_type != "tab"; // tabs only go in tab bars
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
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c)) out += (char)std::tolower(c);
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.empty()) out = "item";
    if (std::isdigit((unsigned char)out[0])) out = "n" + out; // ids double as C++ names
    return out;
}

std::string UniqueId(const json& doc, const std::string& base_name)
{
    std::set<std::string> ids;
    if (doc.contains("windows")) CollectIds(doc["windows"], ids);
    const std::string base = Slug(base_name);
    if (!ids.count(base)) return base;
    for (int i = 2;; i++) {
        std::string candidate = base + "_" + std::to_string(i);
        if (!ids.count(candidate)) return candidate;
    }
}

json MakeNode(const std::string& type, const json& doc, const std::string& label)
{
    const TypeDef* t = FindType(type);
    if (!t) return json::object();

    json node = json::object();
    node["id"] = UniqueId(doc, label.empty() ? type : label);
    node["type"] = type;

    // Only write the defaults that matter so new nodes stay short. Everything
    // else falls back to the default when it's read anyway.
    static const std::set<std::string> worth_writing = {
        "label", "title", "text", "items", "default", "min", "max", "size", "pos",
        "count", "height", "format", "border", "image", "color"};
    for (auto& p : t->props) {
        if (p.def.is_null()) continue;
        const bool window_flag = type == "window" && p.key.rfind("no_", 0) == 0;
        if (worth_writing.count(p.key) || window_flag) node[p.key] = p.def;
    }
    if (!label.empty()) {
        for (const char* k : {"label", "title", "text"})
            if (node.contains(k)) { node[k] = label; break; }
    }
    if (t->container) node["children"] = json::array();
    if (type == "tabs") node["children"].push_back(MakeNode("tab", doc, "General")); // empty tab bars look broken
    return node;
}

// Gives a node (and its kids) fresh ids. `taken` collects ids as we go so
// siblings in the copy don't clash with each other either.
static void GiveNewIds(json& n, json& taken)
{
    const std::string id = UniqueId(taken, GetStr(n, "id", GetStr(n, "type", "item")));
    n["id"] = id;
    taken["windows"].push_back(json{{"id", id}});
    if (n.contains("children"))
        for (auto& c : n["children"]) GiveNewIds(c, taken);
}

json CloneWithNewIds(const json& node, const json& doc)
{
    json copy = node;
    json taken = doc;
    GiveNewIds(copy, taken);
    return copy;
}

// ---------------------------------------------------------------- normalize

static void NormalizeNodes(json& arr, std::set<std::string>& seen, std::vector<std::string>& warnings, const std::string& where)
{
    if (!arr.is_array()) { arr = json::array(); return; }
    for (auto it = arr.begin(); it != arr.end();) {
        if (!it->is_object()) {
            warnings.push_back("Removed something that wasn't an object in " + where);
            it = arr.erase(it);
            continue;
        }
        json& n = *it;
        const std::string type = GetStr(n, "type");
        const TypeDef* t = FindType(type);
        if (!t) warnings.push_back("Unknown widget type '" + type + "' (id " + GetStr(n, "id", "?") + ")");

        std::string id = GetStr(n, "id");
        if (id.empty() || seen.count(id)) {
            const std::string base = Slug(id.empty() ? (type.empty() ? "item" : type) : id);
            std::string fixed = base;
            for (int i = 2; seen.count(fixed); i++) fixed = base + "_" + std::to_string(i);
            if (!id.empty()) warnings.push_back("Duplicate id '" + id + "' renamed to '" + fixed + "'");
            n["id"] = fixed;
            id = fixed;
        }
        seen.insert(id);

        if (t && t->container && !n.contains("children")) n["children"] = json::array();
        if (n.contains("children")) NormalizeNodes(n["children"], seen, warnings, id);
        ++it;
    }
}

std::vector<std::string> Normalize(json& doc)
{
    std::vector<std::string> warnings;
    if (!doc.is_object()) {
        doc = DefaultDoc();
        warnings.push_back("The file wasn't a JSON object, started over from the default menu");
        return warnings;
    }
    if (!doc.contains("version")) doc["version"] = 1;
    if (!doc.contains("name")) doc["name"] = "My Menu";
    if (!doc.contains("themes") || !doc["themes"].is_object() || doc["themes"].empty())
        doc["themes"] = json{{"Midnight", ThemePreset("Midnight")}};
    if (!doc["theme"].is_string() || !doc["themes"].contains(doc["theme"].get<std::string>()))
        doc["theme"] = doc["themes"].begin().key();
    if (!doc["fonts"].is_array()) doc["fonts"] = json::array();
    if (!doc["images"].is_array()) doc["images"] = json::array();
    if (!doc.contains("windows")) doc["windows"] = json::array();

    std::set<std::string> seen;
    NormalizeNodes(doc["windows"], seen, warnings, "windows");
    return warnings;
}

json& ActiveTheme(json& doc) { return doc["themes"][doc["theme"].get<std::string>()]; }

// ---------------------------------------------------------------- reading props

std::string GetStr(const json& n, const char* key, const std::string& fallback)
{
    auto it = n.find(key);
    return it != n.end() && it->is_string() ? it->get<std::string>() : fallback;
}

float GetFloat(const json& n, const char* key, float fallback)
{
    auto it = n.find(key);
    return it != n.end() && it->is_number() ? it->get<float>() : fallback;
}

int GetInt(const json& n, const char* key, int fallback)
{
    auto it = n.find(key);
    return it != n.end() && it->is_number() ? (int)it->get<double>() : fallback;
}

bool GetBool(const json& n, const char* key, bool fallback)
{
    auto it = n.find(key);
    return it != n.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

ImVec2 GetVec2(const json& n, const char* key, ImVec2 fallback)
{
    auto it = n.find(key);
    if (it == n.end() || !it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number()) return fallback;
    return ImVec2((*it)[0].get<float>(), (*it)[1].get<float>());
}

bool GetColor(const json& n, const char* key, ImVec4& out)
{
    auto it = n.find(key);
    return it != n.end() && ParseColor(*it, out);
}

std::vector<std::string> GetItems(const json& n, const char* key)
{
    std::vector<std::string> items;
    auto it = n.find(key);
    if (it == n.end() || !it->is_array()) return items;
    for (auto& s : *it) items.push_back(s.is_string() ? s.get<std::string>() : s.dump());
    return items;
}

bool Has(const json& n, const char* key)
{
    auto it = n.find(key);
    return it != n.end() && !it->is_null();
}

bool ParseColor(const json& j, ImVec4& out)
{
    try {
        if (j.is_array() && j.size() >= 3) {
            ImVec4 c(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j.size() > 3 ? j[3].get<float>() : 1.0f);
            // anything above 1 means someone wrote 0..255 values
            if (c.x > 1.0f || c.y > 1.0f || c.z > 1.0f) {
                c.x /= 255.0f; c.y /= 255.0f; c.z /= 255.0f;
                if (c.w > 1.0f) c.w /= 255.0f;
            }
            out = c;
            return true;
        }
        if (j.is_string()) {
            std::string s = j.get<std::string>();
            if (!s.empty() && s[0] == '#') s.erase(0, 1);
            if (s.size() != 6 && s.size() != 8) return false;
            unsigned v = std::stoul(s, nullptr, 16);
            if (s.size() == 6) v = (v << 8) | 0xFF;
            out = ImVec4(((v >> 24) & 255) / 255.0f, ((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f);
            return true;
        }
    } catch (...) {
        // bad value, just report "not a color"
    }
    return false;
}

json ColorToJson(const ImVec4& c)
{
    auto r = [](float v) { return std::round(v * 1000.0f) / 1000.0f; };
    return json::array({r(c.x), r(c.y), r(c.z), r(c.w)});
}
