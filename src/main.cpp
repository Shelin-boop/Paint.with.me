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
#include "instancelink.h"
#include "canvas/gpusupport.h"
#include <QJsonObject>
#include <QTimer>

int main(int argc, char *argv[]) {
    // "--version": print the version and quit, without opening a window.
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--version") == 0) {
            std::printf("%s %s\n", AppInfo::kDisplayName, PWM_VERSION_STR);
            return 0;
        }
    }

    // "--new-window": a window of its own, even if one is open already.
    // "--take <socket> <image>": then take over that image from the window
    // listening there ("Move to New Window").
    bool newWindow = false;
    bool noGpu = false;   // "--no-gpu": draw with the processor this time
    QString takeSocket, takeImage;
    QStringList files;
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--new-window") {
            newWindow = true;
        } else if (arg == "--take" && i + 2 < argc) {
            takeSocket = QString::fromLocal8Bit(argv[++i]);
            takeImage = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == "--no-gpu") {
            noGpu = true;
        } else if (arg == "-platform" || arg == "--platform") {
            ++i;   // Qt's own option and its value
        } else if (!arg.startsWith('-')) {
            const QString path = QString::fromLocal8Bit(arg);
            if (QFileInfo::exists(path)) files << path;
        }
    }

    // A Paint.with.me window is open already: it opens the files (as new
    // images) and comes to the front; this process is not needed.
    if (!newWindow && InstanceLink::handOverEnabled() && InstanceLink::handOver(files))
        return 0;

    // On a Wayland desktop: natively, or XWayland when chosen in Settings.
    // QT_QPA_PLATFORM or -platform, when given, always wins.
    DisplayServer::applyAtStartup(argc, argv);

    // The canvas may draw with OpenGL; panels with their own OpenGL views can
    // then move between windows.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    // No organization name, so the data folder is ~/.local/share/paint-with-me.
    app.setApplicationName(AppInfo::kId);
    app.setApplicationVersion(QStringLiteral(PWM_VERSION_STR));
    app.setWindowIcon(QIcon(QStringLiteral(":/app-icon.png")));
    // Lets the desktop match the window to its menu entry and icon.
    QGuiApplication::setDesktopFileName(AppInfo::kDesktopId);

    // Restore the saved color scheme before any UI is built.
    Theme::loadFromSettings();

    if (noGpu) GpuSupport::disableForThisRun();
    MainWindow window;
    // Listens for other Paint.with.me windows (files, moved images).
    InstanceLink link;
    window.setInstanceLink(&link);
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
    for (const QString &path : files)
        window.openFile(path);
    if (!takeSocket.isEmpty()) {
        const QJsonObject from{{"socket", takeSocket}, {"doc", takeImage.toDouble()}};
        QTimer::singleShot(0, &window, [&window, from]() { window.receiveImage(from); });
    }

    const int result = app.exec();
    link.close();
    // Settings > User Interface > window system, "Restart Now": the window
    // has closed (after asking about unsaved changes); start again, as a
    // window of its own (this one is still closing).
    if (window.restartRequested()
        && !DisplayServer::relaunch(QStringList{QStringLiteral("--new-window")} + window.restartFiles()))
        std::fprintf(stderr, "Paint.with.me could not start itself again.\n");
    return result;
}
