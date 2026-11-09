# ImGui Menu Editor

A native Dear ImGui editor for designing mod menus. Change the menu by clicking,
or type a prompt and Claude edits it live. When you're done, export clean C++.

## Build

Needs CMake, a C++20 compiler, GLFW and OpenGL (all dependencies are in `third_party/`).

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/imgui-menu-editor                 # opens ~/ImGuiMenus/MyMenu (created on first run)
./build/imgui-menu-editor ~/ImGuiMenus/Other
./build/imgui-menu-editor ~/ImGuiMenus/Other --export   # headless export to <project>/export
```

## How it works

- A **project** is a folder holding `menu.json` (the whole design), `assets/` (fonts, images)
  and `CLAUDE.md` (the schema guide Claude reads).
- The editor autosaves `menu.json` on every change and **hot-reloads** it when something else
  edits it (Claude, your text editor, git).
- The preview draws with the same widget code (`runtime/mm_widgets.*`) that the export uses,
  so the exported menu looks exactly like the preview.

### Panels

| Panel | Use |
|---|---|
| Canvas | Live menu. **Edit** mode: click to select, drag to move/resize windows. **Interact** mode (Ctrl+E): use it as a player would |
| Hierarchy | Tree of windows → tabs → groups → widgets. Drag and drop to re-parent, right-click for more |
| Inspector | Every property of the selection, plus a raw JSON editor |
| Theme | Multiple themes, presets, quick recolor from an accent, every ImGui color and size |
| Assets | Fonts (any .ttf/.otf, Font Awesome icons merged), images/logos, icon browser |
| Claude | Prompt box: the bridge to Claude |
| Code | Live preview of the generated `menu_generated.h` |

Shortcuts: Ctrl+Z / Ctrl+Y undo/redo, Ctrl+D duplicate, Del delete, Ctrl+C / Ctrl+V copy/paste
nodes (as JSON), Ctrl+E edit/interact, Esc deselect.

## Claude bridge

**Claude Code CLI** (default): each prompt runs Claude Code headless in the project folder
(`claude -p ... --output-format stream-json`), limited to file tools (Read, Edit, Write, Glob, Grep)
plus the editor's own `tool` helpers. It has no other shell access. Claude edits `menu.json` and the editor reloads it as each edit lands. The conversation
continues across prompts (New chat resets it). The selected widget is sent as context, so
"make this red" works.

One-time setup: the CLI needs its own login. Install Claude Code (https://claude.com/claude-code)
or use the copy bundled with the desktop app (the gear menu in the Claude panel shows which one
the editor found), run it once in a terminal and use `/login`.

### Recreating a menu from a screenshot

Give Claude a reference image in the Claude panel, using any of these:

- **Ctrl+V** with an image (or a copied image file) on the clipboard
- **Drag and drop** image files onto the editor window (dropping `.ttf`/`.otf` files adds fonts)
- **Attach** opens a file picker
- **Capture** lets you drag a box around any part of your screen (grim + slurp on Wayland, ImageMagick `import` on X11)

Press Send. An empty prompt means "recreate this as accurately as possible", or describe what you want
("copy the sidebar from this, keep my widgets"). Claude reads the image, samples exact colors, zooms into
details, picks or imports matching fonts, builds the menu, then renders it and compares it with your
screenshot, fixing differences over several passes. Opus usually gets closer than Sonnet on complex menus.

Claude can use these helpers (you can run them too):

```bash
./build/imgui-menu-editor tool render  [--project DIR] [--out FILE] [--show NODE_ID]   # menu -> PNG + widget rectangles
./build/imgui-menu-editor tool inspect IMAGE [--top N] [x,y ...] [x,y,w,h ...]         # palette and exact pixel colors
./build/imgui-menu-editor tool compare REFERENCE RENDER                                # similarity + where they differ
./build/imgui-menu-editor tool crop    IMAGE x y w h OUT.png [--scale N] [--key auto]  # cut out / zoom in
./build/imgui-menu-editor tool fonts   [filter]                                        # installed fonts
./build/imgui-menu-editor tool import-font PATH                                        # copy a font into assets/fonts
```

Building blocks for pixel-accurate copies:

- `pos` / `offset` on any widget, and per-widget `style` overrides (colors, sizes, accent)
- sliders: `look` `thin` / `filled` / `line` (value under a gradient bar), `track_height`, `markers` (− / +)
- checkboxes: `look` `filled` / `circle`, `box_size`
- tabs: image or large icons (`image`, `icon_size`, `icon_only`, `layout`), `highlight` `bar` / `box` / `fill`,
  `separators`, `sidebar_border`, `tab_height`
- groups: `title_style` `border` (title set into the frame, classic group box)
- `rect` for decorative bars and gradients
- artwork cropped straight from the screenshot (`tool crop ... --key auto` removes the background)

**Inbox file**: prompts are appended to `<project>/.bridge/inbox.jsonl`; replies are read from
`.bridge/outbox.jsonl`. Use this when a Claude session is already watching the project folder.

## Export

File → Export (or `--export`) writes to `<project>/export/`:

- `menu_generated.h`: `menu::State` (every value), `LoadFonts`, `LoadImages`, `ApplyStyle`,
  `ApplyTheme(i)` for every theme, `Render(state)`
- `mm_widgets.h/.cpp`: the custom widget runtime (add the `.cpp` to your build)
- `assets/`: fonts and images
- `example/`: a small GLFW + OpenGL3 app to try the export on its own

## Layout

```
src/          editor (model, renderer, theme, assets, bridge, exporter, UI)
runtime/      mm_widgets: shared by the preview and exported code
data/         CLAUDE.md template, export example
third_party/  Dear ImGui (docking), nlohmann/json, stb, Font Awesome 6
```
