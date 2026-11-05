#include "thumbnails.h"

#include "document.h"
#include "layer.h"

#include <QHash>
#include <QPainter>
#include <cmath>
#include <vector>

namespace Thumbnails {

namespace {

struct Key {
    qint64 image;
    int w, h;
    bool operator==(const Key &o) const { return image == o.image && w == o.w && h == o.h; }
};
size_t qHash(const Key &k, size_t seed = 0) { return ::qHash(k.image, seed) ^ size_t(k.w * 7919 + k.h); }

// A few hundred tiny images at most; cleared wholesale when it grows past that
// (old content versions are never asked for again).
QHash<Key, QImage> &cache() {
    static QHash<Key, QImage> c;
    return c;
}
constexpr int kMaxEntries = 512;

QSize fitted(QSize size, QSize box) {
    if (size.isEmpty() || box.isEmpty()) return QSize();
    if (size.width() <= box.width() && size.height() <= box.height()) return size;
    return size.scaled(box, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
}

} // namespace

QImage scaled(const QImage &image, QSize box) {
    const QSize target = fitted(image.size(), box);
    if (target.isEmpty()) return QImage();
    if (target == image.size()) return image;
    // Step 1: pick pixels down to four times the target size (cost depends on
    // the small size only). Step 2: smooth the rest of the way, which averages
    // 4 x 4 picked pixels into each final pixel.
    const QSize mid(std::min(image.width(), target.width() * 4),
                    std::min(image.height(), target.height() * 4));
    QImage step = (mid.width() < image.width() || mid.height() < image.height())
                      ? image.scaled(mid, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                      : image;
    return step.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QImage cached(const QImage &image, QSize box) {
    if (image.isNull()) return QImage();
    const Key key{image.cacheKey(), box.width(), box.height()};
    auto &c = cache();
    auto it = c.constFind(key);
    if (it != c.constEnd()) return it.value();
    if (c.size() >= kMaxEntries) c.clear();
    QImage result = scaled(image, box);
    c.insert(key, result);
    return result;
}

QImage document(const Document &doc, QSize box) {
    const QSize target = fitted(doc.size(), box);
    if (target.isEmpty()) return QImage();
    const double sx = double(target.width()) / doc.width();
    const double sy = double(target.height()) / doc.height();
    std::vector<Layer> small;
    small.reserve(doc.layerCount());
    for (int i = 0; i < doc.layerCount(); ++i) {
        const Layer *layer = doc.layerAt(i);
        if (!layer || !layer->isVisible() || layer->opacity() <= 0) continue;
        const QSize layerTarget(std::max(1, int(std::lround(layer->width() * sx))),
                                std::max(1, int(std::lround(layer->height() * sy))));
        Layer part(cached(layer->image(), layerTarget), layer->name());
        part.setOpacity(layer->opacity());
        part.setBlendMode(layer->blendMode());
        part.setOffset(QPoint(int(std::lround(layer->offset().x() * sx)),
                              int(std::lround(layer->offset().y() * sy))));
        small.push_back(part);
    }
    return Document::compositeLayers(small, target);
}

void clearCache() { cache().clear(); }

} // namespace Thumbnails
