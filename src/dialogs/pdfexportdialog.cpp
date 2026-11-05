#include "dialogs/pdfexportdialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include "appinfo.h"

PdfExportDialog::PdfExportDialog(const QSize &imageSize, double dpi, int layerCount, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Export as PDF"));
    QSettings s(AppInfo::kId, AppInfo::kId);
    auto *root = new QVBoxLayout(this);

    // Page size follows the image's print size, as in GIMP.
    const double wmm = PdfExport::millimeters(imageSize.width(), dpi);
    const double hmm = PdfExport::millimeters(imageSize.height(), dpi);
    auto *size = new QLabel(QStringLiteral(
        "Page size: %1 x %2 mm (%3 x %4 in), from %5 x %6 px at %7 dpi. "
        "Change the resolution in Image > Resize.")
        .arg(wmm, 0, 'f', 1).arg(hmm, 0, 'f', 1)
        .arg(wmm / 25.4, 0, 'f', 2).arg(hmm / 25.4, 0, 'f', 2)
        .arg(imageSize.width()).arg(imageSize.height()).arg(dpi, 0, 'f', 0));
    size->setObjectName("pdfPageSize");
    size->setWordWrap(true);
    root->addWidget(size);

    auto *pages = new QGroupBox(QStringLiteral("Pages"));
    auto *pagesLayout = new QVBoxLayout(pages);
    m_wholeImage = new QRadioButton(QStringLiteral("The image, as it looks (one page)"));
    m_layersAsPages = new QRadioButton(QStringLiteral("One page per layer (%1 layers)").arg(layerCount));
    m_layersAsPages->setObjectName("pdfLayersAsPages");
    m_topLayerFirst = new QCheckBox(QStringLiteral("Top layer first"));
    m_omitHidden = new QCheckBox(QStringLiteral("Leave out hidden layers"));
    auto *pageGroup = new QButtonGroup(this);
    pageGroup->addButton(m_wholeImage);
    pageGroup->addButton(m_layersAsPages);
    pagesLayout->addWidget(m_wholeImage);
    pagesLayout->addWidget(m_layersAsPages);
    auto *layerOptions = new QVBoxLayout;
    layerOptions->setContentsMargins(24, 0, 0, 0);
    layerOptions->addWidget(m_topLayerFirst);
    layerOptions->addWidget(m_omitHidden);
    pagesLayout->addLayout(layerOptions);
    root->addWidget(pages);

    auto *images = new QGroupBox(QStringLiteral("Pictures"));
    auto *imagesLayout = new QVBoxLayout(images);
    m_lossless = new QRadioButton(QStringLiteral("Lossless, every pixel exact (best for print)"));
    auto *jpegRow = new QHBoxLayout;
    m_jpeg = new QRadioButton(QStringLiteral("JPEG (smaller file), quality:"));
    m_jpeg->setObjectName("pdfJpeg");
    m_quality = new QSpinBox;
    m_quality->setRange(10, 100);
    m_quality->setSuffix(QStringLiteral(" %"));
    jpegRow->addWidget(m_jpeg);
    jpegRow->addWidget(m_quality);
    jpegRow->addStretch();
    auto *compressionGroup = new QButtonGroup(this);
    compressionGroup->addButton(m_lossless);
    compressionGroup->addButton(m_jpeg);
    imagesLayout->addWidget(m_lossless);
    imagesLayout->addLayout(jpegRow);
    m_whiteBackground = new QCheckBox(QStringLiteral("White background instead of transparency"));
    m_whiteBackground->setToolTip(QStringLiteral("Some print shops want PDFs without transparency."));
    imagesLayout->addWidget(m_whiteBackground);
    root->addWidget(images);

    // Remember the last choices.
    (s.value("pdf/layersAsPages", false).toBool() && layerCount > 1 ? m_layersAsPages : m_wholeImage)->setChecked(true);
    m_layersAsPages->setEnabled(layerCount > 1);
    m_topLayerFirst->setChecked(s.value("pdf/topLayerFirst", true).toBool());
    m_omitHidden->setChecked(s.value("pdf/omitHidden", true).toBool());
    (s.value("pdf/jpeg", false).toBool() ? m_jpeg : m_lossless)->setChecked(true);
    m_quality->setValue(s.value("pdf/jpegQuality", 92).toInt());
    m_whiteBackground->setChecked(s.value("pdf/whiteBackground", false).toBool());

    connect(m_layersAsPages, &QRadioButton::toggled, this, &PdfExportDialog::updateEnabled);
    connect(m_jpeg, &QRadioButton::toggled, this, &PdfExportDialog::updateEnabled);
    updateEnabled();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Export"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        QSettings st(AppInfo::kId, AppInfo::kId);
        const PdfExport::Options o = options();
        st.setValue("pdf/layersAsPages", o.layersAsPages);
        st.setValue("pdf/topLayerFirst", o.topLayerFirst);
        st.setValue("pdf/omitHidden", o.omitHidden);
        st.setValue("pdf/jpeg", o.jpeg);
        st.setValue("pdf/jpegQuality", o.jpegQuality);
        st.setValue("pdf/whiteBackground", o.whiteBackground);
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

void PdfExportDialog::updateEnabled() {
    m_topLayerFirst->setEnabled(m_layersAsPages->isChecked());
    m_omitHidden->setEnabled(m_layersAsPages->isChecked());
    m_quality->setEnabled(m_jpeg->isChecked());
}

PdfExport::Options PdfExportDialog::options() const {
    PdfExport::Options o;
    o.layersAsPages = m_layersAsPages->isChecked();
    o.topLayerFirst = m_topLayerFirst->isChecked();
    o.omitHidden = m_omitHidden->isChecked();
    o.jpeg = m_jpeg->isChecked();
    o.jpegQuality = m_quality->value();
    o.whiteBackground = m_whiteBackground->isChecked();
    return o;
}
