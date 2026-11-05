#pragma once

// Names and identifiers of the application, kept in one place.
namespace AppInfo {

// Shown to people: window title, About dialog.
inline constexpr char kDisplayName[] = "Paint.with.me";

// Settings file (~/.config/paint-with-me/paint-with-me.conf), data folder
// (~/.local/share/paint-with-me) and executable name.
inline constexpr char kId[] = "paint-with-me";

// Desktop file and AppStream ID. On Wayland the compositor uses it to match
// the window to its menu entry and icon.
inline constexpr char kDesktopId[] = "io.github.shelin_boop.PaintWithMe";

inline constexpr char kHomepage[] = "https://github.com/Shelin-boop/Paint.with.me";

} // namespace AppInfo

// The version comes from CMake (PWM_VERSION_STR); a hand-rolled build without
// the definition reports "dev".
#ifndef PWM_VERSION_STR
#define PWM_VERSION_STR "dev"
#endif
