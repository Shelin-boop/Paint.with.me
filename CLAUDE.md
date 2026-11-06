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
  and save `.psd` (Adobe), open `.psb`. Flat: PNG, JPEG, BMP, GIF, TIFF, WebP
  and the like. The inherited `.psw` format was removed on 2026-11-05.
- Undo: a memory budget instead of a fixed step count (done 2026-11-05).
- Tools Paint.NET does not have: perspective guides (2026-11-05), smudge
  (German "verwischen", drags color like a finger through wet paint) and blur
  brush (both 2026-11-06, key U).
- Owner's pen display: XPPen Artist 15.6 Pro V2 on Kubuntu; pen pressure
  works under XWayland and native Wayland (confirmed by the owner 2026-11-06).
  Pen extras (2026-11-06): pressure on size, opacity or both (per tool, tool
  bar "Pressure"), pressure curve and tilt (Settings > Tools).
- Window system: native Wayland by default in a Wayland session since
  2026-11-06 (owner tested it on Plasma: pen, panels and sharpness fine);
  XWayland stays as a compatibility choice (Settings > User Interface >
  Window System, setting `display/windowSystem` = `wayland` / `xwayland`).
  Changing it offers "Restart Now". `QT_QPA_PLATFORM` or `-platform` always
  win.
- Antialiasing is one setting for all tools (Settings > Tools,
  `tools/antialiasing`), not a tool bar switch.
- Text (2026-11-06): the Text tool makes text frames, each a text layer that
  stays editable until Layers > Convert Text to Pixels. Font sizes are points
  at the image's dpi (1 pt = 1/72 in). Painting tools are kept off text layers
  (status message); menu commands that change pixels ask to convert first.
- Sizes (2026-11-06): New, Resize and Canvas Size take pixels or a print size
  in mm, cm or in, and a resolution in px/in or px/cm; a new resolution keeps
  whichever was typed last (pixels or print size). Units are remembered
  (`units/length`, `units/resolution`); default cm on metric locales.
- PDF (2026-11-06): File > Extract Picture from PDF (also File > Open and
  drag and drop of a .pdf) takes pictures out, first one chosen; pages are not
  rendered.
- The AppImage carries a PNG `.DirIcon`, so file managers can show its icon
  (Dolphin: Configure Dolphin > Previews > "AppImage").
- Clean up code that is badly structured, as part of the work.
- Default panel layout (owner's choice, 2026-11-05): Tools above Colors on the
  left, History above Layers on the right. The Tools panel widens to 3 or 4
  columns for pinned menu commands and brush presets.

## Roadmap (owner may reorder)

1. Rebrand and English-only UI. Done 2026-11-05.
2. AppImage for Kubuntu: start-menu entry on first launch, current Qt,
   release on GitHub. Done 2026-11-05.
3. Speed with large images (owner's test case: 5000 x 5000 px, 16 layers,
   600 dpi) and panels docked inside the main window. Done 2026-11-05.
4. File formats: remove `.psw`; layered PSD read and write; autosave keeps
   layers. Done 2026-11-05.
5. Undo rework: memory budget instead of 100 steps. Done 2026-11-05. Also
   done on the owner's request: PDF export and a perspective tool.
6. Code cleanup and native Wayland. Done 2026-11-05: tool registry,
   `mainwindow.cpp` split by job, Qt's Wayland plugins in the AppImage.
   Native Wayland became the default on 2026-11-06 after the owner's test.
7. Smudge tool and blur brush. Done 2026-11-06, together with pen extras,
   icons for the Image, Layers, Adjustments and Effects commands, a filled
   Settings > Tools page and a bigger, centered open-images strip.
8. Editable text frames, print sizes with units and dpi, Extract Picture
   from PDF, AppImage file icon. Done 2026-11-06.

### Ideas for later (owner's list; ask which comes next)

- (empty: text frames were done on 2026-11-06)

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
- Every change keeps all twelve test suites passing; new behavior gets a test.

## Build and test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --build build --target pwm_tests pwm_brush_tests pwm_paste_tests pwm_preview_tests pwm_panel_tests pwm_psd_tests pwm_pdf_tests pwm_perspective_tests pwm_integration_tests pwm_units_tests pwm_text_tests pwm_pdfpictures_tests
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

Pictures for a visual check (main window layouts and a sheet of all command
icons; Settings pages; smudge and blur strokes):

```bash
PWM_SCREENSHOT_DIR=/tmp/shots QT_QPA_PLATFORM=offscreen build/pwm_panel_tests
PWM_SCREENSHOT_DIR=/tmp/shots QT_QPA_PLATFORM=offscreen build/pwm_paste_tests
PWM_SCREENSHOT_DIR=/tmp/shots QT_QPA_PLATFORM=offscreen build/pwm_brush_tests
PWM_SCREENSHOT_DIR=/tmp/shots QT_QPA_PLATFORM=offscreen build/pwm_units_tests        # New, Resize, Canvas Size
PWM_SCREENSHOT_DIR=/tmp/shots QT_QPA_PLATFORM=offscreen build/pwm_text_tests         # a text frame being edited
PWM_SCREENSHOT_DIR=/tmp/shots QT_QPA_PLATFORM=offscreen build/pwm_pdfpictures_tests  # the PDF picture dialog
```

Note: `cmake --build build` builds only the app; build the test targets
explicitly (the command above), or old test programs run.

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
| X11 / XWayland / native Wayland choice at startup | `src/displayserver.cpp`, called from `src/main.cpp` |
| Main window: construction, settings, tool switching, title, events | `src/mainwindow.cpp` |
| Main window: docks, Tools panel, default layout, floating panels, status bar | `src/mainwindow_panels.cpp` |
| Main window: menus, shortcuts, toolbar, perspective commands | `src/mainwindow_menus.cpp` |
| Main window: open, save, PDF export, autosave, recovery, drag and drop | `src/mainwindow_files.cpp` |
| Main window: edit, image and layer commands | `src/mainwindow_edit.cpp` |
| Main window: effects and adjustments (one numeric `switch`) | `src/mainwindow_effects.cpp` |
| Every tool's key, name, shortcut, panel slot, tooltip, hint; creating tools | `src/tools/toolregistry.cpp` |
| Pen: pressure curve, tilt, settings; shared dab code (size, opacity, tilt) | `src/core/pensettings.cpp`, `Tool::dabAt`, `forEachDab`, `paintDab` |
| Smudge tool and blur brush (shared base `RetouchTool`) | `src/tools/retouchtool.cpp` |
| Icons for the Image, Layers, Adjustments, Effects commands | `src/commandicons.cpp` |
| Settings dialog; pen widgets; keyboard shortcut table | `src/dialogs/settingsdialog.cpp`, `penwidgets.cpp`, `shortcutshelp.cpp` |
| Restart after changing the window system | `MainWindow::restartApplication`, `DisplayServer::relaunch` |
| Document, layers, selection, undo history | `src/core/` |
| Undo memory budget (steps report their images; shared pixels count once) | `HistoryManager::memoryUsed`, `trim` in `src/core/history.cpp` |
| PDF export (own writer) and its dialog | `src/core/pdfexport.cpp`, `src/dialogs/pdfexportdialog.cpp` |
| Perspective guides: geometry, drawing, settings; the tool; canvas snapping | `src/core/perspective.cpp`, `src/tools/perspectivetool.cpp`, `CanvasWidget::perspectiveSnapped` |
| `.pdn` reader (own MS-NRBF parser, read-only) | `src/core/pdnformat.cpp` |
| `.psd` / `.psb` reader and `.psd` writer | `src/core/psdformat.cpp` |
| Saving and opening, resolution, load notes | `Document::save`, `Document::load` in `src/core/document.cpp` |
| Save dialogs, PSD warnings, autosave, recovery | `MainWindow::saveDocumentAs`, `writeDocument`, `performAutosave`, `checkForRecovery` |
| Combining layers (strips on all cores, reduced size) | `Document::compositeInto`, `Layer::compositeInto` |
| Canvas widget, zoom, rendering cache | `src/canvas/canvaswidget.cpp` |
| Thumbnails (Layers panel, image list), cached | `src/core/thumbnails.cpp` |
| Tools (one class per tool, base `Tool`) | `src/tools/` |
| Adjustments (base `Adjustment::apply(QImage)`) | `src/adjustments/` |
| Effects (base `Effect::apply(QImage)`) | `src/effects/` |
| Panels: layers, colors, history, tool options | `src/panels/` |
| Tools panel: tool grid, extra columns, pinned commands and presets | `src/panels/toolspanel.cpp` |
| Open-images strip (thumbnails in the tool bar, centered) | `src/panels/imagelistbar.cpp` |
| Brush presets (saved tool settings, JSON in the settings) | `src/tools/brushpreset.cpp` |
| Default panel layout and reset | `MainWindow::applyDefaultPanelLayout`, `resetUtilityWindow` |
| Tool icons, drawn in code (no image files) | `src/toolicons.cpp` |
| App icon (SVG source and PNG sizes) | `packaging/icons/` |
| Plugin C ABI (kept compatible with paint.software) | `src/plugins/plugin_api.h` |
| Units, print sizes (mm, cm, in, dpi, px/cm) | `src/core/units.cpp`; shared fields `src/dialogs/sizefields.cpp` |
| New, Resize, Canvas Size dialogs | `src/dialogs/newdocumentdialog.cpp`, `resizedialog.cpp`, `canvassizedialog.cpp` |
| Text frames: model, layout at 72 dpi, drawing, JSON | `src/core/textframe.cpp` |
| Text layers in a layer (valid while pixels unchanged) | `Layer::textFrame`, `setText`, `convertTextToPixels` |
| Text tool: frames, caret, keys, input methods, format | `src/tools/texttool.cpp` |
| Text-layer undo steps; finishing open edits first | `LayerReplaceCommand`, `HistoryManager::setBeforeChange` / `finishPendingEdit` |
| Asking before pixel commands change text layers | `MainWindow::convertTextLayersFor` |
| Pictures inside PDFs (own reader) and its dialog | `src/core/pdfpictures.cpp`, `src/dialogs/pdfpicturedialog.cpp` |
| Tests (custom harness, not QTest) | `tests/` |

### Adding a tool

1. `ToolType` enum in `src/tools/tool.h`.
2. New `src/tools/<name>tool.{h,cpp}`, listed in `TOOLS_SOURCES` in CMakeLists.txt.
3. `src/tools/toolregistry.cpp`: an entry in `toolInfos()` (stable key for
   settings and presets, never changed later; name; shortcut key; slot in the
   Tools panel; tooltip; status-bar hint) and a line in `createTool()`. The main
   window, the Tools panel, the tool list, shortcuts and presets read from it.
4. `src/toolicons.cpp`: draw the icon (`forTool`).
5. `src/panels/tooloptionspanel.cpp`: which options are shown (`updateFromTool`).
6. If strokes should follow perspective guides: `CanvasWidget::startsPerspectiveSnap`.
7. A test in `tests/` (`tests/test_toolspanel.cpp` checks the registry is
   complete; update its "last ToolType" check).

Brush-like tools build their dabs with `Tool::dabAt` / `forEachDab` (size,
opacity and tilt from the pen, pressure smoothed along each segment) and
paint coverage with `Tool::paintDab`; tools that rework existing pixels
derive from `RetouchTool`. A new menu command in Image, Layers, Adjustments
or Effects needs an icon in `src/commandicons.cpp` (a test checks).

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
- Autosave writes a layered `.psd` in a background thread (copies of the
  layers share their pixels, so starting it costs nothing).

### File formats

- `.psd` is written like Krita does (8-bit RGB, 4 channels, negative layer
  count, layers cropped to their content, RLE, `luni` names, `lspf` lock,
  resolution 0x03ED, selected layer 0x0400) and read with groups, masks,
  16-bit, CMYK, ZIP and `.psb`. What could not be kept on opening goes into
  `Document::loadNotes()`, shown in a non-blocking message.
- Test files: `tests/data/psd/` (GIMP and Krita files; `make_fixtures.py`
  writes the synthetic ones). To check written files in other programs:
  `PWM_PSD_OUT=/tmp/out build/pwm_psd_tests`, then open them with psd-tools
  (`pip install psd-tools`), `xvfb-run krita --export --export-filename x.ora
  file.psd`, or GIMP's `file-psd-load` in `gimp-console -i -b`.
- Never write to a file directly: `QSaveFile`, so a failed save leaves the old
  file intact.
- Text layers: pixels as usual plus a private tagged block `pwmT` (JSON from
  `TextFrame::serialize`); the reader attaches it after the pixels are set.
  Other programs skip it (checked: psd-tools, GIMP 2.10).
- PDF pictures: `tests/data/pdf/` (`make_fixtures.py` makes them with img2pdf,
  ReportLab, pikepdf and qpdf; `manifest.json` lists the expected pictures).
  Cross-check with poppler: `pdfimages -list file.pdf`. CMYK JPEGs: Qt 6.4
  gives RGB, Qt 6.8+ gives CMYK8888; both resolve the Adobe inversion, so a
  PDF's `Decode [1 0 ...]` is not applied again (tested on Qt 6.12 by the
  AppImage workflow).

### Text layers

- A layer's text counts only while `QImage::cacheKey()` equals the key of the
  drawing (`Layer::textFrame`): any pixel change makes it ordinary pixels, and
  undo that brings the drawing back brings the text back.
- The tool finds its layer by object, not index. While a frame is open,
  `HistoryManager::setBeforeChange` makes every Document change (and undo,
  redo, push) first record the text step, so steps stay in order.
- Image > Resize keeps text (scaled fonts) when proportions stay; Canvas Size
  and Crop move it; Rotate, Flip and Merge Down turn it into pixels.

### Undo

- `HistoryCommand::forEachImage()` lists the images a step keeps; a new kind
  of step must implement it, or its memory is invisible to the budget.
- The budget is enforced when a step is pushed and when it is changed in
  Settings; the latest step is always kept.

### Wayland

- The AppImage ships Qt's Wayland plugins (`EXTRA_PLATFORM_PLUGINS` from the
  Qt in use, plus `waylandcompositor` in `EXTRA_QT_MODULES` for the plugin
  folders). Its smoke test starts it in a headless Weston with
  `QT_QPA_PLATFORM=wayland`; CI runs ten suites natively there too.
- On Wayland the app cannot place its own windows: "Keep floating panels
  attached" is disabled, and `repositionFloatingUtilityWindows` does nothing.
- "Restart Now" closes like quitting (every unsaved image is asked about),
  then `main()` starts the app again: from an AppImage via `$APPIMAGE`,
  without `QT_QPA_PLATFORM` (unless it came from outside) and the AppImage's
  own variables.
- Testing natively in the sandbox: `weston --backend=headless-backend.so
  --socket=wl-test`, then `WAYLAND_DISPLAY=$XDG_RUNTIME_DIR/wl-test
  QT_QPA_PLATFORM=wayland build/<test>`. Closer to Plasma: KWin 5.27
  (`apt-get install kwin-wayland`), copied out of `/usr/bin` under the name
  `kwin_wayland` (the original has file capabilities the sandbox refuses), run
  as `dbus-run-session -- ./kwin_wayland --virtual --socket kwin-test`. The
  paste suite (keyboard focus in dialogs that were never activated) and the
  panel suites (window placement) are expected to fail there.

### Settings

All settings go through `QSettings(AppInfo::kId, AppInfo::kId)`, stored in
`~/.config/paint-with-me/paint-with-me.conf`.
