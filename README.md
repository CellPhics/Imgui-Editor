# ImGui Menu Editor

> **Heads up:** this version is built and tested on **Arch Linux + Wayland**. It'll probably
> work on other distros, but that's what I'm running. **A Windows version is coming soon.**

I got tired of hand-writing ImGui menus and recompiling every time I wanted to move a slider 4 pixels,
so I made this. It's a visual editor for Dear ImGui mod menus: you click stuff around, tweak colors,
or just tell Claude what you want ("make it red, add a players tab") and it edits the menu live.
When it looks right, you export it as plain C++ and drop it into your project.

## Getting it running

You need CMake, a C++20 compiler, GLFW and OpenGL. Everything else is already in `third_party/`.

```bash
sudo pacman -S --needed base-devel cmake ninja glfw zenity wl-clipboard
cmake -S . -B build -G Ninja
cmake --build build
./build/imgui-menu-editor
```

First launch makes a starter menu in `~/ImGuiMenus/MyMenu` so you've got something to mess with.
`zenity` gives you the file pickers and `wl-clipboard` lets you paste images. Both are optional.

## What you get

- **Live canvas.** Your menu runs right there. In Edit mode you click a widget to select it and drag
  windows around. Hit Ctrl+E to switch to Interact mode and use it like you would in game.
- **Hierarchy.** Windows → tabs → groups → widgets. Drag things around, right-click for more.
- **Inspector.** Every setting on whatever you've got selected, plus a raw JSON box if you want full control.
- **Themes.** A bunch of presets, a quick "recolor everything from one accent" option, and every ImGui
  color and size if you want to go deep.
- **Assets.** Fonts (any .ttf/.otf, Font Awesome icons are built in), images/logos, and an icon browser.
- **Widgets.** Animated toggles, keybinds, multi-select combos, sidebar/top/pill tabs, group boxes,
  sliders, color pickers, text inputs, images, progress bars, decorative bars.
- **Claude panel.** Type what you want changed and Claude edits the menu while you watch.
- **Code panel.** Shows the C++ it's going to export, live.

Undo/redo works on everything and it autosaves, so you can't really lose work.

### Shortcuts

| | |
|---|---|
| Ctrl+N / Ctrl+O | New / open a project |
| Ctrl+S / Ctrl+Shift+S | Save / save as |
| Ctrl+Z / Ctrl+Y | Undo / redo |
| Ctrl+D, Del | Duplicate, delete |
| Ctrl+C / Ctrl+V | Copy / paste widgets |
| Ctrl+E | Edit ↔ interact mode |
| Esc | Deselect |

## Setting up Claude

The Claude panel runs [Claude Code](https://claude.com/claude-code) in the background inside your
project folder. It can only touch your menu files (no shell access beyond the editor's own helper
commands), and it keeps the conversation going between prompts. Whatever you have selected gets sent
along, so "make this bigger" just works.

You need to log in once: run `claude` in a terminal and type `/login`. If you have the Claude desktop
app installed, the editor can use the copy that comes with it. The gear icon in the Claude panel
shows which one it found. For the hard stuff, pick Opus in the model dropdown.

## Exporting

File → Export (or run with `--export`) dumps everything into `<project>/export/`:

- `menu_generated.h` with a `menu::State` struct holding every value, plus `LoadFonts`, `LoadImages`,
  `ApplyStyle`, `ApplyTheme(i)` for each theme, and `Render(state)`
- `mm_widgets.h/.cpp`, the custom widget code (add the `.cpp` to your build)
- `assets/` with your fonts and images
- `example/`, a tiny GLFW + OpenGL3 app if you want to try the menu on its own

Hooking it up is basically:

```cpp
#include "menu_generated.h"

menu::LoadFonts("path/to/assets");
menu::ApplyStyle();
static menu::State state;

// every frame
menu::Render(state);
if (state.aim_enabled) { /* your stuff */ }
```

Needs Dear ImGui 1.92 or newer.

## How it's laid out

```
src/
  main.cpp            startup, window, command line options
  editor*.cpp         the editor UI (core, panels, Claude panel, file dialogs)
  model.cpp           the menu format: widget types, tree helpers, reading props
  renderer.cpp        draws a menu straight from the JSON (the canvas)
  exporter.cpp        turns a menu into C++
  theme.cpp           themes and presets
  assets.cpp          fonts, images, icons
  bridge.cpp          talks to Claude Code
  tools.cpp           the `tool` command line helpers
  util.cpp            file/shell/string bits
runtime/              mm_widgets: the custom widgets, shared by the editor and the exported code
data/                 templates and the export example
third_party/          Dear ImGui (docking), nlohmann/json, stb, Font Awesome 6
```

Each menu project is just a folder with `menu.json` (the whole design), an `assets/` folder and a
`CLAUDE.md` that tells Claude how the format works. You can edit `menu.json` by hand too; the editor
picks up changes instantly.
