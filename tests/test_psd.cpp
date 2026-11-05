// Photoshop documents: write and read back, files from GIMP and Krita, and
// synthetic files for CMYK, grayscale, 16-bit, ZIP, .psb and flat documents
// (tests/data/psd, see make_fixtures.py there). Headless.
#include <QAbstractButton>
#include <QApplication>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <functional>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QTemporaryDir>
#include <cstdio>

#include "canvas/canvaswidget.h"
#include "core/document.h"
#include "core/layer.h"
#include "core/psdformat.h"
#include "appinfo.h"
#include "mainwindow.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; \
    std::printf("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static const QString kData = QStringLiteral(PWM_TEST_DATA_DIR "/psd/");

// PWM_PSD_OUT=<folder> also keeps the written files (and a PNG of how they
// should look) for checking in other programs.
static void keep(const Document &doc, const QString &psdPath, const char *name) {
    const QByteArray dir = qgetenv("PWM_PSD_OUT");
    if (dir.isEmpty()) return;
    const QString base = QString::fromLocal8Bit(dir) + '/' + name;
    QFile::remove(base + ".psd");
    QFile::copy(psdPath, base + ".psd");
    doc.flatten().save(base + ".png");
}

// What a straight-alpha round trip keeps of a premultiplied pixel.
static QImage roundTripped(const QImage &img) {
    QImage out = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < out.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) row[x] = qPremultiply(qUnpremultiply(row[x]));
    }
    return out;
}

static QColor px(const Document &doc, int layer, int x, int y) {
    const Layer *l = doc.layerAt(layer);
    const QPoint p = QPoint(x, y) - l->offset();
    return QColor::fromRgba(qUnpremultiply(l->image().pixel(p)));
}

static void roundTrip() {
    std::printf("== Save and open again ==\n");
    QTemporaryDir dir;
    Document doc(64, 48);
    doc.setDpi(600);
    doc.activeLayer()->clear(Qt::white);
    doc.setLayerName(0, "Paper");

    // Every alpha value, and colors that are hard to keep through straight alpha.
    QImage ramp(64, 48, QImage::Format_ARGB32);
    ramp.fill(Qt::transparent);
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 64; ++x)
            if (x > 3 && y > 1) ramp.setPixelColor(x, y, QColor((x * 37) % 256, (y * 53) % 256, (x * y) % 256, (x * 4 + y) % 256));
    doc.addLayer(ramp, QString::fromUtf8("Ebene \xc3\xa4 \xe6\x96\x87\xe5\xad\x97"));
    doc.setLayerOpacity(1, 0.37f);
    doc.setLayerBlendMode(1, BlendMode::Multiply);

    QImage dot(64, 48, QImage::Format_ARGB32_Premultiplied);
    dot.fill(Qt::transparent);
    { QPainter p(&dot); p.fillRect(30, 20, 5, 4, QColor(0, 160, 255)); }
    doc.addLayer(dot, "Hidden locked screen");
    doc.setLayerVisibility(2, false);
    doc.setLayerLocked(2, true);
    doc.setLayerBlendMode(2, BlendMode::Screen);

    doc.addLayer(QString("Empty"));
    doc.setLayerBlendMode(3, BlendMode::Additive);
    doc.setActiveLayer(2);

    const QString path = dir.path() + "/round.psd";
    CHECK(Document::isLayeredFormat(path));
    CHECK(doc.save(path));
    CHECK(Psd::isPsdFile(path));
    CHECK(doc.filePath() == path && !doc.isModified());
    keep(doc, path, "round");

    Document back;
    CHECK(back.load(path));
    CHECK(back.loadNotes().isEmpty());
    CHECK(back.size() == QSize(64, 48));
    CHECK(back.layerCount() == 4);
    if (back.layerCount() != 4) return;
    CHECK(qAbs(back.dpi() - 600) < 0.01);
    CHECK(back.activeLayerIndex() == 2);
    CHECK(back.layerAt(0)->name() == "Paper");
    CHECK(back.layerAt(1)->name() == QString::fromUtf8("Ebene \xc3\xa4 \xe6\x96\x87\xe5\xad\x97"));
    CHECK(qAbs(back.layerAt(1)->opacity() - 0.37f) < 0.003f);
    CHECK(back.layerAt(1)->blendMode() == BlendMode::Multiply);
    CHECK(!back.layerAt(2)->isVisible() && back.layerAt(2)->isLocked());
    CHECK(back.layerAt(2)->blendMode() == BlendMode::Screen);
    CHECK(back.layerAt(3)->blendMode() == BlendMode::Additive);
    CHECK(back.layerAt(1)->isVisible() && !back.layerAt(1)->isLocked());
    for (int i = 0; i < 4; ++i) {
        CHECK(back.layerAt(i)->image().size() == QSize(64, 48) && back.layerAt(i)->offset().isNull());
        CHECK(!back.layerAt(i)->retainsOffCanvasPixels());
        CHECK(back.layerAt(i)->image() == roundTripped(doc.layerAt(i)->image()));
    }

    // Overwriting keeps a valid file and does not leave temporary files.
    CHECK(back.save(path));
    CHECK(QDir(dir.path()).entryList(QDir::Files).size() == 1);
}

static void offCanvas() {
    std::printf("== Layers reaching past the canvas ==\n");
    QTemporaryDir dir;
    Document doc(800, 600);
    QImage big(840, 630, QImage::Format_ARGB32_Premultiplied);
    big.fill(Qt::transparent);
    { QPainter p(&big); p.fillRect(0, 0, 20, 20, Qt::yellow); p.fillRect(805, 595, 25, 25, Qt::blue); }
    doc.pasteImage(big, "Pasted", false);
    doc.activeLayer()->setOffset(QPoint(-50, -40));
    CHECK(doc.activeLayer()->retainsOffCanvasPixels());
    const QString path = dir.path() + "/off.psd";
    CHECK(doc.save(path));
    keep(doc, path, "off-canvas");
    Document back;
    CHECK(back.load(path));
    CHECK(back.layerCount() == 2);
    if (back.layerCount() != 2) return;
    const Layer *l = back.layerAt(1);
    CHECK(l->retainsOffCanvasPixels());
    CHECK(px(back, 1, -50, -40) == QColor(Qt::yellow));    // left of and above the canvas
    CHECK(px(back, 1, 755 + 24, 555 + 24) == QColor(Qt::blue));
    CHECK(back.flatten() == doc.flatten());
}

static void limits() {
    std::printf("== What a PSD cannot keep ==\n");
    std::vector<Layer> layers;
    layers.emplace_back(4, 4, "Plain");
    Layer glow(4, 4, "Shine");
    glow.setBlendMode(BlendMode::Glow);
    layers.push_back(glow);
    Layer xr(4, 4, "Exclusive");
    xr.setBlendMode(BlendMode::Xor);
    layers.push_back(xr);
    const QStringList lost = Psd::layersWithoutPsdBlendMode(layers);
    CHECK(lost.size() == 2 && lost.value(0).startsWith("Shine"));
    bool exact = true;
    CHECK(Psd::blendModeKey(BlendMode::Reflect, &exact) == "norm" && !exact);
    CHECK(Psd::blendModeKey(BlendMode::ColorBurn, &exact) == "idiv" && exact);
    BlendMode m;
    CHECK(Psd::blendModeFromKey("div ", &m) && m == BlendMode::ColorDodge);
    CHECK(!Psd::blendModeFromKey("sLit", &m));

    // Too large for .psd.
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    QString error;
    CHECK(!Psd::write(&buffer, layers, QSize(Psd::kMaxSide + 1, 10), {}, &error) && !error.isEmpty());

    // Damaged and foreign files fail with a message instead of crashing.
    Psd::ImageData out;
    CHECK(!Psd::read(QByteArray("8BPS\0\1", 6), out, &error) && !error.isEmpty());
    CHECK(!Psd::read(QByteArray("GIF89a"), out, &error));
    QBuffer good;
    good.open(QIODevice::ReadWrite);
    CHECK(Psd::write(&good, layers, QSize(4, 4)));
    const QByteArray bytes = good.data();
    for (int cut : {30, 60, int(bytes.size()) / 2, int(bytes.size()) - 3}) {
        Psd::ImageData partial;
        Psd::read(bytes.left(cut), partial, &error);   // must not crash
    }
    QByteArray noisy = bytes;
    for (int i = 40; i < noisy.size(); i += 7) noisy[i] = char(noisy[i] ^ 0x5a);
    Psd::ImageData garbled;
    Psd::read(noisy, garbled, &error);   // must not crash
    CHECK(true);
}

static void fromGimpAndKrita() {
    std::printf("== Files from GIMP and Krita ==\n");
    Document gimp;
    CHECK(gimp.load(kData + "gimp.psd"));
    CHECK(gimp.layerCount() == 3);
    if (gimp.layerCount() == 3) {
        CHECK(gimp.layerAt(1)->name() == "Red half");
        CHECK(gimp.layerAt(1)->blendMode() == BlendMode::Multiply);
        CHECK(qAbs(gimp.layerAt(1)->opacity() - 128 / 255.0f) < 0.003f);
        CHECK(px(gimp, 1, 10, 5) == QColor(Qt::red) && px(gimp, 1, 9, 5).alpha() == 0);
        CHECK(!gimp.layerAt(2)->isVisible() && gimp.layerAt(2)->blendMode() == BlendMode::Screen);
        CHECK(px(gimp, 0, 0, 0) == QColor(Qt::white));
    }

    Document krita;
    CHECK(krita.load(kData + "krita.psd"));
    CHECK(krita.layerCount() == 3 && qAbs(krita.dpi() - 600) < 0.5);
    if (krita.layerCount() == 3) {
        CHECK(krita.layerAt(2)->name() == "Semi" && krita.layerAt(2)->blendMode() == BlendMode::Screen);
        const QColor semi = px(krita, 2, 25, 15);
        CHECK(semi.blue() == 255 && qAbs(semi.alpha() - 128) <= 1);
        CHECK(!krita.layerAt(1)->isVisible());
    }

    Document grouped;
    CHECK(grouped.load(kData + "gimp-group-mask.psd"));
    // Paper, In group A, In group B, Masked: the group records are not layers.
    CHECK(grouped.layerCount() == 4);
    if (grouped.layerCount() == 4) {
        CHECK(grouped.layerAt(1)->name() == "In group A" && grouped.layerAt(2)->name() == "In group B");
        CHECK(qAbs(grouped.layerAt(1)->opacity() - 128 / 255.0f) < 0.003f);   // group's 50 %
        CHECK(qAbs(grouped.layerAt(0)->opacity() - 1.0f) < 0.001f);
        // The mask hides the right half of "Masked".
        CHECK(px(grouped, 3, 4, 4) == QColor(Qt::red));
        CHECK(px(grouped, 3, 20, 4).alpha() == 0);
    }
    const QString notes = grouped.loadNotes().join(' ');
    CHECK(notes.contains("group") && notes.contains("mask"));

    Document deep;
    CHECK(deep.load(kData + "gimp-16bit.psd"));
    CHECK(deep.layerCount() == 4 && deep.loadNotes().join(' ').contains("16 bits"));
    if (deep.layerCount() == 4) CHECK(px(deep, 3, 4, 4) == QColor(Qt::red));
}

static void synthetic() {
    std::printf("== CMYK, grayscale, 16-bit ZIP, PSB, flat ==\n");
    Document cmyk;
    CHECK(cmyk.load(kData + "cmyk.psd"));
    CHECK(cmyk.layerCount() == 1);
    CHECK(px(cmyk, 0, 0, 0) == QColor(0, 255, 255));   // cyan ink
    CHECK(px(cmyk, 0, 5, 3) == QColor(0, 0, 0));       // black ink
    CHECK(cmyk.loadNotes().join(' ').contains("CMYK"));

    Document gray;
    CHECK(gray.load(kData + "gray.psd"));
    CHECK(gray.layerCount() == 2);
    if (gray.layerCount() == 2) {
        CHECK(px(gray, 0, 3, 0) == QColor(150, 150, 150));
        CHECK(!gray.layerAt(1)->isVisible() && qAbs(gray.layerAt(1)->opacity() - 0.4f) < 0.003f);
    }

    Document deep;
    CHECK(deep.load(kData + "zip16-lr16.psd"));
    CHECK(deep.layerCount() == 1 && qAbs(deep.dpi() - 300) < 0.01);
    if (deep.layerCount() == 1) {
        const QColor c = px(deep, 0, 5, 3);
        CHECK(c.red() == 255 && c.green() == 255 && c.alpha() == 255);
        CHECK(qAbs(px(deep, 0, 2, 0).green() - 102) <= 2 && qAbs(px(deep, 0, 2, 0).alpha() - 64) <= 1);
        CHECK(deep.layerAt(0)->blendMode() == BlendMode::Multiply);
    }

    Document psb;
    CHECK(psb.load(kData + "large.psb"));
    CHECK(Document::isOpenOnlyFormat(kData + "large.psb"));
    CHECK(psb.layerCount() == 2);
    if (psb.layerCount() == 2) {
        CHECK(psb.layerAt(1)->retainsOffCanvasPixels());
        CHECK(px(psb, 1, -2, 1) == QColor(Qt::red) && px(psb, 1, 1, 2) == QColor(Qt::red));
        CHECK(psb.layerAt(1)->blendMode() == BlendMode::Normal);
        CHECK(psb.loadNotes().join(' ').contains("Soft Light"));
    }

    Document flat;
    CHECK(flat.load(kData + "flat.psd"));
    CHECK(flat.layerCount() == 1 && px(flat, 0, 2, 2) == QColor(200, 100, 50));
}

static void resolution() {
    std::printf("== Resolution ==\n");
    QTemporaryDir dir;
    // Paint.NET keeps it as EXIF tags; both samples say 96 dpi.
    for (const char *name : {"Untitled3.pdn", "oldPDN3510.pdn"}) {
        Document pdn;
        CHECK(pdn.load(QStringLiteral(PWM_TEST_DATA_DIR "/pdn/") + name));
        std::printf("  %s: %.2f dpi\n", name, pdn.dpi());
        CHECK(qAbs(pdn.dpi() - 96) < 0.01);
    }
    // 96/1 -> 600/1 in a copy (same length, so the file stays valid).
    {
        QFile in(QStringLiteral(PWM_TEST_DATA_DIR "/pdn/Untitled3.pdn"));
        CHECK(in.open(QIODevice::ReadOnly));
        QByteArray bytes = in.readAll();
        CHECK(bytes.count("YAAAAAEAAAA=") >= 1);
        bytes.replace("YAAAAAEAAAA=", "WAIAAAEAAAA=");
        const QString copy = dir.path() + "/600dpi.pdn";
        QFile out(copy);
        CHECK(out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size());
        out.close();
        Document pdn;
        CHECK(pdn.load(copy) && qAbs(pdn.dpi() - 600) < 0.01);
        // ... and saved as .psd it stays 600 dpi.
        CHECK(pdn.save(dir.path() + "/600dpi.psd"));
        Document psd;
        CHECK(psd.load(dir.path() + "/600dpi.psd") && qAbs(psd.dpi() - 600) < 0.01);
    }
    // Flat images keep it too.
    Document doc(20, 10);
    doc.setDpi(300);
    const QString png = dir.path() + "/print.png";
    CHECK(doc.save(png));
    Document back;
    CHECK(back.load(png) && qAbs(back.dpi() - 300) < 0.1);
    // The rulers use the document's resolution.
    CanvasWidget canvas;
    canvas.setDocument(&back);
    CHECK(qAbs(canvas.dpi() - 300) < 0.1);
    canvas.setDpi(600);
    CHECK(qAbs(back.dpi() - 600) < 0.01);
}

// Answers the next modal question by clicking `button`; `seen` tells whether
// one came up. Delete the returned timer (a QPointer) once the step is over.
static QPointer<QTimer> answerNextQuestion(QMessageBox::StandardButton button, bool *seen, int delayMs = 50) {
    QPointer<QTimer> timer = new QTimer;
    timer->setInterval(delayMs);
    QTimer *raw = timer;
    QObject::connect(raw, &QTimer::timeout, raw, [raw, button, seen]() {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            if (seen) *seen = true;
            raw->stop();
            if (QAbstractButton *b = box->button(button)) b->click();
            else box->reject();
        }
    });
    timer->start();
    return timer;
}

static void waitFor(const std::function<bool()> &done, int ms = 20000) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
}

static void mainWindow() {
    std::printf("== Main window: open, save, autosave, recovery ==\n");
    QTemporaryDir dir;
    const QString recovery = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recovery/autosave.psd";
    {
        QSettings(AppInfo::kId, AppInfo::kId).remove("recovery");
        MainWindow window;
        auto *canvas = window.findChild<CanvasWidget *>();
        // A file with groups and masks opens, with a note that does not block.
        window.openFile(kData + "gimp-group-mask.psd");
        QApplication::processEvents();
        Document *doc = canvas->document();
        CHECK(doc->layerCount() == 4);
        QMessageBox *note = nullptr;
        for (QWidget *w : QApplication::topLevelWidgets())
            if (auto *box = qobject_cast<QMessageBox *>(w); box && box->isVisible()) note = box;
        CHECK(note != nullptr && note->informativeText().contains("group"));
        if (note) note->close();

        // Save to a .psd: a layer in Glow mode asks once, then saves.
        doc->setLayerBlendMode(1, BlendMode::Glow);
        const QString path = dir.path() + "/cover.psd";
        doc->setFilePath(path);
        bool asked = false;
        QPointer<QTimer> answer = answerNextQuestion(QMessageBox::Save, &asked);
        CHECK(QMetaObject::invokeMethod(&window, "saveDocument"));
        delete answer.data();
        CHECK(asked && QFile::exists(path) && !doc->isModified());
        asked = false;
        doc->setModified(true);
        answer = answerNextQuestion(QMessageBox::Cancel, &asked, 20);
        CHECK(QMetaObject::invokeMethod(&window, "saveDocument"));
        QApplication::processEvents();
        delete answer.data();
        CHECK(!asked && !doc->isModified());   // not asked again for this image

        // Autosave keeps the layers.
        doc->setModified(true);
        QTimer *autosave = nullptr;
        for (QTimer *t : window.findChildren<QTimer *>())
            if (t->interval() == 60 * 1000) autosave = t;
        CHECK(autosave != nullptr);
        if (autosave) QMetaObject::invokeMethod(autosave, "timeout");
        waitFor([&]() { return QSettings(AppInfo::kId, AppInfo::kId).value("recovery/pending").toBool(); });
        CHECK(QFile::exists(recovery));
        Document snapshot;
        CHECK(snapshot.load(recovery) && snapshot.layerCount() == 4);
        CHECK(QSettings(AppInfo::kId, AppInfo::kId).value("recovery/origPath").toString() == path);
        doc->setModified(false);   // closing must not ask
    }
    {
        // Next start: the autosave is offered and comes back with its layers,
        // saving to the original file again.
        bool asked = false;
        QPointer<QTimer> answer = answerNextQuestion(QMessageBox::Yes, &asked, 100);
        MainWindow window;
        auto *canvas = window.findChild<CanvasWidget *>();
        waitFor([&]() { return asked && canvas->document()->layerCount() == 4; }, 5000);
        delete answer.data();
        Document *doc = canvas->document();
        CHECK(asked && doc->layerCount() == 4);
        CHECK(doc->filePath() == dir.path() + "/cover.psd" && doc->isModified());
        CHECK(QMetaObject::invokeMethod(&window, "saveDocument"));
        CHECK(!doc->isModified());
        // Saved: the recovery snapshot is no longer needed.
        CHECK(!QFile::exists(recovery));
        CHECK(!QSettings(AppInfo::kId, AppInfo::kId).value("recovery/pending").toBool());
    }
}

static void largeDocument() {
    std::printf("== 2000 x 2000 px, 6 layers ==\n");
    QTemporaryDir dir;
    Document doc(2000, 2000);
    for (int i = 0; i < 5; ++i) {
        QImage img(2000, 2000, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor::fromHsv(i * 60, 200, 230, 160));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPoint(300 + i * 300, 1000), 400, 700);
        p.end();
        doc.addLayer(img, QString("Shape %1").arg(i));
    }
    const QString path = dir.path() + "/big.psd";
    CHECK(doc.save(path));
    keep(doc, path, "big");
    Document back;
    CHECK(back.load(path));
    CHECK(back.layerCount() == 6);
    bool same = back.layerCount() == 6;
    for (int i = 0; same && i < 6; ++i) same = back.layerAt(i)->image() == roundTripped(doc.layerAt(i)->image());
    CHECK(same);
    std::printf("  file size: %.1f MB\n", QFileInfo(path).size() / 1e6);
}

int main(int argc, char **argv) {
    // Settings, autosave and recent files go to a temporary folder.
    QTemporaryDir home;
    for (const char *name : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"}) {
        const QString path = home.path() + '/' + QString::fromLatin1(name);
        QDir().mkpath(path);
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv(name, path.toUtf8());
    }
    QApplication app(argc, argv);
    roundTrip();
    offCanvas();
    limits();
    fromGimpAndKrita();
    synthetic();
    resolution();
    mainWindow();
    largeDocument();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
