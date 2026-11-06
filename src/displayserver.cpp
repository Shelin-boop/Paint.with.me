#include "displayserver.h"

#include <QCoreApplication>
#include <QProcess>
#include <QSettings>
#include <QtGlobal>

#include "appinfo.h"

namespace DisplayServer {

namespace {
constexpr const char *kSettingKey = "display/windowSystem";
// Opt-in switch of the first test builds (native Wayland was not yet the
// default); dropped so everyone gets the new default.
constexpr const char *kOldSettingKey = "display/nativeWayland";
bool g_forced = false;
}  // namespace

Environment Environment::fromSystem(int argc, char **argv) {
    Environment env;
    env.platformForced = qEnvironmentVariableIsSet("QT_QPA_PLATFORM");
    // Qt also takes "-platform name" (and "--platform name") on the command line.
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "-platform" || arg == "--platform" || arg.startsWith("-platform=")
            || arg.startsWith("--platform="))
            env.platformForced = true;
    }
    env.waylandSession = !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
    env.xDisplay = !qEnvironmentVariableIsEmpty("DISPLAY");
    return env;
}

QByteArray platformFor(const Environment &env, bool nativeWayland) {
    if (env.platformForced || !env.waylandSession) return {};
    // Natively, falling back to XWayland if the Wayland plugin cannot start.
    if (nativeWayland) return env.xDisplay ? QByteArray("wayland;xcb") : QByteArray("wayland");
    // No XWayland running: Wayland is the only choice, Qt finds it itself.
    if (!env.xDisplay) return {};
    return "xcb";
}

void applyAtStartup(int argc, char **argv) {
    QSettings(AppInfo::kId, AppInfo::kId).remove(kOldSettingKey);
    const Environment env = Environment::fromSystem(argc, argv);
    g_forced = env.platformForced;
    const QByteArray platform = platformFor(env, nativeWaylandEnabled());
    if (!platform.isEmpty()) qputenv("QT_QPA_PLATFORM", platform);
}

bool platformForcedAtStartup() { return g_forced; }

bool nativeWaylandEnabled() {
    // Readable before the QApplication exists: the names are given here.
    QSettings settings(AppInfo::kId, AppInfo::kId);
    return settings.value(kSettingKey).toString() != QLatin1String("xwayland");
}

void setNativeWaylandEnabled(bool on) {
    QSettings settings(AppInfo::kId, AppInfo::kId);
    settings.setValue(kSettingKey, on ? QStringLiteral("wayland") : QStringLiteral("xwayland"));
}

bool runningAsChosen(const QString &qtPlatform, bool nativeWayland) {
    return nativeWayland ? qtPlatform.startsWith(QLatin1String("wayland"))
                         : qtPlatform == QLatin1String("xcb");
}

Relaunch relaunchCommand(const QString &applicationFile, const QStringList &files,
                         const QProcessEnvironment &environment, bool platformForced) {
    Relaunch r;
    const QString appImage = environment.value(QStringLiteral("APPIMAGE"));
    r.program = appImage.isEmpty() ? applicationFile : appImage;
    r.arguments = files;
    r.environment = environment;
    if (!platformForced) r.environment.remove(QStringLiteral("QT_QPA_PLATFORM"));
    for (const char *name : {"APPIMAGE", "APPDIR", "ARGV0", "OWD"})
        r.environment.remove(QString::fromLatin1(name));
    return r;
}

bool relaunch(const QStringList &files) {
    const Relaunch r = relaunchCommand(QCoreApplication::applicationFilePath(), files,
                                       QProcessEnvironment::systemEnvironment(), g_forced);
    QProcess process;
    process.setProgram(r.program);
    process.setArguments(r.arguments);
    process.setProcessEnvironment(r.environment);
    return process.startDetached();
}

QString describe(const QString &qtPlatform, bool waylandSession) {
    if (qtPlatform.startsWith(QLatin1String("wayland"))) return QStringLiteral("Wayland (native)");
    if (qtPlatform == QLatin1String("xcb"))
        return waylandSession ? QStringLiteral("X11 through XWayland") : QStringLiteral("X11");
    return qtPlatform;
}

bool canPlaceWindows(const QString &qtPlatform) {
    return !qtPlatform.startsWith(QLatin1String("wayland"));
}

}  // namespace DisplayServer
