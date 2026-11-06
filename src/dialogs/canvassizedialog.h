#pragma once
#include <QDialog>
#include <QToolButton>
#include <QButtonGroup>

class QDialogButtonBox;
class SizeFields;

// Image > Canvas Size: adds or removes space around the image without
// scaling it. The size can be given in pixels, as a percentage, or as a
// print size (mm, cm, in) at a resolution.
class CanvasSizeDialog : public QDialog {
    Q_OBJECT
public:
    explicit CanvasSizeDialog(int currentWidth, int currentHeight, double dpi = 96.0, QWidget *parent = nullptr);
    int newWidth() const;
    int newHeight() const;
    double resolution() const;   // pixels per inch
    int anchorX() const;
    int anchorY() const;
    void setAnchor(int index);   // 0 = top left ... 8 = bottom right
    SizeFields *sizeFields() const { return m_fields; }

private:
    SizeFields *m_fields;
    QButtonGroup *m_anchorGroup;
    QToolButton *m_anchorButtons[9];
    QDialogButtonBox *m_buttons;
    int m_anchorIndex = 0;
    int m_currentWidth;
    int m_currentHeight;
};
