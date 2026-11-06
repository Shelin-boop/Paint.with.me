#include "zoomtool.h"
#include "../canvas/canvaswidget.h"
#include <QMouseEvent>
#include <QCursor>
#include <QPixmap>
#include <QPainter>
#include <algorithm>

QCursor ZoomTool::cursor() const {
    // Magnifier cursor: draw a 24x24 magnifier icon
    QPixmap pm(24, 24);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Qt::black, 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(3, 3, 13, 13);
    p.drawLine(14, 14, 20, 20);
    p.setPen(QPen(QColor(80, 80, 80), 1.2));
    p.drawLine(8, 9, 11, 9);
    p.drawLine(9, 6, 9, 12);
    p.end();
    return QCursor(pm, 9, 9);
}

void ZoomTool::mousePressEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) {
    Q_UNUSED(canvas);
    if (event->button() == Qt::LeftButton) {
        // A left press may become a zoom-to-rectangle drag; decide on release.
        m_dragging = true;
        m_startCanvas = canvasPos;
        m_currentCanvas = canvasPos;
    }
}

void ZoomTool::mouseMoveEvent(const QPointF &canvasPos, QMouseEvent *, CanvasWidget &canvas) {
    if (!m_dragging) return;
    m_currentCanvas = canvasPos;
    canvas.update();
}

void ZoomTool::mouseReleaseEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) {
    const bool wasDragging = m_dragging;
    m_dragging = false;

    // A dragged rectangle (in widget pixels) big enough to mean "zoom here"
    // maximises that region, like Paint.NET. Anything smaller is a plain click.
    if (wasDragging && event->button() == Qt::LeftButton) {
        const QRectF rectCanvas = QRectF(m_startCanvas, canvasPos).normalized();
        if (rectCanvas.width() * canvas.zoom() > 8 && rectCanvas.height() * canvas.zoom() > 8) {
            canvas.zoomToRect(rectCanvas.toAlignedRect());   // fills the view, turned or not
            return;
        }
    }

    // Plain click: left zooms in, right zooms out, point under cursor fixed.
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
        const double factor = (event->button() == Qt::LeftButton) ? 1.5 : (1.0 / 1.5);
        canvas.zoomAt(event->position(), canvas.zoom() * factor);
    }
}

void ZoomTool::drawOverlay(QPainter &painter, const CanvasWidget &canvas) {
    if (!m_dragging) return;
    const QRectF box = QRectF(m_startCanvas, m_currentCanvas).normalized();
    if (box.width() * canvas.zoom() < 2 && box.height() * canvas.zoom() < 2) return;
    painter.setPen(QPen(QColor(0, 120, 215), 1, Qt::DashLine));
    painter.setBrush(QColor(0, 120, 215, 40));
    painter.drawPolygon(canvas.canvasTransform().map(QPolygonF(box)));   // turns with the view
}
