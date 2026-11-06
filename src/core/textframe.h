#pragma once

#include <QByteArray>
#include <QFont>
#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QString>
#include <memory>

class QTextDocument;

// The text of a text layer: formatted text in a frame. A text layer shows
// the drawing of its frame as ordinary pixels and keeps this next to them,
// so the text can be edited again until it is turned into pixels.
//
// Sizes are in document pixels. The text is QTextDocument HTML, laid out on
// a 72 dpi device, so a "point" size in it is exactly one pixel; the user
// sees points at the image's resolution (1 pt = 1/72 in, so 12 pt is 50 px
// at 300 dpi), as in print layout programs.
struct TextFrame {
    QString html;              // QTextDocument::toHtml(); font sizes in pixels
    QRectF box;                // frame in document pixels when the text was drawn
    bool autoWidth = false;    // grows with the text (a click), else wraps at box.width()
    bool antialias = true;
    QFont defaultFont;         // for text typed into an empty frame; pixel size as point size

    // A QTextDocument laid out like the drawing: 72 dpi device, no margin,
    // width from the frame.
    void setUp(QTextDocument &doc) const;
    // Lays out an existing document for this frame's width rules.
    void layOut(QTextDocument &doc) const;
    // The frame as drawn: box position, its width (or the text's own width
    // for autoWidth), and at least the text's height.
    QRectF frameRect(const QTextDocument &laidOut) const;
    QRectF frameRect() const;

    // The drawing: a transparent image covering the frame plus room for
    // glyphs that reach past it (italics, large descenders). Its top-left
    // corner in document pixels goes to `origin`.
    QImage render(QPoint *origin) const;
    static QImage render(QTextDocument &laidOut, const TextFrame &geometry, QPoint *origin);

    QString plainText() const;
    // A layer name from the first line ("Text" when empty).
    QString suggestedLayerName() const;
    static QString layerNameFor(const QString &plainText);
    // Largest font size in pixels.
    static double largestPixelSize(const QTextDocument &doc);

    // Same text, box and font sizes scaled (Image > Resize).
    std::shared_ptr<TextFrame> scaled(double factor) const;

    // Stored in a .psd next to the layer's pixels (UTF-8 JSON).
    QByteArray serialize() const;
    static std::shared_ptr<TextFrame> deserialize(const QByteArray &data);

    // The device every text frame is laid out on (72 dpi).
    static class QPaintDevice *layoutDevice();
};
