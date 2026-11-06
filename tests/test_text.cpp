// Editable text frames: creating, typing, formatting, editing again, undo,
// moving and resizing frames, painting tools kept off text layers, turning
// text into pixels, image operations, and .psd round trips. Headless.
//
// PWM_SCREENSHOT_DIR=<folder> saves a picture of a frame being edited.
#include <QApplication>
#include <QAbstractButton>
#include <QBuffer>
#include <QDir>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>
#include <cmath>
#include <cstdio>

#include "canvas/canvaswidget.h"
#include "core/document.h"
#include "core/layer.h"
#include "core/psdformat.h"
#include "core/textframe.h"
#include "core/thumbnails.h"
#include "mainwindow.h"
#include "tools/brushtool.h"
#include "tools/movetool.h"
#include "tools/texttool.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; \
    std::printf("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

namespace {

void press(Tool &tool, CanvasWidget &canvas, QPointF at, Qt::KeyboardModifiers mods = Qt::NoModifier,
           QEvent::Type type = QEvent::MouseButtonPress) {
    QMouseEvent e(type, at, at, Qt::LeftButton, Qt::LeftButton, mods);
    tool.mousePressEvent(at, &e, canvas);
}
void move(Tool &tool, CanvasWidget &canvas, QPointF at) {
    QMouseEvent e(QEvent::MouseMove, at, at, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    tool.mouseMoveEvent(at, &e, canvas);
}
void release(Tool &tool, CanvasWidget &canvas, QPointF at) {
    QMouseEvent e(QEvent::MouseButtonRelease, at, at, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    tool.mouseReleaseEvent(at, &e, canvas);
}
void click(Tool &tool, CanvasWidget &canvas, QPointF at) {
    press(tool, canvas, at);
    release(tool, canvas, at);
}
void drag(Tool &tool, CanvasWidget &canvas, QPointF from, QPointF to) {
    press(tool, canvas, from);
    move(tool, canvas, (from + to) / 2);
    move(tool, canvas, to);
    release(tool, canvas, to);
}
void type(Tool &tool, CanvasWidget &canvas, const QString &text) {
    for (QChar ch : text) {
        QKeyEvent k(QEvent::KeyPress, 0, Qt::NoModifier, QString(ch));
        tool.keyPressEvent(&k, canvas);
    }
}
void key(Tool &tool, CanvasWidget &canvas, int k, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QKeyEvent e(QEvent::KeyPress, k, mods);
    tool.keyPressEvent(&e, canvas);
}

// Bounding box of pixels darker than mid gray (on white).
QRect ink(const QImage &img) {
    int x0 = img.width(), y0 = img.height(), x1 = -1, y1 = -1;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(img.pixel(x, y)) > 128 && qGray(img.pixel(x, y)) < 128) {
                x0 = std::min(x0, x); y0 = std::min(y0, y);
                x1 = std::max(x1, x); y1 = std::max(y1, y);
            }
    return x1 < 0 ? QRect() : QRect(QPoint(x0, y0), QPoint(x1, y1));
}

int textLayerCount(const Document &doc) {
    int n = 0;
    for (int i = 0; i < doc.layerCount(); ++i)
        if (doc.layerAt(i)->isTextLayer()) ++n;
    return n;
}

QString textOf(const Layer *layer) {
    auto t = layer ? layer->textFrame() : nullptr;
    return t ? t->plainText() : QStringLiteral("<not text>");
}

QTextDocument *laidOut(const Layer *layer, QTextDocument &doc) {
    auto t = layer->textFrame();
    t->setUp(doc);
    return &doc;
}

struct Setup {
    Document doc;
    CanvasWidget canvas;
    TextTool tool;
    explicit Setup(int w = 400, int h = 200) : doc(w, h) {
        doc.activeLayer()->clear(Qt::white);
        doc.setPrimaryColor(Qt::black);
        canvas.setDocument(&doc);
        canvas.setCurrentTool(&tool);
        tool.setFontSize(24);
    }
};

}  // namespace

static void createAndUndo() {
    Setup s;
    const QImage background = s.doc.layerAt(0)->image();
    const int stepsBefore = s.doc.history().currentIndex();
    click(s.tool, s.canvas, QPointF(20, 30));
    CHECK(s.tool.isEditing());
    CHECK(s.doc.layerCount() == 2);                    // the frame is a layer of its own
    CHECK(s.doc.activeLayerIndex() == 1);
    type(s.tool, s.canvas, QStringLiteral("Hello"));
    CHECK(s.tool.plainText() == QStringLiteral("Hello"));
    CHECK(textOf(s.doc.layerAt(1)) == QStringLiteral("Hello"));   // drawn while typing
    CHECK(s.doc.layerAt(1)->name() == QStringLiteral("Hello"));    // named after its text
    CHECK(!ink(s.doc.flatten()).isEmpty());
    key(s.tool, s.canvas, Qt::Key_Escape);
    CHECK(!s.tool.isEditing());
    CHECK(s.doc.history().currentIndex() == stepsBefore + 1);
    CHECK(s.doc.history().undoDescription() == QStringLiteral("Text"));
    CHECK(s.doc.layerAt(0)->image() == background);    // the background was not painted on
    const QRect drawn = ink(s.doc.flatten());
    CHECK(drawn.left() >= 19 && drawn.left() <= 26 && drawn.top() >= 30 && drawn.top() <= 40);

    s.doc.history().undo();
    CHECK(s.doc.layerCount() == 1);
    CHECK(ink(s.doc.flatten()).isEmpty());
    s.doc.history().redo();
    CHECK(s.doc.layerCount() == 2 && s.doc.layerAt(1)->isTextLayer());
    CHECK(textOf(s.doc.layerAt(1)) == QStringLiteral("Hello"));

    // A click that types nothing leaves nothing behind, not even "modified".
    s.doc.setModified(false);
    const int steps = s.doc.history().currentIndex();
    click(s.tool, s.canvas, QPointF(300, 150));
    CHECK(s.doc.layerCount() == 3);
    key(s.tool, s.canvas, Qt::Key_Escape);
    CHECK(s.doc.layerCount() == 2);
    CHECK(s.doc.history().currentIndex() == steps);
    CHECK(!s.doc.isModified());
}

static void wrapAndAlign() {
    Setup s(600, 400);
    drag(s.tool, s.canvas, QPointF(50, 40), QPointF(250, 200));
    CHECK(s.tool.isEditing());
    type(s.tool, s.canvas, QStringLiteral("one two three four five six seven eight"));
    const QRect wrapped = ink(s.doc.flatten());
    CHECK(wrapped.right() <= 252);                 // wraps inside the frame
    CHECK(wrapped.height() > 60);                  // over several lines

    s.tool.selectAll();
    s.tool.setAlignment(TextTool::Align::Right);
    const QRect right = ink(s.doc.flatten());
    CHECK(right.right() >= 240 && right.right() <= 252);
    s.tool.setAlignment(TextTool::Align::Center);
    CHECK(s.tool.alignment() == TextTool::Align::Center);
    s.tool.setAlignment(TextTool::Align::Justify);
    {
        QTextDocument d;
        laidOut(s.doc.activeLayer(), d);
        CHECK(d.begin().blockFormat().alignment() & Qt::AlignJustify);
    }
    key(s.tool, s.canvas, Qt::Key_Escape);
    CHECK(s.doc.history().currentIndex() == 1);    // one step for the whole session

    // Paragraphs and line breaks.
    Setup p;
    click(p.tool, p.canvas, QPointF(10, 10));
    type(p.tool, p.canvas, QStringLiteral("a"));
    key(p.tool, p.canvas, Qt::Key_Return);
    type(p.tool, p.canvas, QStringLiteral("b"));
    key(p.tool, p.canvas, Qt::Key_Return, Qt::ShiftModifier);
    type(p.tool, p.canvas, QStringLiteral("c"));
    QTextDocument d;
    p.tool.finishEditing();
    laidOut(p.doc.activeLayer(), d);
    CHECK(d.blockCount() == 2);
    CHECK(d.lastBlock().text() == QStringLiteral("b") + QChar(QChar::LineSeparator) + QStringLiteral("c"));
}

static void formatting() {
    Setup s(400, 200);
    s.doc.setDpi(300);
    s.tool.setFontSize(12);
    click(s.tool, s.canvas, QPointF(10, 10));
    type(s.tool, s.canvas, QStringLiteral("plain bold"));
    s.tool.select(6, 10);
    s.tool.setBold(true);
    s.doc.setPrimaryColor(Qt::red);
    s.tool.setColor(Qt::red);
    s.tool.select(0, 5);
    CHECK(!s.tool.bold());
    CHECK(std::abs(s.tool.fontSize() - 12.0) < 0.01);   // points at the image's resolution
    s.tool.select(7, 7);
    CHECK(s.tool.bold());
    s.tool.finishEditing();

    QTextDocument d;
    laidOut(s.doc.activeLayer(), d);
    QTextCursor c(&d);
    c.setPosition(3);
    CHECK(c.charFormat().fontWeight() < QFont::DemiBold);
    CHECK(std::abs(c.charFormat().fontPointSize() - 50.0) < 0.01);   // 12 pt at 300 dpi = 50 px
    c.setPosition(8);
    CHECK(c.charFormat().fontWeight() >= QFont::Bold);
    CHECK(c.charFormat().foreground().color() == QColor(Qt::red));

    // Red text shows up red, black text black and gray (no colored fringes).
    const QImage flat = s.doc.flatten();
    int red = 0, colored = 0;
    for (int y = 0; y < flat.height(); ++y)
        for (int x = 0; x < flat.width(); ++x) {
            const QColor px = flat.pixelColor(x, y);
            if (px.red() > 200 && px.green() < 60) ++red;
            if (x < 120 && (std::abs(px.red() - px.green()) > 2 || std::abs(px.green() - px.blue()) > 2)) ++colored;
        }
    CHECK(red > 50);
    CHECK(colored == 0);

    // Ctrl+B toggles bold on the selection; Ctrl+Z inside the frame undoes typing.
    Setup k;
    click(k.tool, k.canvas, QPointF(10, 10));
    type(k.tool, k.canvas, QStringLiteral("abc"));
    key(k.tool, k.canvas, Qt::Key_A, Qt::ControlModifier);
    key(k.tool, k.canvas, Qt::Key_B, Qt::ControlModifier);
    CHECK(k.tool.bold());
    key(k.tool, k.canvas, Qt::Key_Right);
    type(k.tool, k.canvas, QStringLiteral("d"));
    CHECK(k.tool.plainText() == QStringLiteral("abcd"));
    key(k.tool, k.canvas, Qt::Key_Z, Qt::ControlModifier);
    CHECK(k.tool.plainText() == QStringLiteral("abc"));
    key(k.tool, k.canvas, Qt::Key_Backspace, Qt::ControlModifier);
    CHECK(k.tool.plainText().isEmpty());

    // Antialiasing off: hard edges only.
    Setup a;
    a.tool.setAntialiased(false);
    click(a.tool, a.canvas, QPointF(10, 10));
    type(a.tool, a.canvas, QStringLiteral("Wavy"));
    a.tool.finishEditing();
    const QImage &img = a.doc.activeLayer()->image();
    int partial = 0, solid = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const int alpha = qAlpha(img.pixel(x, y));
            if (alpha == 255) ++solid; else if (alpha != 0) ++partial;
        }
    CHECK(solid > 50 && partial == 0);
}

static void editAgain() {
    Setup s;
    click(s.tool, s.canvas, QPointF(20, 20));
    type(s.tool, s.canvas, QStringLiteral("first"));
    s.tool.finishEditing();
    CHECK(s.doc.history().currentIndex() == 1);

    // Clicking the text opens it again with the caret at the click.
    s.doc.setActiveLayer(0);
    press(s.tool, s.canvas, QPointF(22, 30));
    release(s.tool, s.canvas, QPointF(22, 30));
    CHECK(s.tool.isEditing());
    CHECK(s.doc.layerCount() == 2);            // no new layer
    CHECK(s.doc.activeLayerIndex() == 1);
    key(s.tool, s.canvas, Qt::Key_End);
    type(s.tool, s.canvas, QStringLiteral(" second"));
    key(s.tool, s.canvas, Qt::Key_Escape);
    CHECK(textOf(s.doc.layerAt(1)) == QStringLiteral("first second"));
    CHECK(s.doc.history().undoDescription() == QStringLiteral("Edit Text"));
    s.doc.history().undo();
    CHECK(textOf(s.doc.layerAt(1)) == QStringLiteral("first"));
    CHECK(s.doc.layerAt(1)->name() == QStringLiteral("first"));
    s.doc.history().redo();
    CHECK(textOf(s.doc.layerAt(1)) == QStringLiteral("first second"));

    // Opening it and changing nothing records nothing.
    const int steps = s.doc.history().currentIndex();
    click(s.tool, s.canvas, QPointF(22, 30));
    CHECK(s.tool.isEditing());
    key(s.tool, s.canvas, Qt::Key_Escape);
    CHECK(s.doc.history().currentIndex() == steps);

    // Undo from the menu while typing first records the typing, then undoes it.
    click(s.tool, s.canvas, QPointF(22, 30));
    key(s.tool, s.canvas, Qt::Key_End);
    type(s.tool, s.canvas, QStringLiteral("!"));
    s.doc.history().undo();
    CHECK(!s.tool.isEditing());
    CHECK(textOf(s.doc.layerAt(1)) == QStringLiteral("first second"));
    s.doc.history().redo();
    CHECK(textOf(s.doc.layerAt(1)) == QStringLiteral("first second!"));

    // Another command while typing: the text step comes first.
    click(s.tool, s.canvas, QPointF(22, 30));
    key(s.tool, s.canvas, Qt::Key_End);
    type(s.tool, s.canvas, QStringLiteral("?"));
    s.doc.addLayer(QStringLiteral("after"));
    const QStringList list = s.doc.history().historyList();
    CHECK(list.size() >= 2 && list.at(list.size() - 2) == QStringLiteral("Edit Text"));
    CHECK(!s.tool.isEditing());
    CHECK(textOf(s.doc.layerAt(1)) == QStringLiteral("first second!?"));

    // Switching documents finishes the frame in its own document.
    Setup t;
    Document other(100, 100);
    click(t.tool, t.canvas, QPointF(10, 10));
    type(t.tool, t.canvas, QStringLiteral("stay"));
    t.canvas.setDocument(&other);
    CHECK(!t.tool.isEditing());
    CHECK(t.doc.layerCount() == 2 && textOf(t.doc.layerAt(1)) == QStringLiteral("stay"));
    CHECK(other.layerCount() == 1);
}

static void moveAndResizeFrame() {
    Setup s(500, 300);
    drag(s.tool, s.canvas, QPointF(40, 40), QPointF(200, 120));
    type(s.tool, s.canvas, QStringLiteral("frame text that wraps"));
    const QRect before = ink(s.doc.flatten());
    // Drag the frame's left border (not a handle) 60 px right and 30 down.
    const QPointF border(40, 90);
    press(s.tool, s.canvas, border);
    move(s.tool, s.canvas, border + QPointF(30, 15));
    move(s.tool, s.canvas, border + QPointF(60, 30));
    release(s.tool, s.canvas, border + QPointF(60, 30));
    const QRect after = ink(s.doc.flatten());
    CHECK(after.topLeft() - before.topLeft() == QPoint(60, 30));
    CHECK(s.tool.isEditing());
    // The right-middle handle sets a wider frame: fewer lines.
    const QRect frame = ink(s.doc.flatten());
    const QPointF handle(260, 30 + 40 + (120 - 40) / 2.0);
    press(s.tool, s.canvas, handle);
    move(s.tool, s.canvas, handle + QPointF(200, 0));
    release(s.tool, s.canvas, handle + QPointF(200, 0));
    CHECK(ink(s.doc.flatten()).height() < frame.height());
    s.tool.finishEditing();
    CHECK(s.doc.history().currentIndex() == 1);

    // The Move tool moves a text layer without a selection and it stays text.
    MoveTool mover;
    s.canvas.setCurrentTool(&mover);
    const QRect start = ink(s.doc.flatten());
    QMouseEvent p(QEvent::MouseButtonPress, QPointF(150, 100), QPointF(150, 100), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    mover.mousePressEvent(QPointF(150, 100), &p, s.canvas);
    QMouseEvent m(QEvent::MouseMove, QPointF(130, 150), QPointF(130, 150), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    mover.mouseMoveEvent(QPointF(130, 150), &m, s.canvas);
    QMouseEvent r(QEvent::MouseButtonRelease, QPointF(130, 150), QPointF(130, 150), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    mover.mouseReleaseEvent(QPointF(130, 150), &r, s.canvas);
    CHECK(s.doc.activeLayer()->isTextLayer());
    CHECK(ink(s.doc.flatten()).topLeft() - start.topLeft() == QPoint(-20, 50));
    CHECK(s.doc.activeLayer()->textShift() == QPoint(-20, 50));
    // Editing it again draws it where it now is.
    s.canvas.setCurrentTool(&s.tool);
    const QPointF inside = QPointF(ink(s.doc.flatten()).center());
    click(s.tool, s.canvas, inside);
    CHECK(s.tool.isEditing());
    key(s.tool, s.canvas, Qt::Key_End, Qt::ControlModifier);
    type(s.tool, s.canvas, QStringLiteral("."));
    s.tool.finishEditing();
    CHECK(std::abs(ink(s.doc.flatten()).left() - start.left() + 20) <= 1);
    CHECK(s.doc.activeLayer()->textShift() == QPoint());
}

static void paintingToolsStayOff() {
    Setup s;
    click(s.tool, s.canvas, QPointF(20, 20));
    type(s.tool, s.canvas, QStringLiteral("keep"));
    s.tool.finishEditing();
    BrushTool brush;
    s.canvas.setCurrentTool(&brush);
    int blocked = 0;
    QObject::connect(&s.canvas, &CanvasWidget::textLayerBlocked, [&blocked](const QString &) { ++blocked; });
    const QImage layerBefore = s.doc.activeLayer()->image();
    s.canvas.show();
    QApplication::processEvents();
    QMouseEvent p(QEvent::MouseButtonPress, s.canvas.canvasToWidget(QPointF(30, 30)),
                  s.canvas.mapToGlobal(s.canvas.canvasToWidget(QPointF(30, 30)).toPoint()),
                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&s.canvas, &p);
    QMouseEvent r(QEvent::MouseButtonRelease, s.canvas.canvasToWidget(QPointF(30, 30)),
                  s.canvas.mapToGlobal(s.canvas.canvasToWidget(QPointF(30, 30)).toPoint()),
                  Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&s.canvas, &r);
    CHECK(blocked == 1);
    CHECK(s.doc.activeLayer()->isTextLayer());
    CHECK(s.doc.activeLayer()->image() == layerBefore);
    // On the background it paints as always.
    s.doc.setActiveLayer(0);
    QApplication::sendEvent(&s.canvas, &p);
    QApplication::sendEvent(&s.canvas, &r);
    CHECK(blocked == 1);
    CHECK(s.doc.history().undoDescription() != QStringLiteral("Text"));
}

static void convertToPixels() {
    Setup s;
    click(s.tool, s.canvas, QPointF(20, 20));
    type(s.tool, s.canvas, QStringLiteral("Pixels"));
    s.tool.finishEditing();
    const QImage flat = s.doc.flatten();
    CHECK(s.doc.convertTextToPixels(1));
    const Layer *layer = s.doc.layerAt(1);
    CHECK(!layer->isTextLayer());
    CHECK(QRect(layer->offset(), layer->image().size()).contains(QRect(QPoint(), s.doc.size())));
    CHECK(s.doc.flatten() == flat);                    // looks the same
    CHECK(s.doc.history().undoDescription() == QStringLiteral("Convert Text to Pixels"));
    s.doc.history().undo();
    CHECK(s.doc.layerAt(1)->isTextLayer());
    CHECK(textOf(s.doc.layerAt(1)) == QStringLiteral("Pixels"));
    CHECK(!s.doc.convertTextToPixels(0));              // not a text layer
}

static void imageOperations() {
    Setup s(400, 200);
    click(s.tool, s.canvas, QPointF(40, 40));
    type(s.tool, s.canvas, QStringLiteral("Scale"));
    s.tool.finishEditing();
    const QRect small = ink(s.doc.flatten());
    QTextDocument d0;
    laidOut(s.doc.layerAt(1), d0);
    const double sizeBefore = TextFrame::largestPixelSize(d0);

    s.doc.resize(800, 400);   // twice the size, same proportions: still text, sharp
    CHECK(s.doc.layerAt(1)->isTextLayer());
    QTextDocument d1;
    laidOut(s.doc.layerAt(1), d1);
    CHECK(std::abs(TextFrame::largestPixelSize(d1) - 2 * sizeBefore) < 0.01);
    const QRect big = ink(s.doc.flatten());
    CHECK(std::abs(big.left() - 2 * small.left()) <= 3 && std::abs(big.width() - 2 * small.width()) <= 6);
    s.doc.history().undo();
    CHECK(s.doc.layerAt(1)->isTextLayer() && ink(s.doc.flatten()) == small);

    s.doc.resize(800, 200);   // stretched: becomes pixels
    CHECK(!s.doc.layerAt(1)->isTextLayer());
    s.doc.history().undo();
    CHECK(s.doc.layerAt(1)->isTextLayer());

    s.doc.resizeCanvas(500, 300, 50, 25);   // Canvas Size moves it along
    CHECK(s.doc.layerAt(1)->isTextLayer());
    CHECK(ink(s.doc.flatten()).topLeft() == small.topLeft() + QPoint(50, 25));
    s.doc.cropTo(QRect(30, 20, 300, 200));   // Crop keeps it whole
    CHECK(s.doc.layerAt(1)->isTextLayer());
    CHECK(ink(s.doc.flatten()).topLeft() == small.topLeft() + QPoint(20, 5));

    s.doc.rotate(1, QStringLiteral("Rotate"));   // a frame cannot turn: pixels
    CHECK(!s.doc.layerAt(1)->isTextLayer());
    s.doc.history().undo();
    CHECK(s.doc.layerAt(1)->isTextLayer());

    // Merging a text layer down gives pixels covering the canvas.
    s.doc.mergeLayerDown(1);
    CHECK(s.doc.layerCount() == 1 && !s.doc.layerAt(0)->isTextLayer());
    s.doc.history().undo();
    CHECK(s.doc.layerAt(1)->isTextLayer());

    // Duplicating keeps it text; reading it (thumbnails, combining) does not
    // change it.
    s.doc.duplicateLayer(1);
    CHECK(s.doc.layerAt(2)->isTextLayer());
    Thumbnails::cached(s.doc.layerAt(2)->image(), QSize(40, 30));
    s.doc.flattenVisible();
    QImage view(200, 100, QImage::Format_ARGB32_Premultiplied);
    s.doc.compositeInto(view, QPoint(0, 0));
    s.canvas.resize(300, 200);
    s.canvas.grab();
    CHECK(s.doc.layerAt(1)->isTextLayer() && s.doc.layerAt(2)->isTextLayer());
}

static void psdRoundTrip() {
    QTemporaryDir dir;
    const QString path = dir.filePath("text.psd");
    Setup s(300, 150);
    s.doc.setDpi(150);
    drag(s.tool, s.canvas, QPointF(20, 20), QPointF(200, 100));
    type(s.tool, s.canvas, QStringLiteral("Round trip"));
    s.tool.select(0, 5);
    s.tool.setItalic(true);
    s.tool.finishEditing();
    // Moved after drawing: the file stores where it is now.
    s.doc.layerAt(1)->setOffset(s.doc.layerAt(1)->offset() + QPoint(10, 5));
    const QImage flat = s.doc.flatten();
    const QRectF box = [&] { TextFrame f = *s.doc.layerAt(1)->textFrame(); return f.box.translated(s.doc.layerAt(1)->textShift()); }();
    CHECK(s.doc.save(path));

    Document back(path);
    CHECK(back.layerCount() == 2);
    CHECK(back.layerAt(1)->isTextLayer());
    CHECK(textOf(back.layerAt(1)) == QStringLiteral("Round trip"));
    CHECK(back.layerAt(1)->textFrame()->box == box);
    CHECK(back.flatten() == flat);
    CHECK(back.loadNotes().isEmpty());
    {
        QTextDocument d;
        laidOut(back.layerAt(1), d);
        QTextCursor c(&d);
        c.setPosition(2);
        CHECK(c.charFormat().fontItalic());
        c.setPosition(8);
        CHECK(!c.charFormat().fontItalic());
    }
    // ... and can be edited there.
    CanvasWidget canvas;
    canvas.setDocument(&back);
    TextTool tool;
    canvas.setCurrentTool(&tool);
    CHECK(tool.beginEditing(canvas, 1));
    key(tool, canvas, Qt::Key_End, Qt::ControlModifier);
    type(tool, canvas, QStringLiteral("!"));
    tool.finishEditing();
    CHECK(textOf(back.layerAt(1)) == QStringLiteral("Round trip!"));

    // Other programs read the block as data they skip.
    Psd::ImageData data;
    CHECK(Psd::read(path, data));
    CHECK(data.layers.size() == 2 && !data.layers[1].textFrame.isEmpty());
    CHECK(data.layers[0].textFrame.isEmpty());

    // A damaged block opens as pixels.
    QFile f(path);
    CHECK(f.open(QIODevice::ReadOnly));
    QByteArray bytes = f.readAll();
    f.close();
    const int at = bytes.indexOf("\"html\"");
    CHECK(at > 0);
    bytes[at + 1] = 'X';
    QFile g(dir.filePath("damaged.psd"));
    CHECK(g.open(QIODevice::WriteOnly));
    g.write(bytes);
    g.close();
    Document damaged(dir.filePath("damaged.psd"));
    CHECK(damaged.layerCount() == 2 && !damaged.layerAt(1)->isTextLayer());
    CHECK(damaged.flatten() == flat);

    if (const QString out = qEnvironmentVariable("PWM_PSD_OUT"); !out.isEmpty()) {
        QDir().mkpath(out);
        QFile::copy(path, out + "/text-layer.psd");
    }
}

static void keysAndInputMethods() {
    Setup s;
    CHECK(!s.tool.wantsKeyInput());
    click(s.tool, s.canvas, QPointF(20, 20));
    CHECK(s.tool.wantsKeyInput());
    // Letters that are tool shortcuts are typed; text chords act on the text;
    // other chords (Ctrl+S) still reach the program.
    int claimed = 0;
    for (char c = 'a'; c <= 'z'; ++c) {
        QKeyEvent so(QEvent::ShortcutOverride, Qt::Key_A + (c - 'a'), Qt::NoModifier, QString(c));
        QApplication::sendEvent(&s.canvas, &so);
        if (so.isAccepted()) ++claimed;
    }
    CHECK(claimed == 26);
    QKeyEvent ctrlA(QEvent::ShortcutOverride, Qt::Key_A, Qt::ControlModifier, QString());
    QApplication::sendEvent(&s.canvas, &ctrlA);
    CHECK(ctrlA.isAccepted());
    QKeyEvent ctrlS(QEvent::ShortcutOverride, Qt::Key_S, Qt::ControlModifier, QString());
    QApplication::sendEvent(&s.canvas, &ctrlS);
    CHECK(!ctrlS.isAccepted());
    // AltGr characters (German keyboard: @, euro sign) are typed.
    QKeyEvent at(QEvent::KeyPress, Qt::Key_At, Qt::GroupSwitchModifier, QStringLiteral("@"));
    s.tool.keyPressEvent(&at, s.canvas);
    // An input method commits composed text.
    CHECK(s.canvas.inputMethodQuery(Qt::ImEnabled).toBool());
    QInputMethodEvent im;
    im.setCommitString(QStringLiteral("été"));
    QApplication::sendEvent(&s.canvas, &im);
    CHECK(s.tool.plainText() == QStringLiteral("@été"));
    CHECK(s.canvas.inputMethodQuery(Qt::ImCursorRectangle).toRect().isValid());
    key(s.tool, s.canvas, Qt::Key_Escape);
    CHECK(!s.canvas.inputMethodQuery(Qt::ImEnabled).toBool());
    QKeyEvent so(QEvent::ShortcutOverride, Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    QApplication::sendEvent(&s.canvas, &so);
    CHECK(!so.isAccepted());
    // Delete while typing deletes a character, never the selected pixels.
    s.doc.selection().selectRect(QRect(0, 0, 400, 200));
    click(s.tool, s.canvas, QPointF(22, 30));
    key(s.tool, s.canvas, Qt::Key_Home, Qt::ControlModifier);
    QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QApplication::sendEvent(&s.canvas, &del);
    CHECK(s.tool.plainText() == QStringLiteral("été"));
    CHECK(s.doc.layerAt(0)->image().pixelColor(5, 5) == QColor(Qt::white));
}

static void mainWindowCommands() {
    MainWindow w;
    auto *canvas = w.findChild<CanvasWidget *>();
    Document *doc = canvas->document();
    CHECK(QMetaObject::invokeMethod(&w, "selectTool", Qt::DirectConnection, Q_ARG(ToolType, ToolType::Text)));
    auto *tool = static_cast<TextTool *>(canvas->currentTool());
    click(*tool, *canvas, QPointF(50, 50));
    type(*tool, *canvas, QStringLiteral("Menu"));
    // A primary color picked while typing colors the selection.
    tool->selectAll();
    doc->setPrimaryColor(QColor(0, 0, 255));
    QTextDocument d;
    tool->finishEditing();
    laidOut(doc->activeLayer(), d);
    QTextCursor c(&d);
    c.setPosition(2);
    CHECK(c.charFormat().foreground().color() == QColor(0, 0, 255));

    auto answer = [](const QString &button) {
        QTimer::singleShot(0, [button] {
            for (QWidget *top : QApplication::topLevelWidgets()) {
                auto *box = qobject_cast<QMessageBox *>(top);
                if (!box || box->objectName() != QStringLiteral("convertTextToPixels")) continue;
                for (QAbstractButton *b : box->buttons())
                    if (b->text() == button || (button == "Cancel" && box->buttonRole(b) == QMessageBox::RejectRole)) {
                        b->click();
                        return;
                    }
            }
        });
    };
    // An effect on a text layer asks first; Cancel leaves it text.
    answer(QStringLiteral("Cancel"));
    CHECK(!w.convertTextLayersFor({doc->activeLayerIndex()}));
    CHECK(doc->activeLayer()->isTextLayer());
    answer(QStringLiteral("Convert to Pixels"));
    CHECK(w.convertTextLayersFor({doc->activeLayerIndex()}));
    CHECK(!doc->activeLayer()->isTextLayer());
    CHECK(w.convertTextLayersFor({0}));   // no text layer: nothing to ask
    doc->setModified(false);
}

static void screenshot() {
    const QString dir = qEnvironmentVariable("PWM_SCREENSHOT_DIR");
    if (dir.isEmpty()) return;
    QDir().mkpath(dir);
    MainWindow w;
    w.resize(1400, 900);
    w.show();
    QApplication::processEvents();
    auto *canvas = w.findChild<CanvasWidget *>();
    QMetaObject::invokeMethod(&w, "selectTool", Qt::DirectConnection, Q_ARG(ToolType, ToolType::Text));
    auto *tool = static_cast<TextTool *>(canvas->currentTool());
    canvas->setZoom(1.0);
    const QPointF a(120, 120), b(560, 330);
    press(*tool, *canvas, a);
    move(*tool, *canvas, b);
    release(*tool, *canvas, b);
    tool->setFontSize(28);
    type(*tool, *canvas, QStringLiteral("Text frames stay editable until you turn them into pixels."));
    tool->select(0, 11);
    tool->setBold(true);
    canvas->document()->setPrimaryColor(QColor(200, 40, 40));
    tool->select(17, 26);
    tool->setItalic(true);
    tool->setUnderline(true);
    tool->select(30, 35);
    QApplication::processEvents();
    w.grab().save(dir + "/text-frame-editing.png");
    tool->finishEditing();
    canvas->document()->setModified(false);
}

int main(int argc, char **argv) {
    QTemporaryDir home;
    for (const char *name : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"}) {
        const QString path = home.path() + '/' + QString::fromLatin1(name);
        QDir().mkpath(path);
        qputenv(name, path.toUtf8());
    }
    QApplication app(argc, argv);
    createAndUndo();
    wrapAndAlign();
    formatting();
    editAgain();
    moveAndResizeFrame();
    paintingToolsStayOff();
    convertToPixels();
    imageOperations();
    psdRoundTrip();
    keysAndInputMethods();
    mainWindowCommands();
    screenshot();
    std::printf("text: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
