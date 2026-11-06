#pragma once
#include "tool.h"
#include "core/textframe.h"
#include "core/document.h"
#include "canvas/canvaswidget.h"

#include <QFont>
#include <QPointer>
#include <QTextCursor>
#include <functional>
#include <memory>
#include <vector>

class QTextDocument;
class Layer;

// Text frames that stay editable (Paint.with.me's Text tool).
//
// Dragging opens a frame of that size; text wraps inside it. A click without
// dragging starts a frame that grows with the text. Each frame is a text
// layer of its own: it can be clicked and edited again, moved, resized and
// reformatted until Layers > Convert Text to Pixels turns it into an
// ordinary layer. While a frame is being edited, the tool bar's font, size,
// bold, italic, underline, strikethrough and alignment, and the primary
// color, apply to the selected text (or to what is typed next).
//
// One editing session is one undo step ("Text" for a new frame, "Edit Text"
// otherwise). Inside it, Ctrl+Z undoes typing.
class TextTool : public Tool {
public:
    TextTool();
    ~TextTool() override;

    ToolType type() const override { return ToolType::Text; }
    QString name() const override { return "Text"; }
    QCursor cursor() const override { return Qt::IBeamCursor; }
    QCursor cursorAt(const QPointF &canvasPos, const CanvasWidget &canvas) const override;

    void mousePressEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) override;
    void mouseMoveEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) override;
    void mouseReleaseEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) override;
    void keyPressEvent(QKeyEvent *event, CanvasWidget &canvas) override;
    void inputMethodEvent(QInputMethodEvent *event, CanvasWidget &canvas) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query, const CanvasWidget &canvas) const override;
    void drawOverlay(QPainter &painter, const CanvasWidget &canvas) override;
    bool wantsKeyInput() const override { return isEditing(); }
    bool wantsShortcut(const QKeyEvent *event) const override;
    void deactivate(CanvasWidget &canvas) override;
    // The drawn text reports the area it changed (Document::regionChanged).
    bool updatesCanvasRegion() const override { return true; }

    // Insert clipboard text at the caret while editing. No-op otherwise, so a
    // stray Ctrl+V can't create text out of nowhere.
    void insertText(const QString &text, CanvasWidget &canvas);
    // Ends editing and records the undo step (kept for older callers).
    void commitText(CanvasWidget &canvas);

    // ---- Editing ----
    bool isEditing() const { return m_text != nullptr; }
    Document *editedDocument() const { return m_doc; }
    int editedLayerIndex() const;   // -1 when not editing
    // Starts editing the text layer at `layerIndex` (caret at the end).
    bool beginEditing(CanvasWidget &canvas, int layerIndex);
    // Records the undo step and stops editing. Safe to call any time.
    void finishEditing();
    QString plainText() const;
    void selectAll();
    // Selects characters [from, to) of the frame being edited.
    void select(int from, int to);
    // Called whenever the format at the caret may have changed (the tool bar
    // shows it) or editing starts or stops.
    void setFormatListener(std::function<void()> listener) { m_formatListener = std::move(listener); }
    // The topmost visible, unlocked text layer whose frame contains `docPos`.
    static int textLayerAt(const Document *doc, const QPointF &docPos);

    // ---- Format (tool bar) ----
    // While editing: the format at the caret, and setting one changes the
    // selection (or what is typed next). Otherwise: the format of the next
    // frame. Sizes are points at the image's resolution.
    void setFont(const QFont &font);
    QFont font() const;
    void setFontFamily(const QString &family);
    QString fontFamily() const;
    void setFontSize(double points);
    double fontSize() const;
    void setBold(bool on);
    bool bold() const;
    void setItalic(bool on);
    bool italic() const;
    void setUnderline(bool on);
    bool underline() const;
    void setStrikeOut(bool on);
    bool strikeOut() const;
    enum class Align { Left, Center, Right, Justify };
    void setAlignment(Align a);
    Align alignment() const;
    // The primary color: applies to the selection while editing.
    void setColor(const QColor &color);

private:
    enum class Drag { None, Create, Select, Move, Resize };

    void startNewFrame(CanvasWidget &canvas, const QPointF &at);
    void openDocument(const TextFrame &frame);
    void refresh(bool markChanged = true);  // draw the layer again from the text
    void notifyFormat();
    QRectF frameRect() const;               // current frame, document pixels
    QPointF textPoint(const QPointF &docPos) const;
    int hitPosition(const QPointF &docPos) const;
    QRectF caretRect() const;               // document pixels
    std::vector<QRectF> selectionRects() const;
    int handleAt(const QPointF &docPos, double zoom) const;   // 0..7 or -1
    bool onBorder(const QPointF &docPos, double zoom) const;
    void applyCharFormat(const QTextCharFormat &format);
    QTextCharFormat formatAtCaret() const;
    QFont fontAtCaret() const;
    double dpi() const;
    QTextCharFormat newTextFormat(const QColor &color) const;
    Layer *editedLayer() const;
    void moveCursor(QTextCursor::MoveOperation op, bool select);

    // Format for new frames (points; pixels follow from the image's dpi).
    QString m_family = QStringLiteral("Sans Serif");
    double m_sizePt = 24.0;
    bool m_bold = false;
    bool m_italic = false;
    bool m_underline = false;
    bool m_strikeout = false;
    Align m_align = Align::Left;

    // The frame being edited.
    // The layer is found by its object, not its index: other layers may be
    // added or removed while the frame is open.
    QPointer<Document> m_doc;
    QPointer<CanvasWidget> m_canvas;
    Layer *m_layer = nullptr;
    int m_previousActive = -1;
    bool m_newLayer = false;
    bool m_changed = false;
    bool m_wasModified = false;
    QString m_autoName;
    std::shared_ptr<const Layer> m_before;
    TextFrame m_geometry;                   // box, width rule, antialiasing, default font
    std::unique_ptr<QTextDocument> m_text;
    QTextCursor m_cursor;

    Drag m_drag = Drag::None;
    QPointF m_dragStart;
    QRectF m_startBox;
    bool m_startAutoWidth = false;
    QPoint m_startOffset;
    int m_handle = -1;
    std::function<void()> m_formatListener;
};
