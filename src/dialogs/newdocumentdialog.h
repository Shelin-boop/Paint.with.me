#pragma once
#include <QDialog>
#include <QColor>

class QComboBox;
class QPushButton;
class QLabel;
class QDialogButtonBox;
class SizeFields;

// File > New: size in pixels or as a print size (mm, cm, in) at a
// resolution, screen and paper presets, background color. With a picture on
// the clipboard, it starts at the picture's size (preset "Clipboard").
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
    // A picture of this size is on the clipboard: offered as the first preset
    // and chosen. `dpi` > 0: its resolution (copied from Paint.with.me).
    void setClipboardPicture(QSize size, double dpi = 0);

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
    QSize m_clipboardSize;
    double m_clipboardDpi = 0;
};
