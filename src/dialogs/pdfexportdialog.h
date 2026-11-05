#pragma once

#include <QDialog>

#include "core/pdfexport.h"

class QCheckBox;
class QLabel;
class QRadioButton;
class QSpinBox;

// File > Export as PDF... options, after the file name is chosen (like
// GIMP's): the image or one page per layer, lossless or JPEG, transparency.
class PdfExportDialog : public QDialog {
    Q_OBJECT
public:
    PdfExportDialog(const QSize &imageSize, double dpi, int layerCount, QWidget *parent = nullptr);
    PdfExport::Options options() const;

private:
    void updateEnabled();

    QRadioButton *m_wholeImage;
    QRadioButton *m_layersAsPages;
    QCheckBox *m_topLayerFirst;
    QCheckBox *m_omitHidden;
    QRadioButton *m_lossless;
    QRadioButton *m_jpeg;
    QSpinBox *m_quality;
    QCheckBox *m_whiteBackground;
};
