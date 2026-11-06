// "Extract Picture from PDF": the reader against the PDF files in
// tests/data/pdf (made by real producers and by hand, see make_fixtures.py),
// pictures written by our own PDF export, and damaged input. Headless.
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPdfWriter>
#include <QPageSize>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

#include "core/document.h"
#include "core/layer.h"
#include "core/pdfexport.h"
#include "core/pdfpictures.h"
#include "dialogs/pdfpicturedialog.h"
#include "canvas/canvaswidget.h"
#include "mainwindow.h"
#include <QAbstractButton>
#include <QMessageBox>
#include <QTimer>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; \
    std::printf("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static const QString kData = QStringLiteral(PWM_TEST_DATA_DIR) + "/pdf/";

// Largest channel difference over pixels the expected picture shows; where it
// is fully transparent, ours must be too.
static int difference(const QImage &ours, const QImage &expected) {
    if (ours.size() != expected.size()) return 999;
    const QImage a = ours.convertToFormat(QImage::Format_ARGB32);
    const QImage b = expected.convertToFormat(QImage::Format_ARGB32);
    int worst = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
            if (qAlpha(q) == 0) {
                worst = std::max(worst, qAlpha(p));
                continue;
            }
            worst = std::max({worst, std::abs(qAlpha(p) - qAlpha(q))});
            if (qAlpha(q) == 255)
                worst = std::max({worst, std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)),
                                  std::abs(qBlue(p) - qBlue(q))});
        }
    return worst;
}

static void fixtures() {
    QFile f(kData + "manifest.json");
    CHECK(f.open(QIODevice::ReadOnly));
    const QJsonObject manifest = QJsonDocument::fromJson(f.readAll()).object();
    CHECK(manifest.size() >= 18);
    for (auto it = manifest.begin(); it != manifest.end(); ++it) {
        const int failsBefore = g_fail;
        PdfPictures pdf;
        const bool opened = pdf.open(kData + it.key());
        if (it.value().isObject()) {   // expected to be refused
            CHECK(!opened && !pdf.error().isEmpty());
            if (g_fail != failsBefore) std::printf("    in %s\n", qPrintable(it.key()));
            continue;
        }
        CHECK(opened);
        const QJsonArray expected = it.value().toArray();
        CHECK(pdf.pictures().size() == expected.size());
        for (int i = 0; i < expected.size() && i < pdf.pictures().size(); ++i) {
            const QJsonObject e = expected.at(i).toObject();
            const PdfPictures::Picture &p = pdf.pictures().at(i);
            CHECK(p.page == e.value("page").toInt());
            CHECK(p.size == QSize(e.value("width").toInt(), e.value("height").toInt()));
            CHECK(std::abs(PdfPictures::suggestedDpi(p) - e.value("dpi").toDouble()) < 0.01);
            QString problem;
            const QImage image = pdf.decode(i, &problem);
            if (e.value("problem").toBool()) {
                CHECK(image.isNull() && !problem.isEmpty() && !p.problem.isEmpty());
                continue;
            }
            CHECK(!image.isNull() && problem.isEmpty());
            CHECK(image.format() == QImage::Format_ARGB32_Premultiplied);
            const QImage want(kData + e.value("png").toString());
            const int diff = difference(image, want);
            CHECK(diff <= e.value("tolerance").toInt());
            if (diff > e.value("tolerance").toInt())
                std::printf("    picture %d differs by %d\n", i, diff);
        }
        if (g_fail != failsBefore) std::printf("    in %s\n", qPrintable(it.key()));
    }
}

static void ownExport() {
    // Our own PDF export, lossless: the picture comes back pixel for pixel,
    // at the resolution it was exported with.
    QTemporaryDir dir;
    Document doc(123, 77);
    QImage img(123, 77, QImage::Format_ARGB32_Premultiplied);
    QRandomGenerator rng(7);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            img.setPixel(x, y, qRgb(rng.bounded(256), rng.bounded(256), rng.bounded(256)));
    doc.activeLayer()->setImage(img);
    doc.setDpi(254);
    PdfExport::Options options;
    const QString path = dir.filePath("own.pdf");
    CHECK(PdfExport::write(path, PdfExport::pagesFor(doc, options), doc.dpi(), options));
    PdfPictures pdf;
    CHECK(pdf.open(path));
    CHECK(pdf.pictures().size() == 1);
    if (!pdf.pictures().isEmpty()) {
        CHECK(std::abs(PdfPictures::suggestedDpi(pdf.pictures().first()) - 254.0) < 0.01);
        CHECK(difference(pdf.decode(0), img) == 0);
    }
}

static void qtWriter() {
    // A PDF made by Qt's own writer (QPdfWriter): a photo-like picture with
    // transparency, scaled onto an A4 page, and a drawn rectangle.
    QTemporaryDir dir;
    const QString path = dir.filePath("qt.pdf");
    QImage img(200, 150, QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            img.setPixel(x, y, qRgba(x, y, (x + y) / 2, x < 150 ? 255 : 0));
    {
        QPdfWriter writer(path);
        writer.setResolution(300);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter p(&writer);
        p.fillRect(QRect(100, 100, 400, 50), Qt::blue);
        p.drawImage(QRect(300, 300, 400, 300), img);   // 200 px on 400 device px at 300 dpi = 150 dpi
    }
    PdfPictures pdf;
    CHECK(pdf.open(path));
    CHECK(pdf.pictures().size() == 1);
    if (pdf.pictures().size() == 1) {
        CHECK(pdf.pictures().first().size == img.size());
        CHECK(std::abs(PdfPictures::suggestedDpi(pdf.pictures().first()) - 150.0) < 0.5);
        // Qt stores it as JPEG plus a 1-bit stencil mask: colors within JPEG
        // rounding, the transparent part exactly transparent.
        CHECK(pdf.pictures().first().compression == QStringLiteral("JPEG"));
        const QImage back = pdf.decode(0);
        CHECK(difference(back, img.convertToFormat(QImage::Format_ARGB32_Premultiplied)) <= 8);
    }
}

static void damagedInput() {
    PdfPictures pdf;
    CHECK(!pdf.openData(QByteArray("not a pdf at all")));
    CHECK(!pdf.error().isEmpty());
    CHECK(!pdf.open(kData + "does-not-exist.pdf"));
    // Cut off in the middle, and random bytes after a header: no crash, no hang.
    QFile f(kData + "reportlab-pages.pdf");
    CHECK(f.open(QIODevice::ReadOnly));
    const QByteArray whole = f.readAll();
    for (int cut : {20, 400, int(whole.size() / 2), int(whole.size() - 30)}) {
        PdfPictures part;
        part.openData(whole.left(cut));
        for (int i = 0; i < part.pictures().size(); ++i) part.decode(i);
    }
    QRandomGenerator rng(3);
    for (int round = 0; round < 50; ++round) {
        QByteArray junk = whole;
        for (int k = 0; k < 40; ++k) junk[rng.bounded(int(junk.size()))] = char(rng.bounded(256));
        PdfPictures noisy;
        noisy.openData(junk);
        for (int i = 0; i < noisy.pictures().size(); ++i) noisy.decode(i);
    }
    QByteArray garbage = "%PDF-1.7\n";
    for (int k = 0; k < 20000; ++k) garbage.append(char(rng.bounded(256)));
    PdfPictures g;
    g.openData(garbage);
    CHECK(true);   // reaching this line is the check
}

static void dialog() {
    PdfPictures pdf;
    CHECK(pdf.open(kData + "reportlab-pages.pdf"));
    PdfPictureDialog dlg(pdf, QStringLiteral("reportlab-pages.pdf"));
    CHECK(dlg.currentIndex() == 0);   // the first picture is chosen
    CHECK(dlg.currentImage().size() == QSize(8, 8));
    dlg.showNext();
    CHECK(dlg.currentIndex() == 1 && dlg.currentImage().size() == QSize(60, 40));
    CHECK(std::abs(dlg.currentDpi() - 300.0) < 0.01);
    dlg.showPrevious();
    CHECK(dlg.currentIndex() == 0);
    // A picture that cannot be decoded says why and offers no action.
    PdfPictures jpx;
    CHECK(jpx.open(kData + "jpx.pdf"));
    PdfPictureDialog j(jpx, QStringLiteral("jpx.pdf"));
    CHECK(j.currentImage().isNull() && !j.actionsEnabled());
    j.showNext();
    CHECK(!j.currentImage().isNull() && j.actionsEnabled());
    if (const QString out = qEnvironmentVariable("PWM_SCREENSHOT_DIR"); !out.isEmpty()) {
        QDir().mkpath(out);
        dlg.showNext();
        dlg.show();
        QApplication::processEvents();
        dlg.grab().save(out + "/pdf-picture-dialog.png");
    }
}

// Answers the next modal dialog: clicks the buttons named, in order.
static void answer(const QStringList &buttons, bool *seen = nullptr) {
    QTimer::singleShot(0, [buttons, seen]() {
        QWidget *modal = QApplication::activeModalWidget();
        if (seen) *seen = modal != nullptr;
        if (!modal) return;
        for (const QString &name : buttons)
            for (QAbstractButton *b : modal->findChildren<QAbstractButton *>())
                if (b->text().remove('&') == name && b->isEnabled()) { b->click(); break; }
    });
}

static void mainWindow() {
    MainWindow w;
    auto *canvas = w.findChild<CanvasWidget *>();
    Document *first = canvas->document();
    const QString pdf = kData + "reportlab-pages.pdf";

    // Open as a new image: the second picture (60 x 40, transparent right third).
    bool seen = false;
    answer({QStringLiteral("Next >"), QStringLiteral("Open as New Image")}, &seen);
    w.extractPictureFromPdf(pdf);
    CHECK(seen);
    Document *opened = canvas->document();
    CHECK(opened != first);
    CHECK(opened->size() == QSize(60, 40));
    CHECK(std::abs(opened->dpi() - 300.0) < 0.01);
    CHECK(qAlpha(opened->layerAt(0)->image().pixel(55, 20)) == 0);
    CHECK(qAlpha(opened->layerAt(0)->image().pixel(10, 20)) == 255);
    CHECK(!opened->isModified());

    // Copy to the clipboard: the first picture as it is chosen.
    answer({QStringLiteral("Copy to Clipboard")});
    w.extractPictureFromPdf(pdf);
    // Wayland lets only a focused window set the clipboard; a headless
    // compositor (CI) has no focus.
    if (QGuiApplication::platformName() != QLatin1String("wayland"))
        CHECK(QApplication::clipboard()->image().size() == QSize(8, 8));
    CHECK(canvas->document() == opened);

    // Add as a new layer of the open image.
    const int layers = opened->layerCount();
    answer({QStringLiteral("Next >"), QStringLiteral("Next >"), QStringLiteral("Add as New Layer")});
    w.extractPictureFromPdf(pdf);
    CHECK(opened->layerCount() == layers + 1);
    CHECK(opened->activeLayer()->name() == QStringLiteral("reportlab-pages, picture 3"));

    // File > Open with a PDF goes the same way; Cancel does nothing.
    answer({QStringLiteral("Cancel")}, &seen);
    w.openFile(pdf);
    CHECK(seen && canvas->document() == opened);

    // A PDF without pictures, and one that is encrypted, say so.
    QTemporaryDir dir;
    const QString textOnly = dir.filePath("text.pdf");
    {
        QPdfWriter writer(textOnly);
        QPainter p(&writer);
        p.drawText(QPointF(100, 100), QStringLiteral("Only text"));
    }
    for (const QString &path : {textOnly, kData + "encrypted.pdf"}) {
        seen = false;
        QTimer::singleShot(0, [&seen]() {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                box && box->objectName() == QStringLiteral("pdfPictureError")) {
                seen = true;
                box->accept();
            }
        });
        w.extractPictureFromPdf(path);
        CHECK(seen);
    }
    for (int i = 0; i < 4; ++i) canvas->document()->setModified(false);
    first->setModified(false);
    opened->setModified(false);
}

int main(int argc, char **argv) {
    QTemporaryDir home;
    for (const char *name : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"}) {
        const QString path = home.path() + '/' + QString::fromLatin1(name);
        QDir().mkpath(path);
        qputenv(name, path.toUtf8());
    }
    QApplication app(argc, argv);
    fixtures();
    ownExport();
    qtWriter();
    damagedInput();
    dialog();
    mainWindow();
    std::printf("pdf pictures: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
