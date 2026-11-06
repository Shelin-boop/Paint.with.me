#pragma once
#include "tool.h"
#include <QImage>
#include <QVector>

class BrushTool : public Tool {
public:
    ToolType type() const override { return ToolType::Brush; }
    QString name() const override { return "Paintbrush"; }
    bool changesLayerPixels(const Document *) const override { return true; }
    QCursor cursor() const override { return Qt::CrossCursor; }

    void mousePressEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) override;
    void mouseMoveEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) override;
    void mouseReleaseEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) override;
    void drawOverlay(QPainter &painter, const CanvasWidget &canvas) override;
    bool updatesCanvasRegion() const override { return true; }

private:
    void drawBrushStroke(const QPointF &from, const QPointF &to, const QColor &color);
    void drawBrushDab(const Dab &dab, const QColor &color);
    QRect compositeStroke(class Document *doc, class Layer *layer, const QColor &color);
    void compositeOverwrite(class Document *doc, class Layer *layer, const QColor &color, const QRect &dirty);
    QPainter::CompositionMode brushCompositionMode() const;

    // Switches the current segment's colour mid-stroke (a different button became
    // active) without breaking the line: bakes the drawn coverage into the base
    // and starts a fresh buffer in the new colour.
    void switchColour(class Document *doc, class Layer *layer, const QColor &color);

    bool m_drawing = false;
    QPointF m_lastPos;
    QImage m_beforeImage;   // layer pixels at stroke start (for one undo entry)
    QImage m_baseImage;     // running base the current-colour segment composites onto
    QImage m_strokeBuffer;  // accumulates the current segment's coverage
    QRect m_dirtyRect;       // only dabs added since the previous composite
    QRect m_segmentBounds;   // all coverage in this colour segment
    int m_compositedOpacity = -1;
    int m_compositedBlendMode = -1;
    qint64 m_selectionKey = -1;
    QRegion m_selectionRegion;
    QColor m_strokeColor;
    QPointF m_currentPos;
    bool m_strokePeak = false;   // pressure on opacity: keep the peak coverage
    QImage m_dabScratch;
};
