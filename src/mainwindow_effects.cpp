// Main window: running effects, adjustments and plugin effects.
// Split from mainwindow.cpp; one class, several files.
#include "mainwindow.h"
#include "dialogs/pastedialog.h"
#include "core/adjustmentpreview.h"
#include "dialogs/huesaturationdialog.h"
#include "dialogs/curvesdialog.h"
#include "dialogs/previewdialog.h"
#include "plugins/pluginmanager.h"
#include "plugins/plugineffect.h"
#include "tools/tool.h"
#include "effects/blureffect.h"
#include "effects/sharpeneffect.h"
#include "effects/noiseeffect.h"
#include "effects/embosseffect.h"
#include "effects/edgedetecteffect.h"
#include "effects/oilpainteffect.h"
#include "effects/pixelateeffect.h"
#include "effects/motionblureffect.h"
#include "effects/radialblureffect.h"
#include "effects/zoomblureffect.h"
#include "effects/surfaceblureffect.h"
#include "effects/unfocuseffect.h"
#include "effects/fragmenteffect.h"
#include "effects/bulgeeffect.h"
#include "effects/twisteffect.h"
#include "effects/frostedglasseffect.h"
#include "effects/crystalizeeffect.h"
#include "effects/tileeffect.h"
#include "effects/dentseffect.h"
#include "effects/polarinversioneffect.h"
#include "effects/medianeffect.h"
#include "effects/reducenoiseeffect.h"
#include "effects/quantizeeffect.h"
#include "effects/gloweffect.h"
#include "effects/redeyeremoveeffect.h"
#include "effects/softenportraiteffect.h"
#include "effects/vignetteeffect.h"
#include "effects/turbulenceeffect.h"
#include "effects/reliefeffect.h"
#include "effects/outlineeffect.h"
#include "effects/morphologyeffect.h"
#include "effects/dropshadoweffect.h"
#include "adjustments/brightnesscontrast.h"
#include "adjustments/huesaturation.h"
#include "adjustments/levels.h"
#include "adjustments/curves.h"
#include "adjustments/invertcolors.h"
#include "adjustments/sepia.h"
#include "adjustments/posterize.h"
#include "adjustments/threshold.h"
#include "adjustments/desaturate.h"
#include "adjustments/colorbalance.h"
#include "adjustments/exposure.h"
#include "adjustments/highlightsshadows.h"
#include "adjustments/temperaturetint.h"
#include "adjustments/invertalpha.h"
#include <QDialog>

void MainWindow::runPluginEffect(PluginEffect *effect) {
    if (!effect || !m_document) return;
    if (!m_document || !convertTextLayersFor(targetLayerIndices())) return;
    auto *layer = m_document->activeLayer();
    if (!layer) return;

    if (effect->paramCount() == 0) {
        applyImageOperationToTargetLayers(
            [effect](const QImage &image) { return effect->apply(image); }, effect->name());
        return;
    }

    QVector<PreviewDialog::Param> dlgParams;
    for (const auto &p : effect->params())
        dlgParams.push_back({p.label, p.minValue, p.maxValue, p.defValue, p.suffix});

    PreviewDialog dlg(effect->name(), layer->image(), dlgParams,
        [effect](const QImage &src, const QVector<int> &v) {
            effect->setValues(v);
            return effect->apply(src);
        }, this);
    if (dlg.exec() != QDialog::Accepted) return;

    effect->setValues(dlg.values());
    applyImageOperationToTargetLayers(
        [effect](const QImage &image) { return effect->apply(image); }, effect->name());
}

void MainWindow::applyEffect(int effectIndex) {
    if (!m_document || !convertTextLayersFor(targetLayerIndices())) return;
    auto *layer = m_document->activeLayer();
    if (!layer) return;

    std::unique_ptr<Effect> effect;
    switch (effectIndex) {
    case 0: {
        BlurEffect def;
        PreviewDialog dlg(QStringLiteral("Gaussian Blur"), layer->image(),
            {{QStringLiteral("Radius"), 1, 100, def.radius(), ""}},
            [](const QImage &src, const QVector<int> &v) { BlurEffect t; t.setRadius(v[0]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new BlurEffect; e->setRadius(dlg.values()[0]);
        effect.reset(e);
        break;
    }
    case 1: {
        SharpenEffect def;
        PreviewDialog dlg(QStringLiteral("Sharpen"), layer->image(),
            {{QStringLiteral("Amount"), 1, 100, def.amount(), ""}},
            [](const QImage &src, const QVector<int> &v) { SharpenEffect t; t.setAmount(v[0]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new SharpenEffect; e->setAmount(dlg.values()[0]);
        effect.reset(e);
        break;
    }
    case 2: {
        NoiseEffect def;
        PreviewDialog dlg(QStringLiteral("Add Noise"), layer->image(),
            {{QStringLiteral("Intensity"), 1, 100, def.intensity(), ""}},
            [](const QImage &src, const QVector<int> &v) { NoiseEffect t; t.setIntensity(v[0]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new NoiseEffect; e->setIntensity(dlg.values()[0]);
        effect.reset(e);
        break;
    }
    case 3: {
        effect = std::make_unique<EmbossEffect>();
        break;
    }
    case 4: {
        effect = std::make_unique<EdgeDetectEffect>();
        break;
    }
    case 5: {
        OilPaintEffect def;
        PreviewDialog dlg(QStringLiteral("Oil Painting"), layer->image(),
            {{QStringLiteral("Brush size"), 1, 10, def.radius(), ""}},
            [](const QImage &src, const QVector<int> &v) { OilPaintEffect t; t.setRadius(v[0]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new OilPaintEffect; e->setRadius(dlg.values()[0]);
        effect.reset(e);
        break;
    }
    case 6: {
        PixelateEffect def;
        PreviewDialog dlg(QStringLiteral("Pixelate"), layer->image(),
            {{QStringLiteral("Cell size"), 2, 100, def.cellSize(), ""}},
            [](const QImage &src, const QVector<int> &v) { PixelateEffect t; t.setCellSize(v[0]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new PixelateEffect; e->setCellSize(dlg.values()[0]);
        effect.reset(e);
        break;
    }
    case 7: {
        MotionBlurEffect def;
        PreviewDialog dlg(QStringLiteral("Motion Blur"), layer->image(),
            {{QStringLiteral("Distance"), 1, 100, def.distance(), ""}},
            [](const QImage &src, const QVector<int> &v) { MotionBlurEffect t; t.setDistance(v[0]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new MotionBlurEffect; e->setDistance(dlg.values()[0]);
        effect.reset(e);
        break;
    }
    case 8: {
        // Ink Sketch - edge detection + invert for sketch look
        auto inkSketch = [](const QImage &image, int strength) {
            QImage img = image.convertToFormat(QImage::Format_ARGB32);
            EdgeDetectEffect edgeEff;
            QImage edges = edgeEff.apply(img);
            for (int y = 0; y < edges.height(); ++y) {
                QRgb *line = reinterpret_cast<QRgb*>(edges.scanLine(y));
                for (int x = 0; x < edges.width(); ++x) {
                    int r = 255 - qRed(line[x]);
                    int g = 255 - qGreen(line[x]);
                    int b = 255 - qBlue(line[x]);
                    float f = strength / 100.0f;
                    r = qBound(0, int(255 * (1 - f) + r * f), 255);
                    g = qBound(0, int(255 * (1 - f) + g * f), 255);
                    b = qBound(0, int(255 * (1 - f) + b * f), 255);
                    line[x] = qRgba(r, g, b, qAlpha(line[x]));
                }
            }
            return edges;
        };
        PreviewDialog dlg(QStringLiteral("Ink Sketch"), layer->image(),
            {{QStringLiteral("Ink outline"), 1, 100, 50, ""}},
            [inkSketch](const QImage &src, const QVector<int> &v) { return inkSketch(src, v[0]); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        const int strength = dlg.values()[0];
        applyImageOperationToTargetLayers([inkSketch, strength](const QImage &image) {
            return inkSketch(image, strength);
        }, "Ink Sketch");
        return;
    }
    case 9: {
        auto pencilSketch = [](const QImage &image, int pencilSize) {
            QImage img = image.convertToFormat(QImage::Format_ARGB32);
            EdgeDetectEffect edgeEff;
            QImage edges = edgeEff.apply(img);
            for (int y = 0; y < edges.height(); ++y) {
                QRgb *line = reinterpret_cast<QRgb*>(edges.scanLine(y));
                for (int x = 0; x < edges.width(); ++x) {
                    int gray = 255 - qGray(line[x]);
                    line[x] = qRgba(gray, gray, gray, qAlpha(line[x]));
                }
            }
            // Lead size thickens the strokes: a separable min-filter (darkening
            // dilation) of radius pencilSize/2 over the grey lines.
            const int r = pencilSize / 2;
            if (r >= 1) {
                const int w = edges.width(), h = edges.height();
                QImage tmp = edges;
                for (int y = 0; y < h; ++y) {
                    const QRgb *s = reinterpret_cast<const QRgb*>(edges.constScanLine(y));
                    QRgb *d = reinterpret_cast<QRgb*>(tmp.scanLine(y));
                    for (int x = 0; x < w; ++x) {
                        int mn = 255;
                        for (int dx = -r; dx <= r; ++dx) {
                            int nx = x + dx;
                            if (nx < 0 || nx >= w) continue;
                            mn = std::min(mn, qRed(s[nx]));
                        }
                        d[x] = qRgba(mn, mn, mn, qAlpha(s[x]));
                    }
                }
                for (int x = 0; x < w; ++x) {
                    for (int y = 0; y < h; ++y) {
                        int mn = 255;
                        for (int dy = -r; dy <= r; ++dy) {
                            int ny = y + dy;
                            if (ny < 0 || ny >= h) continue;
                            mn = std::min(mn, qRed(reinterpret_cast<const QRgb*>(tmp.constScanLine(ny))[x]));
                        }
                        QRgb cur = reinterpret_cast<const QRgb*>(tmp.constScanLine(y))[x];
                        reinterpret_cast<QRgb*>(edges.scanLine(y))[x] = qRgba(mn, mn, mn, qAlpha(cur));
                    }
                }
            }
            return edges;
        };
        PreviewDialog dlg(QStringLiteral("Pencil Sketch"), layer->image(),
            {{QStringLiteral("Lead size"), 1, 20, 2, ""}},
            [pencilSketch](const QImage &src, const QVector<int> &v) { return pencilSketch(src, v[0]); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        const int pencilSize = dlg.values()[0];
        applyImageOperationToTargetLayers([pencilSketch, pencilSize](const QImage &image) {
            return pencilSketch(image, pencilSize);
        }, "Pencil Sketch");
        return;
    }
    case 10: {
        // Clouds - Perlin-like noise render
        auto genClouds = [](int w, int h, int scale) {
            QImage img(w, h, QImage::Format_ARGB32);
            srand(42);
            for (int y = 0; y < img.height(); ++y) {
                QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(y));
                for (int x = 0; x < img.width(); ++x) {
                    double freq = scale / 100.0;
                    double val = sin(x * freq * 0.01) * cos(y * freq * 0.01) * 0.5 + 0.5;
                    val += sin(x * freq * 0.02 + y * freq * 0.03) * 0.25;
                    val = qBound(0.0, val * 0.7 + 0.15, 1.0);
                    int c = static_cast<int>(val * 255);
                    line[x] = qRgba(c, c, c, 255);
                }
            }
            return img;
        };
        PreviewDialog dlg(QStringLiteral("Clouds"), layer->image(),
            {{QStringLiteral("Scale"), 10, 500, 100, ""}},
            [genClouds](const QImage &src, const QVector<int> &v) { return genClouds(src.width(), src.height(), v[0]); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        QImage img = genClouds(m_document->width(), m_document->height(), dlg.values()[0]);
        applyImageOperationToTargetLayers([img](const QImage &) {
            return img;
        }, "Clouds");
        return;
    }
    case 11: {
        // Julia Fractal
        QImage img(m_document->width(), m_document->height(), QImage::Format_ARGB32);
        int maxIter = 256;
        double cx = -0.7, cy = 0.27015;
        for (int y = 0; y < img.height(); ++y) {
            QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                double zx = 3.0 * (x - img.width() / 2.0) / img.width();
                double zy = 2.0 * (y - img.height() / 2.0) / img.height();
                int iter = 0;
                while (zx * zx + zy * zy < 4.0 && iter < maxIter) {
                    double tmp = zx * zx - zy * zy + cx;
                    zy = 2.0 * zx * zy + cy;
                    zx = tmp;
                    iter++;
                }
                int c = (iter == maxIter) ? 0 : (iter * 255 / maxIter);
                line[x] = qRgba(c, c / 2, c * 2 > 255 ? 255 : c * 2, 255);
            }
        }
        applyImageOperationToTargetLayers([img](const QImage &) {
            return img;
        }, "Julia Fractal");
        return;
    }
    case 12: {
        // Mandelbrot Fractal
        QImage img(m_document->width(), m_document->height(), QImage::Format_ARGB32);
        int maxIter = 256;
        for (int y = 0; y < img.height(); ++y) {
            QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                double cx2 = 3.5 * x / img.width() - 2.5;
                double cy2 = 2.0 * y / img.height() - 1.0;
                double zx = 0, zy = 0;
                int iter = 0;
                while (zx * zx + zy * zy < 4.0 && iter < maxIter) {
                    double tmp = zx * zx - zy * zy + cx2;
                    zy = 2.0 * zx * zy + cy2;
                    zx = tmp;
                    iter++;
                }
                int c = (iter == maxIter) ? 0 : (iter * 255 / maxIter);
                line[x] = qRgba(c, c * 3 / 4, c / 2, 255);
            }
        }
        applyImageOperationToTargetLayers([img](const QImage &) {
            return img;
        }, "Mandelbrot Fractal");
        return;
    }
    case 13: {
        RadialBlurEffect def;
        PreviewDialog dlg(QStringLiteral("Radial Blur"), layer->image(),
            {{QStringLiteral("Angle"), 1, 90, def.angle(), ""}},
            [](const QImage &src, const QVector<int> &v) {
                RadialBlurEffect t; t.setAngle(v[0]);
                t.setCenter(QPoint(src.width() / 2, src.height() / 2));
                return t.apply(src);
            }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new RadialBlurEffect; e->setAngle(dlg.values()[0]);
        e->setCenter(QPoint(layer->image().width() / 2, layer->image().height() / 2));
        effect.reset(e);
        break;
    }
    case 14: {
        ZoomBlurEffect def;
        PreviewDialog dlg(QStringLiteral("Zoom Blur"), layer->image(),
            {{QStringLiteral("Amount"), 1, 100, def.amount(), ""}},
            [](const QImage &src, const QVector<int> &v) { ZoomBlurEffect t; t.setAmount(v[0]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new ZoomBlurEffect; e->setAmount(dlg.values()[0]);
        effect.reset(e);
        break;
    }
    case 15: {
        SurfaceBlurEffect def;
        PreviewDialog dlg(QStringLiteral("Surface Blur"), layer->image(),
            {{QStringLiteral("Radius"), 1, 20, def.radius(), ""}, {QStringLiteral("Threshold"), 1, 100, def.threshold(), ""}},
            [](const QImage &src, const QVector<int> &v) { SurfaceBlurEffect t; t.setRadius(v[0]); t.setThreshold(v[1]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new SurfaceBlurEffect; e->setRadius(dlg.values()[0]); e->setThreshold(dlg.values()[1]);
        effect.reset(e);
        break;
    }
    case 16: {
        UnfocusEffect def;
        PreviewDialog dlg(QStringLiteral("Blur"), layer->image(),
            {{QStringLiteral("Radius"), 1, 50, def.radius(), ""}},
            [](const QImage &src, const QVector<int> &v) { UnfocusEffect t; t.setRadius(v[0]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new UnfocusEffect; e->setRadius(dlg.values()[0]);
        effect.reset(e);
        break;
    }
    case 17: {
        FragmentEffect def;
        PreviewDialog dlg(QStringLiteral("Fragment"), layer->image(),
            {{QStringLiteral("Fragments"), 2, 16, def.fragments(), ""}, {QStringLiteral("Distance"), 1, 50, def.distance(), ""}},
            [](const QImage &src, const QVector<int> &v) { FragmentEffect t; t.setFragments(v[0]); t.setDistance(v[1]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new FragmentEffect; e->setFragments(dlg.values()[0]); e->setDistance(dlg.values()[1]);
        effect.reset(e);
        break;
    }
    case 18: {
        BulgeEffect def;
        PreviewDialog dlg(QStringLiteral("Bulge"), layer->image(),
            {{QStringLiteral("Amount"), -100, 100, def.amount(), ""}},
            [](const QImage &src, const QVector<int> &v) { BulgeEffect t; t.setAmount(v[0]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new BulgeEffect; e->setAmount(dlg.values()[0]);
        effect.reset(e);
        break;
    }
    case 19: {
        FrostedGlassEffect def;
        PreviewDialog dlg(QStringLiteral("Frosted Glass"), layer->image(),
            {{QStringLiteral("Amount"), 1, 20, def.amount(), ""}},
            [](const QImage &src, const QVector<int> &v) { FrostedGlassEffect t; t.setAmount(v[0]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new FrostedGlassEffect; e->setAmount(dlg.values()[0]);
        effect.reset(e);
        break;
    }
    case 20: {
        CrystalizeEffect def;
        PreviewDialog dlg(QStringLiteral("Crystalize"), layer->image(),
            {{QStringLiteral("Cell size"), 2, 100, def.cellSize(), ""}},
            [](const QImage &src, const QVector<int> &v) { CrystalizeEffect t; t.setCellSize(v[0]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new CrystalizeEffect; e->setCellSize(dlg.values()[0]);
        effect.reset(e);
        break;
    }
    case 21: {
        TileEffect def;
        PreviewDialog dlg(QStringLiteral("Tile Reflection"), layer->image(),
            {{QStringLiteral("Tile size"), 2, 200, def.tileSize(), ""}, {QStringLiteral("Rotation"), -180, 180, def.rotation(), ""}},
            [](const QImage &src, const QVector<int> &v) { TileEffect t; t.setTileSize(v[0]); t.setRotation(v[1]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new TileEffect; e->setTileSize(dlg.values()[0]); e->setRotation(dlg.values()[1]);
        effect.reset(e);
        break;
    }
    case 22: {
        DentsEffect def;
        PreviewDialog dlg(QStringLiteral("Dents"), layer->image(),
            {{QStringLiteral("Scale"), 1, 200, def.scale(), ""}, {QStringLiteral("Refraction"), 1, 200, def.refraction(), ""}},
            [](const QImage &src, const QVector<int> &v) { DentsEffect t; t.setScale(v[0]); t.setRefraction(v[1]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new DentsEffect; e->setScale(dlg.values()[0]); e->setRefraction(dlg.values()[1]);
        effect.reset(e);
        break;
    }
    case 23: {
        PolarInversionEffect def;
        PreviewDialog dlg(QStringLiteral("Polar Inversion"), layer->image(),
            {{QStringLiteral("Amount"), 0, 100, def.amount(), ""}},
            [](const QImage &src, const QVector<int> &v) { PolarInversionEffect t; t.setAmount(v[0]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new PolarInversionEffect; e->setAmount(dlg.values()[0]);
        effect.reset(e);
        break;
    }
    case 24: {
        TwistEffect def;
        PreviewDialog dlg(QStringLiteral("Twist"), layer->image(),
            {{QStringLiteral("Amount"), -360, 360, def.amount(), ""}},
            [](const QImage &src, const QVector<int> &v) { TwistEffect t; t.setAmount(v[0]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new TwistEffect; e->setAmount(dlg.values()[0]);
        effect.reset(e);
        break;
    }
    case 25: {
        MedianEffect def;
        PreviewDialog dlg(QStringLiteral("Median"), layer->image(),
            {{QStringLiteral("Radius"), 1, 10, def.radius(), ""}},
            [](const QImage &src, const QVector<int> &v) { MedianEffect t; t.setRadius(v[0]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new MedianEffect; e->setRadius(dlg.values()[0]);
        effect.reset(e);
        break;
    }
    case 26: {
        ReduceNoiseEffect def;
        PreviewDialog dlg(QStringLiteral("Reduce Noise"), layer->image(),
            {{QStringLiteral("Radius"), 1, 10, def.radius(), ""}, {QStringLiteral("Strength"), 1, 100, def.strength(), ""}},
            [](const QImage &src, const QVector<int> &v) { ReduceNoiseEffect t; t.setRadius(v[0]); t.setStrength(v[1]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new ReduceNoiseEffect; e->setRadius(dlg.values()[0]); e->setStrength(dlg.values()[1]);
        effect.reset(e);
        break;
    }
    case 27: {
        QuantizeEffect def;
        PreviewDialog dlg(QStringLiteral("Quantize"), layer->image(),
            {{QStringLiteral("Colors"), 2, 256, def.colors(), ""}},
            [](const QImage &src, const QVector<int> &v) { QuantizeEffect t; t.setColors(v[0]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new QuantizeEffect; e->setColors(dlg.values()[0]);
        effect.reset(e);
        break;
    }
    case 28: {
        GlowEffect def;
        PreviewDialog dlg(QStringLiteral("Glow"), layer->image(),
            {{QStringLiteral("Radius"), 1, 20, def.radius(), ""}, {QStringLiteral("Brightness"), 0, 100, def.brightness(), ""}},
            [](const QImage &src, const QVector<int> &v) { GlowEffect t; t.setRadius(v[0]); t.setBrightness(v[1]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new GlowEffect; e->setRadius(dlg.values()[0]); e->setBrightness(dlg.values()[1]);
        effect.reset(e);
        break;
    }
    case 29: {
        RedEyeRemoveEffect def;
        PreviewDialog dlg(QStringLiteral("Red Eye Removal"), layer->image(),
            {{QStringLiteral("Tolerance"), 0, 100, def.tolerance(), ""}, {QStringLiteral("Saturation"), 0, 100, def.saturation(), ""}},
            [](const QImage &src, const QVector<int> &v) { RedEyeRemoveEffect t; t.setTolerance(v[0]); t.setSaturation(v[1]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new RedEyeRemoveEffect; e->setTolerance(dlg.values()[0]); e->setSaturation(dlg.values()[1]);
        effect.reset(e);
        break;
    }
    case 30: {
        SoftenPortraitEffect def;
        PreviewDialog dlg(QStringLiteral("Soften Portrait"), layer->image(),
            {{QStringLiteral("Softness"), 1, 20, def.softness(), ""}, {QStringLiteral("Warmth"), 0, 100, def.warmth(), ""}},
            [](const QImage &src, const QVector<int> &v) { SoftenPortraitEffect t; t.setSoftness(v[0]); t.setWarmth(v[1]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new SoftenPortraitEffect; e->setSoftness(dlg.values()[0]); e->setWarmth(dlg.values()[1]);
        effect.reset(e);
        break;
    }
    case 31: {
        VignetteEffect def;
        PreviewDialog dlg(QStringLiteral("Vignette"), layer->image(),
            {{QStringLiteral("Amount"), 0, 100, def.amount(), ""}, {QStringLiteral("Radius"), 0, 100, def.radius(), ""}},
            [](const QImage &src, const QVector<int> &v) { VignetteEffect t; t.setAmount(v[0]); t.setRadius(v[1]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new VignetteEffect; e->setAmount(dlg.values()[0]); e->setRadius(dlg.values()[1]);
        effect.reset(e);
        break;
    }
    case 32: {
        TurbulenceEffect def;
        PreviewDialog dlg(QStringLiteral("Turbulence"), layer->image(),
            {{QStringLiteral("Scale"), 10, 500, def.scale(), ""}, {QStringLiteral("Roughness"), 1, 100, def.roughness(), ""}},
            [](const QImage &src, const QVector<int> &v) { TurbulenceEffect t; t.setScale(v[0]); t.setRoughness(v[1]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new TurbulenceEffect; e->setScale(dlg.values()[0]); e->setRoughness(dlg.values()[1]);
        effect.reset(e);
        break;
    }
    case 33: {
        ReliefEffect def;
        PreviewDialog dlg(QStringLiteral("Relief"), layer->image(),
            {{QStringLiteral("Angle"), 0, 360, def.angle(), ""}},
            [](const QImage &src, const QVector<int> &v) { ReliefEffect t; t.setAngle(v[0]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new ReliefEffect; e->setAngle(dlg.values()[0]);
        effect.reset(e);
        break;
    }
    case 34: {
        OutlineEffect def;
        PreviewDialog dlg(QStringLiteral("Outline"), layer->image(),
            {{QStringLiteral("Thickness"), 1, 10, def.thickness(), ""}, {QStringLiteral("Intensity"), 1, 100, def.intensity(), ""}},
            [](const QImage &src, const QVector<int> &v) { OutlineEffect t; t.setThickness(v[0]); t.setIntensity(v[1]); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new OutlineEffect; e->setThickness(dlg.values()[0]); e->setIntensity(dlg.values()[1]);
        effect.reset(e);
        break;
    }
    case 35: {
        MorphologyEffect def;
        PreviewDialog dlg(QStringLiteral("Morphology"), layer->image(),
            {{QStringLiteral("Radius"), 1, 10, def.radius(), ""}, {QStringLiteral("Mode (0=Dilate, 1=Erode)"), 0, 1, 0, ""}},
            [](const QImage &src, const QVector<int> &v) { MorphologyEffect t; t.setRadius(v[0]); t.setDilate(v[1] == 0); return t.apply(src); }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new MorphologyEffect; e->setRadius(dlg.values()[0]); e->setDilate(dlg.values()[1] == 0);
        effect.reset(e);
        break;
    }
    case 36: {   // Drop Shadow
        PreviewDialog dlg(QStringLiteral("Drop Shadow"), layer->image(),
            {{QStringLiteral("Radius"), 0, 50, 8, ""}, {QStringLiteral("Offset X"), -100, 100, 6, ""},
             {QStringLiteral("Offset Y"), -100, 100, 6, ""}, {QStringLiteral("Opacity"), 0, 100, 70, ""}},
            [](const QImage &src, const QVector<int> &v) {
                DropShadowEffect t; t.setRadius(v[0]); t.setOffsetX(v[1]); t.setOffsetY(v[2]); t.setOpacity(v[3]);
                return t.apply(src);
            }, this);
        if (dlg.exec() != QDialog::Accepted) return;
        auto *e = new DropShadowEffect;
        e->setRadius(dlg.values()[0]); e->setOffsetX(dlg.values()[1]);
        e->setOffsetY(dlg.values()[2]); e->setOpacity(dlg.values()[3]);
        effect.reset(e);
        break;
    }
    default: return;
    }

    if (effect) {
        applyImageOperationToTargetLayers([effectPtr = effect.get()](const QImage &image) {
            return effectPtr->apply(image);
        }, effect->name());
    }
}

// ---- Adjustments ----

void MainWindow::applyAdjustment(int adjustmentIndex) {
    if (!m_document || !convertTextLayersFor(targetLayerIndices())) return;
    auto *layer = m_document->activeLayer();
    if (!layer) return;

    switch (adjustmentIndex) {
    case 0: {
        auto build = [](const QVector<int> &v) {
            BrightnessContrast a; a.setBrightness(v[0]); a.setContrast(v[1]); return a;
        };
        PreviewDialog dlg(QStringLiteral("Brightness / Contrast"), layer->image(),
            {{QStringLiteral("Brightness"), -100, 100, 0, ""}, {QStringLiteral("Contrast"), -100, 100, 0, ""}},
            [build](const QImage &src, const QVector<int> &v) mutable { return build(v).apply(src); }, this);
        AdjustmentPreview preview(m_document, m_canvas, targetLayerIndices());
        auto operation = [build](const QVector<int> &values) -> ImageOperation {
            return [build, values](const QImage &image) { return build(values).apply(image); };
        };
        connect(&dlg, &QDialog::rejected, &preview, &AdjustmentPreview::cancel);
        connect(&dlg, &PreviewDialog::parametersChanged, &preview, [&]() {
            preview.request(operation(dlg.values()));
        });
        preview.request(operation(dlg.values()));
        if (dlg.exec() != QDialog::Accepted) { preview.cancel(); return; }
        auto finalOperation = operation(dlg.values());
        const QString name = BrightnessContrast().name();
        preview.commit(finalOperation, name);
        m_lastEffect = [this, finalOperation, name]() {
            AdjustmentPreview repeat(m_document, m_canvas, targetLayerIndices());
            repeat.commit(finalOperation, name);
        };
        m_lastEffectLabel = name;
        if (m_repeatEffectAction) {
            m_repeatEffectAction->setEnabled(true);
            m_repeatEffectAction->setText(QStringLiteral("Repeat: ") + name);
        }
        break;
    }
    case 1: {
        HueSaturationDialog dlg(this);
        AdjustmentPreview preview(m_document, m_canvas, targetLayerIndices());
        auto operation = [&dlg]() -> ImageOperation {
            const int hue = dlg.hue();
            // The dialog uses 0..200 with 100 neutral; the adjustment uses -100..100.
            const int saturation = dlg.saturation() - 100;
            const int lightness = dlg.lightness();
            return [hue, saturation, lightness](const QImage &image) {
                HueSaturation adj;
                adj.setHue(hue);
                adj.setSaturation(saturation);
                adj.setLightness(lightness);
                return adj.apply(image);
            };
        };
        connect(&dlg, &QDialog::rejected, &preview, &AdjustmentPreview::cancel);
        connect(&dlg, &HueSaturationDialog::parametersChanged, &preview, [&]() {
            preview.request(operation());
        });
        preview.request(operation());
        if (dlg.exec() != QDialog::Accepted) { preview.cancel(); return; }
        auto finalOperation = operation();
        const QString name = HueSaturation().name();
        preview.commit(finalOperation, name);
        m_lastEffect = [this, finalOperation, name]() {
            AdjustmentPreview repeat(m_document, m_canvas, targetLayerIndices());
            repeat.commit(finalOperation, name);
        };
        m_lastEffectLabel = name;
        if (m_repeatEffectAction) {
            m_repeatEffectAction->setEnabled(true);
            m_repeatEffectAction->setText(QStringLiteral("Repeat: ") + name);
        }
        break;
    }
    case 2: {
        // Gamma slider is in percent (100 = 1.0) so it fits the integer sliders.
        auto build = [](const QVector<int> &v) {
            Levels a; a.setInputBlack(v[0]); a.setInputWhite(v[1]); a.setGamma(v[2] / 100.0); return a;
        };
        PreviewDialog dlg(QStringLiteral("Levels"), layer->image(),
            {{QStringLiteral("Input black"), 0, 255, 0, ""},
             {QStringLiteral("Input white"), 0, 255, 255, ""},
             {QStringLiteral("Gamma"), 10, 1000, 100, "%"}},
            [build](const QImage &src, const QVector<int> &v) mutable { return build(v).apply(src); }, this);
        AdjustmentPreview preview(m_document, m_canvas, targetLayerIndices());
        auto operation = [build](const QVector<int> &values) -> ImageOperation {
            return [build, values](const QImage &image) { return build(values).apply(image); };
        };
        connect(&dlg, &QDialog::rejected, &preview, &AdjustmentPreview::cancel);
        connect(&dlg, &PreviewDialog::parametersChanged, &preview, [&]() {
            preview.request(operation(dlg.values()));
        });
        preview.request(operation(dlg.values()));
        if (dlg.exec() != QDialog::Accepted) { preview.cancel(); return; }
        auto finalOperation = operation(dlg.values());
        const QString name = Levels().name();
        preview.commit(finalOperation, name);
        m_lastEffect = [this, finalOperation, name]() {
            AdjustmentPreview repeat(m_document, m_canvas, targetLayerIndices());
            repeat.commit(finalOperation, name);
        };
        m_lastEffectLabel = name;
        if (m_repeatEffectAction) {
            m_repeatEffectAction->setEnabled(true);
            m_repeatEffectAction->setText(QStringLiteral("Repeat: ") + name);
        }
        break;
    }
    case 3: {
        CurvesDialog dlg(this);
        AdjustmentPreview preview(m_document, m_canvas, targetLayerIndices());
        auto operation = [](const QVector<QPointF> &points) -> ImageOperation {
            return [points](const QImage &image) {
                Curves adj;
                adj.setControlPoints(points);
                return adj.apply(image);
            };
        };
        connect(&dlg, &QDialog::rejected, &preview, &AdjustmentPreview::cancel);
        connect(&dlg, &CurvesDialog::curveChanged, &preview, [&]() {
            preview.request(operation(dlg.controlPoints()));
        });
        if (dlg.exec() != QDialog::Accepted) { preview.cancel(); return; }
        auto finalOperation = operation(dlg.controlPoints());
        preview.commit(finalOperation, "Curves");
        m_lastEffect = [this, finalOperation]() {
            AdjustmentPreview repeat(m_document, m_canvas, targetLayerIndices());
            repeat.commit(finalOperation, "Curves");
        };
        m_lastEffectLabel = "Curves";
        if (m_repeatEffectAction) {
            m_repeatEffectAction->setEnabled(true);
            m_repeatEffectAction->setText(QStringLiteral("Repeat: ") + m_lastEffectLabel);
        }
        break;
    }
    case 4: {
        InvertColors adj;
        applyImageOperationToTargetLayers([adj](const QImage &image) mutable {
            return adj.apply(image);
        }, adj.name());
        break;
    }
    case 5: {
        auto build = [](const QVector<int> &v) { Sepia a; a.setIntensity(v[0]); return a; };
        PreviewDialog dlg(QStringLiteral("Sepia"), layer->image(),
            {{QStringLiteral("Intensity"), 0, 100, 80, "%"}},
            [build](const QImage &src, const QVector<int> &v) mutable { return build(v).apply(src); }, this);
        AdjustmentPreview preview(m_document, m_canvas, targetLayerIndices());
        auto operation = [build](const QVector<int> &values) -> ImageOperation {
            return [build, values](const QImage &image) { return build(values).apply(image); };
        };
        connect(&dlg, &QDialog::rejected, &preview, &AdjustmentPreview::cancel);
        connect(&dlg, &PreviewDialog::parametersChanged, &preview, [&]() {
            preview.request(operation(dlg.values()));
        });
        preview.request(operation(dlg.values()));
        if (dlg.exec() != QDialog::Accepted) { preview.cancel(); return; }
        auto finalOperation = operation(dlg.values());
        const QString name = Sepia().name();
        preview.commit(finalOperation, name);
        m_lastEffect = [this, finalOperation, name]() {
            AdjustmentPreview repeat(m_document, m_canvas, targetLayerIndices());
            repeat.commit(finalOperation, name);
        };
        m_lastEffectLabel = name;
        if (m_repeatEffectAction) {
            m_repeatEffectAction->setEnabled(true);
            m_repeatEffectAction->setText(QStringLiteral("Repeat: ") + name);
        }
        break;
    }
    case 6: {
        auto build = [](const QVector<int> &v) { Posterize a; a.setLevels(v[0]); return a; };
        PreviewDialog dlg(QStringLiteral("Posterize"), layer->image(),
            {{QStringLiteral("Levels"), 2, 64, 4, ""}},
            [build](const QImage &src, const QVector<int> &v) mutable { return build(v).apply(src); }, this);
        AdjustmentPreview preview(m_document, m_canvas, targetLayerIndices());
        auto operation = [build](const QVector<int> &values) -> ImageOperation {
            return [build, values](const QImage &image) { return build(values).apply(image); };
        };
        connect(&dlg, &QDialog::rejected, &preview, &AdjustmentPreview::cancel);
        connect(&dlg, &PreviewDialog::parametersChanged, &preview, [&]() {
            preview.request(operation(dlg.values()));
        });
        preview.request(operation(dlg.values()));
        if (dlg.exec() != QDialog::Accepted) { preview.cancel(); return; }
        auto finalOperation = operation(dlg.values());
        const QString name = Posterize().name();
        preview.commit(finalOperation, name);
        m_lastEffect = [this, finalOperation, name]() {
            AdjustmentPreview repeat(m_document, m_canvas, targetLayerIndices());
            repeat.commit(finalOperation, name);
        };
        m_lastEffectLabel = name;
        if (m_repeatEffectAction) {
            m_repeatEffectAction->setEnabled(true);
            m_repeatEffectAction->setText(QStringLiteral("Repeat: ") + name);
        }
        break;
    }
    case 7: {
        // Paint.NET "Black and White" desaturates (instant, no threshold prompt).
        Desaturate adj;
        applyImageOperationToTargetLayers([adj](const QImage &image) mutable {
            return adj.apply(image);
        }, adj.name());
        break;
    }
    case 8: {
        auto build = [](const QVector<int> &v) {
            ColorBalance a; a.setCyanRed(v[0]); a.setMagentaGreen(v[1]); a.setYellowBlue(v[2]); return a;
        };
        PreviewDialog dlg(QStringLiteral("Color Balance"), layer->image(),
            {{QStringLiteral("Cyan / Red"), -100, 100, 0, ""},
             {QStringLiteral("Magenta / Green"), -100, 100, 0, ""},
             {QStringLiteral("Yellow / Blue"), -100, 100, 0, ""}},
            [build](const QImage &src, const QVector<int> &v) mutable { return build(v).apply(src); }, this);
        AdjustmentPreview preview(m_document, m_canvas, targetLayerIndices());
        auto operation = [build](const QVector<int> &values) -> ImageOperation {
            return [build, values](const QImage &image) { return build(values).apply(image); };
        };
        connect(&dlg, &QDialog::rejected, &preview, &AdjustmentPreview::cancel);
        connect(&dlg, &PreviewDialog::parametersChanged, &preview, [&]() {
            preview.request(operation(dlg.values()));
        });
        preview.request(operation(dlg.values()));
        if (dlg.exec() != QDialog::Accepted) { preview.cancel(); return; }
        auto finalOperation = operation(dlg.values());
        const QString name = ColorBalance().name();
        preview.commit(finalOperation, name);
        m_lastEffect = [this, finalOperation, name]() {
            AdjustmentPreview repeat(m_document, m_canvas, targetLayerIndices());
            repeat.commit(finalOperation, name);
        };
        m_lastEffectLabel = name;
        if (m_repeatEffectAction) {
            m_repeatEffectAction->setEnabled(true);
            m_repeatEffectAction->setText(QStringLiteral("Repeat: ") + name);
        }
        break;
    }
    case 9: {
        // Auto-Level: stretch histogram to full 0-255 range per channel
        applyImageOperationToTargetLayers([](const QImage &image) {
            QImage img = image.convertToFormat(QImage::Format_ARGB32);
            int minR = 255, minG = 255, minB = 255;
            int maxR = 0, maxG = 0, maxB = 0;
            for (int y = 0; y < img.height(); ++y) {
                const QRgb *line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
                for (int x = 0; x < img.width(); ++x) {
                    int r = qRed(line[x]), g = qGreen(line[x]), b = qBlue(line[x]);
                    minR = std::min(minR, r); maxR = std::max(maxR, r);
                    minG = std::min(minG, g); maxG = std::max(maxG, g);
                    minB = std::min(minB, b); maxB = std::max(maxB, b);
                }
            }
            auto stretch = [](int val, int lo, int hi) -> int {
                if (hi == lo) return val;
                return qBound(0, (val - lo) * 255 / (hi - lo), 255);
            };
            for (int y = 0; y < img.height(); ++y) {
                QRgb *line = reinterpret_cast<QRgb*>(img.scanLine(y));
                for (int x = 0; x < img.width(); ++x) {
                    line[x] = qRgba(
                        stretch(qRed(line[x]), minR, maxR),
                        stretch(qGreen(line[x]), minG, maxG),
                        stretch(qBlue(line[x]), minB, maxB),
                        qAlpha(line[x]));
                }
            }
            return img;
        }, "Auto-Level");
        break;
    }
    case 10: {   // Exposure
        auto build = [](const QVector<int> &v) { Exposure a; a.setExposure(v[0]); return a; };
        PreviewDialog dlg(QStringLiteral("Exposure"), layer->image(),
            {{QStringLiteral("Exposure"), -100, 100, 0, ""}},
            [build](const QImage &src, const QVector<int> &v) mutable { return build(v).apply(src); }, this);
        AdjustmentPreview preview(m_document, m_canvas, targetLayerIndices());
        auto operation = [build](const QVector<int> &values) -> ImageOperation {
            return [build, values](const QImage &image) { return build(values).apply(image); };
        };
        connect(&dlg, &QDialog::rejected, &preview, &AdjustmentPreview::cancel);
        connect(&dlg, &PreviewDialog::parametersChanged, &preview, [&]() {
            preview.request(operation(dlg.values()));
        });
        preview.request(operation(dlg.values()));
        if (dlg.exec() != QDialog::Accepted) { preview.cancel(); return; }
        auto finalOperation = operation(dlg.values());
        const QString name = Exposure().name();
        preview.commit(finalOperation, name);
        m_lastEffect = [this, finalOperation, name]() {
            AdjustmentPreview repeat(m_document, m_canvas, targetLayerIndices());
            repeat.commit(finalOperation, name);
        };
        m_lastEffectLabel = name;
        if (m_repeatEffectAction) {
            m_repeatEffectAction->setEnabled(true);
            m_repeatEffectAction->setText(QStringLiteral("Repeat: ") + name);
        }
        break;
    }
    case 11: {   // Highlights / Shadows
        auto build = [](const QVector<int> &v) {
            HighlightsShadows a; a.setHighlights(v[0]); a.setShadows(v[1]); return a;
        };
        PreviewDialog dlg(QStringLiteral("Highlights / Shadows"), layer->image(),
            {{QStringLiteral("Highlights"), -100, 100, 0, ""}, {QStringLiteral("Shadows"), -100, 100, 0, ""}},
            [build](const QImage &src, const QVector<int> &v) mutable { return build(v).apply(src); }, this);
        AdjustmentPreview preview(m_document, m_canvas, targetLayerIndices());
        auto operation = [build](const QVector<int> &values) -> ImageOperation {
            return [build, values](const QImage &image) { return build(values).apply(image); };
        };
        connect(&dlg, &QDialog::rejected, &preview, &AdjustmentPreview::cancel);
        connect(&dlg, &PreviewDialog::parametersChanged, &preview, [&]() {
            preview.request(operation(dlg.values()));
        });
        preview.request(operation(dlg.values()));
        if (dlg.exec() != QDialog::Accepted) { preview.cancel(); return; }
        auto finalOperation = operation(dlg.values());
        const QString name = HighlightsShadows().name();
        preview.commit(finalOperation, name);
        m_lastEffect = [this, finalOperation, name]() {
            AdjustmentPreview repeat(m_document, m_canvas, targetLayerIndices());
            repeat.commit(finalOperation, name);
        };
        m_lastEffectLabel = name;
        if (m_repeatEffectAction) {
            m_repeatEffectAction->setEnabled(true);
            m_repeatEffectAction->setText(QStringLiteral("Repeat: ") + name);
        }
        break;
    }
    case 12: {   // Temperature / Tint
        auto build = [](const QVector<int> &v) {
            TemperatureTint a; a.setTemperature(v[0]); a.setTint(v[1]); return a;
        };
        PreviewDialog dlg(QStringLiteral("Temperature / Tint"), layer->image(),
            {{QStringLiteral("Temperature (cool → warm)"), -100, 100, 0, ""},
             {QStringLiteral("Tint (green → magenta)"), -100, 100, 0, ""}},
            [build](const QImage &src, const QVector<int> &v) mutable { return build(v).apply(src); }, this);
        AdjustmentPreview preview(m_document, m_canvas, targetLayerIndices());
        auto operation = [build](const QVector<int> &values) -> ImageOperation {
            return [build, values](const QImage &image) { return build(values).apply(image); };
        };
        connect(&dlg, &QDialog::rejected, &preview, &AdjustmentPreview::cancel);
        connect(&dlg, &PreviewDialog::parametersChanged, &preview, [&]() {
            preview.request(operation(dlg.values()));
        });
        preview.request(operation(dlg.values()));
        if (dlg.exec() != QDialog::Accepted) { preview.cancel(); return; }
        auto finalOperation = operation(dlg.values());
        const QString name = TemperatureTint().name();
        preview.commit(finalOperation, name);
        m_lastEffect = [this, finalOperation, name]() {
            AdjustmentPreview repeat(m_document, m_canvas, targetLayerIndices());
            repeat.commit(finalOperation, name);
        };
        m_lastEffectLabel = name;
        if (m_repeatEffectAction) {
            m_repeatEffectAction->setEnabled(true);
            m_repeatEffectAction->setText(QStringLiteral("Repeat: ") + name);
        }
        break;
    }
    case 13: {   // Invert Alpha
        InvertAlpha adj;
        applyImageOperationToTargetLayers([adj](const QImage &image) mutable { return adj.apply(image); }, adj.name());
        break;
    }
    case 14: {   // Threshold
        auto build = [](const QVector<int> &v) { Threshold a; a.setThreshold(v[0]); return a; };
        PreviewDialog dlg(QStringLiteral("Threshold"), layer->image(),
            {{QStringLiteral("Threshold:"), 0, 255, 128, ""}},
            [build](const QImage &src, const QVector<int> &v) mutable { return build(v).apply(src); }, this);
        AdjustmentPreview preview(m_document, m_canvas, targetLayerIndices());
        auto operation = [build](const QVector<int> &values) -> ImageOperation {
            return [build, values](const QImage &image) { return build(values).apply(image); };
        };
        connect(&dlg, &QDialog::rejected, &preview, &AdjustmentPreview::cancel);
        connect(&dlg, &PreviewDialog::parametersChanged, &preview, [&]() {
            preview.request(operation(dlg.values()));
        });
        preview.request(operation(dlg.values()));
        if (dlg.exec() != QDialog::Accepted) { preview.cancel(); return; }
        auto finalOperation = operation(dlg.values());
        const QString name = Threshold().name();
        preview.commit(finalOperation, name);
        m_lastEffect = [this, finalOperation, name]() {
            AdjustmentPreview repeat(m_document, m_canvas, targetLayerIndices());
            repeat.commit(finalOperation, name);
        };
        m_lastEffectLabel = name;
        if (m_repeatEffectAction) {
            m_repeatEffectAction->setEnabled(true);
            m_repeatEffectAction->setText(QStringLiteral("Repeat: ") + name);
        }
        break;
    }
    }
}

// ---- UI updates ----
