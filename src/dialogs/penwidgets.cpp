#include "dialogs/penwidgets.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTabletEvent>

PressureCurveView::PressureCurveView(QWidget *parent) : QWidget(parent) {
    setObjectName("pressureCurveView");
    setMinimumSize(80, 80);
    setToolTip(QStringLiteral("Across: how hard you press. Up: how strongly it paints."));
}

void PressureCurveView::setCurve(int curve) {
    if (m_curve == curve) return;
    m_curve = curve;
    update();
}

void PressureCurveView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect()).adjusted(4.5, 4.5, -4.5, -4.5);
    p.fillRect(box, palette().base());
    p.setPen(QPen(palette().mid().color(), 1));
    p.drawRect(box);
    // The straight line: the pen's own pressure.
    p.setPen(QPen(palette().mid().color(), 1, Qt::DashLine));
    p.drawLine(box.bottomLeft(), box.topRight());
    QPainterPath curve;
    for (int i = 0; i <= 64; ++i) {
        const double x = i / 64.0;
        const QPointF pt(box.left() + x * box.width(),
                         box.bottom() - Pen::mapPressure(x, m_curve) * box.height());
        if (i == 0) curve.moveTo(pt);
        else curve.lineTo(pt);
    }
    p.setPen(QPen(palette().highlight().color(), 2));
    p.drawPath(curve);
}

PenTestPad::PenTestPad(QWidget *parent) : QWidget(parent) {
    setObjectName("penTestPad");
    setMinimumHeight(90);
    setAttribute(Qt::WA_TabletTracking);
    setCursor(Qt::CrossCursor);
    setToolTip(QStringLiteral("Draw here with your pen to try the settings."));
}

void PenTestPad::clear() {
    m_strokes.clear();
    update();
}

void PenTestPad::addPoint(const QPointF &pos, double pressure, const Pen::TipShape &tip,
                          bool startsStroke) {
    if (startsStroke || m_strokes.isEmpty()) m_strokes.append(QVector<Point>());
    m_strokes.last().append(Point{pos, pressure, tip});
    update();
}

void PenTestPad::tabletEvent(QTabletEvent *event) {
    const double raw = event->pressure();
    const double mapped = Pen::mapPressure(raw);
    const QPointF tilt(event->xTilt(), event->yTilt());
    if (onPenReading) onPenReading(raw, mapped, tilt);
    const Pen::TipShape tip = Pen::settings().tiltFlattens ? Pen::tipForTilt(tilt) : Pen::TipShape();
    if (event->type() == QEvent::TabletPress) {
        m_penDown = true;
        addPoint(event->position(), mapped, tip, true);
    } else if (event->type() == QEvent::TabletMove && m_penDown && raw > 0.0) {
        addPoint(event->position(), mapped, tip, false);
    } else if (event->type() == QEvent::TabletRelease) {
        m_penDown = false;
    }
    event->accept();   // the pen draws here itself; no mouse events from it
}

void PenTestPad::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    m_mouseDown = true;
    addPoint(event->position(), 1.0, Pen::TipShape(), true);
}

void PenTestPad::mouseMoveEvent(QMouseEvent *event) {
    if (m_mouseDown) addPoint(event->position(), 1.0, Pen::TipShape(), false);
}

void PenTestPad::mouseReleaseEvent(QMouseEvent *) {
    m_mouseDown = false;
}

void PenTestPad::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::white);
    p.setPen(QPen(QColor(0, 0, 0, 60), 1));
    p.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
    if (m_strokes.isEmpty()) {
        p.setPen(QColor(120, 120, 120));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Try your pen here"));
        return;
    }
    // A row of dabs, like the brush: 16 px wide at full pressure.
    constexpr double kRadius = 8.0;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 20, 20));
    for (const auto &stroke : m_strokes) {
        for (int i = 0; i < stroke.size(); ++i) {
            const Point &a = stroke[i];
            const Point &b = i + 1 < stroke.size() ? stroke[i + 1] : a;
            const double dist = QLineF(a.pos, b.pos).length();
            const int steps = qMax(1, int(dist / 1.5));
            for (int s = 0; s < steps; ++s) {
                const double t = double(s) / steps;
                const QPointF pos = a.pos + (b.pos - a.pos) * t;
                const double r = kRadius * (a.pressure + (b.pressure - a.pressure) * t);
                if (r < 0.3) continue;
                p.save();
                p.translate(pos);
                p.rotate(a.tip.angle);
                p.scale(1.0, a.tip.roundness);
                p.drawEllipse(QPointF(), r, r);
                p.restore();
            }
        }
    }
}
