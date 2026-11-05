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

## Roadmap (owner may reorder)

1. Rebrand and English-only UI. Done 2026-11-05.
2. AppImage for Kubuntu: start-menu entry on first launch, current Qt,
   release on GitHub. Done 2026-11-05. Native Wayland waits for step 5.
3. File formats: remove `.psw`; layered PSD read and write; autosave keeps
   layers.
4. Undo rework.
5. Code cleanup, especially `src/mainwindow.cpp` and a tool registry. Move
   the floating panels inside the main window, then drop the XWayland
   preference in `src/main.cpp` so the app runs natively on Wayland.
6. Smudge tool and blur brush.

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
- Every change keeps all five test suites passing; new behavior gets a test.

## Build and test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --build build --target pwm_tests pwm_brush_tests pwm_paste_tests pwm_preview_tests pwm_integration_tests
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Ubuntu 24.04 packages: `qt6-base-dev qt6-image-formats-plugins libgl1-mesa-dev
zlib1g-dev cmake ninja-build`. Qt 6.4 is the minimum. On a 2-core machine the
test targets take about 15 minutes to build, because three of them compile
`mainwindow.cpp` again.

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
| Canvas widget, zoom, rendering | `src/canvas/canvaswidget.cpp` |
| Tools (one class per tool, base `Tool`) | `src/tools/` |
| Adjustments (base `Adjustment::apply(QImage)`) | `src/adjustments/` |
| Effects (base `Effect::apply(QImage)`) | `src/effects/` |
| Panels: layers, colors, history, tool options | `src/panels/` |
| Tool icons, drawn in code (no image files) | `src/toolicons.cpp` |
| App icon (SVG source and PNG sizes) | `packaging/icons/` |
| Plugin C ABI (kept compatible with paint.software) | `src/plugins/plugin_api.h` |
| Tests (custom harness, not QTest) | `tests/` |

### Adding a tool touches all of these (until the cleanup adds a registry)

1. `ToolType` enum in `src/tools/tool.h`.
2. New `src/tools/<name>tool.{h,cpp}`, listed in `TOOLS_SOURCES` in CMakeLists.txt.
3. `src/mainwindow.cpp`: `m_tools.push_back(...)`, `addToolShortcut(...)`, the
   status-bar hint, the tool-grid entry and the per-tool `switch` statements.
4. `src/toolicons.cpp`: draw the icon.
5. `src/panels/tooloptionspanel.cpp`: name list and which options are shown.
6. A test in `tests/`.

### Settings

All settings go through `QSettings(AppInfo::kId, AppInfo::kId)`, stored in
`~/.config/paint-with-me/paint-with-me.conf`.
