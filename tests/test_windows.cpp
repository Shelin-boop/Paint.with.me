// Several images and windows (2026-11-26): the chosen color (primary or
// secondary) is what the tools paint with; layer thumbnails in the image's
// shape; File > New sized from the clipboard; Copy takes only the selected
// area; arrow keys move the selection; the open-images strip (x to close,
// menu, reordering); other Paint.with.me windows (files opened from outside,
// images moved between windows); crash recovery per window.
// Headless (QT_QPA_PLATFORM=offscreen).
//
// The window tests start the app itself (PWM_APP_PATH) as a second window.
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QLockFile>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <cstdio>
#include <functional>
#include <memory>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "appinfo.h"
#include "canvas/canvaswidget.h"
#include "core/document.h"
#include "core/layer.h"
#include "core/nudge.h"
#include "core/psdformat.h"
#include "dialogs/newdocumentdialog.h"
#include "dialogs/settingsdialog.h"
#include <QCheckBox>
#include "dialogs/sizefields.h"
#include "instancelink.h"
#include "mainwindow.h"
#include "panels/colorspanel.h"
#include "panels/imagelistbar.h"
#include "panels/layerspanel.h"
#include "panels/toolspanel.h"
#include "tools/tool.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; \
    std::printf("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static void settle(int rounds = 5) {
    for (int i = 0; i < rounds; ++i) QApplication::processEvents();
}

// Processes events until `done` or `ms` have passed.
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

// PWM_SCREENSHOT_DIR set: a picture of `w` for a visual check.
static void screenshot(QWidget &w, const char *name) {
    const QByteArray dir = qgetenv("PWM_SCREENSHOT_DIR");
    if (dir.isEmpty()) return;
    QDir().mkpath(QString::fromLocal8Bit(dir));
    w.grab().save(QString::fromLocal8Bit(dir) + '/' + name + ".png");
}

static QMouseEvent mouse(QEvent::Type t, QPointF p, Qt::MouseButton b, Qt::MouseButtons bs) {
    return QMouseEvent(t, p, p, b, bs, Qt::NoModifier);
}

static void stroke(CanvasWidget *canvas, QPointF a, QPointF b, Qt::MouseButton button) {
    QMouseEvent e1 = mouse(QEvent::MouseButtonPress, canvas->canvasToWidget(a), button, button);
    QApplication::sendEvent(canvas, &e1);
    QMouseEvent e2 = mouse(QEvent::MouseMove, canvas->canvasToWidget(b), Qt::NoButton, button);
    QApplication::sendEvent(canvas, &e2);
    QMouseEvent e3 = mouse(QEvent::MouseButtonRelease, canvas->canvasToWidget(b), button, Qt::NoButton);
    QApplication::sendEvent(canvas, &e3);
    settle();
}

static void key(QWidget *w, QEvent::Type type, int k, bool autoRepeat = false) {
    QKeyEvent e(type, k, Qt::NoModifier, QString(), autoRepeat);
    QApplication::sendEvent(w, &e);
}

static QAction *menuAction(MainWindow &w, const QString &text) {
    for (QAction *a : w.findChildren<QAction *>())
        if (a->text() == text) return a;
    std::printf("  (no menu command \"%s\")\n", qPrintable(text));
    static QAction none;   // triggering it does nothing
    return &none;
}

// Runs `act` when a modal dialog shows up within `ms`; gives up after that.
static QPointer<QTimer> whenModal(const std::function<void(QWidget *)> &act, int ms = 8000) {
    auto *timer = new QTimer;
    timer->setInterval(20);
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    QObject::connect(timer, &QTimer::timeout, [timer, act, clock, ms]() {
        if (QWidget *modal = QApplication::activeModalWidget()) {
            timer->stop();
            timer->deleteLater();
            act(modal);
        } else if (clock->elapsed() > ms) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start();
    return timer;
}

// ---------------------------------------------------------------------------

static void chosenColor() {
    std::printf("== Primary or secondary: what the tools paint with ==\n");
    // The document alone.
    Document doc(10, 10);
    doc.setPrimaryColor(Qt::black);
    doc.setSecondaryColor(Qt::red);
    CHECK(doc.paintColor(false) == QColor(Qt::black) && doc.paintColor(true) == QColor(Qt::red));
    doc.setSecondaryActive(true);
    CHECK(doc.paintColor(false) == QColor(Qt::red) && doc.paintColor(true) == QColor(Qt::black));
    CHECK(doc.otherPaintColor(false) == QColor(Qt::black));
    doc.setPaintColor(false, Qt::green);   // the Color Picker's left button
    CHECK(doc.secondaryColor() == QColor(Qt::green) && doc.primaryColor() == QColor(Qt::black));

    MainWindow w;
    w.resize(1400, 900);
    w.show();
    settle();
    auto *canvas = w.findChild<CanvasWidget *>();
    auto *panel = w.findChild<ColorsPanel *>();
    auto *tools = w.findChild<ToolsPanel *>();
    CHECK(canvas && panel && tools);
    if (!canvas || !panel || !tools) return;
    Document *d = canvas->document();
    d->setPrimaryColor(Qt::black);
    d->setSecondaryColor(Qt::white);

    // Clicking the back swatch chooses the secondary color ...
    auto *secondary = panel->findChild<QLabel *>("secondarySwatch");
    auto *slot = panel->findChild<QComboBox *>("colorSlot");
    CHECK(secondary && slot);
    if (!secondary || !slot) return;
    QMouseEvent press = mouse(QEvent::MouseButtonPress, QPointF(4, 4), Qt::LeftButton, Qt::LeftButton);
    QApplication::sendEvent(secondary, &press);
    settle();
    CHECK(d->secondaryActive());
    CHECK(slot->currentText() == "Secondary");
    // ... a palette color then goes into it ...
    for (QToolButton *b : panel->findChildren<QToolButton *>())
        if (b->toolTip().contains("#ff0000")) { b->click(); break; }
    settle();
    CHECK(d->secondaryColor() == QColor(Qt::red) && d->primaryColor() == QColor(Qt::black));
    // ... and the left button paints with it, the right one with the primary.
    tools->toolButton(ToolType::Brush)->click();
    settle();
    stroke(canvas, {100, 100}, {200, 100}, Qt::LeftButton);
    stroke(canvas, {100, 300}, {200, 300}, Qt::RightButton);
    const QImage &image = d->activeLayer()->image();
    CHECK(image.pixelColor(150, 100) == QColor(Qt::red));
    CHECK(image.pixelColor(150, 300) == QColor(Qt::black));
    // Paint bucket too.
    d->activeLayer()->clear(Qt::white);
    tools->toolButton(ToolType::Fill)->click();
    settle();
    stroke(canvas, {20, 20}, {20, 20}, Qt::LeftButton);
    CHECK(d->activeLayer()->image().pixelColor(5, 5) == QColor(Qt::red));

    // Choosing "Primary" in the list goes back.
    slot->setCurrentIndex(0);
    settle();
    CHECK(!d->secondaryActive());
    // The C key switches.
    panel->toggleActiveColorSlot();
    CHECK(d->secondaryActive());

    // A new image takes over both colors and the choice (they belong to the
    // window, as in Paint.NET).
    QApplication::clipboard()->setImage(QImage(30, 20, QImage::Format_ARGB32));
    menuAction(w, "Paste into New &Image")->trigger();
    settle();
    Document *other = canvas->document();
    CHECK(other != d);
    CHECK(other->secondaryActive() && other->secondaryColor() == QColor(Qt::red)
          && other->primaryColor() == QColor(Qt::black));
    d->setModified(false);
    other->setModified(false);
}

// ---------------------------------------------------------------------------

static void layerThumbnails() {
    std::printf("== Layer thumbnails in the image's shape ==\n");
    // The owner's book cover: 7625 x 5250 px (here a tenth, same shape).
    Document doc(1525, 1050);
    doc.activeLayer()->clear(Qt::red);
    LayersPanel panel;
    panel.setDocument(&doc);
    panel.resize(240, 300);
    panel.show();
    settle();
    const QIcon icon = panel.makeThumbnail(*doc.activeLayer());
    const QImage pic = icon.pixmap(QSize(48, 36), 1.0).toImage().convertToFormat(QImage::Format_ARGB32);
    CHECK(pic.size() == QSize(48, 36));
    // Where the red is: the image's own shape, no checkerboard band under it.
    QRect red;
    for (int y = 0; y < pic.height(); ++y)
        for (int x = 0; x < pic.width(); ++x)
            if (pic.pixelColor(x, y) == QColor(Qt::red)) red |= QRect(x, y, 1, 1);
    CHECK(!red.isEmpty());
    const double shape = double(red.width()) / red.height();
    CHECK(std::abs(shape - 1525.0 / 1050.0) < 0.06);
    // Inside the red area nothing else; below it the frame, then nothing.
    int other = 0;
    for (int y = red.top(); y <= red.bottom(); ++y)
        for (int x = red.left(); x <= red.right(); ++x)
            if (pic.pixelColor(x, y) != QColor(Qt::red)) ++other;
    CHECK(other == 0);
    if (red.bottom() + 2 < pic.height()) {
        CHECK(pic.pixelColor(red.center().x(), red.bottom() + 1).alpha() == 255);   // frame
        CHECK(pic.pixelColor(red.center().x(), red.bottom() + 2).alpha() == 0);     // outside
    }
    // An empty layer: the checkerboard fills the same shape only.
    doc.addLayer();
    const QImage empty = panel.makeThumbnail(*doc.activeLayer()).pixmap(QSize(48, 36), 1.0).toImage()
                             .convertToFormat(QImage::Format_ARGB32);
    QRect board;
    for (int y = 0; y < empty.height(); ++y)
        for (int x = 0; x < empty.width(); ++x) {
            const QColor c = empty.pixelColor(x, y);
            if (c.alpha() == 255 && (c == QColor(255, 255, 255) || c == QColor(204, 204, 204))) board |= QRect(x, y, 1, 1);
        }
    CHECK(board == red);
    // A moved layer shows where it sits: its left part is empty now.
    doc.layerAt(0)->setOffset(QPoint(762, 0));
    const QImage moved = panel.makeThumbnail(*doc.layerAt(0)).pixmap(QSize(48, 36), 1.0).toImage()
                             .convertToFormat(QImage::Format_ARGB32);
    CHECK(moved.pixelColor(red.left() + 2, red.center().y()) != QColor(Qt::red));
    CHECK(moved.pixelColor(red.right() - 2, red.center().y()) == QColor(Qt::red));
    // Sharp on a 200 % screen: drawn with twice the pixels.
    const QPixmap hi = icon.pixmap(QSize(48, 36), 2.0);
    CHECK(hi.width() >= 48);
    doc.layerAt(0)->setOffset(QPoint());
    doc.layerAt(0)->setName("Background");
    doc.activeLayer()->setName("Empty layer");
    panel.updateLayerList();
    settle();
    screenshot(panel, "layers-panel");
}

// ---------------------------------------------------------------------------

static void clipboardAndNew() {
    std::printf("== Copy takes the selected area; File > New takes its size ==\n");
    MainWindow w;
    w.resize(1400, 900);
    w.show();
    settle();
    auto *canvas = w.findChild<CanvasWidget *>();
    Document *doc = canvas->document();
    doc->setDpi(600);
    QImage &img = doc->activeLayer()->image();
    img.fill(Qt::white);
    for (int y = 20; y < 60; ++y)
        for (int x = 50; x < 80; ++x) img.setPixelColor(x, y, QColor(0, 0, 255));
    doc->selection().selectRect(QRect(50, 20, 30, 40));
    emit doc->selectionChanged();
    menuAction(w, "&Copy")->trigger();
    settle();
    const QImage copied = QApplication::clipboard()->image();
    CHECK(copied.size() == QSize(30, 40));
    CHECK(copied.pixelColor(0, 0) == QColor(0, 0, 255) && copied.pixelColor(29, 39) == QColor(0, 0, 255));
    double dpi = 0;
    CHECK(MainWindow::clipboardPictureSize(&dpi) == QSize(30, 40));
    CHECK(dpi == 600);

    // Paste puts it back in its place ...
    doc->selection().clear();
    emit doc->selectionChanged();
    menuAction(w, "&Paste")->trigger();
    settle();
    CHECK(doc->layerCount() == 2);
    CHECK(doc->activeLayer()->image().pixelColor(50, 20) == QColor(0, 0, 255));
    CHECK(doc->activeLayer()->image().pixelColor(49, 20).alpha() == 0);

    // ... File > New starts at its size and resolution ...
    whenModal([](QWidget *modal) {
        auto *dlg = qobject_cast<NewDocumentDialog *>(modal);
        CHECK(dlg != nullptr);
        if (!dlg) { modal->close(); return; }
        CHECK(dlg->imageWidth() == 30 && dlg->imageHeight() == 40);
        CHECK(std::abs(dlg->resolution() - 600) < 1e-6);
        CHECK(dlg->presetCombo()->currentText().startsWith("Clipboard (30 x 40)"));
        dlg->accept();
    });
    menuAction(w, "&New...")->trigger();
    settle();
    Document *made = canvas->document();
    CHECK(made != doc && made->size() == QSize(30, 40) && std::abs(made->dpi() - 600) < 1e-6);

    // ... and a picture from elsewhere (no place data) still gives its size.
    QApplication::clipboard()->setImage(QImage(321, 123, QImage::Format_ARGB32));
    CHECK(MainWindow::clipboardPictureSize() == QSize(321, 123));
    whenModal([](QWidget *modal) {
        auto *dlg = qobject_cast<NewDocumentDialog *>(modal);
        CHECK(dlg && dlg->imageWidth() == 321 && dlg->imageHeight() == 123);
        if (dlg) dlg->reject(); else modal->close();
    });
    menuAction(w, "&New...")->trigger();
    settle();
    // Text on the clipboard: the usual 800 x 600.
    QApplication::clipboard()->setText("not a picture");
    CHECK(MainWindow::clipboardPictureSize().isEmpty());
    whenModal([](QWidget *modal) {
        auto *dlg = qobject_cast<NewDocumentDialog *>(modal);
        CHECK(dlg && dlg->imageWidth() == 800 && dlg->imageHeight() == 600);
        if (dlg) dlg->reject(); else modal->close();
    });
    menuAction(w, "&New...")->trigger();
    settle();

    // Pasted into a smaller image, a copied area that would not fit there
    // goes to the top left.
    doc->setModified(false);
    made->setModified(false);
    Document small(40, 50);
    QImage part(30, 40, QImage::Format_ARGB32_Premultiplied);
    part.fill(Qt::green);
    small.pasteImage(part, "Pasted", false, QPoint(50, 20));
    CHECK(small.activeLayer()->image().pixelColor(0, 0) == QColor(Qt::green));
    Document big(200, 100);
    big.pasteImage(part, "Pasted", false, QPoint(50, 20));
    CHECK(big.activeLayer()->image().pixelColor(50, 20) == QColor(Qt::green));
    CHECK(big.activeLayer()->image().pixelColor(0, 0).alpha() == 0);
}

// ---------------------------------------------------------------------------

static void arrowKeys() {
    std::printf("== Arrow keys move the selection ==\n");
    // Steps: one pixel; after three seconds 1 % of width or height.
    CHECK(Nudge::stepFor(Qt::Key_Right, 0, QSize(7625, 5250)) == QPoint(1, 0));
    CHECK(Nudge::stepFor(Qt::Key_Up, 2999, QSize(7625, 5250)) == QPoint(0, -1));
    CHECK(Nudge::stepFor(Qt::Key_Left, 3000, QSize(7625, 5250)) == QPoint(-76, 0));
    CHECK(Nudge::stepFor(Qt::Key_Down, 5000, QSize(7625, 5250)) == QPoint(0, 53));
    CHECK(Nudge::stepFor(Qt::Key_Down, 5000, QSize(20, 20)) == QPoint(0, 1));   // at least one pixel
    CHECK(Nudge::stepFor(Qt::Key_A, 0, QSize(100, 100)).isNull());

    MainWindow w;
    w.resize(1400, 900);
    w.show();
    settle();
    auto *canvas = w.findChild<CanvasWidget *>();
    auto *tools = w.findChild<ToolsPanel *>();
    Document *doc = canvas->document();
    QImage &img = doc->activeLayer()->image();
    img.fill(Qt::white);
    for (int y = 100; y < 110; ++y)
        for (int x = 100; x < 110; ++x) img.setPixelColor(x, y, Qt::blue);
    doc->selection().selectRect(QRect(100, 100, 10, 10));
    emit doc->selectionChanged();
    tools->toolButton(ToolType::Brush)->click();   // any tool but Move Selection
    settle();
    canvas->setFocus();
    const int stepsBefore = doc->history().stepCount();

    // Three presses: three pixels to the right; a hole behind.
    for (int i = 0; i < 3; ++i) {
        key(canvas, QEvent::KeyPress, Qt::Key_Right);
        key(canvas, QEvent::KeyRelease, Qt::Key_Right);
    }
    settle();
    const QImage &now = doc->activeLayer()->image();
    CHECK(now.pixelColor(103, 100) == QColor(Qt::blue) && now.pixelColor(112, 109) == QColor(Qt::blue));
    CHECK(now.pixelColor(100, 100).alpha() == 0 && now.pixelColor(102, 105).alpha() == 0);
    CHECK(doc->selection().boundingRect() == QRect(103, 100, 10, 10));
    CHECK(doc->history().stepCount() == stepsBefore + 3);   // one undo step per key press
    // Held down: one undo step for all its repeats.
    key(canvas, QEvent::KeyPress, Qt::Key_Down);
    for (int i = 0; i < 4; ++i) key(canvas, QEvent::KeyPress, Qt::Key_Down, true);
    key(canvas, QEvent::KeyRelease, Qt::Key_Down);
    settle();
    CHECK(doc->selection().boundingRect() == QRect(103, 105, 10, 10));
    CHECK(doc->history().stepCount() == stepsBefore + 4);
    CHECK(doc->activeLayer()->image().pixelColor(103, 105) == QColor(Qt::blue));
    // Undo takes the last key press back.
    doc->history().undo();
    settle();
    CHECK(doc->activeLayer()->image().pixelColor(103, 100) == QColor(Qt::blue));
    CHECK(doc->activeLayer()->image().pixelColor(103, 114) == QColor(Qt::white));

    // Held for more than three seconds: steps of 1 % (the image is 800 x 600).
    doc->selection().selectRect(QRect(103, 100, 10, 10));
    emit doc->selectionChanged();
    key(canvas, QEvent::KeyPress, Qt::Key_Right);
    QElapsedTimer held;
    held.start();
    while (held.elapsed() < Nudge::kLongPressMs + 100) {
        settle(1);
        QThread::msleep(20);
    }
    key(canvas, QEvent::KeyPress, Qt::Key_Right, true);
    key(canvas, QEvent::KeyRelease, Qt::Key_Right);
    settle();
    CHECK(doc->selection().boundingRect() == QRect(103 + 1 + 8, 100, 10, 10));

    // Move Selection: only the outline moves.
    tools->toolButton(ToolType::MoveSelection)->click();
    settle();
    const QImage pixels = doc->activeLayer()->image().copy();
    const QRect outline = doc->selection().boundingRect();
    key(canvas, QEvent::KeyPress, Qt::Key_Left);
    key(canvas, QEvent::KeyRelease, Qt::Key_Left);
    settle();
    CHECK(doc->selection().boundingRect() == outline.translated(-1, 0));
    CHECK(doc->activeLayer()->image() == pixels);

    // Without a selection the keys do nothing here.
    doc->selection().clear();
    emit doc->selectionChanged();
    const int steps = doc->history().stepCount();
    key(canvas, QEvent::KeyPress, Qt::Key_Left);
    key(canvas, QEvent::KeyRelease, Qt::Key_Left);
    CHECK(doc->history().stepCount() == steps);
    doc->setModified(false);
}

// ---------------------------------------------------------------------------

static void imageStrip() {
    std::printf("== Open-images strip: x to close, menu, reordering ==\n");
    MainWindow w;
    w.resize(1400, 900);
    w.show();
    settle();
    auto *bar = w.findChild<ImageListBar *>();
    auto *canvas = w.findChild<CanvasWidget *>();
    CHECK(bar != nullptr);
    if (!bar) return;
    for (int i = 0; i < 2; ++i) {
        QApplication::clipboard()->setImage(QImage(40 + 10 * i, 30, QImage::Format_ARGB32));
        menuAction(w, "Paste into New &Image")->trigger();
        settle();
    }
    CHECK(w.imageCount() == 3);
    settle();
    screenshot(*bar, "image-strip");
    // The x shows on the image in view (the last one) only.
    CHECK(bar->closeButton(2) && bar->closeButton(2)->isVisible());
    CHECK(bar->closeButton(0) && !bar->closeButton(0)->isVisible());
    // Under the mouse it shows too.
    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(bar->thumbnail(0), &enter);
    CHECK(bar->closeButton(0)->isVisible());
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(bar->thumbnail(0), &leave);
    CHECK(!bar->closeButton(0)->isVisible());

    // Reordering: the image in view stays in view.
    Document *third = w.imageAt(2), *first = w.imageAt(0);
    w.reorderImage(2, 0);
    settle();
    CHECK(w.imageAt(0) == third && w.imageAt(1) == first && canvas->document() == third);
    CHECK(bar->insertionIndexAt(-100) == 0 && bar->insertionIndexAt(100000) == 3);

    // The x closes that image (unchanged, so without asking).
    for (int i = 0; i < w.imageCount(); ++i) w.imageAt(i)->setModified(false);
    bar->closeButton(0)->click();
    settle();
    CHECK(w.imageCount() == 2 && w.imageAt(0) == first);
    // A middle-click closes exactly one image (it was wired twice and closed two).
    QMouseEvent middle = mouse(QEvent::MouseButtonRelease, QPointF(5, 5), Qt::MiddleButton, Qt::NoButton);
    QApplication::sendEvent(bar->thumbnail(0), &middle);
    settle();
    CHECK(w.imageCount() == 1);

    // Without a link to other windows, "Move to New Window" is off.
    bool sawMenu = false;
    QTimer::singleShot(50, [&sawMenu]() {
        for (QWidget *top : QApplication::topLevelWidgets())
            if (auto *menu = qobject_cast<QMenu *>(top); menu && menu->objectName() == "imageMenu" && menu->isVisible()) {
                sawMenu = true;
                for (QAction *a : menu->actions())
                    if (a->text() == "Move to New Window") CHECK(!a->isEnabled());
                menu->close();
            }
    });
    QContextMenuEvent context(QContextMenuEvent::Mouse, QPoint(5, 5), bar->thumbnail(0)->mapToGlobal(QPoint(5, 5)));
    QApplication::sendEvent(bar->thumbnail(0), &context);
    settle();
    CHECK(sawMenu);
    // Drag data names this window and the image.
    QMimeData *data = w.imageDragData(0);
    CHECK(data == nullptr);   // no link: nothing to drag to
}

// ---------------------------------------------------------------------------

// The app itself as a second window, with this test's folders.
static QProcess *startApp(const QStringList &arguments, const QString &home) {
    auto *app = new QProcess;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("QT_QPA_PLATFORM", "offscreen");
    env.insert("PWM_INSTANCE_DIR", qEnvironmentVariable("PWM_INSTANCE_DIR"));
    env.insert("XDG_CONFIG_HOME", home + "/config");
    env.insert("XDG_DATA_HOME", home + "/data");
    env.insert("XDG_CACHE_HOME", home + "/cache");
    env.remove("APPIMAGE");
    app->setProcessEnvironment(env);
    app->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    app->start(QStringLiteral(PWM_APP_PATH), arguments);
    app->waitForStarted(10000);
    return app;
}

static QJsonArray imagesOf(const QString &socket) {
    return InstanceLink::call(socket, {{"cmd", "list"}}, 5000).value("images").toArray();
}

static void otherWindows(const QString &home) {
    std::printf("== Other windows: files from outside, moving images ==\n");
    const QString dir = InstanceLink::folder();
    CHECK(dir == qEnvironmentVariable("PWM_INSTANCE_DIR"));

    // Settings > User Interface: on by default, can be turned off.
    {
        SettingsDialog dialog;
        auto *single = dialog.findChild<QCheckBox *>("singleWindow");
        CHECK(single && single->isChecked() && InstanceLink::handOverEnabled());
        if (single) single->setChecked(false);
        CHECK(!InstanceLink::handOverEnabled());
        if (single) single->setChecked(true);
        CHECK(InstanceLink::handOverEnabled());
    }

    // A socket left behind by a crashed window is cleared away.
    {
        const QString stale = dir + "/99999999.sock";
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", QFile::encodeName(stale).constData());
        ::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        ::close(fd);   // bound, never listened: refused
        CHECK(QFile::exists(stale));
        CHECK(!InstanceLink::handOver({}));   // nobody to give it to
        CHECK(!QFile::exists(stale));
    }

    MainWindow w;
    w.resize(1200, 800);
    InstanceLink link;
    CHECK(link.isListening());
    w.setInstanceLink(&link);
    w.show();
    settle();
    auto *canvas = w.findChild<CanvasWidget *>();
    QFile active(dir + "/active");
    CHECK(active.open(QIODevice::ReadOnly) && active.readAll().trimmed().toLongLong() == QCoreApplication::applicationPid());
    active.close();

    // Starting the app again hands the file over and ends at once.
    const QString png = home + "/handed.png";
    QImage picture(64, 48, QImage::Format_ARGB32);
    picture.fill(Qt::magenta);
    picture.save(png);
    QProcess *again = startApp({png}, home);
    CHECK(waitFor([again]() { return again->state() == QProcess::NotRunning; }, 15000));
    CHECK(again->exitCode() == 0);
    delete again;
    CHECK(waitFor([&]() { return canvas->document()->filePath() == png; }, 5000));
    CHECK(w.imageCount() == 1);   // the pristine untitled image made room

    // A layered, changed image to move.
    Document *doc = canvas->document();
    doc->addLayer(QStringLiteral("Second"));
    doc->addLayer(QStringLiteral("Third"));
    doc->setDpi(300);
    doc->setModified(true);
    const quint64 id = MainWindow::imageId(doc);
    QMimeData *drag = w.imageDragData(0);
    CHECK(drag && drag->hasFormat(InstanceLink::imageMimeType()));
    if (drag) {
        const QJsonObject payload = QJsonDocument::fromJson(drag->data(InstanceLink::imageMimeType())).object();
        CHECK(payload.value("socket").toString() == link.socketPath());
        CHECK(quint64(payload.value("doc").toDouble()) == id);
        delete drag;
    }

    // "Move to New Window": a new window takes the image over ...
    QProcess *second = startApp({"--new-window", "--take", link.socketPath(), QString::number(id)}, home);
    const QString secondSocket = InstanceLink::socketPathFor(second->processId());
    CHECK(waitFor([&]() { return w.imageCount() == 1 && w.imageAt(0) != doc; }, 20000));
    CHECK(w.imageAt(0) && w.imageAt(0)->filePath().isEmpty());   // a fresh image is left here
    QJsonArray there;
    waitFor([&]() { there = imagesOf(secondSocket); return !there.isEmpty(); }, 5000);
    CHECK(there.size() == 1);
    if (!there.isEmpty()) {
        const QJsonObject moved = there.first().toObject();
        CHECK(moved.value("path").toString() == png);
        CHECK(moved.value("layers").toInt() == 3);
        CHECK(moved.value("modified").toBool());
        CHECK(moved.value("width").toInt() == 64 && moved.value("height").toInt() == 48);

        // ... and dragging it back moves it here again.
        const QJsonObject from{{"socket", secondSocket}, {"doc", moved.value("doc")}, {"title", "handed.png"}};
        CHECK(w.receiveImage(from));
        CHECK(canvas->document()->filePath() == png && canvas->document()->layerCount() == 3);
        CHECK(std::abs(canvas->document()->dpi() - 300) < 0.5);
        CHECK(canvas->document()->layerAt(2)->name() == "Third");
        CHECK(w.imageCount() == 1);   // the fresh image made room
        CHECK(waitFor([&]() { return imagesOf(secondSocket).size() == 1
                                     && imagesOf(secondSocket).first().toObject().value("path").toString().isEmpty(); },
                      5000));
    }
    // A move that does not work leaves both as they were.
    QString error;
    whenModal([&error](QWidget *modal) {
        error = modal->objectName();
        modal->close();
    });
    CHECK(!w.receiveImage({{"socket", secondSocket}, {"doc", 123456.0}, {"title", "gone"}}));
    CHECK(error == "moveImageError");
    CHECK(w.imageCount() == 1);

    // A request older than the sender waits is not followed.
    const QJsonObject late = InstanceLink::call(secondSocket, {{"cmd", "open"}, {"files", QJsonArray{png}},
                                                               {"sent", double(QDateTime::currentMSecsSinceEpoch() - 60000)}},
                                                5000);
    CHECK(late.value("ok").toBool() == false && late.value("error").toString() == "late");

    second->terminate();
    second->waitForFinished(5000);
    delete second;
    for (int i = 0; i < w.imageCount(); ++i) w.imageAt(i)->setModified(false);
}

// ---------------------------------------------------------------------------

static void recoveryPerWindow() {
    std::printf("== Crash recovery: a snapshot folder per window ==\n");
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recovery";
    QDir(root).removeRecursively();

    // A snapshot of a window that is still open is left alone.
    {
        MainWindow first;
        first.show();
        settle();
        auto *canvas = first.findChild<CanvasWidget *>();
        canvas->document()->setModified(true);
        QTimer *autosave = nullptr;
        for (QTimer *t : first.findChildren<QTimer *>())
            if (t->interval() == 60 * 1000) autosave = t;
        CHECK(autosave != nullptr);
        if (!autosave) return;
        autosave->setInterval(1);
        QString file;
        CHECK(waitFor([&]() {
            QDirIterator it(root, {"autosave.psd"}, QDir::Files, QDirIterator::Subdirectories);
            if (it.hasNext()) file = it.next();
            return !file.isEmpty() && QFile::exists(QFileInfo(file).path() + "/info.ini");
        }, 10000));
        autosave->stop();
        // Its folder is locked while the window is open.
        QLockFile probe(QFileInfo(file).path() + "/lock");
        probe.setStaleLockTime(0);
        CHECK(!probe.tryLock(0));

        bool asked = false;
        QPointer<QTimer> watch = whenModal([&asked](QWidget *modal) { asked = true; modal->close(); });
        MainWindow second;
        second.show();
        waitFor([]() { return false; }, 700);   // recovery is checked after 400 ms
        delete watch.data();
        CHECK(!asked);
        CHECK(QFile::exists(file));
        // A clean exit removes the window's folder.
        canvas->document()->setModified(false);
        first.close();
        settle();
        CHECK(!QFile::exists(file) && !QDir(QFileInfo(file).path()).exists());
        if (QWidget *modal = QApplication::activeModalWidget()) modal->close();
    }

    // A folder left by a window that ended without closing is offered.
    const QString dead = root + "/12345-1";
    QDir().mkpath(dead);
    QImage(20, 10, QImage::Format_ARGB32).save(dead + "/autosave.png");
    {
        QSettings info(dead + "/info.ini", QSettings::IniFormat);
        info.setValue("recovery/pending", true);
        info.setValue("recovery/origPath", "/tmp/cover.psd");
        info.setValue("recovery/when", "2026-11-26T12:00:00");
    }
    QString question;
    whenModal([&question](QWidget *modal) {
        if (auto *box = qobject_cast<QMessageBox *>(modal)) {
            question = box->text();
            box->button(QMessageBox::No)->click();
        } else {
            modal->close();
        }
    });
    MainWindow third;
    third.show();
    CHECK(waitFor([&]() { return !question.isEmpty(); }, 3000));
    CHECK(question.contains("cover.psd"));
    settle();
    CHECK(!QDir(dead).exists());   // answered: gone
}

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // Settings, autosave and the window sockets away from the real ones.
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
    qputenv("XDG_CACHE_HOME", (home.path() + "/cache").toUtf8());
    QDir().mkpath(home.path() + "/run");
    qputenv("PWM_INSTANCE_DIR", (home.path() + "/run").toUtf8());

    QApplication app(argc, argv);
    app.setApplicationName(AppInfo::kId);

    chosenColor();
    layerThumbnails();
    clipboardAndNew();
    arrowKeys();
    imageStrip();
    otherWindows(home.path());
    recoveryPerWindow();

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
