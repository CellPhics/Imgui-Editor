# ImGui Menu Editor project

Helper command for this project (run from this folder): `{{TOOL}}` with `render`, `compare`,
`inspect`, `crop`, `fonts`, `import-font`. Run each as its own command (no pipes or `&&`). See "Recreating a screenshot" below.

This folder is a Dear ImGui mod-menu design. **Everything lives in `menu.json`**; the
editor hot-reloads it whenever the file is saved, so edit it directly. Keep it valid
JSON, keep every `id` unique, and change only what the user asked for.

The user's prompt ends with an `[Editor context]` block that names the node currently
selected in the editor. "this", "it" and "selected" mean that node.

## File layout

```jsonc
{
  "version": 1,
  "name": "Nexus",                 // menu name (used in exported code comments)
  "theme": "Midnight",             // active theme: a key of "themes"
  "themes": { "Midnight": THEME, "Crimson": THEME },
  "fonts":  [ { "id": "main", "path": "assets/fonts/Roboto-Medium.ttf", "size": 15 } ], // first = default
  "images": [ { "id": "logo", "path": "assets/images/logo.png" } ],
  "windows": [ WINDOW, ... ]
}
```

Fonts and images must be files under `assets/`. Font Awesome 6 (solid) icons are
merged into every font automatically.

## Nodes

Every node is an object with `"id"` (unique, snake_case) and `"type"`. Containers
have `"children": [...]`. Props not listed fall back to defaults; leave them out
unless you need them.

Props every widget accepts: `label`, `icon` (Font Awesome name such as `crosshairs`, `eye`,
`gear`, `bolt`, `user`, `skull`, `shield-halved`, `palette`, `floppy-disk`, `wand-magic-sparkles`),
`tooltip`, `same_line` (bool: same row as the previous widget), `width` (0 = default,
-1 = fill, >0 = px), `disabled`, `hidden`, `var` (C++ variable name in the export), and:

- `pos` [x,y]: exact position in pixels relative to the parent window/group's top-left corner
  (overrides normal flow; use for pixel-exact layouts).
- `offset` [dx,dy]: nudge from the normal flow position.
- `style`: per-node overrides that apply to the node **and everything inside it**:
  `{"colors": {"Button": "#2A2A35", "Text": [1,1,1,1]}, "vars": {"FrameRounding": 2, "ItemSpacing": [8,4]}, "mm": {"accent": "#FF3355"}}`.
  Works on windows, groups, columns, tabs and every widget. This is how you match one-off colors
  (a red button, a group with a different background via `ChildBg`, a window border via `Border`).

| type | container | key props |
|---|---|---|
| `window` | yes | `title`, `pos` [x,y], `size` [w,h], `no_title_bar` (default true), `no_resize`, `no_move`, `no_collapse`, `no_scrollbar`, `no_background`, `padding` [x,y] or null (use [0,0] with a sidebar), `fade` (fade-in), `font` (font id) |
| `tabs` | children must be `tab` | `style`: `sidebar` \| `top` \| `pills`; `highlight`: `bar` (accent tint + marker) \| `box` (selected tab takes the content's WindowBg with Border lines above/below, opening into the content) \| `fill` \| `none`; `layout`: `row` \| `stacked` (icon above label); `icon_only`; `icon_size` (px); `image_size`; `tint_images` (default true: images are colored like tab text, dim when unselected); `tab_height`; `tab_gap`; `separators` (lines between tabs); `sidebar_border`; `sidebar_padding` [x,y]; `sidebar_width`; `logo` (image id); `logo_size`; `header` (brand text); `header_font`; `header_size`; `default` (selected index) |
| `tab` | yes | `label`, `icon`, `image` (image id: artwork used instead of the icon) |
| `group` | yes | `title`, `title_style`: `inside` (title + accent underline) \| `border` (title set into the top border line, classic group box) \| `plain`; `size` [w,h] (0 width = fill, 0 height = fit contents), `border`, `same_line` |
| `columns` | yes | `count`; each child goes in the next column (usually `group`s) |
| `text` | | `text`, `color` [r,g,b,a] or "#RRGGBB", `font`, `size`, `align` left\|center\|right, `wrap` |
| `separator` | | `label` (optional) |
| `spacing` | | `height` |
| `button` | | `size` [w,h] (-1 width = fill) |
| `checkbox` | | `default` bool, `look`: `default` (check mark) \| `filled` (solid square fill) \| `circle`; `box_size` px for filled/circle (small squares like 6-8 px are common) |
| `toggle` | | `default` bool. Animated switch (label left, switch right when `mm.toggle_right`) |
| `slider_float`, `slider_int` | | `min`, `max`, `default`, `format` (printf, e.g. "%.0f deg"), `look`: `default` (ImGui grab) \| `thin` (label left + value right on one row, thin track with a knob below) \| `filled` (frame fills up to the value, value centered) \| `line` (thin bar filled with a SliderGrab→SliderGrabActive vertical gradient, value text centered under the bar, label above); `track_height` px; `markers` (draw - and + at the ends); `width` px works with every look |
| `rect` | | decorative bar/strip/divider: `size` [w,h] (w -1 = fill), `color`, `color2` (gradient), `vertical`, `rounding` |
| `combo` | | `items` [strings], `default` index |
| `multi_combo` | | `items`, `default` [bools] |
| `radio` | | `items`, `default` index, `horizontal` |
| `color` | | `default` [r,g,b,a], `alpha`, `inline` (label left, swatch on the right) |
| `keybind` | | `default`: ImGuiKey name: `None`, `F`, `Insert`, `MouseX1`, `LeftAlt`, `F5`, ... |
| `input_text` | | `hint`, `default`, `max_length`, `password` |
| `image` | | `image` (image id), `size`, `rounding`, `align` |
| `progress` | | `default` 0..1, `overlay`, `size` |

Typical mod menu: `window` (padding [0,0]) → `tabs` (style sidebar) → `tab`s →
`columns` (count 2) → `group`s → widgets.

## Recreating a screenshot

When the user attaches a reference image, match it as closely as you can. Work in passes:

1. **Look.** Read the image. Identify the structure: windows, title bar or not, sidebar vs top tabs,
   groups/columns, every widget with its exact text, icons, and which tab is shown.
2. **Measure.** Run `{{TOOL}} inspect IMAGE --top 20` for the image size and its palette, then sample
   exact colors: `{{TOOL}} inspect IMAGE x,y ...` (window background, sidebar, group background,
   borders, accent, text, frames). Zoom in on small details with
   `{{TOOL}} crop IMAGE x y w h .bridge/zoom.png --scale 3` and Read the result.
   Estimate pixel geometry from the image: window size, sidebar width, group positions and sizes,
   row spacing, frame heights, rounding, font size (cap height ≈ 0.7 × font size).
3. **Fonts.** Pick the closest font: bundled Roboto-Medium / Karla / Cousine / DroidSans, or a system font
   (`{{TOOL}} fonts dejavu`, then `{{TOOL}} import-font PATH`). Set sizes per font entry and per text.
   Classic game-cheat menus use Verdana/Tahoma at 11-13 px: use DejaVu Sans (Bold) — it is derived from
   the same design as Verdana. Match the font size by measuring text height in the reference.
4. **Build.** Keep the user's existing themes: add a new theme for the reference and make it active.
   Write menu.json: window `size` = the menu's pixel size in the image, `padding` [0,0] for
   edge-to-edge layouts, a theme whose colors/vars come from your samples, `look` variants for sliders
   and checkboxes, `style` overrides for one-off colors, `pos`/`offset` when flow layout can't place
   something exactly, `rect` for decorative bars (e.g. a gradient line along the top of the window).
   **Artwork:** anything Font Awesome can't match (big sidebar icons, logos, weapon pictures) should be
   cropped from the reference with `{{TOOL}} crop` into `assets/images/` (crop each icon tightly, one
   file per icon, and add `--key auto` so its background becomes transparent), registered in `images`, and used via tab `image` / `image` widgets / `logo`. For
   tab icons that change brightness when selected, crop the brightest version and keep `tint_images`.
   Small text tags at the end of a row (like `[MS]`) are a `text` with `same_line` and `align: right`.
5. **Compare.** `{{TOOL}} render --show <a node on the visible tab>` writes `.bridge/render.png` and
   prints each widget's rectangle. Then `{{TOOL}} compare REFERENCE .bridge/render.png` gives a
   similarity score, any size mismatch, and the most different areas with the reference vs render
   colors (plus `.bridge/diff.png`, differences in red). Fix the biggest differences first (window
   size, layout, then colors, then fonts and small offsets) and repeat. Do at least 6 passes and aim
   for 97%+ similarity with no size mismatch. Don't stop while it's still improving.
6. **Report** briefly what matches and anything you couldn't reproduce exactly.

## Themes

```jsonc
THEME = {
  "mm":     { "accent": [r,g,b,a], "sidebar_bg": [...], "toggle_off": [...], "text_dim": [...],
              "group_title": [...], "anim_speed": 12, "animations": true, "tab_height": 36,
              "tab_rounding": 6, "toggle_right": true, "labels_on_top": true },
  "vars":   { "WindowRounding": 10, "FrameRounding": 6, "WindowPadding": [14,14], "ItemSpacing": [10,8], ... },
  "colors": { "WindowBg": [r,g,b,a], "ChildBg": [...], "FrameBg": [...], "Button": [...], "SliderGrab": [...], ... }
}
```

`colors` keys are Dear ImGui `ImGuiCol_` names without the prefix (Text, WindowBg, ChildBg,
PopupBg, Border, FrameBg, FrameBgHovered, FrameBgActive, CheckMark, SliderGrab,
SliderGrabActive, Button, ButtonHovered, ButtonActive, Header, HeaderHovered,
HeaderActive, Separator, ScrollbarGrab, Tab, TabHovered, TabSelected, TextSelectedBg, ...).
`vars` keys are `ImGuiStyle` member names. Colors are floats 0..1 or "#RRGGBB(AA)".
When changing the accent color, also update `mm.accent` and the accent-derived colors
(CheckMark, SliderGrab, ButtonActive, Header*, TabSelected, TextSelectedBg) so the theme
stays consistent. To add a theme, copy an existing one under a new key.

## Bridge inbox (only for a Claude session watching this folder)

When the editor's bridge is in **inbox** mode it appends prompts to
`.bridge/inbox.jsonl` as `{"id", "prompt", "full_prompt", "selected", "selected_type"}`.
Reply by appending one JSON line per message to `.bridge/outbox.jsonl`:
`{"id": <same id>, "text": "what you changed", "done": true}`.
