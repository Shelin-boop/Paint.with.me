#include "document.h"
#include "pdnformat.h"
#include "psdformat.h"
#include <QPainter>
#include <QtConcurrent/QtConcurrentMap>
#include <cstring>
#include <QImageReader>
#include <QImageWriter>
#include <cstdlib>
#include <limits>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>

Document::Document(int width, int height, QObject *parent)
    : QObject(parent), m_width(width), m_height(height), m_selection(width, height)
{
    auto layer = std::make_shared<Layer>(width, height, "Background");
    layer->clear(Qt::white);
    m_layers.push_back(layer);
}

Document::Document(const QString &filePath, QObject *parent)
    : QObject(parent)
{
    load(filePath);
}

void Document::resize(int width, int height) {
    if (width <= 0 || height <= 0 || (width == m_width && height == m_height)) return;
    auto cmd = std::make_unique<DocumentSnapshotCommand>(this, "Resize Image");
    const QSize oldSize = size();
    const double scaleX = double(width) / m_width;
    const double scaleY = double(height) / m_height;
    m_width = width;
    m_height = height;
    // Resample every bitmap and origin by the document scale, not buffer size.
    for (auto &layer : m_layers) {
        const QPoint origin = layer->offset();
        QImage scaled = layer->image().scaled(
            qMax(1, qRound(layer->width() * scaleX)),
            qMax(1, qRound(layer->height() * scaleY)),
            Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (origin.isNull() && layer->image().size() == oldSize) {
            layer->setImage(scaled); // Preserve ordinary canvas-sized resampling.
            continue;
        }
        const QPoint scaledOrigin(qRound(origin.x() * scaleX), qRound(origin.y() * scaleY));
        const QRect bounds = QRect(scaledOrigin, scaled.size()).united(QRect(QPoint(), size()));
        QImage result(bounds.size(), QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        painter.drawImage(scaledOrigin - bounds.topLeft(), scaled);
        // Rounded bitmap sizes/origins change the sampling phase, and hidden
        // pixels must not bleed into the visible canvas at its edges. Sample
        // that finite domain exactly as an ordinary layer, while keeping the
        // rounded, document-scaled off-canvas bitmap outside it.
        const QImage visible = layer->image().copy(QRect(-origin, oldSize)).scaled(
            size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_ARGB32_Premultiplied);
        // Source replaces only the new canvas rectangle, not the off-canvas
        // bitmap. SourceOver would blend the same partial-alpha pixels twice.
        painter.setClipRect(QRect(-bounds.topLeft(), size()));
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(-bounds.topLeft(), visible);
        painter.end();
        layer->setOffset(bounds.topLeft());
        layer->setImage(result);
    }
    m_selection.resize(width, height);
    cmd->captureAfter();
    m_history.push(std::move(cmd));
    m_modified = true;
    emit sizeChanged(width, height);
    emit documentChanged();
    emit layersChanged();
}

void Document::resizeCanvas(int width, int height, int anchorX, int anchorY) {
    if (width <= 0 || height <= 0) return;
    auto cmd = std::make_unique<DocumentSnapshotCommand>(this, "Canvas Size");
    for (auto &layer : m_layers) {
        QImage newImg(width, height, QImage::Format_ARGB32_Premultiplied);
        newImg.fill(Qt::transparent);
        QPainter p(&newImg);
        p.drawImage(anchorX, anchorY, layer->image());
        layer->setImage(newImg);
    }
    m_width = width;
    m_height = height;
    // Re-anchor the selection mask consistently with the layers.
    m_selection.resize(width, height);
    if (!m_selection.isEmpty())
        m_selection.translate(QPoint(anchorX, anchorY));
    cmd->captureAfter();
    m_history.push(std::move(cmd));
    m_modified = true;
    emit sizeChanged(width, height);
    emit documentChanged();
    emit layersChanged();
}

Layer *Document::layerAt(int index) {
    if (index < 0 || index >= static_cast<int>(m_layers.size())) return nullptr;
    return m_layers[index].get();
}

const Layer *Document::layerAt(int index) const {
    if (index < 0 || index >= static_cast<int>(m_layers.size())) return nullptr;
    return m_layers[index].get();
}

Layer *Document::activeLayer() {
    return layerAt(m_activeLayer);
}

void Document::setActiveLayer(int index) {
    if (index >= 0 && index < static_cast<int>(m_layers.size())) {
        m_activeLayer = index;
        emit activeLayerChanged(index);
    }
}

void Document::setEditAllLayers(bool editAllLayers) {
    if (m_editAllLayers == editAllLayers) return;
    m_editAllLayers = editAllLayers;
    emit editScopeChanged(m_editAllLayers);
}

int Document::addLayer(const QString &name) {
    QString layerName = name.isEmpty() ? QString("Layer %1").arg(m_layers.size() + 1) : name;
    auto layer = std::make_shared<Layer>(m_width, m_height, layerName);
    // New layers are transparent (the Layer ctor already fills transparent), so
    // the artwork on layers below shows through. Filling white here made every
    // added layer opaque and hid everything under it (issue #3). Only the initial
    // Background layer is white.
    int index = m_activeLayer + 1;
    m_layers.insert(m_layers.begin() + index, layer);

    auto cmd = std::make_unique<LayerAddCommand>(this, index, "Add Layer");
    m_history.push(std::move(cmd));

    m_activeLayer = index;
    m_modified = true;
    emit layersChanged();
    emit activeLayerChanged(m_activeLayer);
    return index;
}

int Document::addLayer(const QImage &image, const QString &name) {
    QString layerName = name.isEmpty() ? QString("Layer %1").arg(m_layers.size() + 1) : name;
    // Every layer must be canvas-sized — the Move tool and other tools assume it.
    // A pasted/imported image that was smaller than the canvas used to become a
    // layer of its own size, so moving it clipped the pixels to that little box
    // instead of letting them travel across the canvas (issue #18). Composite the
    // image into a canvas-sized transparent layer at the top-left. (An image that
    // already matches the canvas — e.g. a layer restored from a .psw — is copied
    // unchanged.)
    QImage canvasSized(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
    canvasSized.fill(Qt::transparent);
    {
        QPainter p(&canvasSized);
        p.drawImage(0, 0, image);
    }
    auto layer = std::make_shared<Layer>(canvasSized, layerName);
    int index = m_activeLayer + 1;
    m_layers.insert(m_layers.begin() + index, layer);

    auto cmd = std::make_unique<LayerAddCommand>(this, index, "Add Layer");
    m_history.push(std::move(cmd));

    m_activeLayer = index;
    m_modified = true;
    emit layersChanged();
    emit activeLayerChanged(m_activeLayer);
    emit documentChanged();
    return index;
}

int Document::pasteImage(const QImage &image, const QString &name, bool expandCanvas) {
    if (image.isNull()) return -1;
    if (expandCanvas) {
        const int width = qMax(m_width, image.width()), height = qMax(m_height, image.height());
        // Check local extents before changing any document state. Offsets read
        // from native files can require a bitmap too wide/tall for QImage.
        for (const auto &layer : m_layers) {
            if (qint64(width) - qMin(layer->offset().x(), 0) > std::numeric_limits<int>::max()
                || qint64(height) - qMin(layer->offset().y(), 0) > std::numeric_limits<int>::max())
                return -1;
        }
    }
    auto cmd = std::make_unique<DocumentSnapshotCommand>(this, "Paste");
    const QSize oldSize = size();
    if (expandCanvas) {
        m_width = qMax(m_width, image.width());
        m_height = qMax(m_height, image.height());
        for (auto &layer : m_layers) {
            // Preserve any already-retained content and its document-space offset.
            // A negative origin needs extra local storage to reach the new
            // right/bottom canvas edges. Keep the origin and all existing pixels.
            const QPoint origin = layer->offset();
            const QSize canvasExtent(m_width - qMin(origin.x(), 0),
                                     m_height - qMin(origin.y(), 0));
            const QSize buffer = layer->image().size().expandedTo(canvasExtent);
            if (buffer == layer->image().size()) continue;
            QImage padded(buffer, QImage::Format_ARGB32_Premultiplied);
            padded.fill(Qt::transparent);
            { QPainter painter(&padded); painter.drawImage(0, 0, layer->image()); }
            layer->setImage(padded);
        }
    }
    QImage pasted(size().expandedTo(image.size()), QImage::Format_ARGB32_Premultiplied);
    pasted.fill(Qt::transparent);
    { QPainter painter(&pasted); painter.drawImage(0, 0, image); }
    const int index = m_activeLayer + 1;
    auto layer = std::make_shared<Layer>(pasted, name);
    layer->setRetainsOffCanvasPixels(!expandCanvas && image.size().expandedTo(oldSize) != oldSize);
    m_layers.insert(m_layers.begin() + index, layer);
    m_activeLayer = index;
    m_selection.resize(m_width, m_height);
    m_selection.clear();
    cmd->captureAfter();
    m_history.push(std::move(cmd));
    m_modified = true;
    if (oldSize != size()) emit sizeChanged(m_width, m_height);
    emit selectionChanged();
    emit layersChanged();
    emit activeLayerChanged(m_activeLayer);
    emit documentChanged();
    return index;
}

void Document::removeLayer(int index) {
    if (m_layers.size() <= 1 || index < 0 || index >= static_cast<int>(m_layers.size())) return;

    auto cmd = std::make_unique<LayerRemoveCommand>(this, index, "Remove Layer");
    m_layers.erase(m_layers.begin() + index);
    m_history.push(std::move(cmd));

    // Keep the active layer pointing at the same logical layer: if we removed a
    // layer at or below it, shift down; then clamp.
    if (m_activeLayer > index || m_activeLayer >= static_cast<int>(m_layers.size()))
        m_activeLayer = std::max(0, m_activeLayer - 1);
    m_modified = true;
    emit layersChanged();
    emit activeLayerChanged(m_activeLayer);
}

void Document::moveLayer(int fromIndex, int toIndex) {
    if (fromIndex < 0 || fromIndex >= static_cast<int>(m_layers.size())) return;
    if (toIndex < 0 || toIndex >= static_cast<int>(m_layers.size())) return;
    if (fromIndex == toIndex) return;

    auto cmd = std::make_unique<LayerMoveCommand>(this, fromIndex, toIndex, "Move Layer");

    auto layer = m_layers[fromIndex];
    m_layers.erase(m_layers.begin() + fromIndex);
    m_layers.insert(m_layers.begin() + toIndex, layer);
    m_history.push(std::move(cmd));

    m_activeLayer = toIndex;
    m_modified = true;
    emit layersChanged();
    emit activeLayerChanged(m_activeLayer);
}

void Document::duplicateLayer(int index) {
    if (index < 0 || index >= static_cast<int>(m_layers.size())) return;
    auto copy = std::make_shared<Layer>(*m_layers[index]);
    copy->setName(copy->name() + " copy");
    int newIndex = index + 1;
    m_layers.insert(m_layers.begin() + newIndex, copy);

    auto cmd = std::make_unique<LayerAddCommand>(this, newIndex, "Duplicate Layer");
    m_history.push(std::move(cmd));

    m_activeLayer = newIndex;
    m_modified = true;
    emit layersChanged();
    emit activeLayerChanged(m_activeLayer);
}

// If the image has a uniform border colour (all four corners identical), returns
// a copy where every pixel matching that colour (within a small tolerance) is
// made transparent — i.e. the flat background is knocked out, leaving artwork.
static QImage knockOutBackground(const QImage &src) {
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width(), h = img.height();
    if (w < 2 || h < 2) return img;
    auto px = [&](int x, int y) { return reinterpret_cast<const QRgb*>(img.constScanLine(y))[x]; };
    const QRgb c0 = px(0, 0), c1 = px(w-1, 0), c2 = px(0, h-1), c3 = px(w-1, h-1);
    if (c0 != c1 || c0 != c2 || c0 != c3) return img;   // no uniform background
    if (qAlpha(c0) == 0) return img;                    // already transparent
    const int br = qRed(c0), bg = qGreen(c0), bb = qBlue(c0);
    const int tol = 10;
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = line[x];
            if (std::abs(qRed(p) - br) <= tol && std::abs(qGreen(p) - bg) <= tol &&
                std::abs(qBlue(p) - bb) <= tol)
                line[x] = qRgba(0, 0, 0, 0);
        }
    }
    return img;
}

void Document::mergeLayerDown(int index, bool keepArtwork) {
    if (index <= 0 || index >= static_cast<int>(m_layers.size())) return;
    auto cmd = std::make_unique<DocumentSnapshotCommand>(this, "Merge Layer Down");

    Layer *upper = m_layers[index].get();
    Layer *lower = m_layers[index - 1].get();

    // Both bitmaps live in document space. Retain their union rather than
    // clipping the upper to the lower buffer or applying the lower offset twice.
    const QRect bounds = QRect(lower->offset(), lower->image().size()).united(
        QRect(upper->offset(), upper->image().size()));
    QImage base(bounds.size(), QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::transparent);
    { QPainter p(&base); p.drawImage(lower->offset() - bounds.topLeft(), lower->image()); }
    const QPoint upperOrigin = upper->offset() - bounds.topLeft();
    QImage merged;
    if (keepArtwork) {
        // Knock out the upper layer's flat background so the lower layer's
        // artwork isn't hidden by an opaque (e.g. white) fill.
        QImage top = knockOutBackground(upper->image()).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        merged = base;
        QPainter p(&merged);
        p.setOpacity(upper->opacity());
        p.drawImage(upperOrigin, top);
        p.end();
    } else {
        Layer positionedUpper(*upper);
        positionedUpper.setOffset(upperOrigin);
        merged = positionedUpper.composited(base);
    }
    lower->setImage(merged);
    lower->setOffset(bounds.topLeft());
    lower->setRetainsOffCanvasPixels(lower->retainsOffCanvasPixels() || upper->retainsOffCanvasPixels());
    lower->setOpacity(1.0f);
    lower->setBlendMode(BlendMode::Normal);
    m_layers.erase(m_layers.begin() + index);
    m_activeLayer = index - 1;

    cmd->captureAfter();
    m_history.push(std::move(cmd));
    m_modified = true;
    emit layersChanged();
    emit activeLayerChanged(m_activeLayer);
    emit documentChanged();
}

void Document::flattenImage() {
    if (m_layers.size() <= 1) return;
    auto cmd = std::make_unique<DocumentSnapshotCommand>(this, "Flatten Image");

    QImage flat = flattenVisible();
    m_layers.clear();
    auto layer = std::make_shared<Layer>(flat, "Background");
    m_layers.push_back(layer);
    m_activeLayer = 0;

    cmd->captureAfter();
    m_history.push(std::move(cmd));
    m_modified = true;
    emit layersChanged();
    emit activeLayerChanged(m_activeLayer);
    emit documentChanged();
}

void Document::insertLayerDirect(int index, std::shared_ptr<Layer> layer) {
    index = qBound(0, index, static_cast<int>(m_layers.size()));
    m_layers.insert(m_layers.begin() + index, layer);
    m_activeLayer = index;
    emit layersChanged();
    emit activeLayerChanged(m_activeLayer);
    emit documentChanged();
}

void Document::removeLayerDirect(int index) {
    if (index >= 0 && index < static_cast<int>(m_layers.size())) {
        m_layers.erase(m_layers.begin() + index);
        if (m_activeLayer >= static_cast<int>(m_layers.size()))
            m_activeLayer = std::max(0, static_cast<int>(m_layers.size()) - 1);
        emit layersChanged();
        emit documentChanged();
    }
}

void Document::moveLayerDirect(int fromIndex, int toIndex) {
    const int n = static_cast<int>(m_layers.size());
    if (fromIndex < 0 || fromIndex >= n || toIndex < 0 || toIndex >= n) return;
    auto layer = m_layers[fromIndex];
    m_layers.erase(m_layers.begin() + fromIndex);
    m_layers.insert(m_layers.begin() + toIndex, layer);
    m_activeLayer = toIndex;
    emit layersChanged();
    emit activeLayerChanged(m_activeLayer);
    emit documentChanged();
}

void Document::pushImageEdit(int layerIndex, const QImage &before, const QString &desc) {
    if (auto *l = layerAt(layerIndex)) {
        auto cmd = std::make_unique<ImageEditCommand>(this, layerIndex, before, l->image(), desc);
        m_history.push(std::move(cmd));
        m_modified = true;
        emit documentChanged();
    }
}

bool Document::commitImageEdits(const std::vector<ImageEditState> &edits, const QString &description) {
    if (edits.empty()) return false;
    for (const auto &edit : edits)
        if (auto *layer = layerAt(edit.index)) layer->setImage(edit.after);
    m_history.push(std::make_unique<MultiImageEditCommand>(this, edits, description));
    m_modified = true;
    notifyContentChanged();
    return true;
}
QImage Document::compositeLayers(const std::vector<Layer> &layers, QSize size) {
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) return result;
    result.fill(Qt::transparent);
    QPainter painter(&result);
    for (const auto &layer : layers)
        layer.compositeInto(painter, result, QPoint(), result.rect());
    return result;
}

QImage Document::flatten() const {
    QImage result(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) return result;
    compositeInto(result, QPoint());
    return result;
}

QImage Document::flattenVisible() const {
    return flatten();
}

QImage Document::flattenVisible(const QRect &region) const {
    const QRect tile = region.intersected(QRect(QPoint(), size()));
    if (tile.isEmpty()) return QImage();
    QImage result(tile.size(), QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) return result;
    compositeInto(result, tile.topLeft());
    return result;
}

void Document::compositeInto(QImage &dest, const QPoint &origin, int shift) const {
    if (dest.isNull()) return;
    Q_ASSERT(dest.format() == QImage::Format_ARGB32_Premultiplied);
    shift = std::max(0, shift);
    const QRect region(origin, dest.size());   // in `dest` (possibly reduced) pixels
    const QSize full = reducedSize(shift);

    // Which layers take part, and the part of `region` each one can change.
    // For big areas, the empty margins of each layer are left out (finding them
    // costs one pass over a changed layer, then it is remembered). An Overwrite
    // layer also changes pixels below its transparent parts, so it keeps its
    // full rectangle.
    const bool useBounds = qint64(region.width()) * region.height() * 8
                           >= qint64(full.width()) * full.height();
    struct Item { const Layer *layer; QRect area; };   // area: in `dest` pixels
    std::vector<Item> items;
    items.reserve(m_layers.size());
    for (const auto &ptr : m_layers) {
        const Layer &layer = *ptr;
        if (!layer.isVisible() || layer.opacity() <= 0.0f || layer.image().isNull()) continue;
        QRect area = (useBounds && layer.blendMode() != BlendMode::Overwrite)
                         ? layer.contentBounds()
                         : layer.image().rect();
        area = Layer::reducedRect(area.translated(layer.offset()), shift)
                   .intersected(region).translated(-origin);
        if (!area.isEmpty()) items.push_back({&layer, area});
    }

    // Strips of about 256 KB stay in the CPU cache while all layers are blended
    // onto them, instead of streaming the whole result through memory per layer.
    uchar *bits = dest.bits();   // once, here: never detach from worker threads
    const qsizetype bpl = dest.bytesPerLine();
    const int width = dest.width();
    const int stripRows = std::max(8, int(65536 / std::max(1, width)));
    struct Strip { int y0, y1; };
    std::vector<Strip> strips;
    for (int y = 0; y < dest.height(); y += stripRows)
        strips.push_back({y, std::min(dest.height(), y + stripRows)});

    auto work = [&](const Strip &strip) {
        const int rows = strip.y1 - strip.y0;
        uchar *first = bits + strip.y0 * bpl;
        for (int r = 0; r < rows; ++r)
            std::memset(first + r * bpl, 0, size_t(width) * 4);
        QImage view(first, width, rows, bpl, QImage::Format_ARGB32_Premultiplied);
        const QRect stripRect(0, strip.y0, width, rows);
        QPainter painter;
        for (const Item &item : items) {
            const QRect part = item.area.intersected(stripRect);
            if (part.isEmpty()) continue;
            if (!painter.isActive()) painter.begin(&view);
            item.layer->compositeInto(painter, view, origin + QPoint(0, strip.y0),
                                      part.translated(0, -strip.y0), shift);
        }
    };

    // Small areas (a brush dab while zoomed in) are quicker without threads.
    if (strips.size() < 2 || qint64(width) * dest.height() < 128 * 1024) {
        for (const Strip &strip : strips) work(strip);
    } else {
        QtConcurrent::blockingMap(strips, work);
    }
}

bool Document::isLayeredFormat(const QString &filePath) {
    return QFileInfo(filePath).suffix().compare(QLatin1String("psd"), Qt::CaseInsensitive) == 0;
}

bool Document::loadPsd(const QString &filePath) {
    Psd::ImageData psd;
    QString error;
    if (!Psd::read(filePath, psd, &error)) {
        m_lastError = error;
        return false;
    }
    std::vector<std::shared_ptr<Layer>> layers;
    layers.reserve(psd.layers.size());
    for (Psd::LayerData &data : psd.layers) {
        auto layer = std::make_shared<Layer>(data.image, data.name);
        layer->setOffset(data.offset);
        layer->setRetainsOffCanvasPixels(data.retainsOffCanvasPixels);
        layer->setVisible(data.visible);
        layer->setLocked(data.locked);
        layer->setOpacity(data.opacity);
        layer->setBlendMode(data.blendMode);
        layers.push_back(layer);
    }

    m_width = psd.width;
    m_height = psd.height;
    m_layers = std::move(layers);
    m_activeLayer = psd.activeLayer >= 0 ? psd.activeLayer : static_cast<int>(m_layers.size()) - 1;
    if (psd.dpi > 0) m_dpi = psd.dpi;
    m_loadNotes = psd.notes;
    m_selection = Selection(m_width, m_height);
    m_history.clear();
    m_filePath = filePath;
    m_modified = false;

    emit layersChanged();
    emit activeLayerChanged(m_activeLayer);
    emit sizeChanged(m_width, m_height);
    emit documentChanged();
    return true;
}

bool Document::isOpenOnlyFormat(const QString &filePath) {
    const QString suffix = QFileInfo(filePath).suffix();
    return suffix.compare(QLatin1String("pdn"), Qt::CaseInsensitive) == 0
        || suffix.compare(QLatin1String("psb"), Qt::CaseInsensitive) == 0;
}

bool Document::loadPdn(const QString &filePath) {
    Pdn::ImageData pdn;
    if (!Pdn::read(filePath, pdn)) return false;

    std::vector<std::shared_ptr<Layer>> layers;
    layers.reserve(pdn.layers.size());
    for (Pdn::LayerData &data : pdn.layers) {
        auto layer = std::make_shared<Layer>(data.image, data.name);
        layer->setVisible(data.visible);
        layer->setOpacity(data.opacity);
        layer->setBlendMode(data.blendMode);
        layers.push_back(layer);
    }

    m_width = pdn.width;
    m_height = pdn.height;
    m_layers = std::move(layers);
    m_activeLayer = static_cast<int>(m_layers.size()) - 1;   // Paint.NET opens on the top layer
    if (pdn.dpi > 0) m_dpi = pdn.dpi;
    m_selection = Selection(m_width, m_height);
    m_history.clear();
    m_filePath = filePath;
    m_modified = false;

    emit layersChanged();
    emit activeLayerChanged(m_activeLayer);
    emit sizeChanged(m_width, m_height);
    emit documentChanged();
    return true;
}

bool Document::save(const QString &filePath, int quality) {
    m_lastError.clear();
    if (isLayeredFormat(filePath)) {
        std::vector<Layer> layers;
        layers.reserve(m_layers.size());
        for (const auto &l : m_layers) layers.push_back(*l);   // pixels are shared, not copied
        Psd::WriteOptions options;
        options.dpi = m_dpi;
        options.activeLayer = m_activeLayer;
        if (!Psd::write(filePath, layers, size(), options, &m_lastError)) return false;
        m_filePath = filePath;
        m_modified = false;
        return true;
    }

    // Remember an explicit quality; a later plain Save (quality == -1) reuses it.
    if (quality >= 0) m_saveQuality = quality;
    else quality = m_saveQuality;

    QImage flat = flatten();

    // Formats without an alpha channel need an opaque background so
    // transparent areas don't turn black; formats that support alpha
    // (PNG, WebP, TIFF...) keep full transparency.
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    static const QStringList alphaFormats = {"png", "webp", "tiff", "tif", "ico", "gif"};
    if (!alphaFormats.contains(suffix)) {
        QImage opaque(flat.size(), QImage::Format_RGB32);
        opaque.fill(Qt::white);
        QPainter p(&opaque);
        p.drawImage(0, 0, flat);
        p.end();
        flat = opaque;
    }
    const int dotsPerMeter = qRound(m_dpi / 0.0254);
    flat.setDotsPerMeterX(dotsPerMeter);
    flat.setDotsPerMeterY(dotsPerMeter);

    // Written next to the old file and swapped in at the end.
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_lastError = file.errorString();
        return false;
    }
    QImageWriter writer(&file, suffix.toLatin1());
    if (quality >= 0) writer.setQuality(quality);
    if (!writer.write(flat)) {
        m_lastError = writer.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        m_lastError = file.errorString();
        return false;
    }
    m_filePath = filePath;
    m_modified = false;
    return true;
}

bool Document::load(const QString &filePath) {
    m_loadNotes.clear();
    m_lastError.clear();
    if (Psd::isPsdFile(filePath))
        return loadPsd(filePath);
    if (Pdn::isPdnFile(filePath))
        return loadPdn(filePath);

    QImageReader reader(filePath);
    QImage image = reader.read();
    if (image.isNull()) {
        m_lastError = reader.errorString();
        return false;
    }
    if (image.dotsPerMeterX() > 0) m_dpi = qRound(image.dotsPerMeterX() * 0.0254 * 100.0) / 100.0;

    m_width = image.width();
    m_height = image.height();
    m_layers.clear();
    auto layer = std::make_shared<Layer>(image, "Background");
    m_layers.push_back(layer);
    m_activeLayer = 0;
    m_selection = Selection(m_width, m_height);
    m_history.clear();
    m_filePath = filePath;
    m_modified = false;

    emit layersChanged();
    emit activeLayerChanged(0);
    emit sizeChanged(m_width, m_height);
    emit documentChanged();
    return true;
}

std::vector<std::shared_ptr<Layer>> Document::snapshotLayers() const {
    std::vector<std::shared_ptr<Layer>> copy;
    copy.reserve(m_layers.size());
    for (const auto &l : m_layers)
        copy.push_back(std::make_shared<Layer>(*l));
    return copy;
}

void Document::restoreFromSnapshot(std::vector<std::shared_ptr<Layer>> layers,
                                   int width, int height, int activeLayer) {
    m_layers = std::move(layers);
    bool sizeChangedFlag = (width != m_width || height != m_height);
    m_width = width;
    m_height = height;
    if (sizeChangedFlag)
        m_selection.resize(width, height);
    m_activeLayer = qBound(0, activeLayer, static_cast<int>(m_layers.size()) - 1);
    m_modified = true;
    if (sizeChangedFlag)
        emit sizeChanged(m_width, m_height);
    emit layersChanged();
    emit activeLayerChanged(m_activeLayer);
    emit documentChanged();
}

void Document::cropTo(const QRect &rect) {
    QRect r = rect.intersected(QRect(0, 0, m_width, m_height));
    if (r.isEmpty()) return;
    auto cmd = std::make_unique<DocumentSnapshotCommand>(this, "Crop to Selection");

    for (auto &layer : m_layers) {
        // The crop is in document space, not the retained bitmap's local space.
        layer->setImage(layer->image().copy(r.translated(-layer->offset())));
        layer->setOffset(QPoint());
        // Crop deliberately discards the outside bitmap, including its retention state.
        layer->setRetainsOffCanvasPixels(false);
    }
    m_width = r.width();
    m_height = r.height();
    m_selection.clear();
    m_selection.resize(m_width, m_height);

    cmd->captureAfter();
    m_history.push(std::move(cmd));
    m_modified = true;
    emit sizeChanged(m_width, m_height);
    emit layersChanged();
    emit documentChanged();
}

void Document::rotate(int quarterTurns, const QString &description) {
    quarterTurns = ((quarterTurns % 4) + 4) % 4;
    if (quarterTurns == 0) return;
    auto cmd = std::make_unique<DocumentSnapshotCommand>(this, description);

    QTransform t;
    t.rotate(90.0 * quarterTurns);
    for (auto &layer : m_layers) {
        const QPoint origin = layer->offset();
        const QSize buffer = layer->image().size();
        QPoint rotatedOrigin;
        if (quarterTurns == 1)
            rotatedOrigin = QPoint(m_height - origin.y() - buffer.height(), origin.x());
        else if (quarterTurns == 2)
            rotatedOrigin = QPoint(m_width - origin.x() - buffer.width(),
                                   m_height - origin.y() - buffer.height());
        else
            rotatedOrigin = QPoint(origin.y(), m_width - origin.x() - buffer.width());
        layer->setImage(layer->image().transformed(t));
        layer->setOffset(rotatedOrigin);
    }

    // 90° / 270° swap width and height; 180° keeps them.
    if (quarterTurns == 1 || quarterTurns == 3)
        std::swap(m_width, m_height);
    m_selection.clear();
    m_selection.resize(m_width, m_height);

    cmd->captureAfter();
    m_history.push(std::move(cmd));
    m_modified = true;
    emit sizeChanged(m_width, m_height);
    emit layersChanged();
    emit documentChanged();
}

void Document::flip(bool horizontal, const QString &description) {
    auto cmd = std::make_unique<DocumentSnapshotCommand>(this, description);
    for (auto &layer : m_layers) {
        const QPoint origin = layer->offset();
        layer->setOffset(horizontal ? QPoint(m_width - origin.x() - layer->width(), origin.y())
                                    : QPoint(origin.x(), m_height - origin.y() - layer->height()));
        layer->setImage(layer->image().mirrored(horizontal, !horizontal));
    }
    // Flipping keeps the dimensions; drop the selection so its mask can't go stale.
    m_selection.clear();
    m_selection.resize(m_width, m_height);

    cmd->captureAfter();
    m_history.push(std::move(cmd));
    m_modified = true;
    emit layersChanged();
    emit documentChanged();
}

void Document::reorderLayers(const std::vector<int> &newOrder, int keepActiveOriginalIndex) {
    if (newOrder.size() != m_layers.size()) return;
    // Validate it's a permutation.
    std::vector<bool> seen(m_layers.size(), false);
    for (int i : newOrder) {
        if (i < 0 || i >= static_cast<int>(m_layers.size()) || seen[i]) return;
        seen[i] = true;
    }
    // No-op if already in this order.
    bool identical = true;
    for (size_t i = 0; i < newOrder.size(); ++i)
        if (newOrder[i] != static_cast<int>(i)) { identical = false; break; }
    if (identical) return;

    auto cmd = std::make_unique<DocumentSnapshotCommand>(this, "Reorder Layers");
    std::vector<std::shared_ptr<Layer>> reordered;
    reordered.reserve(m_layers.size());
    int newActive = 0;
    for (size_t i = 0; i < newOrder.size(); ++i) {
        reordered.push_back(m_layers[newOrder[i]]);
        if (newOrder[i] == keepActiveOriginalIndex)
            newActive = static_cast<int>(i);
    }
    m_layers = std::move(reordered);
    m_activeLayer = newActive;
    cmd->captureAfter();
    m_history.push(std::move(cmd));
    m_modified = true;
    emit layersChanged();
    emit activeLayerChanged(m_activeLayer);
    emit documentChanged();
}

void Document::setLayerOpacity(int index, float opacity) {
    if (auto *l = layerAt(index)) {
        l->setOpacity(opacity);
        m_modified = true;
        emit documentChanged();
        emit layersChanged();
    }
}

void Document::setLayerVisibility(int index, bool visible) {
    if (auto *l = layerAt(index)) {
        l->setVisible(visible);
        m_modified = true;
        emit documentChanged();
        emit layersChanged();
    }
}

void Document::setLayerBlendMode(int index, BlendMode mode) {
    if (auto *l = layerAt(index)) {
        l->setBlendMode(mode);
        m_modified = true;
        emit documentChanged();
        emit layersChanged();
    }
}

void Document::setLayerName(int index, const QString &name) {
    if (auto *l = layerAt(index)) {
        l->setName(name);
        m_modified = true;
        emit layersChanged();
    }
}

void Document::setLayerLocked(int index, bool locked) {
    if (auto *l = layerAt(index)) {
        l->setLocked(locked);
        emit layersChanged();
    }
}

void Document::setPrimaryColor(const QColor &color) {
    m_primaryColor = color;
    emit primaryColorChanged(color);
}

void Document::setSecondaryColor(const QColor &color) {
    m_secondaryColor = color;
    emit secondaryColorChanged(color);
}

void Document::swapColors() {
    std::swap(m_primaryColor, m_secondaryColor);
    emit primaryColorChanged(m_primaryColor);
    emit secondaryColorChanged(m_secondaryColor);
}
