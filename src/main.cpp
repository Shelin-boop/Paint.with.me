#include <QApplication>
#include <QIcon>
#include <QFileInfo>
#include <QStatusBar>
#include <cstdio>
#include "mainwindow.h"

#include "theme.h"
#include "appinfo.h"
#include "desktopintegration.h"

// On a Wayland desktop, run through XWayland unless told otherwise.
//
// The Tools, Colors, History and Layers panels are separate floating windows
// that the app places next to the main window. Wayland does not let an
// application position its own windows, so natively the compositor would
// place them, and "keep panels attached" and "reset position" would not work.
// XWayland keeps the intended behavior. This goes away once the panels live
// inside the main window.
//
// QT_QPA_PLATFORM, when set, always wins (QT_QPA_PLATFORM=wayland forces
// native Wayland). Without XWayland (no DISPLAY) Qt's default is kept.
static void preferXWaylandOnWayland() {
    if (qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) return;
    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) return;
    if (qEnvironmentVariableIsEmpty("DISPLAY")) return;
    qputenv("QT_QPA_PLATFORM", "xcb");
}

int main(int argc, char *argv[]) {
    // "--version": print the version and quit, without opening a window.
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--version") == 0) {
            std::printf("%s %s\n", AppInfo::kDisplayName, PWM_VERSION_STR);
            return 0;
        }
    }

    preferXWaylandOnWayland();

    QApplication app(argc, argv);
    // No organization name, so the data folder is ~/.local/share/paint-with-me.
    app.setApplicationName(AppInfo::kId);
    app.setApplicationVersion(QStringLiteral(PWM_VERSION_STR));
    app.setWindowIcon(QIcon(QStringLiteral(":/app-icon.png")));
    // Lets the desktop match the window to its menu entry and icon.
    QGuiApplication::setDesktopFileName(AppInfo::kDesktopId);

    // Restore the saved color scheme before any UI is built.
    Theme::loadFromSettings();

    MainWindow window;
    window.show();

    // Started from an AppImage: add or refresh the application menu entry.
    switch (DesktopIntegration::applyAtStartup()) {
    case DesktopIntegration::StartupResult::Added:
        window.statusBar()->showMessage(
            QStringLiteral("Paint.with.me was added to your application menu. "
                           "To remove it, use Help > Remove from application menu."), 15000);
        break;
    case DesktopIntegration::StartupResult::Failed:
        window.statusBar()->showMessage(
            QStringLiteral("Could not add Paint.with.me to the application menu."), 15000);
        break;
    default:
        break;
    }

    // Open any images passed on the command line (each becomes an entry in the
    // image list, like paint.net).
    for (int i = 1; i < argc; ++i) {
        const QString path = QString::fromLocal8Bit(argv[i]);
        if (QFileInfo::exists(path))
            window.openFile(path);
    }

    return app.exec();
}
