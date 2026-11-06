#include "commandicons.h"

#include <QAction>
#include <QConicalGradient>
#include <QHash>
#include <QLinearGradient>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QtMath>
#include <functional>

#include "theme.h"
#include "toolicons.h"

namespace {

// All drawings use a 20 x 20 box; makeIcon renders them at 16, 20, 32 and 40 px.
using Draw = std::function<void(QPainter &)>;

// ---- Colors: lines follow the theme, fills read on light and dark ----
QColor ink() { return Theme::isDark() ? QColor(214, 218, 226) : QColor(58, 64, 74); }
QColor paper() { return Theme::isDark() ? QColor(78, 84, 96) : QColor(255, 255, 255); }
const QColor kSky(112, 172, 232);
const QColor kHill(88, 168, 82);
const QColor kSun(250, 198, 58);
const QColor kBlue(58, 118, 206);
const QColor kOrange(232, 138, 38);
const QColor kRed(220, 68, 68);
const QColor kGreen(66, 168, 78);
const QColor kPurple(138, 92, 200);
const QColor kTeal(28, 150, 134);
const QColor kBrown(156, 106, 58);
const QColor kGray(140, 146, 156);

QIcon makeIcon(const Draw &draw) {
    QIcon icon;
    for (int size : {16, 20, 32, 40}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.scale(size / 20.0, size / 20.0);
        draw(p);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

QPen line(const QColor &c, qreal w = 1.2) {
    return QPen(c, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

// ---- Shared motifs ----

// A small landscape picture: sky, a hill and the sun.
void picture(QPainter &p, const QRectF &r) {
    p.save();
    QLinearGradient sky(r.topLeft(), r.bottomLeft());
    sky.setColorAt(0, kSky.lighter(115));
    sky.setColorAt(1, kSky);
    p.setPen(Qt::NoPen);
    p.setBrush(sky);
    p.drawRect(r);
    QPainterPath hill;
    hill.moveTo(r.left(), r.bottom());
    hill.lineTo(r.left(), r.top() + r.height() * 0.72);
    hill.quadTo(r.left() + r.width() * 0.35, r.top() + r.height() * 0.38,
                r.left() + r.width() * 0.62, r.top() + r.height() * 0.66);
    hill.quadTo(r.left() + r.width() * 0.82, r.top() + r.height() * 0.55, r.right(), r.top() + r.height() * 0.62);
    hill.lineTo(r.right(), r.bottom());
    hill.closeSubpath();
    p.setBrush(kHill);
    p.drawPath(hill);
    const qreal sr = qMax(1.0, r.width() * 0.12);
    p.setBrush(kSun);
    p.drawEllipse(QPointF(r.right() - r.width() * 0.26, r.top() + r.height() * 0.28), sr, sr);
    p.setPen(line(ink(), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
    p.restore();
}

// A layer card seen from above at an angle (a flat diamond).
void slab(QPainter &p, qreal cx, qreal cy, qreal w, qreal h, const QColor &fill) {
    QPolygonF d;
    d << QPointF(cx, cy - h / 2) << QPointF(cx + w / 2, cy) << QPointF(cx, cy + h / 2) << QPointF(cx - w / 2, cy);
    p.setPen(line(ink(), 1.0));
    p.setBrush(fill);
    p.drawPolygon(d);
}

void layerStack(QPainter &p, qreal cx = 9, qreal top = 6, int count = 2) {
    const QColor fills[] = {QColor(150, 186, 236), QColor(196, 216, 246), QColor(226, 236, 250)};
    for (int i = count - 1; i >= 0; --i) slab(p, cx, top + i * 4, 15, 7, fills[i % 3]);
}

void arrow(QPainter &p, QPointF a, QPointF b, const QColor &c, qreal w = 1.6, qreal head = 3.2) {
    p.setPen(line(c, w));
    p.drawLine(a, b);
    const QLineF l(b, a);
    const qreal ang = qDegreesToRadians(-l.angle());
    const QPointF dir(std::cos(ang), std::sin(ang));
    const QPointF n(-dir.y(), dir.x());
    QPolygonF tip;
    tip << b << b + dir * head + n * head * 0.65 << b + dir * head - n * head * 0.65;
    p.setPen(line(c, 1.0));
    p.setBrush(c);
    p.drawPolygon(tip);
}

// An arc arrow around `c`: from `start` degrees through `span` (Qt angles,
// counter-clockwise positive), with the head at the end.
void arcArrow(QPainter &p, QPointF c, qreal r, qreal start, qreal span, const QColor &col, qreal w = 1.6) {
    p.setPen(line(col, w));
    p.setBrush(Qt::NoBrush);
    const QRectF box(c.x() - r, c.y() - r, 2 * r, 2 * r);
    p.drawArc(box, int(start * 16), int(span * 16));
    const qreal end = qDegreesToRadians(start + span);
    const QPointF tipPt(c.x() + r * std::cos(end), c.y() - r * std::sin(end));
    // Tangent in the direction of travel.
    const qreal sgn = span >= 0 ? 1.0 : -1.0;
    const QPointF tangent(-std::sin(end) * sgn, -std::cos(end) * sgn);
    const QPointF n(-tangent.y(), tangent.x());
    QPolygonF tip;
    tip << tipPt + tangent * 3.0 << tipPt + n * 2.4 << tipPt - n * 2.4;
    p.setPen(line(col, 0.8));
    p.setBrush(col);
    p.drawPolygon(tip);
}

// A round badge in the bottom-right corner with a plus, minus or cross.
void badge(QPainter &p, const QColor &c, char sign) {
    p.setPen(QPen(paper(), 1.2));
    p.setBrush(c);
    p.drawEllipse(QPointF(15, 15), 4.2, 4.2);
    p.setPen(line(Qt::white, 1.5));
    if (sign == '+' || sign == '-') p.drawLine(QPointF(12.8, 15), QPointF(17.2, 15));
    if (sign == '+') p.drawLine(QPointF(15, 12.8), QPointF(15, 17.2));
    if (sign == 'x') {
        p.drawLine(QPointF(13.3, 13.3), QPointF(16.7, 16.7));
        p.drawLine(QPointF(16.7, 13.3), QPointF(13.3, 16.7));
    }
}

// Two mirrored triangles about a dashed axis: the left (or top) one filled,
// its mirror image as an outline.
void flipGlyph(QPainter &p, bool horizontal, const QRectF &r) {
    QPolygonF first, second;
    QLineF axis;
    if (horizontal) {
        const qreal cx = r.center().x();
        first << QPointF(cx - 1.5, r.top() + 2) << QPointF(cx - 1.5, r.bottom() - 2) << QPointF(r.left() + 1, r.bottom() - 2);
        second << QPointF(cx + 1.5, r.top() + 2) << QPointF(cx + 1.5, r.bottom() - 2) << QPointF(r.right() - 1, r.bottom() - 2);
        axis = QLineF(cx, r.top(), cx, r.bottom());
    } else {
        const qreal cy = r.center().y();
        first << QPointF(r.left() + 2, cy - 1.5) << QPointF(r.right() - 2, cy - 1.5) << QPointF(r.right() - 2, r.top() + 1);
        second << QPointF(r.left() + 2, cy + 1.5) << QPointF(r.right() - 2, cy + 1.5) << QPointF(r.right() - 2, r.bottom() - 1);
        axis = QLineF(r.left(), cy, r.right(), cy);
    }
    p.setPen(line(ink(), 1.0));
    p.setBrush(kBlue);
    p.drawPolygon(first);
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(second);
    QPen dash(ink(), 1.0, Qt::DashLine);
    dash.setDashPattern({1.5, 1.5});
    p.setPen(dash);
    p.drawLine(axis);
}

void histogram(QPainter &p, const QRectF &r, const QColor &c) {
    const qreal heights[] = {0.25, 0.45, 0.8, 0.95, 0.7, 0.5, 0.35, 0.2};
    const qreal bw = r.width() / 8.0;
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    for (int i = 0; i < 8; ++i) {
        const qreal h = heights[i] * r.height();
        p.drawRect(QRectF(r.left() + i * bw, r.bottom() - h, bw - 0.4, h));
    }
    p.setPen(line(ink(), 1.0));
    p.drawLine(r.bottomLeft(), r.bottomRight());
}

void circle(QPainter &p, QPointF c, qreal r, const QBrush &fill, bool outline = true) {
    p.setPen(outline ? line(ink(), 1.0) : QPen(Qt::NoPen));
    p.setBrush(fill);
    p.drawEllipse(c, r, r);
}

// A square "sample" for effects: a colored ball on a light square.
void sample(QPainter &p, const QRectF &r) {
    p.setPen(line(ink(), 1.0));
    p.setBrush(paper());
    p.drawRect(r);
}

// ---- The icons ----
QHash<QString, Draw> &table() {
    static QHash<QString, Draw> t;
    if (!t.isEmpty()) return t;

    // ================= Image =================
    t["Image > Resize"] = [](QPainter &p) {
        QPen dash(ink(), 1.0, Qt::DashLine);
        dash.setDashPattern({1.5, 1.5});
        p.setPen(dash);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(1.5, 1.5, 17, 17));
        picture(p, QRectF(1.5, 9.5, 9, 9));
        arrow(p, QPointF(9.5, 10.5), QPointF(17, 3), kOrange);
    };
    t["Image > Canvas Size"] = [](QPainter &p) {
        p.setPen(line(ink(), 1.0));
        p.setBrush(paper());
        p.drawRect(QRectF(1.5, 1.5, 17, 17));
        picture(p, QRectF(6, 6, 8, 8));
        const QColor c = kOrange;
        arrow(p, QPointF(10, 5), QPointF(10, 2.3), c, 1.2, 2.2);
        arrow(p, QPointF(10, 15), QPointF(10, 17.7), c, 1.2, 2.2);
        arrow(p, QPointF(5, 10), QPointF(2.3, 10), c, 1.2, 2.2);
        arrow(p, QPointF(15, 10), QPointF(17.7, 10), c, 1.2, 2.2);
    };
    t["Image > Rotate 90° Clockwise"] = [](QPainter &p) {
        picture(p, QRectF(5.5, 7.5, 9, 11));
        arcArrow(p, QPointF(10, 10), 7.5, 160, -120, kOrange);
    };
    t["Image > Rotate 90° Counter-Clockwise"] = [](QPainter &p) {
        picture(p, QRectF(5.5, 7.5, 9, 11));
        arcArrow(p, QPointF(10, 10), 7.5, 20, 120, kOrange);
    };
    t["Image > Rotate 180°"] = [](QPainter &p) {
        p.save();
        p.translate(10, 11);
        p.rotate(180);
        p.translate(-10, -11);
        picture(p, QRectF(5.5, 7, 9, 8));
        p.restore();
        arcArrow(p, QPointF(10, 10), 8, 200, -220, kOrange);
    };
    t["Image > Flip Horizontal"] = [](QPainter &p) { flipGlyph(p, true, QRectF(1, 3, 18, 14)); };
    t["Image > Flip Vertical"] = [](QPainter &p) { flipGlyph(p, false, QRectF(3, 1, 14, 18)); };
    t["Image > Flatten"] = [](QPainter &p) {
        slab(p, 9, 4.5, 13, 5, QColor(196, 216, 246));
        slab(p, 9, 8, 13, 5, QColor(150, 186, 236));
        arrow(p, QPointF(9, 10), QPointF(9, 13.2), kOrange, 1.5, 2.6);
        p.setPen(line(ink(), 1.0));
        p.setBrush(QColor(110, 150, 220));
        QPolygonF d;
        d << QPointF(9, 13.5) << QPointF(17.5, 16.5) << QPointF(9, 19.5) << QPointF(0.5, 16.5);
        p.drawPolygon(d);
    };

    // ================= Layers =================
    t["Layers > Add New Layer"] = [](QPainter &p) { layerStack(p, 9, 6, 2); badge(p, kGreen, '+'); };
    t["Layers > Delete Layer"] = [](QPainter &p) { layerStack(p, 9, 6, 2); badge(p, kRed, 'x'); };
    t["Layers > Duplicate Layer"] = [](QPainter &p) {
        slab(p, 11, 6, 15, 7, QColor(196, 216, 246));
        slab(p, 8, 11, 15, 7, QColor(150, 186, 236));
        badge(p, kBlue, '+');
    };
    t["Layers > Merge Layer Down"] = [](QPainter &p) {
        slab(p, 9, 4.5, 14, 6, QColor(196, 216, 246));
        slab(p, 9, 14.5, 14, 6, QColor(150, 186, 236));
        arrow(p, QPointF(9, 7.5), QPointF(9, 11.5), kOrange, 1.5, 2.6);
    };
    t["Layers > Import from File"] = [](QPainter &p) {
        slab(p, 9, 14, 15, 7, QColor(150, 186, 236));
        p.setPen(line(ink(), 1.0));
        p.setBrush(paper());
        QPolygonF doc;
        doc << QPointF(11, 1) << QPointF(16, 1) << QPointF(18.5, 3.5) << QPointF(18.5, 9) << QPointF(11, 9);
        p.drawPolygon(doc);
        arrow(p, QPointF(13, 6), QPointF(8, 11), kGreen, 1.5, 2.6);
    };
    t["Layers > Convert Text to Pixels"] = [](QPainter &p) {
        // A letter T, an arrow, and a block of pixels.
        p.setPen(line(ink(), 1.8));
        p.drawLine(QPointF(1.5, 4), QPointF(8.5, 4));
        p.drawLine(QPointF(5, 4), QPointF(5, 13));
        arrow(p, QPointF(7, 15.5), QPointF(12, 15.5), kOrange, 1.4, 2.4);
        p.setPen(Qt::NoPen);
        const QColor cells[4] = {kBlue, QColor(150, 186, 236), QColor(150, 186, 236), kBlue};
        for (int i = 0; i < 4; ++i)
            p.fillRect(QRectF(12 + (i % 2) * 3.5, 5 + (i / 2) * 3.5, 3.2, 3.2), cells[i]);
    };
    t["Layers > Move Layer Up"] = [](QPainter &p) {
        layerStack(p, 8, 7, 2);
        arrow(p, QPointF(16.5, 17), QPointF(16.5, 3), kBlue);
    };
    t["Layers > Move Layer Down"] = [](QPainter &p) {
        layerStack(p, 8, 7, 2);
        arrow(p, QPointF(16.5, 3), QPointF(16.5, 17), kBlue);
    };
    t["Layers > Flip Layer > Horizontal"] = [](QPainter &p) {
        slab(p, 10, 15, 17, 7, QColor(196, 216, 246));
        flipGlyph(p, true, QRectF(3, 1, 14, 11));
    };
    t["Layers > Flip Layer > Vertical"] = [](QPainter &p) {
        slab(p, 10, 15, 17, 7, QColor(196, 216, 246));
        flipGlyph(p, false, QRectF(4.5, 0.5, 11, 12));
    };
    t["Layers > Rotate / Zoom Layer"] = [](QPainter &p) {
        p.save();
        p.translate(9, 11);
        p.rotate(-18);
        p.translate(-9, -11);
        slab(p, 9, 11, 14, 8, QColor(150, 186, 236));
        p.restore();
        arcArrow(p, QPointF(9, 11), 7.5, 120, 120, kOrange, 1.4);
        p.setPen(line(ink(), 1.4));
        p.setBrush(paper());
        p.drawEllipse(QPointF(15, 5), 3, 3);
        p.drawLine(QPointF(17.1, 7.1), QPointF(19, 9));
    };
    t["Layers > Layer Properties"] = [](QPainter &p) {
        layerStack(p, 8, 5, 1);
        p.setPen(line(ink(), 1.0));
        p.setBrush(paper());
        p.drawRoundedRect(QRectF(5.5, 9.5, 13, 9), 1.5, 1.5);
        p.setPen(line(kGray, 1.2));
        for (int i = 0; i < 3; ++i) p.drawLine(QPointF(7.5, 12 + i * 2.2), QPointF(16.5, 12 + i * 2.2));
        p.setPen(Qt::NoPen);
        p.setBrush(kBlue);
        p.drawEllipse(QPointF(10, 12), 1.3, 1.3);
        p.drawEllipse(QPointF(14.5, 14.2), 1.3, 1.3);
        p.drawEllipse(QPointF(9, 16.4), 1.3, 1.3);
    };
    t["Layers > Toggle Visibility"] = [](QPainter &p) {
        QPainterPath eye;
        eye.moveTo(1.5, 10);
        eye.quadTo(10, 1.5, 18.5, 10);
        eye.quadTo(10, 18.5, 1.5, 10);
        p.setPen(line(ink(), 1.2));
        p.setBrush(paper());
        p.drawPath(eye);
        circle(p, QPointF(10, 10), 3.6, kBlue, false);
        circle(p, QPointF(10, 10), 1.6, QColor(30, 34, 40), false);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(11.2, 8.8), 0.8, 0.8);
    };

    // ================= Adjustments =================
    t["Adjustments > Auto-Level"] = [](QPainter &p) {
        histogram(p, QRectF(1.5, 6, 13, 12), kGray);
        // A spark: done automatically.
        p.setPen(Qt::NoPen);
        p.setBrush(kSun.darker(105));
        QPolygonF star;
        for (int i = 0; i < 8; ++i) {
            const qreal r = (i % 2) ? 1.6 : 4.5;
            const qreal a = qDegreesToRadians(i * 45.0 - 90);
            star << QPointF(15 + r * std::cos(a), 5 + r * std::sin(a));
        }
        p.drawPolygon(star);
    };
    t["Adjustments > Brightness / Contrast"] = [](QPainter &p) {
        p.setPen(line(kOrange, 1.4));
        for (int i = 0; i < 8; ++i) {
            const qreal a = qDegreesToRadians(i * 45.0);
            p.drawLine(QPointF(10 + 6.4 * std::cos(a), 10 + 6.4 * std::sin(a)),
                       QPointF(10 + 8.8 * std::cos(a), 10 + 8.8 * std::sin(a)));
        }
        p.setPen(line(ink(), 1.0));
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(10, 10), 5, 5);
        QPainterPath half;
        half.moveTo(10, 5);
        half.arcTo(QRectF(5, 5, 10, 10), 90, 180);
        half.closeSubpath();
        p.setBrush(QColor(40, 44, 52));
        p.drawPath(half);
    };
    t["Adjustments > Hue / Saturation"] = [](QPainter &p) {
        QConicalGradient g(QPointF(10, 10), 90);
        const QColor stops[] = {Qt::red, Qt::yellow, Qt::green, Qt::cyan, Qt::blue, Qt::magenta, Qt::red};
        for (int i = 0; i < 7; ++i) g.setColorAt(i / 6.0, stops[i]);
        circle(p, QPointF(10, 10), 8.5, g);
        QRadialGradient white(QPointF(10, 10), 5);
        white.setColorAt(0, QColor(255, 255, 255, 255));
        white.setColorAt(1, QColor(255, 255, 255, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(white);
        p.drawEllipse(QPointF(10, 10), 5, 5);
    };
    t["Adjustments > Levels"] = [](QPainter &p) {
        histogram(p, QRectF(1.5, 2, 17, 11), kGray);
        const qreal xs[] = {2.5, 10, 17.5};
        const QColor cs[] = {QColor(30, 30, 30), QColor(140, 140, 140), Qt::white};
        for (int i = 0; i < 3; ++i) {
            QPolygonF tri;
            tri << QPointF(xs[i], 14) << QPointF(xs[i] + 2.4, 18.5) << QPointF(xs[i] - 2.4, 18.5);
            p.setPen(line(ink(), 0.9));
            p.setBrush(cs[i]);
            p.drawPolygon(tri);
        }
    };
    t["Adjustments > Curves"] = [](QPainter &p) {
        p.setPen(line(ink(), 1.0));
        p.setBrush(paper());
        p.drawRect(QRectF(1.5, 1.5, 17, 17));
        p.setPen(QPen(kGray, 0.6));
        for (int i = 1; i < 4; ++i) {
            p.drawLine(QPointF(1.5 + i * 4.25, 1.5), QPointF(1.5 + i * 4.25, 18.5));
            p.drawLine(QPointF(1.5, 1.5 + i * 4.25), QPointF(18.5, 1.5 + i * 4.25));
        }
        QPainterPath s;
        s.moveTo(1.5, 18.5);
        s.cubicTo(9, 18, 8, 2, 18.5, 1.5);
        p.setPen(line(kBlue, 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawPath(s);
        p.setPen(line(ink(), 1.0));
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(6.5, 13), 1.5, 1.5);
        p.drawEllipse(QPointF(13.5, 6.8), 1.5, 1.5);
    };
    t["Adjustments > Invert Colors"] = [](QPainter &p) {
        p.setPen(line(ink(), 1.0));
        p.setBrush(kOrange);
        p.drawRect(QRectF(1.5, 1.5, 17, 17));
        QPolygonF lower;
        lower << QPointF(18.5, 1.5) << QPointF(18.5, 18.5) << QPointF(1.5, 18.5);
        p.setBrush(QColor(23, 117, 217));   // the orange's complement
        p.drawPolygon(lower);
        circle(p, QPointF(7, 7), 2.6, QColor(23, 117, 217), false);
        circle(p, QPointF(13, 13), 2.6, kOrange, false);
    };
    t["Adjustments > Sepia"] = [](QPainter &p) {
        picture(p, QRectF(1.5, 3.5, 17, 13));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(150, 100, 40, 150));
        p.drawRect(QRectF(1.5, 3.5, 17, 13));
        p.setPen(line(ink(), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(1.5, 3.5, 17, 13));
    };
    t["Adjustments > Posterize"] = [](QPainter &p) {
        const QColor bands[] = {QColor(48, 82, 160), QColor(72, 146, 210), QColor(130, 200, 120), QColor(246, 200, 60)};
        p.setPen(line(ink(), 1.0));
        for (int i = 0; i < 4; ++i) {
            p.setBrush(bands[i]);
            p.drawRect(QRectF(1.5 + i * 4.25, 18.5 - (i + 1) * 4.25, 4.25, (i + 1) * 4.25));
        }
    };
    t["Adjustments > Black and White"] = [](QPainter &p) {
        p.setPen(line(ink(), 1.0));
        for (int i = 0; i < 4; ++i) {
            const int v = 30 + i * 70;
            p.setBrush(QColor(v, v, v));
            p.drawRect(QRectF(1.5 + i * 4.25, 4.5, 4.25, 11));
        }
    };
    t["Adjustments > Threshold"] = [](QPainter &p) {
        p.setPen(line(ink(), 1.0));
        p.setBrush(Qt::white);
        p.drawRect(QRectF(1.5, 1.5, 17, 17));
        QPainterPath dark;
        dark.moveTo(1.5, 18.5);
        dark.lineTo(1.5, 9);
        dark.lineTo(5, 11);
        dark.lineTo(8, 6);
        dark.lineTo(11, 10);
        dark.lineTo(14, 4);
        dark.lineTo(18.5, 8);
        dark.lineTo(18.5, 18.5);
        dark.closeSubpath();
        p.setBrush(QColor(30, 32, 38));
        p.drawPath(dark);
    };
    t["Adjustments > Color Balance"] = [](QPainter &p) {
        p.setPen(line(ink(), 1.3));
        p.drawLine(QPointF(10, 3), QPointF(10, 17));
        p.drawLine(QPointF(5, 17.5), QPointF(15, 17.5));
        p.drawLine(QPointF(2.5, 6), QPointF(17.5, 4));
        p.setPen(line(ink(), 0.9));
        p.drawLine(QPointF(2.5, 6), QPointF(1.5, 10));
        p.drawLine(QPointF(2.5, 6), QPointF(5, 10));
        p.drawLine(QPointF(17.5, 4), QPointF(15, 8));
        p.drawLine(QPointF(17.5, 4), QPointF(19, 8));
        circle(p, QPointF(3.2, 11), 2.2, kRed);
        circle(p, QPointF(17, 9), 2.2, kBlue);
        circle(p, QPointF(10, 3), 1.8, kGreen);
    };
    t["Adjustments > Exposure"] = [](QPainter &p) {
        p.setPen(line(ink(), 1.0));
        p.setBrush(paper());
        p.drawRect(QRectF(1.5, 1.5, 17, 17));
        QPolygonF lower;
        lower << QPointF(18.5, 1.5) << QPointF(18.5, 18.5) << QPointF(1.5, 18.5);
        p.setBrush(QColor(40, 44, 52));
        p.drawPolygon(lower);
        p.setPen(line(QColor(40, 44, 52), 1.6));
        p.drawLine(QPointF(3.5, 6), QPointF(8.5, 6));
        p.drawLine(QPointF(6, 3.5), QPointF(6, 8.5));
        p.setPen(line(Qt::white, 1.6));
        p.drawLine(QPointF(11.5, 14), QPointF(16.5, 14));
    };
    t["Adjustments > Highlights / Shadows"] = [](QPainter &p) {
        circle(p, QPointF(6.5, 6.5), 4.5, kSun);
        QPainterPath moon;
        moon.addEllipse(QPointF(13.5, 13.5), 5, 5);
        QPainterPath bite;
        bite.addEllipse(QPointF(16, 11.5), 4, 4);
        p.setPen(line(ink(), 1.0));
        p.setBrush(QColor(70, 80, 120));
        p.drawPath(moon.subtracted(bite));
    };
    t["Adjustments > Temperature / Tint"] = [](QPainter &p) {
        QLinearGradient g(0, 2, 0, 15);
        g.setColorAt(0, kRed);
        g.setColorAt(1, kBlue);
        p.setPen(line(ink(), 1.0));
        p.setBrush(paper());
        p.drawRoundedRect(QRectF(7.5, 1.5, 5, 13), 2.5, 2.5);
        p.drawEllipse(QPointF(10, 15.5), 3.5, 3.5);
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawRect(QRectF(9, 5, 2, 10));
        p.setBrush(kBlue);
        p.drawEllipse(QPointF(10, 15.5), 2.3, 2.3);
        p.setPen(line(ink(), 1.0));
        for (int i = 0; i < 3; ++i) p.drawLine(QPointF(13.5, 4 + i * 3), QPointF(15.5, 4 + i * 3));
    };
    t["Adjustments > Invert Alpha"] = [](QPainter &p) {
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                p.fillRect(QRectF(1.5 + x * 4.25, 1.5 + y * 4.25, 4.25, 4.25),
                           (x + y) % 2 ? QColor(205, 205, 205) : QColor(255, 255, 255));
        QPolygonF solid;
        solid << QPointF(1.5, 1.5) << QPointF(18.5, 1.5) << QPointF(1.5, 18.5);
        p.setPen(Qt::NoPen);
        p.setBrush(kPurple);
        p.drawPolygon(solid);
        p.setPen(line(ink(), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(1.5, 1.5, 17, 17));
    };

    // ================= Effects =================
    t["Effects > Repeat last effect"] = [](QPainter &p) {
        arcArrow(p, QPointF(10, 10), 7, 100, -300, kPurple, 1.8);
        p.setPen(Qt::NoPen);
        p.setBrush(kPurple);
        p.drawEllipse(QPointF(10, 10), 2.2, 2.2);
    };
    // ---- Artistic ----
    t["Effects > Artistic > Ink Sketch"] = [](QPainter &p) {
        p.setPen(line(QColor(30, 34, 40), 1.0));
        for (int i = 0; i < 4; ++i) p.drawLine(QPointF(1.5, 17 - i * 2.5), QPointF(7 + i * 2, 18.5));
        QPolygonF nib;
        nib << QPointF(9, 13) << QPointF(16, 2) << QPointF(19, 5) << QPointF(10.5, 14.5);
        p.setPen(line(ink(), 1.0));
        p.setBrush(QColor(60, 66, 80));
        p.drawPolygon(nib);
        p.setBrush(kSun);
        QPolygonF tip;
        tip << QPointF(9, 13) << QPointF(10.5, 14.5) << QPointF(7.5, 16);
        p.drawPolygon(tip);
    };
    t["Effects > Artistic > Oil Painting"] = [](QPainter &p) {
        QPainterPath pal;
        pal.addEllipse(QRectF(1.5, 3, 17, 14));
        QPainterPath hole;
        hole.addEllipse(QPointF(6, 13), 2, 2);
        p.setPen(line(ink(), 1.0));
        p.setBrush(QColor(214, 170, 110));
        p.drawPath(pal.subtracted(hole));
        circle(p, QPointF(6, 7.5), 1.8, kRed, false);
        circle(p, QPointF(10.5, 6), 1.8, kSun, false);
        circle(p, QPointF(14.5, 8), 1.8, kBlue, false);
        circle(p, QPointF(13.5, 12.5), 1.8, kGreen, false);
    };
    t["Effects > Artistic > Pencil Sketch"] = [](QPainter &p) {
        p.setPen(line(kGray, 0.9));
        for (int i = 0; i < 5; ++i) p.drawLine(QPointF(1.5 + i * 2.2, 18.5), QPointF(5.5 + i * 2.2, 12));
        p.save();
        p.translate(12.5, 7.5);
        p.rotate(45);
        p.setPen(line(ink(), 1.0));
        p.setBrush(kSun);
        p.drawRect(QRectF(-2, -7, 4, 10));
        p.setBrush(QColor(240, 200, 160));
        QPolygonF tip;
        tip << QPointF(-2, 3) << QPointF(2, 3) << QPointF(0, 7);
        p.drawPolygon(tip);
        p.restore();
    };
    // ---- Blurs ----
    t["Effects > Blurs > Fragment"] = [](QPainter &p) {
        const QPointF offs[] = {{-2.5, -2.5}, {2.5, -2.5}, {-2.5, 2.5}, {2.5, 2.5}};
        for (const QPointF &o : offs) {
            QColor c = kBlue;
            c.setAlpha(110);
            circle(p, QPointF(10, 10) + o, 5.5, c, false);
        }
    };
    t["Effects > Blurs > Gaussian Blur"] = [](QPainter &p) {
        QRadialGradient g(QPointF(10, 10), 9);
        g.setColorAt(0, kBlue);
        g.setColorAt(0.45, QColor(kBlue.red(), kBlue.green(), kBlue.blue(), 200));
        g.setColorAt(1, QColor(kBlue.red(), kBlue.green(), kBlue.blue(), 0));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(QPointF(10, 10), 9, 9);
    };
    t["Effects > Blurs > Motion Blur"] = [](QPainter &p) {
        for (int i = 0; i < 4; ++i) {
            QColor c = kBlue;
            c.setAlpha(50 + i * 40);
            circle(p, QPointF(5 + i * 2.5, 10), 5, c, false);
        }
        circle(p, QPointF(13.5, 10), 5, kBlue);
    };
    t["Effects > Blurs > Radial Blur"] = [](QPainter &p) {
        for (int i = 0; i < 3; ++i) {
            QColor c = kBlue;
            c.setAlpha(90 + i * 60);
            p.setPen(line(c, 1.8));
            p.setBrush(Qt::NoBrush);
            p.drawArc(QRectF(2 + i * 2.5, 2 + i * 2.5, 16 - i * 5, 16 - i * 5), (30 + i * 40) * 16, 150 * 16);
            p.drawArc(QRectF(2 + i * 2.5, 2 + i * 2.5, 16 - i * 5, 16 - i * 5), (210 + i * 40) * 16, 150 * 16);
        }
        circle(p, QPointF(10, 10), 1.6, kBlue, false);
    };
    t["Effects > Blurs > Surface Blur"] = [](QPainter &p) {
        p.setPen(line(ink(), 1.0));
        p.setBrush(kOrange);
        p.drawRect(QRectF(1.5, 1.5, 8.5, 17));
        p.setBrush(kBlue);
        p.drawRect(QRectF(10, 1.5, 8.5, 17));
        QRadialGradient soft(QPointF(5.7, 10), 4);
        soft.setColorAt(0, QColor(255, 255, 255, 150));
        soft.setColorAt(1, QColor(255, 255, 255, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(soft);
        p.drawEllipse(QPointF(5.7, 10), 4, 4);
        soft.setCenter(QPointF(14.3, 10));
        soft.setFocalPoint(QPointF(14.3, 10));
        p.setBrush(soft);
        p.drawEllipse(QPointF(14.3, 10), 4, 4);
    };
    t["Effects > Blurs > Unfocus"] = [](QPainter &p) {
        const QColor cs[] = {QColor(250, 200, 60, 150), QColor(112, 172, 232, 150), QColor(220, 90, 140, 150)};
        const QPointF ps[] = {{6.5, 7}, {13.5, 8}, {9.5, 14}};
        for (int i = 0; i < 3; ++i) circle(p, ps[i], 4.6, cs[i], false);
    };
    t["Effects > Blurs > Zoom Blur"] = [](QPainter &p) {
        for (int i = 0; i < 12; ++i) {
            const qreal a = qDegreesToRadians(i * 30.0);
            QColor c = kBlue;
            c.setAlpha(170);
            p.setPen(line(c, 1.2));
            p.drawLine(QPointF(10 + 3 * std::cos(a), 10 + 3 * std::sin(a)),
                       QPointF(10 + 9 * std::cos(a), 10 + 9 * std::sin(a)));
        }
        circle(p, QPointF(10, 10), 2.5, kBlue);
    };
    // ---- Distort ----
    auto grid = [](QPainter &p, const std::function<QPointF(qreal, qreal)> &warp) {
        p.setPen(line(kBlue, 1.0));
        p.setBrush(Qt::NoBrush);
        for (int i = 0; i <= 4; ++i) {
            QPainterPath h, v;
            for (int j = 0; j <= 16; ++j) {
                const QPointF a = warp(j / 16.0, i / 4.0), b = warp(i / 4.0, j / 16.0);
                if (j == 0) { h.moveTo(a); v.moveTo(b); }
                else { h.lineTo(a); v.lineTo(b); }
            }
            p.drawPath(h);
            p.drawPath(v);
        }
    };
    t["Effects > Distort > Bulge"] = [grid](QPainter &p) {
        grid(p, [](qreal u, qreal v) {
            const qreal x = u * 2 - 1, y = v * 2 - 1;
            const qreal r = std::sqrt(x * x + y * y);
            const qreal k = r > 0 ? std::pow(r / 1.42, 0.6) * 1.42 / r : 1;
            return QPointF(10 + x * k * 8.5 * 0.92, 10 + y * k * 8.5 * 0.92);
        });
    };
    t["Effects > Distort > Crystalize"] = [](QPainter &p) {
        const QPolygonF cells[] = {
            QPolygonF({QPointF(1.5, 1.5), QPointF(9, 1.5), QPointF(7.5, 8), QPointF(1.5, 9)}),
            QPolygonF({QPointF(9, 1.5), QPointF(18.5, 1.5), QPointF(18.5, 7), QPointF(12, 9.5), QPointF(7.5, 8)}),
            QPolygonF({QPointF(1.5, 9), QPointF(7.5, 8), QPointF(12, 9.5), QPointF(10, 18.5), QPointF(1.5, 18.5)}),
            QPolygonF({QPointF(12, 9.5), QPointF(18.5, 7), QPointF(18.5, 18.5), QPointF(10, 18.5)})};
        const QColor cs[] = {QColor(120, 180, 240), QColor(80, 140, 220), QColor(150, 205, 245), QColor(60, 110, 200)};
        p.setPen(line(ink(), 0.9));
        for (int i = 0; i < 4; ++i) {
            p.setBrush(cs[i]);
            p.drawPolygon(cells[i]);
        }
    };
    t["Effects > Distort > Dents"] = [grid](QPainter &p) {
        grid(p, [](qreal u, qreal v) {
            return QPointF(2 + u * 16 + std::sin(v * 9) * 1.2, 2 + v * 16 + std::sin(u * 9) * 1.2);
        });
    };
    t["Effects > Distort > Frosted Glass"] = [](QPainter &p) {
        p.setPen(line(ink(), 1.0));
        p.setBrush(QColor(170, 210, 240));
        p.drawRect(QRectF(1.5, 1.5, 17, 17));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 210));
        const QPointF dots[] = {{4, 4}, {9, 3.5}, {15, 5}, {6, 8}, {12, 9}, {16.5, 11}, {3.5, 13},
                                {8.5, 14}, {13.5, 15.5}, {5.5, 17}, {17, 17}, {10.5, 6.5}};
        for (const QPointF &d : dots) p.drawEllipse(d, 1.1, 1.1);
    };
    t["Effects > Distort > Pixelate"] = [](QPainter &p) {
        const QColor cs[] = {QColor(64, 120, 210), QColor(120, 176, 236), QColor(250, 198, 58), QColor(88, 168, 82)};
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                p.fillRect(QRectF(1.5 + x * 4.25, 1.5 + y * 4.25, 4.25, 4.25), cs[(x * 3 + y * 2 + x * y) % 4]);
        p.setPen(line(ink(), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(1.5, 1.5, 17, 17));
    };
    t["Effects > Distort > Polar Inversion"] = [](QPainter &p) {
        p.setPen(line(kBlue, 1.0));
        p.setBrush(Qt::NoBrush);
        for (int i = 1; i <= 3; ++i) p.drawEllipse(QPointF(10, 10), i * 2.8, i * 2.8);
        for (int i = 0; i < 8; ++i) {
            const qreal a = qDegreesToRadians(i * 45.0);
            p.drawLine(QPointF(10, 10), QPointF(10 + 8.4 * std::cos(a), 10 + 8.4 * std::sin(a)));
        }
        circle(p, QPointF(10, 10), 1.5, kOrange, false);
    };
    t["Effects > Distort > Tile Reflection"] = [](QPainter &p) {
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x) {
                const QRectF r(1.5 + x * 8.5, 1.5 + y * 8.5, 8.5, 8.5);
                QLinearGradient g(r.topLeft(), r.bottomRight());
                g.setColorAt(0, QColor(190, 222, 250));
                g.setColorAt(1, QColor(60, 118, 206));
                p.setPen(line(ink(), 1.0));
                p.setBrush(g);
                p.drawRect(r);
            }
    };
    t["Effects > Distort > Twist"] = [](QPainter &p) {
        QPainterPath spiral;
        for (int i = 0; i <= 80; ++i) {
            const qreal t = i / 80.0;
            const qreal a = t * 4 * M_PI;
            const QPointF pt(10 + 8.5 * t * std::cos(a), 10 + 8.5 * t * std::sin(a));
            if (i == 0) spiral.moveTo(pt);
            else spiral.lineTo(pt);
        }
        p.setPen(line(kBlue, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawPath(spiral);
    };
    // ---- Noise ----
    t["Effects > Noise > Add Noise"] = [](QPainter &p) {
        sample(p, QRectF(1.5, 1.5, 17, 17));
        const QColor cs[] = {kRed, kBlue, kGreen, QColor(40, 44, 52)};
        int k = 7;
        for (int i = 0; i < 26; ++i) {
            k = (k * 37 + 11) % 97;
            const QPointF pt(3 + (k % 15), 3 + ((k * 7) % 15));
            p.fillRect(QRectF(pt, QSizeF(1.4, 1.4)), cs[i % 4]);
        }
    };
    t["Effects > Noise > Median"] = [](QPainter &p) {
        const qreal hs[] = {6, 13, 9, 16, 4};
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 5; ++i) {
            p.setBrush(i == 2 ? kOrange : kGray);
            p.drawRect(QRectF(1.5 + i * 3.5, 18.5 - hs[i], 2.8, hs[i]));
        }
        p.setPen(line(kOrange, 1.2));
        p.drawLine(QPointF(1, 9.5), QPointF(19, 9.5));
    };
    t["Effects > Noise > Reduce Palette"] = [](QPainter &p) {
        const QColor cs[] = {kRed, kSun, kGreen, kBlue};
        p.setPen(line(ink(), 1.0));
        for (int i = 0; i < 4; ++i) {
            p.setBrush(cs[i]);
            p.drawRoundedRect(QRectF(1.5 + (i % 2) * 9, 1.5 + (i / 2) * 9, 8, 8), 1.5, 1.5);
        }
    };
    t["Effects > Noise > Reduce Noise"] = [](QPainter &p) {
        sample(p, QRectF(1.5, 1.5, 17, 17));
        const QPointF dots[] = {{4, 4}, {8, 6}, {5, 10}, {11, 3.5}, {3.5, 15}};
        p.setPen(Qt::NoPen);
        p.setBrush(kGray);
        for (const QPointF &d : dots) p.drawRect(QRectF(d, QSizeF(1.5, 1.5)));
        QLinearGradient g(8, 8, 18.5, 18.5);
        g.setColorAt(0, QColor(112, 172, 232, 0));
        g.setColorAt(1, kSky);
        p.setBrush(g);
        p.drawRect(QRectF(8, 8, 10.5, 10.5));
    };
    // ---- Object ----
    t["Effects > Object > Drop Shadow"] = [](QPainter &p) {
        QRadialGradient sh(QPointF(12, 12), 8);
        sh.setColorAt(0, QColor(0, 0, 0, 120));
        sh.setColorAt(1, QColor(0, 0, 0, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(sh);
        p.drawRoundedRect(QRectF(4.5, 4.5, 15, 15), 3, 3);
        p.setPen(line(ink(), 1.0));
        p.setBrush(kOrange);
        p.drawRoundedRect(QRectF(1.5, 1.5, 12, 12), 2, 2);
    };
    // ---- Photo ----
    t["Effects > Photo > Glow"] = [](QPainter &p) {
        QRadialGradient g(QPointF(10, 10), 9.5);
        g.setColorAt(0, QColor(255, 250, 200));
        g.setColorAt(0.4, QColor(255, 220, 90, 200));
        g.setColorAt(1, QColor(255, 200, 60, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(QPointF(10, 10), 9.5, 9.5);
        circle(p, QPointF(10, 10), 3, QColor(255, 236, 140), false);
    };
    t["Effects > Photo > Red Eye Removal"] = [](QPainter &p) {
        QPainterPath eye;
        eye.moveTo(1.5, 10);
        eye.quadTo(10, 2.5, 18.5, 10);
        eye.quadTo(10, 17.5, 1.5, 10);
        p.setPen(line(ink(), 1.2));
        p.setBrush(paper());
        p.drawPath(eye);
        circle(p, QPointF(10, 10), 3.8, QColor(110, 80, 50), false);
        circle(p, QPointF(10, 10), 2, kRed, false);
        p.setPen(line(kGreen, 1.6));
        p.drawLine(QPointF(12.5, 16.5), QPointF(14.5, 18.5));
        p.drawLine(QPointF(14.5, 18.5), QPointF(19, 13.5));
    };
    t["Effects > Photo > Sharpen"] = [](QPainter &p) {
        QPolygonF tri;
        tri << QPointF(10, 1.5) << QPointF(18.5, 17.5) << QPointF(1.5, 17.5);
        p.setPen(line(ink(), 1.2));
        p.setBrush(kBlue);
        p.drawPolygon(tri);
        QPolygonF light;
        light << QPointF(10, 1.5) << QPointF(10, 17.5) << QPointF(1.5, 17.5);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 110));
        p.drawPolygon(light);
    };
    t["Effects > Photo > Soften Portrait"] = [](QPainter &p) {
        QRadialGradient g(QPointF(10, 10), 10);
        g.setColorAt(0, QColor(255, 220, 200));
        g.setColorAt(1, QColor(255, 220, 200, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(QPointF(10, 10), 10, 10);
        p.setPen(line(ink(), 1.0));
        p.setBrush(QColor(236, 186, 150));
        p.drawEllipse(QPointF(10, 7.5), 3.5, 4);
        QPainterPath body;
        body.moveTo(3.5, 19);
        body.quadTo(4, 12.5, 10, 12.5);
        body.quadTo(16, 12.5, 16.5, 19);
        p.setBrush(QColor(200, 120, 150));
        p.drawPath(body);
    };
    t["Effects > Photo > Vignette"] = [](QPainter &p) {
        picture(p, QRectF(1.5, 2.5, 17, 15));
        QRadialGradient g(QPointF(10, 10), 11);
        g.setColorAt(0.45, QColor(0, 0, 0, 0));
        g.setColorAt(1, QColor(0, 0, 0, 200));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawRect(QRectF(1.5, 2.5, 17, 15));
    };
    // ---- Render ----
    t["Effects > Render > Clouds"] = [](QPainter &p) {
        QPainterPath cloud;
        cloud.addEllipse(QPointF(7, 11.5), 4.5, 4);
        cloud.addEllipse(QPointF(12, 9), 5, 5);
        cloud.addEllipse(QPointF(15.5, 12.5), 3.5, 3.2);
        cloud.addRect(QRectF(5, 12, 12, 3.6));
        cloud = cloud.simplified();
        p.setPen(line(ink(), 1.0));
        p.setBrush(QColor(236, 242, 250));
        p.drawPath(cloud);
    };
    t["Effects > Render > Julia Fractal"] = [](QPainter &p) {
        for (int i = 0; i < 40; ++i) {
            const qreal t = i / 40.0;
            const qreal a = t * 5 * M_PI;
            const qreal r = 1 + t * 7;
            QColor c = QColor::fromHsvF(0.55 + t * 0.35, 0.7, 0.95);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawEllipse(QPointF(10 + r * std::cos(a), 10 + r * std::sin(a) * 0.8), 1.1 - t * 0.5, 1.1 - t * 0.5);
            p.drawEllipse(QPointF(10 - r * std::cos(a), 10 - r * std::sin(a) * 0.8), 1.1 - t * 0.5, 1.1 - t * 0.5);
        }
    };
    t["Effects > Render > Mandelbrot Fractal"] = [](QPainter &p) {
        // The main cardioid (c = e^it / 2 - e^2it / 4) and the bulb at -1.
        auto map = [](qreal x, qreal y) { return QPointF(1.75 + (x + 1.25) * 11.0, 10 + y * 11.0); };
        QPainterPath set;
        for (int i = 0; i <= 96; ++i) {
            const qreal t = i / 96.0 * 2 * M_PI;
            const QPointF pt = map(std::cos(t) / 2 - std::cos(2 * t) / 4, std::sin(t) / 2 - std::sin(2 * t) / 4);
            if (i == 0) set.moveTo(pt);
            else set.lineTo(pt);
        }
        set.closeSubpath();
        set.addEllipse(map(-1, 0), 0.25 * 11, 0.25 * 11);
        set.addEllipse(map(-0.12, 0.74), 0.09 * 11, 0.09 * 11);
        set.addEllipse(map(-0.12, -0.74), 0.09 * 11, 0.09 * 11);
        set.setFillRule(Qt::WindingFill);
        QRadialGradient glow(map(-0.4, 0), 9);
        glow.setColorAt(0, QColor(250, 170, 60, 160));
        glow.setColorAt(1, QColor(250, 170, 60, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(map(-0.4, 0), 9.5, 9.5);
        p.setPen(line(kOrange, 0.8));
        p.setBrush(QColor(40, 30, 90));
        p.drawPath(set.simplified());
    };
    t["Effects > Render > Turbulence"] = [](QPainter &p) {
        const QColor cs[] = {kBlue, kTeal, kPurple};
        for (int k = 0; k < 3; ++k) {
            QPainterPath wave;
            for (int i = 0; i <= 24; ++i) {
                const qreal x = 1.5 + i * (17.0 / 24);
                const qreal y = 5 + k * 5 + std::sin(i * 0.7 + k * 1.3) * 2;
                if (i == 0) wave.moveTo(x, y);
                else wave.lineTo(x, y);
            }
            p.setPen(line(cs[k], 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawPath(wave);
        }
    };
    // ---- Stylize ----
    t["Effects > Stylize > Edge Detect"] = [](QPainter &p) {
        p.fillRect(QRectF(1.5, 1.5, 17, 17), QColor(40, 44, 52));
        p.setPen(line(QColor(255, 255, 255), 1.3));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(8, 9), 4.5, 4.5);
        p.drawRect(QRectF(10.5, 10.5, 6, 6));
    };
    t["Effects > Stylize > Emboss"] = [](QPainter &p) {
        p.fillRect(QRectF(1.5, 1.5, 17, 17), QColor(150, 152, 156));
        QPolygonF hi, lo;
        hi << QPointF(4, 16) << QPointF(4, 4) << QPointF(16, 4);
        lo << QPointF(16, 4) << QPointF(16, 16) << QPointF(4, 16);
        p.setPen(line(QColor(235, 236, 238), 2));
        p.drawPolyline(hi);
        p.setPen(line(QColor(70, 72, 76), 2));
        p.drawPolyline(lo);
    };
    t["Effects > Stylize > Morphology"] = [](QPainter &p) {
        circle(p, QPointF(10, 10), 7.5, QColor(150, 186, 236));
        circle(p, QPointF(10, 10), 3.5, kBlue);
        arrow(p, QPointF(12.2, 7.8), QPointF(16.5, 3.5), kOrange, 1.3, 2.4);
        arrow(p, QPointF(7.8, 12.2), QPointF(3.5, 16.5), kOrange, 1.3, 2.4);
    };
    t["Effects > Stylize > Outline"] = [](QPainter &p) {
        QPainterPath star;
        for (int i = 0; i < 10; ++i) {
            const qreal r = (i % 2) ? 3.6 : 8.5;
            const qreal a = qDegreesToRadians(i * 36.0 - 90);
            const QPointF pt(10 + r * std::cos(a), 10.6 + r * std::sin(a));
            if (i == 0) star.moveTo(pt);
            else star.lineTo(pt);
        }
        star.closeSubpath();
        p.setPen(line(kPurple, 2.6));
        p.setBrush(Qt::NoBrush);
        p.drawPath(star);
        p.setPen(line(Qt::white, 0.9));
        p.drawPath(star);
    };
    t["Effects > Stylize > Relief"] = [](QPainter &p) {
        picture(p, QRectF(1.5, 2.5, 17, 15));
        p.setPen(line(QColor(255, 255, 255, 220), 1.4));
        p.drawLine(QPointF(2.5, 16.5), QPointF(2.5, 3.5));
        p.drawLine(QPointF(2.5, 3.5), QPointF(17.5, 3.5));
        p.setPen(line(QColor(0, 0, 0, 160), 1.4));
        p.drawLine(QPointF(17.5, 3.5), QPointF(17.5, 16.5));
        p.drawLine(QPointF(17.5, 16.5), QPointF(2.5, 16.5));
    };
    return t;
}

}  // namespace

namespace CommandIcons {

QString plainLabel(const QString &menuText) {
    QString text = menuText;
    text.replace(QLatin1String("&&"), QString(QChar(0x1F)));
    text.remove(QLatin1Char('&'));
    text.replace(QChar(0x1F), QLatin1Char('&'));
    text.remove(QLatin1String("..."));
    text.remove(QChar(0x2026));
    return text.trimmed();
}

QIcon forPath(const QString &path) {
    if (path == QLatin1String("Image > Crop to Selection")) return ToolIcons::cropAction();
    const auto it = table().constFind(path);
    return it == table().constEnd() ? QIcon() : makeIcon(it.value());
}

void decorate(const QList<QMenu *> &menus) {
    std::function<void(QMenu *, const QString &)> walk = [&](QMenu *menu, const QString &path) {
        for (QAction *a : menu->actions()) {
            if (a->isSeparator()) continue;
            const QString here = path + QStringLiteral(" > ") + plainLabel(a->text());
            if (QMenu *sub = a->menu()) {
                walk(sub, here);
                continue;
            }
            const QIcon icon = forPath(here);
            if (!icon.isNull()) a->setIcon(icon);
        }
    };
    for (QMenu *menu : menus)
        if (menu) walk(menu, plainLabel(menu->title()));
}

QStringList paths() {
    QStringList out = table().keys();
    out << QStringLiteral("Image > Crop to Selection");
    out.sort();
    return out;
}

}  // namespace CommandIcons
