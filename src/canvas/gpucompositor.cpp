#include "gpucompositor.h"

#include "core/document.h"
#include "core/layer.h"

#include <QGenericMatrix>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QVector2D>
#include <QVector4D>
#include <unordered_set>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_UNSIGNED_INT_8_8_8_8_REV
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

namespace {

// Big areas are combined in tiles, so the copy of what lies under a
// non-Normal layer stays small.
constexpr int kTile = 2048;

const char *kQuadVertex = R"(#version 130
in vec2 vertex;
uniform vec4 rect;      // target pixels: x, y, width, height
uniform vec2 target;    // target size
void main() {
    vec2 p = rect.xy + vertex * rect.zw;
    gl_Position = vec4(p / target * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Reading a layer at a target pixel, as Layer::compositeInto does: at full
// size the pixel itself; reduced by 2^shift, the rounded average of the 2 x 2
// pixels at the middle of the block (see Layer::compositeReduced).
const char *kLayerSample = R"(
uniform sampler2D layerTex;
uniform ivec2 texOrigin;   // document pixel of texel (0, 0)
uniform ivec2 texSize;
uniform int shift;
vec4 fetchDoc(ivec2 d) {
    ivec2 t = d - texOrigin;
    if (t.x < 0 || t.y < 0 || t.x >= texSize.x || t.y >= texSize.y) return vec4(0.0);
    return texelFetch(layerTex, t, 0);
}
vec4 layerSample() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    if (shift == 0) return fetchDoc(p);
    int s = 1 << shift;
    ivec2 d = p * s + ivec2(s / 2 - 1);
    vec4 total = floor((fetchDoc(d) + fetchDoc(d + ivec2(1, 0)) + fetchDoc(d + ivec2(0, 1))
                        + fetchDoc(d + ivec2(1, 1))) * 255.0 + 0.5);
    return floor((total + 2.0) / 4.0) / 255.0;
}
)";

const char *kNormalFragment = R"(
uniform float opacity;
out vec4 fragColor;
void main() { fragColor = layerSample() * opacity; }
)";

// The other blend modes read what lies under them (a copy, dstTex). Qt's
// composition modes in premultiplied colors (the formulas of Qt's raster
// engine, qcompositionfunctions.cpp); the layer's opacity then mixes the
// result with what was there. The four Paint.NET modes work on straight
// 8-bit colors, like Layer::blendPerPixel.
const char *kBlendFragment = R"(
uniform sampler2D dstTex;
uniform ivec2 dstOrigin;
uniform int mode;
uniform float opacity;
out vec4 fragColor;

// The processor's own integer rounding (Layer::blendPerPixel), so that the
// bitwise Xor sees the very same 8-bit colors.
uvec3 unpremultiply8(uvec4 c) {
    if (c.a == 0u) return uvec3(0u);
    return min((c.rgb * 255u + c.a / 2u) / c.a, uvec3(255u));
}
uint premultiply8(uint c, uint a) {
    uint t = c * a;
    return (t + (t >> 8) + 128u) >> 8;
}
int reflectOp(int b, int s) {
    if (s >= 255) return 255;
    return min(255, b * b / (255 - s));
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 s = layerSample();
    vec4 d = texelFetch(dstTex, p - dstOrigin, 0);
    // Paint.NET's Glow (7), Negation (9), Reflect (11), Xor (13).
    if (mode == 7 || mode == 9 || mode == 11 || mode == 13) {
        uvec4 s8 = uvec4(floor(s * 255.0 + 0.5));
        if (s8.a == 0u) { fragColor = d; return; }
        uvec4 d8 = uvec4(floor(d * 255.0 + 0.5));
        ivec3 S = ivec3(unpremultiply8(s8));
        ivec3 D = ivec3(unpremultiply8(d8));
        ivec3 R;
        if (mode == 11) R = ivec3(reflectOp(D.r, S.r), reflectOp(D.g, S.g), reflectOp(D.b, S.b));
        else if (mode == 7) R = ivec3(reflectOp(S.r, D.r), reflectOp(S.g, D.g), reflectOp(S.b, D.b));
        else if (mode == 13) R = D ^ S;
        else R = 255 - abs(255 - D - S);
        // Over transparent parts the blend has nothing to work on: toward the
        // layer's own color by the alpha of what lies under it.
        float bA = float(d8.a) / 255.0;
        vec3 faded = clamp(floor(vec3(S) * (1.0 - bA) + vec3(R) * bA + 0.5), 0.0, 255.0);
        uvec3 f = uvec3(faded);
        vec3 premul = vec3(premultiply8(f.r, s8.a), premultiply8(f.g, s8.a), premultiply8(f.b, s8.a)) / 255.0;
        vec4 top = vec4(premul, float(s8.a) / 255.0) * opacity;
        fragColor = top + d * (1.0 - top.a);
        return;
    }
    vec3 sc = s.rgb, dc = d.rgb;
    float sa = s.a, da = d.a;
    vec3 temp = sc * (1.0 - da) + dc * (1.0 - sa);
    vec4 r = vec4(0.0, 0.0, 0.0, sa + da - sa * da);
    if (mode == 1) {                         // Multiply
        r.rgb = sc * dc + temp;
    } else if (mode == 2) {                  // Additive (Qt: Plus)
        r = min(s + d, vec4(1.0));
    } else if (mode == 3) {                  // Color Burn
        for (int i = 0; i < 3; ++i) {
            float a = sc[i] * da + dc[i] * sa;
            r[i] = (sc[i] <= 0.0 || a <= sa * da) ? temp[i] : sa * (a - sa * da) / sc[i] + temp[i];
        }
    } else if (mode == 4) {                  // Color Dodge
        for (int i = 0; i < 3; ++i) {
            float a = sc[i] * da + dc[i] * sa;
            r[i] = (a >= sa * da) ? sa * da + temp[i] : dc[i] * sa / (1.0 - sc[i] / sa) + temp[i];
        }
    } else if (mode == 5) {                  // Darken
        r.rgb = min(sc * da, dc * sa) + temp;
    } else if (mode == 6) {                  // Difference
        r.rgb = sc + dc - 2.0 * min(sc * da, dc * sa);
    } else if (mode == 8) {                  // Lighten
        r.rgb = max(sc * da, dc * sa) + temp;
    } else if (mode == 10) {                 // Overlay
        for (int i = 0; i < 3; ++i)
            r[i] = (2.0 * dc[i] < da) ? 2.0 * sc[i] * dc[i] + temp[i]
                                      : sa * da - 2.0 * (da - dc[i]) * (sa - sc[i]) + temp[i];
    } else if (mode == 12) {                 // Screen
        r.rgb = sc + dc - sc * dc;
    } else if (mode == 14) {                 // Overwrite (Qt: Source)
        r = s;
    } else {                                 // Normal, should it come here
        r = s + d * (1.0 - sa);
    }
    fragColor = clamp(mix(d, r, opacity), 0.0, 1.0);
}
)";

const char *kDisplayVertex = R"(#version 130
in vec2 vertex;
uniform mat3 unitToClip;
void main() {
    vec3 p = unitToClip * vec3(vertex, 1.0);
    gl_Position = vec4(p.xy / p.z, 0.0, 1.0);
}
)";

const char *kDisplayFragment = R"(#version 130
uniform sampler2D composite;
uniform mat3 deviceToDoc;
uniform float viewportHeight;
uniform vec2 docSize;
uniform float scale;          // document pixels per combined pixel
uniform vec2 compositeSize;
uniform bool smoothScaling;
uniform vec2 canvasTopLeft;   // unrotated widget coordinates
uniform float zoom;
uniform vec4 checker1;
uniform vec4 checker2;
out vec4 fragColor;
void main() {
    vec3 h = deviceToDoc * vec3(gl_FragCoord.x, viewportHeight - gl_FragCoord.y, 1.0);
    vec2 doc = h.xy / h.z;
    if (doc.x < 0.0 || doc.y < 0.0 || doc.x >= docSize.x || doc.y >= docSize.y) discard;
    vec2 c = doc / scale;
    vec4 image = smoothScaling ? texture(composite, c / compositeSize)
                               : texelFetch(composite, ivec2(floor(c)), 0);
    // The checkerboard of the canvas: squares of 8 x 8, on the unturned grid.
    vec2 u = canvasTopLeft + doc * zoom;
    ivec2 cell = ivec2(floor(u / 8.0));
    vec4 board = ((cell.x + cell.y) & 1) != 0 ? checker2 : checker1;
    fragColor = image + board * (1.0 - image.a);
}
)";

QMatrix3x3 toMatrix(const QTransform &t) {
    QMatrix3x3 m;
    m(0, 0) = float(t.m11()); m(0, 1) = float(t.m21()); m(0, 2) = float(t.m31());
    m(1, 0) = float(t.m12()); m(1, 1) = float(t.m22()); m(1, 2) = float(t.m32());
    m(2, 0) = float(t.m13()); m(2, 1) = float(t.m23()); m(2, 2) = float(t.m33());
    return m;
}

}  // namespace

GpuCompositor::GpuCompositor() = default;
GpuCompositor::~GpuCompositor() = default;

bool GpuCompositor::initialize() {
    initializeOpenGLFunctions();
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTexture);
    auto build = [this](std::unique_ptr<QOpenGLShaderProgram> &program, const char *vertex,
                        const QByteArray &fragment, const char *name) {
        program = std::make_unique<QOpenGLShaderProgram>();
        program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex);
        program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment);
        program->bindAttributeLocation("vertex", 0);
        if (!program->link()) {
            m_error = QStringLiteral("The %1 shader did not build: %2").arg(QLatin1String(name), program->log());
            return false;
        }
        return true;
    };
    const QByteArray head = QByteArrayLiteral("#version 130\n") + kLayerSample;
    if (!build(m_normal, kQuadVertex, head + kNormalFragment, "Normal")
        || !build(m_blend, kQuadVertex, head + kBlendFragment, "blend mode")
        || !build(m_display, kDisplayVertex, QByteArray(kDisplayFragment), "display"))
        return false;

    m_vao = std::make_unique<QOpenGLVertexArrayObject>();
    m_vao->create();
    QOpenGLVertexArrayObject::Binder bind(m_vao.get());
    m_quad = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    m_quad->create();
    m_quad->bind();
    const GLfloat corners[] = {0, 0, 1, 0, 0, 1, 1, 1};
    m_quad->allocate(corners, sizeof(corners));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    m_quad->release();
    return checkError("starting");
}

void GpuCompositor::release() {
    for (auto &entry : m_layers)
        if (entry.second.id) glDeleteTextures(1, &entry.second.id);
    m_layers.clear();
    if (m_scratch) glDeleteTextures(1, &m_scratch);
    m_scratch = 0;
    m_scratchSize = QSize();
    m_target.reset();
    m_quad.reset();
    m_vao.reset();
    m_normal.reset();
    m_blend.reset();
    m_display.reset();
    m_doc = nullptr;
    m_shift = -1;
}

bool GpuCompositor::checkError(const char *what) {
    bool ok = true;
    for (GLenum e = glGetError(); e != GL_NO_ERROR; e = glGetError()) {
        if (!ok) continue;
        ok = false;
        m_error = e == GL_OUT_OF_MEMORY
                      ? QStringLiteral("The graphics card ran out of memory while %1.").arg(QLatin1String(what))
                      : QStringLiteral("OpenGL error 0x%1 while %2.").arg(e, 0, 16).arg(QLatin1String(what));
    }
    return ok;
}

qint64 GpuCompositor::memoryUsed() const {
    qint64 bytes = 0;
    for (const auto &entry : m_layers)
        if (entry.second.id) bytes += qint64(entry.second.rect.width()) * entry.second.rect.height() * 4;
    if (m_target) bytes += qint64(m_target->width()) * m_target->height() * 4;
    bytes += qint64(m_scratchSize.width()) * m_scratchSize.height() * 4;
    return bytes;
}

int GpuCompositor::partialLayerCount() const {
    int n = 0;
    for (const auto &entry : m_layers)
        if (entry.second.id && !entry.second.whole) ++n;
    return n;
}

bool GpuCompositor::ensureTarget(const Document &doc, int shift) {
    const QSize size = doc.reducedSize(shift);
    if (m_target && m_target->size() == size && m_doc == &doc && m_shift == shift && m_docSize == doc.size())
        return true;
    if (size.width() > m_maxTexture || size.height() > m_maxTexture) {
        m_error = QStringLiteral("The image is larger than the graphics card can show (%1 pixels a side).")
                      .arg(m_maxTexture);
        return false;
    }
    const qint64 old = m_target ? qint64(m_target->width()) * m_target->height() * 4 : 0;
    if (m_budget > 0 && memoryUsed() - old + qint64(size.width()) * size.height() * 4 > m_budget) {
        m_error = QStringLiteral("The image needs more graphics memory than is free.");
        return false;
    }
    if (!m_target || m_target->size() != size) {
        m_target.reset();
        m_target = std::make_unique<QOpenGLFramebufferObject>(size, QOpenGLFramebufferObject::NoAttachment,
                                                              GL_TEXTURE_2D, GL_RGBA8);
        if (!m_target->isValid()) {
            m_target.reset();
            m_error = QStringLiteral("The graphics card could not make room for the image.");
            return false;
        }
        glBindTexture(GL_TEXTURE_2D, m_target->texture());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    m_doc = &doc;
    m_shift = shift;
    m_docSize = doc.size();
    return checkError("making room for the image");
}

bool GpuCompositor::allocate(LayerTexture &t, const QRect &rect) {
    if (rect.width() > m_maxTexture || rect.height() > m_maxTexture) {
        m_error = QStringLiteral("A layer is larger than the graphics card can hold (%1 pixels a side).")
                      .arg(m_maxTexture);
        return false;
    }
    if (t.id && t.rect.size() == rect.size()) {
        t.rect = rect;
        return true;
    }
    const qint64 old = t.id ? qint64(t.rect.width()) * t.rect.height() * 4 : 0;
    if (m_budget > 0 && memoryUsed() - old + qint64(rect.width()) * rect.height() * 4 > m_budget) {
        m_error = QStringLiteral("The layers need more graphics memory than is free.");
        return false;
    }
    if (t.id) glDeleteTextures(1, &t.id);
    t.id = 0;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rect.width(), rect.height(), 0, GL_BGRA,
                 GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
    t.rect = rect;
    return checkError("making room for a layer");
}

namespace {
// The layer pixels of `area` (layer image coordinates) as premultiplied ARGB32.
struct Pixels {
    QImage converted;
    const uchar *data = nullptr;
    int rowLength = 0;
};
Pixels pixelsOf(const QImage &image, const QRect &area) {
    Pixels p;
    if (image.format() == QImage::Format_ARGB32_Premultiplied) {
        p.data = image.constScanLine(area.y()) + size_t(area.x()) * 4;
        p.rowLength = int(image.bytesPerLine() / 4);
    } else {
        p.converted = image.copy(area).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        p.data = p.converted.constBits();
        p.rowLength = int(p.converted.bytesPerLine() / 4);
    }
    return p;
}
}  // namespace

bool GpuCompositor::uploadWhole(LayerTexture &t, const Layer &layer) {
    const QImage &image = layer.image();
    const QRect full(QPoint(), image.size());
    QRect rect = full;
    bool whole = true;
    // A layer with little on it (text, a small picture) keeps only that part;
    // the bounds are known already when the processor combined it.
    const QRect bounds = layer.contentBounds();
    if (bounds.isEmpty()) {
        if (t.id) glDeleteTextures(1, &t.id);
        t.id = 0;
        t.rect = QRect();
        t.whole = false;
        return true;
    }
    const QRect roomy = bounds.adjusted(-32, -32, 32, 32).intersected(full);
    if (qint64(roomy.width()) * roomy.height() * 2 < qint64(full.width()) * full.height()) {
        rect = roomy;
        whole = false;
    }
    if (!allocate(t, rect)) return false;
    t.whole = whole;
    const Pixels p = pixelsOf(image, rect);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, p.rowLength);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rect.width(), rect.height(), GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, p.data);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    return checkError("sending a layer to the graphics card");
}

bool GpuCompositor::uploadArea(LayerTexture &t, const Layer &layer, const QRect &area) {
    const QImage &image = layer.image();
    if (!t.id || (!t.whole && !t.rect.contains(area))) {
        // Painted outside the part it held: from now on the whole layer.
        if (!allocate(t, QRect(QPoint(), image.size()))) return false;
        t.whole = true;
        const Pixels p = pixelsOf(image, t.rect);
        glBindTexture(GL_TEXTURE_2D, t.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, p.rowLength);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, t.rect.width(), t.rect.height(), GL_BGRA,
                        GL_UNSIGNED_INT_8_8_8_8_REV, p.data);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        return checkError("sending a layer to the graphics card");
    }
    const QRect part = area.intersected(t.rect);
    if (part.isEmpty()) return true;
    const Pixels p = pixelsOf(image, part);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, p.rowLength);
    glTexSubImage2D(GL_TEXTURE_2D, 0, part.x() - t.rect.x(), part.y() - t.rect.y(), part.width(), part.height(),
                    GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, p.data);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    return checkError("sending a changed area to the graphics card");
}

bool GpuCompositor::update(const Document &doc, int shift, const QRegion &dirty, bool full) {
    m_error.clear();
    while (glGetError() != GL_NO_ERROR) {}
    if (m_budget > 0 && memoryUsed() > m_budget) {
        m_error = QStringLiteral("The image needs more graphics memory than is free.");
        return false;
    }
    const bool sameTarget = m_target && m_doc == &doc && m_shift == shift && m_docSize == doc.size()
                            && m_target->size() == doc.reducedSize(shift);
    if (!ensureTarget(doc, shift)) return false;
    const bool all = full || !sameTarget;

    // The layers' pixels.
    std::unordered_set<const Layer *> present;
    for (int i = 0; i < doc.layerCount(); ++i) {
        const Layer *layer = doc.layerAt(i);
        present.insert(layer);
        LayerTexture &t = m_layers[layer];
        const QImage &image = layer->image();
        const qint64 key = image.cacheKey();
        if (t.key == key && t.imageSize == image.size() && (t.id || t.rect.isEmpty())) continue;
        bool ok = true;
        if (t.key != 0 && t.imageSize == image.size() && !full && !dirty.isEmpty()) {
            // Changed only inside the reported areas (a brush move, an undo).
            for (const QRect &r : dirty) {
                const QRect area = r.translated(-layer->offset()).intersected(QRect(QPoint(), image.size()));
                if (!area.isEmpty() && !(ok = uploadArea(t, *layer, area))) break;
            }
        } else {
            ok = uploadWhole(t, *layer);
        }
        if (!ok) return false;
        t.key = key;
        t.imageSize = image.size();
    }
    for (auto it = m_layers.begin(); it != m_layers.end();) {
        if (present.count(it->first)) {
            ++it;
            continue;
        }
        if (it->second.id) glDeleteTextures(1, &it->second.id);
        it = m_layers.erase(it);
    }

    // Combining them.
    const QRect targetRect(QPoint(), m_target->size());
    QRegion areas;
    if (all) {
        areas = targetRect;
    } else {
        for (const QRect &r : dirty)
            areas += Layer::reducedRect(r, shift).intersected(targetRect);
    }
    if (!areas.isEmpty()) {
        m_target->bind();
        glViewport(0, 0, targetRect.width(), targetRect.height());
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glEnable(GL_SCISSOR_TEST);
        QOpenGLVertexArrayObject::Binder bind(m_vao.get());
        for (const QRect &r : areas)
            for (int y = r.top(); y <= r.bottom(); y += kTile)
                for (int x = r.left(); x <= r.right(); x += kTile)
                    composite(doc, QRect(x, y, std::min(kTile, r.right() + 1 - x), std::min(kTile, r.bottom() + 1 - y)));
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
        m_target->release();
    }
    return checkError("combining the layers");
}

void GpuCompositor::composite(const Document &doc, const QRect &area) {
    const QSize target = m_target->size();
    glScissor(area.x(), area.y(), area.width(), area.height());
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    for (int i = 0; i < doc.layerCount(); ++i) {
        const Layer *layer = doc.layerAt(i);
        if (!layer->isVisible() || layer->opacity() <= 0.0f || layer->image().isNull()) continue;
        const auto found = m_layers.find(layer);
        if (found == m_layers.end()) continue;
        const LayerTexture &t = found->second;
        const BlendMode mode = layer->blendMode();
        // Outside its texture a layer is transparent; only Overwrite still
        // changes what lies under its whole rectangle.
        const QRect docArea = mode == BlendMode::Overwrite ? QRect(layer->offset(), layer->image().size())
                                                           : t.rect.translated(layer->offset());
        if (docArea.isEmpty() || (!t.id && mode != BlendMode::Overwrite)) continue;
        const QRect a = Layer::reducedRect(docArea, m_shift).intersected(area);
        if (a.isEmpty()) continue;

        QOpenGLShaderProgram *program = mode == BlendMode::Normal ? m_normal.get() : m_blend.get();
        program->bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, t.id);
        program->setUniformValue("layerTex", 0);
        const QPoint origin = layer->offset() + t.rect.topLeft();
        glUniform2i(program->uniformLocation("texOrigin"), origin.x(), origin.y());
        glUniform2i(program->uniformLocation("texSize"), t.id ? t.rect.width() : 0, t.id ? t.rect.height() : 0);
        program->setUniformValue("shift", m_shift);
        program->setUniformValue("opacity", GLfloat(layer->opacity()));
        if (mode == BlendMode::Normal) {
            glEnable(GL_BLEND);
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glDisable(GL_BLEND);
            // What lies under the layer, copied so the shader can read it (on
            // texture unit 1: unit 0 holds the layer).
            glActiveTexture(GL_TEXTURE1);
            if (!m_scratch || m_scratchSize.width() < a.width() || m_scratchSize.height() < a.height()) {
                const QSize size = m_scratchSize.expandedTo(a.size()).expandedTo(QSize(256, 256));
                if (!m_scratch) glGenTextures(1, &m_scratch);
                glBindTexture(GL_TEXTURE_2D, m_scratch);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.width(), size.height(), 0, GL_BGRA,
                             GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
                m_scratchSize = size;
            }
            glBindTexture(GL_TEXTURE_2D, m_scratch);
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, a.x(), a.y(), a.width(), a.height());
            program->setUniformValue("dstTex", 1);
            glUniform2i(program->uniformLocation("dstOrigin"), a.x(), a.y());
            program->setUniformValue("mode", int(mode));
            glActiveTexture(GL_TEXTURE0);
        }
        program->setUniformValue("rect", QVector4D(a.x(), a.y(), a.width(), a.height()));
        program->setUniformValue("target", QVector2D(target.width(), target.height()));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}

void GpuCompositor::draw(const View &view) {
    if (!m_target || !m_doc) return;
    const QSizeF docSize(m_docSize);
    const double zoom = view.canvasRect.width() / std::max(1.0, docSize.width());
    QTransform docToLogical;
    docToLogical.scale(zoom, zoom);
    docToLogical *= QTransform::fromTranslate(view.canvasRect.left(), view.canvasRect.top());
    docToLogical *= view.rotation;
    const QTransform docToDevice = docToLogical * QTransform::fromScale(view.devicePixelRatio, view.devicePixelRatio);
    const QTransform unitToDevice = QTransform::fromScale(docSize.width(), docSize.height()) * docToDevice;
    const QTransform deviceToClip(2.0 / view.viewport.width(), 0, 0, -2.0 / view.viewport.height(), -1, 1);
    bool invertible = false;
    const QTransform deviceToDoc = docToDevice.inverted(&invertible);
    if (!invertible) return;

    glViewport(0, 0, view.viewport.width(), view.viewport.height());
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    m_display->bind();
    m_display->setUniformValue("unitToClip", toMatrix(unitToDevice * deviceToClip));
    m_display->setUniformValue("deviceToDoc", toMatrix(deviceToDoc));
    m_display->setUniformValue("viewportHeight", GLfloat(view.viewport.height()));
    m_display->setUniformValue("docSize", QVector2D(docSize.width(), docSize.height()));
    m_display->setUniformValue("scale", GLfloat(1 << m_shift));
    m_display->setUniformValue("compositeSize", QVector2D(m_target->width(), m_target->height()));
    m_display->setUniformValue("smoothScaling", GLint(view.smooth ? 1 : 0));
    m_display->setUniformValue("canvasTopLeft", QVector2D(view.canvasRect.left(), view.canvasRect.top()));
    m_display->setUniformValue("zoom", GLfloat(zoom));
    auto premultiplied = [](const QColor &c) {
        return QVector4D(c.redF() * c.alphaF(), c.greenF() * c.alphaF(), c.blueF() * c.alphaF(), c.alphaF());
    };
    m_display->setUniformValue("checker1", premultiplied(view.checker1));
    m_display->setUniformValue("checker2", premultiplied(view.checker2));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_target->texture());
    const GLint filter = view.smooth ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    m_display->setUniformValue("composite", 0);
    {
        QOpenGLVertexArrayObject::Binder bind(m_vao.get());
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    m_display->release();
}

QImage GpuCompositor::readBack() {
    if (!m_target) return QImage();
    // The framebuffer's first row is the image's first row here, so it is
    // read as it is (not turned upside down for the screen).
    return m_target->toImage(false).convertToFormat(QImage::Format_ARGB32_Premultiplied);
}
