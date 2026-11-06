#include "canvassizedialog.h"
#include "sizefields.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

CanvasSizeDialog::CanvasSizeDialog(int currentWidth, int currentHeight, double dpi, QWidget *parent)
    : QDialog(parent), m_currentWidth(currentWidth), m_currentHeight(currentHeight) {
    setWindowTitle(QStringLiteral("Canvas Size"));
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    SizeFields::Options options;
    options.percent = true;
    options.keepAspect = false;
    m_fields = new SizeFields(QSize(currentWidth, currentHeight), dpi, options);
    layout->addWidget(m_fields);

    // Anchor 3x3 grid (like Paint.NET)
    auto *anchorGroup = new QGroupBox(QStringLiteral("Anchor"));
    auto *anchorLayout = new QVBoxLayout(anchorGroup);
    auto *anchorGrid = new QGridLayout;
    anchorGrid->setSpacing(2);
    m_anchorGroup = new QButtonGroup(this);
    const char *arrows[] = {
        "\xe2\x86\x96", "\xe2\x86\x91", "\xe2\x86\x97",  // top left, top, top right
        "\xe2\x86\x90", "\xe2\x97\x8f", "\xe2\x86\x92",  // left, center, right
        "\xe2\x86\x99", "\xe2\x86\x93", "\xe2\x86\x98"   // bottom left, bottom, bottom right
    };
    const char *names[] = {"Top left", "Top", "Top right", "Left", "Center", "Right",
                           "Bottom left", "Bottom", "Bottom right"};
    for (int i = 0; i < 9; ++i) {
        m_anchorButtons[i] = new QToolButton;
        m_anchorButtons[i]->setText(QString::fromUtf8(arrows[i]));
        m_anchorButtons[i]->setToolTip(QString::fromLatin1(names[i]));
        m_anchorButtons[i]->setFixedSize(32, 32);
        m_anchorButtons[i]->setCheckable(true);
        m_anchorButtons[i]->setStyleSheet(
            "QToolButton { border: 1px solid #aaa; background: #f0f0f0; font-size: 14px; }"
            "QToolButton:checked { background: #4a90d9; color: white; border: 1px solid #2a5a9a; }"
            "QToolButton:hover { background: #d0e0f0; }");
        m_anchorGroup->addButton(m_anchorButtons[i], i);
        anchorGrid->addWidget(m_anchorButtons[i], i / 3, i % 3);
    }
    m_anchorButtons[0]->setChecked(true);   // top left
    anchorGrid->setColumnStretch(3, 1);
    anchorLayout->addLayout(anchorGrid);
    layout->addWidget(anchorGroup);

    auto *infoLabel = new QLabel(QStringLiteral("The new space will be filled with the secondary color."));
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: #555; font-size: 11px; font-style: italic;");
    layout->addWidget(infoLabel);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);

    connect(m_anchorGroup, &QButtonGroup::idClicked, this, &CanvasSizeDialog::setAnchor);
    connect(m_fields, &SizeFields::changed, this, [this] {
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(m_fields->isValid());
    });
    setMinimumWidth(440);
}

int CanvasSizeDialog::newWidth() const { return m_fields->pixelSize().width(); }
int CanvasSizeDialog::newHeight() const { return m_fields->pixelSize().height(); }
double CanvasSizeDialog::resolution() const { return m_fields->dpi(); }

void CanvasSizeDialog::setAnchor(int index) {
    m_anchorIndex = qBound(0, index, 8);
    m_anchorButtons[m_anchorIndex]->setChecked(true);
}

int CanvasSizeDialog::anchorX() const {
    const int col = m_anchorIndex % 3;
    const int dw = newWidth() - m_currentWidth;
    return col == 0 ? 0 : col == 1 ? dw / 2 : dw;
}

int CanvasSizeDialog::anchorY() const {
    const int row = m_anchorIndex / 3;
    const int dh = newHeight() - m_currentHeight;
    return row == 0 ? 0 : row == 1 ? dh / 2 : dh;
}
