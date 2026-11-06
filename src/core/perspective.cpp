#include "core/perspective.h"

#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <cmath>

#include "appinfo.h"

namespace Perspective {

Settings &settings() {
    static Settings s = []() {
        Settings loaded;
        QSettings q(AppInfo::kId, AppInfo::kId);
        loaded.rays = qBound(4, q.value(QStringLiteral("perspective/rays"), loaded.rays).toInt(), 360);
        loaded.visible = q.value(QStringLiteral("perspective/visible"), loaded.visible).toBool();
        loaded.snap = q.value(QStringLiteral("perspective/snap"), loaded.snap).toBool();
        loaded.straight = q.value(QStringLiteral("perspective/straight"), loaded.straight).toBool();
        return loaded;
    }();
    return s;
}

void saveSettings() {
    const Settings &s = settings();
    QSettings q(AppInfo::kId, AppInfo::kId);
    q.setValue(QStringLiteral("perspective/rays"), s.rays);
    q.setValue(QStringLiteral("perspective/visible"), s.visible);
    q.setValue(QStringLiteral("perspective/snap"), s.snap);
    q.setValue(QStringLiteral("perspective/straight"), s.straight);
}

QColor color(int index) {
    static const QColor colors[] = {QColor(0, 150, 230), QColor(220, 40, 160), QColor(240, 130, 0)};
    return colors[qBound(0, index, 2)];
}

int pointNear(const QVector<QPointF> &points, const QPointF &pos, double radius) {
    int best = -1;
    double bestDist = radius;
    for (int i = 0; i < points.size(); ++i) {
        const double d = QLineF(points[i], pos).length();
        if (d <= bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

QPointF snapDirection(const QVector<QPointF> &points, bool straight, const QPointF &start,
                      const QPointF &pos) {
    const QPointF move = pos - start;
    const double moveLen = std::hypot(move.x(), move.y());
    if (moveLen < 1e-9) return QPointF();
    const QPointF m = move / moveLen;
    QVector<QPointF> candidates;
    for (const QPointF &p : points) {
        const QPointF d = p - start;
        const double len = std::hypot(d.x(), d.y());
        if (len > 1e-6) candidates << d / len;
    }
    if (straight) candidates << QPointF(1, 0) << QPointF(0, 1);
    QPointF best;
    double bestDot = -1;
    for (const QPointF &u : candidates) {
        const double dot = std::fabs(u.x() * m.x() + u.y() * m.y());   // toward or away
        if (dot > bestDot) {
            bestDot = dot;
            best = u;
        }
    }
    return best;
}

QPointF project(const QPointF &start, const QPointF &direction, const QPointF &pos) {
    if (direction.isNull()) return pos;
    const QPointF d = pos - start;
    const double t = d.x() * direction.x() + d.y() * direction.y();
    return start + direction * t;
}

void drawGuides(QPainter &painter, const QVector<QPointF> &points, int rays, const QRectF &area,
                const QTransform &toTarget, qreal penWidth, int alpha) {
    if (points.isEmpty() || rays <= 0 || area.isEmpty()) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;   // the image's outline, also when the view is turned
    clip.addPolygon(toTarget.map(QPolygonF(area)));
    painter.setClipPath(clip);
    for (int i = 0; i < points.size(); ++i) {
        QColor c = color(i);
        c.setAlpha(alpha);
        // Lines are mapped to target coordinates first, so the pen width is
        // in target pixels at any zoom.
        painter.setPen(QPen(c, penWidth));
        // Long enough to cross the whole area from wherever the point is.
        const QPointF p = points[i];
        double reach = 0;
        for (const QPointF &corner : {area.topLeft(), area.topRight(), area.bottomLeft(), area.bottomRight()})
            reach = std::max(reach, QLineF(p, corner).length());
        reach += 10;
        for (int k = 0; k < rays; ++k) {
            const double a = 2 * M_PI * k / rays;
            const QPointF end = p + QPointF(std::cos(a), std::sin(a)) * reach;
            painter.drawLine(toTarget.map(QLineF(p, end)));
        }
    }
    // The horizon: the line through the first two points.
    if (points.size() >= 2 && QLineF(points[0], points[1]).length() > 1e-6) {
        QLineF horizon(points[0], points[1]);
        const double reach = 4 * (QLineF(area.topLeft(), area.bottomRight()).length()
                                  + QLineF(points[0], area.center()).length());
        horizon.setLength(reach);
        QLineF back(points[0], points[0] - (points[1] - points[0]));
        back.setLength(reach);
        QColor c(30, 30, 30, qMin(255, alpha + 60));
        painter.setPen(QPen(c, penWidth * 1.5));
        painter.drawLine(toTarget.map(QLineF(back.p2(), horizon.p2())));
    }
    painter.restore();
}

QImage renderLayer(const QVector<QPointF> &points, int rays, const QSize &size) {
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    // About 1 px per 1000 px of image, so the lines stay visible on big covers.
    const qreal width = std::max<qreal>(1.0, std::min(size.width(), size.height()) / 1000.0);
    drawGuides(p, points, rays, QRectF(QPointF(0, 0), QSizeF(size)), QTransform(), width, 200);
    p.end();
    return img;
}

} // namespace Perspective
