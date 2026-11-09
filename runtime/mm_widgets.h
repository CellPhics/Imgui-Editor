// mm_widgets — custom Dear ImGui widgets for mod menus.
// Shared by the menu editor preview and by exported menu code, so what you
// see in the editor is exactly what your exported menu draws.
// Requires Dear ImGui 1.92+.
#pragma once
#include "imgui.h"

namespace mm {

// Extra colors/settings that standard ImGuiStyle doesn't have.
struct Theme {
    ImVec4 accent        = ImVec4(0.55f, 0.36f, 0.96f, 1.00f);
    ImVec4 sidebar_bg    = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    ImVec4 toggle_off    = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
    ImVec4 text_dim      = ImVec4(0.55f, 0.55f, 0.60f, 1.00f);
    ImVec4 group_title   = ImVec4(0.85f, 0.85f, 0.90f, 1.00f);
    float  anim_speed    = 12.0f;   // higher = snappier
    bool   animations    = true;
    float  tab_height    = 36.0f;   // sidebar / pill tab height
    float  tab_rounding  = 6.0f;
    bool   toggle_right  = true;    // label on the left, switch right-aligned
    bool   labels_on_top = true;    // sliders/combos/inputs: label above, full width
};
Theme& GetTheme();

// Exponentially approaches `target` over time; state keyed by id.
float Animate(ImGuiID id, float target, float speed_mul = 1.0f);
ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t);

// Restart all animations (e.g. replay window fade-in).
void ResetAnimations();

// Wrap sliders/combos/inputs: when Theme::labels_on_top is set, draws the
// visible part of `label` above the widget and makes it full width.
// Returns the label to pass to the widget. Always pair with LabelAboveEnd().
// full_width=false keeps the caller's item width.
const char* LabelAbove(const char* label, bool full_width = true);
void LabelAboveEnd();

// Animated on/off switch.
bool Toggle(const char* label, bool* v);

// Checkbox variants. Colors: FrameBg (box), CheckMark (fill), Text (label).
// box_size > 0 sets the box size in pixels (Filled/Circle looks).
enum CheckboxStyle { CheckboxStyle_Default = 0, CheckboxStyle_Filled = 1, CheckboxStyle_Circle = 2 };
bool CheckboxEx(const char* label, bool* v, int style, float box_size = 0.0f);

// Slider variants.
//  Default: standard ImGui slider.
//  Thin:    label left + value right on one row, thin track below (FrameBg track,
//           SliderGrab fill, Text-colored knob).
//  Filled:  frame filled up to the value with SliderGrab, value text centered.
//  Line:    thin bar (FrameBg) filled with a SliderGrab -> SliderGrabActive vertical
//           gradient, value text centered under the bar, optional "-" / "+" markers.
//           Label comes from LabelAbove() like the default look.
// track_height: bar height for Thin/Line (0 = automatic).
enum SliderStyle { SliderStyle_Default = 0, SliderStyle_Thin = 1, SliderStyle_Filled = 2, SliderStyle_Line = 3 };
bool SliderFloatEx(const char* label, float* v, float v_min, float v_max, const char* format, int style, float track_height = 0.0f, bool markers = false);
bool SliderIntEx(const char* label, int* v, int v_min, int v_max, const char* format, int style, float track_height = 0.0f, bool markers = false);

// Decorative rectangle (accent bars, header strips, dividers). size.x < 0 = fill
// width. Set col2.w < 0 for a solid color, otherwise a gradient col -> col2.
void Rect(ImVec2 size, ImVec4 col, ImVec4 col2, bool vertical, float rounding);
// Click, then press a key/mouse button to bind. Esc cancels, Backspace clears.
bool Keybind(const char* label, int* key /* ImGuiKey */);
// Combo where several items can be enabled at once.
bool MultiCombo(const char* label, bool* values, const char* const items[], int count);
// Icon + text label on one line (icon is a UTF-8 string, may be null/empty).
const char* IconLabel(const char* icon, const char* label); // returns a temporary buffer

enum TabsStyle { TabsStyle_Sidebar = 0, TabsStyle_Top = 1, TabsStyle_Pills = 2 };
// Bar: accent tint + accent marker. Box: selected tab takes the content background
// (WindowBg) with Border lines, opening into the content. Fill: solid accent. None.
enum TabHighlight { TabHighlight_Bar = 0, TabHighlight_Box = 1, TabHighlight_Fill = 2, TabHighlight_None = 3 };
enum TabLayout { TabLayout_Row = 0, TabLayout_Stacked = 1 }; // icon beside / above the label
struct TabsOptions {
    int          style          = TabsStyle_Sidebar;
    int          highlight      = TabHighlight_Bar;
    int          layout         = TabLayout_Row;
    bool         icon_only      = false;
    float        icon_size      = 0.0f;           // 0 = font size
    const ImTextureID* images   = nullptr;        // per-tab image (ImTextureID_Invalid = use icon)
    ImVec2       image_size     = ImVec2(0, 0);   // 0 = icon_size square
    bool         tint_images    = true;           // color images like the tab text (white art -> dim/bright)
    float        tab_height     = 0.0f;           // 0 = Theme::tab_height
    float        tab_gap        = 4.0f;
    bool         separators     = false;          // lines between tabs
    bool         sidebar_border = false;          // line on the sidebar's right edge
    ImVec2       sidebar_padding = ImVec2(10, 12);
    float        sidebar_width  = 170.0f;
    ImTextureID  logo           = ImTextureID_Invalid;
    ImVec2       logo_size      = ImVec2(64, 64);
    const char*  header         = nullptr;  // text under/instead of the logo
    ImFont*      header_font    = nullptr;
    float        header_size    = 0.0f;
};
// Draws the tab strip and opens the content region. Always call EndTabs().
// Returns the index of a tab clicked this frame, or -1.
int  BeginTabs(const char* id, const char* const labels[], const char* const icons[], int count, int* selected, const TabsOptions& opt);
void EndTabs();

// Titled box. size.y == 0 auto-fits the contents.
// Inside: title + accent underline inside the box. Border: title set into the top
// border line (classic group box). Plain: title text only.
enum GroupTitle { GroupTitle_Inside = 0, GroupTitle_Border = 1, GroupTitle_Plain = 2 };
bool BeginGroup(const char* id, const char* title, ImVec2 size, bool border, int title_style = GroupTitle_Inside);
void EndGroup();

// Text with optional color/font/size, alignment 0=left 1=center 2=right.
void TextEx(const char* text, const ImVec4* color, ImFont* font, float size, int align, bool wrap);

// Fade a window in the first time it appears: call PushWindowFade() before
// Begin() and PopWindowFade() after End().
void PushWindowFade(const char* id);
void PopWindowFade();

} // namespace mm
