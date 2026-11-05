#pragma once

#include <QByteArray>
#include <QString>

// Which window system the app runs on inside a Wayland session: X11 through
// XWayland, or natively on Wayland. Decided in main() before Qt starts, from
// the setting "display/nativeWayland" (Settings > User Interface); a change
// applies at the next start.
//
// XWayland stays the default until native Wayland has been tried on KDE
// Plasma. Natively, the app cannot place its own windows (Wayland leaves that
// to the desktop), so "Keep floating panels attached" does nothing there.
namespace DisplayServer {

struct Environment {
    bool platformForced = false;  // QT_QPA_PLATFORM or a -platform argument
    bool waylandSession = false;  // WAYLAND_DISPLAY is set
    bool xDisplay = false;        // DISPLAY is set (X11 or XWayland)

    static Environment fromSystem(int argc, char **argv);
};

// The value for QT_QPA_PLATFORM; empty leaves Qt's own choice alone.
QByteArray platformFor(const Environment &env, bool nativeWayland);

// Sets QT_QPA_PLATFORM from the environment and the setting. Call before the
// QApplication is created.
void applyAtStartup(int argc, char **argv);

// applyAtStartup saw QT_QPA_PLATFORM or -platform: the choice was made
// outside the app, so the setting has no effect this time.
bool platformForcedAtStartup();

bool nativeWaylandEnabled();
void setNativeWaylandEnabled(bool on);

// For people: "Wayland (native)", "X11 through XWayland", "X11", ...
// qtPlatform is QGuiApplication::platformName().
QString describe(const QString &qtPlatform, bool waylandSession);

// Whether the app can move its own top-level windows (false on Wayland).
bool canPlaceWindows(const QString &qtPlatform);

}  // namespace DisplayServer
