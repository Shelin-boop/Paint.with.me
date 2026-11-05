#include <QApplication>
#include <QIcon>
#include <QFileInfo>
#include "mainwindow.h"

#include "theme.h"
#include "appinfo.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    // No organization name, so the data folder is ~/.local/share/paint-with-me.
    app.setApplicationName(AppInfo::kId);
    app.setApplicationVersion(QStringLiteral(PWM_VERSION_STR));
    app.setWindowIcon(QIcon(QStringLiteral(":/app-icon.png")));
    // Lets the Wayland compositor match the window to its menu entry and icon.
    QGuiApplication::setDesktopFileName(AppInfo::kDesktopId);

    // Restore the saved color scheme before any UI is built.
    Theme::loadFromSettings();

    MainWindow window;
    window.show();

    // Open any images passed on the command line (each becomes an entry in the
    // image list, like paint.net).
    for (int i = 1; i < argc; ++i) {
        const QString path = QString::fromLocal8Bit(argv[i]);
        if (QFileInfo::exists(path))
            window.openFile(path);
    }

    return app.exec();
}
