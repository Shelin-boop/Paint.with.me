#pragma once

#include <QDialog>
#include <QHash>
#include <QImage>

class PdfPictures;
class QLabel;
class QPushButton;

// File > Extract Picture from PDF: shows the PDF's pictures one at a time,
// the first one chosen, and what to do with it: open it as a new image, copy
// it to the clipboard, or add it as a new layer.
class PdfPictureDialog : public QDialog {
    Q_OBJECT
public:
    enum class Action { None, OpenAsImage, CopyToClipboard, AddAsLayer };

    PdfPictureDialog(const PdfPictures &pdf, const QString &fileName, QWidget *parent = nullptr);

    Action action() const { return m_action; }
    int currentIndex() const { return m_index; }
    // Null when the picture cannot be decoded.
    QImage currentImage() const;
    double currentDpi() const;   // 0: unknown
    bool actionsEnabled() const;
    void showNext();
    void showPrevious();

private:
    void showPicture(int index);
    void finish(Action action);

    const PdfPictures &m_pdf;
    int m_index = -1;
    mutable QHash<int, QImage> m_decoded;
    mutable QHash<int, QString> m_problems;
    Action m_action = Action::None;
    QLabel *m_preview;
    QLabel *m_counter;
    QLabel *m_details;
    QPushButton *m_previous;
    QPushButton *m_next;
    QPushButton *m_open;
    QPushButton *m_copy;
    QPushButton *m_layer;
};
