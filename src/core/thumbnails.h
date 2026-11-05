#pragma once

#include <QImage>
#include <QSize>

class Document;

// Small preview images for the Layers panel and the image list.
//
// Shrinking a 5000 x 5000 px layer with Qt's smooth scaling reads all
// 25 million pixels and takes a noticeable fraction of a second; doing that for
// every layer whenever anything changed made the program sluggish with large
// images. These helpers shrink in two steps (a fast pick of pixels, then a
// smooth pass over the small result) and remember each result, keyed by the
// image's content version (QImage::cacheKey()), so an unchanged layer is never
// shrunk twice.
namespace Thumbnails {

// `image` shrunk to fit inside `box`, keeping its proportions. Never enlarges.
QImage scaled(const QImage &image, QSize box);

// Same, but remembered: an image whose pixels have not changed since the last
// call returns the remembered result immediately.
QImage cached(const QImage &image, QSize box);

// The visible layers of `doc`, combined with their opacity and blend modes,
// shrunk to fit inside `box`. Built from the remembered layer previews, so it
// costs almost nothing when only one layer changed.
QImage document(const Document &doc, QSize box);

// Forget all remembered previews (tests use this).
void clearCache();

} // namespace Thumbnails
