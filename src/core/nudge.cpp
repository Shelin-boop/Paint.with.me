#include "nudge.h"

#include "document.h"
#include "layer.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

QPoint Nudge::stepFor(int key, qint64 heldMs, QSize imageSize) {
    const bool big = heldMs >= kLongPressMs;
    const int dx = big ? std::max(1, int(std::lround(imageSize.width() * 0.01))) : 1;
    const int dy = big ? std::max(1, int(std::lround(imageSize.height() * 0.01))) : 1;
    switch (key) {
    case Qt::Key_Left: return QPoint(-dx, 0);
    case Qt::Key_Right: return QPoint(dx, 0);
    case Qt::Key_Up: return QPoint(0, -dy);
    case Qt::Key_Down: return QPoint(0, dy);
    default: return QPoint();
    }
}

bool Nudge::active() const { return !m_doc.isNull(); }

void Nudge::start(Document &doc, Mode mode) {
    m_doc = &doc;
    m_mode = mode;
    m_moved = QPoint();
    m_layer = nullptr;
    if (mode != Mode::Pixels) return;
    Layer *layer = doc.activeLayer();
    if (!layer) return;
    m_layer = layer;
    // A shared copy: the layer's pixels are copied once, on the first change.
    m_original = layer->image();
    m_area = doc.selection().boundingRect().translated(-layer->offset()).intersected(m_original.rect());
    if (m_area.isEmpty()) return;
    const QPoint areaInDocument = m_area.topLeft() + layer->offset();
    // Only the selected area is worked on, so a big image costs no more than
    // a small one per key press.
    m_floating = m_original.copy(m_area);
    doc.selection().applyToImage(m_floating, areaInDocument);
    m_hole = m_original.copy(m_area);
    doc.selection().eraseFromImage(m_hole, areaInDocument);
    m_expectedKey = layer->image().cacheKey();
}

QRect Nudge::step(Document &doc, Mode mode, QPoint delta) {
    if (delta.isNull() || !doc.selection().hasSelection()) return QRect();
    Layer *layer = doc.activeLayer();
    if (mode == Mode::Pixels && (!layer || layer->isLocked())) return QRect();
    // Something else happened since the last step (another layer, the image
    // changed in another way): start afresh. The moves so far stay; an
    // outside change to the pixels drops their undo step rather than record
    // a wrong one.
    if (m_doc && (m_doc != &doc || m_mode != mode
                  || (mode == Mode::Pixels && (layer != m_layer || layer->image().cacheKey() != m_expectedKey)))) {
        if (m_doc == &doc && m_mode == Mode::Pixels && layer == m_layer && layer
            && layer->image().cacheKey() != m_expectedKey)
            m_doc = nullptr;   // abandon, see above
        finish();
    }
    if (!m_doc) start(doc, mode);

    QRect changed;
    if (mode == Mode::Pixels && m_layer && !m_area.isEmpty()) {
        QImage &image = m_layer->image();
        const QRect oldRect = m_area.translated(m_moved);
        const QRect newRect = m_area.translated(m_moved + delta);
        const QRect dirty = (oldRect | newRect).intersected(image.rect());
        QPainter p(&image);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.setClipRect(dirty);
        // Back to the layer without the selected pixels ...
        p.drawImage(dirty.topLeft(), m_original, dirty);
        p.drawImage(m_area.topLeft(), m_hole);
        // ... then the selected pixels at their new place.
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.drawImage(newRect.topLeft(), m_floating);
        p.end();
        m_expectedKey = image.cacheKey();
        changed = dirty.translated(m_layer->offset());
    }
    m_moved += delta;
    doc.selection().translate(delta);
    emit doc.selectionChanged();
    return changed;
}

void Nudge::finish() {
    if (m_doc && m_mode == Mode::Pixels && m_layer && !m_moved.isNull() && !m_area.isEmpty()
        && m_layer->image().cacheKey() == m_expectedKey) {
        for (int i = 0; i < m_doc->layerCount(); ++i) {
            if (m_doc->layerAt(i) == m_layer) {
                m_doc->pushImageEdit(i, m_original, QStringLiteral("Move Selection"));
                break;
            }
        }
    }
    m_doc = nullptr;
    m_layer = nullptr;
    m_original = m_hole = m_floating = QImage();
    m_area = QRect();
    m_moved = QPoint();
    m_expectedKey = 0;
}
