#pragma once

#include <QWidget>
#include <QVector>

class QHBoxLayout;
class QSpacerItem;
class Document;

// The open-image thumbnail strip in the toolbar, centred on the window.
// Behaves like browser tabs: one thumbnail per open image (as wide as the
// image's shape needs), click to switch, middle-click to close.
class ImageListBar : public QWidget {
    Q_OBJECT
public:
    explicit ImageListBar(QWidget *parent = nullptr);

    void setDocuments(const QVector<Document*> &docs, int activeIndex);
    // The picture size for an image of this size: 44 px high, 26 to 88 px wide.
    static QSize thumbnailSize(const QSize &image);

signals:
    void imageSelected(int index);
    void imageCloseRequested(int index);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private:
    void rebuild();
    void placeThumbnails();
    int thumbnailsWidth() const;

    QHBoxLayout *m_layout;
    QSpacerItem *m_lead;   // space before the thumbnails, to centre them
    QVector<Document*> m_docs;
    int m_active = 0;
};
