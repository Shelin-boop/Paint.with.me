#include "core/pdfexport.h"

#include "core/document.h"
#include "core/layer.h"

#include <QBuffer>
#include <QDateTime>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>
#include <QTimeZone>
#include <zlib.h>

#include <cstring>

#ifndef PWM_VERSION_STR
#define PWM_VERSION_STR "dev"
#endif

namespace PdfExport {
namespace {

// zlib stream fed row by row, so the raw pixels never exist in one piece.
class Deflater {
public:
    explicit Deflater(int level) {
        std::memset(&m_z, 0, sizeof(m_z));
        m_ok = deflateInit(&m_z, level) == Z_OK;
    }
    ~Deflater() { if (m_ok) deflateEnd(&m_z); }
    void add(const uchar *data, int n) { run(data, n, Z_NO_FLUSH); }
    QByteArray finish() {
        run(nullptr, 0, Z_FINISH);
        return m_out;
    }
    bool ok() const { return m_ok; }

private:
    void run(const uchar *data, int n, int flush) {
        if (!m_ok) return;
        m_z.next_in = const_cast<Bytef *>(data);
        m_z.avail_in = uInt(n);
        char buffer[1 << 16];
        int rc;
        do {
            m_z.next_out = reinterpret_cast<Bytef *>(buffer);
            m_z.avail_out = sizeof(buffer);
            rc = deflate(&m_z, flush);
            if (rc == Z_STREAM_ERROR) { m_ok = false; return; }
            m_out.append(buffer, int(sizeof(buffer) - m_z.avail_out));
        } while (m_z.avail_out == 0 || (flush == Z_FINISH && rc != Z_STREAM_END));
    }
    z_stream m_z;
    QByteArray m_out;
    bool m_ok = false;
};

// PNG "Up" filter: each row starts with filter type 2 and stores the
// difference to the row above. Readers undo it (/Predictor 15).
void addUpRow(Deflater &z, const std::vector<uchar> &row, std::vector<uchar> &previous,
              std::vector<uchar> &scratch) {
    scratch.resize(row.size() + 1);
    scratch[0] = 2;
    for (size_t i = 0; i < row.size(); ++i) scratch[i + 1] = uchar(row[i] - previous[i]);
    z.add(scratch.data(), int(scratch.size()));
    previous = row;
}

struct EncodedPage {
    QByteArray color;   // RGB, Flate with predictor or JPEG
    QByteArray alpha;   // gray soft mask, Flate with predictor; empty when opaque
    bool jpeg = false;
};

bool hasTransparency(const QImage &img) {
    if (!img.hasAlphaChannel()) return false;
    const QImage argb = img.format() == QImage::Format_ARGB32_Premultiplied
        ? img : img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < argb.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x)
            if (qAlpha(row[x]) != 255) return true;
    }
    return false;
}

bool encode(const QImage &source, const Options &options, EncodedPage &out) {
    QImage img = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const bool transparent = !options.whiteBackground && hasTransparency(img);
    if (options.whiteBackground && img.hasAlphaChannel()) {
        QImage white(img.size(), QImage::Format_ARGB32_Premultiplied);
        white.fill(Qt::white);
        QPainter p(&white);
        p.drawImage(0, 0, img);
        p.end();
        img = white;
    }
    const int w = img.width(), h = img.height();

    if (options.jpeg) {
        // JPEG has no transparency: the colors go in as they are, the
        // transparency (if any) as a lossless mask.
        QImage rgb(w, h, QImage::Format_RGB888);
        for (int y = 0; y < h; ++y) {
            const QRgb *src = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            uchar *dst = rgb.scanLine(y);
            for (int x = 0; x < w; ++x) {
                const QRgb p = qUnpremultiply(src[x]);
                dst[3 * x] = uchar(qRed(p));
                dst[3 * x + 1] = uchar(qGreen(p));
                dst[3 * x + 2] = uchar(qBlue(p));
            }
        }
        QBuffer buffer(&out.color);
        buffer.open(QIODevice::WriteOnly);
        QImageWriter writer(&buffer, "jpeg");
        writer.setQuality(qBound(1, options.jpegQuality, 100));
        if (!writer.write(rgb)) return false;
        out.jpeg = true;
    }

    Deflater color(6), alpha(6);
    if (!color.ok() || !alpha.ok()) return false;
    std::vector<uchar> rgbRow(static_cast<size_t>(w) * 3), rgbPrev(static_cast<size_t>(w) * 3, 0);
    std::vector<uchar> aRow(static_cast<size_t>(w)), aPrev(static_cast<size_t>(w), 0), scratch;
    for (int y = 0; y < h; ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = qUnpremultiply(src[x]);
            rgbRow[size_t(3 * x)] = uchar(qRed(p));
            rgbRow[size_t(3 * x + 1)] = uchar(qGreen(p));
            rgbRow[size_t(3 * x + 2)] = uchar(qBlue(p));
            aRow[size_t(x)] = uchar(qAlpha(p));
        }
        if (!options.jpeg) addUpRow(color, rgbRow, rgbPrev, scratch);
        if (transparent) addUpRow(alpha, aRow, aPrev, scratch);
    }
    if (!options.jpeg) out.color = color.finish();
    if (transparent) out.alpha = alpha.finish();
    return !out.color.isEmpty();
}

QByteArray num(double v) {
    QByteArray s = QByteArray::number(v, 'f', 4);
    while (s.contains('.') && (s.endsWith('0') || s.endsWith('.'))) s.chop(1);
    return s.isEmpty() ? QByteArray("0") : s;
}

// A PDF text string: plain ASCII as (...), anything else as UTF-16 hex.
QByteArray pdfText(const QString &text) {
    bool ascii = true;
    for (QChar c : text) ascii = ascii && c.unicode() >= 32 && c.unicode() < 127;
    if (ascii) {
        QByteArray b = text.toLatin1();
        b.replace('\\', "\\\\").replace('(', "\\(").replace(')', "\\)");
        return '(' + b + ')';
    }
    QByteArray hex = "<FEFF";
    for (QChar c : text) hex += QByteArray::number(c.unicode(), 16).rightJustified(4, '0').toUpper();
    return hex + '>';
}

QByteArray pdfDate(const QDateTime &t) {
    const int offset = t.offsetFromUtc() / 60;
    QByteArray s = "D:" + t.toString(QStringLiteral("yyyyMMddHHmmss")).toLatin1();
    s += offset >= 0 ? '+' : '-';
    s += QByteArray::number(qAbs(offset) / 60).rightJustified(2, '0') + '\'';
    s += QByteArray::number(qAbs(offset) % 60).rightJustified(2, '0') + '\'';
    return s;
}

class Writer {
public:
    explicit Writer(QIODevice *d) : m_d(d) {}
    bool ok() const { return m_ok; }
    void put(const QByteArray &b) {
        if (m_ok && m_d->write(b) != b.size()) m_ok = false;
    }
    void object(int id, const QByteArray &dict, const QByteArray &stream = QByteArray(), bool hasStream = false) {
        if (int(m_offsets.size()) <= id) m_offsets.resize(size_t(id) + 1, 0);
        m_offsets[size_t(id)] = m_d->pos();
        put(QByteArray::number(id) + " 0 obj\n" + dict + "\n");
        if (hasStream) {
            put("stream\n");
            put(stream);
            put("\nendstream\n");
        }
        put("endobj\n");
    }
    void finish(int count, int root, int info) {
        const qint64 xref = m_d->pos();
        put("xref\n0 " + QByteArray::number(count) + "\n");
        put("0000000000 65535 f \n");
        for (int id = 1; id < count; ++id) {
            const qint64 off = id < int(m_offsets.size()) ? m_offsets[size_t(id)] : 0;
            put(QByteArray::number(off).rightJustified(10, '0') + " 00000 n \n");
        }
        put("trailer\n<< /Size " + QByteArray::number(count) + " /Root " + QByteArray::number(root)
            + " 0 R /Info " + QByteArray::number(info) + " 0 R >>\nstartxref\n"
            + QByteArray::number(xref) + "\n%%EOF\n");
    }

private:
    QIODevice *m_d;
    std::vector<qint64> m_offsets;
    bool m_ok = true;
};

} // namespace

std::vector<QImage> pagesFor(const Document &doc, const Options &options) {
    std::vector<QImage> pages;
    if (!options.layersAsPages) {
        pages.push_back(doc.flattenVisible());
        return pages;
    }
    const int n = doc.layerCount();
    for (int k = 0; k < n; ++k) {
        const int i = options.topLayerFirst ? n - 1 - k : k;
        const Layer *layer = doc.layerAt(i);
        if (!layer) continue;
        if (options.omitHidden && (!layer->isVisible() || layer->opacity() <= 0.0f)) continue;
        QImage page(doc.size(), QImage::Format_ARGB32_Premultiplied);
        page.fill(Qt::transparent);
        QPainter p(&page);
        p.setOpacity(layer->opacity());
        p.drawImage(layer->offset(), layer->image());
        p.end();
        pages.push_back(page);
    }
    return pages;
}

bool write(const QString &filePath, const std::vector<QImage> &pages, double dpi,
           const Options &options, QString *error) {
    if (pages.empty()) {
        if (error) *error = QStringLiteral("There is nothing to export: every layer is hidden.");
        return false;
    }
    if (dpi <= 0) dpi = 96;
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    Writer w(&file);
    w.put("%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");

    const int catalogId = 1, pagesId = 2, infoId = 3;
    int next = 4;
    QByteArray kids;
    for (const QImage &page : pages) {
        EncodedPage enc;
        if (!encode(page, options, enc)) {
            file.cancelWriting();
            if (error) *error = QStringLiteral("Could not compress the picture.");
            return false;
        }
        const int pageId = next++, contentId = next++, imageId = next++;
        const int maskId = enc.alpha.isEmpty() ? 0 : next++;
        // Points (1/72 inch): the picture's print size at the document's resolution.
        const double pw = page.width() / dpi * 72.0, ph = page.height() / dpi * 72.0;
        const QByteArray content = "q\n" + num(pw) + " 0 0 " + num(ph) + " 0 0 cm\n/Im0 Do\nQ\n";
        w.object(pageId, "<< /Type /Page /Parent " + QByteArray::number(pagesId) + " 0 R /MediaBox [0 0 "
                         + num(pw) + ' ' + num(ph) + "] /Resources << /XObject << /Im0 "
                         + QByteArray::number(imageId) + " 0 R >> >> /Contents "
                         + QByteArray::number(contentId) + " 0 R >>");
        w.object(contentId, "<< /Length " + QByteArray::number(content.size()) + " >>", content, true);
        const QByteArray size = " /Width " + QByteArray::number(page.width()) + " /Height "
                                + QByteArray::number(page.height()) + " /BitsPerComponent 8";
        QByteArray image = "<< /Type /XObject /Subtype /Image" + size + " /ColorSpace /DeviceRGB";
        if (enc.jpeg) {
            image += " /Filter /DCTDecode";
        } else {
            image += " /Filter /FlateDecode /DecodeParms << /Predictor 15 /Colors 3 /BitsPerComponent 8 /Columns "
                     + QByteArray::number(page.width()) + " >>";
        }
        if (maskId) image += " /SMask " + QByteArray::number(maskId) + " 0 R";
        image += " /Length " + QByteArray::number(enc.color.size()) + " >>";
        w.object(imageId, image, enc.color, true);
        if (maskId) {
            w.object(maskId, "<< /Type /XObject /Subtype /Image" + size
                                 + " /ColorSpace /DeviceGray /Filter /FlateDecode /DecodeParms << /Predictor 15"
                                   " /Colors 1 /BitsPerComponent 8 /Columns " + QByteArray::number(page.width())
                                 + " >> /Length " + QByteArray::number(enc.alpha.size()) + " >>",
                     enc.alpha, true);
        }
        kids += QByteArray::number(pageId) + " 0 R ";
    }
    w.object(catalogId, "<< /Type /Catalog /Pages " + QByteArray::number(pagesId) + " 0 R >>");
    w.object(pagesId, "<< /Type /Pages /Kids [ " + kids + "] /Count " + QByteArray::number(int(pages.size())) + " >>");
    const QByteArray producer = pdfText(QStringLiteral("Paint.with.me " PWM_VERSION_STR));
    QByteArray info = "<< /Producer " + producer + " /Creator " + producer + " /CreationDate ("
                      + pdfDate(QDateTime::currentDateTime()) + ")";
    if (!options.title.isEmpty()) info += " /Title " + pdfText(options.title);
    w.object(infoId, info + " >>");
    w.finish(next, catalogId, infoId);

    if (!w.ok()) {
        if (error) *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace PdfExport
