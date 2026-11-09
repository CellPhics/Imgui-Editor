// The menu you get when you make a new project. Just something to poke at.
#include "model.h"
#include "theme.h"

json DefaultDoc()
{
    json d = json::object();
    d["version"] = 1;
    d["name"] = "Nexus";
    d["theme"] = "Midnight";
    d["themes"] = json::object();
    for (const char* preset : {"Midnight", "Crimson", "Cyber", "Emerald", "Gold"}) d["themes"][preset] = ThemePreset(preset);
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
                { "id": "aim_fov_color", "type": "color", "label": "Circle color", "default": [0.55, 0.36, 0.96, 1.0], "inline": true }
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
