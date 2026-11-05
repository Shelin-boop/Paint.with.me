#pragma once

#include "tool.h"

// Places the vanishing points of the perspective guides: click to add one (up
// to three, also outside the image), drag one to move it, right-click one to
// remove it. The guides themselves are drawn by the canvas, and the canvas
// makes painting tools follow them (see Perspective::snapDirection).
class PerspectiveTool : public Tool {
public:
    ToolType type() const override { return ToolType::Perspective; }
    QString name() const override { return QStringLiteral("Perspective"); }
    QCursor cursor() const override { return Qt::CrossCursor; }

    void mousePressEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) override;
    void mouseMoveEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) override;
    void mouseReleaseEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) override;
    void drawOverlay(QPainter &painter, const CanvasWidget &canvas) override;

    static constexpr double kGrabRadius = 10.0;   // screen pixels

private:
    int m_drag = -1;
    int m_hover = -1;
};
