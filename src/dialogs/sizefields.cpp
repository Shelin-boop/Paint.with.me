#include "sizefields.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace {

QDoubleSpinBox *makeDecimalSpin() {
    auto *spin = new QDoubleSpinBox;
    // Commit on Enter or focus-out only. Live tracking rewrote the field
    // while "8.5" was still being typed and dropped the ".5" (issue #14).
    spin->setKeyboardTracking(false);
    spin->setMinimumWidth(110);
    return spin;
}

}  // namespace

SizeFields::SizeFields(QSize original, double dpi, Options options, QWidget *parent)
    : QWidget(parent),
      m_original(original.expandedTo(QSize(1, 1))),
      m_px(m_original),
      m_dpi(dpi > 0 ? dpi : 96.0),
      m_ratio(double(m_original.width()) / m_original.height()),
      m_length(Units::savedLength()),
      m_resUnit(Units::savedResolution()),
      m_options(options) {
    m_printIn = QSizeF(m_px.width() / m_dpi, m_px.height() / m_dpi);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    if (options.percent) {
        auto *modeRow = new QGridLayout;
        modeRow->setHorizontalSpacing(6);
        m_percentRB = new QRadioButton(QStringLiteral("By &percentage:"));
        m_percentSpin = makeDecimalSpin();
        m_percentSpin->setRange(0.01, 10000.0);
        m_percentSpin->setDecimals(2);
        m_percentSpin->setSuffix(QStringLiteral(" %"));
        m_percentSpin->setValue(100.0);
        m_absoluteRB = new QRadioButton(QStringLiteral("By &absolute size:"));
        m_absoluteRB->setChecked(true);
        modeRow->addWidget(m_percentRB, 0, 0);
        modeRow->addWidget(m_percentSpin, 0, 1);
        modeRow->addWidget(m_absoluteRB, 1, 0, 1, 2);
        modeRow->setColumnStretch(2, 1);
        auto *modeGroup = new QButtonGroup(this);
        modeGroup->addButton(m_percentRB);
        modeGroup->addButton(m_absoluteRB);
        layout->addLayout(modeRow);
        connect(modeGroup, &QButtonGroup::buttonClicked, this, [this] { onMode(); });
        connect(m_percentSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                &SizeFields::onPercent);
    }

    m_aspect = new QCheckBox(QStringLiteral("&Maintain aspect ratio"));
    m_aspect->setChecked(options.keepAspect);
    layout->addWidget(m_aspect);

    // Pixel size
    auto *pixelGroup = new QGroupBox(QStringLiteral("Pixel size"));
    auto *pixelGrid = new QGridLayout(pixelGroup);
    pixelGrid->setHorizontalSpacing(6);
    pixelGrid->addWidget(new QLabel(QStringLiteral("Width:")), 0, 0);
    m_widthSpin = new QSpinBox;
    m_widthSpin->setRange(1, kMaxPixels);
    m_widthSpin->setMinimumWidth(110);
    pixelGrid->addWidget(m_widthSpin, 0, 1);
    pixelGrid->addWidget(new QLabel(QStringLiteral("pixels")), 0, 2);
    pixelGrid->addWidget(new QLabel(QStringLiteral("Height:")), 1, 0);
    m_heightSpin = new QSpinBox;
    m_heightSpin->setRange(1, kMaxPixels);
    m_heightSpin->setMinimumWidth(110);
    pixelGrid->addWidget(m_heightSpin, 1, 1);
    pixelGrid->addWidget(new QLabel(QStringLiteral("pixels")), 1, 2);
    if (!options.percent) {
        auto *swap = new QToolButton;
        swap->setText(QStringLiteral("Swap"));
        swap->setToolTip(QStringLiteral("Swap width and height (portrait or landscape)"));
        pixelGrid->addWidget(swap, 0, 3, 2, 1);
        connect(swap, &QToolButton::clicked, this, &SizeFields::swapWidthAndHeight);
    }
    pixelGrid->setColumnStretch(4, 1);
    layout->addWidget(pixelGroup);

    // Print size
    auto *printGroup = new QGroupBox(QStringLiteral("Print size"));
    auto *printGrid = new QGridLayout(printGroup);
    printGrid->setHorizontalSpacing(6);
    printGrid->addWidget(new QLabel(QStringLiteral("Width:")), 0, 0);
    m_printWidthSpin = makeDecimalSpin();
    printGrid->addWidget(m_printWidthSpin, 0, 1);
    m_lengthCombo = new QComboBox;
    for (Units::Length u : {Units::Length::Millimeter, Units::Length::Centimeter, Units::Length::Inch})
        m_lengthCombo->addItem(Units::name(u), int(u));
    m_lengthCombo->setToolTip(QStringLiteral("Unit of the print size"));
    printGrid->addWidget(m_lengthCombo, 0, 2);
    printGrid->addWidget(new QLabel(QStringLiteral("Height:")), 1, 0);
    m_printHeightSpin = makeDecimalSpin();
    printGrid->addWidget(m_printHeightSpin, 1, 1);
    m_heightUnitLabel = new QLabel;
    printGrid->addWidget(m_heightUnitLabel, 1, 2);
    printGrid->addWidget(new QLabel(QStringLiteral("Resolution:")), 2, 0);
    m_resSpin = makeDecimalSpin();
    m_resSpin->setDecimals(2);
    printGrid->addWidget(m_resSpin, 2, 1);
    m_resCombo = new QComboBox;
    m_resCombo->addItem(Units::name(Units::Resolution::PerInch), int(Units::Resolution::PerInch));
    m_resCombo->addItem(Units::name(Units::Resolution::PerCentimeter), int(Units::Resolution::PerCentimeter));
    printGrid->addWidget(m_resCombo, 2, 2);

    auto *keepLabel = new QLabel(QStringLiteral("When the resolution changes, keep:"));
    printGrid->addWidget(keepLabel, 3, 0, 1, 3);
    auto *keepRow = new QHBoxLayout;
    m_keepPixelRB = new QRadioButton(QStringLiteral("Pi&xel size"));
    m_keepPrintRB = new QRadioButton(QStringLiteral("P&rint size"));
    m_keepPixelRB->setToolTip(QStringLiteral(
        "A new resolution only changes how large the image prints; the pixels stay."));
    m_keepPrintRB->setToolTip(QStringLiteral(
        "A new resolution changes the pixel size so the print size stays as typed."));
    keepRow->addWidget(m_keepPixelRB);
    keepRow->addWidget(m_keepPrintRB);
    keepRow->addStretch();
    printGrid->addLayout(keepRow, 4, 0, 1, 3);
    auto *keepGroup = new QButtonGroup(this);
    keepGroup->addButton(m_keepPixelRB);
    keepGroup->addButton(m_keepPrintRB);
    printGrid->setColumnStretch(3, 1);
    layout->addWidget(printGroup);

    m_summary = new QLabel;
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    connect(m_aspect, &QCheckBox::toggled, this, &SizeFields::onAspectToggled);
    connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &SizeFields::onPixelWidth);
    connect(m_heightSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &SizeFields::onPixelHeight);
    connect(m_printWidthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &SizeFields::onPrintWidth);
    connect(m_printHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &SizeFields::onPrintHeight);
    connect(m_resSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SizeFields::onResolution);
    connect(m_lengthCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        setLengthUnit(Units::Length(m_lengthCombo->itemData(index).toInt()));
        Units::saveLength(m_length);
    });
    connect(m_resCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        setResolutionUnit(Units::Resolution(m_resCombo->itemData(index).toInt()));
        Units::saveResolution(m_resUnit);
    });
    connect(m_keepPixelRB, &QRadioButton::clicked, this, [this] { setKeepInternal(Keep::PixelSize); refresh(); });
    connect(m_keepPrintRB, &QRadioButton::clicked, this, [this] { setKeepInternal(Keep::PrintSize); refresh(); });

    refresh();
}

bool SizeFields::keepsAspect() const { return m_aspect->isChecked(); }

bool SizeFields::isValid() const { return !m_overflow; }

double SizeFields::shownPrintWidth() const { return m_printWidthSpin->value(); }
double SizeFields::shownPrintHeight() const { return m_printHeightSpin->value(); }
double SizeFields::shownResolution() const { return m_resSpin->value(); }

void SizeFields::setKeepInternal(Keep keep) { m_keep = keep; }

void SizeFields::printFromPixels() {
    m_printIn = QSizeF(m_px.width() / m_dpi, m_px.height() / m_dpi);
    m_overflow = false;
}

void SizeFields::pixelsFromPrint() {
    const double w = m_printIn.width() * m_dpi;
    const double h = m_printIn.height() * m_dpi;
    m_overflow = !(w < kMaxPixels + 0.5 && h < kMaxPixels + 0.5);
    auto clampPx = [](double v) { return int(qBound<double>(1.0, std::round(v), kMaxPixels)); };
    m_px = QSize(clampPx(w), clampPx(h));
}

void SizeFields::onPixelWidth(int v) {
    setKeepInternal(Keep::PixelSize);
    m_px.setWidth(v);
    if (keepsAspect()) m_px.setHeight(qBound(1, int(std::lround(v / m_ratio)), kMaxPixels));
    printFromPixels();
    refresh();
}

void SizeFields::onPixelHeight(int v) {
    setKeepInternal(Keep::PixelSize);
    m_px.setHeight(v);
    if (keepsAspect()) m_px.setWidth(qBound(1, int(std::lround(v * m_ratio)), kMaxPixels));
    printFromPixels();
    refresh();
}

void SizeFields::onPrintWidth(double v) {
    setKeepInternal(Keep::PrintSize);
    m_printIn.setWidth(Units::toInches(v, m_length));
    if (keepsAspect()) m_printIn.setHeight(m_printIn.width() / m_ratio);
    pixelsFromPrint();
    refresh();
}

void SizeFields::onPrintHeight(double v) {
    setKeepInternal(Keep::PrintSize);
    m_printIn.setHeight(Units::toInches(v, m_length));
    if (keepsAspect()) m_printIn.setWidth(m_printIn.height() * m_ratio);
    pixelsFromPrint();
    refresh();
}

void SizeFields::onResolution(double v) {
    const double dpi = Units::toDpi(v, m_resUnit);
    if (dpi <= 0) return;
    m_dpi = dpi;
    if (m_keep == Keep::PrintSize)
        pixelsFromPrint();
    else
        printFromPixels();
    refresh();
}

void SizeFields::onPercent(double v) {
    if (!m_percentRB || !m_percentRB->isChecked()) return;
    setKeepInternal(Keep::PixelSize);
    const double w = m_original.width() * v / 100.0;
    const double h = m_original.height() * v / 100.0;
    m_overflow = !(w < kMaxPixels + 0.5 && h < kMaxPixels + 0.5);
    m_px = QSize(qBound(1, int(std::lround(w)), kMaxPixels), qBound(1, int(std::lround(h)), kMaxPixels));
    m_printIn = QSizeF(m_px.width() / m_dpi, m_px.height() / m_dpi);
    refresh();
}

void SizeFields::onMode() {
    if (m_percentRB && m_percentRB->isChecked())
        onPercent(m_percentSpin->value());
    else
        refresh();
}

void SizeFields::onAspectToggled(bool on) {
    if (on) {
        if (m_keep == Keep::PrintSize && m_printIn.height() > 0)
            m_ratio = m_printIn.width() / m_printIn.height();
        else
            m_ratio = double(m_px.width()) / m_px.height();
    }
    emit changed();
}

void SizeFields::setPixelSize(QSize size) {
    setKeepInternal(Keep::PixelSize);
    m_px = size.expandedTo(QSize(1, 1)).boundedTo(QSize(kMaxPixels, kMaxPixels));
    m_ratio = double(m_px.width()) / m_px.height();
    printFromPixels();
    refresh();
}

void SizeFields::setPrintSize(QSizeF size, Units::Length unit) {
    m_length = unit;
    setKeepInternal(Keep::PrintSize);
    m_printIn = QSizeF(Units::toInches(size.width(), unit), Units::toInches(size.height(), unit));
    if (m_printIn.height() > 0) m_ratio = m_printIn.width() / m_printIn.height();
    pixelsFromPrint();
    refresh();
}

void SizeFields::setResolution(double value, Units::Resolution unit) {
    m_resUnit = unit;
    onResolution(value);
}

void SizeFields::setPercent(double percent) {
    if (!m_percentRB) return;
    m_percentRB->setChecked(true);
    {
        QSignalBlocker block(m_percentSpin);
        m_percentSpin->setValue(percent);
    }
    onPercent(m_percentSpin->value());
}

void SizeFields::setLengthUnit(Units::Length unit) {
    m_length = unit;
    refresh();
}

void SizeFields::setResolutionUnit(Units::Resolution unit) {
    m_resUnit = unit;
    refresh();
}

void SizeFields::setKeepAspect(bool keep) { m_aspect->setChecked(keep); }

void SizeFields::setKeep(Keep keep) {
    setKeepInternal(keep);
    refresh();
}

void SizeFields::swapWidthAndHeight() {
    m_px.transpose();
    m_printIn.transpose();
    m_ratio = m_ratio > 0 ? 1.0 / m_ratio : 1.0;
    refresh();
}

void SizeFields::refresh() {
    const bool percent = m_percentRB && m_percentRB->isChecked();
    const QSignalBlocker b1(m_widthSpin), b2(m_heightSpin), b3(m_printWidthSpin), b4(m_printHeightSpin),
        b5(m_resSpin), b6(m_lengthCombo), b7(m_resCombo);

    m_widthSpin->setValue(m_px.width());
    m_heightSpin->setValue(m_px.height());

    const int dec = Units::decimals(m_length);
    const double maxLength = 1.0e7;
    for (QDoubleSpinBox *spin : {m_printWidthSpin, m_printHeightSpin}) {
        spin->setDecimals(dec);
        spin->setRange(std::pow(10.0, -dec), maxLength);
    }
    m_printWidthSpin->setValue(Units::fromInches(m_printIn.width(), m_length));
    m_printHeightSpin->setValue(Units::fromInches(m_printIn.height(), m_length));
    m_lengthCombo->setCurrentIndex(m_lengthCombo->findData(int(m_length)));
    m_heightUnitLabel->setText(Units::name(m_length));

    if (m_resUnit == Units::Resolution::PerCentimeter)
        m_resSpin->setRange(0.04, 39370.0);
    else
        m_resSpin->setRange(0.1, 99999.0);
    m_resSpin->setValue(Units::fromDpi(m_dpi, m_resUnit));
    m_resCombo->setCurrentIndex(m_resCombo->findData(int(m_resUnit)));

    m_keepPixelRB->setChecked(m_keep == Keep::PixelSize);
    m_keepPrintRB->setChecked(m_keep == Keep::PrintSize);

    if (m_percentSpin && !percent) {
        const QSignalBlocker bp(m_percentSpin);
        m_percentSpin->setValue(100.0 * m_px.width() / m_original.width());
    }
    if (m_percentSpin) m_percentSpin->setEnabled(percent);
    m_widthSpin->setEnabled(!percent);
    m_heightSpin->setEnabled(!percent);
    m_printWidthSpin->setEnabled(!percent);
    m_printHeightSpin->setEnabled(!percent);

    if (m_overflow) {
        m_summary->setText(QStringLiteral("Too large: an image can be at most %1 pixels wide and high. "
                                          "Choose a smaller size or a lower resolution.")
                               .arg(kMaxPixels));
        m_summary->setStyleSheet(QStringLiteral("color: #c0392b;"));
    } else {
        m_summary->setText(QStringLiteral("New size: ")
                           + Units::describe(m_px.width(), m_px.height(), m_dpi, m_length));
        m_summary->setStyleSheet(QString());
    }
    emit changed();
}
