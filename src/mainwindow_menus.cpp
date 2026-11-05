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
#include <QMenuBar>

void MainWindow::createMenus() {
    auto setShortcuts = [](QAction *action, std::initializer_list<QKeySequence> shortcuts) {
        action->setShortcuts(QList<QKeySequence>(shortcuts));
    };

    // File menu
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("&New..."), QKeySequence::New, this, &MainWindow::newDocument);
    fileMenu->addAction(QStringLiteral("&Open..."), QKeySequence::Open, this, &MainWindow::openDocument);
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
    const QString html = QString(
        "<h3>%1</h3>"
        "<table cellspacing='6'>"
        "<tr><td><b>%2</b></td><td>S · S×2 · S×3 · S×4</td></tr>"
        "<tr><td><b>%3</b></td><td>M · M×2</td></tr>"
        "<tr><td><b>%4</b></td><td>O · O×2</td></tr>"
        "<tr><td>%5</td><td>B · P · E · F · G · H · K · L · R · T · Z</td></tr>"
        "<tr><td>%6</td><td>[ ] &nbsp; Ctrl+[ Ctrl+]</td></tr>"
        "<tr><td>%7</td><td>X · C</td></tr>"
        "<tr><td>%8</td><td>F5 · F6 · F7 · F8</td></tr>"
        "<tr><td>%9</td><td>Ctrl+Maj+F5…F8</td></tr>"
        "<tr><td>%10</td><td>Ctrl+Z · Ctrl+Y</td></tr>"
        "<tr><td>%11</td><td>Ctrl+F</td></tr>"
        "</table>")
        .arg(QStringLiteral("Keyboard shortcuts"),
             QStringLiteral("Selection tools (cycle)"),
             QStringLiteral("Move tools (cycle)"),
             QStringLiteral("Line / Shapes (cycle)"),
             QStringLiteral("Direct tool keys"),
             QStringLiteral("Brush size"),
             QStringLiteral("Swap / toggle colors"),
             QStringLiteral("Tools / History / Layers / Colors windows"),
             QStringLiteral("Reset a window's position"))
        .arg(QStringLiteral("Undo / Redo"),
             QStringLiteral("Repeat last effect"));

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
        btn->setToolTip(QString("<b>%1</b> (%2)<br>%3<br><i>%4</i>")
                            .arg((label), key, (what),
                                 QStringLiteral("Ctrl+Shift+" ) + key + " : " + QStringLiteral("reset the window position")));
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
        auto *toggle = new QAction((label), this);
        toggle->setShortcut(QKeySequence(key));
        connect(toggle, &QAction::triggered, this, [dock]() { dock->setVisible(!dock->isVisible()); });
        addAction(toggle);

        auto *reset = new QAction("Reset " + label, this);
        reset->setShortcut(QKeySequence("Ctrl+Shift+" + key));
        connect(reset, &QAction::triggered, this, [this, dock]() { resetUtilityWindow(dock); });
        addAction(reset);

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
                           "<p>Version %2</p>"
                           "<p>A free image editor for Linux in the style of Paint.NET, built with Qt and C++.</p>"
                           "<p>Based on <a href=\"https://github.com/Univers4craft/paint.software\">paint.software</a> "
                           "by Univers4craft and contributors. MIT license.</p>"
                           "<p><a href=\"%3\">%3</a></p>"
                           "<p>Not affiliated with Paint.NET or dotPDN LLC.</p>")
                .arg(AppInfo::kDisplayName, QStringLiteral(PWM_VERSION_STR), AppInfo::kHomepage));
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

void MainWindow::createKeyboardShortcuts() {
    addShortcutAction("Deselect", {QKeySequence("Return"), QKeySequence("Enter")}, [this]() {
        if (!m_document || !m_document->selection().hasSelection()) return;
        if (m_currentTool && m_currentTool->type() == ToolType::Text) return;
        deselectAll();
    });

    // Tool keys from the registry. Tools sharing a key (S: selection tools,
    // M: move tools, O: line and shapes) cycle in Paint.NET's order, and
    // Shift with that key cycles backwards.
    QMap<QString, std::vector<ToolType>> groups;
    QStringList keyOrder;
    for (const ToolInfo &info : toolInfos()) {
        const QString key = QString::fromLatin1(info.shortcut);
        if (key.isEmpty()) continue;
        if (!groups.contains(key)) keyOrder << key;
        groups[key].push_back(info.type);
    }
    for (const QString &key : keyOrder) {
        const std::vector<ToolType> group = groups.value(key);
        const QString label = group.size() > 1 ? QStringLiteral("Tools ") + key
                                               : QString::fromLatin1(toolInfo(group.front()).name);
        addShortcutAction(label, {QKeySequence(key)}, [this, group]() { cycleToolGroup(group); });
        if (group.size() > 1)
            addShortcutAction(label + QStringLiteral(" reverse"), {QKeySequence(QStringLiteral("Shift+") + key)},
                              [this, group]() { cycleToolGroup(group, true); });
    }

    addShortcutAction("Swap colors", {QKeySequence("X")}, [this]() {
        if (!allowSingleKeyShortcuts() || !m_colorsPanel) return;
        m_colorsPanel->swapPrimaryAndSecondaryColors();
    });
    addShortcutAction("Toggle active color", {QKeySequence("C")}, [this]() {
        if (!allowSingleKeyShortcuts() || !m_colorsPanel) return;
        m_colorsPanel->toggleActiveColorSlot();
    });

    addShortcutAction("Layer above", {QKeySequence("Alt+PgUp")}, [this]() { selectLayerAbove(); });
    addShortcutAction("Layer below", {QKeySequence("Alt+PgDown")}, [this]() { selectLayerBelow(); });
    addShortcutAction("Top layer", {QKeySequence("Ctrl+Alt+PgUp")}, [this]() { selectTopLayer(); });
    addShortcutAction("Bottom layer", {QKeySequence("Ctrl+Alt+PgDown")}, [this]() { selectBottomLayer(); });
    addShortcutAction("Brush size down", {QKeySequence("[")}, [this]() { adjustCurrentToolBrushSize(-1); });
    addShortcutAction("Brush size up", {QKeySequence("]")}, [this]() { adjustCurrentToolBrushSize(1); });
    addShortcutAction("Brush size down fast", {QKeySequence("Ctrl+[")}, [this]() { adjustCurrentToolBrushSize(-5); });
    addShortcutAction("Brush size up fast", {QKeySequence("Ctrl+]")}, [this]() { adjustCurrentToolBrushSize(5); });
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

    // Right side of the toolbar: the open-image thumbnail list (like browser tabs).
    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    // Created here, together with the toolbar that hosts it.
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
