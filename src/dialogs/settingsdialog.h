#pragma once

#include <QDialog>

class QListWidget;
class QStackedWidget;
class QComboBox;
class QCheckBox;
class QSlider;
class QSpinBox;
class QLabel;

// Mirrors paint.net's Settings dialog: a category list on the left, a stacked
// page on the right, and a Close button.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    // Shown on the Undo History page.
    void setHistoryUsage(qint64 bytes, int steps);

signals:
    // Emitted as soon as the user picks a new color scheme, so the
    // application can restyle live.
    void colorSchemeChanged();
    void canvasSettingsChanged();
    void panelBehaviorChanged();
    void historySettingsChanged();

private:
    QWidget *buildUserInterfacePage();
    QWidget *buildCanvasPage();
    QWidget *buildHistoryPage();
    QWidget *buildPlaceholderPage(const QString &text);

    QListWidget *m_categories;
    QStackedWidget *m_pages;

    QComboBox *m_schemeCombo;
    QSlider *m_checkerSlider;
    QSpinBox *m_historyLimit = nullptr;
    QLabel *m_historyUsage = nullptr;
};
