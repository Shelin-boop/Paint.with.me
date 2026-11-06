// View rotation (View > Rotate View): only the view turns, like turning the
// paper on a desk. Checks the mapping between screen and image, that the
// picture is drawn where the mapping says, that tools paint and select in
// image coordinates, the pen's tilt, fitting and zooming, the commands with
// their step and the status bar indicator. Headless.
//
// PWM_SCREENSHOT_DIR=<folder> saves the main window with a turned view.
#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QSettings>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QToolButton>
#include <cmath>
#include <cstdio>

#include "appinfo.h"
#include "canvas/canvaswidget.h"
#include "core/document.h"
#include "core/layer.h"
#include "dialogs/settingsdialog.h"
#include "mainwindow.h"
#include "shortcuts.h"
#include "tools/brushtool.h"
#include "tools/selectiontool.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; \
    std::printf("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static bool near(const QPointF &a, const QPointF &b, double eps = 1e-6) {
    return std::abs(a.x() - b.x()) <= eps && std::abs(a.y() - b.y()) <= eps;
}

static void sendMouse(CanvasWidget &canvas, QEvent::Type type, const QPointF &p, Qt::MouseButton button,
                      Qt::MouseButtons buttons) {
    QMouseEvent e(type, p, canvas.mapToGlobal(p), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &e);
}

static void drag(CanvasWidget &canvas, const QPointF &from, const QPointF &to, int steps = 20) {
    sendMouse(canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= steps; ++i)
        sendMouse(canvas, QEvent::MouseMove, from + (to - from) * (double(i) / steps), Qt::NoButton, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
}

static void mapping() {
    Document doc(400, 300);
    CanvasWidget canvas;
    canvas.resize(800, 600);
    canvas.setDocument(&doc);
    canvas.setZoom(1.5);

    int signals_ = 0;
    QObject::connect(&canvas, &CanvasWidget::rotationChanged, [&signals_]() { ++signals_; });
    const QPointF points[] = {{0, 0}, {123.5, 45.25}, {400, 300}, {-20, 310}};
    for (double angle : {0.0, 15.0, 33.3, 90.0, -135.0, 180.0}) {
        canvas.setRotation(angle);
        const QTransform t = canvas.canvasTransform();
        for (const QPointF &p : points) {
            CHECK(near(canvas.widgetToCanvas(canvas.canvasToWidget(p)), p, 1e-9));
            CHECK(near(t.map(p), canvas.canvasToWidget(p), 1e-9));
        }
    }
    CHECK(signals_ == 5);   // 0 was already set

    // Clockwise: at 90 degrees the image's x axis points down the screen.
    canvas.setRotation(90);
    const QPointF a = canvas.canvasToWidget({100, 100}), b = canvas.canvasToWidget({110, 100});
    CHECK(near(b - a, QPointF(0, 15), 1e-9));
    // The middle of the view stays where it is.
    const QPointF middle(20 + (800 - 20) / 2.0, 20 + (600 - 20) / 2.0);
    const QPointF under = canvas.widgetToCanvas(middle);
    canvas.setRotation(-40);
    CHECK(near(canvas.canvasToWidget(under), middle, 1e-9));

    // Angles are kept in (-180, 180].
    canvas.setRotation(190);
    CHECK(std::abs(canvas.rotation() + 170) < 1e-9);
    canvas.setRotation(-180);
    CHECK(std::abs(canvas.rotation() - 180) < 1e-9);
    canvas.setRotation(720);
    CHECK(canvas.rotation() == 0.0);
    canvas.rotateBy(-15);
    canvas.rotateBy(-15);
    CHECK(std::abs(canvas.rotation() + 30) < 1e-9);
    canvas.setRotation(std::nan(""));
    CHECK(std::abs(canvas.rotation() + 30) < 1e-9);

    // A drag moves the image with the pointer, whatever the angle.
    canvas.setRotation(90);
    const QPointF q(300, 200);
    const QPointF grabbed = canvas.widgetToCanvas(q);
    canvas.panBy(QPointF(40, -25));
    CHECK(near(canvas.canvasToWidget(grabbed), q + QPointF(40, -25), 1e-9));
    // The wheel and the Zoom tool keep the point under the pointer.
    const QPointF anchor = canvas.widgetToCanvas(QPointF(250, 120));
    canvas.zoomAt(QPointF(250, 120), 3.0);
    CHECK(std::abs(canvas.zoom() - 3.0) < 1e-12);
    CHECK(near(canvas.canvasToWidget(anchor), QPointF(250, 120), 1e-9));
    // The menu and the slider keep the middle of the view.
    const QPointF centered = canvas.widgetToCanvas(middle);
    canvas.setZoomCentered(0.5);
    CHECK(near(canvas.canvasToWidget(centered), middle, 1e-9));
    canvas.setRotation(0);
    const QPointF centered0 = canvas.widgetToCanvas(middle);
    canvas.zoomIn();
    CHECK(near(canvas.canvasToWidget(centered0), middle, 1e-9));
}

static void fitting() {
    // A wide image in a tall view: turned by 90 degrees it fits bigger.
    Document doc(600, 300);
    CanvasWidget canvas;
    canvas.resize(500, 700);
    canvas.setDocument(&doc);
    canvas.zoomToFit();
    const double flat = canvas.zoom();
    canvas.setRotation(90);
    canvas.zoomToFit();
    const double turned = canvas.zoom();
    CHECK(turned > flat * 1.3);
    for (const QPointF corner : {QPointF(0, 0), QPointF(600, 0), QPointF(0, 300), QPointF(600, 300)}) {
        const QPointF w = canvas.canvasToWidget(corner);
        CHECK(w.x() >= 19.0 && w.x() <= 501.0 && w.y() >= 19.0 && w.y() <= 701.0);
    }
    // At 45 degrees the turned box (both sides) has to fit.
    canvas.setRotation(45);
    canvas.zoomToFit();
    for (const QPointF corner : {QPointF(0, 0), QPointF(600, 0), QPointF(0, 300), QPointF(600, 300)}) {
        const QPointF w = canvas.canvasToWidget(corner);
        CHECK(w.x() >= 19.0 && w.x() <= 501.0 && w.y() >= 19.0 && w.y() <= 701.0);
    }
    // Zoom to a selection frames the area in the middle.
    canvas.setRotation(30);
    canvas.zoomToRect(QRect(100, 50, 80, 40));
    const QPointF middle(20 + (500 - 20) / 2.0, 20 + (700 - 20) / 2.0);
    CHECK(near(canvas.canvasToWidget(QPointF(140, 70)), middle, 1e-6));
}

static QImage grabCanvas(CanvasWidget &canvas) {
    return canvas.grab().toImage().convertToFormat(QImage::Format_ARGB32);
}

static void drawing() {
    // A white image with a red square in its top left corner.
    Document doc(200, 120);
    QImage img(doc.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 30; ++x) img.setPixel(x, y, qRgb(255, 0, 0));
    doc.activeLayer()->setImage(img);
    CanvasWidget canvas;
    canvas.resize(600, 500);
    canvas.setDocument(&doc);
    canvas.setZoom(2.0);
    canvas.show();
    QApplication::processEvents();

    auto isRed = [](QRgb c) { return qRed(c) > 200 && qGreen(c) < 60 && qBlue(c) < 60; };
    auto isWhite = [](QRgb c) { return qRed(c) > 230 && qGreen(c) > 230 && qBlue(c) > 230; };
    for (double angle : {0.0, 90.0, 33.0, -120.0}) {
        canvas.setRotation(angle);
        const QImage shot = grabCanvas(canvas);
        const QPoint red = canvas.canvasToWidget(QPointF(15, 15)).toPoint();
        const QPoint white = canvas.canvasToWidget(QPointF(150, 80)).toPoint();
        CHECK(isRed(shot.pixel(red)));
        CHECK(isWhite(shot.pixel(white)));
    }

    // A stroke dragged left to right across the screen at 90 degrees runs
    // down the image, where the mapping says, and the view shows it.
    canvas.setRotation(90);
    BrushTool brush;
    brush.setBrushSize(6);
    canvas.setCurrentTool(&brush);
    doc.setPrimaryColor(Qt::black);
    const QPointF from = canvas.canvasToWidget(QPointF(120, 20));
    const QPointF to = canvas.canvasToWidget(QPointF(120, 100));
    CHECK(std::abs(from.y() - to.y()) < 1e-6 && to.x() < from.x());   // turned: image down = screen left
    drag(canvas, from, to);
    const QImage &painted = doc.activeLayer()->image();
    int minX = 1000, maxX = -1, minY = 1000, maxY = -1;
    for (int y = 0; y < painted.height(); ++y)
        for (int x = 30; x < painted.width(); ++x)
            if (qRed(painted.pixel(x, y)) < 128) {
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
            }
    CHECK(maxX >= 0);
    CHECK(minX >= 114 && maxX <= 126);   // a vertical line at x = 120
    CHECK(minY <= 22 && maxY >= 98);
    QApplication::processEvents();
    const QImage shot = grabCanvas(canvas);
    const QRgb onStroke = shot.pixel(canvas.canvasToWidget(QPointF(120, 60)).toPoint());
    CHECK(qRed(onStroke) < 100 && qGreen(onStroke) < 100);

    // A rectangle selection dragged on screen is a rectangle in the image
    // between the two image points.
    SelectionTool select;
    canvas.setCurrentTool(&select);
    canvas.setRotation(90);
    const QPointF s0 = canvas.canvasToWidget(QPointF(40, 30)), s1 = canvas.canvasToWidget(QPointF(90, 70));
    drag(canvas, s0, s1);
    CHECK(doc.selection().hasSelection());
    const QRect sel = doc.selection().boundingRect();
    CHECK(std::abs(sel.left() - 40) <= 1 && std::abs(sel.top() - 30) <= 1);
    CHECK(std::abs(sel.right() + 1 - 90) <= 1 && std::abs(sel.bottom() + 1 - 70) <= 1);
    // The overlay (marching ants, handles) draws turned without trouble.
    canvas.setRotation(25);
    const QImage withSelection = grabCanvas(canvas);
    CHECK(!withSelection.isNull());

    // The pen's tilt is measured on screen and turned into the image.
    canvas.setCurrentTool(&brush);
    canvas.setRotation(90);
    QTabletEvent move(QEvent::TabletMove, QPointingDevice::primaryPointingDevice(), QPointF(50, 50),
                      canvas.mapToGlobal(QPointF(50, 50)), 0.5, 20.0f, -10.0f, 0.0f, 0.0, 0.0f, Qt::NoModifier,
                      Qt::NoButton, Qt::NoButton);
    QApplication::sendEvent(&canvas, &move);
    CHECK(near(brush.tilt(), QPointF(-10, -20), 1e-4));
    canvas.setCurrentTool(nullptr);
}

static void heldKeys() {
    // Shift+Space held and a drag around the middle turns the view.
    Document doc(300, 200);
    CanvasWidget canvas;
    canvas.resize(600, 500);
    canvas.setDocument(&doc);
    BrushTool brush;
    canvas.setCurrentTool(&brush);
    const QPointF middle(20 + (600 - 20) / 2.0, 20 + (500 - 20) / 2.0);
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Space, Qt::ShiftModifier);
    QApplication::sendEvent(&canvas, &down);
    const QImage before = doc.activeLayer()->image();
    // A quarter turn clockwise: from right of the middle to below it.
    const QPointF start = middle + QPointF(100, 0), end = middle + QPointF(0, 100);
    sendMouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 10; ++i) {
        const double a = M_PI / 2 * i / 10;
        sendMouse(canvas, QEvent::MouseMove, middle + QPointF(100 * std::cos(a), 100 * std::sin(a)), Qt::NoButton,
                  Qt::LeftButton);
    }
    sendMouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    QKeyEvent up(QEvent::KeyRelease, Qt::Key_Space, Qt::ShiftModifier);
    QApplication::sendEvent(&canvas, &up);
    CHECK(std::abs(canvas.rotation() - 90) < 0.5);
    CHECK(doc.activeLayer()->image() == before);   // nothing painted

    // Other keys from Settings > Keyboard.
    canvas.setRotation(0);
    canvas.setHoldKeys(QKeySequence(Qt::Key_H), QKeySequence(Qt::Key_R), QKeySequence(Qt::Key_I));
    QKeyEvent r(QEvent::KeyPress, Qt::Key_R, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &r);
    sendMouse(canvas, QEvent::MouseButtonPress, middle + QPointF(0, -100), Qt::LeftButton, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseMove, middle + QPointF(100, 0), Qt::NoButton, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseButtonRelease, middle + QPointF(100, 0), Qt::LeftButton, Qt::NoButton);
    QKeyEvent rUp(QEvent::KeyRelease, Qt::Key_R, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &rUp);
    CHECK(std::abs(canvas.rotation() - 90) < 0.5);
    // Space is an ordinary key now: no pan.
    QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &space);
    const QPointF where = canvas.canvasToWidget(QPointF(50, 50));
    sendMouse(canvas, QEvent::MouseButtonPress, middle, Qt::MiddleButton, Qt::MiddleButton);
    sendMouse(canvas, QEvent::MouseMove, middle + QPointF(30, 0), Qt::NoButton, Qt::MiddleButton);
    sendMouse(canvas, QEvent::MouseButtonRelease, middle + QPointF(30, 0), Qt::MiddleButton, Qt::NoButton);
    CHECK(near(canvas.canvasToWidget(QPointF(50, 50)), where + QPointF(30, 0), 1e-6));   // middle button pans
    QKeyEvent spaceUp(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &spaceUp);

    // The Color Picker key: down, then up only once the pick has finished.
    int held = 0, released = 0;
    QObject::connect(&canvas, &CanvasWidget::colorPickerHeld, [&](bool on) { on ? ++held : ++released; });
    QKeyEvent iDown(QEvent::KeyPress, Qt::Key_I, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &iDown);
    QKeyEvent iRepeat(QEvent::KeyPress, Qt::Key_I, Qt::NoModifier, QString(), true);
    QApplication::sendEvent(&canvas, &iRepeat);
    CHECK(held == 1 && released == 0);
    QKeyEvent iUp(QEvent::KeyRelease, Qt::Key_I, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &iUp);
    CHECK(released == 1);
    // Keypad digits count as digits.
    canvas.setHoldKeys(QKeySequence(), QKeySequence(), QKeySequence(Qt::Key_7));
    QKeyEvent pad(QEvent::KeyPress, Qt::Key_7, Qt::KeypadModifier);
    QApplication::sendEvent(&canvas, &pad);
    CHECK(held == 2);
    // Losing the keyboard focus ends a hold (the key-up goes elsewhere).
    // Not on Wayland: a made-up focus event on a window without a seat makes
    // Qt's Wayland plugin crash (no input device to ask).
    if (QGuiApplication::platformName() != QLatin1String("wayland")) {
        QFocusEvent out(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
        QApplication::sendEvent(&canvas, &out);
        CHECK(released == 2);
    }
    canvas.setCurrentTool(nullptr);
}

static QAction *command(MainWindow &w, const QString &id) {
    const Shortcuts::Entry *e = w.shortcuts()->entry(id);
    return e ? e->action.data() : nullptr;
}

static void commands() {
    QSettings(AppInfo::kId, AppInfo::kId).remove("view/rotationStep");
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QApplication::processEvents();
    auto *canvas = window.findChild<CanvasWidget *>();
    auto *indicator = window.findChild<QToolButton *>("RotationIndicator");
    CHECK(canvas && indicator);
    if (!canvas || !indicator) return;
    CHECK(std::abs(MainWindow::rotationStep() - 15) < 1e-9);

    QAction *left = command(window, "View > Rotate View > Rotate Left");
    QAction *right = command(window, "View > Rotate View > Rotate Right");
    QAction *reset = command(window, "View > Rotate View > Reset Rotation");
    QAction *step30 = command(window, QStringLiteral("View > Rotate View > 30° per Step"));
    CHECK(left && right && reset && step30);
    if (!left || !right || !reset || !step30) return;
    CHECK(left->shortcut() == QKeySequence("4") && right->shortcut() == QKeySequence("6")
          && reset->shortcut() == QKeySequence("5"));
    CHECK(!indicator->isVisible());
    right->trigger();
    CHECK(std::abs(canvas->rotation() - 15) < 1e-9);
    CHECK(indicator->isVisible() && indicator->text().contains("15"));
    left->trigger();
    left->trigger();
    CHECK(std::abs(canvas->rotation() + 15) < 1e-9);
    step30->trigger();
    CHECK(std::abs(MainWindow::rotationStep() - 30) < 1e-9);
    CHECK(step30->isChecked());
    right->trigger();
    CHECK(std::abs(canvas->rotation() - 15) < 1e-9);
    reset->trigger();
    CHECK(canvas->rotation() == 0.0 && !indicator->isVisible());
    right->trigger();
    indicator->click();   // the indicator turns the view back
    CHECK(canvas->rotation() == 0.0);

    // Settings > Canvas sets the same step; the menu shows a custom one.
    {
        SettingsDialog dialog(&window, window.shortcuts());
        auto *spin = dialog.findChild<QDoubleSpinBox *>("rotationStep");
        CHECK(spin && std::abs(spin->value() - 30) < 1e-9);
        if (spin) spin->setValue(7.5);
    }
    CHECK(std::abs(MainWindow::rotationStep() - 7.5) < 1e-9);
    right->trigger();
    CHECK(std::abs(canvas->rotation() - 7.5) < 1e-9);
    bool anyChecked = false;
    QAction *other = nullptr;
    for (QAction *a : window.findChildren<QAction *>()) {
        if (a->text().startsWith("Other Step")) other = a;
    }
    for (QMenu *menu : window.findChildren<QMenu *>())
        if (menu->title() == QStringLiteral("R&otate View")) emit menu->aboutToShow();
    for (QAction *a : step30->actionGroup()->actions()) anyChecked = anyChecked || a->isChecked();
    CHECK(!anyChecked);
    CHECK(other && other->text().contains("7.5"));
    // Rotation does not change the image or mark it modified.
    Document *doc = canvas->document();
    CHECK(doc && !doc->isModified());

    const QByteArray shots = qgetenv("PWM_SCREENSHOT_DIR");
    if (!shots.isEmpty() && doc) {
        QImage picture(doc->size(), QImage::Format_ARGB32_Premultiplied);
        picture.fill(Qt::white);
        for (int y = 0; y < picture.height(); ++y)
            for (int x = 0; x < picture.width(); ++x)
                if (((x / 50) + (y / 50)) % 2 == 0) picture.setPixel(x, y, qRgb(70, 130, 200));
        for (int y = 20; y < 120; ++y)
            for (int x = 20; x < 220; ++x) picture.setPixel(x, y, qRgb(220, 60, 40));
        doc->activeLayer()->setImage(picture);
        canvas->updateCanvas();
        canvas->setRotation(30);
        canvas->zoomToFit();
        QApplication::processEvents();
        QDir().mkpath(QString::fromLocal8Bit(shots));
        window.grab().save(QString::fromLocal8Bit(shots) + "/rotated-view.png");
        SettingsDialog dialog(&window, window.shortcuts());
        if (auto *list = dialog.findChild<QListWidget *>()) list->setCurrentRow(1);
        dialog.grab().save(QString::fromLocal8Bit(shots) + "/settings-canvas.png");
    }
}

int main(int argc, char **argv) {
    QTemporaryDir home;
    for (const char *name : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"}) {
        const QString path = home.path() + '/' + QString::fromLatin1(name);
        QDir().mkpath(path);
        qputenv(name, path.toUtf8());
    }
    QApplication app(argc, argv);
    mapping();
    fitting();
    drawing();
    heldKeys();
    commands();
    std::printf("rotation: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
