#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QTransform>
#include <QVector>

class QPainter;

// Perspective guides: up to three vanishing points per image (stored in the
// Document, in document pixels, may lie outside the canvas). Guide lines
// radiate from them over the canvas; strokes can snap toward them. The guides
// are not part of the image unless drawn into a layer with renderLayer().
namespace Perspective {

constexpr int kMaxPoints = 3;

// App-wide choices, kept in the settings.
struct Settings {
    int rays = 48;          // guide lines per point, around the full circle
    bool visible = true;    // draw the guides over the canvas
    bool snap = true;       // strokes follow the guide they start along
    bool straight = true;   // horizontal and vertical lines count as guides too
};
Settings &settings();
void saveSettings();

QColor color(int index);

// Index of the point within `radius` of `pos`, or -1.
int pointNear(const QVector<QPointF> &points, const QPointF &pos, double radius);

// The direction (unit vector) a stroke from `start` currently heading for
// `pos` snaps to: the line toward the vanishing point that matches best, or
// horizontal or vertical when `straight`. Null when there is nothing to snap
// to or start and pos coincide.
QPointF snapDirection(const QVector<QPointF> &points, bool straight, const QPointF &start,
                      const QPointF &pos);
// `pos` moved onto the line through `start` along `direction`.
QPointF project(const QPointF &start, const QPointF &direction, const QPointF &pos);

// Draws the guide lines of every point over `area` (document coordinates)
// with `toTarget` mapping document to painter coordinates.
void drawGuides(QPainter &painter, const QVector<QPointF> &points, int rays, const QRectF &area,
                const QTransform &toTarget, qreal penWidth, int alpha);

// The guide lines as a picture of `size`, for a layer of their own.
QImage renderLayer(const QVector<QPointF> &points, int rays, const QSize &size);

} // namespace Perspective
