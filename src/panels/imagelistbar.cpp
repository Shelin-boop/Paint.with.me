#include "imagelistbar.h"
#include "core/document.h"
#include "core/thumbnails.h"

#include <QHBoxLayout>
#include <QToolButton>
#include <QPainter>
#include <QFileInfo>
#include <QMouseEvent>
#include <QEvent>

namespace {
constexpr int kImageHeight = 44;   // thumbnail height (logical pixels)
constexpr int kMinWidth = 26;      // very tall images: at least this wide
constexpr int kMaxWidth = 88;      // very wide images: at most this wide
constexpr int kFrame = 3;          // space around the picture inside its button
constexpr int kMargin = 4;         // space above and below the strip
}

ImageListBar::ImageListBar(QWidget *parent) : QWidget(parent) {
    setObjectName("imageListBar");
    // Takes the free width of the toolbar and centres the thumbnails in the
    // window (see placeThumbnails).
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, kMargin, 0, kMargin);
    m_layout->setSpacing(4);
    m_lead = new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
}

QSize ImageListBar::thumbnailSize(const QSize &image) {
    if (image.isEmpty()) return QSize(kImageHeight, kImageHeight);
    const int width = qRound(kImageHeight * double(image.width()) / image.height());
    return QSize(qBound(kMinWidth, width, kMaxWidth), kImageHeight);
}

void ImageListBar::setDocuments(const QVector<Document*> &docs, int activeIndex) {
    m_docs = docs;
    m_active = activeIndex;
    rebuild();
}

void ImageListBar::rebuild() {
    // Clear existing buttons (the leading spacer is kept and put back first).
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();   // gone at once, not when the deferred delete runs
            w->deleteLater();
        }
        if (item != m_lead) delete item;
    }
    m_layout->addItem(m_lead);

    const qreal dpr = devicePixelRatioF();
    for (int i = 0; i < m_docs.size(); ++i) {
        Document *doc = m_docs[i];
        if (!doc) continue;

        auto *btn = new QToolButton(this);
        btn->setCheckable(true);
        btn->setChecked(i == m_active);
        btn->setAutoRaise(true);

        // The flattened image on a checkerboard, as wide as its shape needs,
        // drawn at the screen's pixel density so it stays sharp when scaled.
        const QSize box = thumbnailSize(doc->size());
        QPixmap pm(box * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            for (int y = 0; y < box.height(); y += 6)
                for (int x = 0; x < box.width(); x += 6)
                    p.fillRect(x, y, 6, 6,
                               ((x / 6 + y / 6) % 2 == 0) ? QColor(255, 255, 255) : QColor(205, 205, 205));
            // Built from small copies of the layers, so a big image costs no
            // more than a small one here.
            const QImage thumb = Thumbnails::document(*doc, box * dpr);
            const QSizeF shown = QSizeF(thumb.size()) / dpr;
            p.drawImage(QRectF(QPointF((box.width() - shown.width()) / 2, (box.height() - shown.height()) / 2),
                               shown), thumb);
            p.setPen(QPen(QColor(120, 120, 120), 1));
            p.drawRect(QRectF(0.5, 0.5, box.width() - 1, box.height() - 1));
        }
        btn->setIcon(QIcon(pm));
        btn->setIconSize(box);
        btn->setFixedSize(box + QSize(2 * kFrame, 2 * kFrame));

        QString name = doc->filePath().isEmpty() ? QStringLiteral("Untitled")
                                                 : QFileInfo(doc->filePath()).fileName();
        if (doc->isModified()) name += " *";
        btn->setToolTip(QString(QStringLiteral("%1\n%2 x %3 px\n(middle-click to close)"))
                            .arg(name).arg(doc->width()).arg(doc->height()));

        connect(btn, &QToolButton::clicked, this, [this, i]() { emit imageSelected(i); });
        btn->setProperty("imageIndex", i);
        btn->installEventFilter(this);   // middle-click closes the image
        m_layout->addWidget(btn);
    }
    m_layout->addStretch(1);
    placeThumbnails();
}

int ImageListBar::thumbnailsWidth() const {
    int width = 0, count = 0;
    for (int i = 0; i < m_layout->count(); ++i)
        if (QWidget *w = m_layout->itemAt(i)->widget()) {
            width += w->maximumWidth();
            ++count;
        }
    return width + qMax(0, count - 1) * m_layout->spacing();
}

void ImageListBar::placeThumbnails() {
    // Centre the thumbnails on the window, not just on this bar (which starts
    // after the toolbar buttons), as far as the bar's own width allows.
    const int content = thumbnailsWidth();
    int lead = 0;
    if (QWidget *top = window(); top && width() > content) {
        const int barLeft = mapTo(top, QPoint(0, 0)).x();
        lead = qBound(0, top->width() / 2 - barLeft - content / 2, width() - content);
    }
    if (m_lead->sizeHint().width() != lead) {
        m_lead->changeSize(lead, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        m_layout->invalidate();
    }
}

void ImageListBar::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    placeThumbnails();
}

void ImageListBar::moveEvent(QMoveEvent *event) {
    QWidget::moveEvent(event);
    placeThumbnails();
}

bool ImageListBar::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::MiddleButton) {
            bool ok = false;
            int index = obj->property("imageIndex").toInt(&ok);
            if (ok) {
                emit imageCloseRequested(index);
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}
