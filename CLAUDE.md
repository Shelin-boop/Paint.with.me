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
- Distribution: public and meant for other people too, so license and branding
  must be clean.
- Delivery: an AppImage that adds a start-menu entry on first launch. A `.deb`
  is built on demand as well.
- Target desktop of the owner: Kubuntu, KDE Plasma, Wayland session.
- Interface: English only. No translation system.
- File formats: no format of our own. Layered: open `.pdn` (Paint.NET), open
  and save `.psd` (Adobe). Flat: PNG, JPEG, BMP, GIF, TIFF, WebP and the like.
  The inherited `.psw` format is to be removed.
- Undo: to be reworked into a memory-efficient history (store only what
  changed, with a memory budget instead of a fixed step count).
- New tools: a smudge tool (German "verwischen", drags color like a finger
  through wet paint) and a blur brush. Paint.NET has neither.
- Clean up code that is badly structured, as part of the work.
- Default panel layout (owner's choice, 2026-11-05): Tools above Colors on the
  left, History above Layers on the right. The Tools panel widens to 3 or 4
  columns for pinned menu commands and brush presets.

## Roadmap (owner may reorder)

1. Rebrand and English-only UI. Done 2026-11-05.
2. AppImage for Kubuntu: start-menu entry on first launch, current Qt,
   release on GitHub. Done 2026-11-05. Native Wayland waits for step 6.
3. Speed with large images (owner's test case: 5000 x 5000 px, 16 layers,
   600 dpi) and panels docked inside the main window. Done 2026-11-05.
4. File formats: remove `.psw`; layered PSD read and write; autosave keeps
   layers.
5. Undo rework: memory budget instead of 100 steps. (Image edits already
   store only the changed rectangle.)
6. Code cleanup, especially `src/mainwindow.cpp` and a tool registry. Then
   ship Qt's Wayland plugin in the AppImage, test on Plasma and drop the
   XWayland preference in `src/main.cpp`.
7. Smudge tool and blur brush.

## Rules

- Push directly to `main`; no pull requests needed. Small, focused commits.
- Never force-push and never rewrite published history.
- Leave the upstream repository alone: never push to it, open issues or pull
  requests there, or comment. Reading and merging from it is fine.
- Keep the upstream copyright line in LICENSE and the credits in the About
  dialog and AppStream metadata.
- Do not use the name "Paint.NET", its logo or its icons in the product name,
  icons or app ID. Mentioning it to describe compatibility is fine.
- Never copy code from current Paint.NET (closed source). Paint.NET 3.36 code is
  MIT but is C#; porting algorithms from it requires keeping its notice.
- Keep `tests/data/pdn/LICENSE-pypdn` with the pypdn test files.
- Write documentation and UI text in American English. Dates as yyyy-mm-dd,
  times in 24-hour format, SI units. Menus use "..." (three dots), not "…".
- Every change keeps all six test suites passing; new behavior gets a test.

## Build and test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --build build --target pwm_tests pwm_brush_tests pwm_paste_tests pwm_preview_tests pwm_panel_tests pwm_integration_tests
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Ubuntu 24.04 packages: `qt6-base-dev qt6-image-formats-plugins libgl1-mesa-dev
zlib1g-dev cmake ninja-build`. Qt 6.4 is the minimum. Everything except
`main.cpp` is compiled once into the object library `pwm_app`, which the app and
all test programs link; a clean build of everything takes about 2 minutes on
2 cores.

Speed benchmark (not in ctest; needs about 3 GB of memory):

```bash
cmake --build build --target pwm_bench
QT_QPA_PLATFORM=offscreen build/pwm_bench          # 5000 x 5000 px, 16 layers
QT_QPA_PLATFORM=offscreen build/pwm_bench --quick  # 2000 x 2000 px, 8 layers
```

Run it before and after any change to compositing, the canvas, painting tools,
panels that react to document changes, undo or autosave.

Pictures of the main window for a visual check (default layout, four columns
with shortcuts, narrow panel):

```bash
PWM_SCREENSHOT_DIR=/tmp/shots QT_QPA_PLATFORM=offscreen build/pwm_panel_tests
```

## Releases

- `.github/workflows/appimage.yml`, started by hand (Actions tab, or
  `gh api repos/Shelin-boop/Paint.with.me/actions/workflows/appimage.yml/dispatches -f ref=main`),
  replaces the rolling `continuous` pre-release ("Latest build").
- Pushing a tag `vX.Y.Z` creates a normal release.
- The AppImage uses Qt 6.12.0 (pinned in the workflow; the input "latest" tries
  the newest Qt 6). It needs glibc 2.35 or newer (Ubuntu 22.04 and later).
- Version shown to users: `<project major.minor>.<commit count>`, for example 0.1.115.
- The CI workflow (`ci.yml`) runs on every push; the AppImage workflow does not,
  to save build minutes on the private repository.

## Code map

| Area | Where |
|---|---|
| App names, IDs, version macro | `src/appinfo.h` |
| AppImage menu entry (install, update, remove) | `src/desktopintegration.cpp` |
| Main window, menus, tool registration, shortcuts | `src/mainwindow.cpp` (about 3,800 lines) |
| Document, layers, selection, undo history | `src/core/` |
| `.pdn` reader (own MS-NRBF parser, read-only) | `src/core/pdnformat.cpp` |
| Inherited `.psw` format (to be removed) | `Document::saveNative` in `src/core/document.cpp` |
| Combining layers (strips on all cores, reduced size) | `Document::compositeInto`, `Layer::compositeInto` |
| Canvas widget, zoom, rendering cache | `src/canvas/canvaswidget.cpp` |
| Thumbnails (Layers panel, image list), cached | `src/core/thumbnails.cpp` |
| Tools (one class per tool, base `Tool`) | `src/tools/` |
| Adjustments (base `Adjustment::apply(QImage)`) | `src/adjustments/` |
| Effects (base `Effect::apply(QImage)`) | `src/effects/` |
| Panels: layers, colors, history, tool options | `src/panels/` |
| Tools panel: tool grid, extra columns, pinned commands and presets | `src/panels/toolspanel.cpp` |
| Brush presets (saved tool settings, JSON in the settings) | `src/tools/brushpreset.cpp` |
| Default panel layout and reset | `MainWindow::applyDefaultPanelLayout`, `resetUtilityWindow` |
| Tool icons, drawn in code (no image files) | `src/toolicons.cpp` |
| App icon (SVG source and PNG sizes) | `packaging/icons/` |
| Plugin C ABI (kept compatible with paint.software) | `src/plugins/plugin_api.h` |
| Tests (custom harness, not QTest) | `tests/` |

### Adding a tool touches all of these (until the cleanup adds a registry)

1. `ToolType` enum in `src/tools/tool.h`.
2. New `src/tools/<name>tool.{h,cpp}`, listed in `TOOLS_SOURCES` in CMakeLists.txt.
3. `src/mainwindow.cpp`: `m_tools.push_back(...)`, `addToolShortcut(...)`, the
   status-bar hint, the tool-grid entry (list in `createToolsPanel`) and the
   per-tool `switch` statements.
4. `src/tools/tool.cpp`: a stable name in `kToolKeys` (used by brush presets).
5. `src/toolicons.cpp`: draw the icon.
6. `src/panels/tooloptionspanel.cpp`: name list and which options are shown.
7. A test in `tests/`.

A tool that changes pixels while the mouse moves reports the changed document
area with `canvas.updateCanvasRegion(rect)` and returns `true` from
`updatesCanvasRegion()`; otherwise the whole image is combined again on every
mouse move. `tests/test_brush.cpp` compares such tools against a full redraw.

### Speed rules (large images)

- Nothing may combine, copy or scale whole layers on the UI thread for every
  event. Caches are keyed by `QImage::cacheKey()`, which changes whenever the
  pixels can have changed.
- The canvas keeps a combined image; it is redone only when
  `renderSignature()` (content version, visibility, opacity, blend mode and
  position of every layer) changes, or area by area through
  `updateCanvasRegion()`. Zoomed out below 50 %, it combines at half, quarter,
  ... size (exact integer averaging, so areas and the whole always agree).
- Image-edit undo steps keep only the changed rectangle (`ImageEditCommand`).
- Autosave combines and writes the PNG in a background thread.

### Settings

All settings go through `QSettings(AppInfo::kId, AppInfo::kId)`, stored in
`~/.config/paint-with-me/paint-with-me.conf`.
