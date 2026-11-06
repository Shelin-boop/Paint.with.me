// Main window: menus, the toolbar, the menu bar icons, keyboard shortcuts and
// the perspective guide commands.
// Split from mainwindow.cpp; one class, several files.
#include "mainwindow.h"
#include "toolicons.h"
#include "theme.h"
#include "core/adjustmentpreview.h"
#include "canvas/canvaswidget.h"
#include "panels/colorspanel.h"
#include "panels/tooloptionspanel.h"
#include "panels/imagelistbar.h"
#include "panels/toolspanel.h"
#include "core/perspective.h"
#include "tools/toolregistry.h"
#include "core/history.h"
#include "dialogs/settingsdialog.h"
#include "plugins/pluginmanager.h"
#include "plugins/plugineffect.h"
#include "tools/tool.h"
#include "tools/shapetool.h"
#include "tools/linetool.h"
#include "adjustments/levels.h"
#include "adjustments/curves.h"
#include "adjustments/sepia.h"
#include "adjustments/posterize.h"
#include "adjustments/threshold.h"
#include "adjustments/exposure.h"
#include <QToolBar>
#include <QDockWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QApplication>
#include <QUrl>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QToolButton>
#include <QSizePolicy>
#include <QSettings>
#include <QMenu>
#include <QDir>
#include "appinfo.h"
#include "desktopintegration.h"
#include "displayserver.h"
#include "dialogs/shortcutshelp.h"
#include "commandicons.h"
#include <QMenuBar>
#include <QInputDialog>
#include <QLocale>
#include <cmath>
#include "shortcuts.h"

void MainWindow::createMenus() {
    auto setShortcuts = [](QAction *action, std::initializer_list<QKeySequence> shortcuts) {
        action->setShortcuts(QList<QKeySequence>(shortcuts));
    };

    // File menu
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("&New..."), QKeySequence::New, this, &MainWindow::newDocument);
    fileMenu->addAction(QStringLiteral("&Open..."), QKeySequence::Open, this, &MainWindow::openDocument);
    auto *extractAction = fileMenu->addAction(QStringLiteral("Extract Picture from P&DF..."), this,
                                              [this]() { extractPictureFromPdf(); });
    extractAction->setToolTip(QStringLiteral(
        "Takes a picture out of a PDF: the first one is chosen, the others can be picked. "
        "Open it as an image, copy it to the clipboard, or add it as a layer."));
    m_recentFilesMenu = fileMenu->addMenu(QStringLiteral("&Recent files"));
    m_recentFiles = QSettings(AppInfo::kId, AppInfo::kId).value("recentFiles").toStringList();
    updateRecentFilesMenu();
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("&Save"), QKeySequence::Save, this, &MainWindow::saveDocument);
    fileMenu->addAction(QStringLiteral("Save &As..."), QKeySequence("Ctrl+Shift+S"), this, &MainWindow::saveDocumentAs);
    fileMenu->addAction(QStringLiteral("&Export as PDF..."), this, &MainWindow::exportPdf);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("&Print..."), QKeySequence::Print, this, &MainWindow::printDocument);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("&Close image"), QKeySequence("Ctrl+W"), this, &MainWindow::closeCurrentDocument);
    fileMenu->addAction(QStringLiteral("E&xit"), QKeySequence::Quit, this, &QMainWindow::close);

    // Edit menu
    auto *editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    m_undoAction = editMenu->addAction(QStringLiteral("&Undo"), QKeySequence::Undo, this, &MainWindow::undo);
    m_redoAction = editMenu->addAction(QStringLiteral("&Redo"), this, &MainWindow::redo);
    // paint.net redoes with Ctrl+Y; keep the platform default as an alias.
    setShortcuts(m_redoAction, {QKeySequence("Ctrl+Y"), QKeySequence::Redo});
    m_undoAction->setEnabled(false);
    m_redoAction->setEnabled(false);
    editMenu->addSeparator();
    auto *cutAction = editMenu->addAction(QStringLiteral("Cu&t"), QKeySequence::Cut, this, &MainWindow::cut);
    auto *copyAction = editMenu->addAction(QStringLiteral("&Copy"), QKeySequence::Copy, this, &MainWindow::copy);
    editMenu->addAction(QStringLiteral("Copy &Merged"), QKeySequence("Ctrl+Shift+C"), this, &MainWindow::copyMerged);
    auto *pasteAction = editMenu->addAction(QStringLiteral("&Paste"), QKeySequence::Paste, this, &MainWindow::paste);
    editMenu->addAction(QStringLiteral("Paste into &New Layer"), QKeySequence("Ctrl+Shift+V"), this, &MainWindow::pasteIntoNewLayer);
    editMenu->addAction(QStringLiteral("Paste into New &Image"), QKeySequence("Ctrl+Alt+V"), this, &MainWindow::pasteIntoNewImage);
    auto *deleteSelectionAction = editMenu->addAction(QStringLiteral("&Delete"), QKeySequence(Qt::Key_Delete), this, &MainWindow::deleteSelectionContents);
    deleteSelectionAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    auto *fillSelectionAction = editMenu->addAction(QStringLiteral("&Fill Selection"), QKeySequence(Qt::Key_Backspace), this, &MainWindow::fillSelection);
    fillSelectionAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    editMenu->addSeparator();
    auto *selectAllAction = editMenu->addAction(QStringLiteral("Select &All"), QKeySequence::SelectAll, this, &MainWindow::selectAll);
    auto *deselectAction = editMenu->addAction(QStringLiteral("&Deselect All"), QKeySequence("Ctrl+D"), this, &MainWindow::deselectAll);
    auto *invertSelectionAction = editMenu->addAction(QStringLiteral("&Invert Selection"), QKeySequence("Ctrl+I"), this, &MainWindow::invertSelection);
    auto *modifySelMenu = editMenu->addMenu(QStringLiteral("Modif&y Selection"));
    modifySelMenu->addAction(QStringLiteral("&Grow..."), this, &MainWindow::growSelection);
    modifySelMenu->addAction(QStringLiteral("Shr&ink..."), this, &MainWindow::shrinkSelection);
    modifySelMenu->addAction(QStringLiteral("&Feather..."), this, &MainWindow::featherSelection);
    setShortcuts(cutAction, {QKeySequence::Cut, QKeySequence("Shift+Delete")});
    setShortcuts(copyAction, {QKeySequence::Copy, QKeySequence("Ctrl+Insert")});
    setShortcuts(pasteAction, {QKeySequence::Paste, QKeySequence("Shift+Insert")});
    setShortcuts(selectAllAction, {QKeySequence::SelectAll});
    setShortcuts(deselectAction, {QKeySequence("Ctrl+D")});
    setShortcuts(invertSelectionAction, {QKeySequence("Ctrl+I")});

    // View menu (3rd, as in paint.net: File, Edit, View, Image, Layers, Adjustments, Effects)
    auto *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(QStringLiteral("Zoom &In"), QKeySequence::ZoomIn, this, &MainWindow::zoomIn);
    viewMenu->addAction(QStringLiteral("Zoom &Out"), QKeySequence::ZoomOut, this, &MainWindow::zoomOut);
    auto *zoomFitAction = viewMenu->addAction(QStringLiteral("&Best Fit"), QKeySequence("Ctrl+B"), this, &MainWindow::zoomToFit);
    viewMenu->addAction(QStringLiteral("Zoom to &Selection"), QKeySequence("Ctrl+Shift+B"), this, &MainWindow::zoomToSelection);
    auto *actualSizeAction = viewMenu->addAction(QStringLiteral("&Actual Size"), QKeySequence("Ctrl+0"), this, &MainWindow::zoomToActual);
    // Turning the paper: only the view turns, the image stays as it is.
    // Keys 4, 6 and 5 as in Krita; the step is chosen here or in Settings.
    auto *rotateViewMenu = viewMenu->addMenu(QStringLiteral("R&otate View"));
    auto *rotateLeft = rotateViewMenu->addAction(QStringLiteral("Rotate &Left"), QKeySequence("4"), this,
                                                 &MainWindow::rotateViewLeft);
    rotateLeft->setToolTip(QStringLiteral("Turns the view counterclockwise by the step. The image does not change."));
    auto *rotateRight = rotateViewMenu->addAction(QStringLiteral("Rotate &Right"), QKeySequence("6"), this,
                                                  &MainWindow::rotateViewRight);
    rotateRight->setToolTip(QStringLiteral("Turns the view clockwise by the step. The image does not change."));
    rotateViewMenu->addAction(QStringLiteral("R&eset Rotation"), QKeySequence("5"), this,
                              &MainWindow::resetViewRotation);
    rotateViewMenu->addSeparator();
    m_rotationStepGroup = new QActionGroup(this);
    m_rotationStepGroup->setExclusive(true);
    for (double step : {1.0, 5.0, 10.0, 15.0, 30.0, 45.0, 90.0}) {
        auto *a = rotateViewMenu->addAction(QStringLiteral("%1\u00B0 per Step").arg(step), this,
                                            [this, step]() { setRotationStep(step); updateRotationStepActions(); });
        a->setCheckable(true);
        a->setData(step);
        m_rotationStepGroup->addAction(a);
    }
    m_otherRotationStepAction = rotateViewMenu->addAction(QStringLiteral("Other Step..."), this, [this]() {
        bool ok = false;
        const double step = QInputDialog::getDouble(
            this, QStringLiteral("Rotation Step"), QStringLiteral("Degrees per step:"), rotationStep(),
            0.1, 180.0, 1, &ok);
        if (!ok) return;
        setRotationStep(step);
        updateRotationStepActions();
    });
    connect(rotateViewMenu, &QMenu::aboutToShow, this, &MainWindow::updateRotationStepActions);
    updateRotationStepActions();
    viewMenu->addSeparator();
    m_gridAction = viewMenu->addAction(QStringLiteral("&Pixel Grid"), this, &MainWindow::toggleGrid);
    m_gridAction->setCheckable(true);
    m_rulersAction = viewMenu->addAction(QStringLiteral("&Rulers"), this, [this]() {
        bool show = !m_canvas->showRulers();
        m_canvas->setShowRulers(show);
        m_rulersAction->setChecked(show);
    });
    // Units submenu (Pixels / Inches / Centimeters).
    auto *unitsMenu = viewMenu->addMenu(QStringLiteral("&Units"));
    auto *unitGroup = new QActionGroup(this);
    auto addUnit = [&](const QString &label, CanvasWidget::Unit u, bool checked) {
        auto *a = unitsMenu->addAction(label, this, [this, u]() { m_canvas->setUnit(u); });
        a->setCheckable(true);
        a->setChecked(checked);
        unitGroup->addAction(a);
    };
    addUnit(QStringLiteral("Pixels"), CanvasWidget::Unit::Pixels, true);
    addUnit(QStringLiteral("Inches"), CanvasWidget::Unit::Inches, false);
    addUnit(QStringLiteral("Centimeters"), CanvasWidget::Unit::Centimeters, false);
    m_rulersAction->setCheckable(true);
    m_rulersAction->setChecked(true);
    viewMenu->addSeparator();
    m_perspectiveGuidesAction = viewMenu->addAction(QStringLiteral("Perspective &Guides"), this, [this](bool on) {
        Perspective::settings().visible = on;
        perspectiveSettingsChanged();
    });
    m_perspectiveGuidesAction->setCheckable(true);
    m_perspectiveSnapAction = viewMenu->addAction(QStringLiteral("Snap to Perspecti&ve"), this, [this](bool on) {
        Perspective::settings().snap = on;
        perspectiveSettingsChanged();
    });
    m_perspectiveSnapAction->setCheckable(true);
    m_perspectiveGuidesAction->setChecked(Perspective::settings().visible);
    m_perspectiveSnapAction->setChecked(Perspective::settings().snap);
    viewMenu->addSeparator();
    viewMenu->addAction(QStringLiteral("Reset &Panel Layout"), this, &MainWindow::applyDefaultPanelLayout);
    setShortcuts(zoomFitAction, {QKeySequence("Ctrl+B")});
    setShortcuts(actualSizeAction, {QKeySequence("Ctrl+0"), QKeySequence("Ctrl+1")});

    // Image menu
    auto *imageMenu = menuBar()->addMenu(QStringLiteral("&Image"));
    imageMenu->addAction(QStringLiteral("&Resize..."), QKeySequence("Ctrl+R"), this, &MainWindow::resizeImage);
    imageMenu->addAction(QStringLiteral("&Canvas Size..."), QKeySequence("Ctrl+Shift+R"), this, &MainWindow::canvasSize);
    imageMenu->addSeparator();
    auto *rotateCwAction = imageMenu->addAction(QStringLiteral("Rotate 90° Clockwise"), this, &MainWindow::rotateClockwise);
    auto *rotateCcwAction = imageMenu->addAction(QStringLiteral("Rotate 90° Counter-Clockwise"), this, &MainWindow::rotateCounterClockwise);
    imageMenu->addAction(QStringLiteral("Rotate 180°"), this, &MainWindow::rotate180);
    imageMenu->addSeparator();
    imageMenu->addAction(QStringLiteral("Flip &Horizontal"), this, &MainWindow::flipHorizontal);
    imageMenu->addAction(QStringLiteral("Flip &Vertical"), this, &MainWindow::flipVertical);
    imageMenu->addSeparator();
    auto *cropAction = imageMenu->addAction(QStringLiteral("Crop to Selection"), this, &MainWindow::cropToSelection);
    auto *flattenAction = imageMenu->addAction(QStringLiteral("&Flatten"), this, &MainWindow::flattenImage);
    setShortcuts(rotateCwAction, {QKeySequence("Ctrl+H")});
    setShortcuts(rotateCcwAction, {QKeySequence("Ctrl+G")});
    setShortcuts(cropAction, {QKeySequence("Ctrl+Shift+X")});
    setShortcuts(flattenAction, {QKeySequence("Ctrl+Shift+F")});

    // Layers menu
    auto *layersMenu = menuBar()->addMenu(QStringLiteral("&Layers"));
    layersMenu->addAction(QStringLiteral("Add New Layer"), QKeySequence("Ctrl+Shift+N"), this, [this]() { m_document->addLayer(); });
    auto *deleteLayerAction = layersMenu->addAction(QStringLiteral("Delete Layer"), this, [this]() { m_document->removeLayer(m_document->activeLayerIndex()); });
    layersMenu->addAction(QStringLiteral("Duplicate Layer"), QKeySequence("Ctrl+Shift+D"), this, [this]() { m_document->duplicateLayer(m_document->activeLayerIndex()); });
    layersMenu->addAction(QStringLiteral("Merge Layer Down"), QKeySequence("Ctrl+M"), this, [this]() { m_document->mergeLayerDown(m_document->activeLayerIndex()); });
    layersMenu->addAction(QStringLiteral("Import from File..."), this, &MainWindow::importLayerFromFile);
    auto *convertTextAction = layersMenu->addAction(QStringLiteral("Convert Text to Pixels"), this,
                                                    &MainWindow::convertActiveTextToPixels);
    convertTextAction->setToolTip(QStringLiteral(
        "Turns the selected text layer into an ordinary layer you can paint on. "
        "Its text can then no longer be edited."));
    connect(layersMenu, &QMenu::aboutToShow, this, [this, convertTextAction]() {
        const Layer *layer = m_document ? m_document->activeLayer() : nullptr;
        convertTextAction->setEnabled(layer && layer->isTextLayer());
    });
    layersMenu->addSeparator();
    layersMenu->addAction(QStringLiteral("Move Layer Up"), QKeySequence("Ctrl+Shift+Up"), this, &MainWindow::moveActiveLayerUp);
    layersMenu->addAction(QStringLiteral("Move Layer Down"), QKeySequence("Ctrl+Shift+Down"), this, &MainWindow::moveActiveLayerDown);
    auto *flipLayerMenu = layersMenu->addMenu(QStringLiteral("Flip Layer"));
    flipLayerMenu->addAction(QStringLiteral("&Horizontal"), this, &MainWindow::flipActiveLayerHorizontal);
    flipLayerMenu->addAction(QStringLiteral("&Vertical"), this, &MainWindow::flipActiveLayerVertical);
    layersMenu->addAction(QStringLiteral("Rotate / Zoom Layer..."), this, &MainWindow::showRotateZoomLayerDialog);
    layersMenu->addSeparator();
    auto *layerPropertiesAction = layersMenu->addAction(QStringLiteral("Layer &Properties..."), this, &MainWindow::showLayerPropertiesDialog);
    auto *toggleLayerVisibilityAction = layersMenu->addAction(QStringLiteral("Toggle Visibility"), this, &MainWindow::toggleActiveLayerVisibility);
    setShortcuts(deleteLayerAction, {QKeySequence("Ctrl+Shift+Delete")});
    setShortcuts(layerPropertiesAction, {QKeySequence("F4")});
    setShortcuts(toggleLayerVisibilityAction, {QKeySequence("Ctrl+,")});

    // Adjustments menu
    auto *adjMenu = menuBar()->addMenu(QStringLiteral("&Adjustments"));
    auto *autoLevelAction = adjMenu->addAction(QStringLiteral("Auto-Level"), this, [this]() { applyAdjustment(9); });
    adjMenu->addSeparator();
    auto *brightnessAction = adjMenu->addAction(QStringLiteral("Brightness / Contrast..."), this, [this]() { applyAdjustment(0); });
    auto *hueSatAction = adjMenu->addAction(QStringLiteral("Hue / Saturation..."), this, [this]() { applyAdjustment(1); });
    auto *levelsAction = adjMenu->addAction(QStringLiteral("Levels..."), this, [this]() { applyAdjustment(2); });
    auto *curvesAction = adjMenu->addAction(QStringLiteral("Curves..."), this, [this]() { applyAdjustment(3); });
    adjMenu->addSeparator();
    auto *invertColorsAction = adjMenu->addAction(QStringLiteral("Invert Colors"), this, [this]() { applyAdjustment(4); });
    auto *sepiaAction = adjMenu->addAction(QStringLiteral("Sepia..."), this, [this]() { applyAdjustment(5); });
    auto *posterizeAction = adjMenu->addAction(QStringLiteral("Posterize..."), this, [this]() { applyAdjustment(6); });
    auto *blackWhiteAction = adjMenu->addAction(QStringLiteral("Black and White"), this, [this]() { applyAdjustment(7); });
    adjMenu->addAction(QStringLiteral("Threshold..."), this, [this]() { applyAdjustment(14); });
    adjMenu->addAction(QStringLiteral("Color Balance..."), this, [this]() { applyAdjustment(8); });
    adjMenu->addSeparator();
    adjMenu->addAction(QStringLiteral("Exposure..."), this, [this]() { applyAdjustment(10); });
    adjMenu->addAction(QStringLiteral("Highlights / Shadows..."), this, [this]() { applyAdjustment(11); });
    adjMenu->addAction(QStringLiteral("Temperature / Tint..."), this, [this]() { applyAdjustment(12); });
    adjMenu->addAction(QStringLiteral("Invert Alpha"), this, [this]() { applyAdjustment(13); });
    setShortcuts(autoLevelAction, {QKeySequence("Ctrl+Shift+L")});
    setShortcuts(brightnessAction, {QKeySequence("Ctrl+Shift+T")});
    setShortcuts(hueSatAction, {QKeySequence("Ctrl+Shift+U")});
    setShortcuts(levelsAction, {QKeySequence("Ctrl+L")});
    setShortcuts(curvesAction, {QKeySequence("Ctrl+Shift+M")});
    setShortcuts(invertColorsAction, {QKeySequence("Ctrl+Shift+I")});
    setShortcuts(sepiaAction, {QKeySequence("Ctrl+Shift+E")});
    setShortcuts(posterizeAction, {QKeySequence("Ctrl+Shift+P")});
    setShortcuts(blackWhiteAction, {QKeySequence("Ctrl+Shift+G")});

    // Effects menu
    auto *fxMenu = menuBar()->addMenu(QStringLiteral("Effe&cts"));

    m_repeatEffectAction = fxMenu->addAction(QStringLiteral("Repeat last effect"), this, [this]() {
        if (m_lastEffect) m_lastEffect();
    });
    m_repeatEffectAction->setShortcut(QKeySequence("Ctrl+F"));
    m_repeatEffectAction->setEnabled(false);
    fxMenu->addSeparator();

    auto *artisticMenu = fxMenu->addMenu(QStringLiteral("Artistic"));
    artisticMenu->addAction(QStringLiteral("Ink Sketch..."), this, [this]() { applyEffect(8); });
    artisticMenu->addAction(QStringLiteral("Oil Painting..."), this, [this]() { applyEffect(5); });
    artisticMenu->addAction(QStringLiteral("Pencil Sketch..."), this, [this]() { applyEffect(9); });

    auto *blursMenu = fxMenu->addMenu(QStringLiteral("Blurs"));
    blursMenu->addAction(QStringLiteral("Fragment..."), this, [this]() { applyEffect(17); });
    blursMenu->addAction(QStringLiteral("Gaussian Blur..."), this, [this]() { applyEffect(0); });
    blursMenu->addAction(QStringLiteral("Motion Blur..."), this, [this]() { applyEffect(7); });
    blursMenu->addAction(QStringLiteral("Radial Blur..."), this, [this]() { applyEffect(13); });
    blursMenu->addAction(QStringLiteral("Surface Blur..."), this, [this]() { applyEffect(15); });
    blursMenu->addAction(QStringLiteral("Unfocus..."), this, [this]() { applyEffect(16); });
    blursMenu->addAction(QStringLiteral("Zoom Blur..."), this, [this]() { applyEffect(14); });

    auto *distortMenu = fxMenu->addMenu(QStringLiteral("Distort"));
    distortMenu->addAction(QStringLiteral("Bulge..."), this, [this]() { applyEffect(18); });
    distortMenu->addAction(QStringLiteral("Crystalize..."), this, [this]() { applyEffect(20); });
    distortMenu->addAction(QStringLiteral("Dents..."), this, [this]() { applyEffect(22); });
    distortMenu->addAction(QStringLiteral("Frosted Glass..."), this, [this]() { applyEffect(19); });
    distortMenu->addAction(QStringLiteral("Pixelate..."), this, [this]() { applyEffect(6); });
    distortMenu->addAction(QStringLiteral("Polar Inversion..."), this, [this]() { applyEffect(23); });
    distortMenu->addAction(QStringLiteral("Tile Reflection..."), this, [this]() { applyEffect(21); });
    distortMenu->addAction(QStringLiteral("Twist..."), this, [this]() { applyEffect(24); });

    auto *noiseMenu = fxMenu->addMenu(QStringLiteral("Noise"));
    noiseMenu->addAction(QStringLiteral("Add Noise..."), this, [this]() { applyEffect(2); });
    noiseMenu->addAction(QStringLiteral("Median..."), this, [this]() { applyEffect(25); });
    noiseMenu->addAction(QStringLiteral("Reduce Palette..."), this, [this]() { applyEffect(27); });
    noiseMenu->addAction(QStringLiteral("Reduce Noise..."), this, [this]() { applyEffect(26); });

    auto *objectMenu = fxMenu->addMenu(QStringLiteral("Object"));
    objectMenu->addAction(QStringLiteral("Drop Shadow..."), this, [this]() { applyEffect(36); });

    auto *photoMenu = fxMenu->addMenu(QStringLiteral("Photo"));
    photoMenu->addAction(QStringLiteral("Glow..."), this, [this]() { applyEffect(28); });
    photoMenu->addAction(QStringLiteral("Red Eye Removal..."), this, [this]() { applyEffect(29); });
    photoMenu->addAction(QStringLiteral("Sharpen..."), this, [this]() { applyEffect(1); });
    photoMenu->addAction(QStringLiteral("Soften Portrait..."), this, [this]() { applyEffect(30); });
    photoMenu->addAction(QStringLiteral("Vignette..."), this, [this]() { applyEffect(31); });

    auto *renderMenu = fxMenu->addMenu(QStringLiteral("Render"));
    renderMenu->addAction(QStringLiteral("Clouds..."), this, [this]() { applyEffect(10); });
    renderMenu->addAction(QStringLiteral("Julia Fractal..."), this, [this]() { applyEffect(11); });
    renderMenu->addAction(QStringLiteral("Mandelbrot Fractal..."), this, [this]() { applyEffect(12); });
    renderMenu->addAction(QStringLiteral("Turbulence..."), this, [this]() { applyEffect(32); });

    auto *stylizeMenu = fxMenu->addMenu(QStringLiteral("Stylize"));
    stylizeMenu->addAction(QStringLiteral("Edge Detect"), this, [this]() { applyEffect(4); });
    stylizeMenu->addAction(QStringLiteral("Emboss..."), this, [this]() { applyEffect(3); });
    stylizeMenu->addAction(QStringLiteral("Morphology..."), this, [this]() { applyEffect(35); });
    stylizeMenu->addAction(QStringLiteral("Outline..."), this, [this]() { applyEffect(34); });
    stylizeMenu->addAction(QStringLiteral("Relief..."), this, [this]() { applyEffect(33); });

    // External effect plugins (Effects > Plugins). Entirely additive — the submenu is
    // always present so the feature is discoverable, and lists whatever loaded.
    fxMenu->addSeparator();
    auto *pluginsMenu = fxMenu->addMenu(QStringLiteral("Plugins"));
    populatePluginsMenu(pluginsMenu);

    // Icons for the Image, Layers, Adjustments and Effects commands (also
    // used when they are pinned to the Tools panel).
    m_iconMenus = {imageMenu, layersMenu, adjMenu, fxMenu};
    CommandIcons::decorate(m_iconMenus);

    // Every menu command's keys can be changed (Settings > Keyboard); the
    // keys given above are the defaults.
    m_shortcuts->addMenuBar(menuBar(), {m_recentFilesMenu});

    // paint.net has no Window/Help menus: the four utility windows plus Settings
    // and Help live as six icons on the RIGHT side of the menu bar.
    createMenuBarCornerIcons();
}

void MainWindow::populatePluginsMenu(QMenu *pluginsMenu) {
    if (!pluginsMenu) return;

    if (!m_pluginManager) {
        m_pluginManager = std::make_unique<PluginManager>();
        m_pluginManager->loadFrom(PluginManager::defaultPluginDirs());
    }

    const auto &effects = m_pluginManager->effects();
    if (effects.empty()) {
        QAction *none = pluginsMenu->addAction(QStringLiteral("No plugin loaded"));
        none->setEnabled(false);
    } else {
        for (const auto &eff : effects) {
            PluginEffect *raw = eff.get();
            pluginsMenu->addAction(eff->name(), this, [this, raw]() { runPluginEffect(raw); });
        }
    }

    pluginsMenu->addSeparator();
    pluginsMenu->addAction(QStringLiteral("Open the plugins folder..."), this, [this]() {
        const QStringList dirs = PluginManager::defaultPluginDirs();
        if (dirs.isEmpty()) return;
        const QString target = dirs.last();   // the writable app-data plugins dir
        QDir().mkpath(target);
        QDesktopServices::openUrl(QUrl::fromLocalFile(target));
    });
}

void MainWindow::showShortcutsHelp() {
    const QString html = ShortcutsHelp::html(m_shortcuts);

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Keyboard shortcuts"));
    box.setTextFormat(Qt::RichText);
    box.setText(html);
    box.exec();
}

void MainWindow::createMenuBarCornerIcons() {
    auto *corner = new QWidget(this);
    auto *cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(0, 0, 6, 0);
    cornerLayout->setSpacing(2);

    // Helper: a checkable toggle button bound to a dock, with an F-key shortcut.
    // The tooltip spells out what the window is for and how to reset it.
    auto addDockToggle = [&](QDockWidget *dock, const QIcon &icon, const QString &label,
                             const QString &key, const QString &what) {
        auto *btn = new QToolButton(corner);
        btn->setIcon(icon);
        btn->setIconSize(QSize(16, 16));
        btn->setCheckable(true);
        btn->setChecked(dock->isVisible());
        btn->setAutoRaise(true);
        // "Active" state = a blue underline, NOT a filled blue background: the global
        // QToolButton:checked fill hid the blue Tools/History icons (blue on blue) in
        // dark mode. An underline keeps every icon fully visible in both themes.
        btn->setStyleSheet(
            "QToolButton { background: transparent; border: none;"
            " border-bottom: 2px solid transparent; border-radius: 3px; padding: 2px 2px 0 2px; }"
            "QToolButton:hover { background: rgba(127,127,127,0.28); }"
            "QToolButton:checked { background: transparent; border-bottom: 2px solid #4a90d9; }");
        connect(btn, &QToolButton::clicked, this, [dock](bool on) { dock->setVisible(on); });
        connect(dock, &QDockWidget::visibilityChanged, btn, &QToolButton::setChecked);

        // F5-F8 toggle; Ctrl+Shift+F* resets position (like paint.net).
        // Both can be changed in Settings > Keyboard.
        auto *toggle = new QAction((label), this);
        connect(toggle, &QAction::triggered, this, [dock]() { dock->setVisible(!dock->isVisible()); });
        addAction(toggle);

        auto *reset = new QAction("Reset " + label, this);
        connect(reset, &QAction::triggered, this, [this, dock]() { resetUtilityWindow(dock); });
        addAction(reset);

        const QString id = QStringLiteral("panels.") + label.toLower();
        m_shortcuts->add({id, QStringLiteral("Panels"), QStringLiteral("Show or Hide ") + label,
                          {QKeySequence(key)}, Shortcuts::Kind::Command, toggle, what});
        m_shortcuts->add({id + QStringLiteral(".reset"), QStringLiteral("Panels"),
                          QStringLiteral("Put ") + label + QStringLiteral(" Back in Its Place"),
                          {QKeySequence(QStringLiteral("Ctrl+Shift+") + key)}, Shortcuts::Kind::Command, reset,
                          QString()});
        auto updateTip = [this, btn, label, what, id]() {
            const QString keys = Shortcuts::text(m_shortcuts->keys(id));
            const QString resetKeys = Shortcuts::text(m_shortcuts->keys(id + QStringLiteral(".reset")));
            QString tip = QStringLiteral("<b>%1</b>").arg(label);
            if (!keys.isEmpty()) tip += QStringLiteral(" (%1)").arg(keys);
            tip += QStringLiteral("<br>") + what;
            if (!resetKeys.isEmpty())
                tip += QStringLiteral("<br><i>%1: reset the window position</i>").arg(resetKeys);
            btn->setToolTip(tip);
        };
        updateTip();
        m_shortcutTooltipUpdates << updateTip;

        cornerLayout->addWidget(btn);
        return btn;
    };

    addDockToggle(m_toolsDock, ToolIcons::toolsWindow(), "Tools", "F5",
                  "Shows or hides the Tools palette (brush, selection, shapes...).");
    addDockToggle(m_historyDock, ToolIcons::historyWindow(), "History", "F6",
                  "Shows or hides History: every action is listed; click one to go back to it.");
    addDockToggle(m_layersDock, ToolIcons::layersWindow(), "Layers", "F7",
                  "Shows or hides Layers: add, delete, reorder, opacity and blend mode.");
    addDockToggle(m_colorsDock, ToolIcons::colorsWindow(), "Colors", "F8",
                  "Shows or hides Colors: primary/secondary color, wheel, RGB/HSV and palette.");

    // Settings
    auto *settingsBtn = new QToolButton(corner);
    settingsBtn->setIcon(ToolIcons::settings());
    settingsBtn->setIconSize(QSize(16, 16));
    settingsBtn->setAutoRaise(true);
    settingsBtn->setToolTip(QString("<b>%1</b><br>%2")
        .arg(QStringLiteral("Settings"),
             QStringLiteral("Opens Settings: light or dark color scheme, panels and canvas.")));
    connect(settingsBtn, &QToolButton::clicked, this, &MainWindow::showSettingsDialog);
    cornerLayout->addWidget(settingsBtn);

    // Help
    auto *helpBtn = new QToolButton(corner);
    helpBtn->setIcon(ToolIcons::help());
    helpBtn->setIconSize(QSize(16, 16));
    helpBtn->setAutoRaise(true);
    helpBtn->setToolTip(QString("<b>%1</b><br>%2")
        .arg(QStringLiteral("Help"),
             QStringLiteral("Keyboard shortcuts, documentation and version information.")));
    helpBtn->setPopupMode(QToolButton::InstantPopup);
    auto *helpMenu = new QMenu(helpBtn);
    helpMenu->addAction(QStringLiteral("Keyboard shortcuts"), this, &MainWindow::showShortcutsHelp);
    addAppMenuEntryAction(helpMenu);
    helpMenu->addSeparator();
    helpMenu->addAction(QStringLiteral("&About Paint.with.me"), this, [this]() {
        QMessageBox::about(this, QStringLiteral("About Paint.with.me"),
            QStringLiteral("<h2>%1</h2>"
                           "<p>Version %2, running on %4</p>"
                           "<p>A free image editor for Linux in the style of Paint.NET, built with Qt and C++.</p>"
                           "<p>Based on <a href=\"https://github.com/Univers4craft/paint.software\">paint.software</a> "
                           "by Univers4craft and contributors. MIT license.</p>"
                           "<p><a href=\"%3\">%3</a></p>"
                           "<p>Not affiliated with Paint.NET or dotPDN LLC.</p>")
                .arg(AppInfo::kDisplayName, QStringLiteral(PWM_VERSION_STR), AppInfo::kHomepage,
                     DisplayServer::describe(QGuiApplication::platformName(),
                                             !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))));
    });
    helpBtn->setMenu(helpMenu);
    cornerLayout->addWidget(helpBtn);

    menuBar()->setCornerWidget(corner, Qt::TopRightCorner);
}

void MainWindow::addAppMenuEntryAction(QMenu *helpMenu) {
    // Only an AppImage manages its own menu entry; a .deb has a system one.
    if (!DesktopIntegration::Environment::fromSystem().isAppImage()) return;
    auto *action = helpMenu->addAction(QString());
    auto refresh = [action]() {
        const auto st = DesktopIntegration::state(DesktopIntegration::Environment::fromSystem());
        const bool present = st == DesktopIntegration::State::Current
                             || st == DesktopIntegration::State::Outdated;
        action->setText(present ? QStringLiteral("Remove from application menu")
                                : QStringLiteral("Add to application menu"));
        action->setEnabled(st != DesktopIntegration::State::Foreign);
    };
    refresh();
    connect(helpMenu, &QMenu::aboutToShow, action, refresh);
    connect(action, &QAction::triggered, this, [this]() {
        const auto env = DesktopIntegration::Environment::fromSystem();
        const auto st = DesktopIntegration::state(env);
        QString error;
        if (st == DesktopIntegration::State::Current || st == DesktopIntegration::State::Outdated) {
            if (DesktopIntegration::uninstall(env, &error)) {
                DesktopIntegration::setUserRemovedEntry(true);
                statusBar()->showMessage(QStringLiteral("Removed from the application menu."), 8000);
            } else {
                QMessageBox::warning(this, AppInfo::kDisplayName, error);
            }
        } else if (DesktopIntegration::install(env, &error)) {
            DesktopIntegration::setUserRemovedEntry(false);
            statusBar()->showMessage(QStringLiteral("Added to the application menu."), 8000);
        } else {
            QMessageBox::warning(this, AppInfo::kDisplayName, error);
        }
    });
}

QAction *MainWindow::addShortcutAction(const QString &text, const QList<QKeySequence> &shortcuts,
                                       const std::function<void()> &handler,
                                       Qt::ShortcutContext context) {
    auto *action = new QAction(text, this);
    action->setShortcuts(shortcuts);
    action->setShortcutContext(context);
    connect(action, &QAction::triggered, this, [handler]() { handler(); });
    addAction(action);
    return action;
}

bool MainWindow::allowSingleKeyShortcuts() const {
    QWidget *focus = QApplication::focusWidget();
    if (!focus) return true;
    // Suppress letter shortcuts only while a tool is actually capturing keystrokes
    // (the Text tool mid-typing), not merely because it is selected — otherwise B
    // could never switch away from an idle Text tool.
    if (focus == m_canvas && m_currentTool && m_currentTool->wantsKeyInput()) return false;
    return !focus->inherits("QLineEdit")
        && !focus->inherits("QTextEdit")
        && !focus->inherits("QPlainTextEdit")
        && !focus->inherits("QAbstractSpinBox")
        && !focus->inherits("QComboBox");
}

void MainWindow::cycleToolGroup(const std::vector<ToolType> &types, bool reverse) {
    if (!allowSingleKeyShortcuts() || types.empty()) return;

    int currentIndex = -1;
    for (size_t i = 0; i < types.size(); ++i) {
        if (m_currentTool && m_currentTool->type() == types[i]) {
            currentIndex = static_cast<int>(i);
            break;
        }
    }

    int nextIndex = 0;
    if (currentIndex >= 0) {
        int delta = reverse ? -1 : 1;
        nextIndex = (currentIndex + delta + static_cast<int>(types.size())) % static_cast<int>(types.size());
    } else if (reverse) {
        nextIndex = static_cast<int>(types.size()) - 1;
    }

    selectTool(types[nextIndex]);
}

namespace {
QString toolShortcutId(const ToolInfo &info) {
    return QStringLiteral("tool.") + QString::fromLatin1(info.key);
}
}  // namespace

void MainWindow::createKeyboardShortcuts() {
    // Commands without a menu entry. Each is listed in Settings > Keyboard;
    // the keys here are the defaults.
    auto command = [this](const QString &id, const QString &category, const QString &label,
                          const QList<QKeySequence> &keys, const std::function<void()> &handler,
                          const QString &hint = QString()) {
        QAction *action = addShortcutAction(label, keys, handler);
        m_shortcuts->add({id, category, label, keys, Shortcuts::Kind::Command, action, hint});
    };
    // Letters and digits do nothing while typing into a field.
    auto singleKey = [this](const std::function<void()> &handler) {
        return [this, handler]() { if (allowSingleKeyShortcuts()) handler(); };
    };

    command(QStringLiteral("edit.deselectEnter"), QStringLiteral("Edit"), QStringLiteral("Deselect (Extra Keys)"),
            {QKeySequence("Return"), QKeySequence("Enter")}, [this]() {
                if (!m_document || !m_document->selection().hasSelection()) return;
                if (m_currentTool && m_currentTool->type() == ToolType::Text) return;
                deselectAll();
            }, QStringLiteral("Keys that finish a selection, besides Edit > Deselect All."));

    // Tools: one entry each, with the registry's key as default. Tools that
    // end up with the same key take turns (applyShortcutKeys()).
    for (const ToolInfo &info : toolInfos()) {
        QList<QKeySequence> keys;
        if (info.shortcut && *info.shortcut) keys << QKeySequence(QString::fromLatin1(info.shortcut));
        m_shortcuts->add({toolShortcutId(info), QStringLiteral("Tools"), QString::fromLatin1(info.name), keys,
                          Shortcuts::Kind::Tool, nullptr,
                          QStringLiteral("Tools with the same key take turns: press it again for the next one, "
                                         "with Shift for the one before.")});
    }

    const QString painting = QStringLiteral("Painting");
    command(QStringLiteral("painting.lastTool"), painting, QStringLiteral("Last Used Painting Tool"),
            {QKeySequence("N")}, singleKey([this]() { selectLastPaintTool(); }),
            QStringLiteral("Back to the Paintbrush, Pencil, Smudge, Blur Brush, Clone Stamp or Recolor tool "
                           "used last, for example after the Eraser or the Color Picker."));
    command(QStringLiteral("painting.eraserToggle"), painting, QStringLiteral("Eraser, Again for the Last Painting Tool"),
            {QKeySequence("Shift+E")}, singleKey([this]() { toggleEraser(); }),
            QStringLiteral("Switches between the Eraser and the painting tool used last."));
    command(QStringLiteral("painting.sizeDown"), painting, QStringLiteral("Brush Width Smaller"),
            {QKeySequence("[")}, [this]() { adjustCurrentToolBrushSize(-1); });
    command(QStringLiteral("painting.sizeUp"), painting, QStringLiteral("Brush Width Bigger"),
            {QKeySequence("]")}, [this]() { adjustCurrentToolBrushSize(1); });
    command(QStringLiteral("painting.sizeDownFast"), painting, QStringLiteral("Brush Width Smaller, Big Steps"),
            {QKeySequence("Ctrl+[")}, [this]() { adjustCurrentToolBrushSize(-5); });
    command(QStringLiteral("painting.sizeUpFast"), painting, QStringLiteral("Brush Width Bigger, Big Steps"),
            {QKeySequence("Ctrl+]")}, [this]() { adjustCurrentToolBrushSize(5); });
    command(QStringLiteral("colors.swap"), painting, QStringLiteral("Swap Primary and Secondary Color"),
            {QKeySequence("X")}, singleKey([this]() {
                if (m_colorsPanel) m_colorsPanel->swapPrimaryAndSecondaryColors();
            }));
    command(QStringLiteral("colors.toggle"), painting, QStringLiteral("Switch Which Color the Palette Edits"),
            {QKeySequence("C")}, singleKey([this]() {
                if (m_colorsPanel) m_colorsPanel->toggleActiveColorSlot();
            }));

    const QString layers = QStringLiteral("Layers");
    command(QStringLiteral("layers.above"), layers, QStringLiteral("Select Layer Above"),
            {QKeySequence("Alt+PgUp")}, [this]() { selectLayerAbove(); });
    command(QStringLiteral("layers.below"), layers, QStringLiteral("Select Layer Below"),
            {QKeySequence("Alt+PgDown")}, [this]() { selectLayerBelow(); });
    command(QStringLiteral("layers.top"), layers, QStringLiteral("Select Top Layer"),
            {QKeySequence("Ctrl+Alt+PgUp")}, [this]() { selectTopLayer(); });
    command(QStringLiteral("layers.bottom"), layers, QStringLiteral("Select Bottom Layer"),
            {QKeySequence("Ctrl+Alt+PgDown")}, [this]() { selectBottomLayer(); });

    // Keys that work while held, over the image (the canvas handles them).
    const QString canvas = QStringLiteral("Canvas");
    m_shortcuts->add({QStringLiteral("hold.pan"), canvas, QStringLiteral("Pan While Held"),
                      {QKeySequence(Qt::Key_Space)}, Shortcuts::Kind::Hold, nullptr,
                      QStringLiteral("Hold the key and drag to move the view, with any tool.")});
    m_shortcuts->add({QStringLiteral("hold.rotate"), canvas, QStringLiteral("Rotate View While Held"),
                      {QKeySequence(Qt::SHIFT | Qt::Key_Space)}, Shortcuts::Kind::Hold, nullptr,
                      QStringLiteral("Hold the key and drag around the middle of the view to turn it freely.")});
    m_shortcuts->add({QStringLiteral("hold.colorPicker"), canvas, QStringLiteral("Color Picker While Held"),
                      {QKeySequence("I")}, Shortcuts::Kind::Hold, nullptr,
                      QStringLiteral("Hold the key and click (or touch with the pen) to pick a color; "
                                     "letting go brings back the tool you had.")});

    connect(m_canvas, &CanvasWidget::colorPickerHeld, this, &MainWindow::onColorPickerHeld);
    connect(m_shortcuts, &Shortcuts::changed, this, &MainWindow::applyShortcutKeys);
    applyShortcutKeys();
}

void MainWindow::applyShortcutKeys() {
    // Tool keys: one action per key. Tools sharing a key (S: selection
    // tools, M: move tools, O: line and shapes) cycle in Paint.NET's order,
    // and Shift with that key cycles backwards when Shift+key is free.
    for (QAction *a : m_toolKeyActions) {
        a->setShortcuts({});
        removeAction(a);
        a->deleteLater();
    }
    m_toolKeyActions.clear();
    QList<QKeySequence> keyOrder;
    QMap<QString, std::vector<ToolType>> groups;
    QMap<ToolType, QString> toolKeys;
    for (const ToolInfo &info : toolInfos()) {
        const QList<QKeySequence> keys = m_shortcuts->keys(toolShortcutId(info));
        toolKeys.insert(info.type, Shortcuts::text(keys));
        for (const QKeySequence &key : keys) {
            const QString k = key.toString(QKeySequence::PortableText);
            if (!groups.contains(k)) keyOrder << key;
            groups[k].push_back(info.type);
        }
    }
    for (const QKeySequence &key : keyOrder) {
        const std::vector<ToolType> group = groups.value(key.toString(QKeySequence::PortableText));
        const QString keyText = key.toString(QKeySequence::NativeText);
        const QString label = group.size() > 1 ? QStringLiteral("Tools ") + keyText
                                               : QString::fromLatin1(toolInfo(group.front()).name);
        m_toolKeyActions << addShortcutAction(label, {key}, [this, group]() { cycleToolGroup(group); });
        const QKeySequence shifted(key[0].keyboardModifiers() | Qt::ShiftModifier | key[0].key());
        if (group.size() > 1 && !(key[0].keyboardModifiers() & Qt::ShiftModifier)
            && m_shortcuts->clashes(QString(), shifted).isEmpty())
            m_toolKeyActions << addShortcutAction(label + QStringLiteral(" reverse"), {shifted},
                                                  [this, group]() { cycleToolGroup(group, true); });
    }
    if (m_toolsPanel) {
        m_toolsPanel->setToolKeys(toolKeys);
        m_toolsPanel->refreshPins();   // pinned commands show their keys
    }

    m_canvas->setHoldKeys(m_shortcuts->key(QStringLiteral("hold.pan")),
                          m_shortcuts->key(QStringLiteral("hold.rotate")),
                          m_shortcuts->key(QStringLiteral("hold.colorPicker")));
    for (const auto &update : m_shortcutTooltipUpdates) update();
    updateRotationIndicator(m_canvas->rotation());
}

void MainWindow::selectLastPaintTool() {
    selectTool(m_lastPaintTool);
}

void MainWindow::toggleEraser() {
    if (m_currentTool && m_currentTool->type() == ToolType::Eraser) selectLastPaintTool();
    else selectTool(ToolType::Eraser);
}

void MainWindow::onColorPickerHeld(bool held) {
    if (held) {
        if (m_pickerHeld || !m_currentTool) return;
        m_pickerHeld = true;
        m_toolBeforePicker = m_currentTool->type();
        if (m_toolBeforePicker != ToolType::ColorPicker) selectTool(ToolType::ColorPicker);
        return;
    }
    if (!m_pickerHeld) return;
    m_pickerHeld = false;
    // Back only if nothing else chose a tool meanwhile.
    if (m_currentTool && m_currentTool->type() == ToolType::ColorPicker
        && m_toolBeforePicker != ToolType::ColorPicker)
        selectTool(m_toolBeforePicker);
}

double MainWindow::rotationStep() {
    const double step = QSettings(AppInfo::kId, AppInfo::kId).value(QStringLiteral("view/rotationStep"), 15.0).toDouble();
    return std::isfinite(step) ? qBound(0.1, step, 180.0) : 15.0;
}

void MainWindow::setRotationStep(double degrees) {
    QSettings(AppInfo::kId, AppInfo::kId).setValue(QStringLiteral("view/rotationStep"), qBound(0.1, degrees, 180.0));
}

void MainWindow::rotateViewLeft() {
    m_canvas->rotateBy(-rotationStep());
}

void MainWindow::rotateViewRight() {
    m_canvas->rotateBy(rotationStep());
}

void MainWindow::resetViewRotation() {
    m_canvas->setRotation(0.0);
}

void MainWindow::updateRotationStepActions() {
    if (!m_rotationStepGroup) return;
    const double step = rotationStep();
    bool preset = false;
    for (QAction *a : m_rotationStepGroup->actions()) {
        const bool match = std::abs(a->data().toDouble() - step) < 1e-6;
        a->setChecked(match);
        preset = preset || match;
    }
    if (!preset) {
        // Uncheck all (an exclusive group keeps one checked otherwise).
        m_rotationStepGroup->setExclusive(false);
        for (QAction *a : m_rotationStepGroup->actions()) a->setChecked(false);
        m_rotationStepGroup->setExclusive(true);
    }
    if (m_otherRotationStepAction)
        m_otherRotationStepAction->setText(
            preset ? QStringLiteral("Other Step...")
                   : QStringLiteral("Other Step (now %1\u00B0)...").arg(QLocale().toString(step, 'g', 4)));
}

void MainWindow::updateRotationIndicator(double degrees) {
    if (!m_rotationButton) return;
    m_rotationButton->setVisible(degrees != 0.0);
    m_rotationButton->setText(QStringLiteral("\u21BB %1\u00B0").arg(QLocale().toString(degrees, 'f', degrees == std::round(degrees) ? 0 : 1)));
    const QString keys = Shortcuts::text(m_shortcuts->keys(QStringLiteral("View > Rotate View > Reset Rotation")));
    m_rotationButton->setToolTip(
        QStringLiteral("<b>View rotated</b><br>Only the view is turned; the image is not. "
                       "Click to turn it back%1.").arg(keys.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(keys)));
}

void MainWindow::createToolBar() {
    auto *toolbar = addToolBar(QStringLiteral("Toolbar"));
    toolbar->setObjectName("FixedToolbar");
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(16, 16));
    m_fixedToolbar = toolbar;
    populateFixedToolbar();
}

void MainWindow::populateFixedToolbar() {
    QToolBar *toolbar = m_fixedToolbar;
    if (!toolbar) return;

    // Exact paint.net toolbar order:
    // New, Open, Save, Print | Cut, Copy, Paste | Crop, Deselect | Undo, Redo | Grid, Rulers
    toolbar->addAction(ToolIcons::newDoc(), QStringLiteral("New"), this, &MainWindow::newDocument);
    toolbar->addAction(ToolIcons::openDoc(), QStringLiteral("Open"), this, &MainWindow::openDocument);
    toolbar->addAction(ToolIcons::saveDoc(), QStringLiteral("Save"), this, &MainWindow::saveDocument);
    toolbar->addAction(ToolIcons::printAction(), QStringLiteral("Print"), this, &MainWindow::printDocument);
    toolbar->addSeparator();
    toolbar->addAction(ToolIcons::cutAction(), QStringLiteral("Cut"), this, &MainWindow::cut);
    toolbar->addAction(ToolIcons::copyAction(), QStringLiteral("Copy"), this, &MainWindow::copy);
    toolbar->addAction(ToolIcons::pasteAction(), QStringLiteral("Paste"), this, &MainWindow::paste);
    toolbar->addSeparator();
    toolbar->addAction(ToolIcons::cropAction(), QStringLiteral("Crop to Selection"), this, &MainWindow::cropToSelection);
    toolbar->addAction(ToolIcons::deselectAction(), QStringLiteral("Deselect"), this, &MainWindow::deselectAll);
    toolbar->addSeparator();
    m_undoToolbarAction = toolbar->addAction(ToolIcons::undoAction(), QStringLiteral("Undo"), this, &MainWindow::undo);
    m_redoToolbarAction = toolbar->addAction(ToolIcons::redoAction(), QStringLiteral("Redo"), this, &MainWindow::redo);
    m_undoToolbarAction->setEnabled(false);
    m_redoToolbarAction->setEnabled(false);
    toolbar->addSeparator();

    auto *gridToggle = toolbar->addAction(ToolIcons::pixelGridAction(), QStringLiteral("Pixel Grid"), this, &MainWindow::toggleGrid);
    gridToggle->setCheckable(true);
    gridToggle->setChecked(m_canvas && m_canvas->showGrid());
    auto *rulersToggle = toolbar->addAction(ToolIcons::rulersAction(), QStringLiteral("Rulers"), this, [this]() {
        bool show = !m_canvas->showRulers();
        m_canvas->setShowRulers(show);
        if (m_rulersAction) m_rulersAction->setChecked(show);
    });
    rulersToggle->setCheckable(true);
    rulersToggle->setChecked(m_canvas ? m_canvas->showRulers() : true);

    // Keep the toolbar toggles and the View-menu entries in sync.
    if (m_gridAction) {
        connect(m_gridAction, &QAction::toggled, gridToggle, &QAction::setChecked);
        connect(gridToggle, &QAction::toggled, m_gridAction, &QAction::setChecked);
    }
    if (m_rulersAction)
        connect(m_rulersAction, &QAction::toggled, rulersToggle, &QAction::setChecked);

    // The rest of the toolbar: the open-image thumbnails (like browser tabs),
    // centred on the window. Created here, together with the toolbar.
    m_imageListBar = new ImageListBar(this);
    connect(m_imageListBar, &ImageListBar::imageSelected, this, &MainWindow::setActiveDocument);
    connect(m_imageListBar, &ImageListBar::imageCloseRequested, this, &MainWindow::closeDocument);
    toolbar->addWidget(m_imageListBar);
    updateImageList();

    // Force top bars to match Paint.NET structure:
    // row 1: menu, row 2: fixed toolbar, row 3: variable toolbar.
    if (m_variableToolbar) {
        insertToolBar(m_variableToolbar, m_fixedToolbar);
        insertToolBarBreak(m_variableToolbar);
    }
}

void MainWindow::perspectiveSettingsChanged() {
    Perspective::saveSettings();
    const Perspective::Settings &ps = Perspective::settings();
    if (m_perspectiveGuidesAction) m_perspectiveGuidesAction->setChecked(ps.visible);
    if (m_perspectiveSnapAction) m_perspectiveSnapAction->setChecked(ps.snap);
    if (m_toolOptionsPanel) m_toolOptionsPanel->syncPerspectiveControls();
    if (m_canvas) m_canvas->update();
}

void MainWindow::perspectiveGuidesToLayer() {
    if (!m_document) return;
    const QVector<QPointF> &points = m_document->vanishingPoints();
    if (points.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Place a vanishing point first: click on the image with the Perspective tool."), 8000);
        return;
    }
    m_document->addLayer(Perspective::renderLayer(points, Perspective::settings().rays, m_document->size()),
                         QStringLiteral("Perspective guides"));
    m_canvas->updateCanvas();
}

// ---- Edit operations ----
