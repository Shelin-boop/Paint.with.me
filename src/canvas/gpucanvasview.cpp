#include "gpucanvasview.h"

#include "canvaswidget.h"
#include "gpucompositor.h"
#include "gpusupport.h"

#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QTimer>

GpuCanvasView::GpuCanvasView(CanvasWidget *canvas) : QOpenGLWidget(canvas), m_canvas(canvas) {
    setObjectName(QStringLiteral("gpuCanvas"));
    // Mouse, pen, wheel and keys go to the CanvasWidget under it.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    // Smooth edges for the marks drawn on top (brush outline, guides).
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSamples(4);
    setFormat(format);
}

GpuCanvasView::~GpuCanvasView() {
    releaseResources();
}

void GpuCanvasView::releaseResources() {
    if (!m_compositor) return;
    if (context()) {
        makeCurrent();
        m_compositor->release();
        doneCurrent();
    }
    m_compositor.reset();
}

void GpuCanvasView::initializeGL() {
    m_compositor = std::make_unique<GpuCompositor>();
    if (!m_compositor->initialize()) {
        m_failed = true;
        const QString why = m_compositor->error();
        QTimer::singleShot(0, this, [this, why]() { emit failed(why); });
        return;
    }
    // Room for the layers: what was free at the start, less some for the
    // desktop and the window itself; 1.5 GB when the driver does not say.
    const qint64 freeMb = GpuSupport::probe().freeMemoryMb;
    const qint64 budget = freeMb > 0 ? std::max<qint64>(256, freeMb - 512) : 1536;
    m_compositor->setMemoryBudget(budget * 1024 * 1024);
    // Before the context goes (the window is rebuilt, the program ends).
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this]() {
        if (!m_compositor) return;
        makeCurrent();
        m_compositor->release();
        doneCurrent();
        m_compositor.reset();
    });
}

void GpuCanvasView::paintGL() {
    if (m_failed || !m_compositor) {
        m_canvas->paintOnGpuView(this, nullptr);
        return;
    }
    if (!m_canvas->paintOnGpuView(this, m_compositor.get())) {
        m_failed = true;
        const QString why = m_compositor->error();
        QTimer::singleShot(0, this, [this, why]() { emit failed(why); });
    }
}
