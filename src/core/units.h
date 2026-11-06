#pragma once

#include <QString>
#include <QSizeF>

// Print sizes and resolutions. The document keeps its resolution in dots
// (pixels) per inch; everything a person types is converted through here,
// so every dialog rounds the same way.
namespace Units {

enum class Length { Millimeter, Centimeter, Inch };
enum class Resolution { PerInch, PerCentimeter };

constexpr double kMillimetersPerInch = 25.4;

double toInches(double value, Length unit);
double fromInches(double inches, Length unit);
double toDpi(double value, Resolution unit);       // px/cm -> px/in
double fromDpi(double dpi, Resolution unit);       // px/in -> px/cm

// Pixels for a printed length at a resolution, rounded to the nearest pixel
// and at least 1.
int pixelsFor(double length, Length unit, double dpi);
// Printed length of a pixel count at a resolution.
double lengthFor(double pixels, double dpi, Length unit);

QString suffix(Length unit);               // "mm", "cm", "in"
QString name(Length unit);                 // "millimeters" ...
QString name(Resolution unit);             // "pixels/inch (dpi)", "pixels/cm"
QString suffix(Resolution unit);           // "px/in", "px/cm"
int decimals(Length unit);                 // 2 for mm, 3 for cm and in

// Saved choice (settings "units/length", "units/resolution"). Without a saved
// choice: centimeters where the system locale is metric, else inches; dpi.
Length savedLength();
void saveLength(Length unit);
Resolution savedResolution();
void saveResolution(Resolution unit);

// "2480 x 3508 pixels (209.97 x 297.01 mm at 300 dpi)" for a pixel size.
QString describe(int widthPx, int heightPx, double dpi, Length unit);

}  // namespace Units
