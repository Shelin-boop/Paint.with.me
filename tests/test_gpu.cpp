// Faster combining of layers (2026-11-26): the processor keeps the layers
// below and above the active one combined (LayerGroupCache); the graphics
// card combines and shows the image (GpuCompositor, GpuCanvasView).
//
// The graphics card part needs OpenGL 3.0: run under a display with OpenGL,
// e.g. `xvfb-run -a env QT_QPA_PLATFORM=xcb build/pwm_gpu_tests` (Mesa's
// software renderer is enough). Without one it is skipped, unless
// PWM_REQUIRE_GPU=1 (CI), which makes that a failure.
#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QPainter>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <cstdlib>
#include <functional>

#include "appinfo.h"
#include "canvas/canvaswidget.h"
#include "canvas/gpucanvasview.h"
#include "canvas/gpucompositor.h"
#include "canvas/gpusupport.h"
#include "canvas/layergroupcache.h"
#include "core/document.h"
#include "core/layer.h"
#include "core/selection.h"
#include "tools/brushtool.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; \
    std::printf("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static void settle(int rounds = 5) {
    for (int i = 0; i < rounds; ++i) QApplication::processEvents();
}

static bool waitFor(const std::function<bool()> &done, int ms) {
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

// Largest difference of one color channel between two images of one size.
static int maxDiff(const QImage &a, const QImage &b, const QRect &area = QRect()) {
    if (a.size() != b.size()) return 999;
    const QImage x = a.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage y = b.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QRect r = area.isNull() ? x.rect() : area.intersected(x.rect());
    int worst = 0;
    for (int j = r.top(); j <= r.bottom(); ++j) {
        const QRgb *p = reinterpret_cast<const QRgb *>(x.constScanLine(j));
        const QRgb *q = reinterpret_cast<const QRgb *>(y.constScanLine(j));
        for (int i = r.left(); i <= r.right(); ++i) {
            worst = std::max({worst, std::abs(qRed(p[i]) - qRed(q[i])), std::abs(qGreen(p[i]) - qGreen(q[i])),
                              std::abs(qBlue(p[i]) - qBlue(q[i])), std::abs(qAlpha(p[i]) - qAlpha(q[i]))});
        }
    }
    return worst;
}

// Share of pixels (0..1) in `area` that differ by more than `limit`.
static double shareOver(const QImage &a, const QImage &b, const QRect &area, int limit) {
    const QImage x = a.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage y = b.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QRect r = area.intersected(x.rect());
    qint64 over = 0, all = 0;
    for (int j = r.top(); j <= r.bottom(); ++j)
        for (int i = r.left(); i <= r.right(); ++i) {
            const QRgb p = x.pixel(i, j), q = y.pixel(i, j);
            ++all;
            if (std::max({std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)),
                          std::abs(qBlue(p) - qBlue(q))}) > limit) ++over;
        }
    return all ? double(over) / double(all) : 0.0;
}

// Soft, partly transparent shapes, different on every layer.
static QImage shapes(QSize size, int seed, bool opaqueBackground = false) {
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    QRandomGenerator rng(quint32(seed * 7919 + 3));
    if (opaqueBackground) {
        QLinearGradient g(0, 0, size.width(), size.height());
        g.setColorAt(0, QColor(70, 110, 160));
        g.setColorAt(1, QColor(230, 200, 150));
        QPainter p(&img);
        p.fillRect(img.rect(), g);
        return img;
    }
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    for (int s = 0; s < 6; ++s) {
        const QColor c = QColor::fromHsv(rng.bounded(360), 60 + rng.bounded(190), 40 + rng.bounded(215),
                                         40 + rng.bounded(216));
        const int w = size.width() / 6 + rng.bounded(size.width() / 2);
        const int h = size.height() / 6 + rng.bounded(size.height() / 2);
        const QRect r(rng.bounded(size.width() - w), rng.bounded(size.height() - h), w, h);
        if (s % 2) {
            QRadialGradient rg(r.center(), std::max(w, h) / 2.0);
            rg.setColorAt(0, c);
            QColor edge = c;
            edge.setAlpha(0);
            rg.setColorAt(1, edge);
            p.setBrush(rg);
        } else {
            p.setBrush(c);
        }
        p.drawEllipse(r);
    }
    return img;
}

// 8 layers: a background, below the active one various blend modes, above
// it Normal layers with different opacity; one layer moved.
static void fillDocument(Document &doc) {
    doc.activeLayer()->setImage(shapes(doc.size(), 0, true));
    const BlendMode below[] = {BlendMode::Multiply, BlendMode::Screen, BlendMode::Overlay};
    for (int i = 1; i < 8; ++i) {
        doc.addLayer(shapes(doc.size(), i), QStringLiteral("Layer %1").arg(i));
        Layer *layer = doc.activeLayer();
        if (i <= 3) layer->setBlendMode(below[i - 1]);
        if (i >= 5) layer->setOpacity(0.5f + 0.1f * float(i - 5));
        if (i == 6) layer->setOffset(QPoint(23, -11));
    }
    doc.setActiveLayer(4);
}

static void send(CanvasWidget &canvas, QEvent::Type kind, QPointF p, Qt::MouseButton button,
                 Qt::MouseButtons buttons) {
    QMouseEvent e(kind, p, p, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &e);
}

// ---------------------------------------------------------------------------

static void groupCache() {
    std::printf("== Processor: layers below and above the active one kept combined ==\n");
    Document doc(640, 400);
    fillDocument(doc);
    for (int shift : {0, 1, 2}) {
        const QSize size = doc.reducedSize(shift);
        LayerGroupCache cache;
        QImage viaCache(size, QImage::Format_ARGB32_Premultiplied), plain = viaCache.copy();
        cache.composite(doc, viaCache, QPoint(), shift);
        doc.compositeInto(plain, QPoint(), shift);
        CHECK(cache.belowCached() && cache.aboveCached());
        // Above: the same within rounding.
        const int grouped = maxDiff(viaCache, plain);
        CHECK(grouped <= 3);
        // Pieces and the whole agree to the bit.
        QImage pieces(size, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < size.height(); y += 97)
            for (int x = 0; x < size.width(); x += 131) {
                const QRect r = QRect(x, y, 131, 97).intersected(pieces.rect());
                QImage view(pieces.bits() + r.y() * pieces.bytesPerLine() + r.x() * 4, r.width(), r.height(),
                            pieces.bytesPerLine(), QImage::Format_ARGB32_Premultiplied);
                cache.composite(doc, view, r.topLeft(), shift);
            }
        CHECK(pieces == viaCache);
        // Only "below" (a non-Normal layer above): exactly the same.
        doc.layerAt(7)->setBlendMode(BlendMode::Darken);
        cache.composite(doc, viaCache, QPoint(), shift);
        doc.compositeInto(plain, QPoint(), shift);
        CHECK(cache.belowCached() && !cache.aboveCached());
        CHECK(viaCache == plain);
        doc.layerAt(7)->setBlendMode(BlendMode::Normal);
        // Painting on the active layer: the groups stay, the result follows.
        QImage &active = doc.activeLayer()->image();
        { QPainter p(&active); p.fillRect(QRect(100, 80, 200, 120), QColor(200, 20, 40, 180)); }
        cache.composite(doc, viaCache, QPoint(), shift);
        doc.compositeInto(plain, QPoint(), shift);
        CHECK(maxDiff(viaCache, plain) <= 3);
        // A layer below switched off: the group is made again.
        doc.layerAt(2)->setVisible(false);
        cache.composite(doc, viaCache, QPoint(), shift);
        doc.compositeInto(plain, QPoint(), shift);
        CHECK(maxDiff(viaCache, plain) <= 3);
        doc.layerAt(2)->setVisible(true);
        std::printf("  shift %d: grouped above differs by at most %d/255\n", shift, grouped);
    }
    // Painting on the bottom layers: nothing to keep.
    doc.setActiveLayer(0);
    LayerGroupCache none;
    QImage a(doc.size(), QImage::Format_ARGB32_Premultiplied), b = a.copy();
    doc.layerAt(7)->setBlendMode(BlendMode::Multiply);
    none.composite(doc, a, QPoint(), 0);
    doc.compositeInto(b, QPoint(), 0);
    CHECK(!none.belowCached() && !none.aboveCached() && a == b && none.memoryUsed() == 0);

    // The real canvas: brush strokes area by area versus combining it all.
    Document live(900, 600);
    fillDocument(live);
    CanvasWidget actual, forced;
    BrushTool brush;
    brush.setBrushSize(41);
    brush.setOpacity(70);
    for (CanvasWidget *c : {&actual, &forced}) {
        c->resize(500, 360);
        c->setDocument(&live);
        c->setCurrentTool(&brush);
        c->setShowRulers(false);
        c->setZoom(0.35);
        c->setPan({0, 0});
    }
    QImage ra(actual.size(), QImage::Format_ARGB32_Premultiplied), rb = ra.copy();
    actual.render(&ra);
    send(actual, QEvent::MouseButtonPress, {60, 60}, Qt::LeftButton, Qt::LeftButton);
    for (int i = 0; i < 12; ++i)
        send(actual, QEvent::MouseMove, {60.0 + i * 13.7, 60.0 + i * 7.3}, Qt::NoButton, Qt::LeftButton);
    actual.render(&ra);
    forced.updateCanvas();
    forced.render(&rb);
    CHECK(ra == rb);
    send(actual, QEvent::MouseButtonRelease, {225, 148}, Qt::LeftButton, Qt::NoButton);
    live.history().undo();
    actual.render(&ra);
    forced.updateCanvas();
    forced.render(&rb);
    CHECK(ra == rb);
}

// ---------------------------------------------------------------------------

struct GlContext {
    QOpenGLContext context;
    QOffscreenSurface surface;
    bool ok = false;
    GlContext() {
        surface.create();
        ok = context.create() && context.makeCurrent(&surface);
    }
};

static void compositorMatchesProcessor() {
    std::printf("== Graphics card: combining like the processor, all blend modes ==\n");
    GlContext gl;
    CHECK(gl.ok);
    if (!gl.ok) return;
    GpuCompositor gpu;
    CHECK(gpu.initialize());
    if (!gpu.error().isEmpty()) std::printf("  %s\n", qPrintable(gpu.error()));

    // Two layers: the top one in each blend mode, at full and half opacity,
    // at full size and reduced.
    Document doc(301, 203);
    doc.activeLayer()->setImage(shapes(doc.size(), 11));
    { QPainter p(&doc.activeLayer()->image()); p.fillRect(QRect(0, 0, 150, 203), QColor(120, 160, 90, 255)); }
    doc.addLayer(shapes(doc.size(), 12), QStringLiteral("Top"));
    int worstAll = 0;
    for (BlendMode mode : Layer::allBlendModes()) {
        int worst = 0;
        for (float opacity : {1.0f, 0.55f}) {
            doc.activeLayer()->setBlendMode(mode);
            doc.activeLayer()->setOpacity(opacity);
            for (int shift : {0, 1, 2}) {
                CHECK(gpu.update(doc, shift, QRegion(), true));
                QImage cpu(doc.reducedSize(shift), QImage::Format_ARGB32_Premultiplied);
                doc.compositeInto(cpu, QPoint(), shift);
                worst = std::max(worst, maxDiff(gpu.readBack(), cpu));
            }
        }
        std::printf("  %-12s differs by at most %d/255\n", qPrintable(Layer::blendModeName(mode)), worst);
        CHECK(worst <= 3);
        worstAll = std::max(worstAll, worst);
    }
    doc.activeLayer()->setBlendMode(BlendMode::Normal);
    doc.activeLayer()->setOpacity(1.0f);

    // A whole document: offsets, several modes, changed areas sent alone.
    Document big(900, 600);
    fillDocument(big);
    // A layer with little on it is held as only that part.
    big.addLayer(QStringLiteral("Small"));
    { QPainter p(&big.activeLayer()->image()); p.fillRect(QRect(400, 300, 60, 40), QColor(10, 200, 90)); }
    for (int shift : {0, 2}) {
        CHECK(gpu.update(big, shift, QRegion(), true));
        QImage cpu(big.reducedSize(shift), QImage::Format_ARGB32_Premultiplied);
        big.compositeInto(cpu, QPoint(), shift);
        CHECK(maxDiff(gpu.readBack(), cpu) <= 3);
    }
    CHECK(gpu.partialLayerCount() >= 1);
    // Painting inside, then outside the small layer's part: only those areas go.
    for (const QRect &area : {QRect(410, 305, 20, 20), QRect(30, 40, 120, 80)}) {
        { QPainter p(&big.activeLayer()->image()); p.fillRect(area, QColor(250, 240, 10, 200)); }
        CHECK(gpu.update(big, 0, QRegion(area), false));
        QImage cpu(big.size(), QImage::Format_ARGB32_Premultiplied);
        big.compositeInto(cpu, QPoint(), 0);
        CHECK(maxDiff(gpu.readBack(), cpu) <= 3);
    }
    // Layers gone or switched off.
    big.removeLayer(big.activeLayerIndex());
    big.layerAt(3)->setVisible(false);
    CHECK(gpu.update(big, 1, QRegion(), true));
    QImage cpu(big.reducedSize(1), QImage::Format_ARGB32_Premultiplied);
    big.compositeInto(cpu, QPoint(), 1);
    CHECK(maxDiff(gpu.readBack(), cpu) <= 3);
    // Too little graphics memory: it says so, nothing breaks.
    gpu.setMemoryBudget(1024);
    Document huge(1200, 900);
    huge.activeLayer()->setImage(shapes(huge.size(), 3, true));
    CHECK(!gpu.update(huge, 0, QRegion(), true));
    CHECK(gpu.error().contains("memory"));
    gpu.setMemoryBudget(0);
    gpu.release();
    std::printf("  all modes: at most %d/255 apart\n", worstAll);
}

// ---------------------------------------------------------------------------

static void canvasOnGpu() {
    std::printf("== Graphics card: the canvas ==\n");
    GpuSupport::setMode(GpuSupport::Mode::On);
    Document doc(900, 600);
    fillDocument(doc);
    CanvasWidget cpu, gpu;
    for (CanvasWidget *c : {&cpu, &gpu}) {
        c->resize(640, 420);
        c->setDocument(&doc);
        c->setShowRulers(false);
        c->setZoom(0.5);
        c->setPan({20, 10});
    }
    gpu.applyGpuSetting();
    CHECK(gpu.gpuActive());
    CHECK(gpu.gpuStatus().startsWith("Graphics card: "));
    gpu.show();
    CHECK(waitFor([&]() { return gpu.gpuView() && gpu.gpuView()->compositor(); }, 5000));
    settle(10);
    CHECK(!GpuSupport::stoppedLastTime());   // the first frame cleared the mark
    // Events go to the canvas, not to the view on top of it.
    CHECK(gpu.childAt(QPoint(100, 100)) == nullptr);

    // The processor's canvas keeps the layers above the active one combined
    // (up to 2/255 off), the card combines them in order (up to 2/255 off).
    auto compare = [&](const char *what, int tolerance, bool turned = false) {
        gpu.update();
        settle(10);
        const QImage shown = gpu.gpuView()->grabFramebuffer().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QImage expected(cpu.size(), QImage::Format_ARGB32_Premultiplied);
        cpu.render(&expected);
        // The image area, away from its edge.
        const QRect canvasArea = cpu.canvasTransform().mapRect(QRectF(0, 0, doc.width(), doc.height()))
                                     .toAlignedRect().adjusted(4, 4, -4, -4).intersected(expected.rect());
        const QImage sameSize = shown.scaled(expected.size());
        if (turned) {
            // Turned, Qt's raster engine and the card sample a little apart:
            // only edges of shapes differ, and only slightly.
            const double share = shareOver(sameSize, expected, canvasArea, tolerance);
            std::printf("  %-38s %.2f %% of pixels differ by more than %d/255\n", what, share * 100, tolerance);
            CHECK(share < 0.02);
            return;
        }
        const int d = maxDiff(sameSize, expected, canvasArea);
        std::printf("  %-38s differs by at most %d/255\n", what, d);
        CHECK(d <= tolerance);
    };
    compare("zoomed out, reduced combining", 5);
    for (CanvasWidget *c : {&cpu, &gpu}) c->setZoom(3.0);
    compare("zoomed in, blocky pixels", 5);
    for (CanvasWidget *c : {&cpu, &gpu}) { c->setZoom(1.0); c->setPan({-100, -50}); }
    compare("actual size", 5);
    // A brush stroke: only the changed area goes to the card.
    BrushTool brush;
    brush.setBrushSize(31);
    for (CanvasWidget *c : {&cpu, &gpu}) c->setCurrentTool(&brush);
    send(gpu, QEvent::MouseButtonPress, {200, 200}, Qt::LeftButton, Qt::LeftButton);
    for (int i = 0; i < 10; ++i) send(gpu, QEvent::MouseMove, {200.0 + i * 15, 200.0 + i * 6}, Qt::NoButton, Qt::LeftButton);
    send(gpu, QEvent::MouseButtonRelease, {350, 260}, Qt::LeftButton, Qt::NoButton);
    for (CanvasWidget *c : {&cpu, &gpu}) c->setCurrentTool(nullptr);
    cpu.updateCanvas();
    compare("after a brush stroke", 5);
    // Layer switched off, blend mode changed: everything combined again.
    doc.setLayerVisibility(2, false);
    doc.setLayerBlendMode(5, BlendMode::Difference);
    cpu.updateCanvas();
    gpu.updateCanvas();
    compare("after layer changes", 5);
    // The view turned.
    for (CanvasWidget *c : {&cpu, &gpu}) c->setRotation(30);
    compare("turned by 30 degrees", 8, true);
    for (CanvasWidget *c : {&cpu, &gpu}) c->setRotation(0);

    // The card fails (no memory left): the processor takes over by itself.
    QString reason;
    QObject::connect(&gpu, &CanvasWidget::gpuFailed, [&reason](const QString &r) { reason = r; });
    gpu.gpuView()->compositor()->setMemoryBudget(1);
    doc.activeLayer()->setOpacity(0.3f);   // anything that needs the layers again
    doc.layerAt(1)->image().fill(Qt::red);
    gpu.updateCanvas();
    CHECK(waitFor([&]() { return !gpu.gpuActive(); }, 5000));
    CHECK(!reason.isEmpty());
    CHECK(gpu.gpuStatus().startsWith("Processor"));
    // Switched off in Settings: the processor.
    GpuSupport::setMode(GpuSupport::Mode::Off);
    gpu.applyGpuSetting();
    CHECK(!gpu.gpuActive() && gpu.gpuStatus().contains("Settings"));
    GpuSupport::setMode(GpuSupport::Mode::Auto);
}

static void selectionMove() {
    std::printf("== Moving the selection mask (arrow keys) ==\n");
    for (const QPoint &delta : {QPoint(13, -9), QPoint(-40, 25), QPoint(200, 0)}) {
        Selection sel(120, 90);
        sel.selectEllipse(QRect(10, 20, 70, 50));
        // What the painter-based move gave: the new copy must give the same.
        QImage expected(sel.mask().size(), QImage::Format_Grayscale8);
        expected.fill(0);
        { QPainter p(&expected); p.drawImage(delta, sel.mask()); }
        sel.translate(delta);
        CHECK(sel.mask() == expected);
        CHECK(sel.isEmpty() == (delta.x() >= 120));
    }
}

static void crashGuard() {
    std::printf("== A graphics card that stopped the program last time ==\n");
    GpuSupport::setMode(GpuSupport::Mode::Auto);
    CHECK(!GpuSupport::stoppedLastTime());
    GpuSupport::markStarting(true);   // as if the program had crashed while starting the card
    QString why;
    CHECK(!GpuSupport::wanted(&why));
    CHECK(why.contains("last time"));
    // Choosing again in Settings tries again.
    GpuSupport::setMode(GpuSupport::Mode::Auto);
    CHECK(!GpuSupport::stoppedLastTime());
}

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    app.setApplicationName(AppInfo::kId);

    groupCache();
    selectionMove();
    crashGuard();

    GpuSupport::setMode(GpuSupport::Mode::On);
    QString why;
    const bool gl = GpuSupport::wanted(&why);
    if (gl) {
        std::printf("OpenGL: %s\n", qPrintable(GpuSupport::describe()));
        compositorMatchesProcessor();
        canvasOnGpu();
    } else {
        std::printf("Graphics card tests SKIPPED: %s\n", qPrintable(why));
        if (qEnvironmentVariableIntValue("PWM_REQUIRE_GPU")) {
            std::printf("  FAIL: PWM_REQUIRE_GPU is set\n");
            ++g_fail;
        }
    }
    GpuSupport::setMode(GpuSupport::Mode::Auto);

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
