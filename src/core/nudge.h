#pragma once

#include <QImage>
#include <QPoint>
#include <QPointer>
#include <QRect>

class Document;
class Layer;

// Moving the selection with the arrow keys. Each key press moves it by one
// step: the selected pixels of the active layer (a hole is left behind, as
// with Move Selected Pixels), or only the outline (Move Selection tool).
// The moves of one key press, held or not, become one undo step when
// finish() is called (the key is let go, the mouse is used, the tool or image
// changes).
class Nudge {
public:
    enum class Mode { Pixels, Outline };

    // Moves by `delta` document pixels. Returns the document area whose
    // pixels changed (empty for outline moves, a locked layer ...); the
    // selection outline is announced with Document::selectionChanged.
    QRect step(Document &doc, Mode mode, QPoint delta);
    // Ends the moves: the pixels moved become one undo step.
    void finish();
    bool active() const;

    // Key press `key` (Qt::Key_Left ...) held for `heldMs` milliseconds: the
    // step in document pixels. One pixel, or after 3 seconds 1% of the
    // image's width (left, right) or height (up, down).
    static QPoint stepFor(int key, qint64 heldMs, QSize imageSize);
    static constexpr qint64 kLongPressMs = 3000;

private:
    void start(Document &doc, Mode mode);

    QPointer<Document> m_doc;
    Mode m_mode = Mode::Pixels;
    Layer *m_layer = nullptr;
    QImage m_original;   // the layer before the first step (for undo)
    QImage m_hole;       // the layer with the selected pixels taken out
    QImage m_floating;   // the selected pixels, cropped to m_area
    QRect m_area;        // where m_floating was, in layer pixels
    QPoint m_moved;      // moved so far
    qint64 m_expectedKey = 0;   // the layer's cacheKey after the last step
};
