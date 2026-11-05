# About this fork

## Origin

| | |
|---|---|
| Upstream project | [paint.software](https://github.com/Univers4craft/paint.software) by Univers4craft |
| Fork point | upstream commit `db9691f`, 2026-10-31 (merge of PR #31, ".pdn import") |
| Upstream license | MIT |
| This fork | Paint.with.me, MIT |

The full upstream history (108 commits) was merged into this repository on
2026-11-05, so every upstream author stays visible in `git log`.

Paint.with.me does not push to, open pull requests on, or otherwise change the
upstream repository. It only reads from it.

## License obligations

- `LICENSE` keeps the upstream copyright line next to this fork's. Do not remove it.
- Code copied from upstream stays under MIT with its original notice.
- `tests/data/pdn/` contains test files from pypdn (MIT); keep `LICENSE-pypdn`.
- The About dialog and the AppStream metadata credit paint.software.

## What differs from upstream

### 2026-11-05: code cleanup, native Wayland as an option

- Tool registry (`src/tools/toolregistry.cpp`): one table with every tool's
  stable key, name, shortcut, Tools panel slot, tooltip and status-bar hint.
  The main window, the Tools panel, shortcuts and brush presets read from it.
- `src/mainwindow.cpp` (about 3,800 lines) split by job into
  `mainwindow_panels`, `_menus`, `_files`, `_edit` and `_effects`; upstream's
  French comments translated, an empty event filter removed.
- The AppImage ships Qt's Wayland plugins. XWayland stays the default;
  Settings > User Interface > Window System > "Run natively on Wayland
  (experimental)" switches at the next start (`src/displayserver.cpp`). The
  About dialog names the window system in use.

### 2026-11-05: undo memory budget, PDF export, perspective tool

- Undo keeps steps within a memory budget instead of 100 steps: every step
  reports its images, pixels shared with the image or other steps count once
  (`QImage::cacheKey`), the oldest steps go first. Default 1/8 of the
  computer's memory (512 MB to 2 GB), Settings > Undo History.
- File > Export as PDF... (`src/core/pdfexport.cpp`, own writer): page size from
  the resolution, lossless (Flate with PNG prediction) or JPEG, soft-mask
  transparency or white, the image or one page per layer.
- Perspective tool (`src/tools/perspectivetool.cpp`, `src/core/perspective.cpp`):
  up to three vanishing points per image, guide lines drawn by the canvas,
  strokes of painting tools projected onto the guide they start along, guides
  into a new layer. Not in Paint.NET.
- New test suites `pdf_export` and `perspective`; the functional tests keep
  their settings in a temporary folder.

### 2026-11-05: file formats (Photoshop .psd instead of .psw)

- Upstream's own layered format `.psw` is gone (open and save). Layered work is
  saved as Photoshop `.psd` (`src/core/psdformat.cpp`, own reader and writer,
  8-bit RGB, RLE, layers cropped to their content, like Krita writes them);
  `.psb` is read too. Checked against psd-tools 1.26, Krita 5.2 and GIMP 2.10.
- Kept in `.psd`: layer name (Unicode), position (also outside the canvas),
  opacity, visibility, lock, blend mode where Photoshop has the same, the
  resolution and the selected layer. A `.psd` cannot say that a layer keeps
  pixels outside the canvas ("Keep" paste); a reopened layer keeps them
  whenever it has such pixels.
- Documents have a resolution (dpi), read from `.psd`, `.pdn` (its EXIF tags)
  and image files, written to `.psd` and image files, set in New and Resize,
  and used by the rulers.
- Saving goes through a temporary file (`QSaveFile`), for every format.
- Autosave writes a layered `.psd` in the background; recovery saves back to
  the original file. Saving an image removes its recovery snapshot.
- The app is offered for `.psd` files in the menu entry (MIME type).
- New test suite `psd_format` (`tests/test_psd.cpp`) with files from GIMP and
  Krita and synthetic ones (`tests/data/psd/make_fixtures.py`).

### 2026-11-05: Tools panel columns and shortcuts, new default layout

- Default panel layout: Tools above Colors on the left, History above Layers
  on the right (the owner's arrangement). Saved layouts are kept.
- The Tools panel (`src/panels/toolspanel.cpp`, moved out of the main window)
  shows 3 or 4 columns when it is wide enough, or a fixed count chosen by
  right-click. The extra columns hold pinned menu commands and brush presets
  (`src/tools/brushpreset.cpp`); "+" and the right-click menu add them, drag
  moves them. Stored in the settings under `toolsPanel/`.
- New test suite `tools_panel` (`tests/test_toolspanel.cpp`).

### 2026-11-05: speed with large images, docked panels

Measured with `tests/bench_large.cpp` (5000 x 5000 px, 16 layers, 2 CPU
cores): switching a layer on or off went from 4.4 s to 0.09 s, painting with a
1500 px brush from 8 to 41 frames per second, the end of a stroke from 4.1 s to
0.08 s, undo from 4.8 s to 0.05 s, and undo memory per stroke from 172 MB to
42 MB; autosave no longer freezes the window.

- Layers are combined in place, in strips spread over all CPU cores, skipping
  the empty margins of each layer (was: a full copy of the image per layer).
- Zoomed out, the canvas combines the layers at a reduced size.
- The canvas no longer recombines the whole image after a brush stroke or its
  undo; eraser, pencil, recolor and clone stamp now update only the area they
  touch, like the brush already did.
- Layers panel and image list thumbnails are cached and made from small copies.
- Undo steps for painting keep only the changed rectangle.
- Autosave runs in a background thread.
- Tools, History, Layers and Colors are docked inside the main window by
  default (saved layout version 8; older floating layouts are reset once). They
  can still be moved, tabbed, resized, hidden or torn off; View > Reset Panel
  Layout and Ctrl+Shift+F5 to F8 put them back.
- Build: one shared object library for the app and the tests.

### 2026-11-05: AppImage with start-menu entry

- When started from an AppImage, the app adds a menu entry and icons to
  `~/.local/share` (src/desktopintegration.*), keeps it pointing at the
  AppImage's current location, and offers Help > Add to / Remove from
  application menu.
- On Wayland it prefers XWayland (src/main.cpp), because floating panels
  need to position themselves. `QT_QPA_PLATFORM` overrides this. (Native
  Wayland became an option later the same day, see above.)
- `--version` prints the version and exits.
- The AppImage workflow builds with the newest Qt 6 from aqtinstall, runs the
  tests against it, packs with appimagetool's static runtime (no libfuse2),
  smoke-tests the result and publishes a GitHub release.

### 2026-11-05: rebrand and English-only interface

- New name, icon and IDs:

  | | Upstream | Paint.with.me |
  |---|---|---|
  | Executable | `paintdotnet` | `paint-with-me` |
  | Desktop and AppStream ID | `io.github.univers4craft.PaintSoftware` | `io.github.shelin_boop.PaintWithMe` |
  | Settings | `~/.config/PaintDali/PaintDali.conf` | `~/.config/paint-with-me/paint-with-me.conf` |
  | Data folder | `~/.local/share/PaintDali/paint.software` | `~/.local/share/paint-with-me` |
  | System plugins | `/usr/lib/paint.software/plugins` | `/usr/lib/paint-with-me/plugins` |
  | Version macro | `PAINTSW_VERSION_STR` | `PWM_VERSION_STR` |
  | Test targets | `paintdotnet_*` | `pwm_*` |

  The names live in `src/appinfo.h`.
- The interface is English only. Upstream wrote its UI strings in French and
  translated them through its own table (`src/i18n.cpp`). That table, the
  language setting and the "retranslate" code are gone; the strings are English
  in the source. About 20 strings that upstream never translated (history
  entries, dialog labels, file filters) were translated along the way.
- Distribution: AppImage (main) and `.deb`, both built by GitHub Actions on
  demand. Upstream's signed APT repository, Flatpak and AUR files are removed,
  because they were tied to upstream's accounts and signing keys.
- New icon (`packaging/icons/`), original artwork.
- The plugin C interface (`psw_register_plugin`, `PSW_*`) is unchanged, so
  plugins written for paint.software still load.

### Repository setup

- `README.md`, `FORK.md`, `CLAUDE.md`, `SECURITY.md`, `CONTRIBUTING.md` and
  `.github/CODEOWNERS` are fork-specific.

## Pulling in upstream changes

Upstream code is French-sourced and uses `TR("...")`; this fork is not.
Merging upstream changes therefore needs care: new upstream UI strings have to
be converted to English by hand.

```bash
git fetch upstream
git merge upstream/main
# resolve conflicts, convert new French strings, build, run the tests
```

The remote `upstream` points to https://github.com/Univers4craft/paint.software.git.
A fresh clone does not have it yet:

```bash
git remote add upstream https://github.com/Univers4craft/paint.software.git
```
