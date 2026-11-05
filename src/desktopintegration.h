#pragma once

#include <QString>

// Adds Paint.with.me to the desktop's application menu when it runs as an
// AppImage, so nobody has to install anything by hand.
//
// What it writes, all inside the user's data folder (~/.local/share):
//   applications/io.github.shelin_boop.PaintWithMe.desktop
//   icons/hicolor/<size>/apps/io.github.shelin_boop.PaintWithMe.{png,svg}
//
// The menu entry carries a marker key, so an entry that was not written here
// (for example one the user made) is never changed or deleted.
namespace DesktopIntegration {

struct Environment {
    QString appImagePath;   // $APPIMAGE: the .AppImage file being run
    QString appDir;         // $APPDIR: where the AppImage's contents are mounted
    QString dataHome;       // $XDG_DATA_HOME, or ~/.local/share
    QString version;        // recorded in the entry, for information only
    // Ask the desktop to reread its menu after a change. On for the real
    // system, off by default so tests never touch the real desktop.
    bool refreshCaches = false;

    // Reads the variables above from the running process.
    static Environment fromSystem();
    bool isAppImage() const { return !appImagePath.isEmpty(); }
};

enum class State {
    NotAppImage,   // not started from an AppImage: nothing to do
    Missing,       // no menu entry yet
    Outdated,      // our entry exists but points elsewhere or is from an older build
    Current,       // our entry exists and matches this AppImage
    Foreign,       // a file with our name exists but was not written by us
};

QString desktopFilePath(const Environment &env);
// The exact menu entry install() writes for this environment.
QString desktopFileContent(const Environment &env);
State state(const Environment &env);

// Writes the menu entry and icons. Returns false (and sets *error) on failure.
bool install(const Environment &env, QString *error = nullptr);
// Removes the entry and icons written by install(). A foreign entry is kept.
bool uninstall(const Environment &env, QString *error = nullptr);

// What to do at startup, given the current state and whether the user removed
// the entry earlier.
enum class StartupAction { None, Install, Update };
StartupAction startupAction(State state, bool userRemovedEntry);

// The user's earlier choice, kept in the settings.
bool userRemovedEntry();
void setUserRemovedEntry(bool removed);

// Runs the startup policy on the real system.
enum class StartupResult { Nothing, Added, Updated, Failed };
StartupResult applyAtStartup();

} // namespace DesktopIntegration
