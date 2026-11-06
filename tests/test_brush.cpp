// Focused, deterministic replay. The frozen reference is deliberately slow.
#include <QApplication>
#include <QElapsedTimer>
#include <cstdio>
#include <cmath>
#include "core/document.h"
#include "canvas/canvaswidget.h"
#include "tools/brushtool.h"
#include "tools/erasertool.h"
#include "tools/penciltool.h"
#include "tools/recolortool.h"
#include "tools/clonestamptool.h"
#include "referencebrush.h"
#include "core/pensettings.h"
#include <QTabletEvent>
#include <QPointingDevice>
#include <QTemporaryDir>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { ++failures; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static void send(Tool &tool, CanvasWidget &canvas, QEvent::Type kind, QPointF p,
                 Qt::MouseButton button, Qt::MouseButtons buttons, bool viaWidget = false) {
    QMouseEvent e(kind, p, p, button, buttons, Qt::NoModifier);
    if (viaWidget) QApplication::sendEvent(&canvas, &e);
    else if (kind == QEvent::MouseButtonPress) tool.mousePressEvent(p, &e, canvas);
    else if (kind == QEvent::MouseMove) tool.mouseMoveEvent(p, &e, canvas);
    else tool.mouseReleaseEvent(p, &e, canvas);
}
static void differential() {
    for (int mode = 0; mode < 15; ++mode) for (int style = 0; style < 4; ++style) {
        Document a(192, 128), b(192, 128);
        QImage initial(a.size(), QImage::Format_ARGB32_Premultiplied);
        for (int y=0; y<initial.height(); ++y) for (int x=0; x<initial.width(); ++x)
            initial.setPixelColor(x,y,QColor((x*17)%256,(y*19)%256,(x+y)%256,(x*7+y*3)%256));
        a.activeLayer()->setImage(initial); b.activeLayer()->setImage(initial);
        CanvasWidget ca, cb; ca.setDocument(&a); cb.setDocument(&b);
        BrushTool actual; ReferenceBrushTool reference;
        for (Tool *t : {static_cast<Tool*>(&actual), static_cast<Tool*>(&reference)}) {
            t->setBrushSize(17.5); t->setHardness(style == 0 ? 100 : 43);
            t->setOpacity(47); t->setBlendMode(mode); t->setAntialiased(style != 2);
            t->setFillStyle(style == 3 ? 4 : 0); t->setSpacing(style == 2 ? 80 : 15);
        }
        for (Document *d : {&a,&b}) {
            d->setPrimaryColor(QColor(210,30,85,173)); d->setSecondaryColor(QColor(20,190,130,119));
            if (style > 0) d->selection().selectEllipse(QRect(4,3,175,115));
        }
        auto replay = [&](QEvent::Type type, QPointF p, Qt::MouseButton button, Qt::MouseButtons buttons) {
            send(actual,ca,type,p,button,buttons); send(reference,cb,type,p,button,buttons);
            if (a.activeLayer()->image() != b.activeLayer()->image()) {
                fprintf(stderr,"pixel mismatch mode=%d style=%d event=%d pos=%.1f,%.1f\n",mode,style,int(type),p.x(),p.y());
                CHECK(false);
            }
        };
        replay(QEvent::MouseButtonPress, {1.25,2.75}, Qt::LeftButton, Qt::LeftButton);
        for (int i=0;i<32;++i) {
            // Variable pressure, overlaps, edges and nonintegral coordinates.
            actual.setPressure((i%7)/6.0); reference.setPressure((i%7)/6.0);
            if (i==20) {
                actual.setOpacity(72); reference.setOpacity(72);
                actual.setBlendMode((mode+1)%15); reference.setBlendMode((mode+1)%15);
                a.selection().selectRect(QRect(20,20,70,65));
                b.selection().selectRect(QRect(20,20,70,65));
            }
            if (i==25) { a.selection().clear(); b.selection().clear(); }
            const QPointF p(85+90*std::cos(i*.31),62+63*std::sin(i*.31));
            if (i==12) replay(QEvent::MouseButtonPress,p,Qt::RightButton,Qt::LeftButton|Qt::RightButton);
            if (i==13) replay(QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::RightButton);
            replay(QEvent::MouseMove,p,Qt::NoButton,i<12 ? Qt::LeftButton : Qt::RightButton);
        }
        replay(QEvent::MouseButtonRelease,{60,60},Qt::RightButton,Qt::NoButton);
        const QImage final = a.activeLayer()->image().copy();
        a.history().undo(); CHECK(a.activeLayer()->image()==initial);
        a.history().redo(); CHECK(a.activeLayer()->image()==final);
    }
}

// ---- Pen extras: pressure curve, pressure on opacity, tilt ----
static int maxAlpha(const QImage &img) {
    int m = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) m = std::max(m, qAlpha(img.pixel(x, y)));
    return m;
}
static QRect paintedRect(const QImage &img, int threshold = 0) {
    QRect r;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(img.pixel(x, y)) > threshold) r |= QRect(x, y, 1, 1);
    return r;
}
// Draws a stroke with `tool` on a transparent 200 x 100 image and returns it.
static QImage penStroke(Tool &tool, bool stylus, double pressure, QPointF from, QPointF to,
                        double endPressure = -1) {
    Document doc(200, 100);
    doc.activeLayer()->clear(Qt::transparent);
    doc.setPrimaryColor(Qt::black);
    CanvasWidget canvas;
    canvas.setDocument(&doc);
    tool.setStylus(stylus);
    tool.setPressure(pressure);
    send(tool, canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    if (endPressure >= 0) tool.setPressure(endPressure);
    send(tool, canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
    send(tool, canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    return doc.activeLayer()->image().copy();
}
static void penExtras() {
    // The curve: straight at 0, soft lifts light pressure, firm lowers it.
    CHECK(std::fabs(Pen::mapPressure(0.5, 0) - 0.5) < 1e-9);
    CHECK(Pen::mapPressure(0.5, -100) > 0.8 && Pen::mapPressure(0.5, 100) < 0.1);
    CHECK(Pen::mapPressure(0.0, 60) == 0.0 && std::fabs(Pen::mapPressure(1.0, -60) - 1.0) < 1e-9);
    CHECK(std::fabs(Pen::curveExponent(50) - 2.0) < 1e-9);
    // Tilt: none = round; 60 deg to the right = half as wide across, along x.
    CHECK(Pen::tipForTilt(QPointF()).roundness == 1.0);
    const Pen::TipShape right = Pen::tipForTilt(QPointF(60, 0));
    CHECK(std::fabs(right.roundness - 0.5) < 1e-6 && std::fabs(right.angle) < 1e-6);
    CHECK(std::fabs(Pen::tipForTilt(QPointF(0, 45)).angle - 90.0) < 1e-6);

    // Pressure on opacity: a light pen stroke stays light, however many dabs
    // overlap, and keeps the full size.
    BrushTool brush;
    brush.setBrushSize(20);
    brush.setSpacing(5);   // many overlapping dabs
    brush.setPressureTarget(PressureTarget::Opacity);
    QImage light = penStroke(brush, true, 0.3, {30, 50}, {170, 50});
    CHECK(std::abs(maxAlpha(light) - 77) <= 2);
    CHECK(paintedRect(light, 0).height() >= 19);
    // The same with a mouse: full pressure, full opacity.
    CHECK(maxAlpha(penStroke(brush, false, 0.3, {30, 50}, {170, 50})) == 255);
    // Size and opacity together: thinner and lighter.
    brush.setPressureTarget(PressureTarget::SizeAndOpacity);
    QImage both = penStroke(brush, true, 0.5, {30, 50}, {170, 50});
    CHECK(std::abs(maxAlpha(both) - 128) <= 2 && paintedRect(both, 0).height() <= 13);
    // Size only (Paint.NET): full opacity, thinner.
    brush.setPressureTarget(PressureTarget::Size);
    QImage thin = penStroke(brush, true, 0.5, {30, 50}, {170, 50});
    CHECK(maxAlpha(thin) == 255 && paintedRect(thin, 0).height() <= 13);
    // Pressure goes smoothly along a segment: thin at the start, wide at the end.
    QImage swell = penStroke(brush, true, 0.2, {20, 50}, {180, 50}, 1.0);
    auto columnHeight = [](const QImage &img, int x) {
        int n = 0;
        for (int y = 0; y < img.height(); ++y) if (qAlpha(img.pixel(x, y)) > 128) ++n;
        return n;
    };
    CHECK(columnHeight(swell, 40) < columnHeight(swell, 100));
    CHECK(columnHeight(swell, 100) < columnHeight(swell, 170));

    // Tilt flattens the tip when chosen: a pen tilted to the right makes a dab
    // as wide as before along x but half as tall.
    Pen::settings().tiltFlattens = true;
    brush.setTilt(QPointF(60, 0));
    QImage dab = penStroke(brush, true, 1.0, {100, 50}, {100, 50});
    const QRect flat = paintedRect(dab, 128);
    CHECK(flat.width() >= 18 && flat.height() <= 12);
    // A mouse ignores tilt.
    const QRect mouseDab = paintedRect(penStroke(brush, false, 1.0, {100, 50}, {100, 50}), 128);
    CHECK(mouseDab.height() >= 18);
    Pen::settings().tiltFlattens = false;
    CHECK(paintedRect(penStroke(brush, true, 1.0, {100, 50}, {100, 50}), 128).height() >= 18);
    brush.setTilt(QPointF());

    // The eraser follows pressure on opacity too: half pressure leaves half alpha.
    {
        Document doc(200, 100);
        doc.activeLayer()->clear(Qt::black);
        CanvasWidget canvas;
        canvas.setDocument(&doc);
        EraserTool eraser;
        eraser.setBrushSize(20);
        eraser.setSpacing(5);
        eraser.setPressureTarget(PressureTarget::Opacity);
        eraser.setStylus(true);
        eraser.setPressure(0.5);
        send(eraser, canvas, QEvent::MouseButtonPress, {30, 50}, Qt::LeftButton, Qt::LeftButton);
        send(eraser, canvas, QEvent::MouseMove, {170, 50}, Qt::NoButton, Qt::LeftButton);
        send(eraser, canvas, QEvent::MouseButtonRelease, {170, 50}, Qt::LeftButton, Qt::NoButton);
        CHECK(std::abs(qAlpha(doc.activeLayer()->image().pixel(100, 50)) - 128) <= 2);
    }

    // The canvas applies the curve to the pen's pressure and passes the tilt on.
    {
        Document doc(200, 100);
        CanvasWidget canvas;
        canvas.resize(300, 200);
        canvas.setDocument(&doc);
        BrushTool tool;
        canvas.setCurrentTool(&tool);
        Pen::settings().pressureCurve = 50;   // exponent 2
        QTabletEvent move(QEvent::TabletMove, QPointingDevice::primaryPointingDevice(),
                          QPointF(50, 50), QPointF(50, 50), 0.5, 20.0f, -10.0f, 0.0f, 0.0, 0.0f,
                          Qt::NoModifier, Qt::NoButton, Qt::NoButton);
        QApplication::sendEvent(&canvas, &move);
        CHECK(std::fabs(tool.pressure() - 0.25) < 1e-6);
        CHECK(tool.stylus() && tool.tilt() == QPointF(20, -10));
        Pen::settings().pressureCurve = 0;
    }
}

static double benchmark(int w, int h, int mode, bool paint, bool selected = false) {
    Document doc(w,h); CanvasWidget canvas; BrushTool brush;
    canvas.resize(512,512); canvas.setDocument(&doc); canvas.setShowRulers(false);
    canvas.setCurrentTool(&brush); canvas.setZoom(1); canvas.setPan({0,0});
    brush.setBlendMode(mode); brush.setBrushSize(12); brush.setOpacity(60);
    if (selected) doc.selection().selectEllipse(QRect(0,0,w,h));
    QImage display(canvas.size(),QImage::Format_ARGB32_Premultiplied);
    if (paint) canvas.render(&display);
    send(brush,canvas,QEvent::MouseButtonPress,{350,250},Qt::LeftButton,Qt::LeftButton,paint);
    if (paint) canvas.render(&display);
    const uchar *storage=doc.activeLayer()->image().constBits();
    bool stable=true;
    QElapsedTimer timer; timer.start();
    for (int i=1;i<=128;++i) {
        QPointF p(250+100*std::cos(i*6.283185307179586/128),250+100*std::sin(i*6.283185307179586/128));
        send(brush,canvas,QEvent::MouseMove,p,Qt::NoButton,Qt::LeftButton,paint);
        stable &= storage==doc.activeLayer()->image().constBits();
        if (paint) canvas.render(&display);
    }
    const double milliseconds = timer.nsecsElapsed()/128.0/1e6;
    printf("replay %dx%d mode=%d paint=%d selected=%d: %.3f ms/move stable-storage=%d\n",w,h,mode,paint,selected,milliseconds,stable);
    // Deterministic regression: a local dab must not replace the full layer on every move.
    CHECK(stable);
    send(brush,canvas,QEvent::MouseButtonRelease,{350,250},Qt::LeftButton,Qt::NoButton,paint);
    return milliseconds;
}
static void renderDifferential() {
    // All layer modes (including custom per-pixel ones), offsets and tile edges.
    Document doc(192,128);
    QImage base(doc.size(),QImage::Format_ARGB32_Premultiplied);
    for (int y=0;y<base.height();++y) for (int x=0;x<base.width();++x)
        base.setPixelColor(x,y,QColor(x%256,y%256,170,(x+y)%256));
    doc.activeLayer()->setImage(base);
    QImage top=base.copy(0,0,119,83);
    doc.addLayer(top); doc.activeLayer()->setOpacity(.63f);
    for (auto mode : Layer::allBlendModes()) for (QPoint offset : {QPoint(13,-7),QPoint(-15,27)}) {
        doc.activeLayer()->setBlendMode(mode); doc.activeLayer()->setOffset(offset);
        const QImage flat=doc.flattenVisible();
        for (QRect tile : {QRect(0,0,30,20),QRect(20,15,90,75),QRect(105,70,80,53)})
            CHECK(doc.flattenVisible(tile)==flat.copy(tile));
    }
    // Real CanvasWidget dispatch and cache patches versus forced full flattening.
    // 4K case exercises fractional zoom, offset layers and disjoint/coalesced moves.
    Document large(3840,2160);
    large.activeLayer()->clear(QColor(30,60,90,120));
    large.addLayer(QImage(3840,2160,QImage::Format_ARGB32_Premultiplied));
    large.activeLayer()->clear(Qt::transparent);
    large.activeLayer()->setOffset({5,-3});
    large.activeLayer()->setBlendMode(BlendMode::Multiply);
    large.activeLayer()->setOpacity(.72f);
    CanvasWidget actual, forced;
    BrushTool brush; brush.setOpacity(43); brush.setHardness(40); brush.setBrushSize(31.5);
    for (CanvasWidget *c : {&actual,&forced}) {
        c->resize(480,320); c->setDocument(&large); c->setCurrentTool(&brush);
        c->setShowRulers(false); c->setZoom(.125); c->setPan({0,0});
    }
    QImage a(actual.size(),QImage::Format_ARGB32_Premultiplied), b=a.copy();
    actual.render(&a); forced.render(&b);
    auto compare = [&]() {
        actual.render(&a); forced.updateCanvas(); forced.render(&b); CHECK(a==b);
    };
    send(brush,actual,QEvent::MouseButtonPress,{80,80},Qt::LeftButton,Qt::LeftButton,true);
    compare();
    for (int i=0;i<12;++i) {
        send(brush,actual,QEvent::MouseMove,{80.3+i*3.2,80.7+i*2.1},Qt::NoButton,Qt::LeftButton,true);
        if (i%3==2) compare();
    }
    send(brush,actual,QEvent::MouseButtonRelease,{120,100},Qt::LeftButton,Qt::NoButton,true);
    compare(); large.history().undo(); compare(); large.history().redo(); compare();
    printf("regional layer/canvas comparisons complete\n");
}

static void largeDifferential() {
    for (int mode : {0,14}) {
        Document a(3840,2160), b(3840,2160);
        for (Document *doc : {&a,&b}) {
            doc->activeLayer()->clear(QColor(45,80,120,137));
            doc->setPrimaryColor(QColor(210,20,90,163));
            doc->selection().selectEllipse(QRect(800,400,2200,1400));
        }
        CanvasWidget ca,cb; ca.setDocument(&a); cb.setDocument(&b);
        BrushTool actual; ReferenceBrushTool reference;
        for (Tool *t : {static_cast<Tool*>(&actual),static_cast<Tool*>(&reference)}) {
            t->setBlendMode(mode); t->setBrushSize(33.5); t->setHardness(38); t->setOpacity(53);
        }
        for (int i=0;i<=16;++i) {
            const QPointF p(1900+900*std::cos(i*6.283185307179586/16),1100+600*std::sin(i*6.283185307179586/16));
            for (int side=0;side<2;++side)
                send(side ? static_cast<Tool&>(reference) : static_cast<Tool&>(actual),side ? cb : ca,
                     i ? QEvent::MouseMove : QEvent::MouseButtonPress,p,i ? Qt::NoButton : Qt::LeftButton,Qt::LeftButton);
            CHECK(a.activeLayer()->image()==b.activeLayer()->image());
        }
        send(actual,ca,QEvent::MouseButtonRelease,{2800,1100},Qt::LeftButton,Qt::NoButton);
        send(reference,cb,QEvent::MouseButtonRelease,{2800,1100},Qt::LeftButton,Qt::NoButton);
        CHECK(a.activeLayer()->image()==b.activeLayer()->image());
    }
    printf("fresh 4K versus v1.1.75 oracle comparisons complete\n");
}


// Eraser, pencil, recolor and clone stamp redraw only the area they changed.
// The canvas they update area by area must match one that combines the whole
// image again, at full size and zoomed out (reduced rendering).
static void regionalToolsDifferential() {
    for (double zoom : {1.0, 0.3, 0.12}) {
        Document doc(900, 600);
        doc.activeLayer()->clear(QColor(30, 60, 90, 200));
        QImage top(900, 600, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < 600; ++y) for (int x = 0; x < 900; ++x)
            top.setPixel(x, y, qPremultiply(qRgba(x % 256, y % 256, (x + y) % 256, (x * 3 + y) % 256)));
        doc.addLayer(top);
        doc.activeLayer()->setOffset({7, -4});
        doc.activeLayer()->setBlendMode(BlendMode::Multiply);
        doc.activeLayer()->setOpacity(.8f);
        doc.setPrimaryColor(QColor(200, 30, 60));
        EraserTool eraser; eraser.setBrushSize(41); eraser.setHardness(30); eraser.setOpacity(70);
        PencilTool pencil;
        RecolorTool recolor; recolor.setBrushSize(35);
        CloneStampTool clone; clone.setBrushSize(25);
        for (Tool *tool : {static_cast<Tool *>(&eraser), static_cast<Tool *>(&pencil),
                           static_cast<Tool *>(&recolor), static_cast<Tool *>(&clone)}) {
            CHECK(tool->updatesCanvasRegion());
            CanvasWidget actual, forced;
            for (CanvasWidget *c : {&actual, &forced}) {
                c->resize(500, 360); c->setDocument(&doc); c->setCurrentTool(tool);
                c->setShowRulers(false); c->setZoom(zoom); c->setPan({0, 0});
            }
            QImage a(actual.size(), QImage::Format_ARGB32_Premultiplied), b = a.copy();
            actual.render(&a); forced.render(&b);
            auto compare = [&]() {
                actual.render(&a); forced.updateCanvas(); forced.render(&b); CHECK(a == b);
            };
            if (tool == &clone) {   // Ctrl+click sets the clone source
                QMouseEvent e(QEvent::MouseButtonPress, QPointF(40, 40), QPointF(40, 40),
                              Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
                clone.mousePressEvent(actual.widgetToCanvas(QPointF(40, 40)), &e, actual);
                QMouseEvent r(QEvent::MouseButtonRelease, QPointF(40, 40), QPointF(40, 40),
                              Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
                clone.mouseReleaseEvent(actual.widgetToCanvas(QPointF(40, 40)), &r, actual);
            }
            send(*tool, actual, QEvent::MouseButtonPress, {90, 80}, Qt::LeftButton, Qt::LeftButton, true);
            compare();
            for (int i = 0; i < 9; ++i) {
                send(*tool, actual, QEvent::MouseMove, {90.0 + i * 17.3, 80.0 + i * 9.1}, Qt::NoButton, Qt::LeftButton, true);
                if (i % 3 == 2) compare();
            }
            send(*tool, actual, QEvent::MouseButtonRelease, {240, 160}, Qt::LeftButton, Qt::NoButton, true);
            compare();
            doc.history().undo(); compare();
            doc.history().redo(); compare();
        }
    }
    printf("eraser/pencil/recolor/clone regional comparisons complete\n");
}

int main(int argc,char **argv) {
    // Settings (pen settings) go to a temporary folder, not the real ~/.config.
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QApplication app(argc,argv);
    differential();
    penExtras();
    renderDifferential();
    largeDifferential();
    regionalToolsDifferential();
    for (int mode : {0,14}) for (bool paint : {false,true}) {
        const double small = benchmark(512,512,mode,paint);
        const double large = benchmark(3840,2160,mode,paint);
        // Generous noise allowance; baseline full-image work exceeds this by far.
        CHECK(large < small * 5 + 2);
    }
    const double selectedSmall = benchmark(512,512,0,true,true);
    const double selectedLarge = benchmark(3840,2160,0,true,true);
    CHECK(selectedLarge < selectedSmall * 5 + 2);
    printf("brush regression failures: %d\n",failures);
    return failures ? 1 : 0;
}
