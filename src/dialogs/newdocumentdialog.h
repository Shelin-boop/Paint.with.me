#pragma once
#include <QDialog>
#include <QColor>

class QComboBox;
class QPushButton;
class QLabel;
class QDialogButtonBox;
class SizeFields;

// File > New: size in pixels or as a print size (mm, cm, in) at a
// resolution, screen and paper presets, background color.
class NewDocumentDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewDocumentDialog(QWidget *parent = nullptr);
    int imageWidth() const;
    int imageHeight() const;
    double resolution() const;   // pixels per inch
    QColor backgroundColor() const { return m_bgColor; }
    SizeFields *sizeFields() const { return m_fields; }
    QComboBox *presetCombo() const { return m_presetCombo; }

private:
    void onPresetChanged(int index);
    void onChooseColor();
    bool matchesPreset() const;

    SizeFields *m_fields;
    QComboBox *m_presetCombo;
    QPushButton *m_colorBtn;
    QLabel *m_colorLabel;
    QDialogButtonBox *m_buttons;
    QColor m_bgColor = Qt::white;
    bool m_applyingPreset = false;
};
