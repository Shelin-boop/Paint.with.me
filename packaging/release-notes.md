**Paint.with.me @VER@**, built from `@SHA@`.

Pick the file for your system:

| Your system | File | How to install |
|---|---|---|
| Any 64-bit Linux with glibc @GLIBC@ or newer (for example Ubuntu 22.04 or newer) | **Paint.with.me-x86_64.AppImage** | Make it executable (right-click, Properties, Permissions, "Is executable", or `chmod +x Paint.with.me-x86_64.AppImage`), then start it. On the first start it adds itself to the application menu. It brings its own Qt @QT@. |
| Ubuntu 24.04 or newer, Kubuntu, Linux Mint 22 or newer, Debian 12 or newer | **paint-with-me_amd64.deb** | Open it with your software installer, or run `sudo apt install ./paint-with-me_amd64.deb` |
| Fedora @FEDORA@ or newer | **paint-with-me-fedora.x86_64.rpm** | Open it with your software installer, or run `sudo dnf install ./paint-with-me-fedora.x86_64.rpm` |
| openSUSE Leap @LEAP@ or newer, openSUSE Tumbleweed | **paint-with-me-opensuse.x86_64.rpm** | `sudo zypper install --allow-unsigned-rpm ./paint-with-me-opensuse.x86_64.rpm` |
| Arch Linux, EndeavourOS | **paint-with-me-arch-x86_64.pkg.tar.zst** | `sudo pacman -U ./paint-with-me-arch-x86_64.pkg.tar.zst` |

The packages use your system's own Qt and get its updates; on KDE Plasma they
also take on the desktop's look. To update, install the newer file the same
way. The AppImage needs no installation at all.

Check a download: `sha256sum -c --ignore-missing SHA256SUMS`
