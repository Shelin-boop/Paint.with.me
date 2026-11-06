#pragma once
#include "tool.h"
#include <QImage>

class CloneStampTool : public Tool {
public:
    ToolType type() const override { return ToolType::CloneStamp; }
    QString name() const override { return "Clone Stamp"; }
    bool changesLayerPixels(const Document *) const override { return true; }

    void mousePressEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) override;
    void mouseMoveEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) override;
    void mouseReleaseEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) override;
    void drawOverlay(QPainter &painter, const CanvasWidget &canvas) override;

private:
    void cloneDab(const QPointF &pos, QImage &target, const QImage &source);
    void reportDab(CanvasWidget &canvas, const QPointF &from, const QPointF &to);

    bool m_sourceSet = false;
    QPointF m_sourcePos;
    QPointF m_offset;
public:
    bool updatesCanvasRegion() const override { return true; }
private:
    bool m_drawing = false;
    QPointF m_lastPos;
    QPointF m_currentPos;
    QImage m_beforeImage;
    QImage m_sourceImage;
    QImage m_coverage;   // per-pixel max coverage this stroke (prevents opacity buildup)
    class Document *m_doc = nullptr;
};
