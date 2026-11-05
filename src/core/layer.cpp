#include "layer.h"
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

Layer::Layer(int width, int height, const QString &name)
    : m_image(width, height, QImage::Format_ARGB32_Premultiplied)
    , m_name(name)
{
    m_image.fill(Qt::transparent);
}

Layer::Layer(const QImage &image, const QString &name)
    : m_image(image.convertToFormat(QImage::Format_ARGB32_Premultiplied))
    , m_name(name)
{
}

Layer::Layer(const Layer &other)
    : m_image(other.m_image)
    , m_name(other.m_name)
    , m_opacity(other.m_opacity)
    , m_visible(other.m_visible)
    , m_locked(other.m_locked)
    , m_blendMode(other.m_blendMode)
    , m_offset(other.m_offset)
    , m_retainsOffCanvasPixels(other.m_retainsOffCanvasPixels)
{
}

Layer &Layer::operator=(const Layer &other) {
    if (this != &other) {
        m_image = other.m_image;
        m_name = other.m_name;
        m_opacity = other.m_opacity;
        m_visible = other.m_visible;
        m_locked = other.m_locked;
        m_blendMode = other.m_blendMode;
        m_offset = other.m_offset;
        m_retainsOffCanvasPixels = other.m_retainsOffCanvasPixels;
    }
    return *this;
}

void Layer::resize(int width, int height) {
    QImage newImage(width, height, QImage::Format_ARGB32_Premultiplied);
    newImage.fill(Qt::transparent);
    QPainter painter(&newImage);
    painter.drawImage(0, 0, m_image);
    m_image = newImage;
}

void Layer::clear(const QColor &color) {
    m_image.fill(color);
}

namespace {

bool isPerPixelMode(BlendMode mode) {
    // Paint.NET blend modes that Qt has no composition mode for.
    return mode == BlendMode::Glow || mode == BlendMode::Reflect
        || mode == BlendMode::Negation || mode == BlendMode::Xor;
}

QPainter::CompositionMode qtMode(BlendMode mode) {
    switch (mode) {
    case BlendMode::Normal:     return QPainter::CompositionMode_SourceOver;
    case BlendMode::Multiply:   return QPainter::CompositionMode_Multiply;
    case BlendMode::Additive:   return QPainter::CompositionMode_Plus;
    case BlendMode::ColorBurn:  return QPainter::CompositionMode_ColorBurn;
    case BlendMode::ColorDodge: return QPainter::CompositionMode_ColorDodge;
    case BlendMode::Darken:     return QPainter::CompositionMode_Darken;
    case BlendMode::Difference: return QPainter::CompositionMode_Difference;
    case BlendMode::Lighten:    return QPainter::CompositionMode_Lighten;
    case BlendMode::Overlay:    return QPainter::CompositionMode_Overlay;
    case BlendMode::Screen:     return QPainter::CompositionMode_Screen;
    case BlendMode::Overwrite:  return QPainter::CompositionMode_Source;
    // Per-pixel modes never get here; Qt's CompositionMode_Xor is a
    // Porter-Duff alpha XOR, not Paint.NET's colour XOR.
    case BlendMode::Glow: case BlendMode::Reflect:
    case BlendMode::Negation: case BlendMode::Xor:
        break;
    }
    return QPainter::CompositionMode_SourceOver;
}

int reflect(int b, int s) {
    if (s >= 255) return 255;
    return std::min(255, b * b / (255 - s));
}

} // namespace

QImage Layer::composited(const QImage &below) const {
    if (!m_visible || m_opacity <= 0.0f)
        return below;
    QImage result = below.copy();
    compositeInto(result);
    return result;
}

void Layer::compositeInto(QImage &dest, const QPoint &destOrigin) const {
    if (!m_visible || m_opacity <= 0.0f || m_image.isNull() || dest.isNull())
        return;
    QPainter painter(&dest);
    compositeInto(painter, dest, destOrigin, dest.rect());
}

QRect Layer::reducedRect(const QRect &rect, int shift) {
    if (shift <= 0 || rect.isEmpty()) return rect;
    // Arithmetic shifts round toward minus infinity, also for negative offsets.
    return QRect(QPoint((rect.left() >> shift) - 1, (rect.top() >> shift) - 1),
                 QPoint((rect.right() >> shift) + 1, (rect.bottom() >> shift) + 1));
}

void Layer::compositeInto(QPainter &painter, QImage &dest, const QPoint &destOrigin,
                          const QRect &limit, int shift) const {
    if (!m_visible || m_opacity <= 0.0f || m_image.isNull())
        return;
    if (shift > 0) {
        compositeReduced(painter, dest, destOrigin, limit, shift);
        return;
    }
    // Where this layer lands in `dest`, and the matching part of the layer.
    const QRect target = QRect(m_offset - destOrigin, m_image.size())
                             .intersected(dest.rect()).intersected(limit);
    if (target.isEmpty())
        return;
    const QRect source = target.translated(destOrigin - m_offset);

    if (!isPerPixelMode(m_blendMode)) {
        painter.setOpacity(m_opacity);
        painter.setCompositionMode(qtMode(m_blendMode));
        painter.drawImage(target.topLeft(), m_image, source);
        return;
    }

    // Per-pixel modes work on straight (unpremultiplied) colours.
    const QImage top = m_image.copy(source).convertToFormat(QImage::Format_ARGB32);
    blendPerPixel(painter, dest, target, top);
}

void Layer::blendPerPixel(QPainter &painter, const QImage &dest, const QRect &target,
                          const QImage &top) const {
    // "blended" holds the blend result with the top layer's alpha; drawn
    // SourceOver so transparent top pixels leave the base untouched.
    const QImage base = dest.copy(target).convertToFormat(QImage::Format_ARGB32);
    QImage blended(target.size(), QImage::Format_ARGB32);
    blended.fill(Qt::transparent);
    for (int y = 0; y < target.height(); ++y) {
        const QRgb *topLine = reinterpret_cast<const QRgb *>(top.constScanLine(y));
        const QRgb *baseLine = reinterpret_cast<const QRgb *>(base.constScanLine(y));
        QRgb *outLine = reinterpret_cast<QRgb *>(blended.scanLine(y));
        for (int x = 0; x < target.width(); ++x) {
            const QRgb s = topLine[x];
            if (qAlpha(s) == 0) continue;
            const QRgb b = baseLine[x];
            int rr, gg, bb;
            switch (m_blendMode) {
            case BlendMode::Reflect:
                rr = reflect(qRed(b), qRed(s));
                gg = reflect(qGreen(b), qGreen(s));
                bb = reflect(qBlue(b), qBlue(s));
                break;
            case BlendMode::Glow:  // inverse of reflect (base/source swapped)
                rr = reflect(qRed(s), qRed(b));
                gg = reflect(qGreen(s), qGreen(b));
                bb = reflect(qBlue(s), qBlue(b));
                break;
            case BlendMode::Xor:  // Paint.NET: per-channel bitwise XOR of colours
                rr = qRed(b)   ^ qRed(s);
                gg = qGreen(b) ^ qGreen(s);
                bb = qBlue(b)  ^ qBlue(s);
                break;
            case BlendMode::Negation:
            default:
                rr = 255 - std::abs(255 - qRed(b) - qRed(s));
                gg = 255 - std::abs(255 - qGreen(b) - qGreen(s));
                bb = 255 - std::abs(255 - qBlue(b) - qBlue(s));
                break;
            }
            // Where the base is (partly) transparent the blend has nothing to
            // work against, so fade toward the source colour by the base alpha.
            // Otherwise these modes render black fringes over transparent areas.
            const double bA = qAlpha(b) / 255.0;
            rr = static_cast<int>(qRed(s)   * (1.0 - bA) + rr * bA + 0.5);
            gg = static_cast<int>(qGreen(s) * (1.0 - bA) + gg * bA + 0.5);
            bb = static_cast<int>(qBlue(s)  * (1.0 - bA) + bb * bA + 0.5);
            outLine[x] = qRgba(qBound(0, rr, 255), qBound(0, gg, 255),
                               qBound(0, bb, 255), qAlpha(s));
        }
    }
    painter.setOpacity(m_opacity);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.drawImage(target.topLeft(), blended.convertToFormat(QImage::Format_ARGB32_Premultiplied));
}

namespace {
// Per-channel average of four premultiplied pixels, rounded to nearest.
inline quint32 average4(quint32 a, quint32 b, quint32 c, quint32 d) {
    const quint32 rb = (a & 0x00ff00ffu) + (b & 0x00ff00ffu) + (c & 0x00ff00ffu)
                     + (d & 0x00ff00ffu) + 0x00020002u;
    const quint32 ag = ((a >> 8) & 0x00ff00ffu) + ((b >> 8) & 0x00ff00ffu)
                     + ((c >> 8) & 0x00ff00ffu) + ((d >> 8) & 0x00ff00ffu) + 0x00020002u;
    return ((rb >> 2) & 0x00ff00ffu) | (((ag >> 2) & 0x00ff00ffu) << 8);
}
} // namespace

void Layer::compositeReduced(QPainter &painter, QImage &dest, const QPoint &destOrigin,
                             const QRect &limit, int shift) const {
    const int s = 1 << shift;
    const QRect layerRect(m_offset, m_image.size());   // document pixels
    const QRect target = reducedRect(layerRect, shift).translated(-destOrigin)
                             .intersected(dest.rect()).intersected(limit);
    if (target.isEmpty())
        return;

    // Each reduced pixel is the average of the 2 x 2 document pixels at the
    // centre of the s x s block it stands for (the same as smooth sampling
    // there). Plain integer arithmetic, so a pixel comes out identical whether
    // it is combined as part of the whole image or of a small changed area.
    // Pixels outside the layer count as transparent.
    QImage small(target.size(), QImage::Format_ARGB32_Premultiplied);
    const int w = m_image.width(), h = m_image.height();
    const int half = s / 2;
    std::vector<int> xs(size_t(target.width()) * 2);
    for (int x = 0; x < target.width(); ++x) {
        const int lx = (target.left() + x + destOrigin.x()) * s + half - 1 - m_offset.x();
        xs[size_t(x) * 2] = lx;
        xs[size_t(x) * 2 + 1] = lx + 1;
    }
    for (int y = 0; y < target.height(); ++y) {
        const int ly = (target.top() + y + destOrigin.y()) * s + half - 1 - m_offset.y();
        const quint32 *row0 = (ly >= 0 && ly < h)
            ? reinterpret_cast<const quint32 *>(m_image.constScanLine(ly)) : nullptr;
        const quint32 *row1 = (ly + 1 >= 0 && ly + 1 < h)
            ? reinterpret_cast<const quint32 *>(m_image.constScanLine(ly + 1)) : nullptr;
        quint32 *out = reinterpret_cast<quint32 *>(small.scanLine(y));
        for (int x = 0; x < target.width(); ++x) {
            const int x0 = xs[size_t(x) * 2], x1 = xs[size_t(x) * 2 + 1];
            const bool in0 = x0 >= 0 && x0 < w, in1 = x1 >= 0 && x1 < w;
            out[x] = average4(row0 && in0 ? row0[x0] : 0u, row0 && in1 ? row0[x1] : 0u,
                              row1 && in0 ? row1[x0] : 0u, row1 && in1 ? row1[x1] : 0u);
        }
    }

    if (isPerPixelMode(m_blendMode)) {
        blendPerPixel(painter, dest, target, small.convertToFormat(QImage::Format_ARGB32));
        return;
    }
    painter.setOpacity(m_opacity);
    painter.setCompositionMode(qtMode(m_blendMode));
    painter.drawImage(target.topLeft(), small);
}

QRect Layer::contentBounds() const {
    if (m_image.isNull()) return QRect();
    const qint64 key = m_image.cacheKey();
    if (key == m_boundsKey) return m_bounds;
    // Premultiplied ARGB: a fully transparent pixel is all zero bits.
    const int w = m_image.width(), h = m_image.height();
    auto rowEmpty = [&](int y, int from, int to) {
        const quint32 *line = reinterpret_cast<const quint32 *>(m_image.constScanLine(y));
        for (int x = from; x < to; ++x)
            if (line[x]) return false;
        return true;
    };
    int top = 0;
    while (top < h && rowEmpty(top, 0, w)) ++top;
    QRect bounds;
    if (top < h) {
        int bottom = h - 1;
        while (bottom > top && rowEmpty(bottom, 0, w)) --bottom;
        int left = w, right = -1;
        for (int y = top; y <= bottom; ++y) {
            const quint32 *line = reinterpret_cast<const quint32 *>(m_image.constScanLine(y));
            int x = 0;
            while (x < left && !line[x]) ++x;
            left = std::min(left, x);
            int r = w - 1;
            while (r > right && !line[r]) --r;
            right = std::max(right, r);
        }
        bounds = QRect(QPoint(left, top), QPoint(right, bottom));
    }
    m_bounds = bounds;
    m_boundsKey = key;
    return bounds;
}

QString Layer::blendModeName(BlendMode mode) {
    switch (mode) {
    case BlendMode::Normal: return "Normal";
    case BlendMode::Multiply: return "Multiply";
    case BlendMode::Additive: return "Additive";
    case BlendMode::ColorBurn: return "Color Burn";
    case BlendMode::ColorDodge: return "Color Dodge";
    case BlendMode::Darken: return "Darken";
    case BlendMode::Difference: return "Difference";
    case BlendMode::Glow: return "Glow";
    case BlendMode::Lighten: return "Lighten";
    case BlendMode::Negation: return "Negation";
    case BlendMode::Overlay: return "Overlay";
    case BlendMode::Reflect: return "Reflect";
    case BlendMode::Screen: return "Screen";
    case BlendMode::Xor: return "Xor";
    case BlendMode::Overwrite: return "Overwrite";
    }
    return "Normal";
}

QList<BlendMode> Layer::allBlendModes() {
    return {
        BlendMode::Normal, BlendMode::Multiply, BlendMode::Additive,
        BlendMode::ColorBurn, BlendMode::ColorDodge, BlendMode::Darken,
        BlendMode::Difference, BlendMode::Glow, BlendMode::Lighten,
        BlendMode::Negation, BlendMode::Overlay, BlendMode::Reflect,
        BlendMode::Screen, BlendMode::Xor, BlendMode::Overwrite
    };
}
