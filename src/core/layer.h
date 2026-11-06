#pragma once

#include <QImage>
#include <QString>
#include <QPainter>
#include <memory>

struct TextFrame;

enum class BlendMode {
    Normal,
    Multiply,
    Additive,
    ColorBurn,
    ColorDodge,
    Darken,
    Difference,
    Glow,
    Lighten,
    Negation,
    Overlay,
    Reflect,
    Screen,
    Xor,
    Overwrite
};

class Layer {
public:
    Layer(int width, int height, const QString &name = "Layer");
    Layer(const QImage &image, const QString &name = "Layer");
    Layer(const Layer &other);
    Layer &operator=(const Layer &other);

    const QImage &image() const { return m_image; }
    QImage &image() { return m_image; }
    // Always store premultiplied ARGB32 so the layer invariant holds even when an
    // adjustment/effect hands back a straight-alpha (Format_ARGB32) image.
    void setImage(const QImage &image) {
        m_image = image.format() == QImage::Format_ARGB32_Premultiplied
                      ? image
                      : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    float opacity() const { return m_opacity; }
    void setOpacity(float opacity) { m_opacity = qBound(0.0f, opacity, 1.0f); }

    bool isVisible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }

    bool isLocked() const { return m_locked; }
    void setLocked(bool locked) { m_locked = locked; }

    BlendMode blendMode() const { return m_blendMode; }
    void setBlendMode(BlendMode mode) { m_blendMode = mode; }

    QPoint offset() const { return m_offset; }
    void setOffset(const QPoint &offset) { m_offset = offset; }

    // Keep-paste layers retain nondestructive movement even if a later canvas
    // expansion matches their bitmap dimensions, or their origin returns to zero.
    bool retainsOffCanvasPixels() const { return m_retainsOffCanvasPixels; }
    void setRetainsOffCanvasPixels(bool retain) { m_retainsOffCanvasPixels = retain; }

    int width() const { return m_image.width(); }
    int height() const { return m_image.height(); }

    void resize(int width, int height);
    void clear(const QColor &color = Qt::transparent);
    QImage composited(const QImage &below) const;

    // Blends this layer onto `dest` in place. `dest` shows the document area
    // starting at document pixel `destOrigin` (a whole image: (0, 0); a tile:
    // its top-left corner). Only pixels this layer covers are touched, and the
    // result equals composited() for them. Safe to call from several threads at
    // once for different `dest` images.
    void compositeInto(QImage &dest, const QPoint &destOrigin = QPoint()) const;
    // Same, with a painter already open on `dest` (cheaper when many layers go
    // onto the same image), touching only `limit` (in `dest` pixels).
    //
    // With `shift` > 0, `dest` is a reduced view of the document: one `dest`
    // pixel covers 2^shift x 2^shift document pixels, and `destOrigin` is in
    // those reduced pixels. Used for the zoomed-out canvas, where combining at
    // full size would only be thrown away when shrinking for the screen.
    void compositeInto(QPainter &painter, QImage &dest, const QPoint &destOrigin,
                       const QRect &limit, int shift = 0) const;

    // The area of `rect` (document pixels) in a view reduced by 2^shift,
    // grown by one pixel for smooth sampling when shift > 0.
    static QRect reducedRect(const QRect &rect, int shift);

    // The smallest rectangle (in layer pixels) holding every pixel that is not
    // fully transparent; empty for an empty layer. Remembered until the pixels
    // change, so asking again is free. Call from the main thread only.
    QRect contentBounds() const;

    static QString blendModeName(BlendMode mode);
    static QList<BlendMode> allBlendModes();

    // ---- Text layers ----
    // A text layer keeps its text next to the pixels it was drawn as. It
    // counts only while the image is exactly that drawing (the same
    // QImage::cacheKey()); any other change to the pixels makes it an
    // ordinary layer, so stale text can never be drawn over painted pixels.
    // Undo steps that bring the drawing back bring the text back too.
    std::shared_ptr<const TextFrame> textFrame() const;
    bool isTextLayer() const { return textFrame() != nullptr; }
    // How far the layer moved since its text was drawn (Move tool, Canvas
    // Size, Crop): the frame's box is that much further on.
    QPoint textShift() const { return m_offset - m_textOffset; }
    // Shows `rendered`, drawn from `text` with its top-left corner at
    // document pixel `origin`, as this layer.
    void setText(std::shared_ptr<const TextFrame> text, const QImage &rendered, const QPoint &origin);
    // The pixels already are the drawing of `text` (opening a file).
    void adoptText(std::shared_ptr<const TextFrame> text);
    // Turns a text layer into an ordinary layer covering at least the
    // canvas (so painting next to the text is not cut off). Pixels shown
    // stay the same. No-op for other layers.
    void convertTextToPixels(const QSize &canvasSize);

private:
    void compositeReduced(QPainter &painter, QImage &dest, const QPoint &destOrigin,
                          const QRect &limit, int shift) const;
    void blendPerPixel(QPainter &painter, const QImage &dest, const QRect &target,
                       const QImage &top) const;

    QImage m_image;
    QString m_name;
    float m_opacity = 1.0f;
    bool m_visible = true;
    bool m_locked = false;
    BlendMode m_blendMode = BlendMode::Normal;
    QPoint m_offset{0, 0};
    bool m_retainsOffCanvasPixels = false;
    std::shared_ptr<const TextFrame> m_text;
    qint64 m_textKey = 0;     // cacheKey() of the drawing of m_text
    QPoint m_textOffset;      // offset when m_text was drawn
    mutable qint64 m_boundsKey = 0;   // QImage::cacheKey() m_bounds belongs to
    mutable QRect m_bounds;
};
