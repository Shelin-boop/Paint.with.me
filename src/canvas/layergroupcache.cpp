#include "layergroupcache.h"

#include "core/document.h"
#include "core/layer.h"

#include <QRegion>
#include <algorithm>

std::vector<qint64> LayerGroupCache::signatureOf(const Document &doc, int shift, int from, int to) {
    std::vector<qint64> sig;
    sig.reserve(6 + size_t(std::max(0, to - from)) * 6);
    sig.push_back(reinterpret_cast<qintptr>(&doc));
    sig.push_back(shift);
    sig.push_back(doc.width());
    sig.push_back(doc.height());
    sig.push_back(from);
    sig.push_back(to);
    for (int i = from; i < to; ++i) {
        const Layer *layer = doc.layerAt(i);
        sig.push_back(reinterpret_cast<qintptr>(layer));
        sig.push_back(layer->image().cacheKey());
        sig.push_back(layer->isVisible() ? 1 : 0);
        sig.push_back(qRound(layer->opacity() * 1000000.0f));
        sig.push_back(static_cast<qint64>(layer->blendMode()));
        sig.push_back((qint64(layer->offset().x()) << 32) ^ qint64(uint(layer->offset().y())));
    }
    return sig;
}

void LayerGroupCache::Group::release() {
    image = QImage();
    signature.clear();
    valid.clear();
    tiles = QSize();
    used = false;
}

void LayerGroupCache::prepare(Group &group, const Document &doc, int shift, int from, int to) {
    std::vector<qint64> sig = signatureOf(doc, shift, from, to);
    const QSize full = doc.reducedSize(shift);
    group.used = true;
    if (sig == group.signature && group.image.size() == full) return;
    // Something in the group changed: start again, tile by tile.
    if (group.image.size() != full || group.image.format() != QImage::Format_ARGB32_Premultiplied)
        group.image = QImage(full, QImage::Format_ARGB32_Premultiplied);
    group.signature = std::move(sig);
    group.tiles = QSize((full.width() + kTile - 1) / kTile, (full.height() + kTile - 1) / kTile);
    group.valid.assign(size_t(group.tiles.width()) * size_t(group.tiles.height()), 0);
}

void LayerGroupCache::Group::ensure(const Document &doc, const QRect &area, int shift, int from, int to) {
    const QRect bounds(QPoint(), image.size());
    const QRect wanted = area.intersected(bounds);
    if (wanted.isEmpty()) return;
    const int tx0 = wanted.left() / kTile, tx1 = wanted.right() / kTile;
    const int ty0 = wanted.top() / kTile, ty1 = wanted.bottom() / kTile;
    QRegion missing;
    for (int ty = ty0; ty <= ty1; ++ty)
        for (int tx = tx0; tx <= tx1; ++tx) {
            char &ok = valid[size_t(ty) * size_t(tiles.width()) + size_t(tx)];
            if (ok) continue;
            missing += QRect(tx * kTile, ty * kTile, kTile, kTile).intersected(bounds);
            ok = 1;
        }
    if (missing.isEmpty()) return;
    // Joined into as few rectangles as the region allows, so big parts are
    // combined on all cores at once.
    uchar *bits = image.bits();
    const qsizetype bpl = image.bytesPerLine();
    Document::CompositeParts parts;
    parts.from = from;
    parts.to = to;
    for (const QRect &r : missing) {
        QImage view(bits + r.y() * bpl + r.x() * 4, r.width(), r.height(), bpl,
                    QImage::Format_ARGB32_Premultiplied);
        doc.compositeInto(view, r.topLeft(), shift, parts);
    }
}

void LayerGroupCache::composite(const Document &doc, QImage &dest, const QPoint &origin, int shift) {
    const int n = doc.layerCount();
    const int k = doc.activeLayerIndex();
    // Above: only Normal layers can be combined ahead (the others blend with
    // what lies under them), and only two or more save anything.
    int aboveVisible = 0;
    bool allNormal = true;
    for (int i = k + 1; i < n; ++i) {
        const Layer *layer = doc.layerAt(i);
        if (!layer->isVisible() || layer->opacity() <= 0.0f) continue;
        ++aboveVisible;
        if (layer->blendMode() != BlendMode::Normal) allNormal = false;
    }
    const bool useBelow = k >= 2 && k < n;
    const bool useAbove = k >= 0 && aboveVisible >= 2 && allNormal;
    if (!useBelow && !useAbove) {
        clear();
        doc.compositeInto(dest, origin, shift);
        return;
    }
    const QRect area(origin, dest.size());
    if (useBelow) {
        prepare(m_below, doc, shift, 0, k);
        m_below.ensure(doc, area, shift, 0, k);
    } else {
        m_below.release();
    }
    if (useAbove) {
        prepare(m_above, doc, shift, k + 1, n);
        m_above.ensure(doc, area, shift, k + 1, n);
    } else {
        m_above.release();
    }
    Document::CompositeParts parts;
    parts.from = useBelow ? k : 0;
    parts.to = useAbove ? k + 1 : n;
    parts.below = useBelow ? &m_below.image : nullptr;
    parts.above = useAbove ? &m_above.image : nullptr;
    doc.compositeInto(dest, origin, shift, parts);
}

void LayerGroupCache::clear() {
    m_below.release();
    m_above.release();
}

qint64 LayerGroupCache::memoryUsed() const {
    return m_below.image.sizeInBytes() + m_above.image.sizeInBytes();
}
