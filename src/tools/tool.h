#pragma once

#include <QPointF>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QString>
#include <QIcon>
#include <QVariant>
#include <functional>

class CanvasWidget;
class Document;
class QImage;

#include <cmath>

inline QPoint toPixelPos(const QPointF &pos) {
    return QPoint(std::floor(pos.x()), std::floor(pos.y()));
}

// What stylus pressure changes on brush-like tools.
enum class PressureTarget { Size, Opacity, SizeAndOpacity };

// One stamp of a brush-like tool, after pressure and tilt.
struct Dab {
    QPointF pos;
    double radius = 0.0;      // half the stamp's size
    double alpha = 1.0;       // 0..1; below 1 when pressure controls opacity
    double angle = 0.0;       // degrees: direction of the pen's tilt
    double roundness = 1.0;   // 1 = round; smaller = flattened across the tilt
};

enum class ToolType {
    Brush,
    Eraser,
    Pencil,
    Fill,
    ColorPicker,
    Recolor,
    RectSelection,
    EllipseSelection,
    LassoSelection,
    MagicWand,
    Move,
    MoveSelection,
    Zoom,
    Pan,
    Text,
    Shape,
    Line,
    Gradient,
    CloneStamp,
    Perspective,
    Smudge,
    BlurBrush
};

// Stable text names for tool types ("paintbrush", "clone-stamp", ...), used
// wherever a tool is stored in settings. The enum order may change when tools
// are added; these names must not.
QString toolTypeKey(ToolType type);
bool toolTypeFromKey(const QString &key, ToolType *type);

class Tool {
public:
    virtual ~Tool() = default;

    virtual ToolType type() const = 0;
    virtual QString name() const = 0;
    virtual QCursor cursor() const { return Qt::CrossCursor; }

    virtual void mousePressEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) = 0;
    virtual void mouseMoveEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) = 0;
    virtual void mouseReleaseEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) = 0;
    virtual void keyPressEvent(QKeyEvent *event, CanvasWidget &canvas) { Q_UNUSED(event); Q_UNUSED(canvas); }
    virtual void keyReleaseEvent(QKeyEvent *event, CanvasWidget &canvas) { Q_UNUSED(event); Q_UNUSED(canvas); }

    // True while the tool is capturing raw keystrokes (the Text tool typing).
    // The canvas uses this to claim ShortcutOverride, so letters that are also
    // tool shortcuts (B, E, S…) reach the tool as text instead of being eaten by
    // the shortcut system and dropped — that was ~60% of the alphabet.
    virtual bool wantsKeyInput() const { return false; }

    // A tool can claim a specific key (by Qt::Key value) so a global single-key
    // shortcut doesn't swallow it — e.g. the Line/Curve tool needs Enter/Escape
    // to commit/cancel, but Enter is also the Deselect shortcut.
    virtual bool wantsCommitKey(int key) const { Q_UNUSED(key); return false; }
    // While typing, a tool can also claim chords such as Ctrl+A, Ctrl+B or
    // Ctrl+Z, which then act on the text instead of the image.
    virtual bool wantsShortcut(const QKeyEvent *event) const { Q_UNUSED(event); return false; }
    // Text from an input method (compose, dead keys, CJK input).
    virtual void inputMethodEvent(class QInputMethodEvent *event, CanvasWidget &canvas) { Q_UNUSED(event); Q_UNUSED(canvas); }
    virtual QVariant inputMethodQuery(Qt::InputMethodQuery query, const CanvasWidget &canvas) const {
        Q_UNUSED(query); Q_UNUSED(canvas); return QVariant();
    }
    // Whether a press would change the active layer's pixels. The canvas
    // keeps such tools off text layers (they must be turned into pixels
    // first, Layers > Convert Text to Pixels).
    virtual bool changesLayerPixels(const Document *doc) const { Q_UNUSED(doc); return false; }
    // The pointer shape over a point of the image (handles, frames ...).
    virtual QCursor cursorAt(const QPointF &canvasPos, const CanvasWidget &canvas) const {
        Q_UNUSED(canvasPos); Q_UNUSED(canvas); return cursor();
    }
    virtual void drawOverlay(QPainter &painter, const CanvasWidget &canvas) { Q_UNUSED(painter); Q_UNUSED(canvas); }

    // Called just before the tool stops being current (tool switch). A tool that
    // holds an uncommitted edit (Line/Curve nodes, Text being typed) bakes it in
    // here so switching away doesn't silently discard the work.
    virtual void deactivate(CanvasWidget &canvas) { Q_UNUSED(canvas); }

    // Opt-in: the tool reports its pixel damage through updateCanvasRegion().
    // Other tools retain the conservative full-cache invalidation path.
    virtual bool updatesCanvasRegion() const { return false; }

    // Tool options
    // Brush size is fractional and ranges 1..2000, matching Paint.NET (which
    // antialiases sub-integer sizes). The drawing code already works in doubles.
    double brushSize() const { return m_brushSize; }
    void setBrushSize(double size) { m_brushSize = qBound(1.0, size, 2000.0); }

    int hardness() const { return m_hardness; }
    void setHardness(int hardness) { m_hardness = qBound(0, hardness, 100); }

    int opacity() const { return m_opacity; }
    void setOpacity(int opacity) { m_opacity = qBound(1, opacity, 100); }

    bool antialiased() const { return m_antialiased; }
    void setAntialiased(bool aa) { m_antialiased = aa; }

    // Pen pressure 0..1, after the pressure curve (the canvas sets it from
    // tablet events; a mouse always reports 1).
    double pressure() const { return m_pressure; }
    void setPressure(double p) { m_pressure = qBound(0.0, p, 1.0); }
    // Pen tilt in degrees (Qt's xTilt, yTilt); zero for a mouse.
    QPointF tilt() const { return m_tilt; }
    void setTilt(const QPointF &degrees) { m_tilt = degrees; }
    // Whether the current input is a pen (set by the canvas). Pressure on
    // opacity and tilt only apply to pen strokes.
    bool stylus() const { return m_stylus; }
    void setStylus(bool on) { m_stylus = on; }

    // Tablet pressure sensitivity. When on, pressure scales the dab size
    // between 0 and the brush size (like Paint.NET), its opacity, or both.
    bool pressureSensitivity() const { return m_pressureSensitivity; }
    void setPressureSensitivity(bool on) { m_pressureSensitivity = on; }
    PressureTarget pressureTarget() const { return m_pressureTarget; }
    void setPressureTarget(PressureTarget target) { m_pressureTarget = target; }
    bool pressureSetsSize() const {
        return m_pressureSensitivity && m_pressureTarget != PressureTarget::Opacity;
    }
    bool pressureSetsOpacity() const {
        return m_pressureSensitivity && m_pressureTarget != PressureTarget::Size;
    }
    // Stable names for settings and brush presets.
    static QString pressureTargetKey(PressureTarget target);
    static PressureTarget pressureTargetFromKey(const QString &key);

    // Tolerance is a 0..100 percentage, as in Paint.NET. Tools convert it to the
    // 0..255 colour-distance basis with toleranceDistance().
    int tolerance() const { return m_tolerance; }
    void setTolerance(int t) { m_tolerance = qBound(0, t, 100); }
    int toleranceDistance() const { return int(m_tolerance * 2.55 + 0.5); }

    // Dab spacing as a percentage of brush size (smaller = smoother strokes).
    int spacing() const { return m_spacing; }
    void setSpacing(int s) { m_spacing = qBound(1, s, 200); }

    // Brush blend mode index (maps to QPainter composition modes in the tool).
    int blendMode() const { return m_blendMode; }
    void setBlendMode(int m) { m_blendMode = m; }

    // Maps a blend-mode combo index (Layer::allBlendModes order) to a Qt
    // composition mode. Shared by the brush and paint-bucket tools. Index 14
    // (Overwrite) needs a per-pixel replace, so callers handle it separately;
    // this returns CompositionMode_Source for it as a sane fallback.
    static QPainter::CompositionMode compositionModeFor(int blendIndex);

    // Fill Style, like Paint.NET: 0 = Solid Color, 1..N = a GDI+ hatch pattern
    // (see Hatch::). Used by the brush and shape tools.
    int fillStyle() const { return m_fillStyle; }
    void setFillStyle(int s) { m_fillStyle = s; }

protected:
    // ---- Dabs of brush-like tools (brush, eraser, smudge, blur) ----
    // The dab at `pos` for a pressure, with the brush size, the pressure
    // target and, for a pen, the tilt (Pen::settings().tiltFlattens).
    Dab dabAt(const QPointF &pos, double pressure) const;
    // Dabs from `from` (excluded) to `to` at the brush spacing; pressure goes
    // smoothly from `fromPressure` to `toPressure` along the way.
    void forEachDab(const QPointF &from, const QPointF &to, double fromPressure,
                    double toPressure, const std::function<void(const Dab &)> &paint) const;
    // Whether this stroke keeps each point at the highest pressure it got
    // (pressure on opacity with a pen), instead of building coverage up dab
    // after dab, which would make light strokes dark after a few dabs.
    bool strokeUsesPeakCoverage() const { return m_stylus && pressureSetsOpacity(); }
    // Paints a dab's coverage, in one opaque color, into a stroke buffer
    // (premultiplied ARGB32): the hardness falloff, the fill-style pattern,
    // the tilt flattening, and dab.alpha. With `peak`, coverage only rises to
    // what this dab has (see strokeUsesPeakCoverage); `scratch` is reused
    // between dabs. Returns the touched area (buffer pixels).
    static QRect paintDab(QImage &buffer, const Dab &dab, const QColor &opaqueColor,
                          int hardness, int fillStyle, bool antialiased, bool peak,
                          QImage &scratch);

    // Selection-awareness helpers shared by all painting tools.
    // Clips a painter (drawing into a layer image, image space) to the active
    // selection so strokes never spill outside it. No-op if nothing is selected.
    // Painter geometry is document-space; its target is the active layer buffer.
    static void clipToSelection(QPainter &painter, Document *doc);
    static QPointF imagePoint(const QPointF &documentPoint, Document *doc);
    static bool imageSelectionAllows(Document *doc, int x, int y);
    // For pixel-level tools: true if (x,y) may be edited given the selection.
    static bool selectionAllows(Document *doc, int x, int y);
    // Masks a freshly produced full-layer image so only selected pixels differ
    // from the original. Used by tools that rebuild the whole image.
    static QImage maskEditToSelection(Document *doc, const QImage &before, const QImage &after);
    // A fully transparent premultiplied ARGB32 image of `size`, for stroke
    // buffers. The memory comes from the system already zeroed, so creating a
    // layer-sized buffer (100 MB for 5000 x 5000 px) costs nothing up front;
    // only the parts a stroke touches are ever used.
    static QImage transparentImage(const QSize &size);

    double m_brushSize = 10;
    int m_hardness = 100;
    int m_opacity = 100;
    bool m_antialiased = true;
    double m_pressure = 1.0;
    double m_lastPressure = 1.0;   // at the previous point of the stroke (brush-like tools)
    QPointF m_tilt;
    bool m_stylus = false;
    bool m_pressureSensitivity = true;   // stylus pressure varies dab size or opacity
    PressureTarget m_pressureTarget = PressureTarget::Size;
    int m_tolerance = 50;   // percent, Paint.NET's default
    int m_spacing = 15;
    int m_blendMode = 0;
    int m_fillStyle = 0;   // 0 = Solid Color; 1..N = a hatch pattern (Hatch::)
};
