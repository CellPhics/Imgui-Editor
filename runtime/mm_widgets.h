// mm_widgets - the custom Dear ImGui widgets used by mod menus made with
// ImGui Menu Editor (toggles, keybinds, sidebar tabs, group boxes...).
//
// The editor's preview and the exported code both draw with these, so what
// you see in the editor is exactly what you get in your app.
// Needs Dear ImGui 1.92 or newer.
#pragma once
#include "imgui.h"

namespace mm {

// ---------------------------------------------------------------- theme
// The stuff ImGuiStyle doesn't have a slot for.
struct Theme {
    ImVec4 accent        = ImVec4(0.55f, 0.36f, 0.96f, 1.00f);
    ImVec4 sidebar_bg    = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    ImVec4 toggle_off    = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
    ImVec4 text_dim      = ImVec4(0.55f, 0.55f, 0.60f, 1.00f); // unselected tabs
    ImVec4 group_title   = ImVec4(0.85f, 0.85f, 0.90f, 1.00f);
    float  anim_speed    = 12.0f; // higher = snappier
    bool   animations    = true;
    float  tab_height    = 36.0f; // sidebar / pill tab height
    float  tab_rounding  = 6.0f;
    bool   toggle_right  = true;  // label on the left, switch pushed to the right edge
    bool   labels_on_top = true;  // sliders/combos/inputs get their label above them, full width
};
Theme& GetTheme();

// ---------------------------------------------------------------- animation helpers
// Smoothly moves towards `target` every frame. State is keyed by id.
float Animate(ImGuiID id, float target, float speed_mul = 1.0f);
ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t);
void ResetAnimations(); // e.g. to replay the window fade-in

// Fades a window in the first time it shows up. Call PushWindowFade() before
// Begin() and PopWindowFade() after End().
void PushWindowFade(const char* id);
void PopWindowFade();

// ---------------------------------------------------------------- labels
// "icon  label" in a temporary buffer (icon is a UTF-8 string, can be empty).
const char* IconLabel(const char* icon, const char* label);

// Wrap sliders/combos/inputs with these. If Theme::labels_on_top is on, the
// visible part of `label` gets drawn above the widget and the widget goes full
// width. Returns the label to pass to the widget. full_width = false keeps
// whatever item width you already pushed.
const char* LabelAbove(const char* label, bool full_width = true);
void LabelAboveEnd();

// ---------------------------------------------------------------- widgets

// Animated on/off switch.
bool Toggle(const char* label, bool* v);

// Click it, then press a key or mouse button to bind. Esc cancels, Backspace clears.
bool Keybind(const char* label, int* key /* ImGuiKey */);

// Combo where you can turn on several items at once.
bool MultiCombo(const char* label, bool* values, const char* const items[], int count);

// Checkbox looks. Colors come from FrameBg (box), CheckMark (fill), Text (label).
// box_size > 0 sets the box size in pixels for the Filled/Circle looks.
enum CheckboxStyle { CheckboxStyle_Default = 0, CheckboxStyle_Filled = 1, CheckboxStyle_Circle = 2 };
bool CheckboxEx(const char* label, bool* v, int style, float box_size = 0.0f);

// Slider looks:
//   Default  plain ImGui slider
//   Thin     label left + value right on one row, thin track with a knob below
//   Filled   the frame fills up to the value, value text in the middle
//   Line     thin bar with a SliderGrab -> SliderGrabActive gradient, value under it.
//            Gets its label from LabelAbove() like the default look.
// track_height is the bar height for Thin/Line (0 = auto). markers draws "-" and "+" at the ends.
enum SliderStyle { SliderStyle_Default = 0, SliderStyle_Thin = 1, SliderStyle_Filled = 2, SliderStyle_Line = 3 };
bool SliderFloatEx(const char* label, float* v, float v_min, float v_max, const char* format, int style, float track_height = 0.0f, bool markers = false);
bool SliderIntEx(const char* label, int* v, int v_min, int v_max, const char* format, int style, float track_height = 0.0f, bool markers = false);

// Plain colored rectangle for accent bars, header strips, dividers...
// size.x < 0 fills the width. col2.w < 0 = solid, otherwise a gradient from col to col2.
void Rect(ImVec2 size, ImVec4 col, ImVec4 col2, bool vertical, float rounding);

// Text with an optional color/font/size. align: 0 left, 1 center, 2 right.
void TextEx(const char* text, const ImVec4* color, ImFont* font, float size, int align, bool wrap);

// ---------------------------------------------------------------- tabs

enum TabsStyle { TabsStyle_Sidebar = 0, TabsStyle_Top = 1, TabsStyle_Pills = 2 };
// How the selected tab is marked:
//   Bar   accent tint + a little accent bar
//   Box   takes the content's background (WindowBg) with Border lines, so it looks
//         like it opens into the page
//   Fill  solid accent
enum TabHighlight { TabHighlight_Bar = 0, TabHighlight_Box = 1, TabHighlight_Fill = 2, TabHighlight_None = 3 };
enum TabLayout { TabLayout_Row = 0, TabLayout_Stacked = 1 }; // icon next to / above the label

struct TabsOptions {
    int style = TabsStyle_Sidebar;
    int highlight = TabHighlight_Bar;
    int layout = TabLayout_Row;
    bool icon_only = false;
    float icon_size = 0.0f;               // 0 = font size
    const ImTextureID* images = nullptr;  // one per tab, ImTextureID_Invalid = use the icon instead
    ImVec2 image_size = ImVec2(0, 0);     // 0 = icon_size square
    bool tint_images = true;              // color images like the tab text (so white art dims when unselected)
    float tab_height = 0.0f;              // 0 = Theme::tab_height
    float tab_gap = 4.0f;
    bool separators = false;              // lines between tabs
    bool sidebar_border = false;          // line down the sidebar's right edge
    ImVec2 sidebar_padding = ImVec2(10, 12);
    float sidebar_width = 170.0f;
    ImTextureID logo = ImTextureID_Invalid;
    ImVec2 logo_size = ImVec2(64, 64);
    const char* header = nullptr;         // brand text at the top of the sidebar
    ImFont* header_font = nullptr;
    float header_size = 0.0f;
};

// Draws the tabs and opens the content area. Always call EndTabs() after.
// Returns the index of the tab clicked this frame, or -1.
int BeginTabs(const char* id, const char* const labels[], const char* const icons[], int count, int* selected, const TabsOptions& opt);
void EndTabs();

// ---------------------------------------------------------------- group boxes

// Inside: title + accent underline inside the box
// Border: title sits on the top border line (classic group box)
// Plain:  just the title text
enum GroupTitle { GroupTitle_Inside = 0, GroupTitle_Border = 1, GroupTitle_Plain = 2 };
// size.y == 0 fits the height to the contents. Always call EndGroup() after.
bool BeginGroup(const char* id, const char* title, ImVec2 size, bool border, int title_style = GroupTitle_Inside);
void EndGroup();

} // namespace mm
