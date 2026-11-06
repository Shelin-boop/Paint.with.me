#pragma once

#include <QDialog>

class QListWidget;
class QStackedWidget;
class QComboBox;
class QCheckBox;
class QSlider;
class QSpinBox;
class QLabel;
class Shortcuts;

// Mirrors paint.net's Settings dialog: a category list on the left, a stacked
// page on the right, and a Close button.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    // Without `shortcuts`, the Keyboard page only says where keys are set.
    explicit SettingsDialog(QWidget *parent = nullptr, Shortcuts *shortcuts = nullptr);
    // Shown on the Undo History page.
    void setHistoryUsage(qint64 bytes, int steps);
    // "Restart Now" was chosen after changing the window system; the dialog
    // has closed itself and the caller restarts the app.
    bool restartRequested() const { return m_restartRequested; }

signals:
    // Emitted as soon as the user picks a new color scheme, so the
    // application can restyle live.
    void colorSchemeChanged();
    void canvasSettingsChanged();
    void panelBehaviorChanged();
    void historySettingsChanged();
    // Antialiasing, pressure curve or tilt changed (Tools page).
    void toolSettingsChanged();

private:
    QWidget *buildUserInterfacePage();
    QWidget *buildCanvasPage();
    QWidget *buildHistoryPage();
    QWidget *buildToolsPage();
    QWidget *buildKeyboardPage();
    QWidget *buildHelpPage();
    void askForRestart(const QString &windowSystem);

    QListWidget *m_categories;
    QStackedWidget *m_pages;

    QComboBox *m_schemeCombo;
    QSlider *m_checkerSlider;
    QSpinBox *m_historyLimit = nullptr;
    QLabel *m_historyUsage = nullptr;
    bool m_restartRequested = false;
    Shortcuts *m_shortcuts = nullptr;
};
