// The menu document. A menu is one JSON file (menu.json):
//
//   { "name", "theme", "themes": {...}, "fonts": [...], "images": [...],
//     "windows": [ window -> tabs -> tab -> group/columns -> widgets ] }
//
// Every node is an object with a unique "id" and a "type". Containers keep
// their kids in "children". Everything else is just flat props on the node,
// which keeps the file easy to edit by hand (or by Claude).
#pragma once
#include "imgui.h"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>

// ordered_json so keys stay in the order they were written. Makes diffs and
// hand edits way less confusing.
using json = nlohmann::ordered_json;

// ---------------------------------------------------------------- widget types

// What kind of editor the inspector shows for a prop.
enum class PropKind {
    Bool, Int, Float, String, Text, Color, Vec2, Items, Enum, Image, Font, Icon, Key, BoolList, Style
};

struct PropDef {
    std::string key;
    PropKind kind;
    json def;          // default value, null = unset / optional
    std::string help;  // tooltip in the inspector
    float min = 0, max = 0;
    std::vector<std::string> options; // for PropKind::Enum
};

struct TypeDef {
    std::string type;          // what goes in "type"
    std::string name;          // what the UI shows
    std::string icon;          // Font Awesome icon name
    bool container = false;
    std::string child_type;    // if set, the only type allowed inside (tabs -> tab)
    bool has_value = false;    // gets a variable in the exported State struct
    std::vector<PropDef> props;
};

const std::vector<TypeDef>& Types();
const TypeDef* FindType(const std::string& type);
const PropDef* FindProp(const TypeDef& t, const std::string& key);

// ---------------------------------------------------------------- tree helpers

struct NodeRef {
    json* node = nullptr;
    json* parent_array = nullptr; // the array the node sits in
    json* parent = nullptr;       // parent node, null for windows
    int index = -1;
    explicit operator bool() const { return node != nullptr; }
};

NodeRef FindNode(json& doc, const std::string& id);
json* ChildArray(json& doc, json* container); // container == nullptr -> the windows array
std::vector<std::string> PathTo(json& doc, const std::string& id); // ids from the window down to `id`
bool IsAncestor(json& doc, const std::string& ancestor, const std::string& id);
bool CanContain(const json* container, const std::string& child_type);

std::string Slug(const std::string& s); // "Aim Key!" -> "aim_key"
std::string UniqueId(const json& doc, const std::string& base);
json MakeNode(const std::string& type, const json& doc, const std::string& label = "");
json CloneWithNewIds(const json& node, const json& doc);

// Fixes up whatever it can (missing ids, duplicate ids, missing keys) and
// returns a list of what it changed or didn't understand.
std::vector<std::string> Normalize(json& doc);
json& ActiveTheme(json& doc);
json DefaultDoc(); // the starter menu, see starter_menu.cpp

// ---------------------------------------------------------------- reading props
// These never throw: wrong type or missing key just gives you the fallback.

std::string GetStr(const json& n, const char* key, const std::string& fallback = "");
float GetFloat(const json& n, const char* key, float fallback);
int GetInt(const json& n, const char* key, int fallback);
bool GetBool(const json& n, const char* key, bool fallback);
ImVec2 GetVec2(const json& n, const char* key, ImVec2 fallback);
bool GetColor(const json& n, const char* key, ImVec4& out);
std::vector<std::string> GetItems(const json& n, const char* key);
bool Has(const json& n, const char* key); // key exists and isn't null

// Colors can be [r,g,b(,a)] as 0..1 floats, 0..255 ints, or "#RRGGBB[AA]".
bool ParseColor(const json& j, ImVec4& out);
json ColorToJson(const ImVec4& c); // rounded to 3 decimals
