#include "perspectivetool.h"

#include "canvas/canvaswidget.h"
#include "core/document.h"
#include "core/perspective.h"

#include <QMouseEvent>
#include <QPainter>

void PerspectiveTool::mousePressEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) {
    Document *doc = canvas.document();
    if (!doc) return;
    QVector<QPointF> &points = doc->vanishingPoints();
    const int near = Perspective::pointNear(points, canvasPos, kGrabRadius / canvas.zoom());
    if (event->button() == Qt::RightButton) {
        if (near >= 0) points.remove(near);
        m_hover = -1;
        canvas.update();
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    if (near >= 0) {
        m_drag = near;
    } else if (points.size() < Perspective::kMaxPoints) {
        points.append(canvasPos);
        m_drag = int(points.size()) - 1;
    }
    canvas.update();
}

void PerspectiveTool::mouseMoveEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) {
    Document *doc = canvas.document();
    if (!doc) return;
    QVector<QPointF> &points = doc->vanishingPoints();
    if (m_drag >= 0 && m_drag < points.size() && (event->buttons() & Qt::LeftButton)) {
        points[m_drag] = canvasPos;
        canvas.update();
        return;
    }
    const int hover = Perspective::pointNear(points, canvasPos, kGrabRadius / canvas.zoom());
    if (hover != m_hover) {
        m_hover = hover;
        canvas.update();
    }
}

void PerspectiveTool::mouseReleaseEvent(const QPointF &, QMouseEvent *, CanvasWidget &canvas) {
    m_drag = -1;
    canvas.update();
}

void PerspectiveTool::drawOverlay(QPainter &painter, const CanvasWidget &canvas) {
    const Document *doc = canvas.document();
    if (!doc) return;
    const QVector<QPointF> &points = doc->vanishingPoints();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont font = painter.font();
    font.setPixelSize(10);
    font.setBold(true);
    painter.setFont(font);
    for (int i = 0; i < points.size(); ++i) {
        const QPointF w = canvas.canvasToWidget(points[i]);
        const double r = (i == m_hover || i == m_drag) ? 9 : 7;
        painter.setPen(QPen(Qt::white, 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(w, r, r);
        painter.setPen(QPen(Perspective::color(i), 2));
        painter.drawEllipse(w, r, r);
        painter.drawLine(w - QPointF(r + 4, 0), w + QPointF(r + 4, 0));
        painter.drawLine(w - QPointF(0, r + 4), w + QPointF(0, r + 4));
        const QRectF label(w + QPointF(r + 3, -r - 14), QSizeF(14, 14));
        painter.setPen(Qt::NoPen);
        painter.setBrush(Perspective::color(i));
        painter.drawRoundedRect(label, 3, 3);
        painter.setPen(Qt::white);
        painter.drawText(label, Qt::AlignCenter, QString::number(i + 1));
    }
    painter.restore();
}
