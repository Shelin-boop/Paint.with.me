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
#include <QPushButton>
#include <QSettings>
#include "appinfo.h"
#include "displayserver.h"
#include "core/history.h"
#include <QGroupBox>
#include <QGuiApplication>
#include <QSignalBlocker>
#include <QSpinBox>

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Settings"));
    resize(560, 400);

    auto *root = new QVBoxLayout(this);
    auto *body = new QHBoxLayout;

    // ---- Category list (left) ----
    m_categories = new QListWidget;
    m_categories->setFixedWidth(160);
    m_categories->addItem(QStringLiteral("User Interface"));
    m_categories->addItem(QStringLiteral("Canvas"));
    m_categories->addItem(QStringLiteral("Undo History"));
    m_categories->addItem(QStringLiteral("Tools"));
    m_categories->addItem(QStringLiteral("Help"));
    body->addWidget(m_categories);

    // ---- Pages (right) ----
    m_pages = new QStackedWidget;
    m_pages->addWidget(buildUserInterfacePage());
    m_pages->addWidget(buildCanvasPage());
    m_pages->addWidget(buildHistoryPage());
    m_pages->addWidget(buildPlaceholderPage(
        QStringLiteral("Brush width ( [ and ] to adjust )")));
    m_pages->addWidget(buildPlaceholderPage(
        QStringLiteral("Tip: F5–F8 show/hide the Tools, History, ")));
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

    // ---- Window system: XWayland or native Wayland ----
    const bool waylandSession = !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
    auto *windowGroup = new QGroupBox(QStringLiteral("Window System"));
    auto *windowLayout = new QVBoxLayout(windowGroup);
    auto *current = new QLabel(QStringLiteral("Running on: %1")
                                   .arg(DisplayServer::describe(platform, waylandSession)));
    current->setObjectName("windowSystem");
    windowLayout->addWidget(current);

    auto *native = new QCheckBox(QStringLiteral("Run natively on Wayland (experimental)"));
    native->setObjectName("nativeWayland");
    native->setToolTip(QStringLiteral(
        "Sharper with fractional display scaling, and pen input goes to Wayland directly. "
        "Floating panels are then placed by the desktop, not by the app."));
    native->setChecked(DisplayServer::nativeWaylandEnabled());
    native->setEnabled(waylandSession);
    windowLayout->addWidget(native);

    auto *note = new QLabel;
    note->setObjectName("nativeWaylandNote");
    note->setWordWrap(true);
    if (!waylandSession)
        note->setText(QStringLiteral("Only applies in a Wayland session."));
    else if (DisplayServer::platformForcedAtStartup())
        note->setText(QStringLiteral("This time QT_QPA_PLATFORM or -platform decided."));
    else
        note->setText(QStringLiteral("Applies the next time Paint.with.me starts."));
    windowLayout->addWidget(note);
    connect(native, &QCheckBox::toggled, this, [](bool checked) {
        DisplayServer::setNativeWaylandEnabled(checked);
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

    layout->addStretch();
    return page;
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

QWidget *SettingsDialog::buildPlaceholderPage(const QString &text) {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignTop);
    layout->addWidget(label);
    layout->addStretch();
    return page;
}
