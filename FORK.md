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

## License obligations

- `LICENSE` keeps the upstream copyright line next to this fork's. Do not remove it.
- Code copied from upstream stays under MIT with its original notice.
- `tests/data/pdn/` contains test files from pypdn (MIT); keep `LICENSE-pypdn`.

## What differs from upstream so far

1. `README.md`, `FORK.md`, `CLAUDE.md`, `SECURITY.md` and `.github/CODEOWNERS`
   are fork-specific.
2. The publishing workflows (`appimage.yml`, `apt-repo.yml`, `flatpak.yml`,
   `package.yml`) are manual-only. Upstream runs them on every push to `main`;
   here they would publish packages still named "paint.software". The CI
   workflow (`ci.yml`, build and tests) runs on every push and pull request as
   upstream.

The application code is unchanged.

## Still upstream-branded (to change in the rebrand)

- Executable name `paintdotnet` and test targets `paintdotnet_*` (CMakeLists.txt).
- CMake project `PaintDali`, version macro `PAINTSW_VERSION`, QSettings
  organization `PaintDali` and application name `paint.software` (src/main.cpp).
- App ID `io.github.univers4craft.PaintSoftware`, desktop file, AppStream
  metainfo, Flatpak manifests, AUR files, APT pages (packaging/).
- Logo `src/resources/paintdali-logo.png`.
- Window title (src/mainwindow.cpp) and UI strings that say "paint.software".
- Settings path: every `QSettings("PaintDali", "PaintDali")` call, which stores
  settings in `~/.config/PaintDali/PaintDali.conf`.
- CONTRIBUTING.md, SIGNING.md and packaging/DISTRIBUTION.md.
- Plugin API names (`psw_register_plugin`, `PSW_*`). Renaming these breaks
  existing plugins, so decide deliberately.

## Pulling in upstream changes

```bash
git fetch upstream
git switch -c sync/upstream-YYYY-MM-DD
git merge upstream/main
# resolve conflicts, build, run the tests, then open a pull request
```

The remote `upstream` points to https://github.com/Univers4craft/paint.software.git.
A fresh clone does not have it yet:

```bash
git remote add upstream https://github.com/Univers4craft/paint.software.git
```
