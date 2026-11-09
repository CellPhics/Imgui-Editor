// mm_widgets - see mm_widgets.h for what everything does.
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "mm_widgets.h"
#include "imgui_internal.h"
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace mm {

static Theme g_theme;
static ImGuiStorage g_anim_values;     // Animate() state, keyed by id
static ImGuiID g_listening_keybind = 0; // the keybind waiting for a key press
static ImVector<bool> g_label_stack;   // did LabelAbove() push an item width?

Theme& GetTheme() { return g_theme; }

ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

// Exponential smoothing: covers the same fraction of the distance every
// second no matter the frame rate.
static float AnimateFrom(ImGuiID id, float target, float initial, float speed_mul)
{
    float* v = g_anim_values.GetFloatRef(id, FLT_MAX);
    if (*v == FLT_MAX) *v = initial; // first time we see this id
    if (!g_theme.animations) {
        *v = target;
        return target;
    }
    const float k = 1.0f - expf(-g_theme.anim_speed * speed_mul * ImGui::GetIO().DeltaTime);
    *v += (target - *v) * k;
    if (fabsf(target - *v) < 0.001f) *v = target;
    return *v;
}

float Animate(ImGuiID id, float target, float speed_mul) { return AnimateFrom(id, target, target, speed_mul); }

void ResetAnimations() { g_anim_values.Clear(); }

const char* LabelAbove(const char* label, bool full_width)
{
    const char* hash = strstr(label, "##");
    const char* vis_end = hash ? hash : label + strlen(label);
    const bool pushed = g_theme.labels_on_top && vis_end > label;
    g_label_stack.push_back(pushed);
    if (!pushed) return label;
    ImGui::TextUnformatted(label, vis_end);
    ImGui::PushItemWidth(full_width ? -FLT_MIN : ImGui::CalcItemWidth());
    if (hash) return hash;
    // no ## in the label: make one so the widget doesn't draw the label a second time
    static char buffers[8][256];
    static int next = 0;
    char* b = buffers[next++ & 7];
    snprintf(b, 256, "##%s", label);
    return b;
}

void LabelAboveEnd()
{
    if (g_label_stack.empty()) return;
    if (g_label_stack.back()) ImGui::PopItemWidth();
    g_label_stack.pop_back();
}

const char* IconLabel(const char* icon, const char* label)
{
    // a few rotating buffers so you can use it more than once in one expression
    static char buffers[8][512];
    static int next = 0;
    char* b = buffers[next++ & 7];
    if (icon && icon[0] && label && label[0]) snprintf(b, 512, "%s  %s", icon, label);
    else if (icon && icon[0])                 snprintf(b, 512, "%s", icon);
    else                                      snprintf(b, 512, "%s", label ? label : "");
    return b;
}

// ImVec4 -> ImU32, respecting style.Alpha (so fades and BeginDisabled work)
static ImU32 Col(const ImVec4& c, float alpha_mul = 1.0f)
{
    ImVec4 v = c;
    v.w *= alpha_mul * ImGui::GetStyle().Alpha;
    return ImGui::ColorConvertFloat4ToU32(v);
}

// ---------------------------------------------------------------- toggle / checkbox

bool Toggle(const char* label, bool* v)
{
    using namespace ImGui;
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiStyle& style = GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, nullptr, true);
    const float fh = GetFrameHeight();
    const float h = IM_TRUNC(fh * 0.78f);
    const float w = IM_TRUNC(h * 1.85f);
    const ImVec2 pos = window->DC.CursorPos;

    float total_w = w + (label_size.x > 0 ? style.ItemInnerSpacing.x + label_size.x : 0.0f);
    if (g_theme.toggle_right && label_size.x > 0)
        total_w = ImMax(total_w, GetContentRegionAvail().x);
    const ImRect bb(pos, pos + ImVec2(total_w, fh));
    ItemSize(bb, style.FramePadding.y);
    if (!ItemAdd(bb, id)) return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) { *v = !*v; MarkItemEdited(id); }

    const float t = Animate(id, *v ? 1.0f : 0.0f);         // 0 = off, 1 = on
    const float hover = Animate(id + 1, hovered ? 1.0f : 0.0f);
    ImDrawList* dl = window->DrawList;

    ImVec2 sw_pos;   // where the switch goes
    ImVec2 text_pos;
    if (g_theme.toggle_right && label_size.x > 0) {
        sw_pos = ImVec2(bb.Max.x - w, pos.y + (fh - h) * 0.5f);
        text_pos = ImVec2(pos.x, pos.y + style.FramePadding.y);
    } else {
        sw_pos = ImVec2(pos.x, pos.y + (fh - h) * 0.5f);
        text_pos = ImVec2(pos.x + w + style.ItemInnerSpacing.x, pos.y + style.FramePadding.y);
    }
    ImVec4 off = g_theme.toggle_off;
    off = LerpColor(off, ImVec4(off.x + 0.08f, off.y + 0.08f, off.z + 0.08f, off.w), hover); // a bit lighter on hover
    dl->AddRectFilled(sw_pos, sw_pos + ImVec2(w, h), Col(LerpColor(off, g_theme.accent, t)), h * 0.5f);
    const float r = h * 0.5f - 2.5f;
    const ImVec2 knob(sw_pos.x + 2.5f + r + t * (w - 2 * r - 5.0f), sw_pos.y + h * 0.5f);
    dl->AddCircleFilled(knob + ImVec2(0, 1), r, Col(ImVec4(0, 0, 0, 0.25f))); // tiny shadow
    dl->AddCircleFilled(knob, r, Col(ImVec4(1, 1, 1, 1)));
    if (label_size.x > 0)
        RenderText(text_pos, label);
    return pressed;
}

bool CheckboxEx(const char* label, bool* v, int style, float box_size)
{
    using namespace ImGui;
    if (style == CheckboxStyle_Default) return Checkbox(label, v);
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiStyle& st = GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, nullptr, true);
    const float row = GetFrameHeight();
    const float sz = box_size > 0 ? box_size : row;
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, pos + ImVec2(sz + (label_size.x > 0 ? st.ItemInnerSpacing.x + label_size.x : 0.0f), row));
    ItemSize(bb, st.FramePadding.y);
    if (!ItemAdd(bb, id)) return false;
    bool hovered, held;
    bool pressed = ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) { *v = !*v; MarkItemEdited(id); }
    const float t = Animate(id, *v ? 1.0f : 0.0f);
    ImDrawList* dl = window->DrawList;
    const ImVec2 bp(pos.x, pos.y + IM_TRUNC((row - sz) * 0.5f));
    const ImRect box(bp, bp + ImVec2(sz, sz));
    const bool circle = style == CheckboxStyle_Circle;
    const float r = circle ? sz * 0.5f : st.FrameRounding;
    const ImU32 frame = GetColorU32(held ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    dl->AddRectFilled(box.Min, box.Max, frame, r);
    if (st.FrameBorderSize > 0) dl->AddRect(box.Min, box.Max, GetColorU32(ImGuiCol_Border), r, 0, st.FrameBorderSize);
    if (t > 0.01f) {
        // tiny boxes get filled edge to edge, bigger ones get a gap that shrinks as it turns on
        const float inset = box_size > 0 && box_size < 10 ? 0.0f : ImMax(2.0f, IM_TRUNC(sz * 0.18f));
        const float grow = inset > 0 ? sz * 0.15f * (1.0f - t) : 0.0f;
        ImVec4 c = GetStyleColorVec4(ImGuiCol_CheckMark);
        c.w *= t;
        const ImVec2 a = box.Min + ImVec2(inset + grow, inset + grow), b = box.Max - ImVec2(inset + grow, inset + grow);
        if (circle) dl->AddCircleFilled((a + b) * 0.5f, (b.x - a.x) * 0.5f, ColorConvertFloat4ToU32(c));
        else dl->AddRectFilled(a, b, ColorConvertFloat4ToU32(c), ImMax(0.0f, r - inset * 0.5f));
    }
    if (label_size.x > 0) RenderText(ImVec2(box.Max.x + st.ItemInnerSpacing.x, pos.y + st.FramePadding.y), label);
    return pressed;
}

// ---------------------------------------------------------------- sliders

template <typename T>
static bool SliderEx(const char* label, T* v, T v_min, T v_max, const char* format, int style, float track_height, bool markers)
{
    using namespace ImGui;
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiStyle& st = GetStyle();
    const ImGuiID id = window->GetID(label);
    const char* label_end = FindRenderedTextEnd(label);
    char value_buf[64];
    snprintf(value_buf, sizeof(value_buf), format, *v);
    const float w = CalcItemWidth();
    const ImVec2 pos = window->DC.CursorPos;
    const float line = GetTextLineHeight();
    ImRect bb, track;
    const float marker_w = markers ? CalcTextSize("+").x + 6.0f : 0.0f;
    if (style == SliderStyle_Thin) {
        const float track_h = track_height > 0 ? track_height : ImMax(4.0f, IM_TRUNC(line * 0.3f));
        const float total_h = line + st.ItemInnerSpacing.y + track_h + 6.0f;
        bb = ImRect(pos, pos + ImVec2(w, total_h));
        track = ImRect(ImVec2(pos.x + 6.0f + marker_w, bb.Max.y - 3.0f - track_h), ImVec2(bb.Max.x - 6.0f - marker_w, bb.Max.y - 3.0f));
    } else if (style == SliderStyle_Line) {
        const float track_h = track_height > 0 ? track_height : ImMax(3.0f, IM_TRUNC(line * 0.35f));
        bb = ImRect(pos, pos + ImVec2(w, track_h + IM_TRUNC(line * 0.85f)));
        track = ImRect(ImVec2(pos.x + marker_w, pos.y), ImVec2(bb.Max.x - marker_w, pos.y + track_h));
    } else {
        bb = ImRect(pos, pos + ImVec2(w, GetFrameHeight()));
        track = bb;
    }
    ItemSize(bb, style == SliderStyle_Filled ? st.FramePadding.y : 0.0f);
    if (!ItemAdd(bb, id)) return false;
    bool hovered, held;
    // simple drag handling: while held, the value follows the mouse x along the track
    ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_PressedOnClick);
    bool changed = false;
    if (held && v_max != v_min) {
        const float t = ImSaturate((GetIO().MousePos.x - track.Min.x) / ImMax(1.0f, track.GetWidth()));
        T nv;
        if constexpr (std::is_integral_v<T>) nv = (T)ImFloor(v_min + (v_max - v_min) * t + 0.5f);
        else nv = (T)(v_min + (v_max - v_min) * t);
        if (nv != *v) { *v = nv; changed = true; MarkItemEdited(id); }
    }
    const float frac = v_max == v_min ? 0.0f : ImSaturate((float)(*v - v_min) / (float)(v_max - v_min));
    const float af = Animate(id, frac, 2.0f); // animated fill so jumps slide instead of snapping
    ImDrawList* dl = window->DrawList;
    snprintf(value_buf, sizeof(value_buf), format, *v);
    if (markers && style != SliderStyle_Filled) {
        const float my = (track.Min.y + track.Max.y) * 0.5f - line * 0.5f;
        dl->AddText(ImVec2(bb.Min.x + (style == SliderStyle_Thin ? 6.0f : 0.0f), my), GetColorU32(ImGuiCol_TextDisabled), "-");
        dl->AddText(ImVec2(track.Max.x + 6.0f, my), GetColorU32(ImGuiCol_TextDisabled), "+");
    }
    if (style == SliderStyle_Line) {
        dl->AddRectFilled(track.Min, track.Max, GetColorU32(ImGuiCol_FrameBg), st.FrameRounding);
        const float x = track.Min.x + track.GetWidth() * af;
        if (x > track.Min.x + 0.5f) {
            const ImU32 c1 = GetColorU32(ImGuiCol_SliderGrab), c2 = GetColorU32(ImGuiCol_SliderGrabActive);
            dl->AddRectFilledMultiColor(track.Min, ImVec2(x, track.Max.y), c1, c1, c2, c2);
        }
        if (hovered || held) dl->AddRect(track.Min, track.Max, GetColorU32(ImGuiCol_FrameBgHovered), st.FrameRounding);
        const ImVec2 vs = CalcTextSize(value_buf);
        dl->AddText(ImVec2(IM_TRUNC(track.Min.x + (track.GetWidth() - vs.x) * 0.5f), track.Max.y - 1.0f), GetColorU32(ImGuiCol_Text), value_buf);
        return changed;
    }
    if (style == SliderStyle_Thin) {
        const float th = track.GetHeight();
        if (label_end > label) RenderText(pos, label, label_end, false);
        const ImVec2 vs = CalcTextSize(value_buf);
        dl->AddText(ImVec2(bb.Max.x - vs.x, pos.y), GetColorU32(ImGuiCol_Text, 0.75f), value_buf);
        dl->AddRectFilled(track.Min, track.Max, GetColorU32(ImGuiCol_FrameBg), th * 0.5f);
        const float x = track.Min.x + track.GetWidth() * af;
        dl->AddRectFilled(track.Min, ImVec2(x, track.Max.y), GetColorU32(held ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab), th * 0.5f);
        const float kr = th * 0.5f + 3.0f + (hovered || held ? 1.0f : 0.0f);
        dl->AddCircleFilled(ImVec2(x, (track.Min.y + track.Max.y) * 0.5f), kr, GetColorU32(ImGuiCol_Text));
    } else {
        const ImU32 frame = GetColorU32(held ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
        dl->AddRectFilled(bb.Min, bb.Max, frame, st.FrameRounding);
        if (af > 0.0f)
            dl->AddRectFilled(bb.Min, ImVec2(bb.Min.x + ImMax(st.FrameRounding * 2.0f, bb.GetWidth() * af), bb.Max.y),
                              GetColorU32(held ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab), st.FrameRounding);
        if (st.FrameBorderSize > 0) dl->AddRect(bb.Min, bb.Max, GetColorU32(ImGuiCol_Border), st.FrameRounding, 0, st.FrameBorderSize);
        RenderTextClipped(bb.Min, bb.Max, value_buf, nullptr, nullptr, ImVec2(0.5f, 0.5f));
        if (label_end > label)
            RenderText(ImVec2(bb.Max.x + st.ItemInnerSpacing.x, bb.Min.y + st.FramePadding.y), label, label_end, false);
    }
    return changed;
}

bool SliderFloatEx(const char* label, float* v, float v_min, float v_max, const char* format, int style, float track_height, bool markers)
{
    if (style == SliderStyle_Default) return ImGui::SliderFloat(label, v, v_min, v_max, format);
    return SliderEx<float>(label, v, v_min, v_max, format, style, track_height, markers);
}

bool SliderIntEx(const char* label, int* v, int v_min, int v_max, const char* format, int style, float track_height, bool markers)
{
    if (style == SliderStyle_Default) return ImGui::SliderInt(label, v, v_min, v_max, format);
    return SliderEx<int>(label, v, v_min, v_max, format, style, track_height, markers);
}

// ---------------------------------------------------------------- rect / keybind / multi combo

void Rect(ImVec2 size, ImVec4 col, ImVec4 col2, bool vertical, float rounding)
{
    using namespace ImGui;
    if (size.x < 0) size.x = ImMax(1.0f, GetContentRegionAvail().x + size.x + 1.0f);
    Dummy(size);
    const ImVec2 a = GetItemRectMin(), b = GetItemRectMax();
    ImDrawList* dl = GetWindowDrawList();
    const ImU32 c1 = Col(col);
    if (col2.w < 0) { dl->AddRectFilled(a, b, c1, rounding); return; }
    const ImU32 c2 = Col(col2);
    if (rounding <= 0) {
        if (vertical) dl->AddRectFilledMultiColor(a, b, c1, c1, c2, c2);
        else dl->AddRectFilledMultiColor(a, b, c1, c2, c2, c1);
        return;
    }
    // rounded gradient: draw the rounded shape, then recolor its vertices
    const int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, b, c1, rounding);
    ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, a, vertical ? ImVec2(a.x, b.y) : ImVec2(b.x, a.y), c1, c2);
}

static const char* KeyDisplayName(int key)
{
    if (key == ImGuiKey_None) return "None";
    switch (key) {
        case ImGuiKey_MouseLeft:   return "Mouse1";
        case ImGuiKey_MouseRight:  return "Mouse2";
        case ImGuiKey_MouseMiddle: return "Mouse3";
        case ImGuiKey_MouseX1:     return "Mouse4";
        case ImGuiKey_MouseX2:     return "Mouse5";
        default: return ImGui::GetKeyName((ImGuiKey)key);
    }
}

bool Keybind(const char* label, int* key)
{
    using namespace ImGui;
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiID id = window->GetID(label);
    bool changed = false;
    const bool listening = g_listening_keybind == id;

    ImGui::BeginGroup();
    PushID(label);
    const char* shown = listening ? "..." : KeyDisplayName(*key);
    char btn[96];
    snprintf(btn, sizeof(btn), "%s##kb", shown);
    const float bw = ImMax(CalcTextSize(shown).x + GetStyle().FramePadding.x * 2.0f, GetFrameHeight() * 2.6f);

    const ImVec2 label_size = CalcTextSize(label, nullptr, true);
    if (label_size.x > 0) {
        AlignTextToFramePadding();
        TextUnformatted(label, FindRenderedTextEnd(label));
        SameLine();
        float avail = GetContentRegionAvail().x;
        if (avail > bw) SetCursorPosX(GetCursorPosX() + avail - bw);
    }
    if (listening) PushStyleColor(ImGuiCol_Button, g_theme.accent);
    if (Button(btn, ImVec2(bw, 0)) && !listening)
        g_listening_keybind = id;
    else if (listening) {
        if (IsKeyPressed(ImGuiKey_Escape, false)) g_listening_keybind = 0;
        else if (IsKeyPressed(ImGuiKey_Backspace, false) || IsKeyPressed(ImGuiKey_Delete, false)) { *key = ImGuiKey_None; changed = true; g_listening_keybind = 0; }
        else {
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++) {
                if (k >= ImGuiKey_ReservedForModCtrl && k <= ImGuiKey_ReservedForModSuper) continue;
                if (k == ImGuiKey_MouseLeft || (k >= ImGuiKey_MouseWheelX && k <= ImGuiKey_MouseWheelY)) continue;
                if (IsKeyPressed((ImGuiKey)k, false)) { *key = k; changed = true; g_listening_keybind = 0; break; }
            }
        }
    }
    if (listening) PopStyleColor();
    PopID();
    ImGui::EndGroup();
    if (changed) MarkItemEdited(id);
    return changed;
}

bool MultiCombo(const char* label, bool* values, const char* const items[], int count)
{
    using namespace ImGui;
    char preview[256] = "";
    int n = 0;
    for (int i = 0; i < count; i++) {
        if (!values[i]) continue;
        if (n++) strncat(preview, ", ", sizeof(preview) - strlen(preview) - 1);
        strncat(preview, items[i], sizeof(preview) - strlen(preview) - 1);
    }
    if (!n) snprintf(preview, sizeof(preview), "None");
    bool changed = false;
    if (BeginCombo(label, preview)) {
        for (int i = 0; i < count; i++) {
            PushID(i);
            if (Selectable(items[i], values[i], ImGuiSelectableFlags_NoAutoClosePopups)) { values[i] = !values[i]; changed = true; }
            PopID();
        }
        EndCombo();
    }
    return changed;
}

// ---------------------------------------------------------------- tabs

static ImRect TabButton(ImGuiID id, const ImVec2& pos, const ImVec2& size, bool* pressed, bool* hovered)
{
    using namespace ImGui;
    const ImRect bb(pos, pos + size);
    ItemSize(bb);
    *pressed = false;
    *hovered = false;
    if (!ItemAdd(bb, id)) return bb;
    bool held;
    *pressed = ButtonBehavior(bb, id, hovered, &held);
    return bb;
}

// Measures what goes inside one tab: the icon (or image) and the label.
struct TabContent {
    ImVec2 icon, label, total;
    bool has_label;
};

static TabContent MeasureTab(const TabsOptions& o, const char* icon, const char* label, ImTextureID image)
{
    using namespace ImGui;
    TabContent c;
    c.has_label = !o.icon_only && label && label[0];
    const float icon_px = o.icon_size > 0 ? o.icon_size : GetFontSize();
    c.icon = ImVec2(0, 0);
    if (image != ImTextureID_Invalid) c.icon = o.image_size.x > 0 ? o.image_size : ImVec2(icon_px, icon_px);
    else if (icon && icon[0]) c.icon = GetFont()->CalcTextSizeA(icon_px, FLT_MAX, 0.0f, icon);
    c.label = c.has_label ? CalcTextSize(label) : ImVec2(0, 0);
    const bool both = c.has_label && c.icon.x > 0;
    if (o.layout == TabLayout_Stacked) c.total = ImVec2(ImMax(c.icon.x, c.label.x), c.icon.y + (both ? 4.0f : 0.0f) + c.label.y);
    else c.total = ImVec2(c.icon.x + (both ? 8.0f : 0.0f) + c.label.x, ImMax(c.icon.y, c.label.y));
    return c;
}

static void DrawTabContent(ImDrawList* dl, const ImRect& bb, const TabsOptions& o, const char* icon, const char* label,
                           ImTextureID image, const ImVec4& color, bool center, float pad_x)
{
    using namespace ImGui;
    const TabContent c = MeasureTab(o, icon, label, image);
    const bool stacked = o.layout == TabLayout_Stacked;
    const ImVec2 p = ImFloor(ImVec2(center ? bb.Min.x + (bb.GetWidth() - c.total.x) * 0.5f : bb.Min.x + pad_x,
                                    bb.Min.y + (bb.GetHeight() - c.total.y) * 0.5f));
    const ImU32 col = Col(color);

    // icon/image: centered above the label when stacked, vertically centered when in a row
    const ImVec2 icon_pos = stacked ? ImVec2(p.x + (c.total.x - c.icon.x) * 0.5f, p.y) : ImVec2(p.x, p.y + (c.total.y - c.icon.y) * 0.5f);
    if (image != ImTextureID_Invalid)
        dl->AddImage(image, icon_pos, icon_pos + c.icon, ImVec2(0, 0), ImVec2(1, 1), o.tint_images ? col : Col(ImVec4(1, 1, 1, 1)));
    else if (icon && icon[0])
        dl->AddText(GetFont(), o.icon_size > 0 ? o.icon_size : GetFontSize(), ImFloor(icon_pos), col, icon);

    if (c.has_label) {
        const float gap = c.icon.x > 0 ? (stacked ? 4.0f : 8.0f) : 0.0f;
        const ImVec2 label_pos = stacked ? ImVec2(p.x + (c.total.x - c.label.x) * 0.5f, p.y + c.icon.y + gap)
                                         : ImVec2(p.x + c.icon.x + gap, p.y + (c.total.y - c.label.y) * 0.5f);
        dl->AddText(ImFloor(label_pos), col, label);
    }
}

int BeginTabs(const char* id, const char* const labels[], const char* const icons[], int count, int* selected, const TabsOptions& opt)
{
    using namespace ImGui;
    PushID(id);
    int clicked = -1;
    if (count > 0) *selected = ImClamp(*selected, 0, count - 1);
    const ImGuiStyle& style = GetStyle();
    ImGuiWindow* parent = GetCurrentWindow();
    const ImVec4 text = style.Colors[ImGuiCol_Text];
    const float th = opt.tab_height > 0 ? opt.tab_height : g_theme.tab_height;
    auto image_of = [&](int i) { return opt.images ? opt.images[i] : ImTextureID_Invalid; };
    auto icon_of = [&](int i) { return icons ? icons[i] : nullptr; };

    if (opt.style == TabsStyle_Sidebar) {
        // Sidebar background, rounded only where it touches the window corners.
        const ImVec2 p0 = parent->DC.CursorPos;
        const float h = GetContentRegionAvail().y;
        ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
        const ImRect inner = parent->InnerRect;
        if (fabsf(p0.x - parent->Pos.x) < 1.0f) {
            corners = (parent->Flags & ImGuiWindowFlags_NoTitleBar) && fabsf(p0.y - inner.Min.y) < 1.0f ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersBottomLeft;
        }
        parent->DrawList->AddRectFilled(p0, p0 + ImVec2(opt.sidebar_width, h), Col(g_theme.sidebar_bg), parent->WindowRounding, corners);
        if (opt.sidebar_border)
            parent->DrawList->AddLine(p0 + ImVec2(opt.sidebar_width - 0.5f, 0), p0 + ImVec2(opt.sidebar_width - 0.5f, h), GetColorU32(ImGuiCol_Border));

        PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        PushStyleVar(ImGuiStyleVar_WindowPadding, opt.sidebar_padding);
        BeginChild("##sidebar", ImVec2(opt.sidebar_width, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
        PopStyleVar();
        ImGuiWindow* w = GetCurrentWindow();
        const float avail = GetContentRegionAvail().x;
        if (opt.logo != ImTextureID_Invalid) {
            SetCursorPosX(GetCursorPosX() + (avail - opt.logo_size.x) * 0.5f);
            Image(opt.logo, opt.logo_size);
        }
        if (opt.header && opt.header[0]) {
            PushFont(opt.header_font, opt.header_size);
            PushStyleColor(ImGuiCol_Text, g_theme.accent);
            const float tw = CalcTextSize(opt.header).x;
            SetCursorPosX(GetCursorPosX() + ImMax(0.0f, (avail - tw) * 0.5f));
            TextUnformatted(opt.header);
            PopStyleColor();
            PopFont();
        }
        if (opt.logo != ImTextureID_Invalid || (opt.header && opt.header[0])) Dummy(ImVec2(0, 10));

        const ImVec2 origin = w->DC.CursorPos;
        const float gap = opt.tab_gap;
        // Selection highlight that slides between tabs.
        const float hl_y = Animate(GetID("##hl"), (float)*selected * (th + gap));
        const ImVec2 hl0(origin.x, origin.y + hl_y), hl1 = hl0 + ImVec2(avail, th);
        switch (opt.highlight) {
        case TabHighlight_Bar:
            w->DrawList->AddRectFilled(hl0, hl1, Col(g_theme.accent, 0.16f), g_theme.tab_rounding);
            w->DrawList->AddRectFilled(hl0 + ImVec2(0, th * 0.22f), hl0 + ImVec2(3, th * 0.78f), Col(g_theme.accent), 2.0f);
            break;
        case TabHighlight_Box: {
            // Same color as the content area, framed on the outer sides so it opens into the content.
            ImDrawList* pdl = parent->DrawList;
            const ImVec2 b0(p0.x, hl0.y), b1(p0.x + opt.sidebar_width + 1.0f, hl1.y);
            pdl->PushClipRect(parent->ClipRect.Min, parent->ClipRect.Max);
            pdl->AddRectFilled(b0, b1, GetColorU32(ImGuiCol_WindowBg));
            pdl->AddLine(b0, ImVec2(b1.x, b0.y), GetColorU32(ImGuiCol_Border));
            pdl->AddLine(ImVec2(b0.x, b1.y - 1), ImVec2(b1.x, b1.y - 1), GetColorU32(ImGuiCol_Border));
            pdl->PopClipRect();
            break;
        }
        case TabHighlight_Fill:
            w->DrawList->AddRectFilled(hl0, hl1, Col(g_theme.accent), g_theme.tab_rounding);
            break;
        default: break;
        }

        for (int i = 0; i < count; i++) {
            PushID(i);
            ImGuiID tid = GetID("##tab");
            bool pressed, hovered;
            ImVec2 pos(origin.x, origin.y + i * (th + gap));
            SetCursorScreenPos(pos);
            ImRect bb = TabButton(tid, pos, ImVec2(avail, th), &pressed, &hovered);
            if (pressed) { *selected = i; clicked = i; }
            const float st = Animate(tid, *selected == i ? 1.0f : 0.0f);
            const float hv = Animate(tid + 1, hovered ? 1.0f : 0.0f);
            if (hv > 0.01f && *selected != i && opt.highlight != TabHighlight_None)
                w->DrawList->AddRectFilled(bb.Min, bb.Max, Col(ImVec4(1, 1, 1, 0.04f * hv)), g_theme.tab_rounding);
            if (opt.separators && i + 1 < count) {
                const float y = bb.Max.y + gap * 0.5f;
                parent->DrawList->AddLine(ImVec2(p0.x, y), ImVec2(p0.x + opt.sidebar_width, y), GetColorU32(ImGuiCol_Separator));
            }
            ImVec4 tc = LerpColor(LerpColor(g_theme.text_dim, text, hv * 0.6f), text, st);
            const bool center = opt.icon_only || opt.layout == TabLayout_Stacked || !labels[i] || !labels[i][0];
            DrawTabContent(w->DrawList, bb, opt, icon_of(i), labels[i], image_of(i), tc, center,
                           14.0f + (opt.highlight == TabHighlight_Bar ? st * 4.0f : 0.0f));
            PopID();
        }
        EndChild();
        PopStyleColor();
        SameLine(0, 0);
        BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
    } else {
        // Horizontal strip: underline (Top) or pill background (Pills).
        ImGuiWindow* w = parent;
        const bool pills = opt.style == TabsStyle_Pills;
        const float sth = pills && opt.tab_height <= 0 ? th * 0.85f : th;
        const ImVec2 origin = w->DC.CursorPos;
        const float pad_x = 16.0f;
        float x = 0.0f;
        float sel_x = 0, sel_w = 0;
        ImVector<float> xs, ws;
        for (int i = 0; i < count; i++) {
            const float tw = MeasureTab(opt, icon_of(i), labels[i], image_of(i)).total.x + pad_x * 2;
            xs.push_back(x); ws.push_back(tw);
            if (i == *selected) { sel_x = x; sel_w = tw; }
            x += tw + (pills ? 6.0f : opt.tab_gap * 0.5f);
        }
        const float ax = Animate(GetID("##hlx"), sel_x);
        const float aw = Animate(GetID("##hlw"), sel_w);
        if (pills || opt.highlight == TabHighlight_Fill)
            w->DrawList->AddRectFilled(origin + ImVec2(ax, 0), origin + ImVec2(ax + aw, sth), Col(g_theme.accent), pills ? sth * 0.5f : g_theme.tab_rounding);
        else if (opt.highlight == TabHighlight_Box) {
            w->DrawList->AddRectFilled(origin + ImVec2(ax, 0), origin + ImVec2(ax + aw, sth + 1), GetColorU32(ImGuiCol_ChildBg));
            w->DrawList->AddRect(origin + ImVec2(ax, 0), origin + ImVec2(ax + aw, sth + 1), GetColorU32(ImGuiCol_Border));
        }
        for (int i = 0; i < count; i++) {
            PushID(i);
            ImGuiID tid = GetID("##tab");
            bool pressed, hovered;
            ImVec2 pos = origin + ImVec2(xs[i], 0);
            SetCursorScreenPos(pos);
            ImRect bb = TabButton(tid, pos, ImVec2(ws[i], sth), &pressed, &hovered);
            if (pressed) { *selected = i; clicked = i; }
            const float st = Animate(tid, *selected == i ? 1.0f : 0.0f);
            const float hv = Animate(tid + 1, hovered ? 1.0f : 0.0f);
            if (pills && hv > 0.01f && *selected != i)
                w->DrawList->AddRectFilled(bb.Min, bb.Max, Col(ImVec4(1, 1, 1, 0.05f * hv)), sth * 0.5f);
            if (opt.separators && i + 1 < count) {
                const float sx = bb.Max.x + opt.tab_gap * 0.25f;
                w->DrawList->AddLine(ImVec2(sx, bb.Min.y + sth * 0.25f), ImVec2(sx, bb.Max.y - sth * 0.25f), GetColorU32(ImGuiCol_Separator));
            }
            ImVec4 tc = LerpColor(LerpColor(g_theme.text_dim, text, hv * 0.6f), text, st);
            DrawTabContent(w->DrawList, bb, opt, icon_of(i), labels[i], image_of(i), tc, true, pad_x);
            PopID();
        }
        SetCursorScreenPos(origin + ImVec2(0, sth));
        if (!pills) {
            const float line_w = ImMax(x, GetContentRegionAvail().x);
            w->DrawList->AddLine(origin + ImVec2(0, sth), origin + ImVec2(line_w, sth), Col(style.Colors[ImGuiCol_Separator]), 1.0f);
            if (opt.highlight == TabHighlight_Bar)
                w->DrawList->AddRectFilled(origin + ImVec2(ax + 6, sth - 2), origin + ImVec2(ax + aw - 6, sth + 1), Col(g_theme.accent), 2.0f);
        }
        Dummy(ImVec2(0, style.ItemSpacing.y));
        BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    }
    return clicked;
}

void EndTabs()
{
    ImGui::EndChild();
    ImGui::PopID();
}

// ---------------------------------------------------------------- group boxes
struct GroupState { const char* title; bool border; int title_style; };
static ImVector<GroupState> g_groups;

bool BeginGroup(const char* id, const char* title, ImVec2 size, bool border, int title_style)
{
    using namespace ImGui;
    const bool has_title = title && title[0];
    const bool on_border = has_title && title_style == GroupTitle_Border;
    ImGuiChildFlags flags = ImGuiChildFlags_AlwaysUseWindowPadding;
    if (border && !on_border) flags |= ImGuiChildFlags_Borders;
    if (size.y <= 0.0f) { flags |= ImGuiChildFlags_AutoResizeY; size.y = 0.0f; }
    if (on_border) {
        // Leave room above the box for the upper half of the title.
        const float dy = IM_TRUNC(GetTextLineHeight() * 0.5f) - GetStyle().ItemSpacing.y;
        if (dy > 0) Dummy(ImVec2(0, dy));
    }
    g_groups.push_back({title, border, on_border ? GroupTitle_Border : has_title ? title_style : GroupTitle_Inside});
    bool open = BeginChild(id, size, flags, ImGuiWindowFlags_NoScrollbar);
    if (has_title && on_border) {
        Dummy(ImVec2(0, IM_TRUNC(GetTextLineHeight() * 0.25f)));
    } else if (has_title && title_style == GroupTitle_Inside) {
        PushStyleColor(ImGuiCol_Text, g_theme.group_title);
        TextUnformatted(title);
        PopStyleColor();
        ImGuiWindow* w = GetCurrentWindow();
        const ImVec2 p = w->DC.CursorPos;
        const float avail = GetContentRegionAvail().x;
        w->DrawList->AddRectFilled(p, p + ImVec2(avail, 1), Col(GetStyle().Colors[ImGuiCol_Separator]));
        w->DrawList->AddRectFilled(p, p + ImVec2(ImMin(28.0f, avail), 2), Col(g_theme.accent), 1.0f);
        Dummy(ImVec2(0, 4));
    } else if (has_title && title_style == GroupTitle_Plain) {
        PushStyleColor(ImGuiCol_Text, g_theme.group_title);
        TextUnformatted(title);
        PopStyleColor();
    }
    return open;
}

void EndGroup()
{
    using namespace ImGui;
    GroupState gs{nullptr, false, GroupTitle_Inside};
    if (!g_groups.empty()) { gs = g_groups.back(); g_groups.pop_back(); }
    if (gs.title_style == GroupTitle_Border) {
        // Frame with the title set into the top edge. Drawn last in the child's own draw list
        // (so nothing covers it), with the parent's clip rect so the title can sit above the box.
        ImGuiWindow* w = GetCurrentWindow();
        ImGuiWindow* parent = w->ParentWindow ? w->ParentWindow : w;
        const ImRect r = w->Rect();
        ImDrawList* dl = w->DrawList;
        const ImU32 bc = GetColorU32(ImGuiCol_Border);
        const ImVec2 ts = CalcTextSize(gs.title);
        const float tx = r.Min.x + 10.0f, ty = r.Min.y - IM_TRUNC(ts.y * 0.5f);
        ImRect clip = parent->ClipRect;
        clip.Min.y = ImMax(ImMin(clip.Min.y, ty), parent->OuterRectClipped.Min.y);
        dl->PushClipRect(clip.Min, clip.Max, false);
        if (gs.border) {
            const float bs = ImMax(1.0f, GetStyle().ChildBorderSize);
            const float rr = GetStyle().ChildRounding;
            if (rr <= 0.0f) {
                dl->AddLine(ImVec2(r.Min.x, r.Min.y + 0.5f), ImVec2(tx - 4.0f, r.Min.y + 0.5f), bc, bs);
                dl->AddLine(ImVec2(tx + ts.x + 4.0f, r.Min.y + 0.5f), ImVec2(r.Max.x, r.Min.y + 0.5f), bc, bs);
                dl->AddLine(ImVec2(r.Min.x + 0.5f, r.Min.y), ImVec2(r.Min.x + 0.5f, r.Max.y), bc, bs);
                dl->AddLine(ImVec2(r.Max.x - 0.5f, r.Min.y), ImVec2(r.Max.x - 0.5f, r.Max.y), bc, bs);
                dl->AddLine(ImVec2(r.Min.x, r.Max.y - 0.5f), ImVec2(r.Max.x, r.Max.y - 0.5f), bc, bs);
            } else {
                dl->AddRect(r.Min, r.Max, bc, rr, 0, bs);
                dl->AddRectFilled(ImVec2(tx - 4.0f, r.Min.y - bs), ImVec2(tx + ts.x + 4.0f, r.Min.y + bs), GetColorU32(ImGuiCol_WindowBg));
            }
        }
        dl->AddText(ImVec2(tx, ty), Col(g_theme.group_title), gs.title);
        dl->PopClipRect();
    }
    EndChild();
}

// ---------------------------------------------------------------- text
void TextEx(const char* text, const ImVec4* color, ImFont* font, float size, int align, bool wrap)
{
    using namespace ImGui;
    PushFont(font, size);
    if (color) PushStyleColor(ImGuiCol_Text, *color);
    if (wrap) TextWrapped("%s", text);
    else {
        if (align != 0) {
            float tw = CalcTextSize(text).x;
            float avail = GetContentRegionAvail().x;
            float off = align == 1 ? (avail - tw) * 0.5f : (avail - tw);
            if (off > 0) SetCursorPosX(GetCursorPosX() + off);
        }
        TextUnformatted(text);
    }
    if (color) PopStyleColor();
    PopFont();
}

// ---------------------------------------------------------------- window fade
void PushWindowFade(const char* id)
{
    float a = AnimateFrom(ImHashStr(id) ^ 0x5eedu, 1.0f, 0.0f, 0.6f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * a);
}
void PopWindowFade() { ImGui::PopStyleVar(); }

} // namespace mm
