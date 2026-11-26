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
- Delivery (2026-11-26, owner's wish "download button, installers for common
  Linux systems"): an AppImage that adds a start-menu entry on first launch,
  plus installers built with each system's own Qt: `.deb` (Debian, Ubuntu,
  Kubuntu, Mint), `.rpm` for Fedora and for openSUSE, an Arch package. File
  names stay the same from build to build; the README's download buttons
  link to them in the `continuous` release. The repository is public.
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
- View rotation (2026-11-06): View > Rotate View turns only the view (keys
  4 / 6, 5 resets, as in Krita; Shift+Space and drag turns freely). The step
  is a setting (`view/rotationStep`, default 15 degrees), chosen in the menu
  or Settings > Canvas. Rulers show no scale while turned.
- Keyboard shortcuts (2026-11-06): every menu command, tool and extra key can
  be changed in Settings > Keyboard. New extra keys: last used painting tool
  (N), Eraser and back (Shift+E), Color Picker while held (I). Tools given the
  same key take turns. Keys while typing in a text frame stay fixed.
- Several images and windows (2026-11-26): files opened from outside go to
  the window used last (each window is its own process; `InstanceLink`,
  sockets in `$XDG_RUNTIME_DIR/paint-with-me`, setting `ui/singleWindow`,
  Settings > User Interface). `--new-window`, File > New Window and the menu
  entry's "New Window" action start a window of its own. Images move between
  windows by dragging a thumbnail or "Move to New Window" (a layered .psd is
  handed over; undo history stays behind). Thumbnails have an x to close,
  a right-click menu, and can be dragged to reorder.
- Colors (2026-11-26): the color chosen in the Colors panel (primary or
  secondary, `Document::secondaryActive`) is what the left mouse button
  paints with, the right button the other one (owner's wish; Paint.NET only
  edits the chosen one). Tools ask `Document::paintColor(rightButton)`. The
  colors and the choice carry over to the image switched to.
- Clipboard (2026-11-26): Copy takes the selection's bounding area only, with
  its place and the image's dpi in private data; Paste puts it back in place
  if it fits; File > New starts at the clipboard picture's size.
- Arrow keys (2026-11-26, owner's wish): with a selection, one pixel per
  press; held over 3 s, 1% of the image's width or height per step. Move
  Selection moves the outline, every other tool the selected pixels. One
  undo step per key press.
- Crash recovery (2026-11-26): one snapshot folder per window under
  `recovery/<pid>-<start ms>/`, locked (QLockFile) while the window is open.
- Drawing speed (2026-11-26, owner's choice "combine 1 and 2"): layers below
  and above the active one are kept combined (`LayerGroupCache`), and the
  canvas is drawn by the graphics card with OpenGL (`GpuCompositor`), with
  the processor as fallback. Setting `canvas/gpu` = `auto` (hardware driver
  only) / `on` / `off`, Settings > Canvas > Drawing the Image; `--no-gpu`
  for one start. A crash while starting OpenGL leaves `canvas/gpuStarting`
  set, and the next start uses the processor. Owner's machine: Quadro P1000.
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
9. Rotating the view with a chosen step; keyboard shortcuts of one's own.
   Done 2026-11-06.
10. Owner's list of 2026-11-26: layer thumbnails without the empty band;
    images opened into the running window; New sized from the clipboard;
    secondary color; x on thumbnails; moving images between windows; arrow
    keys move selections. Done 2026-11-26.
11. Faster drawing with many layers: combined layer groups on the processor
    and an OpenGL canvas. Done 2026-11-26.
12. Download buttons on the main page; installers for Debian/Ubuntu, Fedora,
    openSUSE and Arch Linux. Done 2026-11-26.

### Ideas for later (owner's list; ask which comes next)

- (empty: the 2026-11-26 list is done)
- Found, not asked for yet: Canvas Size says the new space gets the secondary
  color, but it stays transparent (inherited).
- Possible later, each needs the owner's own account: Flathub (Flatpak), the
  AUR (Arch), a package repository with a signing key (automatic updates).

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
- Every change keeps all sixteen test suites passing; new behavior gets a test.

## Build and test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --build build --target pwm_all_tests      # every test program (not the benchmark)
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
PWM_SCREENSHOT_DIR=/tmp/shots QT_QPA_PLATFORM=offscreen build/pwm_rotation_tests     # turned view, Settings > Canvas
PWM_SCREENSHOT_DIR=/tmp/shots QT_QPA_PLATFORM=offscreen build/pwm_shortcuts_tests    # Settings > Keyboard
```

Note: `cmake --build build` builds only the app; build `pwm_all_tests`
(the command above), or old test programs run. A new test program made with
`pwm_test()` joins `pwm_all_tests` by itself.

## Releases

- `.github/workflows/release.yml`, started by hand (Actions tab, or
  `gh api repos/Shelin-boop/Paint.with.me/actions/workflows/release.yml/dispatches -f ref=main`;
  add `-F 'inputs[publish]=false'` to only build and test), replaces the
  rolling `continuous` pre-release ("Latest build"). Pushing a tag `vX.Y.Z`
  creates a normal release with the same files.
- Jobs: AppImage (Ubuntu 22.04 runner, Qt 6.12.0 from aqt, pinned; the input
  "latest" tries the newest Qt 6; needs glibc 2.35+); `.deb` built in
  `debian:12`; `.rpm` in the older of Fedora's two supported releases
  (worked out from `fedora:latest`) and in `OPENSUSE_LEAP` (env, 16.0: move it
  on at its end of life); Arch in `archlinux:latest`. Each package job runs
  `pwm_all_tests` on that system first. Then `packaging/test-installed-package.sh`
  installs, starts and removes each package on fresh systems (Debian 12 and
  13, Ubuntu 24.04, latest and rolling, both Fedoras, Leap and Tumbleweed,
  Arch). Publishing waits for all of it, writes `SHA256SUMS`, and checks that
  every file and every download link in README.md answers.
- File names never change (download buttons): `Paint.with.me-x86_64.AppImage`,
  `paint-with-me_amd64.deb`, `paint-with-me-fedora.x86_64.rpm`,
  `paint-with-me-opensuse.x86_64.rpm`, `paint-with-me-arch-x86_64.pkg.tar.zst`.
  Release notes: `packaging/release-notes.md` (placeholders `@VER@` ...).
- Packages are built against the oldest supported release of each system,
  so they install there and on newer ones (Qt and glibc are only asked for
  in the versions used). A `.deb` built on Ubuntu 24.04 would need glibc 2.38
  and not install on Debian 12; Ubuntu's `t64` Qt packages also provide the
  Debian names, so the Debian 12 build installs on Ubuntu.
- The scripts run in any container: `docker run --rm -v "$PWD:/src" -w /src
  debian:12 packaging/build-deb.sh <version> dist` (likewise `build-rpm.sh`,
  `build-arch.sh`). Claude's sandbox cannot pull from Docker Hub, so other
  systems are tested through the workflow (`publish=false`).
- Version shown to users: `<project major.minor>.<commit count>`, for example 0.1.115.
- The CI workflow (`ci.yml`) runs on every push; the release workflow only by
  hand or on a tag, so "Latest build" changes only when wanted.

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
| View rotation: mapping, drawing turned, fit, zoom at a point, held keys | `CanvasWidget::canvasTransform`, `setRotation`, `panBy`, `zoomAt`, `setHoldKeys` |
| Rotate View commands, step, status bar angle | `MainWindow::rotateViewLeft` ..., `rotationStep`, `updateRotationIndicator` |
| Keyboard shortcuts: list, defaults, saved keys, clashes | `src/shortcuts.cpp` (`Shortcuts`) |
| Commands without a menu entry, tool keys, last painting tool, held picker | `MainWindow::createKeyboardShortcuts`, `applyShortcutKeys` in `src/mainwindow_menus.cpp` |
| Settings > Keyboard page | `src/dialogs/keyboardpage.cpp` |
| Other windows: sockets, hand-over at startup, requests | `src/instancelink.cpp` (`InstanceLink`), `main()` |
| Requests from other windows; moving images; drag data; reorder | `src/mainwindow_instances.cpp` |
| Colors: chosen slot, the color a button paints | `Document::secondaryActive`, `paintColor`, `ColorsPanel::onSecondaryClicked` |
| Copy with place data, paste in place, clipboard size for New | `putOnClipboard`, `clipboardPlace`, `MainWindow::clipboardPictureSize` in `src/mainwindow_edit.cpp` |
| Arrow keys move the selection | `src/core/nudge.cpp`, `CanvasWidget::nudgeApplies` |
| Layer thumbnails (image's shape, layer offset, pixel density) | `LayersPanel::makeThumbnail` |
| Crash recovery folders per window | `MainWindow::recoveryRoot`, `prepareAutosaveDir`, `checkForRecovery` |
| Layers below / above the active one kept combined (tiles, signatures) | `src/canvas/layergroupcache.cpp`, `Document::CompositeParts` |
| OpenGL: probe, setting, crash guard | `src/canvas/gpusupport.cpp` (`GpuSupport`) |
| OpenGL: layer textures, combining in shaders, drawing the view | `src/canvas/gpucompositor.cpp` |
| OpenGL widget over the canvas; canvas side | `src/canvas/gpucanvasview.cpp`, `CanvasWidget::paintOnGpuView`, `paintScene`, `setGpuEnabled` |
| Installers: build scripts, Arch PKGBUILD, install test, release notes | `packaging/` |
| Download buttons in the README (SVG) | `docs/download/` |
| Tests (custom harness, not QTest) | `tests/` |

### Adding a tool

1. `ToolType` enum in `src/tools/tool.h`.
2. New `src/tools/<name>tool.{h,cpp}`, listed in `TOOLS_SOURCES` in CMakeLists.txt.
3. `src/tools/toolregistry.cpp`: an entry in `toolInfos()` (stable key for
   settings and presets, never changed later; name; shortcut key; slot in the
   Tools panel; tooltip; status-bar hint) and a line in `createTool()`. The main
   window, the Tools panel, the tool list, shortcuts and presets read from it.
   The shortcut key is the default; Settings > Keyboard lists the tool by
   itself. A tool that puts paint on the image also goes into
   `isPaintingTool` (the "Last Used Painting Tool" key).
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

### Keyboard shortcuts

- Menu commands are listed automatically (`Shortcuts::addMenuBar`, after
  `createMenus`); the keys given in `createMenus` are their defaults. Their
  id is the menu path ("View > Rotate View > Rotate Left"), so renaming a menu
  entry drops a key a user chose for it.
- A command without a menu entry is added in `createKeyboardShortcuts` with a
  fixed id ("painting.lastTool"); tools use "tool." + the registry key; keys
  that work while held ("hold.pan", "hold.rotate", "hold.colorPicker") go to
  `CanvasWidget::setHoldKeys` and work while the canvas has the focus.
- Saved: only changed keys, as JSON in `keyboard/custom` (portable key text;
  an empty list means "no key").
- Tool keys are rebuilt on every change (`applyShortcutKeys`): one action per
  key, tools with the same key take turns, Shift+key goes backwards unless
  another command has Shift+key.
- Single keys check `allowSingleKeyShortcuts()`; while the Text tool types,
  the canvas takes every key without Ctrl or Alt (ShortcutOverride).

### Graphics card (OpenGL)

- `GpuCanvasView` is a `QOpenGLWidget` child covering the canvas, transparent
  for the mouse; `CanvasWidget` keeps all events and paints overlays into it
  with the same `paintScene` as on the processor. Both paths share the
  cache bookkeeping (`m_cacheValid`, `m_dirtyRender`), reset on a switch.
- Layers are textures (only their content area when that is under half the
  layer); changed areas are uploaded with `GL_UNPACK_ROW_LENGTH`. The
  combined image is an RGBA8 framebuffer at the reduced size (same 2x2
  averaging as the processor). Normal uses GL blending; other modes copy
  the target into a scratch texture (unit 1) and run the blend shader.
- Results must match the processor: `pwm_gpu_tests` checks every blend mode,
  opacity and reduction level (at most 3/255 apart). The per-pixel modes
  (Glow, Negation, Reflect, Xor) use the same integer unpremultiply and
  premultiply in `Layer` and in the shader; Qt's own conversion rounds
  differently on different processors.
- Any failure (budget, texture size, GL error) emits `gpuFailed` and switches
  to the processor for the rest of the run. The budget comes from
  `GL_NVX_gpu_memory_info` / `GL_ATI_meminfo` (free memory minus 512 MB),
  else 1536 MB.
- Offscreen has no OpenGL. Testing in the sandbox: `xvfb-run -a env
  QT_QPA_PLATFORM=xcb PWM_REQUIRE_GPU=1 build/pwm_gpu_tests` (Mesa
  llvmpipe; packages `xvfb libgl1-mesa-dri`), or natively on headless
  Weston with `QT_QPA_PLATFORM=wayland` (EGL). Speed on real graphics cards
  cannot be measured here (llvmpipe itself runs on the processor).
- `main()` sets `Qt::AA_ShareOpenGLContexts` before the QApplication.

### Undo

- `HistoryCommand::forEachImage()` lists the images a step keeps; a new kind
  of step must implement it, or its memory is invisible to the budget.
- The budget is enforced when a step is pushed and when it is changed in
  Settings; the latest step is always kept.

### Wayland

- The AppImage ships Qt's Wayland plugins (`EXTRA_PLATFORM_PLUGINS` from the
  Qt in use, plus `waylandcompositor` in `EXTRA_QT_MODULES` for the plugin
  folders). Its smoke test starts it in a headless Weston with
  `QT_QPA_PLATFORM=wayland`; CI runs twelve suites natively there too.
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

### Other windows

- `main()` hands files to a running window before Qt starts (`InstanceLink::handOver`,
  plain POSIX sockets, no QApplication needed); the receiver ignores
  requests older than the sender waits (`kOpenTimeoutMs`), so files never
  open twice. Wayland: the sender passes its `XDG_ACTIVATION_TOKEN`; the
  receiver puts it in its environment before `activateWindow()`, which Qt
  then uses.
- Moving an image: the receiving window asks "take" (the giving one writes
  `move-<pid>-<id>.psd` in the socket folder), opens it, then sends "taken"
  (the giver closes it without asking). Calls are blocking: never call
  the own process's socket (it would wait on itself).
- `pwm_windows_tests` starts the built app (`PWM_APP_PATH`) as a second
  window; `PWM_INSTANCE_DIR` keeps the sockets out of the real runtime folder.
- The restart after a window-system change starts with `--new-window`.

### Settings

All settings go through `QSettings(AppInfo::kId, AppInfo::kId)`, stored in
`~/.config/paint-with-me/paint-with-me.conf`.
