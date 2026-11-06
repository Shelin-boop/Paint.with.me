#include "units.h"
#include "appinfo.h"

#include <QLocale>
#include <QSettings>
#include <cmath>

namespace Units {

double toInches(double value, Length unit) {
    switch (unit) {
    case Length::Millimeter: return value / kMillimetersPerInch;
    case Length::Centimeter: return value * 10.0 / kMillimetersPerInch;
    case Length::Inch: return value;
    }
    return value;
}

double fromInches(double inches, Length unit) {
    switch (unit) {
    case Length::Millimeter: return inches * kMillimetersPerInch;
    case Length::Centimeter: return inches * kMillimetersPerInch / 10.0;
    case Length::Inch: return inches;
    }
    return inches;
}

double toDpi(double value, Resolution unit) {
    return unit == Resolution::PerCentimeter ? value * kMillimetersPerInch / 10.0 : value;
}

double fromDpi(double dpi, Resolution unit) {
    return unit == Resolution::PerCentimeter ? dpi * 10.0 / kMillimetersPerInch : dpi;
}

int pixelsFor(double length, Length unit, double dpi) {
    const double px = toInches(length, unit) * dpi;
    if (!std::isfinite(px) || px < 1.0) return 1;
    if (px > 1e9) return 1000000000;
    return std::max(1, int(std::lround(px)));
}

double lengthFor(double pixels, double dpi, Length unit) {
    if (dpi <= 0) return 0.0;
    return fromInches(pixels / dpi, unit);
}

QString suffix(Length unit) {
    switch (unit) {
    case Length::Millimeter: return QStringLiteral("mm");
    case Length::Centimeter: return QStringLiteral("cm");
    case Length::Inch: return QStringLiteral("in");
    }
    return QString();
}

QString name(Length unit) {
    switch (unit) {
    case Length::Millimeter: return QStringLiteral("millimeters");
    case Length::Centimeter: return QStringLiteral("centimeters");
    case Length::Inch: return QStringLiteral("inches");
    }
    return QString();
}

QString name(Resolution unit) {
    return unit == Resolution::PerCentimeter ? QStringLiteral("pixels/cm")
                                             : QStringLiteral("pixels/inch (dpi)");
}

QString suffix(Resolution unit) {
    return unit == Resolution::PerCentimeter ? QStringLiteral("px/cm") : QStringLiteral("dpi");
}

int decimals(Length unit) {
    return unit == Length::Millimeter ? 2 : 3;
}

Length savedLength() {
    QSettings s(AppInfo::kId, AppInfo::kId);
    const QString v = s.value(QStringLiteral("units/length")).toString();
    if (v == QLatin1String("mm")) return Length::Millimeter;
    if (v == QLatin1String("cm")) return Length::Centimeter;
    if (v == QLatin1String("in")) return Length::Inch;
    return QLocale::system().measurementSystem() == QLocale::MetricSystem ? Length::Centimeter
                                                                           : Length::Inch;
}

void saveLength(Length unit) {
    QSettings s(AppInfo::kId, AppInfo::kId);
    s.setValue(QStringLiteral("units/length"), suffix(unit));
}

Resolution savedResolution() {
    QSettings s(AppInfo::kId, AppInfo::kId);
    return s.value(QStringLiteral("units/resolution")).toString() == QLatin1String("px/cm")
               ? Resolution::PerCentimeter
               : Resolution::PerInch;
}

void saveResolution(Resolution unit) {
    QSettings s(AppInfo::kId, AppInfo::kId);
    s.setValue(QStringLiteral("units/resolution"),
               unit == Resolution::PerCentimeter ? QStringLiteral("px/cm") : QStringLiteral("dpi"));
}

QString describe(int widthPx, int heightPx, double dpi, Length unit) {
    const int dec = decimals(unit);
    auto fmt = [&](double v) {
        // Drop trailing zeros: "210 x 297 mm", not "210.00 x 297.00 mm".
        QString s = QString::number(v, 'f', dec);
        if (s.contains('.')) {
            while (s.endsWith('0')) s.chop(1);
            if (s.endsWith('.')) s.chop(1);
        }
        return s;
    };
    QString dpiText = QString::number(dpi, 'f', 2);
    while (dpiText.endsWith('0')) dpiText.chop(1);
    if (dpiText.endsWith('.')) dpiText.chop(1);
    return QStringLiteral("%1 x %2 pixels (%3 x %4 %5 at %6 dpi)")
        .arg(widthPx).arg(heightPx)
        .arg(fmt(lengthFor(widthPx, dpi, unit)), fmt(lengthFor(heightPx, dpi, unit)), suffix(unit), dpiText);
}

}  // namespace Units
