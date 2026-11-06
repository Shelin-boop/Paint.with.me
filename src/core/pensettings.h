#pragma once

#include <QPointF>

// How a pen on a graphics tablet acts, for all tools (Settings > Tools).
// Which tools follow pressure, and whether it sets size or opacity, is chosen
// per tool in the tool bar.
namespace Pen {

struct Settings {
    // Pressure curve: -100 (soft: a light touch already paints strongly) ..
    // 0 (as the pen reports it) .. 100 (firm: needs a harder press).
    int pressureCurve = 0;
    // Tilting the pen flattens the brush tip along the tilt, like the side of
    // a pencil (brush and eraser).
    bool tiltFlattens = false;
};

// Loaded from the settings on first use; saveSettings() writes them back.
Settings &settings();
void loadSettings();
void saveSettings();

// The exponent the curve stands for: 2^(curve / 50), so 0.25 .. 1 .. 4.
double curveExponent(int curve);
// A pen's raw pressure (0..1) after the curve.
double mapPressure(double raw, int curve);
inline double mapPressure(double raw) { return mapPressure(raw, settings().pressureCurve); }

// The brush tip for a pen tilt (Qt's xTilt / yTilt in degrees): the direction
// of the tilt in degrees (0 = right, 90 = down) and how round the tip stays
// (1 = round, down to 0.2 for a pen lying almost flat).
struct TipShape {
    double angle = 0.0;
    double roundness = 1.0;
};
TipShape tipForTilt(const QPointF &tiltDegrees);

}  // namespace Pen
