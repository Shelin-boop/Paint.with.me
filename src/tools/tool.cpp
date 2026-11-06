#include "tool.h"
#include "tools/toolregistry.h"
#include "core/document.h"
#include "core/selection.h"
#include "core/hatchpatterns.h"
#include "core/pensettings.h"
#include <QPainter>
#include <QImage>
#include <cstdlib>

QString toolTypeKey(ToolType type) {
    return QString::fromLatin1(toolInfo(type).key);
}

bool toolTypeFromKey(const QString &key, ToolType *type) {
    for (const ToolInfo &info : toolInfos()) {
        if (key == QLatin1String(info.key)) {
            if (type) *type = info.type;
            return true;
        }
    }
    return false;
}

QString Tool::pressureTargetKey(PressureTarget target) {
    switch (target) {
    case PressureTarget::Opacity:        return QStringLiteral("opacity");
    case PressureTarget::SizeAndOpacity: return QStringLiteral("size-and-opacity");
    case PressureTarget::Size:           break;
    }
    return QStringLiteral("size");
}

PressureTarget Tool::pressureTargetFromKey(const QString &key) {
    if (key == QLatin1String("opacity")) return PressureTarget::Opacity;
    if (key == QLatin1String("size-and-opacity")) return PressureTarget::SizeAndOpacity;
    return PressureTarget::Size;
}

Dab Tool::dabAt(const QPointF &pos, double pressure) const {
    Dab dab;
    dab.pos = pos;
    dab.radius = (m_brushSize / 2.0) * (pressureSetsSize() ? pressure : 1.0);
    dab.alpha = (m_stylus && pressureSetsOpacity()) ? pressure : 1.0;
    if (m_stylus && Pen::settings().tiltFlattens) {
        const Pen::TipShape tip = Pen::tipForTilt(m_tilt);
        dab.angle = tip.angle;
        dab.roundness = tip.roundness;
    }
    return dab;
}

void Tool::forEachDab(const QPointF &from, const QPointF &to, double fromPressure,
                      double toPressure, const std::function<void(const Dab &)> &paint) const {
    const double dx = to.x() - from.x();
    const double dy = to.y() - from.y();
    const double dist = std::sqrt(dx * dx + dy * dy);
    const double spacing = std::max(1.0, m_brushSize * (m_spacing / 100.0));
    const int steps = std::max(1, static_cast<int>(dist / spacing));
    for (int i = 1; i <= steps; ++i) {
        const double t = double(i) / steps;
        paint(dabAt(QPointF(from.x() + dx * t, from.y() + dy * t),
                    fromPressure + (toPressure - fromPressure) * t));
    }
}

QRect Tool::paintDab(QImage &buffer, const Dab &dab, const QColor &opaqueColor, int hardness,
                     int fillStyle, bool antialiased, bool peak, QImage &scratch) {
    if (dab.radius < 0.4 || dab.alpha <= 0.0) return QRect();   // no pressure, no mark
    const double r = dab.radius;
    // Include the rasterizer's antialias fringe, including fractional centers.
    const QRect area = QRectF(dab.pos.x() - r - 2, dab.pos.y() - r - 2, 2 * r + 4, 2 * r + 4)
                           .toAlignedRect().intersected(buffer.rect());
    if (area.isEmpty()) return area;
    const bool round = dab.roundness >= 0.999;

    // Draws the dab with `painter` at `center`, in `color`.
    auto draw = [&](QPainter &painter, const QPointF &center, const QColor &color) {
        if (antialiased) painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        QPointF c = center;
        if (!round) {
            // Flattened tip: an ellipse, long along the tilt, narrow across it.
            painter.translate(center);
            painter.rotate(dab.angle);
            painter.scale(1.0, dab.roundness);
            c = QPointF();
        }
        if (fillStyle > 0) {
            // Patterned brush (Fill Style): pattern lines in the color,
            // transparent between them, with a hard edge.
            painter.setBrush(Hatch::brush(fillStyle, color, QColor(Qt::transparent)));
        } else if (hardness >= 95) {
            painter.setBrush(color);
        } else {
            QRadialGradient gradient(c, r);
            gradient.setColorAt(0, color);
            gradient.setColorAt(hardness / 100.0, color);
            QColor transparent = color;
            transparent.setAlpha(0);
            gradient.setColorAt(1, transparent);
            painter.setBrush(gradient);
        }
        painter.drawEllipse(c, r, r);
    };

    if (!peak) {
        QPainter painter(&buffer);
        if (dab.alpha < 1.0) painter.setOpacity(dab.alpha);
        draw(painter, dab.pos, opaqueColor);
        return area;
    }

    // Peak coverage: paint the dab alone, then keep the higher coverage.
    if (scratch.width() < area.width() || scratch.height() < area.height())
        scratch = QImage(area.size().expandedTo(scratch.size()), QImage::Format_ARGB32_Premultiplied);
    {
        QPainter painter(&scratch);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(QRect(QPoint(), area.size()), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        draw(painter, dab.pos - area.topLeft(), QColor(0, 0, 0));
    }
    const int cr = opaqueColor.red(), cg = opaqueColor.green(), cb = opaqueColor.blue();
    const int scale = qRound(qBound(0.0, dab.alpha, 1.0) * 256);
    for (int y = 0; y < area.height(); ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(scratch.constScanLine(y));
        QRgb *dst = reinterpret_cast<QRgb *>(buffer.scanLine(y + area.y())) + area.x();
        for (int x = 0; x < area.width(); ++x) {
            const int a = (qAlpha(src[x]) * scale) >> 8;
            if (a <= qAlpha(dst[x])) continue;
            dst[x] = qRgba((cr * a + 127) / 255, (cg * a + 127) / 255, (cb * a + 127) / 255, a);
        }
    }
    return area;
}

QImage Tool::transparentImage(const QSize &size) {
    if (size.isEmpty()) return QImage();
    const qsizetype bpl = qsizetype(size.width()) * 4;
    void *data = std::calloc(size_t(bpl) * size_t(size.height()), 1);
    if (!data) return QImage();
    return QImage(static_cast<uchar *>(data), size.width(), size.height(), bpl,
                  QImage::Format_ARGB32_Premultiplied,
                  [](void *p) { std::free(p); }, data);
}

void Tool::clipToSelection(QPainter &painter, Document *doc) {
    if (!doc) return;
    if (auto *layer = doc->activeLayer()) {
        painter.translate(-layer->offset());
        if (layer->image().size() != doc->size() || !layer->offset().isNull())
            painter.setClipRect(QRect(QPoint(), doc->size()));
    }
    const Selection &sel = doc->selection();
    if (!sel.hasSelection()) return;
    QRegion region = sel.region();
    if (!region.isEmpty())
        painter.setClipRegion(region, Qt::IntersectClip);
}

QPointF Tool::imagePoint(const QPointF &documentPoint, Document *doc) {
    return documentPoint - (doc && doc->activeLayer() ? doc->activeLayer()->offset() : QPoint());
}

bool Tool::imageSelectionAllows(Document *doc, int x, int y) {
    if (!doc) return true;
    const QPoint offset = doc->activeLayer() ? doc->activeLayer()->offset() : QPoint();
    const QPoint point = QPoint(x, y) + offset;
    return QRect(QPoint(), doc->size()).contains(point) && selectionAllows(doc, point.x(), point.y());
}

QPainter::CompositionMode Tool::compositionModeFor(int blendIndex) {
    // Maps the blend-mode combo (Layer::allBlendModes order) to a Qt mode.
    switch (blendIndex) {
    case 1:  return QPainter::CompositionMode_Multiply;   // Multiply
    case 2:  return QPainter::CompositionMode_Plus;        // Additive
    case 3:  return QPainter::CompositionMode_ColorBurn;
    case 4:  return QPainter::CompositionMode_ColorDodge;
    case 5:  return QPainter::CompositionMode_Darken;
    case 6:  return QPainter::CompositionMode_Difference;
    case 7:  return QPainter::CompositionMode_ColorDodge;  // Glow    (approximation)
    case 8:  return QPainter::CompositionMode_Lighten;
    case 9:  return QPainter::CompositionMode_Difference;  // Negation (approximation)
    case 10: return QPainter::CompositionMode_Overlay;
    case 11: return QPainter::CompositionMode_Screen;      // Reflect (approximation)
    case 12: return QPainter::CompositionMode_Screen;
    case 13: return QPainter::CompositionMode_Xor;         // Xor
    case 14: return QPainter::CompositionMode_Source;      // Overwrite (callers replace per-pixel)
    default: return QPainter::CompositionMode_SourceOver;  // Normal + unsupported
    }
}

bool Tool::selectionAllows(Document *doc, int x, int y) {
    if (!doc) return true;
    const Selection &sel = doc->selection();
    if (!sel.hasSelection()) return true;
    return sel.isSelected(x, y);
}

QImage Tool::maskEditToSelection(Document *doc, const QImage &before, const QImage &after) {
    if (!doc) return after;
    const Selection &sel = doc->selection();
    if (!sel.hasSelection()) return after;

    QImage result = before.convertToFormat(QImage::Format_ARGB32);
    QImage src = after.convertToFormat(QImage::Format_ARGB32);
    const int w = result.width(), h = result.height();
    for (int y = 0; y < h; ++y) {
        QRgb *dst = reinterpret_cast<QRgb*>(result.scanLine(y));
        const QRgb *s = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            int cov = sel.selectionValue(x, y);
            if (cov == 0) continue;
            if (cov == 255) { dst[x] = s[x]; continue; }
            // Partial (feathered) coverage: blend before/after per channel.
            QRgb b = dst[x];
            int ia = 255 - cov;
            dst[x] = qRgba(
                (qRed(s[x]) * cov + qRed(b) * ia) / 255,
                (qGreen(s[x]) * cov + qGreen(b) * ia) / 255,
                (qBlue(s[x]) * cov + qBlue(b) * ia) / 255,
                (qAlpha(s[x]) * cov + qAlpha(b) * ia) / 255);
        }
    }
    return result.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}
