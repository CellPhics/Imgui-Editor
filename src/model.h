// Menu document model: a JSON tree of windows -> containers -> widgets.
#pragma once
#include "nlohmann/json.hpp"
#include <string>
#include <vector>

using json = nlohmann::ordered_json;

enum class PK {
    Bool, Int, Float, String, Text, Color, Vec2, Items, Enum, Image, Font, Icon, Key, BoolList, Style
};

struct PropDef {
    std::string key;
    PK kind;
    json def;              // default value (null = optional / unset)
    std::string help;
    float vmin = 0, vmax = 0;
    std::vector<std::string> options; // for PK::Enum
};

struct TypeDef {
    std::string type;       // json "type"
    std::string name;       // display name
    std::string icon;       // font awesome icon name
    bool container = false;
    std::string child_type; // required child type ("" = any widget)
    bool has_value = false; // owns a runtime variable
    std::vector<PropDef> props;
};

const std::vector<TypeDef>& Types();
const TypeDef* FindType(const std::string& type);
const PropDef* FindProp(const TypeDef& t, const std::string& key);

// Tree helpers. Windows live in root["windows"]; containers use "children".
struct NodeRef {
    json* node = nullptr;
    json* parent_array = nullptr; // array containing node
    json* parent = nullptr;       // parent node (null for windows)
    int index = -1;
    explicit operator bool() const { return node != nullptr; }
};
NodeRef FindNode(json& root, const std::string& id);
json* ChildArray(json& root, json* container); // container == nullptr -> windows
bool IsAncestor(json& root, const std::string& ancestor, const std::string& id);
std::vector<std::string> PathTo(json& root, const std::string& id); // ids from window down to id

std::string UniqueId(const json& root, const std::string& base);
std::string Slug(const std::string& s);
json MakeNode(const std::string& type, const json& root, const std::string& label_hint = "");
json CloneWithNewIds(const json& node, const json& root);
bool CanContain(const json* container, const std::string& child_type);

// Fill in missing ids / fix duplicates and missing top-level keys.
// Returns a list of human readable warnings.
std::vector<std::string> Normalize(json& root);
json DefaultDoc();
json& ActiveTheme(json& root);
