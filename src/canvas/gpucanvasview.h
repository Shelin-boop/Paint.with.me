#pragma once

#include <QOpenGLWidget>
#include <memory>

class CanvasWidget;
class GpuCompositor;

// The canvas drawn by the graphics card: an OpenGL child that covers the
// CanvasWidget. The CanvasWidget still gets every mouse, pen and key event
// (this view lets them through) and still draws the marks on top (selection,
// guides, tool marks, rulers) with a QPainter; only the image and its
// checkerboard come from GpuCompositor.
class GpuCanvasView : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit GpuCanvasView(CanvasWidget *canvas);
    ~GpuCanvasView() override;

    // Null until the first frame was drawn (initializeGL).
    GpuCompositor *compositor() const { return m_compositor.get(); }

signals:
    // The graphics card cannot go on (memory, an OpenGL error); the canvas
    // switches to drawing with the processor.
    void failed(const QString &reason);

protected:
    void initializeGL() override;
    void paintGL() override;

private:
    void releaseResources();

    CanvasWidget *m_canvas;
    std::unique_ptr<GpuCompositor> m_compositor;
    bool m_failed = false;
};
