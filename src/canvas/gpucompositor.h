#pragma once

#include <QColor>
#include <QImage>
#include <QOpenGLFunctions>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <QString>
#include <QTransform>
#include <memory>
#include <unordered_map>

class Document;
class Layer;
class QOpenGLBuffer;
class QOpenGLFramebufferObject;
class QOpenGLShaderProgram;
class QOpenGLVertexArrayObject;

// Combines the layers on the graphics card (OpenGL 3.0) and draws the result
// on the canvas: the GPU half of the canvas.
//
// - Every layer is a texture: the whole layer, or only the part with content
//   while that part is small (a text layer); painting outside it widens it.
//   The pixels come from the layer itself (the processor keeps the master
//   copy); after a brush move only the changed rectangle is sent.
// - The layers are combined into a texture of the image's size, or, zoomed
//   out, of a half, quarter ... size, exactly like the processor does it
//   (Document::compositeInto): each reduced pixel is the average of the 2 x 2
//   pixels at the middle of its block. All 15 blend modes are shaders that
//   follow the processor's formulas (Qt's composition modes and the four
//   Paint.NET modes); results agree within rounding (a few 1/255 steps).
// - Drawing turns, zooms and moves the combined image and puts the
//   checkerboard under it, in one pass.
//
// Every call needs the canvas's OpenGL context to be current.
class GpuCompositor : protected QOpenGLFunctions {
public:
    GpuCompositor();
    ~GpuCompositor();

    // Shaders and buffers; false (see error()) if they cannot be made.
    bool initialize();
    // Frees all textures and buffers (the context must still be current).
    void release();

    // Brings the textures up to `doc` and combines the changed parts into the
    // view reduced by 2^shift. `dirty`: document areas that hold every change
    // since the last call; `full`: something else changed (combine all).
    // False when the graphics memory budget or an OpenGL error stops it.
    bool update(const Document &doc, int shift, const QRegion &dirty, bool full);

    struct View {
        QSize viewport;              // device pixels of the framebuffer
        qreal devicePixelRatio = 1.0;
        QTransform rotation;         // widget coordinates: unrotated -> on screen
        QRectF canvasRect;           // the image's place, unrotated (widget coordinates)
        bool smooth = true;          // smooth (bilinear) or blocky (nearest) scaling
        QColor checker1, checker2;   // checkerboard, squares of 8 x 8
    };
    // Draws the combined image over the checkerboard into the bound framebuffer.
    void draw(const View &view);

    // The combined view as it is now (tests).
    QImage readBack();
    int shift() const { return m_shift; }

    QString error() const { return m_error; }
    // Graphics memory in use and the most it may use.
    qint64 memoryUsed() const;
    qint64 memoryBudget() const { return m_budget; }
    void setMemoryBudget(qint64 bytes) { m_budget = bytes; }
    // Number of layers held as only their content part (tests).
    int partialLayerCount() const;

private:
    struct LayerTexture {
        GLuint id = 0;
        QRect rect;            // the part of the layer image it holds
        QSize imageSize;
        qint64 key = 0;        // QImage::cacheKey() it shows
        bool whole = false;    // holds the whole layer image
    };
    bool ensureTarget(const Document &doc, int shift);
    bool uploadWhole(LayerTexture &t, const Layer &layer);
    bool uploadArea(LayerTexture &t, const Layer &layer, const QRect &area);
    bool allocate(LayerTexture &t, const QRect &rect);
    void composite(const Document &doc, const QRect &area);
    bool checkError(const char *what);

    std::unique_ptr<QOpenGLShaderProgram> m_normal, m_blend, m_display;
    std::unique_ptr<QOpenGLBuffer> m_quad;
    std::unique_ptr<QOpenGLVertexArrayObject> m_vao;
    std::unique_ptr<QOpenGLFramebufferObject> m_target;
    GLuint m_scratch = 0;       // a copy of what lies under a non-Normal layer
    QSize m_scratchSize;
    std::unordered_map<const Layer *, LayerTexture> m_layers;
    const Document *m_doc = nullptr;
    int m_shift = -1;
    QSize m_docSize;
    qint64 m_budget = 0;
    int m_maxTexture = 0;
    QString m_error;
};
