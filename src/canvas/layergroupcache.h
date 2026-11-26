#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <vector>

class Document;

// Combining the layers for the canvas, with the layers below the active one
// (and, when they all blend normally, the layers above it) kept combined.
// Painting changes only the active layer, so a changed area then needs three
// images put together instead of every layer: measured on 2026-11-26, a
// 1600 x 1600 px area of 16 layers took 32 ms, the three parts 6 ms.
//
// The groups are filled in tiles, as areas are asked for, so a stroke never
// waits for the whole image. A group is dropped when any of its layers
// changes (pixels, visibility, opacity, blend mode, position), when the
// active layer or the reduced size (shift) changes.
//
// "Below" gives exactly the same pixels as combining all layers in order.
// "Above" (only with two or more visible layers above, all Normal) can differ
// from it by rounding (a few 1/255 steps); the canvas always uses the same
// rule, so a changed area and the whole image agree.
class LayerGroupCache {
public:
    // Like Document::compositeInto(dest, origin, shift).
    void composite(const Document &doc, QImage &dest, const QPoint &origin, int shift);
    // Frees the groups (another image, or the graphics card draws).
    void clear();

    bool belowCached() const { return m_below.used; }
    bool aboveCached() const { return m_above.used; }
    // Bytes the groups hold.
    qint64 memoryUsed() const;

    static constexpr int kTile = 256;   // reduced pixels

private:
    struct Group {
        QImage image;                    // the whole reduced view
        std::vector<qint64> signature;   // of its layers (see signatureOf)
        QSize tiles;
        std::vector<char> valid;         // per tile
        bool used = false;
        void release();
        // The tiles touching `area` combined from layers [from, to).
        void ensure(const Document &doc, const QRect &area, int shift, int from, int to);
    };
    static std::vector<qint64> signatureOf(const Document &doc, int shift, int from, int to);
    static void prepare(Group &group, const Document &doc, int shift, int from, int to);

    Group m_below, m_above;
};
