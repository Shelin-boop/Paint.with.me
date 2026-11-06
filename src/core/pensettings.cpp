#include "pensettings.h"

#include <QSettings>
#include <QtGlobal>
#include <cmath>

#include "appinfo.h"

namespace Pen {

namespace {
bool g_loaded = false;
Settings g_settings;
}  // namespace

Settings &settings() {
    if (!g_loaded) loadSettings();
    return g_settings;
}

void loadSettings() {
    QSettings s(AppInfo::kId, AppInfo::kId);
    g_settings.pressureCurve = qBound(-100, s.value("pen/pressureCurve", 0).toInt(), 100);
    g_settings.tiltFlattens = s.value("pen/tiltFlattens", false).toBool();
    g_loaded = true;
}

void saveSettings() {
    QSettings s(AppInfo::kId, AppInfo::kId);
    s.setValue("pen/pressureCurve", settings().pressureCurve);
    s.setValue("pen/tiltFlattens", settings().tiltFlattens);
}

double curveExponent(int curve) {
    return std::pow(2.0, qBound(-100, curve, 100) / 50.0);
}

double mapPressure(double raw, int curve) {
    const double p = qBound(0.0, raw, 1.0);
    if (curve == 0) return p;
    return std::pow(p, curveExponent(curve));
}

TipShape tipForTilt(const QPointF &tiltDegrees) {
    TipShape tip;
    // Qt reports the angle to the vertical in the x-z and y-z planes.
    const double tx = std::tan(qBound(-80.0, tiltDegrees.x(), 80.0) * M_PI / 180.0);
    const double ty = std::tan(qBound(-80.0, tiltDegrees.y(), 80.0) * M_PI / 180.0);
    const double lean = std::sqrt(tx * tx + ty * ty);   // tan of the angle to the vertical
    if (lean < 1e-6) return tip;
    tip.angle = std::atan2(ty, tx) * 180.0 / M_PI;
    tip.roundness = qMax(0.2, 1.0 / std::sqrt(1.0 + lean * lean));   // cos of that angle
    return tip;
}

}  // namespace Pen
