#include "retouchtool.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

#include "canvas/canvaswidget.h"
#include "core/document.h"
#include "core/layer.h"
#include "core/selection.h"

// ---- Shared: strokes, dab weights, selection, undo ----

double RetouchTool::dabWeight(const Dab &dab, int hardness, bool antialiased, const QPointF &p) {
    double dx = p.x() - dab.pos.x();
    double dy = p.y() - dab.pos.y();
    if (dab.roundness < 0.999) {
        // Into the tip's own frame: along the tilt, and stretched across it.
        const double a = dab.angle * M_PI / 180.0;
        const double u = dx * std::cos(a) + dy * std::sin(a);
        const double v = -dx * std::sin(a) + dy * std::cos(a);
        dx = u;
        dy = v / dab.roundness;
    }
    const double d = std::sqrt(dx * dx + dy * dy);
    const double r = dab.radius;
    if (r <= 0.0) return 0.0;
    if (hardness >= 95) {
        if (!antialiased) return d <= r ? 1.0 : 0.0;
        return std::clamp(r - d + 0.5, 0.0, 1.0);
    }
    const double t = d / r;
    if (t >= 1.0) return 0.0;
    const double core = hardness / 100.0;
    if (t <= core) return 1.0;
    const double w = 1.0 - (t - core) / (1.0 - core);
    return w * w * (3.0 - 2.0 * w);   // smooth falloff
}

QRect RetouchTool::dabArea(const Dab &dab, std::vector<float> &weights) const {
    if (!m_layer || dab.radius < 0.4 || dab.alpha <= 0.0) return QRect();
    const double r = dab.radius;
    const QPoint offset = m_layer->offset();
    QRect area = QRectF(dab.pos.x() - r - 1, dab.pos.y() - r - 1, 2 * r + 2, 2 * r + 2).toAlignedRect();
    area &= m_layer->image().rect();
    area &= QRect(QPoint(), m_doc->size()).translated(-offset);   // only what is inside the image
    if (area.isEmpty()) return QRect();

    weights.assign(size_t(area.width()) * size_t(area.height()), 0.0f);
    const Selection &sel = m_doc->selection();
    const bool selected = sel.hasSelection();
    const QImage &mask = sel.mask();
    bool any = false;
    for (int y = 0; y < area.height(); ++y) {
        const int ly = area.top() + y;
        const uchar *maskLine = selected ? mask.constScanLine(ly + offset.y()) : nullptr;
        for (int x = 0; x < area.width(); ++x) {
            const int lx = area.left() + x;
            double w = dabWeight(dab, m_hardness, m_antialiased, QPointF(lx + 0.5, ly + 0.5));
            if (w <= 0.0) continue;
            if (maskLine) w *= maskLine[lx + offset.x()] / 255.0;
            w *= dab.alpha;
            if (w <= 0.0) continue;
            weights[size_t(y) * area.width() + x] = float(w);
            any = true;
        }
    }
    return any ? area : QRect();
}

void RetouchTool::paintDabAt(const Dab &dab) {
    const QRect area = dabArea(dab, m_weights);
    if (area.isEmpty()) return;
    applyDab(m_layer->image(), dab, area, m_weights);
    m_dirty |= area;
}

void RetouchTool::mousePressEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) {
    if (event->button() != Qt::LeftButton || m_drawing) return;
    Document *doc = canvas.document();
    Layer *layer = doc ? doc->activeLayer() : nullptr;
    if (!layer || layer->isLocked()) return;
    m_doc = doc;
    m_layer = layer;
    m_drawing = true;
    m_currentPos = canvasPos;
    if (layer->image().format() != QImage::Format_ARGB32_Premultiplied) {
        // The dab code works on premultiplied pixels.
        layer->image() = layer->image().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    m_beforeImage = layer->image().copy();
    m_dirty = QRect();
    m_lastPos = imagePoint(canvasPos, doc);
    m_lastPressure = m_pressure;
    const Dab first = dabAt(m_lastPos, m_pressure);
    beginStroke(layer->image(), first);
    paintDabAt(first);
    if (!m_dirty.isEmpty()) canvas.updateCanvasRegion(m_dirty.translated(layer->offset()));
    m_dirty = QRect();
}

void RetouchTool::mouseMoveEvent(const QPointF &canvasPos, QMouseEvent *, CanvasWidget &canvas) {
    m_currentPos = canvasPos;
    if (!m_drawing) return;
    if (canvas.document() != m_doc || m_doc->activeLayer() != m_layer) return;
    const QPointF pos = imagePoint(canvasPos, m_doc);
    forEachDab(m_lastPos, pos, m_lastPressure, m_pressure, [this](const Dab &dab) { paintDabAt(dab); });
    m_lastPressure = m_pressure;
    m_lastPos = pos;
    if (!m_dirty.isEmpty()) canvas.updateCanvasRegion(m_dirty.translated(m_layer->offset()));
    m_dirty = QRect();
}

void RetouchTool::mouseReleaseEvent(const QPointF &, QMouseEvent *event, CanvasWidget &canvas) {
    if (!m_drawing || event->button() != Qt::LeftButton) return;
    m_drawing = false;
    if (canvas.document() == m_doc && m_doc->activeLayer() == m_layer
        && m_layer->image() != m_beforeImage)   // no empty undo steps
        m_doc->pushImageEdit(m_doc->activeLayerIndex(), m_beforeImage, undoName());
    m_beforeImage = QImage();
    m_weights.clear();
    m_weights.shrink_to_fit();
}

void RetouchTool::drawOverlay(QPainter &painter, const CanvasWidget &canvas) {
    const QPointF widgetPos = canvas.canvasToWidget(m_currentPos);
    const double radius = m_brushSize / 2.0 * canvas.zoom();
    painter.setPen(QPen(Qt::black, 1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(widgetPos, radius, radius);
}

namespace {

inline void loadPixel(QRgb px, float *out) {
    out[0] = qRed(px);
    out[1] = qGreen(px);
    out[2] = qBlue(px);
    out[3] = qAlpha(px);
}

// Back to a premultiplied pixel: rounded, and no channel above alpha.
inline QRgb storePixel(const float *c) {
    const int a = std::clamp(int(c[3] + 0.5f), 0, 255);
    const int r = std::clamp(int(c[0] + 0.5f), 0, a);
    const int g = std::clamp(int(c[1] + 0.5f), 0, a);
    const int b = std::clamp(int(c[2] + 0.5f), 0, a);
    return qRgba(r, g, b, a);
}

}  // namespace

// ---- Smudge ----

SmudgeTool::SmudgeTool() {
    m_opacity = 60;   // strength: carries paint a fair way, like a finger
    m_hardness = 50;
    m_spacing = 10;
}

void SmudgeTool::beginStroke(const QImage &image, const Dab &first) {
    m_half = int(std::ceil(m_brushSize / 2.0)) + 2;
    const int side = 2 * m_half + 1;
    m_paint.assign(size_t(side) * side * 4, 0.0f);
    m_valid.assign(size_t(side) * side, 0);
    const QPoint c(qRound(first.pos.x()), qRound(first.pos.y()));
    for (int j = 0; j < side; ++j) {
        const int y = c.y() + j - m_half;
        if (y < 0 || y >= image.height()) continue;
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int i = 0; i < side; ++i) {
            const int x = c.x() + i - m_half;
            if (x < 0 || x >= image.width()) continue;
            const size_t k = size_t(j) * side + i;
            loadPixel(line[x], &m_paint[k * 4]);
            m_valid[k] = 1;
        }
    }
}

void SmudgeTool::applyDab(QImage &image, const Dab &dab, const QRect &area,
                          const std::vector<float> &weights) {
    const int side = 2 * m_half + 1;
    const QPoint c(qRound(dab.pos.x()), qRound(dab.pos.y()));
    // How much of the old paint the finger keeps after each dab.
    const float keep = float(strength());
    for (int y = 0; y < area.height(); ++y) {
        const int ly = area.top() + y;
        const int j = ly - c.y() + m_half;
        if (j < 0 || j >= side) continue;
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(ly));
        for (int x = 0; x < area.width(); ++x) {
            const int lx = area.left() + x;
            const int i = lx - c.x() + m_half;
            if (i < 0 || i >= side) continue;
            const size_t k = size_t(j) * side + i;
            float *paint = &m_paint[k * 4];
            float canvas[4];
            loadPixel(line[lx], canvas);
            // The finger mixes what it carries with what is here ...
            if (m_valid[k]) {
                for (int ch = 0; ch < 4; ++ch) paint[ch] = paint[ch] * keep + canvas[ch] * (1.0f - keep);
            } else {
                for (int ch = 0; ch < 4; ++ch) paint[ch] = canvas[ch];
                m_valid[k] = 1;
            }
            // ... and leaves that mix behind, as much as the dab covers.
            const float w = weights[size_t(y) * area.width() + x];
            if (w > 0.0f) {
                for (int ch = 0; ch < 4; ++ch) canvas[ch] += (paint[ch] - canvas[ch]) * w;
                line[lx] = storePixel(canvas);
            }
        }
    }
}

// ---- Blur brush ----

BlurBrushTool::BlurBrushTool() {
    m_opacity = 50;
    m_hardness = 50;
    m_spacing = 15;
}

int BlurBrushTool::blurRadius(double brushSize) {
    return std::max(1, int(std::lround(brushSize / 8.0)));
}

namespace {

// Box blur of interleaved RGBA floats along rows (step 4) or columns, with a
// window that shrinks at the borders (no darkening from outside).
void boxBlur(std::vector<float> &data, int w, int h, int r, bool horizontal) {
    const int len = horizontal ? w : h;
    const int lines = horizontal ? h : w;
    std::vector<double> prefix(size_t(len + 1) * 4);
    std::vector<float> out(size_t(len) * 4);
    for (int l = 0; l < lines; ++l) {
        auto at = [&](int i) -> float * {
            return horizontal ? &data[(size_t(l) * w + i) * 4] : &data[(size_t(i) * w + l) * 4];
        };
        for (int ch = 0; ch < 4; ++ch) prefix[ch] = 0.0;
        for (int i = 0; i < len; ++i) {
            const float *px = at(i);
            for (int ch = 0; ch < 4; ++ch) prefix[size_t(i + 1) * 4 + ch] = prefix[size_t(i) * 4 + ch] + px[ch];
        }
        for (int i = 0; i < len; ++i) {
            const int lo = std::max(0, i - r), hi = std::min(len, i + r + 1);
            const double n = hi - lo;
            for (int ch = 0; ch < 4; ++ch)
                out[size_t(i) * 4 + ch] = float((prefix[size_t(hi) * 4 + ch] - prefix[size_t(lo) * 4 + ch]) / n);
        }
        for (int i = 0; i < len; ++i) {
            float *px = at(i);
            for (int ch = 0; ch < 4; ++ch) px[ch] = out[size_t(i) * 4 + ch];
        }
    }
}

}  // namespace

void BlurBrushTool::applyDab(QImage &image, const Dab &, const QRect &area,
                             const std::vector<float> &weights) {
    const int br = blurRadius(m_brushSize);
    // Blur a margin around the dab too, so its edge blends with what is beside it.
    const QRect src = area.adjusted(-2 * br, -2 * br, 2 * br, 2 * br) & image.rect();
    const int w = src.width(), h = src.height();
    std::vector<float> data(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(src.top() + y));
        for (int x = 0; x < w; ++x) loadPixel(line[src.left() + x], &data[(size_t(y) * w + x) * 4]);
    }
    // Two box passes each way: close to a Gaussian.
    for (int pass = 0; pass < 2; ++pass) {
        boxBlur(data, w, h, br, true);
        boxBlur(data, w, h, br, false);
    }
    const float s = float(strength());
    for (int y = 0; y < area.height(); ++y) {
        const int ly = area.top() + y;
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(ly));
        for (int x = 0; x < area.width(); ++x) {
            const float wt = weights[size_t(y) * area.width() + x] * s;
            if (wt <= 0.0f) continue;
            const int lx = area.left() + x;
            const float *blurred = &data[(size_t(ly - src.top()) * w + (lx - src.left())) * 4];
            float canvas[4];
            loadPixel(line[lx], canvas);
            for (int ch = 0; ch < 4; ++ch) canvas[ch] += (blurred[ch] - canvas[ch]) * wt;
            line[lx] = storePixel(canvas);
        }
    }
}
