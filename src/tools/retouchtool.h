#pragma once

#include <QImage>
#include <QRect>
#include <vector>

#include "tool.h"

// Brush-like tools that rework the pixels already on the layer instead of
// adding color: the smudge tool and the blur brush. Width, hardness, spacing,
// pen pressure and tilt work as on the paintbrush; "Strength" (the tool's
// opacity value) sets how strongly each dab acts. The active selection limits
// them, soft selection edges included.
class RetouchTool : public Tool {
public:
    void mousePressEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) override;
    void mouseMoveEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) override;
    void mouseReleaseEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) override;
    void drawOverlay(QPainter &painter, const CanvasWidget &canvas) override;
    bool updatesCanvasRegion() const override { return true; }

    // How much one dab weighs at a pixel centre `p` (layer pixels): 1 inside
    // the hard core, falling off to 0 at the dab's edge; flattened tips too.
    static double dabWeight(const Dab &dab, int hardness, bool antialiased, const QPointF &p);

protected:
    // Called once per stroke before the first dab (layer pixels).
    virtual void beginStroke(const QImage &image, const Dab &first) { Q_UNUSED(image); Q_UNUSED(first); }
    // Reworks `image` under one dab. `weights` holds the dab weight times the
    // selection for each pixel of `area` (row by row); the tool multiplies in
    // its strength. Returns the changed area.
    virtual void applyDab(QImage &image, const Dab &dab, const QRect &area,
                          const std::vector<float> &weights) = 0;
    virtual QString undoName() const = 0;

    double strength() const { return m_opacity / 100.0; }

private:
    // The dab's area on the layer and its weights (dab shape x selection);
    // empty if it touches nothing that may change.
    QRect dabArea(const Dab &dab, std::vector<float> &weights) const;
    void paintDabAt(const Dab &dab);

    class Document *m_doc = nullptr;
    class Layer *m_layer = nullptr;
    bool m_drawing = false;
    QPointF m_lastPos;      // layer pixels
    QPointF m_currentPos;   // document pixels (for the outline)
    QImage m_beforeImage;
    QRect m_dirty;          // layer pixels changed since the last canvas update
    std::vector<float> m_weights;
};

// Drags color like a finger through wet paint: each dab lays down the paint
// it picked up at the previous dab, and picks up some of what is under it.
// Strength: how far the paint is carried (100 % carries it to the end).
class SmudgeTool : public RetouchTool {
public:
    SmudgeTool();
    ToolType type() const override { return ToolType::Smudge; }
    QString name() const override { return QStringLiteral("Smudge"); }

protected:
    void beginStroke(const QImage &image, const Dab &first) override;
    void applyDab(QImage &image, const Dab &dab, const QRect &area,
                  const std::vector<float> &weights) override;
    QString undoName() const override { return QStringLiteral("Smudge"); }

private:
    // The picked-up paint: premultiplied RGBA floats in a square around the
    // dab centre, side 2 * m_half + 1; m_valid marks pixels that hold paint.
    int m_half = 0;
    std::vector<float> m_paint;
    std::vector<unsigned char> m_valid;
};

// Softens what is painted: each dab replaces the pixels under it with a
// blurred copy, by the strength. Scrubbing over a spot blurs it further.
class BlurBrushTool : public RetouchTool {
public:
    BlurBrushTool();
    ToolType type() const override { return ToolType::BlurBrush; }
    QString name() const override { return QStringLiteral("Blur Brush"); }
    // Blur radius in pixels for a brush width: an eighth of it, at least 1.
    static int blurRadius(double brushSize);

protected:
    void applyDab(QImage &image, const Dab &dab, const QRect &area,
                  const std::vector<float> &weights) override;
    QString undoName() const override { return QStringLiteral("Blur Brush"); }
};
