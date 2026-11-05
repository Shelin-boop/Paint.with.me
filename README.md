# Paint.with.me

A free, open-source raster image editor for Linux with the familiar look and
workflow of Paint.NET: layers, blend modes, selections, adjustments and effects,
built natively in C++17 and Qt 6. No Wine, no .NET runtime.

> **Status: early fork.** This project is a fork of
> [paint.software](https://github.com/Univers4craft/paint.software) by
> Univers4craft, taken at upstream commit `db9691f` (2026-10-31). The code is
> still identical to upstream apart from repository setup. Rebranding (program
> name, icon, app ID, packages) has not happened yet, so a build still calls
> itself "paint.software". See [FORK.md](FORK.md).

## Goals

- Look and feel close to Paint.NET, on Linux.
- Open Paint.NET `.pdn` files with their layers.
- A portable AppImage that adds itself to the start menu.
- New tools Paint.NET does not have, starting with a smudge tool.

## Build from source

Requirements: a C++17 compiler, CMake 3.16 or newer, Qt 6.4 or newer
(Widgets, Gui, Core, PrintSupport, Concurrent) and zlib.

```bash
# Debian / Ubuntu / Kubuntu / Linux Mint
sudo apt install build-essential cmake ninja-build qt6-base-dev \
  qt6-image-formats-plugins libgl1-mesa-dev zlib1g-dev

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/paintdotnet
```

Run the headless test suite:

```bash
cmake --build build --target paintdotnet_tests paintdotnet_brush_tests \
  paintdotnet_paste_tests paintdotnet_preview_tests
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

No release packages are published from this repository yet.

## Credits

Almost all of the code comes from
[paint.software](https://github.com/Univers4craft/paint.software) by
Univers4craft and its contributors, under the MIT licence. The `.pdn` test files
in `tests/data/pdn` come from [pypdn](https://github.com/addisonElliott/pypdn)
(MIT, see `tests/data/pdn/LICENSE-pypdn`).

## Licence

MIT, see [LICENSE](LICENSE). The original copyright notice of paint.software is
kept there, as the MIT licence requires.

## Disclaimer

Paint.with.me is an independent community project. It is not affiliated with,
endorsed by, or sponsored by dotPDN LLC or Rick Brewster, the makers of
Paint.NET, nor by Univers4craft, the maintainer of paint.software. "Paint.NET"
is named only to describe the software this project is modelled on and
compatible with.
