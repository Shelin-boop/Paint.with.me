#include "imagelistbar.h"
#include "core/document.h"
#include "core/thumbnails.h"

#include <QHBoxLayout>
#include <QToolButton>
#include <QPainter>
#include <QFileInfo>
#include <QMouseEvent>
#include <QEvent>
#include <QApplication>
#include <QCursor>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QJsonDocument>
#include <QMenu>
#include <QMimeData>
#include "instancelink.h"

namespace {
constexpr int kImageHeight = 44;   // thumbnail height (logical pixels)
constexpr int kMinWidth = 26;      // very tall images: at least this wide
constexpr int kMaxWidth = 88;      // very wide images: at most this wide
constexpr int kFrame = 3;          // space around the picture inside its button
constexpr int kMargin = 4;         // space above and below the strip
}

ImageListBar::ImageListBar(QWidget *parent) : QWidget(parent) {
    setObjectName("imageListBar");
    // Takes the free width of the toolbar and centers the thumbnails in the
    // window (see placeThumbnails).
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, kMargin, 0, kMargin);
    m_layout->setSpacing(4);
    m_lead = new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    // Thumbnails dragged here: reordered, or moved over from another window.
    setAcceptDrops(true);
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

namespace {
// The x on a thumbnail: a dark disc with a light cross, readable on any picture.
QIcon closeIcon(qreal dpr) {
    const int side = 14;
    QPixmap pm(QSize(side, side) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 40, 40, 220));
    p.drawEllipse(QRectF(0.5, 0.5, side - 1, side - 1));
    p.setPen(QPen(QColor(245, 245, 245), 1.6, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(4.5, 4.5), QPointF(side - 4.5, side - 4.5));
    p.drawLine(QPointF(side - 4.5, 4.5), QPointF(4.5, side - 4.5));
    p.end();
    return QIcon(pm);
}
}  // namespace

QToolButton *ImageListBar::thumbnail(int index) const {
    return index >= 0 && index < m_buttons.size() ? m_buttons[index] : nullptr;
}

QToolButton *ImageListBar::closeButton(int index) const {
    return index >= 0 && index < m_closeButtons.size() ? m_closeButtons[index] : nullptr;
}

void ImageListBar::rebuild() {
    // Clear existing buttons (the leading spacer is kept and put back first).
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();   // gone at once, not when the deferred delete runs
            w->removeEventFilter(this);
            w->deleteLater();
        }
        if (item != m_lead) delete item;
    }
    m_layout->addItem(m_lead);
    m_buttons.clear();
    m_closeButtons.clear();
    m_pictures.clear();
    m_hovered = -1;

    const qreal dpr = devicePixelRatioF();
    const QIcon close = closeIcon(dpr);
    for (int i = 0; i < m_docs.size(); ++i) {
        Document *doc = m_docs[i];
        if (!doc) continue;

        auto *btn = new QToolButton(this);
        btn->setObjectName(QStringLiteral("imageThumbnail"));
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
        btn->setToolTip(QStringLiteral("%1\n%2 x %3 px\nDrag along the strip to reorder, or into another "
                                       "Paint.with.me window to move it there.\nx or middle-click: close. "
                                       "Right-click: more.")
                            .arg(name).arg(doc->width()).arg(doc->height()));

        connect(btn, &QToolButton::clicked, this, [this, i]() { emit imageSelected(i); });
        btn->setProperty("imageIndex", i);
        btn->installEventFilter(this);   // middle-click, menu, hover, drag

        // The x in the top right corner closes the image.
        auto *closeBtn = new QToolButton(btn);
        closeBtn->setObjectName(QStringLiteral("imageClose"));
        closeBtn->setIcon(close);
        closeBtn->setIconSize(QSize(14, 14));
        closeBtn->setAutoRaise(true);
        closeBtn->setCursor(Qt::ArrowCursor);
        closeBtn->setToolTip(QStringLiteral("Close %1").arg(name));
        closeBtn->setStyleSheet(QStringLiteral("QToolButton { border: none; padding: 0px; background: transparent; }"));
        closeBtn->setFixedSize(16, 16);
        closeBtn->move(btn->width() - closeBtn->width() - 1, 1);
        connect(closeBtn, &QToolButton::clicked, this, [this, i]() { emit imageCloseRequested(i); });

        m_buttons.append(btn);
        m_closeButtons.append(closeBtn);
        m_pictures.append(pm);
        m_layout->addWidget(btn);
    }
    m_layout->addStretch(1);
    placeThumbnails();
    // The strip is rebuilt after every change of an image: keep the x on the
    // thumbnail under the mouse.
    m_layout->activate();
    const QPoint mouse = mapFromGlobal(QCursor::pos());
    for (int i = 0; i < m_buttons.size(); ++i)
        if (m_buttons[i]->geometry().contains(mouse)) m_hovered = i;
    updateCloseButtons();
}

void ImageListBar::updateCloseButtons() {
    // Shown on the image in view and on the one under the mouse (Paint.NET).
    for (int i = 0; i < m_closeButtons.size(); ++i)
        m_closeButtons[i]->setVisible(i == m_active || i == m_hovered);
}

void ImageListBar::showMenu(int index, const QPoint &globalPos) {
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("imageMenu"));
    QAction *close = menu.addAction(QStringLiteral("Close"));
    QAction *move = menu.addAction(QStringLiteral("Move to New Window"));
    move->setEnabled(m_moveEnabled);
    move->setToolTip(QStringLiteral("Opens a new Paint.with.me window with this image, with its layers."));
    QAction *chosen = menu.exec(globalPos);
    if (chosen == close) emit imageCloseRequested(index);
    else if (chosen == move) emit imageMoveToNewWindowRequested(index);
}

int ImageListBar::insertionIndexAt(int x) const {
    for (int i = 0; i < m_buttons.size(); ++i)
        if (x < m_buttons[i]->geometry().center().x()) return i;
    return int(m_buttons.size());
}

void ImageListBar::dragEnterEvent(QDragEnterEvent *event) {
    if (!event->mimeData()->hasFormat(InstanceLink::imageMimeType())) return;
    event->setDropAction(Qt::MoveAction);
    event->accept();
    m_dropIndex = insertionIndexAt(event->position().toPoint().x());
    update();
}

void ImageListBar::dragMoveEvent(QDragMoveEvent *event) {
    if (!event->mimeData()->hasFormat(InstanceLink::imageMimeType())) return;
    event->setDropAction(Qt::MoveAction);
    event->accept();
    const int at = insertionIndexAt(event->position().toPoint().x());
    if (at != m_dropIndex) {
        m_dropIndex = at;
        update();
    }
}

void ImageListBar::dragLeaveEvent(QDragLeaveEvent *event) {
    m_dropIndex = -1;
    update();
    QWidget::dragLeaveEvent(event);
}

void ImageListBar::dropEvent(QDropEvent *event) {
    const int at = insertionIndexAt(event->position().toPoint().x());
    m_dropIndex = -1;
    update();
    const QJsonObject from = QJsonDocument::fromJson(event->mimeData()->data(InstanceLink::imageMimeType())).object();
    if (from.isEmpty()) return;
    event->setDropAction(Qt::MoveAction);
    event->accept();
    if (qint64(from.value("pid").toDouble()) != QCoreApplication::applicationPid()) {
        emit foreignImageDropped(from);
        return;
    }
    // One of this window's own images: a new place in the strip.
    const quint64 id = quint64(from.value("doc").toDouble());
    int index = -1;
    for (int i = 0; i < m_docs.size(); ++i)
        if (m_docs[i] && m_docs[i]->property("pwmImageId").toULongLong() == id) index = i;
    if (index < 0) return;
    const int to = at > index ? at - 1 : at;
    if (to != index) emit imageReordered(index, to);
}

void ImageListBar::paintEvent(QPaintEvent *event) {
    QWidget::paintEvent(event);
    if (m_dropIndex < 0 || m_buttons.isEmpty()) return;
    // Where a dropped image would go: a bar in the gap.
    const int x = m_dropIndex < m_buttons.size() ? m_buttons[m_dropIndex]->geometry().left() - 3
                                                  : m_buttons.last()->geometry().right() + 3;
    const QRect first = m_buttons.first()->geometry();
    QPainter p(this);
    p.fillRect(QRect(x - 1, first.top(), 3, first.height()), palette().color(QPalette::Highlight));
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
    // Center the thumbnails on the window, not just on this bar (which starts
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
    auto *btn = qobject_cast<QToolButton *>(obj);
    bool ok = false;
    const int index = btn ? btn->property("imageIndex").toInt(&ok) : -1;
    if (!ok) return QWidget::eventFilter(obj, event);
    switch (event->type()) {
    case QEvent::Enter:
        m_hovered = index;
        updateCloseButtons();
        break;
    case QEvent::Leave:
        if (m_hovered == index) m_hovered = -1;
        updateCloseButtons();
        break;
    case QEvent::ContextMenu:
        showMenu(index, static_cast<QContextMenuEvent *>(event)->globalPos());
        return true;
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            m_pressIndex = index;
            m_pressPos = me->position().toPoint();
        }
        break;
    }
    case QEvent::MouseMove: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (m_pressIndex == index && (me->buttons() & Qt::LeftButton)
            && (me->position().toPoint() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
            m_pressIndex = -1;
            btn->setDown(false);   // the drag takes over the press: no click
            const QPixmap picture = index < m_pictures.size() ? m_pictures[index] : QPixmap();
            // Runs the drag; the strip may be rebuilt meanwhile, so nothing
            // here is touched afterwards.
            emit imageDragStarted(index, picture);
            return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto *me = static_cast<QMouseEvent *>(event);
        m_pressIndex = -1;
        if (me->button() == Qt::MiddleButton) {
            emit imageCloseRequested(index);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QWidget::eventFilter(obj, event);
}
