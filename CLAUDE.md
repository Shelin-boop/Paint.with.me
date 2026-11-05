# Paint.with.me: notes for Claude sessions

## What this project is

A Paint.NET look-alike image editor for Linux in C++17 and Qt 6, forked from
[paint.software](https://github.com/Univers4craft/paint.software) (MIT) at
upstream commit `db9691f`. Details of the fork: FORK.md.

The owner (GitHub: Shelin-boop) is not a programmer. Claude builds the
software. Explain decisions in plain language and give effort estimates in
Claude sessions and milestones, not in hours of human work.

## Decisions so far

- Stack: C++17, Qt 6 Widgets, CMake. Keep it; do not port to another toolkit.
- Distribution: public and meant for other people too, so licence and branding
  must be clean.
- Delivery: an AppImage that adds a start-menu entry on first launch.
- Target desktop of the owner: Kubuntu, KDE Plasma, Wayland session.
- v1 must-haves: .pdn import, layers, brushes, basic tools including selection,
  colour adjustments (brightness/contrast and similar), and a smudge tool
  (German "verwischen"), which Paint.NET does not have. Confirm with the owner
  that "smudge" is what they mean before building it.
- Name: Paint.with.me. The rebrand away from "paint.software" is still open
  (FORK.md lists every place).

## Rules

- Never force-push and never rewrite published history.
- Work on a branch (`claude/<topic>`), open a pull request, and let CI pass.
  The owner merges unless they say otherwise.
- Keep the upstream copyright line in LICENSE. Keep upstream file headers.
- Do not use the name "Paint.NET", its logo or its icons in the product name,
  icons or app ID. Mentioning it to describe compatibility is fine.
- Never copy code from current Paint.NET (closed source). Paint.NET 3.36 code is
  MIT but is C#; porting algorithms from it requires keeping its notice.
- Keep `tests/data/pdn/LICENSE-pypdn` with the pypdn test files.
- Publishing workflows are manual-only until the rebrand. Do not re-enable
  their push triggers without the owner's go-ahead.

## Build and test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --build build --target paintdotnet_tests paintdotnet_brush_tests \
  paintdotnet_paste_tests paintdotnet_preview_tests
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Ubuntu 24.04 packages: `qt6-base-dev qt6-image-formats-plugins libgl1-mesa-dev
zlib1g-dev cmake ninja-build`. Qt 6.4 is the minimum.

## Code map

| Area | Where |
|---|---|
| Main window, menus, tool registration, shortcuts | `src/mainwindow.cpp` (about 3,800 lines) |
| Document, layers, selection, undo history | `src/core/` |
| `.pdn` reader (own MS-NRBF parser, read-only) | `src/core/pdnformat.cpp` |
| Native layered format `.psw` (QDataStream of QImages) | `Document::saveNative` in `src/core/document.cpp` |
| Canvas widget, zoom, rendering | `src/canvas/canvaswidget.cpp` |
| Tools (one class per tool, base `Tool`) | `src/tools/` |
| Adjustments (base `Adjustment::apply(QImage)`) | `src/adjustments/` |
| Effects (base `Effect::apply(QImage)`) | `src/effects/` |
| Panels: layers, colours, history, tool options | `src/panels/` |
| Tool icons, drawn in code (no image files) | `src/toolicons.cpp` |
| UI translations | `src/i18n.cpp` |
| Plugin C ABI | `src/plugins/plugin_api.h` |
| Tests (custom harness, not QTest) | `tests/` |

### Adding a tool touches all of these

1. `ToolType` enum in `src/tools/tool.h`.
2. New `src/tools/<name>tool.{h,cpp}`, listed in `TOOLS_SOURCES` in CMakeLists.txt.
3. `src/mainwindow.cpp`: `m_tools.push_back(...)`, `addToolShortcut(...)`, the
   status-bar hint, the tool-grid entry and the per-tool `switch` statements.
4. `src/toolicons.cpp`: draw the icon.
5. `src/panels/tooloptionspanel.cpp`: name list and which options are shown.
6. `src/i18n.cpp`: translations.
7. A test in `tests/`.

### Translations

UI source strings are **French**. `TR("…")` looks them up in a French-to-English
table in `src/i18n.cpp`; there is no German yet. Write new UI strings in French
inside `TR()` and add the English entry, or plan a move to Qt Linguist first.
