#pragma once

#include <QByteArray>
#include <QImage>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>
#include <vector>

#include "layer.h"

class QIODevice;

// Adobe Photoshop documents: .psd read and write, .psb (large documents) read.
//
// Kept: every pixel layer with its name, position, opacity, visibility, lock
// and blend mode (where Photoshop has the same mode), the resolution and the
// selected layer. Text layers are written as their pixels plus a private
// block with the text, so Paint.with.me can edit them again. Written like Krita does: 8-bit RGB, each layer cropped to
// its content, PackBits (RLE) compression, a combined picture with
// transparency for programs that do not read layers. Layers reaching past the
// canvas keep those pixels.
//
// Read as well as possible and reported in `notes`: layer groups (their
// layers are opened one by one), layer masks (applied to the layer),
// adjustment and fill layers, layer styles, vector masks and clipping masks
// (left out), blend modes Paint.with.me does not have (shown as Normal),
// 16-bit and CMYK documents (converted to 8-bit RGB).
namespace Psd {

struct LayerData {
    QImage image;          // premultiplied ARGB32
    QPoint offset;         // canvas position of the image's top-left pixel
    bool retainsOffCanvasPixels = false;
    QString name;
    bool visible = true;
    bool locked = false;
    float opacity = 1.0f;
    BlendMode blendMode = BlendMode::Normal;
    // Paint.with.me's own block "pwmT": the text of a text layer
    // (TextFrame::serialize()). Other programs skip it and show the pixels.
    QByteArray textFrame;
};

struct ImageData {
    int width = 0;
    int height = 0;
    double dpi = 0;         // 0: the file does not say
    int activeLayer = -1;   // index into `layers`; -1: the file does not say
    std::vector<LayerData> layers;   // bottom first
    QStringList notes;      // what could not be kept as it was, in plain words
};

// True when the file starts with the "8BPS" signature.
bool isPsdFile(const QString &filePath);

bool read(const QString &filePath, ImageData &out, QString *error = nullptr);
bool read(const QByteArray &data, ImageData &out, QString *error = nullptr);

struct WriteOptions {
    double dpi = 0;          // 0: leave the resolution out
    int activeLayer = -1;    // index into the layers; -1: leave out
};

// Writes `layers` (bottom first) on a canvas of `size`. Safe to call from a
// background thread with copies of the layers. `device` must be open for
// writing and seekable (a file, a QSaveFile or a QBuffer).
bool write(QIODevice *device, const std::vector<Layer> &layers, const QSize &size,
           const WriteOptions &options = {}, QString *error = nullptr);
// Same, into a file, replacing it only once everything is written.
bool write(const QString &filePath, const std::vector<Layer> &layers, const QSize &size,
           const WriteOptions &options = {}, QString *error = nullptr);

// Photoshop's four-letter key for a blend mode. Modes Photoshop does not have
// (Glow, Negation, Reflect, Xor, Overwrite) give "norm" and *exact = false.
QByteArray blendModeKey(BlendMode mode, bool *exact = nullptr);
bool blendModeFromKey(const QByteArray &key, BlendMode *mode);

// Names of the layers whose blend mode a PSD cannot keep.
QStringList layersWithoutPsdBlendMode(const std::vector<Layer> &layers);

// Largest canvas side a .psd may have (larger needs .psb).
constexpr int kMaxSide = 30000;

} // namespace Psd
