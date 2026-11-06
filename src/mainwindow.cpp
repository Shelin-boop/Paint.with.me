#include "mainwindow.h"
#include "tools/texttool.h"
#include "plugins/pluginmanager.h"
#include "dialogs/pastedialog.h"
#include "toolicons.h"
#include "theme.h"
#include "core/document.h"
#include "core/thumbnails.h"
#include "core/adjustmentpreview.h"
#include "canvas/canvaswidget.h"
#include "panels/layerspanel.h"
#include "panels/colorspanel.h"
#include "panels/historypanel.h"
#include "panels/imagelistbar.h"
#include "commandicons.h"
#include "panels/toolspanel.h"
#include "core/pdfexport.h"
#include "core/perspective.h"
#include "tools/toolregistry.h"
#include "core/history.h"
#include "dialogs/settingsdialog.h"
#include "dialogs/saveconfigdialog.h"
#include "tools/tool.h"
#include "tools/clonestamptool.h"

#include <QDockWidget>
#include <QCloseEvent>
#include <QPainter>
#include <QFileInfo>
#include <QTimer>
#include <QSettings>
#include <QScopedValueRollback>
#include <QMoveEvent>
#include <QResizeEvent>
#include "appinfo.h"
#include "desktopintegration.h"
#include <QApplication>
#include <QGuiApplication>
#include "panels/tooloptionspanel.h"


// The version comes from the build system, so the title bar always names the
// build actually running — it used to be hard-coded and drifted to 1.1 while the
// package was on 1.1.31. Handy when someone reports a bug.

static QString appTitleSuffix() {
    return QStringLiteral(" - ") + AppInfo::kDisplayName + QStringLiteral(" " PWM_VERSION_STR);
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Untitled") + appTitleSuffix());
    resize(1400, 900);
    setMinimumSize(800, 600);
    setDockNestingEnabled(true);
    // GroupedDragging is deliberately OFF. It makes Qt wrap dragged panels in an
    // internal QDockWidgetGroupWindow, which inherits this window's title — that
    // is the blank "Untitled - Paint.with.me x.y.z" box users hit when arranging
    // Colors/History/Layers, and closing one could take the app down with it.
    // Without it each panel floats as its own utility window, which is also what
    // paint.net does, and isFloating() then reports the truth (see
    // ColorsPanel::shrinkToFit).
    setDockOptions(QMainWindow::AnimatedDocks
                   | QMainWindow::AllowNestedDocks
                   | QMainWindow::AllowTabbedDocks);
    setAcceptDrops(true);

    // Default document (registered as the first entry of the image list)
    m_document = new Document(800, 600, this);
    m_documents.append(m_document);
    m_activeDocIndex = 0;

    // Canvas
    m_canvas = new CanvasWidget(this);
    m_canvas->setDocument(m_document);
    setCentralWidget(m_canvas);

    createTools();
    createToolsPanel();
    createDockPanels();
    createMenus();
    // Every menu command can be pinned to the Tools panel.
    m_toolsPanel->setCommands(ToolsPanel::commandsFromMenuBar(menuBar(), {m_recentFilesMenu}));
    createToolBar();
    createStatusBar();
    createKeyboardShortcuts();
    loadUiState();

    m_applicationActive = qApp->applicationState() == Qt::ApplicationActive;
    connect(qApp, &QGuiApplication::applicationStateChanged,
            this, &MainWindow::onApplicationStateChanged);

    m_utilityWindowFollowTimer = new QTimer(this);
    m_utilityWindowFollowTimer->setSingleShot(true);
    m_utilityWindowFollowTimer->setInterval(50);
    connect(m_utilityWindowFollowTimer, &QTimer::timeout,
            this, &MainWindow::repositionFloatingUtilityWindows);

    // Connect signals
    connect(m_canvas, &CanvasWidget::cursorPositionChanged, this, &MainWindow::updateStatusBar);
    connect(m_canvas, &CanvasWidget::zoomChanged, this, &MainWindow::onZoomChanged);
    connect(m_canvas, &CanvasWidget::deleteSelectionRequested, this, &MainWindow::deleteSelectionContents);
    connect(m_canvas, &CanvasWidget::fillSelectionRequested, this, &MainWindow::fillSelection);
    connect(m_canvas, &CanvasWidget::selectionContextMenuRequested, this, &MainWindow::showSelectionContextMenu);
    connect(m_canvas, &CanvasWidget::tabletDetected, this, [this]() {
        if (m_toolOptionsPanel) m_toolOptionsPanel->setTabletPresent(true);
    });
    connect(m_document, &Document::documentChanged, this, &MainWindow::updateTitle);
    connect(m_document, &Document::documentChanged, this, &MainWindow::updateImageList);
    connect(m_document, &Document::primaryColorChanged, this, &MainWindow::onPrimaryColorChanged,
            Qt::UniqueConnection);
    connect(m_canvas, &CanvasWidget::textLayerBlocked, this, &MainWindow::onTextLayerBlocked);
    connect(m_document, &Document::sizeChanged, this, [this](int w, int h) {
        m_sizeLabel->setText(QString("%1 × %2").arg(w).arg(h));
    });
    connect(&m_document->history(), &HistoryManager::historyChanged, this, [this]() {
        const bool canUndo = m_document->history().canUndo();
        const bool canRedo = m_document->history().canRedo();
        m_undoAction->setEnabled(canUndo);
        m_redoAction->setEnabled(canRedo);
        if (m_undoToolbarAction) m_undoToolbarAction->setEnabled(canUndo);
        if (m_redoToolbarAction) m_redoToolbarAction->setEnabled(canRedo);
    });

    // Image list (thumbnail tabs) wiring
    if (m_imageListBar) {
        connect(m_imageListBar, &ImageListBar::imageSelected, this, &MainWindow::setActiveDocument);
        connect(m_imageListBar, &ImageListBar::imageCloseRequested, this, &MainWindow::closeDocument);
    }
    updateImageList();

    // Select brush tool by default
    selectTool(ToolType::Brush);
    // Panels the user made floating get their usual spot next to the app frame
    // at every startup (coordinates saved by a differently sized session could
    // be anywhere, even off-screen). Docked and closed panels stay as they are.
    // Deferred so floating panels are real top-level windows and the window
    // manager has finalised the main window's geometry.
    QTimer::singleShot(250, this, [this]() {
        for (QDockWidget *dock : {m_toolsDock, m_historyDock, m_layersDock, m_colorsDock}) {
            // window() != this, not isFloating(): a panel Qt has wrapped in a
            // QDockWidgetGroupWindow is its own window but reports isFloating()
            // false, and would keep whatever size the saved state gave it.
            if (dock && !dock->isHidden() && dock->window() != this)
                positionUtilityWindow(dock, true);
        }
        m_canvas->resetToDefaultView();   // re-centre once sizes are final
    });
    updateTitle();

    // Apply the current colour scheme (light / dark / follow-OS).
    applyTheme();

    // Keep the floating utility windows in sync with the app's minimise state
    // (some WMs don't send a WindowStateChange event on external minimise).
    auto *utilityWindowStateWatch = new QTimer(this);
    utilityWindowStateWatch->setInterval(200);
    connect(utilityWindowStateWatch, &QTimer::timeout,
            this, &MainWindow::syncUtilityWindowsToMainState);
    utilityWindowStateWatch->start();

    // Autosave every 60s so a crash doesn't lose everything.
    auto *autosaveTimer = new QTimer(this);
    autosaveTimer->setInterval(60 * 1000);
    connect(autosaveTimer, &QTimer::timeout, this, &MainWindow::performAutosave);
    autosaveTimer->start();

    // Offer to restore any autosaved image left behind by a previous crash.
    QTimer::singleShot(400, this, [this]() { checkForRecovery(); });
}

MainWindow::~MainWindow() {
}

void MainWindow::loadUiState() {
    QSettings settings(AppInfo::kId, AppInfo::kId);

    // Geometry and state are stored as base64 strings to avoid Qt INI binary corruption.
    const QString geoB64 = settings.value("ui/mainWindowGeometry").toString();
    if (!geoB64.isEmpty()) {
        restoreGeometry(QByteArray::fromBase64(geoB64.toLatin1()));
    }

    // Versioned layout persistence; any older saved state is intentionally
    // ignored. V8 (2026-11-05): panels became docked by default, so the old
    // floating arrangements are dropped once. V7 existed because a V6 state was written while GroupedDragging
    // was on and can carry a QDockWidgetGroupWindow back in — restoring one
    // re-creates the blank title-less window, and tearing it down again crashes
    // (issue #10). Dropping those layouts once is the cheap, safe cure.
    const QString stateB64 = settings.value("ui/mainWindowStateV8").toString();
    if (!stateB64.isEmpty()) {
        const QByteArray savedState = QByteArray::fromBase64(stateB64.toLatin1());
        // Block normalizeDockLayout while restoreState repositions docks.
        QScopedValueRollback<bool> restoreGuard(m_restoringState, true);
        if (!restoreState(savedState, 8)) {
            settings.remove("ui/mainWindowStateV8");
            settings.remove("ui/mainWindowGeometry");
        }
    }

    auto restoreDock = [&settings](QDockWidget *dock, const char *key, bool defaultVisible) {
        if (!dock) return;
        dock->setVisible(settings.value(key, defaultVisible).toBool());
    };

    restoreDock(m_toolsDock, "ui/toolsVisible", true);
    restoreDock(m_historyDock, "ui/historyVisible", true);
    restoreDock(m_layersDock, "ui/layersVisible", true);
    restoreDock(m_colorsDock, "ui/colorsVisible", true);

    // restoreState() does NOT emit dockLocationChanged, so re-apply the tools
    // palette orientation to match the restored dock area (single thin row when
    // docked top/bottom, 2 columns otherwise).
    if (m_toolsDock && !m_toolsDock->isFloating()) {
        const Qt::DockWidgetArea area = dockWidgetArea(m_toolsDock);
        const bool horizontal = (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea);
        layoutToolsPalette(horizontal);
        if (horizontal)
            QTimer::singleShot(0, this, [this]() { resizeDocks({m_toolsDock}, {30}, Qt::Vertical); });
    }
}

void MainWindow::saveUiState() {
    QSettings settings(AppInfo::kId, AppInfo::kId);

    // Store as base64 strings to avoid Qt INI format corrupting binary QByteArray data.
    settings.setValue("ui/mainWindowGeometry", QString(saveGeometry().toBase64()));
    settings.setValue("ui/mainWindowStateV8", QString(saveState(8).toBase64()));
    settings.remove("ui/mainWindowStateV7");

    if (m_toolsDock) settings.setValue("ui/toolsVisible", m_toolsDock->isVisible());
    if (m_historyDock) settings.setValue("ui/historyVisible", m_historyDock->isVisible());
    if (m_layersDock) settings.setValue("ui/layersVisible", m_layersDock->isVisible());
    if (m_colorsDock) settings.setValue("ui/colorsVisible", m_colorsDock->isVisible());
}

void MainWindow::createTools() {
    for (const ToolInfo &info : toolInfos()) m_tools.push_back(createTool(info.type));
    applyToolSettings();
    // The tool bar follows the format at the text caret.
    if (TextTool *text = textTool())
        text->setFormatListener([this] {
            if (m_toolOptionsPanel && m_currentTool && m_currentTool->type() == ToolType::Text)
                m_toolOptionsPanel->syncTextControls();
        });
}

void MainWindow::applyToolSettings() {
    // Antialiasing is one switch for all tools (Settings > Tools).
    const bool antialias = QSettings(AppInfo::kId, AppInfo::kId).value("tools/antialiasing", true).toBool();
    for (auto &tool : m_tools) tool->setAntialiased(antialias);
    if (m_canvas) m_canvas->update();
}

void MainWindow::restartApplication() {
    // Saved images open again in the new instance; unsaved changes are asked
    // about first, as when quitting. Cancelling that cancels the restart.
    QStringList files;
    for (Document *doc : m_documents)
        if (!doc->filePath().isEmpty()) files << doc->filePath();
    m_restartFiles = files;
    m_restartRequested = true;
    if (!close()) {
        m_restartRequested = false;
        m_restartFiles.clear();
    }
}

void MainWindow::applyTheme() {
    // Application-wide, not just this window: file dialogs and other pop-ups are
    // separate top-level widgets, and with only the main window styled their item
    // views kept a white background under light text (issue #7).
    qApp->setStyleSheet(Theme::styleSheet());
    if (m_canvas) {
        m_canvas->setBackdropColor(QColor(Theme::canvasBackdrop()));
        m_canvas->update();
    }
    // Floating utility windows are separate top-level widgets; restyle them too.
    for (QDockWidget *dock : {m_toolsDock, m_historyDock, m_layersDock, m_colorsDock}) {
        if (dock) dock->setStyleSheet(Theme::styleSheet());
    }
    // The reset/swap icons are painted pixmaps, so a stylesheet can't recolour
    // them: redraw them for the new scheme.
    if (m_colorsPanel) m_colorsPanel->refreshIcons();
    // Command icons draw their lines in the theme's color; pinned commands
    // take the new icons when the panel rebuilds.
    CommandIcons::decorate(m_iconMenus);
    if (m_toolsPanel) {
        m_toolsPanel->refreshIcons();
        if (menuBar()) m_toolsPanel->setCommands(ToolsPanel::commandsFromMenuBar(menuBar(), {m_recentFilesMenu}));
    }
}

void MainWindow::showSettingsDialog() {
    SettingsDialog dlg(this);
    connect(&dlg, &SettingsDialog::colorSchemeChanged, this, [this, &dlg]() {
        applyTheme();
        dlg.setStyleSheet(Theme::styleSheet());
    });
    connect(&dlg, &SettingsDialog::canvasSettingsChanged, this, [this]() {
        if (m_canvas) m_canvas->reloadSettings();
    });
    connect(&dlg, &SettingsDialog::panelBehaviorChanged, this, [this]() {
        scheduleUtilityWindowFollow();
        syncUtilityWindowsToMainState();
    });
    auto showUsage = [this, &dlg]() {
        if (m_document)
            dlg.setHistoryUsage(m_document->history().memoryUsed(), m_document->history().stepCount());
    };
    connect(&dlg, &SettingsDialog::historySettingsChanged, this, [this, showUsage]() {
        const qint64 budget = HistoryManager::configuredMemoryBudget();
        for (Document *doc : m_documents) doc->history().setMemoryBudget(budget);
        showUsage();
    });
    connect(&dlg, &SettingsDialog::toolSettingsChanged, this, &MainWindow::applyToolSettings);
    showUsage();
    dlg.exec();
    if (dlg.restartRequested())
        QTimer::singleShot(0, this, &MainWindow::restartApplication);
}

void MainWindow::selectTool(ToolType type) {
    for (auto &tool : m_tools) {
        if (tool->type() == type) {
            m_currentTool = tool.get();
            m_canvas->setCurrentTool(m_currentTool);
            m_canvas->setCursor(m_currentTool->cursor());
            m_toolOptionsPanel->setTool(m_currentTool);
            if (m_toolLabel) m_toolLabel->setText((m_currentTool->name()));
            // Keep the tool palette highlight in sync when selection comes from
            // a keyboard shortcut or tool-group cycling (not just a button click).
            if (auto *action = m_toolActions.value(type, nullptr))
                action->setChecked(true);
            if (m_toolsPanel) m_toolsPanel->setCurrentTool(type);

            if (m_helpTextLabel) m_helpTextLabel->setText(QString::fromLatin1(toolInfo(type).hint));
            return;
        }
    }
}

// ---- Effects ----

void MainWindow::updateTitle() {
    QString filename;
    if (m_document) {
        if (!m_document->filePath().isEmpty()) {
            QFileInfo fi(m_document->filePath());
            filename = fi.fileName();
        } else {
            filename = QStringLiteral("Untitled");
        }
        if (m_document->isModified()) filename += " *";
    } else {
        filename = QStringLiteral("Untitled");
    }
    setWindowTitle(filename + appTitleSuffix());
}

void MainWindow::updateImageThumbnail() {
    if (!m_imageThumbnail || !m_document) return;
    // Honours each layer's opacity and blend mode; built from small layer copies.
    const QImage small = Thumbnails::document(*m_document, QSize(60, 45));
    QImage composite(small.size(), QImage::Format_ARGB32);
    composite.fill(Qt::white);
    QPainter p(&composite);
    p.drawImage(0, 0, small);
    p.end();
    m_imageThumbnail->setPixmap(QPixmap::fromImage(composite));
}

void MainWindow::updateStatusBar(const QPoint &pos) {
    m_positionLabel->setText(QString("%1, %2").arg(pos.x()).arg(pos.y()));
}

void MainWindow::onZoomChanged(double zoom) {
    m_zoomLabel->setText(QString("%1%").arg(static_cast<int>(zoom * 100)));
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (maybeSaveAll()) {
        saveUiState();
        m_autosaveJob.waitForFinished();   // let a running autosave finish first
        clearAutosave();   // clean exit -> discard the recovery snapshot
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::changeEvent(QEvent *event) {
    if (event->type() == QEvent::WindowStateChange)
        syncUtilityWindowsToMainState();
    QMainWindow::changeEvent(event);
}

void MainWindow::moveEvent(QMoveEvent *event) {
    QMainWindow::moveEvent(event);
    scheduleUtilityWindowFollow();
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    scheduleUtilityWindowFollow();
}
