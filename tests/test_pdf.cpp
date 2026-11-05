// PDF export: structure, page size from the resolution, exact pixels in the
// lossless mode, JPEG, transparency, one page per layer. Headless.
//
// PWM_PDF_OUT=<folder> keeps the files, for checking with other programs
// (pdfinfo, qpdf --check, pdftoppm).
#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <zlib.h>
#include <cstdio>
#include <vector>

#include "core/document.h"
#include "core/layer.h"
#include "core/pdfexport.h"
#include "mainwindow.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; \
    std::printf("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

// ---- A tiny reader for what we write ----

struct PdfObject {
    int id = 0;
    QByteArray dict;
    QByteArray stream;
};

struct Pdf {
    QByteArray bytes;
    std::vector<PdfObject> objects;
    bool xrefValid = false;
    const PdfObject *find(int id) const {
        for (const auto &o : objects) if (o.id == id) return &o;
        return nullptr;
    }
    std::vector<const PdfObject *> pages() const {
        std::vector<const PdfObject *> out;
        // Pages in /Kids order.
        for (const auto &o : objects) {
            if (!o.dict.contains("/Type /Pages")) continue;
            const QRegularExpression ref(QStringLiteral("(\\d+) 0 R"));
            const QString kids = QString::fromLatin1(o.dict.mid(o.dict.indexOf("/Kids")));
            auto it = ref.globalMatch(kids.left(kids.indexOf(']')));
            while (it.hasNext()) out.push_back(find(it.next().captured(1).toInt()));
        }
        return out;
    }
};

static int intAfter(const QByteArray &dict, const char *key) {
    const int i = dict.indexOf(key);
    if (i < 0) return -1;
    return QByteArray(dict.mid(i + int(qstrlen(key))).trimmed().split(' ').value(0)).replace('>', "").toInt();
}

static int refAfter(const QByteArray &dict, const char *key) {
    const int i = dict.indexOf(key);
    if (i < 0) return 0;
    return dict.mid(i + int(qstrlen(key))).trimmed().split(' ').value(0).toInt();
}

static Pdf parse(const QString &path) {
    Pdf pdf;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return pdf;
    pdf.bytes = f.readAll();
    const QByteArray &b = pdf.bytes;
    int pos = 0;
    const QRegularExpression head(QStringLiteral("^(\\d+) 0 obj\\n"));
    while ((pos = b.indexOf(" 0 obj\n", pos)) >= 0) {
        int start = pos;
        while (start > 0 && b[start - 1] >= '0' && b[start - 1] <= '9') --start;
        PdfObject o;
        o.id = b.mid(start, pos - start).toInt();
        const int dictStart = pos + 7;
        const int end = b.indexOf("\nendobj\n", dictStart);
        const int streamAt = b.indexOf("\nstream\n", dictStart);
        if (streamAt >= 0 && streamAt < end) {
            o.dict = b.mid(dictStart, streamAt - dictStart);
            const int len = intAfter(o.dict, "/Length");
            o.stream = b.mid(streamAt + 8, len);
        } else {
            o.dict = b.mid(dictStart, end - dictStart);
        }
        pdf.objects.push_back(o);
        pos = end;
    }
    // startxref -> "xref", every entry -> "<id> 0 obj"
    const int sx = b.lastIndexOf("startxref\n");
    const qint64 xref = b.mid(sx + 10).split('\n').value(0).toLongLong();
    pdf.xrefValid = b.mid(int(xref), 4) == "xref";
    if (pdf.xrefValid) {
        const QList<QByteArray> lines = b.mid(int(xref)).split('\n');
        const int count = lines.value(1).split(' ').value(1).toInt();
        for (int id = 1; id < count; ++id) {
            const QByteArray entry = lines.value(2 + id);
            const qint64 off = entry.left(10).toLongLong();
            if (entry.size() != 19 || !b.mid(int(off)).startsWith(QByteArray::number(id) + " 0 obj\n"))
                pdf.xrefValid = false;
        }
    }
    return pdf;
}

// Inflate and undo the PNG "Up" predictor.
static QByteArray unpredict(const QByteArray &deflated, int columns, int colors, int rows) {
    QByteArray raw(int((columns * colors + 1) * rows), '\0');
    uLongf len = uLongf(raw.size());
    if (uncompress(reinterpret_cast<Bytef *>(raw.data()), &len,
                   reinterpret_cast<const Bytef *>(deflated.constData()), uLong(deflated.size())) != Z_OK)
        return QByteArray();
    const int stride = columns * colors;
    QByteArray out(stride * rows, '\0');
    for (int y = 0; y < rows; ++y) {
        const char *in = raw.constData() + y * (stride + 1);
        if (in[0] != 2) return QByteArray();
        for (int i = 0; i < stride; ++i) {
            const uchar up = y > 0 ? uchar(out[(y - 1) * stride + i]) : 0;
            out[y * stride + i] = char(uchar(in[1 + i]) + up);
        }
    }
    return out;
}

static void keep(const QString &path, const char *name) {
    const QByteArray dir = qgetenv("PWM_PDF_OUT");
    if (dir.isEmpty()) return;
    const QString target = QString::fromLocal8Bit(dir) + '/' + name + ".pdf";
    QFile::remove(target);
    QFile::copy(path, target);
}

// 120 x 80 px at 600 dpi: paper, a translucent blue ellipse, a hidden note.
static void makeDocument(Document &doc) {
    doc.setDpi(600);
    doc.activeLayer()->clear(QColor(250, 240, 220));
    doc.setLayerName(0, "Paper");
    QImage art(120, 80, QImage::Format_ARGB32_Premultiplied);
    art.fill(Qt::transparent);
    {
        QPainter p(&art);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(30, 90, 200, 180));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPoint(60, 40), 35, 25);
    }
    doc.addLayer(art, "Art");
    QImage note(120, 80, QImage::Format_ARGB32_Premultiplied);
    note.fill(Qt::transparent);
    { QPainter p(&note); p.fillRect(5, 5, 20, 10, Qt::red); }
    doc.addLayer(note, "Hidden note");
    doc.setLayerVisibility(2, false);
}

static void wholeImage() {
    std::printf("== The image as one lossless page ==\n");
    QTemporaryDir dir;
    Document doc(120, 80);
    makeDocument(doc);
    const QString path = dir.path() + "/cover.pdf";
    PdfExport::Options o;
    o.title = QString::fromUtf8("Cover \xc3\xa4");
    CHECK(PdfExport::write(path, PdfExport::pagesFor(doc, o), doc.dpi(), o));
    keep(path, "whole");
    const Pdf pdf = parse(path);
    CHECK(pdf.bytes.startsWith("%PDF-1.4\n") && pdf.bytes.endsWith("%%EOF\n"));
    CHECK(pdf.xrefValid);
    const auto pages = pdf.pages();
    CHECK(pages.size() == 1);
    if (pages.size() != 1 || !pages[0]) return;
    // 120 x 80 px at 600 dpi = 14.4 x 9.6 pt.
    CHECK(pages[0]->dict.contains("/MediaBox [0 0 14.4 9.6]"));
    const PdfObject *image = pdf.find(refAfter(pages[0]->dict, "/Im0"));
    CHECK(image && image->dict.contains("/FlateDecode") && !image->dict.contains("/SMask"));
    if (!image) return;
    const QByteArray rgb = unpredict(image->stream, 120, 3, 80);
    CHECK(rgb.size() == 120 * 80 * 3);
    const QImage expected = doc.flattenVisible();
    bool same = rgb.size() == 120 * 80 * 3;
    for (int y = 0; same && y < 80; ++y)
        for (int x = 0; same && x < 120; ++x) {
            const QRgb p = qUnpremultiply(expected.pixel(x, y));
            const int i = (y * 120 + x) * 3;
            same = uchar(rgb[i]) == qRed(p) && uchar(rgb[i + 1]) == qGreen(p) && uchar(rgb[i + 2]) == qBlue(p);
        }
    CHECK(same);   // every pixel exact
    CHECK(pdf.bytes.contains("/Title <FEFF0043006F0076006500720020"));
    CHECK(pdf.bytes.contains("/Producer (Paint.with.me"));
}

static void layersAndTransparency() {
    std::printf("== One page per layer, transparency ==\n");
    QTemporaryDir dir;
    Document doc(120, 80);
    makeDocument(doc);
    PdfExport::Options o;
    o.layersAsPages = true;
    const QString path = dir.path() + "/layers.pdf";
    CHECK(PdfExport::write(path, PdfExport::pagesFor(doc, o), doc.dpi(), o));
    keep(path, "layers");
    Pdf pdf = parse(path);
    CHECK(pdf.xrefValid);
    auto pages = pdf.pages();
    CHECK(pages.size() == 2);   // the hidden layer is left out
    if (pages.size() == 2) {
        // Top layer first: "Art" has transparency, "Paper" not.
        const PdfObject *art = pdf.find(refAfter(pages[0]->dict, "/Im0"));
        const PdfObject *paper = pdf.find(refAfter(pages[1]->dict, "/Im0"));
        CHECK(art && art->dict.contains("/SMask"));
        CHECK(paper && !paper->dict.contains("/SMask"));
        if (art) {
            const PdfObject *mask = pdf.find(refAfter(art->dict, "/SMask"));
            CHECK(mask && mask->dict.contains("/DeviceGray"));
            if (mask) {
                const QByteArray a = unpredict(mask->stream, 120, 1, 80);
                CHECK(a.size() == 120 * 80 && uchar(a[0]) == 0 && qAbs(int(uchar(a[40 * 120 + 60])) - 180) <= 1);
            }
        }
    }
    o.omitHidden = false;
    o.topLayerFirst = false;
    CHECK(PdfExport::pagesFor(doc, o).size() == 3);
    // White background: no soft masks at all.
    o.whiteBackground = true;
    CHECK(PdfExport::write(path, PdfExport::pagesFor(doc, o), doc.dpi(), o));
    pdf = parse(path);
    CHECK(pdf.xrefValid && !pdf.bytes.contains("/SMask"));
    // Nothing visible: a clear message instead of an empty file.
    Document empty(10, 10);
    empty.setLayerVisibility(0, false);
    PdfExport::Options only;
    only.layersAsPages = true;
    QString error;
    CHECK(!PdfExport::write(path + "2", PdfExport::pagesFor(empty, only), 96, only, &error) && !error.isEmpty());
}

static void jpegAndLarge() {
    std::printf("== JPEG; a 7625 x 5250 px cover at 600 dpi ==\n");
    QTemporaryDir dir;
    Document doc(7625, 5250);
    doc.setDpi(600);
    {
        QPainter p(&doc.activeLayer()->image());
        QLinearGradient g(0, 0, 7625, 5250);
        g.setColorAt(0, QColor(120, 255, 80));
        g.setColorAt(1, QColor(110, 140, 255));
        p.fillRect(QRect(0, 0, 7625, 5250), g);
    }
    PdfExport::Options o;
    o.jpeg = true;
    o.jpegQuality = 90;
    const QString path = dir.path() + "/big.pdf";
    QElapsedTimer t;
    t.start();
    CHECK(PdfExport::write(path, PdfExport::pagesFor(doc, o), doc.dpi(), o));
    std::printf("  JPEG export: %.1f s, %.1f MB\n", t.elapsed() / 1000.0, QFileInfo(path).size() / 1e6);
    keep(path, "big-jpeg");
    Pdf pdf = parse(path);
    CHECK(pdf.xrefValid);
    auto pages = pdf.pages();
    CHECK(pages.size() == 1 && pages[0]->dict.contains("/MediaBox [0 0 915 630]"));   // 322.8 x 222.3 mm
    const PdfObject *image = pages.empty() ? nullptr : pdf.find(refAfter(pages[0]->dict, "/Im0"));
    CHECK(image && image->dict.contains("/DCTDecode") && image->stream.startsWith("\xFF\xD8"));
    CHECK(qAbs(PdfExport::millimeters(7625, 600) - 322.79) < 0.01);

    o.jpeg = false;
    t.restart();
    CHECK(PdfExport::write(path, PdfExport::pagesFor(doc, o), doc.dpi(), o));
    std::printf("  lossless export: %.1f s, %.1f MB\n", t.elapsed() / 1000.0, QFileInfo(path).size() / 1e6);
    keep(path, "big-lossless");
    CHECK(parse(path).xrefValid);
}

static void fromMainWindow() {
    std::printf("== File > Export as PDF ==\n");
    QTemporaryDir dir;
    MainWindow window;
    QAction *menu = nullptr;
    for (QAction *a : window.findChildren<QAction *>())
        if (a->text() == QStringLiteral("&Export as PDF...")) menu = a;
    CHECK(menu != nullptr);
    const QString path = dir.path() + "/window.pdf";
    CHECK(window.exportPdfTo(path, PdfExport::Options()));
    const Pdf pdf = parse(path);
    CHECK(pdf.xrefValid && pdf.pages().size() == 1);
    CHECK(pdf.bytes.contains("/Title (Untitled)"));
}

int main(int argc, char **argv) {
    QTemporaryDir home;
    for (const char *name : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"}) {
        const QString path = home.path() + '/' + QString::fromLatin1(name);
        QDir().mkpath(path);
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv(name, path.toUtf8());
    }
    QApplication app(argc, argv);
    wholeImage();
    layersAndTransparency();
    jpegAndLarge();
    fromMainWindow();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
