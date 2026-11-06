#include "pdfpicturedialog.h"
#include "core/pdfpictures.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

namespace {

constexpr int kPreviewW = 440;
constexpr int kPreviewH = 320;

// The picture fitted into the preview, over a checkerboard so transparent
// parts show as such. Small pictures are enlarged without smoothing.
QPixmap previewOf(const QImage &image) {
    QPixmap pm(kPreviewW, kPreviewH);
    pm.fill(QColor(128, 128, 128));
    if (image.isNull()) return pm;
    const QSize fitted = image.size().scaled(kPreviewW - 16, kPreviewH - 16, Qt::KeepAspectRatio);
    const QRect target(QPoint((kPreviewW - fitted.width()) / 2, (kPreviewH - fitted.height()) / 2), fitted);
    QPainter p(&pm);
    for (int y = target.top(); y <= target.bottom(); y += 8)
        for (int x = target.left(); x <= target.right(); x += 8)
            p.fillRect(QRect(x, y, 8, 8).intersected(target),
                       ((x - target.left()) / 8 + (y - target.top()) / 8) % 2 ? QColor(204, 204, 204) : Qt::white);
    const bool enlarged = fitted.width() > image.width();
    p.setRenderHint(QPainter::SmoothPixmapTransform, !enlarged);
    p.drawImage(target, image);
    return pm;
}

QString formatDpi(double dpi) {
    QString s = QString::number(dpi, 'f', 2);
    while (s.endsWith('0')) s.chop(1);
    if (s.endsWith('.')) s.chop(1);
    return s;
}

}  // namespace

PdfPictureDialog::PdfPictureDialog(const PdfPictures &pdf, const QString &fileName, QWidget *parent)
    : QDialog(parent), m_pdf(pdf) {
    setWindowTitle(QStringLiteral("Extract Picture from PDF"));
    auto *layout = new QVBoxLayout(this);

    auto *heading = new QLabel(QStringLiteral("Pictures in <b>%1</b>, in the order the pages show them:")
                                   .arg(fileName.toHtmlEscaped()));
    heading->setWordWrap(true);
    layout->addWidget(heading);

    m_preview = new QLabel;
    m_preview->setFixedSize(kPreviewW, kPreviewH);
    m_preview->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_preview, 0, Qt::AlignHCenter);

    auto *nav = new QHBoxLayout;
    m_previous = new QPushButton(QStringLiteral("< &Previous"));
    m_next = new QPushButton(QStringLiteral("&Next >"));
    m_previous->setAutoDefault(false);
    m_next->setAutoDefault(false);
    m_counter = new QLabel;
    m_counter->setAlignment(Qt::AlignCenter);
    nav->addWidget(m_previous);
    nav->addWidget(m_counter, 1);
    nav->addWidget(m_next);
    layout->addLayout(nav);

    m_details = new QLabel;
    m_details->setWordWrap(true);
    m_details->setMinimumHeight(m_details->fontMetrics().height() * 2);
    layout->addWidget(m_details);

    auto *buttons = new QDialogButtonBox;
    m_open = buttons->addButton(QStringLiteral("&Open as New Image"), QDialogButtonBox::AcceptRole);
    m_copy = buttons->addButton(QStringLiteral("&Copy to Clipboard"), QDialogButtonBox::ActionRole);
    m_layer = buttons->addButton(QStringLiteral("Add as New &Layer"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    m_open->setDefault(true);
    m_open->setToolTip(QStringLiteral("Opens the picture as an image of its own, at the resolution it has in the PDF."));
    m_copy->setToolTip(QStringLiteral("Puts the picture on the clipboard, for this or any other program."));
    m_layer->setToolTip(QStringLiteral("Adds the picture as a new layer of the image that is open."));
    layout->addWidget(buttons);

    connect(m_previous, &QPushButton::clicked, this, &PdfPictureDialog::showPrevious);
    connect(m_next, &QPushButton::clicked, this, &PdfPictureDialog::showNext);
    connect(m_open, &QPushButton::clicked, this, [this] { finish(Action::OpenAsImage); });
    connect(m_copy, &QPushButton::clicked, this, [this] { finish(Action::CopyToClipboard); });
    connect(m_layer, &QPushButton::clicked, this, [this] { finish(Action::AddAsLayer); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    showPicture(0);
}

QImage PdfPictureDialog::currentImage() const {
    if (m_index < 0 || m_index >= m_pdf.pictures().size()) return QImage();
    if (!m_decoded.contains(m_index)) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString problem;
        m_decoded.insert(m_index, m_pdf.decode(m_index, &problem));
        m_problems.insert(m_index, problem);
        QApplication::restoreOverrideCursor();
    }
    return m_decoded.value(m_index);
}

double PdfPictureDialog::currentDpi() const {
    if (m_index < 0 || m_index >= m_pdf.pictures().size()) return 0;
    return PdfPictures::suggestedDpi(m_pdf.pictures().at(m_index));
}

bool PdfPictureDialog::actionsEnabled() const { return m_open->isEnabled(); }

void PdfPictureDialog::showNext() { showPicture(m_index + 1); }
void PdfPictureDialog::showPrevious() { showPicture(m_index - 1); }

void PdfPictureDialog::showPicture(int index) {
    const int count = m_pdf.pictures().size();
    if (count == 0) return;
    m_index = qBound(0, index, count - 1);
    const PdfPictures::Picture &p = m_pdf.pictures().at(m_index);
    const QImage image = currentImage();
    m_preview->setPixmap(previewOf(image));
    m_counter->setText(QStringLiteral("Picture %1 of %2").arg(m_index + 1).arg(count));
    QStringList facts;
    if (p.page > 0) facts << QStringLiteral("page %1").arg(p.page);
    facts << QStringLiteral("%1 x %2 pixels").arg(p.size.width()).arg(p.size.height());
    if (currentDpi() > 0) facts << QStringLiteral("placed at %1 dpi").arg(formatDpi(currentDpi()));
    facts << p.compression;
    QString text = facts.join(QStringLiteral(" · "));
    text[0] = text[0].toUpper();
    const QString problem = m_problems.value(m_index);
    if (!problem.isEmpty()) text += QStringLiteral("<br><span style=\"color:#c0392b\">%1</span>").arg(problem.toHtmlEscaped());
    m_details->setText(text);
    m_previous->setEnabled(m_index > 0);
    m_next->setEnabled(m_index < count - 1);
    for (QPushButton *b : {m_open, m_copy, m_layer}) b->setEnabled(!image.isNull());
}

void PdfPictureDialog::finish(Action action) {
    if (currentImage().isNull()) return;
    m_action = action;
    accept();
}
