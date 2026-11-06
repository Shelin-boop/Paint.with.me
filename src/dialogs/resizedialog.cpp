#include "resizedialog.h"
#include "sizefields.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ResizeDialog::ResizeDialog(int currentWidth, int currentHeight, double dpi, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Resize"));
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    SizeFields::Options options;
    options.percent = true;
    options.keepAspect = true;
    m_fields = new SizeFields(QSize(currentWidth, currentHeight), dpi, options);
    layout->addWidget(m_fields);

    auto *resampleLayout = new QHBoxLayout;
    resampleLayout->addWidget(new QLabel(QStringLiteral("Resampling:")));
    m_resampleCombo = new QComboBox;
    m_resampleCombo->addItem(QStringLiteral("Best Quality"));
    m_resampleCombo->addItem(QStringLiteral("Bicubic"));
    m_resampleCombo->addItem(QStringLiteral("Bilinear"));
    m_resampleCombo->addItem(QStringLiteral("Fant"));
    m_resampleCombo->addItem(QStringLiteral("Nearest Neighbor"));
    m_resampleCombo->addItem(QStringLiteral("Super Sampling"));
    resampleLayout->addWidget(m_resampleCombo, 1);
    layout->addLayout(resampleLayout);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);

    connect(m_fields, &SizeFields::changed, this, [this] {
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(m_fields->isValid());
    });
    setMinimumWidth(440);
}

int ResizeDialog::newWidth() const { return m_fields->pixelSize().width(); }
int ResizeDialog::newHeight() const { return m_fields->pixelSize().height(); }
int ResizeDialog::resampleMethod() const { return m_resampleCombo->currentIndex(); }
double ResizeDialog::resolution() const { return m_fields->dpi(); }
