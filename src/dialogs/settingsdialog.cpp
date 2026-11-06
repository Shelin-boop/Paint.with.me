#include "settingsdialog.h"
#include "theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QListWidget>
#include <QStackedWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include "appinfo.h"
#include "displayserver.h"
#include "core/history.h"
#include "core/pensettings.h"
#include "dialogs/penwidgets.h"
#include "dialogs/shortcutshelp.h"
#include <QScrollArea>
#include <QGroupBox>
#include <QGuiApplication>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include "dialogs/keyboardpage.h"

SettingsDialog::SettingsDialog(QWidget *parent, Shortcuts *shortcuts)
    : QDialog(parent), m_shortcuts(shortcuts) {
    setWindowTitle(QStringLiteral("Settings"));
    resize(680, 540);   // room for the Keyboard page's list

    auto *root = new QVBoxLayout(this);
    auto *body = new QHBoxLayout;

    // ---- Category list (left) ----
    m_categories = new QListWidget;
    m_categories->setFixedWidth(160);
    m_categories->addItem(QStringLiteral("User Interface"));
    m_categories->addItem(QStringLiteral("Canvas"));
    m_categories->addItem(QStringLiteral("Undo History"));
    m_categories->addItem(QStringLiteral("Tools"));
    m_categories->addItem(QStringLiteral("Keyboard"));
    m_categories->addItem(QStringLiteral("Help"));
    body->addWidget(m_categories);

    // ---- Pages (right) ----
    m_pages = new QStackedWidget;
    m_pages->addWidget(buildUserInterfacePage());
    m_pages->addWidget(buildCanvasPage());
    m_pages->addWidget(buildHistoryPage());
    m_pages->addWidget(buildToolsPage());
    m_pages->addWidget(buildKeyboardPage());
    m_pages->addWidget(buildHelpPage());
    body->addWidget(m_pages, 1);

    connect(m_categories, &QListWidget::currentRowChanged,
            m_pages, &QStackedWidget::setCurrentIndex);
    m_categories->setCurrentRow(0);

    root->addLayout(body, 1);

    // ---- Close button ----
    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto *closeBtn = new QPushButton(QStringLiteral("Close"));
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);
}

QWidget *SettingsDialog::buildUserInterfacePage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    auto *form = new QFormLayout;

    // Color Scheme: Default / Light / Dark, applied immediately.
    m_schemeCombo = new QComboBox;
    m_schemeCombo->addItem(QStringLiteral("Default (system)"), int(Theme::Scheme::Default));
    m_schemeCombo->addItem(QStringLiteral("Light"),            int(Theme::Scheme::Light));
    m_schemeCombo->addItem(QStringLiteral("Dark"),           int(Theme::Scheme::Dark));
    m_schemeCombo->setCurrentIndex(int(Theme::scheme()));
    connect(m_schemeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        Theme::setScheme(static_cast<Theme::Scheme>(m_schemeCombo->itemData(i).toInt()));
        Theme::saveToSettings();
        emit colorSchemeChanged();
    });
    form->addRow(QStringLiteral("Color Scheme:"), m_schemeCombo);


    layout->addLayout(form);

    QSettings settings(AppInfo::kId, AppInfo::kId);
    auto *followPanels = new QCheckBox(QStringLiteral("Keep floating panels attached to the main window"));
    followPanels->setObjectName("followUtilityPanels");
    followPanels->setToolTip(QStringLiteral("Repositions floating panels in the corners when the main window is moved or resized."));
    followPanels->setChecked(settings.value("ui/followUtilityPanels", false).toBool());
    connect(followPanels, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings settings(AppInfo::kId, AppInfo::kId);
        settings.setValue("ui/followUtilityPanels", checked);
        emit panelBehaviorChanged();
    });
    layout->addWidget(followPanels);

    auto *hidePanels = new QCheckBox(QStringLiteral("Hide floating panels when the application loses focus"));
    hidePanels->setObjectName("hideUtilityPanelsOnDeactivate");
    hidePanels->setToolTip(QStringLiteral("Visible panels reappear when Paint.with.me regains focus. Docked panels are not affected."));
    hidePanels->setChecked(settings.value("ui/hideUtilityPanelsOnDeactivate", false).toBool());
    connect(hidePanels, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings settings(AppInfo::kId, AppInfo::kId);
        settings.setValue("ui/hideUtilityPanelsOnDeactivate", checked);
        emit panelBehaviorChanged();
    });
    layout->addWidget(hidePanels);

    // Wayland leaves window placement to the desktop.
    const QString platform = QGuiApplication::platformName();
    if (!DisplayServer::canPlaceWindows(platform)) {
        followPanels->setEnabled(false);
        followPanels->setToolTip(QStringLiteral("On Wayland the desktop places windows, not the app."));
    }

    // ---- Window system: native Wayland or XWayland ----
    const bool waylandSession = !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
    auto *windowGroup = new QGroupBox(QStringLiteral("Window System"));
    auto *windowLayout = new QVBoxLayout(windowGroup);
    auto *current = new QLabel(QStringLiteral("Running on: %1")
                                   .arg(DisplayServer::describe(platform, waylandSession)));
    current->setObjectName("windowSystem");
    windowLayout->addWidget(current);

    auto *choiceRow = new QHBoxLayout;
    choiceRow->addWidget(new QLabel(QStringLiteral("In a Wayland session, use:")));
    auto *choice = new QComboBox;
    choice->setObjectName("windowSystemChoice");
    choice->addItem(QStringLiteral("Wayland (native, recommended)"), true);
    choice->addItem(QStringLiteral("XWayland (for compatibility)"), false);
    choice->setToolTip(QStringLiteral(
        "Native Wayland is sharper with fractional display scaling, and the desktop places "
        "floating panels. XWayland runs the app as an X11 program, for setups where "
        "native Wayland causes trouble."));
    choice->setCurrentIndex(DisplayServer::nativeWaylandEnabled() ? 0 : 1);
    choice->setEnabled(waylandSession);
    choiceRow->addWidget(choice);
    choiceRow->addStretch();
    windowLayout->addLayout(choiceRow);

    auto *note = new QLabel;
    note->setObjectName("windowSystemNote");
    note->setWordWrap(true);
    if (!waylandSession)
        note->setText(QStringLiteral("Only applies in a Wayland session."));
    else if (DisplayServer::platformForcedAtStartup())
        note->setText(QStringLiteral("This time QT_QPA_PLATFORM or -platform decided."));
    else
        note->setText(QStringLiteral("A change needs a restart of Paint.with.me."));
    windowLayout->addWidget(note);
    connect(choice, &QComboBox::currentIndexChanged, this, [this, choice](int) {
        const bool native = choice->currentData().toBool();
        DisplayServer::setNativeWaylandEnabled(native);
        if (DisplayServer::platformForcedAtStartup()
            || DisplayServer::runningAsChosen(QGuiApplication::platformName(), native))
            return;
        askForRestart(native ? QStringLiteral("native Wayland") : QStringLiteral("XWayland"));
    });
    layout->addWidget(windowGroup);

    layout->addStretch();
    return page;
}

QWidget *SettingsDialog::buildCanvasPage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    QSettings s(AppInfo::kId, AppInfo::kId);

    auto *label = new QLabel(QStringLiteral("Transparency checkerboard brightness:"));
    layout->addWidget(label);

    m_checkerSlider = new QSlider(Qt::Horizontal);
    m_checkerSlider->setRange(0, 100);
    m_checkerSlider->setValue(s.value("canvas/checkerBrightness", 80).toInt());
    connect(m_checkerSlider, &QSlider::valueChanged, this, [this](int v) {
        QSettings s2(AppInfo::kId, AppInfo::kId);
        s2.setValue("canvas/checkerBrightness", v);
        emit canvasSettingsChanged();
    });
    layout->addWidget(m_checkerSlider);

    // Rotate Left / Right (View > Rotate View, keys 4 and 6) turn the view
    // by this much.
    layout->addSpacing(12);
    auto *stepRow = new QHBoxLayout;
    stepRow->addWidget(new QLabel(QStringLiteral("Rotate view by")));
    auto *step = new QDoubleSpinBox;
    step->setObjectName(QStringLiteral("rotationStep"));
    step->setRange(0.1, 180.0);
    step->setDecimals(1);
    step->setSingleStep(5.0);
    step->setSuffix(QStringLiteral("\u00B0"));
    step->setValue(qBound(0.1, s.value("view/rotationStep", 15.0).toDouble(), 180.0));
    step->setToolTip(QStringLiteral("How far View > Rotate View > Rotate Left and Rotate Right turn the view. "
                                    "Only the view turns; the image does not change."));
    connect(step, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        QSettings(AppInfo::kId, AppInfo::kId).setValue("view/rotationStep", v);
        emit canvasSettingsChanged();
    });
    stepRow->addWidget(step);
    stepRow->addWidget(new QLabel(QStringLiteral("per step (Rotate Left, Rotate Right)")));
    stepRow->addStretch();
    layout->addLayout(stepRow);

    layout->addStretch();
    return page;
}

QWidget *SettingsDialog::buildKeyboardPage() {
    if (m_shortcuts) return new KeyboardPage(m_shortcuts);
    auto *label = new QLabel(QStringLiteral("Keys can be changed here when Settings is opened from the main window."));
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    return label;
}

QWidget *SettingsDialog::buildHistoryPage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    constexpr qint64 MB = 1024 * 1024;

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(QStringLiteral("Memory for undo steps:")));
    m_historyLimit = new QSpinBox;
    m_historyLimit->setObjectName("historyMemoryLimit");
    m_historyLimit->setRange(128, 65536);
    m_historyLimit->setSingleStep(128);
    m_historyLimit->setSuffix(QStringLiteral(" MB"));
    m_historyLimit->setValue(int(HistoryManager::configuredMemoryBudget() / MB));
    row->addWidget(m_historyLimit);
    auto *useDefault = new QPushButton(QStringLiteral("Use Default"));
    row->addWidget(useDefault);
    row->addStretch();
    layout->addLayout(row);

    auto *note = new QLabel(QStringLiteral(
        "Each image keeps its undo steps within this amount. When they need more, the oldest "
        "steps are dropped; the latest step is always kept. Default for this computer: %1 MB "
        "(one eighth of its memory, between 512 MB and 2048 MB).")
        .arg(HistoryManager::defaultMemoryBudget() / MB));
    note->setWordWrap(true);
    layout->addWidget(note);

    m_historyUsage = new QLabel;
    m_historyUsage->setObjectName("historyUsage");
    m_historyUsage->setWordWrap(true);
    layout->addWidget(m_historyUsage);

    connect(m_historyLimit, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int mb) {
        QSettings(AppInfo::kId, AppInfo::kId).setValue(QStringLiteral("history/memoryLimitMB"), mb);
        emit historySettingsChanged();
    });
    connect(useDefault, &QPushButton::clicked, this, [this]() {
        {
            const QSignalBlocker block(m_historyLimit);
            m_historyLimit->setValue(int(HistoryManager::defaultMemoryBudget() / (1024 * 1024)));
        }
        QSettings(AppInfo::kId, AppInfo::kId).remove(QStringLiteral("history/memoryLimitMB"));
        emit historySettingsChanged();
    });

    layout->addStretch();
    return page;
}

void SettingsDialog::setHistoryUsage(qint64 bytes, int steps) {
    if (!m_historyUsage) return;
    m_historyUsage->setText(QStringLiteral("The current image's %1 undo step(s) use %2 MB.")
                                .arg(steps).arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1));
}

void SettingsDialog::askForRestart(const QString &windowSystem) {
    QMessageBox box(this);
    box.setObjectName("restartQuestion");
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(QStringLiteral("Restart Required"));
    box.setText(QStringLiteral("Paint.with.me uses %1 after a restart.").arg(windowSystem));
    box.setInformativeText(QStringLiteral(
        "Restart now? You are asked to save unsaved changes first, and saved images "
        "open again."));
    QPushButton *now = box.addButton(QStringLiteral("Restart Now"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("Later"), QMessageBox::RejectRole);
    box.setDefaultButton(now);
    box.exec();
    if (box.clickedButton() != now) return;
    m_restartRequested = true;
    accept();
}

QWidget *SettingsDialog::buildToolsPage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    // ---- Drawing ----
    auto *drawing = new QGroupBox(QStringLiteral("Drawing"));
    auto *drawingLayout = new QVBoxLayout(drawing);
    auto *antialias = new QCheckBox(QStringLiteral("Smooth edges (antialiasing)"));
    antialias->setObjectName("antialiasing");
    antialias->setToolTip(QStringLiteral(
        "Smooths the edges of brush strokes, shapes, lines, fills, gradients and text. "
        "Turn it off for hard pixel edges, for example in pixel art."));
    antialias->setChecked(QSettings(AppInfo::kId, AppInfo::kId).value("tools/antialiasing", true).toBool());
    connect(antialias, &QCheckBox::toggled, this, [this](bool on) {
        QSettings(AppInfo::kId, AppInfo::kId).setValue("tools/antialiasing", on);
        emit toolSettingsChanged();
    });
    drawingLayout->addWidget(antialias);
    auto *keys = new QLabel(QStringLiteral("[ and ] change the brush width; with Ctrl in bigger steps."));
    keys->setWordWrap(true);
    drawingLayout->addWidget(keys);
    layout->addWidget(drawing);

    // ---- Pen and tablet ----
    auto *pen = new QGroupBox(QStringLiteral("Pen and Tablet"));
    auto *penLayout = new QVBoxLayout(pen);
    auto *curveRow = new QHBoxLayout;
    auto *curveControls = new QVBoxLayout;
    curveControls->addWidget(new QLabel(QStringLiteral("Pressure response:")));
    auto *sliderRow = new QHBoxLayout;
    sliderRow->addWidget(new QLabel(QStringLiteral("Soft")));
    auto *curveSlider = new QSlider(Qt::Horizontal);
    curveSlider->setObjectName("pressureCurve");
    curveSlider->setRange(-100, 100);
    curveSlider->setPageStep(10);
    curveSlider->setValue(Pen::settings().pressureCurve);
    curveSlider->setToolTip(QStringLiteral(
        "Soft: a light touch already paints strongly. Firm: you need to press harder."));
    sliderRow->addWidget(curveSlider, 1);
    sliderRow->addWidget(new QLabel(QStringLiteral("Firm")));
    curveControls->addLayout(sliderRow);
    auto *resetCurve = new QPushButton(QStringLiteral("As the Pen Reports It"));
    resetCurve->setObjectName("pressureCurveReset");
    resetCurve->setToolTip(QStringLiteral("A straight curve: no change to the pen's pressure."));
    auto *resetRow = new QHBoxLayout;
    resetRow->addWidget(resetCurve);
    resetRow->addStretch();
    curveControls->addLayout(resetRow);
    curveControls->addStretch();
    curveRow->addLayout(curveControls, 1);
    auto *curveView = new PressureCurveView;
    curveView->setCurve(Pen::settings().pressureCurve);
    curveView->setFixedSize(96, 96);
    curveRow->addWidget(curveView);
    penLayout->addLayout(curveRow);
    connect(curveSlider, &QSlider::valueChanged, this, [this, curveView](int value) {
        Pen::settings().pressureCurve = value;
        Pen::saveSettings();
        curveView->setCurve(value);
        emit toolSettingsChanged();
    });
    connect(resetCurve, &QPushButton::clicked, curveSlider, [curveSlider]() { curveSlider->setValue(0); });

    auto *tilt = new QCheckBox(QStringLiteral("Tilting the pen flattens the brush tip (brush and eraser)"));
    tilt->setObjectName("tiltFlattens");
    tilt->setToolTip(QStringLiteral(
        "Like the side of a pencil: the more you tilt the pen, the narrower the tip gets "
        "across the tilt. Needs a pen that reports tilt."));
    tilt->setChecked(Pen::settings().tiltFlattens);
    connect(tilt, &QCheckBox::toggled, this, [this](bool on) {
        Pen::settings().tiltFlattens = on;
        Pen::saveSettings();
        emit toolSettingsChanged();
    });
    penLayout->addWidget(tilt);
    auto *perTool = new QLabel(QStringLiteral(
        "Whether pressure changes the size, the opacity or both is chosen for each tool in "
        "the tool bar (Pressure); it appears there once a pen has been used."));
    perTool->setWordWrap(true);
    penLayout->addWidget(perTool);

    auto *pad = new PenTestPad;
    penLayout->addWidget(pad, 1);
    auto *readingRow = new QHBoxLayout;
    auto *reading = new QLabel(QStringLiteral("No pen seen yet."));
    reading->setObjectName("penReading");
    readingRow->addWidget(reading, 1);
    auto *clearPad = new QPushButton(QStringLiteral("Clear"));
    readingRow->addWidget(clearPad);
    penLayout->addLayout(readingRow);
    connect(clearPad, &QPushButton::clicked, pad, [pad]() { pad->clear(); });
    pad->onPenReading = [reading](double raw, double mapped, QPointF tiltDegrees) {
        reading->setText(QStringLiteral("Pressure %1, after the curve %2. Tilt %3 deg across, %4 deg down.")
                             .arg(raw, 0, 'f', 2).arg(mapped, 0, 'f', 2)
                             .arg(tiltDegrees.x(), 0, 'f', 0).arg(tiltDegrees.y(), 0, 'f', 0));
    };
    layout->addWidget(pen, 1);
    return page;
}

QWidget *SettingsDialog::buildHelpPage() {
    auto *label = new QLabel(ShortcutsHelp::html(m_shortcuts));
    label->setObjectName("shortcutsHelp");
    label->setTextFormat(Qt::RichText);
    label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    label->setWordWrap(true);
    label->setMargin(6);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(label);
    return scroll;
}
