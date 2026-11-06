#include "newdocumentdialog.h"
#include "sizefields.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

namespace {

struct Preset {
    const char *label;
    double width;
    double height;
    int unit;   // -1 = pixels, else Units::Length
};

// Paper sizes are portrait; "Swap" turns them to landscape.
const Preset kPresets[] = {
    {"640 x 480", 640, 480, -1},
    {"800 x 600", 800, 600, -1},
    {"1024 x 768", 1024, 768, -1},
    {"1280 x 720 (HD)", 1280, 720, -1},
    {"1920 x 1080 (Full HD)", 1920, 1080, -1},
    {"2560 x 1440 (QHD)", 2560, 1440, -1},
    {"3840 x 2160 (4K UHD)", 3840, 2160, -1},
    {nullptr, 0, 0, 0},   // separator
    {"A3 (297 x 420 mm)", 297, 420, int(Units::Length::Millimeter)},
    {"A4 (210 x 297 mm)", 210, 297, int(Units::Length::Millimeter)},
    {"A5 (148 x 210 mm)", 148, 210, int(Units::Length::Millimeter)},
    {"A6 (105 x 148 mm)", 105, 148, int(Units::Length::Millimeter)},
    {"US Letter (8.5 x 11 in)", 8.5, 11, int(Units::Length::Inch)},
    {"US Legal (8.5 x 14 in)", 8.5, 14, int(Units::Length::Inch)},
    {"Photo 10 x 15 cm", 10, 15, int(Units::Length::Centimeter)},
    {"Photo 13 x 18 cm", 13, 18, int(Units::Length::Centimeter)},
};

}  // namespace

NewDocumentDialog::NewDocumentDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("New"));
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    auto *presetLayout = new QHBoxLayout;
    presetLayout->addWidget(new QLabel(QStringLiteral("Preset:")));
    m_presetCombo = new QComboBox;
    m_presetCombo->addItem(QStringLiteral("Custom"), -1);
    for (int i = 0; i < int(std::size(kPresets)); ++i) {
        if (!kPresets[i].label)
            m_presetCombo->insertSeparator(m_presetCombo->count());
        else
            m_presetCombo->addItem(QString::fromLatin1(kPresets[i].label), i);
    }
    m_presetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    presetLayout->addWidget(m_presetCombo);
    layout->addLayout(presetLayout);

    SizeFields::Options options;
    options.percent = false;
    options.keepAspect = false;
    m_fields = new SizeFields(QSize(800, 600), 96.0, options);
    layout->addWidget(m_fields);

    auto *bgLayout = new QHBoxLayout;
    bgLayout->addWidget(new QLabel(QStringLiteral("Background:")));
    m_colorBtn = new QPushButton;
    m_colorBtn->setFixedSize(80, 24);
    m_colorBtn->setStyleSheet("background-color: white; border: 1px solid #aaa;");
    bgLayout->addWidget(m_colorBtn);
    m_colorLabel = new QLabel(QStringLiteral("White"));
    bgLayout->addWidget(m_colorLabel);
    bgLayout->addStretch();
    layout->addLayout(bgLayout);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);

    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &NewDocumentDialog::onPresetChanged);
    connect(m_colorBtn, &QPushButton::clicked, this, &NewDocumentDialog::onChooseColor);
    connect(m_fields, &SizeFields::changed, this, [this] {
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(m_fields->isValid());
        // Typing a different size leaves the preset ("Custom"); a new
        // resolution for a paper size keeps it (A4 stays A4).
        if (!m_applyingPreset && m_presetCombo->currentIndex() != 0 && !matchesPreset()) {
            const QSignalBlocker block(m_presetCombo);
            m_presetCombo->setCurrentIndex(0);
        }
    });
    setMinimumWidth(440);
}

int NewDocumentDialog::imageWidth() const { return m_fields->pixelSize().width(); }
int NewDocumentDialog::imageHeight() const { return m_fields->pixelSize().height(); }
double NewDocumentDialog::resolution() const { return m_fields->dpi(); }

void NewDocumentDialog::onPresetChanged(int index) {
    const int i = m_presetCombo->itemData(index).toInt();
    if (i < 0 || i >= int(std::size(kPresets)) || !kPresets[i].label) return;
    const Preset &p = kPresets[i];
    m_applyingPreset = true;
    if (p.unit < 0)
        m_fields->setPixelSize(QSize(int(p.width), int(p.height)));
    else
        m_fields->setPrintSize(QSizeF(p.width, p.height), Units::Length(p.unit));
    m_applyingPreset = false;
}

bool NewDocumentDialog::matchesPreset() const {
    const int i = m_presetCombo->currentData().toInt();
    if (i < 0 || i >= int(std::size(kPresets)) || !kPresets[i].label) return false;
    const Preset &p = kPresets[i];
    if (p.unit < 0)
        return m_fields->pixelSize() == QSize(int(p.width), int(p.height));
    const Units::Length unit = Units::Length(p.unit);
    if (m_fields->keep() != SizeFields::Keep::PrintSize) return false;
    const QSizeF printed = m_fields->printSizeInches();
    return std::abs(printed.width() - Units::toInches(p.width, unit)) < 1e-9
        && std::abs(printed.height() - Units::toInches(p.height, unit)) < 1e-9;
}

void NewDocumentDialog::onChooseColor() {
    QColor c = QColorDialog::getColor(m_bgColor, this, QStringLiteral("Background color"));
    if (c.isValid()) {
        m_bgColor = c;
        m_colorBtn->setStyleSheet(QString("background-color: %1; border: 1px solid #aaa;").arg(c.name()));
        m_colorLabel->setText(c.name());
    }
}
