// Tests for the AppImage application menu entry (src/desktopintegration.*).
// Everything runs in temporary folders; the real ~/.local/share is never touched.

#include "desktopintegration.h"
#include "appinfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdio>

using namespace DesktopIntegration;

static int failures = 0;
static int checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static QString read(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

static void write(const QString &path, const QByteArray &data) {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
}

static QString iconPath(const QString &root, const QString &size, const QString &suffix) {
    return root + "/icons/hicolor/" + size + "/apps/" + AppInfo::kDesktopId + "." + suffix;
}

// A fake AppImage: the .AppImage file and the mounted folder with its icons.
struct Fake {
    QTemporaryDir dir;
    Environment env;
    explicit Fake(const QString &appImageName = "Paint.with.me-0.1.1-x86_64.AppImage") {
        env.appImagePath = dir.path() + "/Downloads/" + appImageName;
        write(env.appImagePath, "not really an AppImage");
        env.appDir = dir.path() + "/mount";
        write(env.appDir + "/usr/share/icons/hicolor/256x256/apps/" + AppInfo::kDesktopId + ".png", "png256");
        write(env.appDir + "/usr/share/icons/hicolor/48x48/apps/" + AppInfo::kDesktopId + ".png", "png48");
        write(env.appDir + "/usr/share/icons/hicolor/scalable/apps/" + AppInfo::kDesktopId + ".svg", "<svg/>");
        env.dataHome = dir.path() + "/share";
        env.version = "0.1.1";
    }
};

static void notAnAppImage() {
    Environment env;
    env.dataHome = "/nonexistent";
    CHECK(state(env) == State::NotAppImage);
    CHECK(startupAction(state(env), false) == StartupAction::None);
    QString error;
    CHECK(!install(env, &error));
    CHECK(!error.isEmpty());
}

static void firstLaunchInstalls() {
    Fake f;
    CHECK(state(f.env) == State::Missing);
    CHECK(startupAction(State::Missing, false) == StartupAction::Install);
    QString error;
    CHECK(install(f.env, &error));
    CHECK(error.isEmpty());
    CHECK(state(f.env) == State::Current);
    CHECK(startupAction(State::Current, false) == StartupAction::None);

    const QString desktop = read(desktopFilePath(f.env));
    CHECK(desktop.startsWith("[Desktop Entry]\n"));
    CHECK(desktop.contains("Name=Paint.with.me\n"));
    CHECK(desktop.contains("Exec=\"" + f.env.appImagePath + "\" %F\n"));
    CHECK(desktop.contains("TryExec=" + f.env.appImagePath + "\n"));
    CHECK(desktop.contains(QString("Icon=") + AppInfo::kDesktopId + "\n"));
    CHECK(desktop.contains("X-PaintWithMe-AppImage=true\n"));
    CHECK(desktopFilePath(f.env) == f.env.dataHome + "/applications/" + AppInfo::kDesktopId + ".desktop");

    // Every icon from the AppImage is copied, at its size.
    CHECK(read(iconPath(f.env.dataHome, "256x256", "png")) == "png256");
    CHECK(read(iconPath(f.env.dataHome, "48x48", "png")) == "png48");
    CHECK(read(iconPath(f.env.dataHome, "scalable", "svg")) == "<svg/>");
}

static void movedAppImageIsUpdated() {
    Fake f;
    CHECK(install(f.env, nullptr));
    Environment moved = f.env;
    moved.appImagePath = f.dir.path() + "/Apps/Paint.with.me-0.1.2-x86_64.AppImage";
    write(moved.appImagePath, "newer");
    moved.version = "0.1.2";
    CHECK(state(moved) == State::Outdated);
    CHECK(startupAction(State::Outdated, false) == StartupAction::Update);
    CHECK(install(moved, nullptr));
    CHECK(state(moved) == State::Current);
    CHECK(read(desktopFilePath(moved)).contains("Exec=\"" + moved.appImagePath + "\" %F\n"));
}

static void unusualPathsAreQuoted() {
    // Spaces, $, quotes, backquotes, % and backslashes, per the Desktop Entry spec.
    Fake f(QString::fromUtf8("My \"Apps\" $HOME `x` 100% a\\b é.AppImage"));
    CHECK(install(f.env, nullptr));
    const QString desktop = read(desktopFilePath(f.env));
    const QString dir = f.dir.path() + "/Downloads/";
    const QString expected = "Exec=\"" + dir
        + QString::fromUtf8("My \\\\\"Apps\\\\\" \\\\$HOME \\\\`x\\\\` 100%% a\\\\\\\\b é.AppImage\" %F\n");
    CHECK(desktop.contains(expected));
    if (!desktop.contains(expected))
        fprintf(stderr, "  got:      %s\n  expected: %s\n",
                qPrintable(desktop.section('\n', 6, 6)), qPrintable(expected.trimmed()));
    CHECK(desktop.contains("TryExec=" + dir + QString::fromUtf8("My \"Apps\" $HOME `x` 100% a\\\\b é.AppImage\n")));
}

static void fallbackIconWithoutAppDir() {
    Fake f;
    f.env.appDir.clear();
    CHECK(install(f.env, nullptr));
    QFile built(":/app-icon.png");
    CHECK(built.open(QIODevice::ReadOnly));
    CHECK(read(iconPath(f.env.dataHome, "256x256", "png")).toUtf8().size() > 0);
    CHECK(QFileInfo(iconPath(f.env.dataHome, "256x256", "png")).size() == built.size());
    CHECK(QFileInfo(iconPath(f.env.dataHome, "256x256", "png")).isWritable());
}

static void removeAndRemember() {
    Fake f;
    CHECK(install(f.env, nullptr));
    QString error;
    CHECK(uninstall(f.env, &error));
    CHECK(!QFileInfo::exists(desktopFilePath(f.env)));
    CHECK(!QFileInfo::exists(iconPath(f.env.dataHome, "256x256", "png")));
    CHECK(!QFileInfo::exists(iconPath(f.env.dataHome, "scalable", "svg")));
    CHECK(state(f.env) == State::Missing);
    // After the user removed it, startup does not bring it back.
    CHECK(startupAction(State::Missing, true) == StartupAction::None);
    CHECK(startupAction(State::Outdated, true) == StartupAction::None);
    // Removing twice is fine.
    CHECK(uninstall(f.env, nullptr));
}

static void foreignEntryIsLeftAlone() {
    Fake f;
    write(desktopFilePath(f.env), "[Desktop Entry]\nName=My own entry\nExec=something\n");
    write(f.env.dataHome + "/applications/other.desktop", "[Desktop Entry]\nName=Other\n");
    CHECK(state(f.env) == State::Foreign);
    CHECK(startupAction(State::Foreign, false) == StartupAction::None);
    QString error;
    CHECK(!uninstall(f.env, &error));
    CHECK(!error.isEmpty());
    CHECK(read(desktopFilePath(f.env)).contains("My own entry"));
    CHECK(QFileInfo::exists(f.env.dataHome + "/applications/other.desktop"));
}

static void environmentFromSystem() {
    QTemporaryDir dir;
    const QString appImage = dir.path() + "/x.AppImage";
    write(appImage, "x");
    qputenv("APPIMAGE", appImage.toUtf8());
    qputenv("APPDIR", "/tmp/.mount_abc");
    qputenv("XDG_DATA_HOME", (dir.path() + "/data").toUtf8());
    Environment env = Environment::fromSystem();
    CHECK(env.isAppImage());
    CHECK(env.appImagePath == appImage);
    CHECK(env.appDir == "/tmp/.mount_abc");
    CHECK(env.dataHome == dir.path() + "/data");
    CHECK(env.refreshCaches);
    CHECK(!Environment().refreshCaches);   // tests and callers opt in explicitly

    // $APPIMAGE pointing nowhere is not trusted.
    qputenv("APPIMAGE", (dir.path() + "/missing.AppImage").toUtf8());
    CHECK(!Environment::fromSystem().isAppImage());
    qunsetenv("APPIMAGE");
    CHECK(!Environment::fromSystem().isAppImage());

    qunsetenv("XDG_DATA_HOME");
    CHECK(Environment::fromSystem().dataHome == QDir::homePath() + "/.local/share");
    qunsetenv("APPDIR");
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    notAnAppImage();
    firstLaunchInstalls();
    movedAppImageIsUpdated();
    unusualPathsAreQuoted();
    fallbackIconWithoutAppDir();
    removeAndRemember();
    foreignEntryIsLeftAlone();
    environmentFromSystem();
    printf("desktop integration: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
