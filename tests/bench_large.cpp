// Speed benchmark with a large document, run by hand (not part of ctest):
//
//   cmake --build build --target pwm_bench
//   QT_QPA_PLATFORM=offscreen build/pwm_bench [--quick]
//
// It opens the real main window, builds a 5000 x 5000 px image with 16 layers
// (600 dpi, which only affects rulers and printing), and times what a person
// does with it: switch a layer on and off in the Layers panel, paint with a
// 1500 px brush, undo. Every figure includes the screen update, because a
// change only counts as done once it is visible.
//
// --quick uses 2000 x 2000 px and 8 layers, for a fast sanity run.
//
// Settings and data go to a temporary folder, never to the real home folder.

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "mainwindow.h"
#include "canvas/canvaswidget.h"
#include "core/document.h"
#include "core/layer.h"
#include "panels/imagelistbar.h"
#include "panels/layerspanel.h"
#include "tools/tool.h"

namespace {

int gSize = 5000;
int gLayers = 16;
constexpr int kDpi = 600;

double ms(const QElapsedTimer &t) { return t.nsecsElapsed() / 1.0e6; }

long statusKb(const char *key) {
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly)) return -1;
    for (const QByteArray &line : f.readAll().split('\n'))
        if (line.startsWith(key)) return line.mid(int(qstrlen(key))).trimmed().split(' ').first().toLong();
    return -1;
}
double rssMb() { return statusKb("VmRSS:") / 1024.0; }
double peakMb() { return statusKb("VmHWM:") / 1024.0; }

double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

void row(const char *what, double value, const char *unit = "ms") {
    std::printf("  %-58s %9.1f %s\n", what, value, unit);
    std::fflush(stdout);
}

// Counts finished paints of one widget.
class PaintCounter : public QObject {
public:
    int paints = 0;
protected:
    bool eventFilter(QObject *, QEvent *e) override {
        if (e->type() == QEvent::Paint) ++paints;
        return false;
    }
};

// Runs the event loop until the canvas has painted at least once more than
// `before` (or the timeout passes). Returns false on timeout.
bool waitForPaint(PaintCounter &counter, int before, int timeoutMs = 30000) {
    QElapsedTimer t;
    t.start();
    while (counter.paints <= before) {
        QApplication::processEvents(QEventLoop::AllEvents);
        if (t.elapsed() > timeoutMs) return false;
    }
    return true;
}

struct Busy { double total = 0, longest = 0; };

// Keeps the event loop running for `windowMs` and adds up the time the program
// was busy (delayed work such as thumbnail updates). While it is busy, the
// window does not react.
Busy busyFor(int windowMs) {
    Busy b;
    QElapsedTimer window;
    window.start();
    while (window.elapsed() < windowMs) {
        QElapsedTimer t;
        t.start();
        QApplication::processEvents(QEventLoop::AllEvents);
        const double d = ms(t);
        if (d > 1.0) { b.total += d; b.longest = std::max(b.longest, d); }
        QThread::msleep(1);
    }
    return b;
}

QImage makeLayerContent(int index, QRandomGenerator &rng) {
    QImage img(gSize, gSize, QImage::Format_ARGB32_Premultiplied);
    const int dpm = qRound(kDpi / 0.0254);
    img.setDotsPerMeterX(dpm);
    img.setDotsPerMeterY(dpm);
    QPainter p;
    if (index == 0) {
        // Background: opaque gradient, like a photo or a painted backdrop.
        QLinearGradient g(0, 0, gSize, gSize);
        g.setColorAt(0, QColor(70, 110, 160));
        g.setColorAt(1, QColor(230, 200, 150));
        p.begin(&img);
        p.fillRect(img.rect(), g);
    } else {
        img.fill(Qt::transparent);
        p.begin(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        // A mix of soft shapes and strokes covering part of the layer.
        const int shapes = 6 + index % 5;
        for (int s = 0; s < shapes; ++s) {
            const QColor c = QColor::fromHsv(rng.bounded(360), 80 + rng.bounded(170),
                                             90 + rng.bounded(160), 90 + rng.bounded(166));
            const int w = gSize / 10 + rng.bounded(gSize / 3);
            const int h = gSize / 10 + rng.bounded(gSize / 3);
            const int x = rng.bounded(gSize - w), y = rng.bounded(gSize - h);
            if (s % 3 == 0) {
                QRadialGradient rg(x + w / 2.0, y + h / 2.0, std::max(w, h) / 2.0);
                rg.setColorAt(0, c);
                QColor edge = c;
                edge.setAlpha(0);
                rg.setColorAt(1, edge);
                p.setBrush(rg);
                p.drawEllipse(QRect(x, y, w, h));
            } else if (s % 3 == 1) {
                p.setBrush(c);
                p.drawRoundedRect(QRect(x, y, w, h), w / 8.0, h / 8.0);
            } else {
                QPainterPath path;
                path.moveTo(x, y + h);
                path.cubicTo(x + w / 3.0, y, x + 2 * w / 3.0, y + h, x + w, y);
                p.setPen(QPen(c, gSize / 60.0, Qt::SolidLine, Qt::RoundCap));
                p.setBrush(Qt::NoBrush);
                p.drawPath(path);
                p.setPen(Qt::NoPen);
            }
        }
    }
    p.end();
    return img;
}

QMouseEvent mouse(QEvent::Type type, const QPointF &pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
    return QMouseEvent(type, pos, pos, button, buttons, Qt::NoModifier);
}

} // namespace

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], "--quick") == 0) { gSize = 2000; gLayers = 8; }

    // Keep settings, autosave and recovery data away from the real home folder.
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
    qputenv("XDG_CACHE_HOME", (home.path() + "/cache").toUtf8());

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("paint-with-me-bench"));

    std::printf("Paint.with.me benchmark: %d x %d px, %d layers, %d dpi, %d CPU threads\n",
                gSize, gSize, gLayers, kDpi, QThread::idealThreadCount());

    MainWindow window;
    window.resize(1600, 1000);
    window.show();
    busyFor(800);   // startup work (panel placement, first paint)

    // The autosave timer would fire in the middle of the measurements.
    QTimer *autosave = nullptr;
    for (QTimer *t : window.findChildren<QTimer *>())
        if (t->interval() == 60 * 1000) { autosave = t; t->stop(); }

    auto *canvas = window.findChild<CanvasWidget *>();
    auto *layersPanel = window.findChild<LayersPanel *>();
    auto *layerList = layersPanel ? layersPanel->findChild<QListWidget *>() : nullptr;
    auto *imageList = window.findChild<ImageListBar *>();
    if (!canvas || !canvas->document() || !layerList) {
        std::fprintf(stderr, "main window parts not found\n");
        return 1;
    }
    PaintCounter paints;
    canvas->installEventFilter(&paints);
    Document *doc = canvas->document();

    // ---- Build the document ----
    QElapsedTimer setup;
    setup.start();
    QRandomGenerator rng(42);
    doc->resizeCanvas(gSize, gSize);
    doc->layerAt(0)->setImage(makeLayerContent(0, rng));
    for (int i = 1; i < gLayers; ++i)
        doc->addLayer(makeLayerContent(i, rng), QStringLiteral("Layer %1").arg(i + 1));
    // A few layers use other blend modes and opacities, as real projects do.
    doc->setLayerBlendMode(4, BlendMode::Multiply);
    doc->setLayerBlendMode(9, BlendMode::Screen);
    doc->setLayerOpacity(6, 0.7f);
    if (gLayers > 12) doc->setLayerBlendMode(12, BlendMode::Overlay);
    doc->history().clear();
    doc->setModified(false);
    canvas->setDpi(kDpi);
    canvas->resetToDefaultView();
    doc->notifyContentChanged();
    busyFor(1500);
    std::printf("  (document built in %.1f s; zoom %.0f %%)\n\n", ms(setup) / 1000.0, canvas->zoom() * 100);

    std::printf("Memory\n");
    row("after loading", rssMb(), "MB");

    // ---- Building blocks, measured one by one ----
    std::printf("\nBuilding blocks\n");
    {
        std::vector<double> full, region, thumbs, list;
        for (int r = 0; r < 3; ++r) {
            QElapsedTimer t;
            t.start();
            QImage img = doc->flattenVisible();
            full.push_back(ms(t));
            t.restart();
            QImage part = doc->flattenVisible(QRect(gSize / 5, gSize / 5, gSize * 8 / 25, gSize * 8 / 25));
            region.push_back(ms(t));
            t.restart();
            layersPanel->refreshThumbnails();
            thumbs.push_back(ms(t));
            if (imageList) {
                t.restart();
                imageList->setDocuments({doc}, 0);
                list.push_back(ms(t));
            }
        }
        row("combine all layers (whole image)", median(full));
        row("combine all layers (an area the size of a big brush dab)", median(region));
        row("Layers panel: refresh all thumbnails", median(thumbs));
        if (imageList) row("image list: refresh the image thumbnail", median(list));
    }
    busyFor(500);

    // ---- Layer on/off in the Layers panel ----
    std::printf("\nSwitch one layer off and on again (Layers panel check box)\n");
    {
        std::vector<double> visible, busy, longest;
        const int layer = gLayers / 2;
        for (int r = 0; r < 4; ++r) {
            QListWidgetItem *item = layerList->item(layerList->count() - 1 - layer);
            const bool on = item->checkState() == Qt::Checked;
            const int before = paints.paints;
            QElapsedTimer t;
            t.start();
            item->setCheckState(on ? Qt::Unchecked : Qt::Checked);
            if (!waitForPaint(paints, before)) std::printf("  (no repaint seen)\n");
            const double shown = ms(t);
            const Busy after = busyFor(1000);
            visible.push_back(shown);
            busy.push_back(shown + after.total);
            longest.push_back(std::max(shown, after.longest));
            if (doc->layerAt(layer)->isVisible() == on) std::printf("  (layer did not change)\n");
        }
        row("until the canvas shows the change", median(visible));
        row("total time the window is blocked", median(busy));
    }

    // ---- Painting with a 1500 px brush ----
    std::printf("\nPaint with a 1500 px brush (Paintbrush tool, 40 mouse moves)\n");
    Tool *brush = canvas->currentTool();
    if (!brush || brush->type() != ToolType::Brush) {
        std::fprintf(stderr, "brush tool is not active\n");
        return 1;
    }
    brush->setBrushSize(1500);
    doc->setActiveLayer(gLayers - 3);
    busyFor(300);
    const double rssBeforeStrokes = rssMb();
    auto stroke = [&](double yFrac, bool report) {
        const QPointF from(gSize * 0.15, gSize * yFrac), to(gSize * 0.85, gSize * yFrac);
        auto widgetAt = [&](double f) { return canvas->canvasToWidget(from + (to - from) * f); };
        std::vector<double> moves;
        QElapsedTimer t;
        int before = paints.paints;
        t.start();
        {
            QMouseEvent press = mouse(QEvent::MouseButtonPress, widgetAt(0), Qt::LeftButton, Qt::LeftButton);
            QApplication::sendEvent(canvas, &press);
            waitForPaint(paints, before);
        }
        const double pressMs = ms(t);
        const int steps = 40;
        for (int i = 1; i <= steps; ++i) {
            before = paints.paints;
            t.restart();
            QMouseEvent move = mouse(QEvent::MouseMove, widgetAt(double(i) / steps), Qt::NoButton, Qt::LeftButton);
            QApplication::sendEvent(canvas, &move);
            waitForPaint(paints, before);
            moves.push_back(ms(t));
        }
        before = paints.paints;
        t.restart();
        {
            QMouseEvent release = mouse(QEvent::MouseButtonRelease, widgetAt(1), Qt::LeftButton, Qt::NoButton);
            QApplication::sendEvent(canvas, &release);
            waitForPaint(paints, before);
        }
        const double releaseShown = ms(t);
        const Busy after = busyFor(1500);
        if (report) {
            row("mouse press (stroke starts)", pressMs);
            row("each mouse move, median", median(moves));
            row("each mouse move, slowest", *std::max_element(moves.begin(), moves.end()));
            row("  = frames per second while painting (median)", 1000.0 / median(moves), "fps");
            row("mouse release until the canvas is final", releaseShown);
            row("mouse release, total time the window is blocked", releaseShown + after.total);
        }
    };
    stroke(0.30, true);

    // ---- Undo ----
    std::printf("\nUndo the stroke (Edit > Undo)\n");
    {
        QAction *undo = nullptr;
        for (QAction *a : window.findChildren<QAction *>())
            if (a->shortcut() == QKeySequence(QKeySequence::Undo)) { undo = a; break; }
        if (undo) {
            const int before = paints.paints;
            QElapsedTimer t;
            t.start();
            undo->trigger();
            waitForPaint(paints, before);
            const double shown = ms(t);
            const Busy after = busyFor(1500);
            row("until the canvas shows the change", shown);
            row("total time the window is blocked", shown + after.total);
            // Redo, so the history holds the stroke again for the memory figures.
            for (QAction *a : window.findChildren<QAction *>())
                if (a->shortcut() == QKeySequence(QKeySequence::Redo)) { a->trigger(); break; }
            busyFor(1500);
        }
    }

    // ---- Memory use of the undo history ----
    std::printf("\nMemory\n");
    for (int i = 0; i < 4; ++i) stroke(0.40 + 0.1 * i, false);
    const double rssAfter = rssMb();
    row("after 5 brush strokes", rssAfter, "MB");
    row("  growth per brush stroke (mostly undo history)", (rssAfter - rssBeforeStrokes) / 5.0, "MB");
    row("peak", peakMb(), "MB");

    // ---- Autosave ----
    if (autosave) {
        std::printf("\nAutosave (runs every 60 s while the image has unsaved changes)\n");
        doc->setModified(true);
        bool fired = false;
        // Connected after the main window's own handler, so it runs once.
        QObject::connect(autosave, &QTimer::timeout, autosave, [&]() { fired = true; autosave->stop(); });
        autosave->setInterval(1);
        QElapsedTimer t;
        t.start();
        autosave->start();
        Busy busy;
        while (!fired && t.elapsed() < 60000) {
            QElapsedTimer one;
            one.start();
            QApplication::processEvents(QEventLoop::AllEvents);
            busy.total += ms(one);
        }
        const Busy after = busyFor(3000);
        const double first = busy.total;
        row("window blocked by one autosave", first + after.total);
        // The layered .psd is written in the background; wait for it.
        const QString recovery = QDir(home.path()).filePath(QStringLiteral("data/paint-with-me-bench/recovery/autosave.psd"));
        while (!QFile::exists(recovery) && t.elapsed() < 120000) busyFor(100);
        row("  autosave written (in the background, layers kept)", ms(t));
    }

    // ---- Photoshop file ----
    {
        std::printf("\nPhotoshop file (.psd, all layers)\n");
        const QString path = home.path() + QStringLiteral("/bench.psd");
        QElapsedTimer t;
        t.start();
        const bool saved = doc->save(path);
        row("save", ms(t));
        row("  file size", QFileInfo(path).size() / 1.0e6, "MB");
        Document opened;
        t.restart();
        const bool loaded = opened.load(path);
        row("open", ms(t));
        if (!saved || !loaded || opened.layerCount() != doc->layerCount())
            std::printf("  ERROR: the .psd did not round-trip\n");
        row("peak memory", peakMb(), "MB");
    }

    doc->setModified(false);
    window.close();
    return 0;
}
