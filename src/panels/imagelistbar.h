#pragma once

#include <QJsonObject>
#include <QPixmap>
#include <QPoint>
#include <QPointer>
#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QSpacerItem;
class QToolButton;
class Document;

// The open-image thumbnail strip in the toolbar, centered on the window.
// Behaves like browser tabs: one thumbnail per open image (as wide as the
// image's shape needs), click to switch, an x in the corner (shown on the
// image in view and under the mouse) or a middle-click to close, right-click
// for a menu. Dragging a thumbnail along the strip reorders the images;
// dragging it into another Paint.with.me window moves the image there.
class ImageListBar : public QWidget {
    Q_OBJECT
public:
    explicit ImageListBar(QWidget *parent = nullptr);

    void setDocuments(const QVector<Document*> &docs, int activeIndex);
    // The picture size for an image of this size: 44 px high, 26 to 88 px wide.
    static QSize thumbnailSize(const QSize &image);
    // "Move to New Window" offered (the window can talk to other windows).
    void setMoveEnabled(bool enabled) { m_moveEnabled = enabled; }
    // The thumbnail and close button of the image at `index` (tests).
    QToolButton *thumbnail(int index) const;
    QToolButton *closeButton(int index) const;
    // Where a drop at x (in this widget) would put an image: 0 ... count.
    int insertionIndexAt(int x) const;

signals:
    void imageSelected(int index);
    void imageCloseRequested(int index);
    void imageMoveToNewWindowRequested(int index);
    // A thumbnail is dragged: start the drag with this picture.
    void imageDragStarted(int index, const QPixmap &picture);
    // Dropped on the strip: an image of this window to a new place (`to`:
    // the index it ends up at) ...
    void imageReordered(int from, int to);
    // ... or one from another window (its drag data).
    void foreignImageDropped(const QJsonObject &from);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void rebuild();
    void placeThumbnails();
    int thumbnailsWidth() const;
    void showMenu(int index, const QPoint &globalPos);
    void updateCloseButtons();

    QHBoxLayout *m_layout;
    QSpacerItem *m_lead;   // space before the thumbnails, to center them
    QVector<Document*> m_docs;
    QVector<QToolButton *> m_buttons;
    QVector<QToolButton *> m_closeButtons;
    QVector<QPixmap> m_pictures;
    int m_active = 0;
    int m_hovered = -1;
    bool m_moveEnabled = false;
    // A press on a thumbnail that may become a drag.
    int m_pressIndex = -1;
    QPoint m_pressPos;
    int m_dropIndex = -1;   // insertion marker while something is dragged over
};
