#pragma once

#include <QByteArray>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

// Which window system the app runs on inside a Wayland session: natively on
// Wayland (the default since 2026-11-06, after a test on KDE Plasma), or X11
// through XWayland for compatibility. Decided in main() before Qt starts, from
// the setting "display/windowSystem" ("wayland" or "xwayland", Settings > User
// Interface); a change applies at the next start, which Settings offers to do.
//
// Natively, the app cannot place its own windows (Wayland leaves that to the
// desktop), so "Keep floating panels attached" does nothing there.
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

bool nativeWaylandEnabled();   // true unless XWayland was chosen
void setNativeWaylandEnabled(bool on);

// Whether the running app already uses what the setting asks for, so a
// restart would change nothing. qtPlatform is QGuiApplication::platformName().
bool runningAsChosen(const QString &qtPlatform, bool nativeWayland);

// Starting the app again (after a change of window system): the program, its
// arguments (files to reopen) and its environment.
struct Relaunch {
    QString program;
    QStringList arguments;
    QProcessEnvironment environment;
};
// From an AppImage the program is the .AppImage file ($APPIMAGE), not the
// binary inside its mount, which disappears when this instance exits. The
// variables this instance set itself (QT_QPA_PLATFORM, unless it came from
// outside) and the AppImage's own ones are removed, so the new instance
// decides afresh.
Relaunch relaunchCommand(const QString &applicationFile, const QStringList &files,
                         const QProcessEnvironment &environment, bool platformForced);
// Starts it detached; false if it could not be started.
bool relaunch(const QStringList &files);

// For people: "Wayland (native)", "X11 through XWayland", "X11", ...
// qtPlatform is QGuiApplication::platformName().
QString describe(const QString &qtPlatform, bool waylandSession);

// Whether the app can move its own top-level windows (false on Wayland).
bool canPlaceWindows(const QString &qtPlatform);

}  // namespace DisplayServer
