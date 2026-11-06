#pragma once
#include <QDialog>

class QComboBox;
class QDialogButtonBox;
class SizeFields;

// Image > Resize: scales every layer to a new pixel size. The size can be
// given in pixels, as a percentage, or as a print size (mm, cm, in) at a
// resolution.
class ResizeDialog : public QDialog {
    Q_OBJECT
public:
    explicit ResizeDialog(int currentWidth, int currentHeight, double dpi = 96.0, QWidget *parent = nullptr);

    int newWidth() const;
    int newHeight() const;
    int resampleMethod() const;
    double resolution() const;   // pixels per inch
    SizeFields *sizeFields() const { return m_fields; }

private:
    SizeFields *m_fields;
    QComboBox *m_resampleCombo;
    QDialogButtonBox *m_buttons;
};
