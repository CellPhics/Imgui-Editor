// Draws a menu document live (the editor preview), interpreting the JSON
// with the same mm:: widgets the exporter emits.
#pragma once
#include "assets.h"
#include "model.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <map>
#include <string>

struct PreviewValue {
    std::string sig;
    bool b = false;
    int i = 0;
    float f = 0;
    float col[4] = {1, 1, 1, 1};
    int key = 0;
    bool flags[64] = {};
    std::vector<char> text;
};

struct PreviewCtx {
    // inputs
    bool edit_mode = true;
    std::string selected;
    bool selection_changed = false; // switch tabs to reveal the selection
    bool reset_values = false;
    ImVec2 origin;                  // canvas top-left in screen space

    // outputs (per frame)
    std::string hovered;
    std::string clicked;
    bool doc_changed = false;       // window moved / resized by mouse
    struct Hit { ImRect rect, clip; };
    std::map<std::string, Hit> rects;

    // persistent runtime state
    std::map<std::string, PreviewValue> values;
    std::map<std::string, int> tab_sel;
    std::map<std::string, ImVec2> applied_pos, applied_size;
};

void RenderPreview(json& doc, Assets& assets, PreviewCtx& ctx);
void DrawSelectionOverlay(const PreviewCtx& ctx, ImU32 accent);

int KeyFromName(const std::string& name);
std::string KeyName(int key);

// Safe JSON getters.
float       JF(const json& n, const char* k, float d);
int         JI(const json& n, const char* k, int d);
bool        JB(const json& n, const char* k, bool d);
std::string JS(const json& n, const char* k, const std::string& d = "");
ImVec2      JV2(const json& n, const char* k, ImVec2 d);
bool        JCol(const json& n, const char* k, ImVec4& out);
std::vector<std::string> JItems(const json& n, const char* k);
