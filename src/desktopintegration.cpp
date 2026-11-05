#include "desktopintegration.h"

#include "appinfo.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace DesktopIntegration {
namespace {

const QString kMarkerKey = QStringLiteral("X-PaintWithMe-AppImage");
const QString kSettingsKey = QStringLiteral("desktopIntegration/userRemovedEntry");

QString iconFileName(const QString &suffix) {
    return QString::fromLatin1(AppInfo::kDesktopId) + QLatin1Char('.') + suffix;
}

// Escapes a string value for a desktop file (backslash, newline, tab).
QString escapeValue(QString s) {
    s.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    s.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    s.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    return s;
}

// Quotes a path for the Exec key. The Desktop Entry Specification applies the
// quoting rule (escape " ` $ \ inside double quotes) first and the general
// string escaping (\ becomes \\) on top of it. % starts a field code, so a
// literal % is written as %%.
QString execQuote(const QString &path) {
    QString q = path;
    q.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    q.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    q.replace(QLatin1Char('`'), QStringLiteral("\\`"));
    q.replace(QLatin1Char('$'), QStringLiteral("\\$"));
    q.replace(QLatin1Char('%'), QStringLiteral("%%"));
    return escapeValue(QLatin1Char('"') + q + QLatin1Char('"'));
}

QString iconsRoot(const Environment &env) {
    return env.dataHome + QStringLiteral("/icons/hicolor");
}

// Icons inside the AppImage, as (size folder, file path) pairs.
QList<QPair<QString, QString>> bundledIcons(const Environment &env) {
    QList<QPair<QString, QString>> out;
    if (env.appDir.isEmpty()) return out;
    const QString root = env.appDir + QStringLiteral("/usr/share/icons/hicolor");
    QDirIterator it(root, {iconFileName(QStringLiteral("png")), iconFileName(QStringLiteral("svg"))},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        // .../hicolor/<size>/apps/<file>
        const QString sizeFolder = QFileInfo(QFileInfo(path).path()).dir().dirName();
        out.append({sizeFolder, path});
    }
    return out;
}

bool hasMarker(const QString &desktopFile) {
    QFile f(desktopFile);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    return QString::fromUtf8(f.readAll()).contains(kMarkerKey + QStringLiteral("=true"));
}

bool copyReplacing(const QString &from, const QString &to) {
    QDir().mkpath(QFileInfo(to).path());
    QFile::remove(to);
    if (!QFile::copy(from, to)) return false;
    // Files copied out of the AppImage or the resources are read-only.
    QFile::setPermissions(to, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    return true;
}

// Lets the desktop notice the change sooner; harmless if the tool is missing.
void refreshMenuCaches(const Environment &env) {
    if (!env.refreshCaches) return;
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("update-desktop-database"));
    if (!tool.isEmpty())
        QProcess::startDetached(tool, {QStringLiteral("-q"), env.dataHome + QStringLiteral("/applications")});
}

} // namespace

Environment Environment::fromSystem() {
    Environment env;
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty() && QFileInfo(appImage).isFile())
        env.appImagePath = QFileInfo(appImage).absoluteFilePath();
    env.appDir = qEnvironmentVariable("APPDIR");
    env.dataHome = qEnvironmentVariable("XDG_DATA_HOME");
    if (env.dataHome.isEmpty())
        env.dataHome = QDir::homePath() + QStringLiteral("/.local/share");
    env.version = QStringLiteral(PWM_VERSION_STR);
    env.refreshCaches = true;
    return env;
}

QString desktopFilePath(const Environment &env) {
    return env.dataHome + QStringLiteral("/applications/") + QString::fromLatin1(AppInfo::kDesktopId)
           + QStringLiteral(".desktop");
}

QString desktopFileContent(const Environment &env) {
    QString s;
    s += QStringLiteral("[Desktop Entry]\n");
    s += QStringLiteral("Type=Application\n");
    s += QStringLiteral("Version=1.0\n");
    s += QStringLiteral("Name=") + QString::fromLatin1(AppInfo::kDisplayName) + QLatin1Char('\n');
    s += QStringLiteral("GenericName=Image Editor\n");
    s += QStringLiteral("Comment=Image editor in the style of Paint.NET\n");
    s += QStringLiteral("Exec=") + execQuote(env.appImagePath) + QStringLiteral(" %F\n");
    s += QStringLiteral("TryExec=") + escapeValue(env.appImagePath) + QLatin1Char('\n');
    s += QStringLiteral("Icon=") + QString::fromLatin1(AppInfo::kDesktopId) + QLatin1Char('\n');
    s += QStringLiteral("Terminal=false\n");
    s += QStringLiteral("Categories=Graphics;2DGraphics;RasterGraphics;\n");
    s += QStringLiteral("MimeType=image/vnd.adobe.photoshop;image/png;image/jpeg;image/bmp;image/gif;image/tiff;image/webp;\n");
    s += QStringLiteral("Keywords=paint;image;editor;photo;draw;pdn;\n");
    s += kMarkerKey + QStringLiteral("=true\n");
    s += QStringLiteral("X-PaintWithMe-Version=") + escapeValue(env.version) + QLatin1Char('\n');
    return s;
}

State state(const Environment &env) {
    if (!env.isAppImage()) return State::NotAppImage;
    const QString path = desktopFilePath(env);
    if (!QFileInfo::exists(path)) return State::Missing;
    if (!hasMarker(path)) return State::Foreign;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return State::Outdated;
    return QString::fromUtf8(f.readAll()) == desktopFileContent(env) ? State::Current : State::Outdated;
}

bool install(const Environment &env, QString *error) {
    auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (!env.isAppImage()) return fail(QStringLiteral("not running from an AppImage"));

    // Icons first, so the menu never shows an entry without its icon.
    const auto icons = bundledIcons(env);
    if (icons.isEmpty()) {
        const QString to = iconsRoot(env) + QStringLiteral("/256x256/apps/") + iconFileName(QStringLiteral("png"));
        if (!copyReplacing(QStringLiteral(":/app-icon.png"), to))
            return fail(QStringLiteral("could not write the icon to %1").arg(to));
    }
    for (const auto &icon : icons) {
        const QString to = iconsRoot(env) + QLatin1Char('/') + icon.first + QStringLiteral("/apps/")
                           + QFileInfo(icon.second).fileName();
        if (!copyReplacing(icon.second, to))
            return fail(QStringLiteral("could not write the icon to %1").arg(to));
    }

    const QString path = desktopFilePath(env);
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return fail(QStringLiteral("could not write %1").arg(path));
    file.write(desktopFileContent(env).toUtf8());
    if (!file.commit())
        return fail(QStringLiteral("could not write %1").arg(path));
    refreshMenuCaches(env);
    return true;
}

bool uninstall(const Environment &env, QString *error) {
    const QString path = desktopFilePath(env);
    if (QFileInfo::exists(path)) {
        if (!hasMarker(path)) {
            if (error) *error = QStringLiteral("%1 was not created by Paint.with.me; left unchanged").arg(path);
            return false;
        }
        if (!QFile::remove(path)) {
            if (error) *error = QStringLiteral("could not remove %1").arg(path);
            return false;
        }
    }
    QDirIterator it(iconsRoot(env), {iconFileName(QStringLiteral("png")), iconFileName(QStringLiteral("svg"))},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) QFile::remove(it.next());
    refreshMenuCaches(env);
    return true;
}

StartupAction startupAction(State state, bool userRemoved) {
    if (userRemoved) return StartupAction::None;
    switch (state) {
    case State::Missing:  return StartupAction::Install;
    case State::Outdated: return StartupAction::Update;
    default:              return StartupAction::None;
    }
}

bool userRemovedEntry() {
    return QSettings(AppInfo::kId, AppInfo::kId).value(kSettingsKey, false).toBool();
}

void setUserRemovedEntry(bool removed) {
    QSettings(AppInfo::kId, AppInfo::kId).setValue(kSettingsKey, removed);
}

StartupResult applyAtStartup() {
    const Environment env = Environment::fromSystem();
    switch (startupAction(state(env), userRemovedEntry())) {
    case StartupAction::None:   return StartupResult::Nothing;
    case StartupAction::Install: return install(env) ? StartupResult::Added : StartupResult::Failed;
    case StartupAction::Update:  return install(env) ? StartupResult::Updated : StartupResult::Failed;
    }
    return StartupResult::Nothing;
}

} // namespace DesktopIntegration
