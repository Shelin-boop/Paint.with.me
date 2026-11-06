// Main window: the docked panels (Tools, Colors, History, Layers), their
// default layout and reset, floating panels, the status bar.
// Split from mainwindow.cpp; one class, several files.
#include "mainwindow.h"
#include "shortcuts.h"
#include "toolicons.h"
#include "core/adjustmentpreview.h"
#include "canvas/canvaswidget.h"
#include "panels/layerspanel.h"
#include "panels/colorspanel.h"
#include "panels/historypanel.h"
#include "panels/tooloptionspanel.h"
#include "panels/toolspanel.h"
#include "core/psdformat.h"
#include "core/pdfexport.h"
#include "core/perspective.h"
#include "tools/toolregistry.h"
#include "dialogs/pdfexportdialog.h"
#include "tools/brushpreset.h"
#include "dialogs/settingsdialog.h"
#include "dialogs/previewdialog.h"
#include "dialogs/saveconfigdialog.h"
#include "tools/tool.h"
#include <QToolBar>
#include <QDockWidget>
#include <QInputDialog>
#include <QLineEdit>
#include <QInputDialog>
#include <QSlider>
#include <QLabel>
#include <QPainter>
#include <QToolButton>
#include <QSettings>
#include <QScopedValueRollback>
#include <QWindow>
#include <QStyle>
#include <QFontMetrics>
#include "appinfo.h"
#include "desktopintegration.h"
#include "displayserver.h"
#include <QGuiApplication>

Qt::Orientation MainWindow::splitOrientationForArea(Qt::DockWidgetArea area) const {
    // Left/right areas keep narrow docks readable by stacking vertically.
    if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) {
        return Qt::Vertical;
    }
    // Top/bottom areas split horizontally so each panel keeps a practical height.
    return Qt::Horizontal;
}

void MainWindow::normalizeDockLayout(QDockWidget *changedDock) {
    if (!changedDock || changedDock->isFloating()) return;
    if (m_normalizingDockLayout) return;
    if (m_restoringState) return;

    QScopedValueRollback<bool> guard(m_normalizingDockLayout, true);

    Qt::DockWidgetArea area = dockWidgetArea(changedDock);
    if (area == Qt::NoDockWidgetArea) return;

    QList<QDockWidget*> allDocks = {m_toolsDock, m_historyDock, m_layersDock, m_colorsDock};
    QList<QDockWidget*> sameArea;
    for (QDockWidget *dock : allDocks) {
        if (!dock || dock->isFloating()) continue;
        if (dockWidgetArea(dock) == area) {
            sameArea.append(dock);
        }
    }

    if (sameArea.size() < 2) return;

    // Ensure minimum practical sizes so panels stay usable. The tools palette wants
    // a tall minimum only when it's a vertical column (left/right); docked top/bottom
    // it's a single short row, so a tall minimum would leave a big empty band.
    if (m_toolsDock) {
        const Qt::DockWidgetArea ta = dockWidgetArea(m_toolsDock);
        const bool toolsHorizontal = (ta == Qt::TopDockWidgetArea || ta == Qt::BottomDockWidgetArea);
        m_toolsDock->setMinimumSize(toolsHorizontal ? QSize(120, 30) : QSize(toolsColumnWidth(), 240));
    }
    if (m_historyDock) m_historyDock->setMinimumSize(190, 130);
    if (m_layersDock) m_layersDock->setMinimumSize(190, 150);
    if (m_colorsDock) m_colorsDock->setMinimumSize(190, 190);

    const Qt::Orientation orientation = splitOrientationForArea(area);

    // If dock widgets overlap in the same area, split them to tile the area.
    for (QDockWidget *dock : sameArea) {
        if (dock == changedDock) continue;
        if (dock->geometry().intersects(changedDock->geometry())) {
            splitDockWidget(dock, changedDock, orientation);
            break;
        }
    }
}

void MainWindow::resetUtilityWindow(QDockWidget *dock) {
    // Put one panel back where it is by default: docked in the main window,
    // Tools above Colors on the left, History above Layers on the right.
    if (!dock) return;
    QScopedValueRollback<bool> guard(m_restoringState, true);
    dock->show();
    if (dock->isFloating()) dock->setFloating(false);
    if (dock == m_toolsDock) layoutToolsPalette(false);
    const bool left = (dock == m_toolsDock || dock == m_colorsDock);
    const Qt::DockWidgetArea area = left ? Qt::LeftDockWidgetArea : Qt::RightDockWidgetArea;
    const QList<QDockWidget *> order = left ? QList<QDockWidget *>{m_toolsDock, m_colorsDock}
                                            : QList<QDockWidget *>{m_historyDock, m_layersDock};
    // Rebuild that column in the default order, from the panels already
    // docked there plus this one.
    QList<QDockWidget *> column;
    for (QDockWidget *d : order) {
        if (d == dock || (d && d->isVisible() && !d->isFloating() && dockWidgetArea(d) == area))
            column.append(d);
    }
    addDockWidget(area, column.first());
    for (int i = 1; i < column.size(); ++i)
        splitDockWidget(column.at(i - 1), column.at(i), Qt::Vertical);
}

void MainWindow::positionUtilityWindow(QDockWidget *dock, bool resetSize) {
    if (!dock || !m_canvas) return;
    QWidget *top = dock->window();
    if (!top || top == this) return;

    // Default arrangement, reproducing the user's preferred layout. Left/top
    // windows anchor to the app's top-left, right/bottom windows to the app's
    // right/bottom edge, so it holds up if the window is resized. Everything is
    // clamped to stay fully inside the app frame.
    //   Tools  -> upper-left (over the canvas)   History -> upper-right
    //   Colors -> lower-left                      Layers  -> lower-right
    const QRect app = frameGeometry();          // includes the WM title bar
    const int rightW = 262;

    int width = top->width();
    int height = top->height();
    if (resetSize) {
        if (dock == m_toolsDock) {
            width = top->sizeHint().width();
            height = 312;
        } else if (dock == m_historyDock) {
            width = rightW;
            height = 300;
        } else if (dock == m_layersDock) {
            width = rightW;
            height = 260;
        } else if (dock == m_colorsDock) {
            width = 230;
            height = std::max(300, top->sizeHint().height());
        }
    }

    auto place = [&](int x, int y, int w, int h) {
        x = std::max(app.left() + 6, std::min(x, app.right() - w - 6));
        y = std::max(app.top() + 6, std::min(y, app.bottom() - h - 6));
        top->resize(w, h);
        top->move(x, y);
    };

    if (dock == m_toolsDock) {
        place(app.left() + 65, app.top() + 164, width, height);
    } else if (dock == m_historyDock) {
        place(app.right() - width - 28, app.top() + 153, width, height);
    } else if (dock == m_layersDock) {
        place(app.right() - width - 28, app.bottom() - height - 78, width, height);
    } else if (dock == m_colorsDock) {
        place(app.left() + 28, app.bottom() - height - 76, width, height);
    }
}

void MainWindow::scheduleUtilityWindowFollow() {
    if (!m_utilityWindowFollowTimer) return;
    QSettings settings(AppInfo::kId, AppInfo::kId);
    if (settings.value("ui/followUtilityPanels", false).toBool())
        m_utilityWindowFollowTimer->start();
}

void MainWindow::repositionFloatingUtilityWindows() {
    // On Wayland the desktop places windows; moving them would do nothing.
    if (!DisplayServer::canPlaceWindows(QGuiApplication::platformName())) return;
    QSettings settings(AppInfo::kId, AppInfo::kId);
    if (!settings.value("ui/followUtilityPanels", false).toBool()) return;
    for (QDockWidget *dock : {m_toolsDock, m_historyDock, m_layersDock, m_colorsDock}) {
        if (dock && !dock->isHidden() && dock->window() != this)
            positionUtilityWindow(dock);
    }
}

// Default panel columns, as the owner arranged them (2026-11-05): Tools above
// Colors on the left, History above Layers on the right. Widths in px; the
// heights are proportions within each column.
static constexpr int kLeftPanelWidth = 204;

static constexpr int kRightPanelWidth = 262;

static constexpr int kToolsHeight = 340, kColorsHeight = 530;

static constexpr int kHistoryHeight = 310, kLayersHeight = 550;

void MainWindow::createToolsPanel() {
    m_toolActionGroup = new QActionGroup(this);
    m_toolActionGroup->setExclusive(true);

    QVector<ToolsPanel::ToolEntry> tools;
    for (const ToolInfo &info : toolInfos())
        tools.push_back({QString::fromLatin1(info.name), QString::fromLatin1(info.shortcut), info.type,
                         info.row, info.col, QString::fromLatin1(info.description)});

    m_toolsPanel = new ToolsPanel(tools);
    for (const auto &t : tools) {
        QToolButton *btn = m_toolsPanel->toolButton(t.type);
        // NB: no btn->setShortcut() — the single-key shortcuts are owned by the
        // global actions in createKeyboardShortcuts(); binding them twice
        // caused "Ambiguous shortcut overload" and neither would fire.
        auto *action = new QAction(t.name, this);
        action->setCheckable(true);
        m_toolActionGroup->addAction(action);
        m_toolActions.insert(t.type, action);
        connect(action, &QAction::toggled, btn, &QToolButton::setChecked);
        connect(btn, &QToolButton::clicked, action, [action]() { action->setChecked(true); });
    }
    connect(m_toolsPanel, &ToolsPanel::toolClicked, this, &MainWindow::selectTool);

    // Brush presets: the panel keeps them, the main window knows the tools.
    connect(m_toolsPanel, &ToolsPanel::presetActivated, this, [this](const BrushPreset &preset) {
        selectTool(preset.tool);
        if (!m_currentTool || m_currentTool->type() != preset.tool) return;
        preset.applyTo(*m_currentTool);
        if (m_toolOptionsPanel) m_toolOptionsPanel->setTool(m_currentTool);   // show the values
        if (m_canvas) m_canvas->update();
        statusBar()->showMessage(QStringLiteral("Brush preset: %1").arg(preset.name), 4000);
    });
    connect(m_toolsPanel, &ToolsPanel::savePresetRequested, this, [this]() {
        if (!m_currentTool || !BrushPreset::supports(m_currentTool->type())) return;
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("Save Brush Preset"), QStringLiteral("Name:"), QLineEdit::Normal,
            BrushPreset::defaultName(*m_currentTool), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        m_toolsPanel->addPreset(BrushPreset::capture(*m_currentTool, name));
    });
    connect(m_toolsPanel, &ToolsPanel::updatePresetRequested, this, [this](const QString &id) {
        const BrushPreset *old = m_toolsPanel->preset(id);
        if (!old || !m_currentTool || !BrushPreset::supports(m_currentTool->type())) return;
        BrushPreset updated = BrushPreset::capture(*m_currentTool, old->name);
        updated.id = id;
        m_toolsPanel->updatePreset(updated);
    });

    m_toolsDock = new QDockWidget(QStringLiteral("Tools"), this);
    m_toolsDock->setObjectName("ToolsDock");
    m_toolsDock->setWidget(m_toolsPanel);
    m_toolsDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_toolsDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::LeftDockWidgetArea, m_toolsDock);
    layoutToolsPalette(false);   // vertical by default
    // When docked to the top or bottom, lay the tools out in one row;
    // otherwise keep the vertical columns.
    connect(m_toolsDock, &QDockWidget::dockLocationChanged, this, [this](Qt::DockWidgetArea area) {
        const bool horizontal = (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea);
        layoutToolsPalette(horizontal);
        // Setting the widget's max height isn't enough — force the dock itself to
        // collapse to the single-row height so there's no empty band above/below.
        if (horizontal)
            resizeDocks({m_toolsDock}, {ToolsPanel::kButtonHeight + 2}, Qt::Vertical);
        normalizeDockLayout(m_toolsDock);
    });
    connect(m_toolsDock, &QDockWidget::topLevelChanged, this, [this](bool floating) {
        if (floating) layoutToolsPalette(false);   // floating -> vertical
    });
}

int MainWindow::toolsColumnWidth() const {
    // The title bar shows "Tools" plus a float and a close button; narrower
    // than that, Qt shortens the title to "...".
    const QFontMetrics fm(m_toolsDock ? m_toolsDock->font() : font());
    const QString title = m_toolsDock ? m_toolsDock->windowTitle() : QStringLiteral("Tools");
    const int buttons = 2 * (style()->pixelMetric(QStyle::PM_SmallIconSize) + 8);
    return std::max(28 * 2 + 6, fm.horizontalAdvance(title) + buttons + 16);
}

void MainWindow::layoutToolsPalette(bool horizontal) {
    if (!m_toolsPanel) return;
    m_toolsPanel->setHorizontal(horizontal);
    if (!m_toolsDock) return;
    // Clear the sticky tall minimum normalizeDockLayout imposes for the
    // vertical palette — otherwise the dock can't shrink to the single row.
    m_toolsDock->setMinimumSize(0, 0);
    if (!horizontal) m_toolsDock->setMinimumWidth(toolsColumnWidth());
}

void MainWindow::createDockPanels() {
    // Tool Options as horizontal toolbar under the main toolbar
    m_toolOptionsPanel = new ToolOptionsPanel;
    connect(m_toolOptionsPanel, &ToolOptionsPanel::toolChangeRequested,
            this, &MainWindow::selectTool);
    connect(m_toolOptionsPanel, &ToolOptionsPanel::toolOptionsChanged,
            this, [this]() { if (m_canvas) m_canvas->update(); });
    connect(m_toolOptionsPanel, &ToolOptionsPanel::perspectiveSettingsChanged,
            this, &MainWindow::perspectiveSettingsChanged);
    connect(m_toolOptionsPanel, &ToolOptionsPanel::perspectiveToLayerRequested,
            this, &MainWindow::perspectiveGuidesToLayer);
    connect(m_toolOptionsPanel, &ToolOptionsPanel::perspectiveClearRequested, this, [this]() {
        if (!m_document) return;
        m_document->vanishingPoints().clear();
        m_canvas->update();
    });
    auto *optionsBar = new QToolBar("Tool options", this);
    optionsBar->setObjectName("VariableToolbar");
    optionsBar->setMovable(false);
    optionsBar->setIconSize(QSize(14, 14));
    optionsBar->addWidget(m_toolOptionsPanel);
    addToolBar(Qt::TopToolBarArea, optionsBar);
    m_variableToolbar = optionsBar;

    // Colors panel - LEFT side (below Tools)
    m_colorsDock = new QDockWidget(QStringLiteral("Colors"), this);
    m_colorsDock->setObjectName("ColorsDock");
    m_colorsDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_colorsDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    m_colorsPanel = new ColorsPanel;
    m_colorsPanel->setDocument(m_document);
    m_colorsDock->setWidget(m_colorsPanel);
    addDockWidget(Qt::LeftDockWidgetArea, m_colorsDock);
    connect(m_colorsDock, &QDockWidget::dockLocationChanged, this, [this](Qt::DockWidgetArea) {
        normalizeDockLayout(m_colorsDock);
    });

    // History panel - RIGHT top
    m_historyDock = new QDockWidget(QStringLiteral("History"), this);
    m_historyDock->setObjectName("HistoryDock");
    m_historyDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_historyDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    m_historyPanel = new HistoryPanel;
    m_historyPanel->setDocument(m_document);
    m_historyDock->setWidget(m_historyPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_historyDock);
    connect(m_historyDock, &QDockWidget::dockLocationChanged, this, [this](Qt::DockWidgetArea) {
        normalizeDockLayout(m_historyDock);
    });

    // Layers panel - RIGHT bottom
    m_layersDock = new QDockWidget(QStringLiteral("Layers"), this);
    m_layersDock->setObjectName("LayersDock");
    m_layersDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_layersDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    m_layersPanel = new LayersPanel;
    m_layersPanel->setDocument(m_document);
    // Double-clicking a layer row opens the Layer Properties dialog.
    connect(m_layersPanel, &LayersPanel::layerDoubleClicked, this,
            &MainWindow::showLayerPropertiesDialog);
    m_layersDock->setWidget(m_layersPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_layersDock);
    connect(m_layersDock, &QDockWidget::dockLocationChanged, this, [this](Qt::DockWidgetArea) {
        normalizeDockLayout(m_layersDock);
    });

    // The panels live inside the main window by default (Tools above Colors on
    // the left, History above Layers on the right). Each can still be
    // dragged to another side, tabbed with another panel, resized, hidden
    // (F5-F8) or torn off as a floating window; Ctrl+Shift+F5-F8 or
    // View > Reset Panel Layout puts them back.
    applyDefaultPanelLayout();
}

void MainWindow::applyDefaultPanelLayout() {
    if (!m_toolsDock || !m_historyDock || !m_layersDock || !m_colorsDock) return;
    QScopedValueRollback<bool> guard(m_restoringState, true);   // no normalizing meanwhile
    for (QDockWidget *dock : {m_toolsDock, m_historyDock, m_layersDock, m_colorsDock}) {
        if (dock->isFloating()) dock->setFloating(false);
        dock->show();
    }
    layoutToolsPalette(false);
    addDockWidget(Qt::LeftDockWidgetArea, m_toolsDock);
    splitDockWidget(m_toolsDock, m_colorsDock, Qt::Vertical);
    addDockWidget(Qt::RightDockWidgetArea, m_historyDock);
    splitDockWidget(m_historyDock, m_layersDock, Qt::Vertical);
    resizeDocks({m_toolsDock, m_historyDock}, {kLeftPanelWidth, kRightPanelWidth}, Qt::Horizontal);
    resizeDocks({m_toolsDock, m_colorsDock}, {kToolsHeight, kColorsHeight}, Qt::Vertical);
    resizeDocks({m_historyDock, m_layersDock}, {kHistoryHeight, kLayersHeight}, Qt::Vertical);
}

void MainWindow::createStatusBar() {
    // Active tool name on the far left.
    m_toolLabel = new QLabel("Paintbrush");
    m_toolLabel->setStyleSheet("font-size: 11px; font-weight: bold; padding-left: 4px; padding-right: 6px;");
    statusBar()->addWidget(m_toolLabel);

    // Help text on the left (like Paint.NET)
    m_helpTextLabel = new QLabel(QStringLiteral("Left click to draw with the primary color, right click for the secondary color."));
    m_helpTextLabel->setStyleSheet("font-size: 11px; padding-left: 4px;");

    // Size icon (drawn grid icon) + dimensions
    auto *sizeIcon = new QLabel;
    {
        QPixmap pm(12, 12);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setPen(QPen(QColor(100, 100, 100), 1));
        p.drawRect(1, 1, 10, 10);
        p.drawLine(1, 6, 11, 6);
        p.drawLine(6, 1, 6, 11);
        p.end();
        sizeIcon->setPixmap(pm);
    }
    sizeIcon->setFixedWidth(14);
    m_sizeLabel = new QLabel("800 × 600");
    m_sizeLabel->setStyleSheet("font-size: 11px;");

    // Cursor position icon (drawn crosshair) + coords
    auto *cursorIcon = new QLabel;
    {
        QPixmap pm(12, 12);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(100, 100, 100), 1));
        p.drawLine(6, 1, 6, 11);
        p.drawLine(1, 6, 11, 6);
        p.drawEllipse(3, 3, 6, 6);
        p.end();
        cursorIcon->setPixmap(pm);
    }
    cursorIcon->setFixedWidth(14);
    m_positionLabel = new QLabel("0, 0");
    m_positionLabel->setStyleSheet("font-size: 11px;");

    // "px" unit label
    auto *pxLabel = new QLabel("px");
    pxLabel->setStyleSheet("font-size: 11px; color: #666;");

    // Zoom percentage
    m_zoomLabel = new QLabel("100%");
    m_zoomLabel->setStyleSheet("font-size: 11px;");
    m_zoomLabel->setFixedWidth(42);

    // Paint.NET-like zoom controls on the right: [-] [slider] [+]
    auto *zoomOutBtn = new QToolButton;
    zoomOutBtn->setText("-");
    zoomOutBtn->setFixedSize(18, 16);
    zoomOutBtn->setToolTip(QStringLiteral("Zoom Out"));

    auto *zoomSlider = new QSlider(Qt::Horizontal);
    zoomSlider->setFixedWidth(92);
    zoomSlider->setFixedHeight(16);

    auto *zoomInBtn = new QToolButton;
    zoomInBtn->setText("+");
    zoomInBtn->setFixedSize(18, 16);
    zoomInBtn->setToolTip(QStringLiteral("Zoom In"));

    static const int zoomSteps[] = {
        1, 2, 3, 5, 8, 12, 16, 25, 33, 50, 66, 75,
        100, 125, 150, 200, 300, 400, 600, 800,
        1200, 1600, 2400, 3200, 4800, 6400
    };
    const int zoomStepCount = static_cast<int>(sizeof(zoomSteps) / sizeof(zoomSteps[0]));
    zoomSlider->setRange(0, zoomStepCount - 1);

    auto findClosestZoomIndex = [zoomStepCount](int percent) {
        int bestIdx = 0;
        int bestDelta = std::abs(zoomSteps[0] - percent);
        for (int i = 1; i < zoomStepCount; ++i) {
            int d = std::abs(zoomSteps[i] - percent);
            if (d < bestDelta) {
                bestDelta = d;
                bestIdx = i;
            }
        }
        return bestIdx;
    };

    int initialPercent = static_cast<int>(m_canvas->zoom() * 100.0);
    zoomSlider->setValue(findClosestZoomIndex(initialPercent));

    connect(zoomOutBtn, &QToolButton::clicked, this, [this]() { m_canvas->zoomOut(); });
    connect(zoomInBtn, &QToolButton::clicked, this, [this]() { m_canvas->zoomIn(); });

    connect(zoomSlider, &QSlider::valueChanged, this, [this](int idx) {
        m_canvas->setZoomCentered(zoomSteps[idx] / 100.0);
    });

    // Shown while the view is rotated; a click turns it back.
    m_rotationButton = new QToolButton;
    m_rotationButton->setObjectName(QStringLiteral("RotationIndicator"));
    m_rotationButton->setAutoRaise(true);
    m_rotationButton->setFixedHeight(16);
    m_rotationButton->setStyleSheet(QStringLiteral("QToolButton { font-size: 11px; padding: 0 3px; }"));
    m_rotationButton->hide();
    connect(m_rotationButton, &QToolButton::clicked, this, [this]() { m_canvas->setRotation(0.0); });
    connect(m_canvas, &CanvasWidget::rotationChanged, this, &MainWindow::updateRotationIndicator);

    connect(m_canvas, &CanvasWidget::zoomChanged, this, [this, zoomSlider, findClosestZoomIndex](double zoom) {
        int percent = static_cast<int>(zoom * 100.0);
        m_zoomLabel->setText(QString("%1%").arg(percent));
        zoomSlider->blockSignals(true);
        zoomSlider->setValue(findClosestZoomIndex(percent));
        zoomSlider->blockSignals(false);
    });

    statusBar()->addWidget(m_helpTextLabel, 1);
    statusBar()->addPermanentWidget(sizeIcon);
    statusBar()->addPermanentWidget(m_sizeLabel);
    statusBar()->addPermanentWidget(cursorIcon);
    statusBar()->addPermanentWidget(m_positionLabel);
    statusBar()->addPermanentWidget(pxLabel);
    statusBar()->addPermanentWidget(m_rotationButton);
    statusBar()->addPermanentWidget(m_zoomLabel);
    statusBar()->addPermanentWidget(zoomOutBtn);
    statusBar()->addPermanentWidget(zoomSlider);
    statusBar()->addPermanentWidget(zoomInBtn);
}

// ---- File operations ----

void MainWindow::onApplicationStateChanged(Qt::ApplicationState state) {
    m_applicationActive = state == Qt::ApplicationActive;
    syncUtilityWindowsToMainState();
}

// Floating utility windows are separate top-level windows, so on some window
// managers they don't follow the main window when it is minimised. Keep them in
// sync with minimisation and (when opted in) application activation. Called from
// changeEvent, applicationStateChanged and a low-frequency timer because some
// WMs don't deliver a WindowStateChange on external minimise.
void MainWindow::syncUtilityWindowsToMainState() {
    // Only Minimized is authoritative. Minimising under Muffin/Cinnamon walks the
    // window through Hidden -> Windowed -> Minimized, so counting the transient
    // Hidden as minimised hid the panels early, and the Windowed blip that follows
    // read as "restored" and showed them again mid-minimise.
    bool minimized = isMinimized();
    if (QWindow *wh = windowHandle())
        if (wh->visibility() == QWindow::Minimized) minimized = true;
    QSettings settings(AppInfo::kId, AppInfo::kId);
    const bool hideOnDeactivate = settings.value("ui/hideUtilityPanelsOnDeactivate", false).toBool();
    const bool shouldHide = minimized || (hideOnDeactivate && !m_applicationActive);
    if (shouldHide == m_utilityWindowsHidden) return;
    m_utilityWindowsHidden = shouldHide;

    if (shouldHide) {
        m_hiddenUtilityWindows.clear();
        m_hiddenUtilityWindowGeometry.clear();
        for (QDockWidget *dock : {m_toolsDock, m_historyDock, m_layersDock, m_colorsDock}) {
            if (!dock || !dock->isVisible()) continue;
            // Hide the panel's top-level window, not the dock: isFloating() is
            // false for a panel Qt has wrapped in a QDockWidgetGroupWindow, and
            // that group window — not the dock — is what stays on screen.
            QWidget *top = dock->window();
            if (!top || top == this || m_hiddenUtilityWindows.contains(top)) continue;
            m_hiddenUtilityWindows.append(top);
            m_hiddenUtilityWindowGeometry.append(top->geometry());
            top->hide();
        }
    } else {
        for (int i = 0; i < m_hiddenUtilityWindows.size(); ++i) {
            QWidget *top = m_hiddenUtilityWindows.at(i);
            if (!top) continue;
            top->show();
            // Showing a hidden floating dock can drop its floating state, which
            // re-docks the panel into the main window. Put it back where it was.
            // (A group window is not a QDockWidget, and keeps its own state.)
            if (auto *dock = qobject_cast<QDockWidget *>(top)) {
                if (!dock->isFloating()) {
                    dock->setFloating(true);
                    dock->setGeometry(m_hiddenUtilityWindowGeometry.at(i));
                }
            }
        }
        m_hiddenUtilityWindows.clear();
        m_hiddenUtilityWindowGeometry.clear();
    }
}
