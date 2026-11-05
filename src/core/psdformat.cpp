#include "core/psdformat.h"

#include "core/document.h"

#include <QBuffer>
#include <QFile>
#include <QHash>
#include <QSaveFile>
#include <QSet>
#include <QtConcurrent>
#include <QtEndian>
#include <zlib.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <functional>

namespace Psd {
namespace {

// ===================== Blend modes =====================

struct ModeKey { BlendMode mode; const char *key; };
const ModeKey kModeKeys[] = {
    {BlendMode::Normal, "norm"},     {BlendMode::Multiply, "mul "},
    {BlendMode::Additive, "lddg"},   {BlendMode::ColorBurn, "idiv"},
    {BlendMode::ColorDodge, "div "}, {BlendMode::Darken, "dark"},
    {BlendMode::Difference, "diff"}, {BlendMode::Lighten, "lite"},
    {BlendMode::Overlay, "over"},    {BlendMode::Screen, "scrn"},
};

// Photoshop's modes that have no counterpart here, for the notes.
QString photoshopModeName(const QByteArray &key) {
    static const QHash<QByteArray, QString> names = {
        {"pass", "Pass Through"}, {"diss", "Dissolve"},     {"lbrn", "Linear Burn"},
        {"dkCl", "Darker Color"}, {"lgCl", "Lighter Color"}, {"sLit", "Soft Light"},
        {"hLit", "Hard Light"},   {"vLit", "Vivid Light"},  {"lLit", "Linear Light"},
        {"pLit", "Pin Light"},    {"hMix", "Hard Mix"},     {"smud", "Exclusion"},
        {"fsub", "Subtract"},     {"fdiv", "Divide"},       {"hue ", "Hue"},
        {"sat ", "Saturation"},   {"colr", "Color"},        {"lum ", "Luminosity"},
    };
    return names.value(key, QString::fromLatin1(key).trimmed());
}

// "a", "a and b", "a, b, c and 4 more"
QString nameList(const QStringList &names) {
    QStringList shown;
    for (const QString &n : names) shown << QStringLiteral("\"%1\"").arg(n);
    const int extra = int(shown.size()) - 4;
    if (extra > 1) {
        shown = shown.mid(0, 4);
        return shown.join(QStringLiteral(", ")) + QStringLiteral(" and %1 more").arg(extra);
    }
    if (shown.size() <= 1) return shown.value(0);
    const QString last = shown.takeLast();
    return shown.join(QStringLiteral(", ")) + QStringLiteral(" and ") + last;
}

// A transparent premultiplied image whose memory comes from the system already
// zeroed, so a canvas-sized layer costs nothing until it is written to.
QImage zeroedImage(int w, int h) {
    if (w <= 0 || h <= 0) return QImage();
    const qsizetype bpl = qsizetype(w) * 4;
    void *data = std::calloc(size_t(bpl) * size_t(h), 1);
    if (!data) return QImage();
    return QImage(static_cast<uchar *>(data), w, h, bpl, QImage::Format_ARGB32_Premultiplied,
                  [](void *p) { std::free(p); }, data);
}

// ===================== PackBits =====================

void packBits(const uchar *src, int n, QByteArray &out) {
    int i = 0;
    while (i < n) {
        int run = 1;
        while (i + run < n && run < 128 && src[i + run] == src[i]) ++run;
        if (run >= 2) {
            out.append(char(1 - run));
            out.append(char(src[i]));
            i += run;
            continue;
        }
        const int start = i;
        int len = 0;
        while (i < n && len < 128) {
            if (i + 2 < n && src[i] == src[i + 1] && src[i] == src[i + 2]) break;
            ++i;
            ++len;
        }
        out.append(char(len - 1));
        out.append(reinterpret_cast<const char *>(src + start), len);
    }
}

// Lenient: a row that ends early is padded with zeros, extra bytes are ignored.
void unpackBits(const uchar *src, qint64 srcLen, uchar *dst, qint64 dstLen) {
    qint64 i = 0, o = 0;
    while (o < dstLen && i < srcLen) {
        const int h = static_cast<signed char>(src[i++]);
        if (h >= 0) {
            qint64 n = qMin<qint64>(h + 1, qMin(srcLen - i, dstLen - o));
            std::memcpy(dst + o, src + i, size_t(n));
            i += h + 1;
            o += n;
        } else if (h != -128) {
            if (i >= srcLen) break;
            const qint64 n = qMin<qint64>(1 - h, dstLen - o);
            std::memset(dst + o, src[i++], size_t(n));
            o += n;
        }
    }
    if (o < dstLen) std::memset(dst + o, 0, size_t(dstLen - o));
}

// ===================== Reading =====================

class Cursor {
public:
    Cursor(const uchar *data, qint64 size) : m_d(data), m_n(size) {}
    qint64 pos() const { return m_p; }
    qint64 size() const { return m_n; }
    bool ok() const { return !m_bad; }
    void fail() { m_bad = true; }
    bool has(qint64 k) const { return !m_bad && k >= 0 && m_p + k <= m_n; }
    void seek(qint64 to) {
        if (to < 0 || to > m_n) m_bad = true;
        else m_p = to;
    }
    void skip(qint64 k) { seek(m_p + k); }
    const uchar *at(qint64 offset) const { return m_d + offset; }
    quint8 u8() {
        if (!has(1)) { m_bad = true; return 0; }
        return m_d[m_p++];
    }
    quint16 u16() {
        if (!has(2)) { m_bad = true; return 0; }
        const quint16 v = qFromBigEndian<quint16>(m_d + m_p);
        m_p += 2;
        return v;
    }
    quint32 u32() {
        if (!has(4)) { m_bad = true; return 0; }
        const quint32 v = qFromBigEndian<quint32>(m_d + m_p);
        m_p += 4;
        return v;
    }
    quint64 u64() {
        if (!has(8)) { m_bad = true; return 0; }
        const quint64 v = qFromBigEndian<quint64>(m_d + m_p);
        m_p += 8;
        return v;
    }
    qint16 i16() { return qint16(u16()); }
    qint32 i32() { return qint32(u32()); }
    QByteArray bytes(qint64 k) {
        if (!has(k)) { m_bad = true; return QByteArray(); }
        QByteArray b(reinterpret_cast<const char *>(m_d + m_p), int(k));
        m_p += k;
        return b;
    }
    bool sigAt(qint64 offset) const {
        if (offset < 0 || offset + 4 > m_n) return false;
        return std::memcmp(m_d + offset, "8BIM", 4) == 0 || std::memcmp(m_d + offset, "8B64", 4) == 0;
    }

private:
    const uchar *m_d;
    qint64 m_n;
    qint64 m_p = 0;
    bool m_bad = false;
};

QRect readRect(Cursor &c) {
    const qint32 top = c.i32(), left = c.i32(), bottom = c.i32(), right = c.i32();
    return QRect(left, top, qMax(0, right - left), qMax(0, bottom - top));
}

bool isLongKey(const QByteArray &key) {
    static const QSet<QByteArray> keys = {"LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn",
                                          "Alph", "FMsk", "lnk2", "FEid", "FXid", "PxSD"};
    return keys.contains(key);
}

// Walks "additional layer information" blocks up to `end`.
void readTaggedBlocks(Cursor &c, qint64 end, bool psb,
                      const std::function<void(const QByteArray &, qint64, qint64)> &onBlock) {
    while (c.ok() && c.pos() + 12 <= end) {
        if (!c.sigAt(c.pos())) break;
        c.skip(4);
        const QByteArray key = c.bytes(4);
        const quint64 len = (psb && isLongKey(key)) ? c.u64() : c.u32();
        const qint64 start = c.pos();
        if (!c.ok() || len > quint64(end - start)) break;
        onBlock(key, start, qint64(len));
        c.seek(start + qint64(len));
        // Writers pad blocks to 2 or 4 bytes, not always counted in the length.
        for (int pad = 0; pad < 4; ++pad) {
            if (c.pos() + pad + 4 <= end && c.sigAt(c.pos() + pad)) {
                c.skip(pad);
                break;
            }
        }
    }
}

struct Channel {
    qint16 id = 0;
    quint64 length = 0;
    qint64 offset = 0;   // in the file
};

struct Record {
    QRect rect;
    std::vector<Channel> channels;
    QByteArray blendKey = "norm";
    quint8 opacity = 255;
    quint8 clipping = 0;
    quint8 flags = 0;
    QRect maskRect;
    quint8 maskDefault = 0;
    quint8 maskFlags = 0;
    bool hasRealMask = false;
    QRect realMaskRect;
    quint8 realMaskDefault = 0;
    quint8 realMaskFlags = 0;
    QString name;
    int sectionType = 0;   // 1, 2: group (open, closed); 3: end of a group
    QByteArray sectionBlendKey;
    quint32 protection = 0;
    int fillOpacity = 255;
    bool adjustment = false;
    bool effects = false;
    bool vectorMask = false;
    bool text = false;
    // Group visibility and opacity applied:
    bool effectiveVisible = true;
    float effectiveOpacity = 1.0f;
    bool inGroup = false;

    const Channel *channel(int id) const {
        for (const Channel &ch : channels)
            if (ch.id == id) return &ch;
        return nullptr;
    }
};

bool parseLayerInfo(Cursor &c, qint64 end, bool psb, std::vector<Record> &records,
                    bool *mergedAlpha, QString *error) {
    const qint16 count = c.i16();
    if (mergedAlpha) *mergedAlpha = count < 0;
    const int n = std::abs(int(count));
    records.clear();
    records.reserve(n);
    for (int i = 0; i < n && c.ok(); ++i) {
        Record r;
        r.rect = readRect(c);
        const quint16 channelCount = c.u16();
        if (channelCount > 56) {
            if (error) *error = QStringLiteral("A layer has %1 channels, which is not possible.").arg(channelCount);
            return false;
        }
        for (int k = 0; k < channelCount; ++k) {
            Channel ch;
            ch.id = c.i16();
            ch.length = psb ? c.u64() : c.u32();
            r.channels.push_back(ch);
        }
        if (c.bytes(4) != "8BIM") {
            if (error) *error = QStringLiteral("The layer list is damaged.");
            return false;
        }
        r.blendKey = c.bytes(4);
        r.opacity = c.u8();
        r.clipping = c.u8();
        r.flags = c.u8();
        c.skip(1);
        const quint32 extra = c.u32();
        const qint64 extraEnd = c.pos() + extra;
        if (extraEnd > end) {
            if (error) *error = QStringLiteral("The layer list is cut off.");
            return false;
        }

        // Layer mask
        const quint32 maskLen = c.u32();
        const qint64 maskStart = c.pos();
        if (maskLen >= 18) {
            r.maskRect = readRect(c);
            r.maskDefault = c.u8();
            r.maskFlags = c.u8();
            if (maskLen >= 36 && c.has(qint64(maskLen) - 18)) {
                // The "real" user mask (beside a vector mask) is the last 18 bytes.
                Cursor real(c.at(0), c.size());
                real.seek(maskStart + maskLen - 18);
                r.realMaskFlags = real.u8();
                r.realMaskDefault = real.u8();
                r.realMaskRect = readRect(real);
                r.hasRealMask = real.ok();
            }
        }
        c.seek(maskStart + maskLen);
        // Blending ranges: not used.
        c.skip(c.u32());
        // Legacy name: Pascal string padded to a multiple of 4.
        const quint8 nameLen = c.u8();
        r.name = QString::fromLatin1(c.bytes(nameLen));
        const int total = 1 + nameLen;
        c.skip(((total + 3) & ~3) - total);

        readTaggedBlocks(c, extraEnd, psb, [&](const QByteArray &key, qint64 start, qint64 len) {
            Cursor b(c.at(0), c.size());
            b.seek(start);
            if (key == "luni" && len >= 4) {
                const quint32 units = b.u32();
                if (units <= quint32((len - 4) / 2)) {
                    QString s;
                    s.reserve(int(units));
                    for (quint32 u = 0; u < units; ++u) s.append(QChar(b.u16()));
                    while (s.endsWith(QChar(0))) s.chop(1);
                    if (b.ok()) r.name = s;
                }
            } else if ((key == "lsct" || key == "lsdk") && len >= 4) {
                r.sectionType = int(b.u32());
                if (len >= 12) {
                    b.skip(4);
                    r.sectionBlendKey = b.bytes(4);
                }
            } else if (key == "lspf" && len >= 4) {
                r.protection = b.u32();
            } else if (key == "iOpa" && len >= 1) {
                r.fillOpacity = b.u8();
            } else if (key == "lfx2" || key == "lrFX" || key == "lmfx") {
                r.effects = true;
            } else if (key == "vmsk" || key == "vsms") {
                r.vectorMask = true;
            } else if (key == "TySh" || key == "tySh") {
                r.text = true;
            } else {
                static const QSet<QByteArray> adjustments = {
                    "SoCo", "GdFl", "PtFl", "brit", "levl", "curv", "expA", "vibA", "hue ",
                    "hue2", "blnc", "blwh", "phfl", "mixr", "clrL", "nvrt", "post", "thrs",
                    "grdm", "selc"};
                if (adjustments.contains(key)) r.adjustment = true;
            }
        });
        c.seek(extraEnd);
        records.push_back(r);
    }
    // Channel image data follows all records, in the same order.
    for (Record &r : records) {
        for (Channel &ch : r.channels) {
            ch.offset = c.pos();
            if (ch.length > quint64(c.size() - c.pos())) {
                if (error) *error = QStringLiteral("The file is cut off: layer pixels are missing.");
                return false;
            }
            c.skip(qint64(ch.length));
        }
    }
    if (!c.ok()) {
        if (error) *error = QStringLiteral("The layer list is damaged.");
        return false;
    }
    return true;
}

// 16-bit big-endian samples to 8-bit.
void to8Bit(const uchar *src, uchar *dst, int count) {
    for (int i = 0; i < count; ++i) {
        const quint32 v = (quint32(src[2 * i]) << 8) | src[2 * i + 1];
        dst[i] = uchar((v * 255 + 32767) / 65535);
    }
}

// One channel of `w` x `h` pixels as 8-bit samples.
bool decodeChannel(const uchar *data, qint64 avail, int w, int h, int depth, bool psb,
                   std::vector<uchar> &out) {
    out.assign(size_t(qint64(w) * h), 0);
    if (w <= 0 || h <= 0) return true;
    if (avail < 2) return false;
    const int compression = (data[0] << 8) | data[1];
    const uchar *p = data + 2;
    const qint64 left = avail - 2;
    const int bpc = depth / 8;
    const qint64 rowBytes = qint64(w) * bpc;
    std::vector<uchar> row(bpc == 1 ? 0 : size_t(rowBytes));
    auto storeRow = [&](const uchar *src, int y) {
        if (bpc == 1) std::memcpy(out.data() + qint64(y) * w, src, size_t(w));
        else to8Bit(src, out.data() + qint64(y) * w, w);
    };
    switch (compression) {
    case 0:
        if (left < rowBytes * h) return false;
        for (int y = 0; y < h; ++y) storeRow(p + rowBytes * y, y);
        return true;
    case 1: {
        const int cb = psb ? 4 : 2;
        if (left < qint64(h) * cb) return false;
        const uchar *src = p + qint64(h) * cb;
        qint64 srcLeft = left - qint64(h) * cb;
        for (int y = 0; y < h; ++y) {
            const qint64 len = cb == 4 ? qint64(qFromBigEndian<quint32>(p + 4 * y))
                                       : qint64(qFromBigEndian<quint16>(p + 2 * y));
            const qint64 use = qMin(len, srcLeft);
            if (bpc == 1) {
                unpackBits(src, use, out.data() + qint64(y) * w, w);
            } else {
                unpackBits(src, use, row.data(), rowBytes);
                to8Bit(row.data(), out.data() + qint64(y) * w, w);
            }
            src += use;
            srcLeft -= use;
        }
        return true;
    }
    case 2:
    case 3: {
        std::vector<uchar> raw(static_cast<size_t>(rowBytes * h));
        uLongf destLen = uLongf(raw.size());
        const int rc = uncompress(raw.data(), &destLen, p, uLong(left));
        if (rc != Z_OK && !(rc == Z_BUF_ERROR && destLen == raw.size())) return false;
        for (int y = 0; y < h; ++y) {
            uchar *r = raw.data() + rowBytes * y;
            if (compression == 3) {
                if (bpc == 1) {
                    for (int x = 1; x < w; ++x) r[x] = uchar(r[x] + r[x - 1]);
                } else {
                    quint16 prev = qFromBigEndian<quint16>(r);
                    for (int x = 1; x < w; ++x) {
                        const quint16 v = quint16(qFromBigEndian<quint16>(r + 2 * x) + prev);
                        qToBigEndian<quint16>(v, r + 2 * x);
                        prev = v;
                    }
                }
            }
            storeRow(r, y);
        }
        return true;
    }
    default:
        return false;
    }
}

int colorChannelsFor(int mode) {
    switch (mode) {
    case 1: return 1;   // grayscale
    case 3: return 3;   // RGB
    case 4: return 4;   // CMYK
    default: return 0;
    }
}

// Straight color from the decoded planes at index i.
inline QRgb pixelFrom(int mode, const std::vector<uchar> *const *planes, size_t i) {
    if (mode == 1) {
        const uchar g = planes[0] ? (*planes[0])[i] : 0;
        return qRgb(g, g, g);
    }
    if (mode == 4) {
        // Photoshop stores CMYK inverted (255 = no ink). No color profile.
        const int c = planes[0] ? (*planes[0])[i] : 255;
        const int m = planes[1] ? (*planes[1])[i] : 255;
        const int y = planes[2] ? (*planes[2])[i] : 255;
        const int k = planes[3] ? (*planes[3])[i] : 255;
        return qRgb(c * k / 255, m * k / 255, y * k / 255);
    }
    return qRgb(planes[0] ? (*planes[0])[i] : 0, planes[1] ? (*planes[1])[i] : 0,
                planes[2] ? (*planes[2])[i] : 0);
}

struct DecodeJob {
    const Record *record = nullptr;
    LayerData *out = nullptr;
    QString *error = nullptr;
};

bool decodeLayer(const uchar *file, const Record &r, int mode, int depth, bool psb,
                 const QSize &canvas, LayerData &out, bool *cropped) {
    const int colors = colorChannelsFor(mode);
    std::vector<uchar> planes[5];
    const std::vector<uchar> *color[4] = {nullptr, nullptr, nullptr, nullptr};
    const int w = r.rect.width(), h = r.rect.height();
    for (int k = 0; k < colors; ++k) {
        if (const Channel *ch = r.channel(k)) {
            if (!decodeChannel(file + ch->offset, qint64(ch->length), w, h, depth, psb, planes[k]))
                return false;
            color[k] = &planes[k];
        }
    }
    const std::vector<uchar> *alpha = nullptr;
    if (const Channel *ch = r.channel(-1)) {
        if (!decodeChannel(file + ch->offset, qint64(ch->length), w, h, depth, psb, planes[4]))
            return false;
        alpha = &planes[4];
    }

    // The user's layer mask, applied to the layer's transparency.
    std::vector<uchar> maskPlane;
    QRect maskRect;
    int maskDefault = 255;
    bool useMask = false;
    const Channel *maskChannel = r.channel(-3);
    if (maskChannel && r.hasRealMask) {
        maskRect = r.realMaskRect;
        maskDefault = r.realMaskDefault;
        useMask = !(r.realMaskFlags & 0x02);
    } else if ((maskChannel = r.channel(-2))) {
        maskRect = r.maskRect;
        maskDefault = r.maskDefault;
        useMask = !(r.maskFlags & 0x02);
    }
    if (useMask && maskChannel) {
        if (!decodeChannel(file + maskChannel->offset, qint64(maskChannel->length), maskRect.width(),
                           maskRect.height(), depth, psb, maskPlane))
            useMask = false;
    } else {
        useMask = false;
    }

    // Inside the canvas: a canvas-sized layer. Reaching outside: a larger
    // image that keeps those pixels, within reason.
    const QRect canvasRect(QPoint(0, 0), canvas);
    QRect target = canvasRect;
    if (!r.rect.isEmpty() && !canvasRect.contains(r.rect)) {
        const QRect united = canvasRect.united(r.rect);
        if (qint64(united.width()) * united.height() <= 4 * qint64(canvas.width()) * canvas.height()
            && united.width() <= 4 * kMaxSide && united.height() <= 4 * kMaxSide) {
            target = united;
        } else if (cropped) {
            *cropped = true;
        }
    }
    QImage image = zeroedImage(target.width(), target.height());
    if (image.isNull()) return false;
    const QRect visible = r.rect.intersected(target);
    const std::vector<uchar> *const *colorPlanes = color;
    for (int y = visible.top(); y <= visible.bottom(); ++y) {
        QRgb *dst = reinterpret_cast<QRgb *>(image.scanLine(y - target.top())) - target.left();
        const size_t rowBase = size_t(y - r.rect.top()) * size_t(w);
        for (int x = visible.left(); x <= visible.right(); ++x) {
            const size_t i = rowBase + size_t(x - r.rect.left());
            int a = alpha ? (*alpha)[i] : 255;
            if (useMask) {
                int m = maskDefault;
                if (maskRect.contains(x, y))
                    m = maskPlane[size_t(y - maskRect.top()) * size_t(maskRect.width())
                                  + size_t(x - maskRect.left())];
                a = a * m / 255;
            }
            if (a == 0) continue;
            const QRgb c = pixelFrom(mode, colorPlanes, i);
            dst[x] = qPremultiply(qRgba(qRed(c), qGreen(c), qBlue(c), a));
        }
    }
    out.image = image;
    out.offset = target.topLeft();
    out.retainsOffCanvasPixels = target != canvasRect;
    return true;
}

// The combined picture stored after the layers, for files without layers.
bool decodeMerged(Cursor &c, int channels, int mode, int depth, bool psb, bool alpha,
                  const QSize &size, LayerData &out, QString *error) {
    const int w = size.width(), h = size.height();
    const int colors = colorChannelsFor(mode);
    const int used = qMin(channels, colors + (alpha ? 1 : 0));
    const int compression = c.u16();
    const int bpc = depth / 8;
    const qint64 rowBytes = qint64(w) * bpc;
    std::vector<std::vector<uchar>> planes(static_cast<size_t>(channels));
    std::vector<uchar> row(static_cast<size_t>(rowBytes));
    if (compression == 0) {
        for (int k = 0; k < channels; ++k) {
            if (!c.has(rowBytes * h)) break;
            if (k < used) {
                planes[size_t(k)].resize(size_t(qint64(w) * h));
                for (int y = 0; y < h; ++y) {
                    const uchar *src = c.at(c.pos() + rowBytes * y);
                    if (bpc == 1) std::memcpy(planes[size_t(k)].data() + qint64(y) * w, src, size_t(w));
                    else to8Bit(src, planes[size_t(k)].data() + qint64(y) * w, w);
                }
            }
            c.skip(rowBytes * h);
        }
    } else if (compression == 1) {
        const int cb = psb ? 4 : 2;
        const qint64 countsAt = c.pos();
        c.skip(qint64(channels) * h * cb);
        for (int k = 0; k < channels && c.ok(); ++k) {
            if (k < used) planes[size_t(k)].resize(size_t(qint64(w) * h));
            for (int y = 0; y < h; ++y) {
                const uchar *cp = c.at(countsAt + (qint64(k) * h + y) * cb);
                const qint64 len = cb == 4 ? qint64(qFromBigEndian<quint32>(cp)) : qint64(qFromBigEndian<quint16>(cp));
                if (!c.has(len)) { c.fail(); break; }
                if (k < used) {
                    if (bpc == 1) {
                        unpackBits(c.at(c.pos()), len, planes[size_t(k)].data() + qint64(y) * w, w);
                    } else {
                        unpackBits(c.at(c.pos()), len, row.data(), rowBytes);
                        to8Bit(row.data(), planes[size_t(k)].data() + qint64(y) * w, w);
                    }
                }
                c.skip(len);
            }
        }
    } else {
        if (error) *error = QStringLiteral("The combined picture uses a compression Paint.with.me cannot read.");
        return false;
    }
    if (!c.ok() || used < colors || planes[0].empty()) {
        if (error) *error = QStringLiteral("The combined picture in the file is damaged.");
        return false;
    }
    const std::vector<uchar> *color[4] = {nullptr, nullptr, nullptr, nullptr};
    for (int k = 0; k < colors; ++k) color[k] = planes[size_t(k)].empty() ? nullptr : &planes[size_t(k)];
    const std::vector<uchar> *alphaPlane =
        (alpha && used > colors && !planes[size_t(colors)].empty()) ? &planes[size_t(colors)] : nullptr;
    QImage image = zeroedImage(w, h);
    if (image.isNull()) return false;
    for (int y = 0; y < h; ++y) {
        QRgb *dst = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * size_t(w) + size_t(x);
            const int a = alphaPlane ? (*alphaPlane)[i] : 255;
            const QRgb px = pixelFrom(mode, color, i);
            dst[x] = qPremultiply(qRgba(qRed(px), qGreen(px), qBlue(px), a));
        }
    }
    out.image = image;
    out.name = QStringLiteral("Background");
    return true;
}

bool readFrom(const uchar *data, qint64 size, ImageData &out, QString *error) {
    out = ImageData();
    Cursor c(data, size);
    if (c.bytes(4) != "8BPS") {
        if (error) *error = QStringLiteral("This is not a Photoshop document.");
        return false;
    }
    const quint16 version = c.u16();
    if (version != 1 && version != 2) {
        if (error) *error = QStringLiteral("This Photoshop document version (%1) is not supported.").arg(version);
        return false;
    }
    const bool psb = version == 2;
    c.skip(6);
    const int channels = c.u16();
    const qint64 height = c.u32();
    const qint64 width = c.u32();
    const int depth = c.u16();
    const int mode = c.u16();
    if (!c.ok() || width <= 0 || height <= 0 || width > 300000 || height > 300000) {
        if (error) *error = QStringLiteral("The image size in this file is not valid.");
        return false;
    }
    if (depth != 8 && depth != 16) {
        if (error) *error = depth == 32
            ? QStringLiteral("This document has 32 bits per channel (HDR), which Paint.with.me cannot open yet. Save it with 8 or 16 bits per channel.")
            : QStringLiteral("This document has %1 bit(s) per channel, which Paint.with.me cannot open.").arg(depth);
        return false;
    }
    const int colors = colorChannelsFor(mode);
    if (colors == 0) {
        static const char *modes[] = {"Bitmap", "Grayscale", "Indexed Color", "RGB", "CMYK", "?", "?",
                                      "Multichannel", "Duotone", "Lab"};
        const QString name = mode >= 0 && mode <= 9 ? QString::fromLatin1(modes[mode]) : QString::number(mode);
        if (error) *error = QStringLiteral("This document uses the %1 color mode, which Paint.with.me cannot open. "
                                           "Convert it to RGB first.").arg(name);
        return false;
    }
    if (channels < colors) {
        if (error) *error = QStringLiteral("The document has too few color channels.");
        return false;
    }
    out.width = int(width);
    out.height = int(height);
    const QSize canvas(out.width, out.height);
    if (depth == 16) out.notes << QStringLiteral("The document has 16 bits per channel; it was opened with 8.");
    if (mode == 4) out.notes << QStringLiteral("The document is CMYK; it was converted to RGB without a color profile, so colors may look different.");

    // Color mode data
    c.skip(c.u32());
    // Image resources
    const quint32 resLen = c.u32();
    const qint64 resEnd = c.pos() + resLen;
    int targetLayer = -1;
    while (c.ok() && c.pos() + 12 <= resEnd) {
        c.skip(4);   // "8BIM" (or another signature)
        const quint16 id = c.u16();
        const quint8 nameLen = c.u8();
        c.skip(((nameLen + 2) & ~1) - 1);   // Pascal name padded to even
        const quint32 len = c.u32();
        const qint64 start = c.pos();
        if (len > quint64(resEnd - start)) break;
        Cursor r(data, size);
        r.seek(start);
        if (id == 0x03ED && len >= 16) {
            const double h = r.u32() / 65536.0;
            const int unit = r.u16();
            out.dpi = unit == 2 ? h * 2.54 : h;
        } else if (id == 0x0400 && len >= 2) {
            targetLayer = r.u16();
        }
        c.seek(start + ((len + 1) & ~1u));
    }
    c.seek(resEnd);
    if (!c.ok()) {
        if (error) *error = QStringLiteral("The file is damaged.");
        return false;
    }

    // Layer and mask information
    const quint64 lmsLen = psb ? c.u64() : c.u32();
    const qint64 lmsStart = c.pos();
    if (lmsLen > quint64(size - lmsStart)) {
        if (error) *error = QStringLiteral("The file is cut off.");
        return false;
    }
    const qint64 lmsEnd = lmsStart + qint64(lmsLen);
    std::vector<Record> records;
    bool mergedAlpha = false;
    if (lmsLen > 0) {
        const quint64 liLen = psb ? c.u64() : c.u32();
        const qint64 liStart = c.pos();
        if (liLen > 0) {
            if (liLen > quint64(lmsEnd - liStart)) {
                if (error) *error = QStringLiteral("The layer list is cut off.");
                return false;
            }
            if (!parseLayerInfo(c, liStart + qint64(liLen), psb, records, &mergedAlpha, error)) return false;
        }
        c.seek(liStart + qint64(liLen));
        // Global layer mask info, then document-wide blocks; 16-bit files keep
        // their layers in an "Lr16" block here.
        if (c.pos() + 4 <= lmsEnd) c.skip(c.u32());
        QString blockError;
        bool blockFailed = false;
        readTaggedBlocks(c, lmsEnd, psb, [&](const QByteArray &key, qint64 start, qint64 len) {
            if (!records.empty() || (key != "Lr16" && key != "Lr32" && key != "Layr")) return;
            Cursor b(data, size);
            b.seek(start);
            if (!parseLayerInfo(b, start + len, psb, records, &mergedAlpha, &blockError)) blockFailed = true;
        });
        if (blockFailed) {
            if (error) *error = blockError;
            return false;
        }
        c.seek(lmsEnd);
    }

    // Groups: apply their visibility and opacity to the layers inside. In the
    // file a group is [end marker, its layers ..., the group record], bottom
    // to top, so walk from the top down.
    struct Group { bool visible; float opacity; };
    std::vector<Group> groups;
    int groupCount = 0;
    QStringList groupModes;
    for (int i = int(records.size()) - 1; i >= 0; --i) {
        Record &r = records[size_t(i)];
        const bool parentVisible = groups.empty() || groups.back().visible;
        const float parentOpacity = groups.empty() ? 1.0f : groups.back().opacity;
        if (r.sectionType == 1 || r.sectionType == 2) {
            ++groupCount;
            groups.push_back({parentVisible && !(r.flags & 0x02), parentOpacity * (r.opacity / 255.0f)});
            const QByteArray key = r.sectionBlendKey.isEmpty() ? r.blendKey : r.sectionBlendKey;
            if (key != "pass" && key != "norm") groupModes << r.name;
            continue;
        }
        if (r.sectionType == 3) {
            if (!groups.empty()) groups.pop_back();
            continue;
        }
        r.inGroup = !groups.empty();
        r.effectiveVisible = parentVisible && !(r.flags & 0x02);
        r.effectiveOpacity = parentOpacity * (r.opacity / 255.0f) * (r.fillOpacity / 255.0f);
    }

    // Which records become layers.
    QStringList adjustments, effects, masked, clipped, vectorMasks, texts, unknownModes, inGroups;
    std::vector<int> recordOfLayer;
    QHash<int, int> layerOfRecord;
    for (int i = 0; i < int(records.size()); ++i) {
        const Record &r = records[size_t(i)];
        if (r.sectionType != 0) continue;
        if (r.adjustment) {
            adjustments << r.name;
            continue;
        }
        if (r.effects) effects << r.name;
        if (r.channel(-2) || r.channel(-3)) masked << r.name;
        if (r.clipping) clipped << r.name;
        if (r.vectorMask) vectorMasks << r.name;
        if (r.text) texts << r.name;
        if (r.inGroup) inGroups << r.name;
        BlendMode m;
        if (!blendModeFromKey(r.blendKey, &m)) unknownModes << QStringLiteral("%1 (%2)").arg(r.name, photoshopModeName(r.blendKey));
        layerOfRecord.insert(i, int(recordOfLayer.size()));
        recordOfLayer.push_back(i);
    }

    if (recordOfLayer.empty()) {
        // No pixel layers: use the combined picture.
        LayerData merged;
        if (!decodeMerged(c, channels, mode, depth, psb, mergedAlpha, canvas, merged, error)) return false;
        out.layers.push_back(merged);
        out.activeLayer = 0;
        if (!adjustments.isEmpty())
            out.notes << QStringLiteral("Adjustment and fill layers are not supported and were left out: %1.").arg(nameList(adjustments));
        return true;
    }

    out.layers.resize(recordOfLayer.size());
    std::atomic<bool> failed(false);
    std::atomic<bool> cropped(false);
    std::vector<int> indices(recordOfLayer.size());
    for (size_t k = 0; k < indices.size(); ++k) indices[k] = int(k);
    QtConcurrent::blockingMap(indices, [&](int k) {
        const Record &r = records[size_t(recordOfLayer[size_t(k)])];
        LayerData &layer = out.layers[size_t(k)];
        bool wasCropped = false;
        if (!decodeLayer(data, r, mode, depth, psb, canvas, layer, &wasCropped)) failed = true;
        if (wasCropped) cropped = true;
        layer.name = r.name;
        layer.visible = r.effectiveVisible;
        layer.opacity = qBound(0.0f, r.effectiveOpacity, 1.0f);
        layer.locked = (r.protection & 0x80000000u) || (r.protection & 0x02);
        BlendMode m = BlendMode::Normal;
        if (blendModeFromKey(r.blendKey, &m)) layer.blendMode = m;
    });
    if (failed) {
        if (error) *error = QStringLiteral("The layer pixels in this file are damaged or use an unknown compression.");
        return false;
    }
    if (targetLayer >= 0) {
        // The selected layer counts every record, groups included.
        for (int i = qMin(targetLayer, int(records.size()) - 1); i >= 0; --i) {
            if (layerOfRecord.contains(i)) {
                out.activeLayer = layerOfRecord.value(i);
                break;
            }
        }
    }

    if (groupCount > 0)
        out.notes << QStringLiteral("Layer groups are not supported yet. The layers in %1 group(s) were opened one by one, "
                                    "with each group's visibility and opacity applied to them.").arg(groupCount);
    if (!groupModes.isEmpty())
        out.notes << QStringLiteral("Group blend modes were not applied: %1.").arg(nameList(groupModes));
    if (!unknownModes.isEmpty())
        out.notes << QStringLiteral("Blend modes Paint.with.me does not have were replaced by Normal: %1.").arg(nameList(unknownModes));
    if (!masked.isEmpty())
        out.notes << QStringLiteral("Layer masks were applied to their layers: %1.").arg(nameList(masked));
    if (!adjustments.isEmpty())
        out.notes << QStringLiteral("Adjustment and fill layers are not supported and were left out: %1.").arg(nameList(adjustments));
    if (!effects.isEmpty())
        out.notes << QStringLiteral("Layer styles (shadows, strokes, glows ...) are not shown: %1.").arg(nameList(effects));
    if (!clipped.isEmpty())
        out.notes << QStringLiteral("Clipping masks are not supported; these layers are shown unclipped: %1.").arg(nameList(clipped));
    if (!vectorMasks.isEmpty())
        out.notes << QStringLiteral("Vector masks were ignored: %1.").arg(nameList(vectorMasks));
    if (!texts.isEmpty())
        out.notes << QStringLiteral("Text layers were opened as pixels and can no longer be edited as text: %1.").arg(nameList(texts));
    if (cropped)
        out.notes << QStringLiteral("Some layers reach far outside the canvas; those pixels were cut off.");
    return true;
}

// ===================== Writing =====================

class Out {
public:
    explicit Out(QIODevice *d) : m_d(d) {}
    bool ok() const { return m_ok; }
    qint64 pos() const { return m_d->pos(); }
    void raw(const char *p, qint64 n) {
        if (m_ok && n > 0 && m_d->write(p, n) != n) m_ok = false;
    }
    void raw(const QByteArray &b) { raw(b.constData(), b.size()); }
    void u8(quint8 v) { raw(reinterpret_cast<const char *>(&v), 1); }
    void u16(quint16 v) {
        uchar b[2];
        qToBigEndian(v, b);
        raw(reinterpret_cast<const char *>(b), 2);
    }
    void u32(quint32 v) {
        uchar b[4];
        qToBigEndian(v, b);
        raw(reinterpret_cast<const char *>(b), 4);
    }
    void i16(qint16 v) { u16(quint16(v)); }
    void i32(qint32 v) { u32(quint32(v)); }
    void zeros(int n) {
        static const char z[8] = {};
        while (n > 0) {
            raw(z, qMin(n, 8));
            n -= 8;
        }
    }
    void patch32(qint64 at, quint32 v) {
        if (!m_ok) return;
        const qint64 back = m_d->pos();
        if (!m_d->seek(at)) { m_ok = false; return; }
        u32(v);
        if (!m_d->seek(back)) m_ok = false;
    }

private:
    QIODevice *m_d;
    bool m_ok = true;
};

void appendU16(QByteArray &b, quint16 v) {
    b.append(char(v >> 8));
    b.append(char(v & 0xff));
}

// Smallest rectangle with a pixel that is not fully transparent.
QRect contentRect(const QImage &img) {
    const int w = img.width(), h = img.height();
    int top = -1, bottom = -1, left = w, right = -1;
    for (int y = 0; y < h; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        int l = 0;
        while (l < w && qAlpha(row[l]) == 0) ++l;
        if (l == w) continue;
        int r = w - 1;
        while (qAlpha(row[r]) == 0) --r;
        if (top < 0) top = y;
        bottom = y;
        left = qMin(left, l);
        right = qMax(right, r);
    }
    if (top < 0) return QRect();
    return QRect(left, top, right - left + 1, bottom - top + 1);
}

// RLE channel data for the rows of `rect` (image coordinates) in `img`, which
// is premultiplied ARGB32. Channels in `order`: 0..2 = R, G, B, 3 = alpha.
// Returns per channel: the row byte counts and the rows.
struct Encoded {
    QByteArray counts[4];
    QByteArray rows[4];
};

Encoded encodeRect(const QImage &img, const QRect &rect) {
    const int w = rect.width();
    constexpr int kChunk = 64;
    const int chunks = (rect.height() + kChunk - 1) / kChunk;
    std::vector<Encoded> parts(size_t(qMax(0, chunks)));
    std::vector<int> ids(parts.size());
    for (size_t i = 0; i < ids.size(); ++i) ids[i] = int(i);
    auto work = [&](int chunk) {
        Encoded &e = parts[size_t(chunk)];
        std::vector<uchar> plane[4];
        for (auto &p : plane) p.resize(size_t(w));
        const int y0 = rect.top() + chunk * kChunk;
        const int y1 = qMin(rect.bottom() + 1, y0 + kChunk);
        for (int y = y0; y < y1; ++y) {
            const QRgb *src = reinterpret_cast<const QRgb *>(img.constScanLine(y)) + rect.left();
            for (int x = 0; x < w; ++x) {
                const QRgb p = qUnpremultiply(src[x]);
                plane[0][size_t(x)] = uchar(qRed(p));
                plane[1][size_t(x)] = uchar(qGreen(p));
                plane[2][size_t(x)] = uchar(qBlue(p));
                plane[3][size_t(x)] = uchar(qAlpha(p));
            }
            for (int k = 0; k < 4; ++k) {
                const int before = int(e.rows[k].size());
                packBits(plane[k].data(), w, e.rows[k]);
                appendU16(e.counts[k], quint16(e.rows[k].size() - before));
            }
        }
    };
    if (ids.size() < 2) {
        for (int id : ids) work(id);
    } else {
        QtConcurrent::blockingMap(ids, work);
    }
    Encoded all;
    for (int k = 0; k < 4; ++k) {
        qsizetype counts = 0, rows = 0;
        for (const Encoded &e : parts) {
            counts += e.counts[k].size();
            rows += e.rows[k].size();
        }
        all.counts[k].reserve(counts);
        all.rows[k].reserve(rows);
        for (const Encoded &e : parts) {
            all.counts[k].append(e.counts[k]);
            all.rows[k].append(e.rows[k]);
        }
    }
    return all;
}

QByteArray pascalName(const QString &name) {
    QByteArray latin = name.toLatin1().left(255);
    QByteArray b;
    b.append(char(latin.size()));
    b.append(latin);
    while (b.size() % 4) b.append('\0');
    return b;
}

void taggedBlock(QByteArray &b, const char *key, const QByteArray &data) {
    b.append("8BIM", 4);
    b.append(key, 4);
    QByteArray padded = data;
    while (padded.size() % 4) padded.append('\0');
    uchar len[4];
    qToBigEndian(quint32(padded.size()), len);
    b.append(reinterpret_cast<const char *>(len), 4);
    b.append(padded);
}

} // namespace

// ===================== Public =====================

QByteArray blendModeKey(BlendMode mode, bool *exact) {
    for (const ModeKey &k : kModeKeys) {
        if (k.mode == mode) {
            if (exact) *exact = true;
            return QByteArray(k.key, 4);
        }
    }
    if (exact) *exact = false;
    return QByteArrayLiteral("norm");
}

bool blendModeFromKey(const QByteArray &key, BlendMode *mode) {
    for (const ModeKey &k : kModeKeys) {
        if (key == QByteArray(k.key, 4)) {
            if (mode) *mode = k.mode;
            return true;
        }
    }
    return false;
}

QStringList layersWithoutPsdBlendMode(const std::vector<Layer> &layers) {
    QStringList names;
    for (const Layer &l : layers) {
        bool exact = true;
        blendModeKey(l.blendMode(), &exact);
        if (!exact) names << QStringLiteral("%1 (%2)").arg(l.name(), Layer::blendModeName(l.blendMode()));
    }
    return names;
}

bool isPsdFile(const QString &filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    return f.read(4) == "8BPS";
}

bool read(const QByteArray &data, ImageData &out, QString *error) {
    return readFrom(reinterpret_cast<const uchar *>(data.constData()), data.size(), out, error);
}

bool read(const QString &filePath, ImageData &out, QString *error) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    const qint64 size = f.size();
    // Mapped rather than read: a large document is decoded straight from the
    // file, and only the parts that are used are loaded.
    if (uchar *mapped = f.map(0, size)) {
        const bool ok = readFrom(mapped, size, out, error);
        f.unmap(mapped);
        return ok;
    }
    const QByteArray all = f.readAll();
    return read(all, out, error);
}

bool write(QIODevice *device, const std::vector<Layer> &layers, const QSize &size,
           const WriteOptions &options, QString *error) {
    if (!device || !device->isWritable() || device->isSequential()) {
        if (error) *error = QStringLiteral("Cannot write here.");
        return false;
    }
    if (size.width() > kMaxSide || size.height() > kMaxSide) {
        if (error) *error = QStringLiteral("A .psd can be at most %1 x %1 pixels.").arg(kMaxSide);
        return false;
    }
    if (layers.empty() || layers.size() > 8000) {
        if (error) *error = QStringLiteral("The document has no layers to save.");
        return false;
    }
    const int n = int(layers.size());

    // Each layer is stored cropped to its content.
    std::vector<QRect> content(static_cast<size_t>(n));
    {
        std::vector<int> ids(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) ids[size_t(i)] = i;
        QtConcurrent::blockingMap(ids, [&](int i) { content[size_t(i)] = contentRect(layers[size_t(i)].image()); });
    }

    Out out(device);
    // Header
    out.raw("8BPS", 4);
    out.u16(1);
    out.zeros(6);
    out.u16(4);   // R, G, B and transparency
    out.u32(quint32(size.height()));
    out.u32(quint32(size.width()));
    out.u16(8);
    out.u16(3);   // RGB
    out.u32(0);   // color mode data

    // Image resources: resolution and selected layer.
    QByteArray resources;
    auto resource = [&resources](quint16 id, const QByteArray &data) {
        resources.append("8BIM", 4);
        appendU16(resources, id);
        appendU16(resources, 0);   // empty name, padded to even
        uchar len[4];
        qToBigEndian(quint32(data.size()), len);
        resources.append(reinterpret_cast<const char *>(len), 4);
        resources.append(data);
        if (data.size() % 2) resources.append('\0');
    };
    if (options.dpi > 0) {
        QByteArray r;
        const quint32 fixed = quint32(qRound(options.dpi * 65536.0));
        for (int axis = 0; axis < 2; ++axis) {
            uchar v[4];
            qToBigEndian(fixed, v);
            r.append(reinterpret_cast<const char *>(v), 4);
            appendU16(r, 1);   // pixels per inch
            appendU16(r, 1);   // show sizes in inches
        }
        resource(0x03ED, r);
    }
    if (options.activeLayer >= 0 && options.activeLayer < n) {
        QByteArray r;
        appendU16(r, quint16(options.activeLayer));
        resource(0x0400, r);
    }
    out.u32(quint32(resources.size()));
    out.raw(resources);

    // Layer and mask information
    const qint64 lmsLenAt = out.pos();
    out.u32(0);
    const qint64 liLenAt = out.pos();
    out.u32(0);
    out.i16(qint16(-n));   // negative: the combined picture has transparency
    static const qint16 kChannelIds[4] = {-1, 0, 1, 2};
    static const int kPlaneOf[4] = {3, 0, 1, 2};   // channel id -> Encoded plane
    std::vector<std::array<qint64, 4>> lengthAt(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const Layer &layer = layers[size_t(i)];
        const QRect r = content[size_t(i)].isEmpty() ? QRect()
                                                     : content[size_t(i)].translated(layer.offset());
        out.i32(r.isEmpty() ? 0 : r.top());
        out.i32(r.isEmpty() ? 0 : r.left());
        out.i32(r.isEmpty() ? 0 : r.bottom() + 1);
        out.i32(r.isEmpty() ? 0 : r.right() + 1);
        out.u16(4);
        for (int k = 0; k < 4; ++k) {
            out.i16(kChannelIds[k]);
            lengthAt[size_t(i)][size_t(k)] = out.pos();
            out.u32(0);
        }
        out.raw("8BIM", 4);
        out.raw(blendModeKey(layer.blendMode()));
        out.u8(quint8(qRound(layer.opacity() * 255.0f)));
        out.u8(0);   // clipping: base
        out.u8(quint8(0x08 | (layer.isVisible() ? 0 : 0x02)));
        out.u8(0);
        QByteArray extra;
        extra.append(QByteArray(4, '\0'));   // no layer mask
        extra.append(QByteArray(4, '\0'));   // no blending ranges
        extra.append(pascalName(layer.name()));
        QByteArray uni;
        {
            const QString name = layer.name();
            uchar count[4];
            qToBigEndian(quint32(name.size()), count);
            uni.append(reinterpret_cast<const char *>(count), 4);
            for (QChar ch : name) appendU16(uni, ch.unicode());
        }
        taggedBlock(extra, "luni", uni);
        if (layer.isLocked()) {
            QByteArray lock(4, '\0');
            qToBigEndian(quint32(0x80000000u), reinterpret_cast<uchar *>(lock.data()));
            taggedBlock(extra, "lspf", lock);
        }
        out.u32(quint32(extra.size()));
        out.raw(extra);
    }
    // Pixels, layer by layer (bottom first), channel order as listed above.
    std::vector<std::array<quint32, 4>> lengths(static_cast<size_t>(n));
    for (int i = 0; i < n && out.ok(); ++i) {
        const QRect r = content[size_t(i)];
        if (r.isEmpty()) {
            for (int k = 0; k < 4; ++k) {
                out.u16(0);   // raw, no rows
                lengths[size_t(i)][size_t(k)] = 2;
            }
            continue;
        }
        const Encoded e = encodeRect(layers[size_t(i)].image(), r);
        for (int k = 0; k < 4; ++k) {
            const int plane = kPlaneOf[k];
            out.u16(1);   // RLE
            out.raw(e.counts[plane]);
            out.raw(e.rows[plane]);
            lengths[size_t(i)][size_t(k)] = quint32(2 + e.counts[plane].size() + e.rows[plane].size());
        }
    }
    qint64 liLen = out.pos() - (liLenAt + 4);
    if (liLen % 4) {
        out.zeros(int(4 - liLen % 4));
        liLen += 4 - liLen % 4;
    }
    out.u32(0);   // global layer mask info
    const qint64 lmsLen = out.pos() - (lmsLenAt + 4);
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < 4; ++k)
            out.patch32(lengthAt[size_t(i)][size_t(k)], lengths[size_t(i)][size_t(k)]);
    out.patch32(liLenAt, quint32(liLen));
    out.patch32(lmsLenAt, quint32(lmsLen));

    // Combined picture, for programs that only show the flattened image.
    const QImage merged = Document::compositeLayers(layers, size);
    const Encoded m = encodeRect(merged, merged.rect());
    out.u16(1);   // RLE
    for (int k = 0; k < 4; ++k) out.raw(m.counts[k]);
    for (int k = 0; k < 4; ++k) out.raw(m.rows[k]);

    if (!out.ok()) {
        if (error) *error = device->errorString();
        return false;
    }
    return true;
}

bool write(const QString &filePath, const std::vector<Layer> &layers, const QSize &size,
           const WriteOptions &options, QString *error) {
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    if (!write(&file, layers, size, options, error)) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace Psd
