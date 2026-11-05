#include <QApplication>
#include <QIcon>
#include <QFileInfo>
#include <QStatusBar>
#include <cstdio>
#include "mainwindow.h"

#include "theme.h"
#include "appinfo.h"
#include "desktopintegration.h"
#include "displayserver.h"

int main(int argc, char *argv[]) {
    // "--version": print the version and quit, without opening a window.
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--version") == 0) {
            std::printf("%s %s\n", AppInfo::kDisplayName, PWM_VERSION_STR);
            return 0;
        }
    }

    // On a Wayland desktop: XWayland, or natively when chosen in Settings.
    // QT_QPA_PLATFORM or -platform, when given, always wins.
    DisplayServer::applyAtStartup(argc, argv);

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
