// Perspective guides: geometry, placing points with the tool, strokes that
// follow the guides, guides drawn into a layer, and the main window wiring.
// Headless. PWM_SCREENSHOT_DIR=<folder> saves a picture of the guides.
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolButton>
#include <cmath>
#include <cstdio>

#include "canvas/canvaswidget.h"
#include "core/document.h"
#include "core/perspective.h"
#include "mainwindow.h"
#include "panels/tooloptionspanel.h"
#include "panels/toolspanel.h"
#include "tools/brushtool.h"
#include "tools/perspectivetool.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; \
    std::printf("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static bool near(const QPointF &a, const QPointF &b, double eps = 1e-6) {
    return std::fabs(a.x() - b.x()) < eps && std::fabs(a.y() - b.y()) < eps;
}

static void settle() { for (int i = 0; i < 5; ++i) QApplication::processEvents(); }

// Sends a mouse event to the canvas at a document position.
static void mouse(CanvasWidget &canvas, QEvent::Type type, const QPointF &docPos,
                  Qt::MouseButton button, Qt::MouseButtons buttons) {
    const QPointF w = canvas.canvasToWidget(docPos);
    QMouseEvent e(type, w, canvas.mapToGlobal(w), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &e);
}

static void drag(CanvasWidget &canvas, const QPointF &from, const QPointF &to, int steps = 10,
                 Qt::MouseButton button = Qt::LeftButton) {
    mouse(canvas, QEvent::MouseButtonPress, from, button, button);
    for (int i = 1; i <= steps; ++i)
        mouse(canvas, QEvent::MouseMove, from + (to - from) * (double(i) / steps), Qt::NoButton, button);
    mouse(canvas, QEvent::MouseButtonRelease, to, button, Qt::NoButton);
}

static void geometry() {
    std::printf("== Geometry ==\n");
    const QVector<QPointF> one = {QPointF(100, 0)};
    // Heading roughly toward the point: the line toward it.
    QPointF d = Perspective::snapDirection(one, false, QPointF(0, 0), QPointF(50, 3));
    CHECK(near(d, QPointF(1, 0)));
    CHECK(near(Perspective::project(QPointF(0, 0), d, QPointF(50, 3)), QPointF(50, 0)));
    // Away from the point works too.
    d = Perspective::snapDirection(one, false, QPointF(0, 0), QPointF(-40, -2));
    CHECK(near(Perspective::project(QPointF(0, 0), d, QPointF(-40, -2)), QPointF(-40, 0)));
    // Straight lines count when asked.
    d = Perspective::snapDirection(one, true, QPointF(0, 0), QPointF(2, 40));
    CHECK(near(d, QPointF(0, 1)));
    d = Perspective::snapDirection(one, false, QPointF(0, 0), QPointF(2, 40));
    CHECK(near(d, QPointF(1, 0)));   // only the point's line is left
    // Two points: the better matching one.
    const QVector<QPointF> two = {QPointF(-100, 0), QPointF(100, 100)};
    d = Perspective::snapDirection(two, false, QPointF(0, 0), QPointF(30, 28));
    CHECK(near(d, QPointF(1, 1) / std::sqrt(2.0)));
    CHECK(Perspective::snapDirection({}, false, QPointF(0, 0), QPointF(3, 4)).isNull());
    CHECK(Perspective::pointNear(two, QPointF(98, 103), 5) == 1);
    CHECK(Perspective::pointNear(two, QPointF(50, 50), 5) == -1);

    const QImage layer = Perspective::renderLayer(one, 24, QSize(200, 100));
    int painted = 0;
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 200; ++x)
            if (qAlpha(layer.pixel(x, y)) > 0) ++painted;
    CHECK(painted > 200 && painted < 200 * 100 / 2);
    // With 24 lines one runs straight down from (100, 0), in the first point's blue.
    const QColor down = QColor::fromRgba(qUnpremultiply(layer.pixel(100, 60)));
    // (A 1 px line on a pixel edge covers two columns half each.)
    CHECK(down.alpha() >= 80 && down.blue() > 150 && down.red() < 80);
}

static void tool() {
    std::printf("== Placing vanishing points ==\n");
    Document doc(200, 100);
    CanvasWidget canvas;
    canvas.resize(500, 300);
    canvas.setDocument(&doc);
    canvas.show();
    settle();
    PerspectiveTool tool;
    canvas.setCurrentTool(&tool);
    drag(canvas, QPointF(150, 50), QPointF(150, 50), 1);
    CHECK(doc.vanishingPoints().size() == 1);
    // Drag it somewhere else, even outside the image.
    drag(canvas, QPointF(150, 50), QPointF(230, 40));
    CHECK(doc.vanishingPoints().size() == 1 && near(doc.vanishingPoints()[0], QPointF(230, 40), 0.6));
    drag(canvas, QPointF(10, 10), QPointF(10, 10), 1);
    drag(canvas, QPointF(100, 90), QPointF(100, 90), 1);
    drag(canvas, QPointF(60, 60), QPointF(60, 60), 1);   // a fourth: not added
    CHECK(doc.vanishingPoints().size() == Perspective::kMaxPoints);
    // Right-click removes.
    drag(canvas, QPointF(10, 10), QPointF(10, 10), 1, Qt::RightButton);
    CHECK(doc.vanishingPoints().size() == 2);
    // The guides and handles draw without trouble.
    Perspective::settings().visible = true;
    const QImage shot = canvas.grab().toImage();
    CHECK(!shot.isNull());
    const QByteArray dir = qgetenv("PWM_SCREENSHOT_DIR");
    if (!dir.isEmpty()) {
        QDir().mkpath(QString::fromLocal8Bit(dir));
        shot.save(QString::fromLocal8Bit(dir) + "/perspective-guides.png");
    }
}

static int darkInColumn(const QImage &img, int x, int y0, int y1) {
    int count = 0;
    for (int y = y0; y <= y1; ++y)
        if (QColor(img.pixel(x, y)).lightness() < 128) ++count;
    return count;
}

static void snapping() {
    std::printf("== Strokes follow the guides ==\n");
    Perspective::Settings &ps = Perspective::settings();
    ps.snap = true;
    ps.straight = false;
    Document doc(200, 100);
    doc.activeLayer()->clear(Qt::white);
    doc.setPrimaryColor(Qt::black);
    doc.vanishingPoints() = {QPointF(190, 50)};
    CanvasWidget canvas;
    canvas.resize(500, 300);
    canvas.setDocument(&doc);
    canvas.show();
    settle();
    BrushTool brush;
    brush.setBrushSize(3);
    canvas.setCurrentTool(&brush);
    drag(canvas, QPointF(20, 52), QPointF(120, 60), 20);
    QImage img = doc.activeLayer()->image();
    // Along the line toward (190, 50), not toward where the mouse went.
    CHECK(darkInColumn(img, 120, 48, 53) > 0);
    CHECK(darkInColumn(img, 120, 56, 64) == 0);

    // Snap off: the stroke goes where the mouse goes.
    ps.snap = false;
    drag(canvas, QPointF(20, 20), QPointF(120, 35), 20);
    img = doc.activeLayer()->image();
    CHECK(darkInColumn(img, 120, 32, 38) > 0);

    // Horizontal and vertical count too when asked.
    ps.snap = true;
    ps.straight = true;
    drag(canvas, QPointF(60, 10), QPointF(64, 90), 20);
    img = doc.activeLayer()->image();
    CHECK(darkInColumn(img, 60, 80, 90) > 0);
    CHECK(QColor(img.pixel(64, 88)).lightness() > 200);

    // No points: nothing snaps.
    doc.vanishingPoints().clear();
    drag(canvas, QPointF(150, 10), QPointF(170, 30), 20);
    img = doc.activeLayer()->image();
    CHECK(darkInColumn(img, 170, 27, 33) > 0);
}

static void mainWindow() {
    std::printf("== Main window ==\n");
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    settle();
    auto *canvas = window.findChild<CanvasWidget *>();
    auto *panel = window.findChild<ToolsPanel *>();
    CHECK(canvas && panel && panel->toolButton(ToolType::Perspective));
    if (!canvas || !panel || !panel->toolButton(ToolType::Perspective)) return;
    panel->toolButton(ToolType::Perspective)->click();
    settle();
    CHECK(canvas->currentTool() && canvas->currentTool()->type() == ToolType::Perspective);
    auto *rays = window.findChild<QSpinBox *>("perspectiveRays");
    CHECK(rays && rays->isVisible());
    Document *doc = canvas->document();
    const int layers = doc->layerCount();
    // Without points: a hint, no layer.
    QMetaObject::invokeMethod(&window, "perspectiveGuidesToLayer");
    CHECK(doc->layerCount() == layers);
    doc->vanishingPoints() = {QPointF(700, 100), QPointF(-200, 120)};
    canvas->update();
    settle();
    const QByteArray dir = qgetenv("PWM_SCREENSHOT_DIR");
    if (!dir.isEmpty()) {
        QDir().mkpath(QString::fromLocal8Bit(dir));
        window.grab().save(QString::fromLocal8Bit(dir) + "/perspective-window.png");
    }
    QMetaObject::invokeMethod(&window, "perspectiveGuidesToLayer");
    CHECK(doc->layerCount() == layers + 1 && doc->activeLayer()->name() == "Perspective guides");
    doc->history().undo();
    CHECK(doc->layerCount() == layers);
    // The View menu and the tool options stay in step.
    QAction *guides = nullptr;
    for (QAction *a : window.findChildren<QAction *>())
        if (a->text() == QStringLiteral("Perspective &Guides")) guides = a;
    CHECK(guides != nullptr);
    if (guides) {
        const bool before = Perspective::settings().visible;
        guides->trigger();
        CHECK(Perspective::settings().visible == !before);
        QCheckBox *show = nullptr;
        for (QCheckBox *c : window.findChildren<QCheckBox *>())
            if (c->text() == QStringLiteral("Show guides")) show = c;
        CHECK(show && show->isChecked() == Perspective::settings().visible);
        guides->trigger();
    }
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
    geometry();
    tool();
    snapping();
    mainWindow();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
