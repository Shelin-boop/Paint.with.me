#include "displayserver.h"

#include <QSettings>
#include <QtGlobal>

#include "appinfo.h"

namespace DisplayServer {

namespace {
constexpr const char *kSettingKey = "display/nativeWayland";
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
    const Environment env = Environment::fromSystem(argc, argv);
    g_forced = env.platformForced;
    const QByteArray platform = platformFor(env, nativeWaylandEnabled());
    if (!platform.isEmpty()) qputenv("QT_QPA_PLATFORM", platform);
}

bool platformForcedAtStartup() { return g_forced; }

bool nativeWaylandEnabled() {
    // Readable before the QApplication exists: the names are given here.
    QSettings settings(AppInfo::kId, AppInfo::kId);
    return settings.value(kSettingKey, false).toBool();
}

void setNativeWaylandEnabled(bool on) {
    QSettings settings(AppInfo::kId, AppInfo::kId);
    settings.setValue(kSettingKey, on);
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
