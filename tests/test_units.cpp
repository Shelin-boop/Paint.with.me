// Print sizes in mm, cm and inches with a resolution: the conversions, and
// Resize, Canvas Size and New Image giving the same pixels whatever order
// the fields are filled in. Headless.
//
// PWM_SCREENSHOT_DIR=<folder> saves pictures of the three dialogs.
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

#include "core/document.h"
#include "core/layer.h"
#include "core/units.h"
#include "dialogs/canvassizedialog.h"
#include "dialogs/newdocumentdialog.h"
#include "dialogs/resizedialog.h"
#include "dialogs/sizefields.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; \
    std::printf("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) <= eps; }

static void conversions() {
    using namespace Units;
    CHECK(near(toInches(25.4, Length::Millimeter), 1.0));
    CHECK(near(toInches(2.54, Length::Centimeter), 1.0));
    CHECK(near(fromInches(1.0, Length::Millimeter), 25.4));
    CHECK(near(toDpi(118.11, Resolution::PerCentimeter), 299.9994, 1e-3));
    CHECK(near(fromDpi(300, Resolution::PerCentimeter), 118.1102, 1e-3));
    // A4 at 300 dpi is 2480 x 3508 pixels (rounded, not cut off).
    CHECK(pixelsFor(210, Length::Millimeter, 300) == 2480);
    CHECK(pixelsFor(297, Length::Millimeter, 300) == 3508);
    CHECK(pixelsFor(10, Length::Centimeter, 300) == 1181);
    CHECK(pixelsFor(8.5, Length::Inch, 600) == 5100);
    CHECK(pixelsFor(0.0001, Length::Millimeter, 72) == 1);   // never zero
    CHECK(near(lengthFor(5000, 600, Length::Millimeter), 211.6667, 1e-3));
    CHECK(describe(2480, 3508, 300, Length::Millimeter)
          == QStringLiteral("2480 x 3508 pixels (209.97 x 297.01 mm at 300 dpi)"));
}

static void anyOrder() {
    // The owner's case: a 5000 x 5000 px image at 600 dpi, to be printed
    // 100 mm wide at 300 dpi.
    {   // print size first, then the resolution
        ResizeDialog dlg(5000, 5000, 600);
        SizeFields *f = dlg.sizeFields();
        f->setLengthUnit(Units::Length::Millimeter);
        f->setResolutionUnit(Units::Resolution::PerInch);
        CHECK(near(f->shownPrintWidth(), 211.67, 0.005));
        f->setPrintSize(QSizeF(100, 100), Units::Length::Millimeter);
        CHECK(f->keep() == SizeFields::Keep::PrintSize);
        f->setResolution(300, Units::Resolution::PerInch);
        CHECK(dlg.newWidth() == 1181 && dlg.newHeight() == 1181);
        CHECK(near(dlg.resolution(), 300));
        CHECK(near(f->shownPrintWidth(), 100.0));   // what was typed stays
    }
    {   // resolution first, then the print size
        ResizeDialog dlg(5000, 5000, 600);
        SizeFields *f = dlg.sizeFields();
        f->setResolution(300, Units::Resolution::PerInch);
        CHECK(dlg.newWidth() == 5000);              // pixels kept until a print size is typed
        CHECK(f->keep() == SizeFields::Keep::PixelSize);
        f->setPrintSize(QSizeF(100, 100), Units::Length::Millimeter);
        CHECK(dlg.newWidth() == 1181 && dlg.newHeight() == 1181);
    }
    {   // resolution in pixels per centimeter
        ResizeDialog dlg(5000, 5000, 600);
        SizeFields *f = dlg.sizeFields();
        f->setPrintSize(QSizeF(10, 10), Units::Length::Centimeter);
        f->setResolution(118.11, Units::Resolution::PerCentimeter);
        CHECK(dlg.newWidth() == 1181);
        CHECK(near(dlg.resolution(), 299.9994, 1e-3));
    }
    {   // both print sides in cm
        ResizeDialog dlg(4000, 2000, 300);
        SizeFields *f = dlg.sizeFields();
        f->setPrintSize(QSizeF(20, 10), Units::Length::Centimeter);
        CHECK(dlg.newWidth() == 2362 && dlg.newHeight() == 1181);
    }
    {   // percentage
        ResizeDialog dlg(800, 600, 96);
        dlg.sizeFields()->setPercent(50);
        CHECK(dlg.newWidth() == 400 && dlg.newHeight() == 300);
    }
    {   // a print size that would need too many pixels is refused
        ResizeDialog dlg(800, 600, 96);
        SizeFields *f = dlg.sizeFields();
        f->setPrintSize(QSizeF(10000, 10000), Units::Length::Millimeter);
        f->setResolution(1200, Units::Resolution::PerInch);
        CHECK(!f->isValid());
    }
    {   // changing only the unit shows the same size in the new unit
        ResizeDialog dlg(600, 300, 300);
        SizeFields *f = dlg.sizeFields();
        f->setLengthUnit(Units::Length::Inch);
        CHECK(near(f->shownPrintWidth(), 2.0));
        f->setLengthUnit(Units::Length::Millimeter);
        CHECK(near(f->shownPrintWidth(), 50.8));
        f->setResolutionUnit(Units::Resolution::PerCentimeter);
        CHECK(near(f->shownResolution(), 118.11, 0.005));
        CHECK(dlg.newWidth() == 600);
    }
    {   // decimal fields commit on Enter or focus-out only (issue #14)
        ResizeDialog dlg(800, 600, 96);
        int checked = 0;
        for (QDoubleSpinBox *sb : dlg.findChildren<QDoubleSpinBox *>()) {
            CHECK(!sb->keyboardTracking());
            ++checked;
        }
        CHECK(checked >= 4);
    }
}

static void canvasAndNew() {
    {   // an A4 canvas at 300 dpi, centered
        CanvasSizeDialog dlg(1000, 1000, 96);
        SizeFields *f = dlg.sizeFields();
        f->setResolution(300, Units::Resolution::PerInch);
        f->setPrintSize(QSizeF(210, 297), Units::Length::Millimeter);
        dlg.setAnchor(4);
        CHECK(dlg.newWidth() == 2480 && dlg.newHeight() == 3508);
        CHECK(dlg.anchorX() == 740 && dlg.anchorY() == 1254);
        CHECK(near(dlg.resolution(), 300));
    }
    {   // New: A4 preset, then 300 dpi
        NewDocumentDialog dlg;
        QComboBox *presets = dlg.presetCombo();
        const int a4 = presets->findText(QStringLiteral("A4 (210 x 297 mm)"));
        CHECK(a4 > 0);
        presets->setCurrentIndex(a4);
        dlg.sizeFields()->setResolution(300, Units::Resolution::PerInch);
        CHECK(dlg.imageWidth() == 2480 && dlg.imageHeight() == 3508);
        CHECK(presets->currentIndex() == a4);   // still A4, just at 300 dpi
        dlg.sizeFields()->swapWidthAndHeight();
        CHECK(presets->currentIndex() == 0);     // now "Custom"
        CHECK(dlg.imageWidth() == 3508 && dlg.imageHeight() == 2480);
        // A screen preset is in pixels; a new resolution leaves them alone.
        presets->setCurrentIndex(presets->findText(QStringLiteral("1920 x 1080 (Full HD)")));
        dlg.sizeFields()->setResolution(600, Units::Resolution::PerInch);
        CHECK(dlg.imageWidth() == 1920 && dlg.imageHeight() == 1080);
        CHECK(near(dlg.resolution(), 600));
    }
}

static void documentUndo() {
    // A new resolution is part of the undo step.
    Document doc(100, 50);
    doc.setDpi(96);
    doc.resize(200, 100, 300);
    CHECK(doc.width() == 200 && near(doc.dpi(), 300));
    doc.history().undo();
    CHECK(doc.width() == 100 && near(doc.dpi(), 96));
    doc.history().redo();
    CHECK(doc.width() == 200 && near(doc.dpi(), 300));

    // Only the resolution: one step, pixels untouched.
    const qint64 key = doc.layerAt(0)->image().cacheKey();
    doc.resize(200, 100, 600);
    CHECK(near(doc.dpi(), 600) && doc.width() == 200);
    CHECK(doc.layerAt(0)->image().cacheKey() == key);
    CHECK(doc.history().undoDescription() == QStringLiteral("Resolution"));
    doc.history().undo();
    CHECK(near(doc.dpi(), 300));

    doc.resizeCanvas(300, 150, 50, 25, 150);
    CHECK(doc.width() == 300 && near(doc.dpi(), 150));
    doc.history().undo();
    CHECK(doc.width() == 200 && near(doc.dpi(), 300));

    // A layer that keeps pixels outside the canvas moves with the anchor.
    Document d2(40, 40);
    QImage small(10, 10, QImage::Format_ARGB32_Premultiplied);
    small.fill(Qt::red);
    const int idx = d2.addLayer(QStringLiteral("kept"));
    d2.layerAt(idx)->setImage(small);
    d2.layerAt(idx)->setOffset(QPoint(-5, 30));
    d2.layerAt(idx)->setRetainsOffCanvasPixels(true);
    d2.resizeCanvas(60, 60, 10, 10);
    CHECK(d2.layerAt(idx)->offset() == QPoint(5, 40));
    CHECK(d2.layerAt(idx)->image().size() == QSize(10, 10));
}

static void screenshots() {
    const QString dir = qEnvironmentVariable("PWM_SCREENSHOT_DIR");
    if (dir.isEmpty()) return;
    QDir().mkpath(dir);
    {
        ResizeDialog dlg(5000, 5000, 600);
        dlg.sizeFields()->setLengthUnit(Units::Length::Millimeter);
        dlg.show();
        QApplication::processEvents();
        dlg.grab().save(dir + "/resize-dialog.png");
    }
    {
        CanvasSizeDialog dlg(2480, 3508, 300);
        dlg.sizeFields()->setLengthUnit(Units::Length::Millimeter);
        dlg.show();
        QApplication::processEvents();
        dlg.grab().save(dir + "/canvas-size-dialog.png");
    }
    {
        NewDocumentDialog dlg;
        dlg.presetCombo()->setCurrentIndex(dlg.presetCombo()->findText(QStringLiteral("A4 (210 x 297 mm)")));
        dlg.sizeFields()->setResolution(300, Units::Resolution::PerInch);
        dlg.show();
        QApplication::processEvents();
        dlg.grab().save(dir + "/new-dialog.png");
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
    conversions();
    anyOrder();
    canvasAndNew();
    documentUndo();
    screenshots();
    std::printf("units: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
