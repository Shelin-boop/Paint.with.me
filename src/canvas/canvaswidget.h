#pragma once

#include <vector>

#include <QWidget>
#include <QImage>
#include <QTimer>
#include <QPoint>
#include <QTransform>
#include <QKeySequence>
#include <QPainterPath>
#include <QElapsedTimer>
#include "core/nudge.h"
#include "canvas/layergroupcache.h"

class Document;
class Tool;
class GpuCanvasView;
class GpuCompositor;

class CanvasWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanvasWidget(QWidget *parent = nullptr);

    void setDocument(Document *doc);
    Document *document() const { return m_document; }

    void setCurrentTool(Tool *tool);
    Tool *currentTool() const { return m_currentTool; }

    // View
    double zoom() const { return m_zoom; }
    void setZoom(double zoom);
    // Zooms keeping the middle of the view where it is (menu, slider).
    void setZoomCentered(double zoom);
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomToRect(const QRect &canvasRect);   // fit a document rectangle to the view
    void zoomToActual();
    void resetToDefaultView();
    // Centres the image in the viewport at the current zoom.
    void centerView();
    QPointF pan() const { return m_pan; }
    // Any explicit pan is a user gesture: stop auto-centring from then on.
    void setPan(const QPointF &pan) { m_pan = pan; m_autoCenter = false; update(); }

    // Coordinate conversion
    // Image (document) pixels <-> widget pixels, with zoom, pan and rotation.
    QPointF widgetToCanvas(const QPointF &widgetPos) const;
    QPointF canvasToWidget(const QPointF &canvasPos) const;
    // The same as a transform, for drawing in image coordinates (use cosmetic
    // pens, or widths divided by zoom()).
    QTransform canvasTransform() const;

    // View rotation: only the view turns, like turning the paper on a desk;
    // the image does not change. Degrees, clockwise, around the middle of the
    // view, kept in (-180, 180].
    double rotation() const { return m_rotation; }
    void setRotation(double degrees);
    void rotateBy(double degrees) { setRotation(m_rotation + degrees); }
    // Moves the view by a distance on screen (a drag), whatever the rotation.
    void panBy(const QPointF &widgetDelta);
    // Zooms so that the image point under `widgetPos` stays there.
    void zoomAt(const QPointF &widgetPos, double zoom);
    // Keys held for panning and for turning the view by dragging, and for
    // the Color Picker while held (Settings > Keyboard; empty = none).
    void setHoldKeys(const QKeySequence &pan, const QKeySequence &rotate,
                     const QKeySequence &colorPicker = QKeySequence());

    // Rendering
    void setPreviewImage(const QImage &image) { m_previewImage = image; update(); }
    void clearPreviewImage() { m_previewImage = QImage(); updateCanvas(); }
    void updateCanvas();
    void updateCanvasRegion(const QRect &documentRect);
    void setShowGrid(bool show) { m_showGrid = show; update(); }
    bool showGrid() const { return m_showGrid; }
    void setShowRulers(bool show);

    // Measurement unit for the rulers and cursor position.
    enum class Unit { Pixels, Inches, Centimeters };
    void setUnit(Unit u) { m_unit = u; update(); }
    Unit unit() const { return m_unit; }
    // Resolution for the rulers in inches and centimeters: the document's own.
    double dpi() const;
    void setDpi(double d);

    // Backdrop around the image; follows the active colour scheme.
    void setBackdropColor(const QColor &c) { m_backdrop = c; update(); }
    // Re-reads canvas-related preferences (transparency checkerboard brightness).
    void reloadSettings();
    bool showRulers() const;
    int rulerSize() const { return m_showRulers ? 20 : 0; }

    // Drawing with the graphics card (OpenGL) or the processor.
    // applyGpuSetting() follows Settings > Canvas (GpuSupport::wanted); a
    // canvas made on its own (tests) draws with the processor until then.
    void applyGpuSetting();
    void setGpuEnabled(bool on, const QString &why = QString());
    bool gpuActive() const { return m_gpuView != nullptr; }
    GpuCanvasView *gpuView() const { return m_gpuView; }
    // "Graphics card: <name>" or "Processor (<why>)".
    QString gpuStatus() const;
    // Called by the GPU view's paintGL: updates the graphics card's copy
    // (gpu may be null: draw with the processor) and draws the frame. False
    // if the graphics card failed; this frame was then drawn by the processor.
    bool paintOnGpuView(GpuCanvasView *view, GpuCompositor *gpu);

signals:
    void zoomChanged(double zoom);
    void cursorPositionChanged(const QPoint &pos);
    void canvasModified();
    void deleteSelectionRequested();
    void fillSelectionRequested();
    void selectionContextMenuRequested(const QPoint &globalPos);
    // The graphics card stopped (reason); the processor draws from now on.
    void gpuFailed(const QString &reason);
    void gpuStatusChanged(const QString &status);
    void tabletDetected();   // emitted the first time a stylus is used
    // A tool that paints was used on a text layer and nothing happened.
    void textLayerBlocked(const QString &layerName);
    void rotationChanged(double degrees);
    // The Color Picker key went down (true) or came up (false). It comes up
    // only once no mouse button or pen is pressed, so a pick is finished.
    void colorPickerHeld(bool held);

protected:
    bool event(QEvent *event) override;   // claim ShortcutOverride while a tool types
    void focusOutEvent(QFocusEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void tabletEvent(QTabletEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
public:
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
protected:

private:
    void drawCheckerboard(QPainter &painter, const QRect &rect);
    QColor checkerColor(bool dark) const;
    // Everything a frame shows; with `gpu`, the image comes from the
    // graphics card (drawn in native OpenGL between the painter's marks).
    void paintScene(QPainter &painter, GpuCompositor *gpu);
    GpuCanvasView *m_gpuView = nullptr;
    QString m_gpuWhy;                    // why the processor draws
    QSize m_gpuViewport;                 // device pixels, for paintScene
    bool m_gpuConfirmed = false;         // a frame was drawn by the card
    void drawSelectionMarching(QPainter &painter);
    void updateMarchingAnts();

    Document *m_document = nullptr;
    Tool *m_currentTool = nullptr;

    // Perspective snapping of the stroke in progress: it starts at m_snapStart
    // and, once it has moved a few pixels, follows m_snapDirection.
    bool startsPerspectiveSnap(const QMouseEvent *event) const;
    QPointF perspectiveSnapped(const QPointF &canvasPos);
    bool m_snapActive = false;
    QPointF m_snapStart;
    QPointF m_snapDirection;

    double m_zoom = 1.0;
    QPointF m_pan{0, 0};
    // While true the image re-centres on every resize, so the startup view is
    // centred whatever the final window size is (maximised or restored). Any
    // user pan turns it off; resetToDefaultView turns it back on.
    bool m_autoCenter = true;

    bool m_blockedPress = false;   // this press reached a text layer with a painting tool
    double m_rotation = 0.0;
    QTransform viewRotation() const;       // unrotated view -> widget
    QPointF viewCenter() const;            // pivot: middle of the area right of / below the rulers
    double fitZoomFor(const QSizeF &size, double margin) const;
    QKeySequence m_panKey{Qt::Key_Space};
    QKeySequence m_rotateKey{QKeySequence(Qt::SHIFT | Qt::Key_Space)};
    QKeySequence m_pickKey;
    bool m_rotateKeyDown = false;
    bool m_pickKeyDown = false;
    bool m_pickReleasePending = false;   // key up while a button was down
    Qt::MouseButtons m_buttonsDown;      // as of the last press or release
    void endHolds();                     // focus lost: no key-up will come
    // Arrow keys: moving the selection (outline or pixels).
    bool nudgeApplies(const QKeyEvent *event) const;
    Nudge m_nudge;
    QElapsedTimer m_nudgeHeld;           // since the arrow key went down
    void handleMouseRelease(QMouseEvent *event);
    bool m_isRotating = false;
    QPointF m_lastRotatePos;
    bool m_isPanning = false;
    bool m_spaceDown = false;   // Space held: pan-drag with any tool (Paint.NET)
    QPoint m_lastPanPos;

    bool m_showGrid = false;
    QColor m_backdrop{150, 150, 150};
    int m_checkerBrightness = 80;   // 0..100, from Settings > Canvas
    Unit m_unit = Unit::Pixels;
    double m_dpi = 96.0;
    bool m_showRulers = true;

    // Marching ants
    QTimer m_marchingTimer;
    int m_marchingOffset = 0;
    qint64 m_selectionOutlineKey = -1;
    QPainterPath m_selectionOutline;

    // Cached render
    QImage m_previewImage;
    QImage m_cachedRender;   // combined layers, reduced by 2^m_cacheShift
    LayerGroupCache m_groups;   // layers below / above the active one, combined
    int m_cacheShift = 0;
    bool m_cacheValid = false;
    QRegion m_dirtyRender;
    // What the cached render (plus the queued dirty areas) shows: the content
    // version, visibility, opacity, blend mode and position of every layer. A
    // document change that leaves this unchanged needs no recomposition, e.g.
    // the end of a brush stroke whose dabs were already drawn area by area.
    std::vector<qint64> m_cacheSignature;
    std::vector<qint64> renderSignature() const;
    void documentContentChanged();

    // Tablet pressure
    double m_pressure = 1.0;
    bool m_tabletSeen = false;   // a stylus event has arrived at least once
    bool m_penInContact = false; // stylus tip is touching (pressure > 0), not just hovering
    bool m_penInProximity = false; // stylus is within range (hovering or touching)
};
