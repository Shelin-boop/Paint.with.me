#include "gpusupport.h"

#include "appinfo.h"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSettings>
#include <QSurfaceFormat>

namespace GpuSupport {

namespace {
constexpr const char *kSettingKey = "canvas/gpu";
constexpr const char *kStartingKey = "canvas/gpuStarting";
bool g_disabledForThisRun = false;

// GL_NVX_gpu_memory_info (NVIDIA) and GL_ATI_meminfo (AMD, Mesa radeonsi).
constexpr unsigned kNvxCurrentAvailableKb = 0x9049;
constexpr unsigned kAtiTextureFreeMemory = 0x87FC;

Info check() {
    Info info;
    const QString platform = QGuiApplication::platformName();
    if (platform == QLatin1String("offscreen") || platform == QLatin1String("minimal")
        || platform == QLatin1String("vnc")) {
        info.problem = QStringLiteral("this display system has no OpenGL");
        return info;
    }
    QOpenGLContext context;
    context.setFormat(QSurfaceFormat::defaultFormat());
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if (!context.create() || !surface.isValid() || !context.makeCurrent(&surface)) {
        info.problem = QStringLiteral("no OpenGL context could be made");
        return info;
    }
    QOpenGLFunctions *f = context.functions();
    auto text = [f](GLenum name) {
        const auto *s = reinterpret_cast<const char *>(f->glGetString(name));
        return s ? QString::fromLatin1(s) : QString();
    };
    info.vendor = text(GL_VENDOR);
    info.renderer = text(GL_RENDERER);
    info.version = text(GL_VERSION);
    f->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &info.maxTextureSize);
    if (context.hasExtension("GL_NVX_gpu_memory_info")) {
        GLint kb = 0;
        f->glGetIntegerv(kNvxCurrentAvailableKb, &kb);
        if (kb > 0) info.freeMemoryMb = kb / 1024;
    } else if (context.hasExtension("GL_ATI_meminfo")) {
        GLint values[4] = {0, 0, 0, 0};
        f->glGetIntegerv(kAtiTextureFreeMemory, values);
        if (values[0] > 0) info.freeMemoryMb = values[0] / 1024;
    }
    while (f->glGetError() != GL_NO_ERROR) {}
    const QSurfaceFormat format = context.format();
    context.doneCurrent();

    const QString lower = info.renderer.toLower();
    info.hardware = !(lower.contains(QLatin1String("llvmpipe")) || lower.contains(QLatin1String("softpipe"))
                      || lower.contains(QLatin1String("swrast")) || lower.contains(QLatin1String("software")));
    if (context.isOpenGLES()) {
        info.problem = QStringLiteral("only OpenGL ES is offered");
        return info;
    }
    if (format.majorVersion() < 3) {
        info.problem = QStringLiteral("OpenGL %1.%2 is too old (3.0 is needed)")
                           .arg(format.majorVersion()).arg(format.minorVersion());
        return info;
    }
    if (info.maxTextureSize < 8192) {
        info.problem = QStringLiteral("the graphics card takes pictures only up to %1 pixels").arg(info.maxTextureSize);
        return info;
    }
    info.contextOk = true;
    if (!info.hardware) info.problem = QStringLiteral("only a software stand-in (%1) is available").arg(info.renderer);
    return info;
}
}  // namespace

const Info &probe() {
    static const Info info = check();
    return info;
}

Mode mode() {
    const QString value = QSettings(AppInfo::kId, AppInfo::kId).value(kSettingKey).toString();
    if (value == QLatin1String("on")) return Mode::On;
    if (value == QLatin1String("off")) return Mode::Off;
    return Mode::Auto;
}

void setMode(Mode mode) {
    QSettings settings(AppInfo::kId, AppInfo::kId);
    settings.remove(kStartingKey);   // a new choice: try again
    switch (mode) {
    case Mode::On: settings.setValue(kSettingKey, QStringLiteral("on")); break;
    case Mode::Off: settings.setValue(kSettingKey, QStringLiteral("off")); break;
    case Mode::Auto: settings.remove(kSettingKey); break;
    }
}

bool wanted(QString *why) {
    const Mode m = mode();
    if (m == Mode::Off) {
        if (why) *why = QStringLiteral("switched off in Settings > Canvas");
        return false;
    }
    if (g_disabledForThisRun) {
        if (why) *why = QStringLiteral("started with --no-gpu");
        return false;
    }
    if (m == Mode::Auto && stoppedLastTime()) {
        if (why) *why = QStringLiteral("the graphics card stopped Paint.with.me the last time; "
                                       "choose \"Graphics card\" in Settings > Canvas to try again");
        return false;
    }
    const Info &info = probe();
    if (!info.contextOk) {
        if (why) *why = info.problem;
        return false;
    }
    if (m == Mode::Auto && !info.hardware) {
        if (why) *why = info.problem;
        return false;
    }
    if (why) why->clear();
    return true;
}

void markStarting(bool starting) {
    QSettings settings(AppInfo::kId, AppInfo::kId);
    if (starting) settings.setValue(kStartingKey, true);
    else settings.remove(kStartingKey);
}

bool stoppedLastTime() {
    return QSettings(AppInfo::kId, AppInfo::kId).value(kStartingKey, false).toBool();
}

void disableForThisRun() { g_disabledForThisRun = true; }

QString describe() {
    const Info &info = probe();
    if (info.renderer.isEmpty()) return QString();
    return QStringLiteral("%1, OpenGL %2").arg(info.renderer, info.version);
}

}  // namespace GpuSupport
