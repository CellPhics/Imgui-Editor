// Draws a menu straight from the JSON. This is what you see on the canvas.
// It calls the exact same mm:: widgets the exported C++ calls, so the preview
// and the real thing can't drift apart.
#pragma once
#include "assets.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "model.h"
#include <map>
#include <string>

// Runtime value for one widget while you play with it in the preview.
struct PreviewValue {
    std::string signature; // changes when the widget's default changes -> reset the value
    bool b = false;
    int i = 0;
    float f = 0;
    float color[4] = {1, 1, 1, 1};
    int key = 0;
    bool flags[64] = {};
    std::vector<char> text;
};

struct PreviewState {
    // set by the editor
    bool edit_mode = true;           // clicks select stuff instead of only using it
    std::string selected;
    bool selection_changed = false;  // switch tabs so the selection is visible
    bool reset_values = false;
    ImVec2 origin;                   // canvas top-left in screen space

    // filled in every frame
    std::string hovered;
    std::string clicked;
    bool doc_changed = false;        // a window got dragged or resized
    struct Hit { ImRect rect, clip; };
    std::map<std::string, Hit> rects;

    // kept between frames
    std::map<std::string, PreviewValue> values;
    std::map<std::string, int> selected_tab;
    std::map<std::string, ImVec2> applied_pos, applied_size;
};

void RenderPreview(json& doc, Assets& assets, PreviewState& state);
void DrawSelectionOverlay(const PreviewState& state, ImU32 color);

// ImGuiKey <-> the names we store in JSON ("F", "Insert", "MouseX1"...).
int KeyFromName(const std::string& name);
std::string KeyName(int key);

// String props -> mm:: enum values. Shared with the exporter.
int CheckboxLook(const json& n);
int SliderLook(const json& n);
int GroupTitleStyle(const json& n);
int TabsStyle(const json& n);
int TabHighlight(const json& n);
int TextAlign(const json& n); // 0 left, 1 center, 2 right
