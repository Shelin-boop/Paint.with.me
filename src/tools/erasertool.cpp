#include "erasertool.h"
#include "canvas/canvaswidget.h"
#include "core/document.h"
#include "core/layer.h"
#include <QPainter>
#include <cmath>

void EraserTool::mousePressEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) {
    if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton) return;
    if (m_drawing) return;   // a 2nd button mid-stroke must not restart it (issue #17)
    auto *doc = canvas.document();
    auto *layer = doc->activeLayer();
    if (!layer || layer->isLocked()) return;

    m_toSecondary = (event->button() == Qt::RightButton);
    m_drawing = true;
    m_lastPos = canvasPos;
    m_currentPos = canvasPos;
    m_beforeImage = layer->image().copy();
    m_strokeBuffer = transparentImage(layer->image().size());
    m_dirtyRect = QRect();
    m_strokePeak = strokeUsesPeakCoverage();
    m_lastPressure = m_pressure;
    eraseDab(dabAt(canvasPos, m_pressure));
    compositeErase(doc, layer, canvas);
}

void EraserTool::mouseMoveEvent(const QPointF &canvasPos, QMouseEvent *, CanvasWidget &canvas) {
    m_currentPos = canvasPos;
    if (!m_drawing) return;
    auto *doc = canvas.document();
    auto *layer = doc->activeLayer();
    if (!layer) return;
    eraseStroke(m_lastPos, canvasPos);
    m_lastPos = canvasPos;
    compositeErase(doc, layer, canvas);
}

void EraserTool::mouseReleaseEvent(const QPointF &, QMouseEvent *event, CanvasWidget &canvas) {
    if (!m_drawing) return;
    if (event->buttons() & (Qt::LeftButton | Qt::RightButton)) return;   // keep erasing while a button is held (issue #17)
    m_drawing = false;
    auto *layer = canvas.document()->activeLayer();
    if (layer && layer->image() != m_beforeImage)   // skip no-op strokes (no empty undo step)
        canvas.document()->pushImageEdit(canvas.document()->activeLayerIndex(), m_beforeImage, "Eraser");
    m_strokeBuffer = QImage();
    m_beforeImage = QImage();
}

void EraserTool::eraseStroke(const QPointF &from, const QPointF &to) {
    forEachDab(from, to, m_lastPressure, m_pressure, [&](const Dab &dab) { eraseDab(dab); });
    m_lastPressure = m_pressure;
}

void EraserTool::eraseDab(const Dab &dab) {
    // Like the brush: the dab's alpha is the erase coverage (hardness falloff
    // included); opacity is applied when the buffer is composited. Pressure
    // sets the dab's size (Paint.NET), opacity or both.
    m_dirtyRect = m_dirtyRect.united(paintDab(m_strokeBuffer, dab, QColor(0, 0, 0), m_hardness, 0,
                                              m_antialiased, m_strokePeak, m_dabScratch));
}

void EraserTool::compositeErase(Document *doc, Layer *layer, CanvasWidget &canvas) {
    // Only the dabs added since the last call: restore that area from the
    // stroke's start and erase the accumulated coverage there. (Redoing the
    // whole layer on every mouse move made big images unusable.) The stroke
    // buffer is in document pixels, like the dab positions.
    const QRect dirty = m_dirtyRect;
    m_dirtyRect = QRect();
    if (dirty.isEmpty()) return;
    const QRect layerArea = dirty.translated(-layer->offset()).intersected(layer->image().rect());
    QPainter painter(&layer->image());
    if (!layerArea.isEmpty()) {
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(layerArea, m_beforeImage, layerArea);
    }
    clipToSelection(painter, doc);
    // Both buttons erase to transparency; the right button scales the erase by
    // the secondary colour's alpha, matching Paint.NET (left uses the primary
    // colour's alpha — full here, since the eraser ignores primary RGB).
    double strength = (m_opacity / 100.0);
    if (m_toSecondary)
        strength *= doc->secondaryColor().alphaF();
    painter.setOpacity(strength);
    // DestinationOut subtracts (strokeCoverage * strength) of alpha.
    painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    painter.drawImage(dirty, m_strokeBuffer, dirty);
    painter.end();
    canvas.updateCanvasRegion(dirty);
}

void EraserTool::drawOverlay(QPainter &painter, const CanvasWidget &canvas) {
    QPointF widgetPos = canvas.canvasToWidget(m_currentPos);
    double radius = m_brushSize / 2.0 * canvas.zoom();
    painter.setPen(QPen(Qt::gray, 1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(widgetPos, radius, radius);
}
