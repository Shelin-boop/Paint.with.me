#pragma once

#include <QImage>
#include <QString>
#include <vector>

class Document;

// Export to PDF, in the spirit of GIMP's: every page is one picture at the
// document's resolution, so the page has the image's print size (7625 x 5250
// px at 600 dpi gives a 322.8 x 222.3 mm page). Pixels are embedded unchanged,
// either lossless (Flate with PNG row prediction) or as JPEG; transparency is
// kept as a soft mask unless a white background is asked for.
namespace PdfExport {

struct Options {
    bool layersAsPages = false;   // one page per layer instead of the image
    bool topLayerFirst = true;    // with layersAsPages
    bool omitHidden = true;       // with layersAsPages: leave out hidden layers
    bool jpeg = false;            // false: lossless
    int jpegQuality = 92;
    bool whiteBackground = false; // fill transparent areas with white
    QString title;                // document title shown by PDF viewers
};

// The pictures a document becomes, one per page (premultiplied ARGB32).
std::vector<QImage> pagesFor(const Document &doc, const Options &options);

// Writes `pages` (premultiplied ARGB32 or any QImage) at `dpi`. The file is
// replaced only once it is complete. Safe to call from a background thread.
bool write(const QString &filePath, const std::vector<QImage> &pages, double dpi,
           const Options &options, QString *error = nullptr);

// Page size in millimeters for an image of `pixels` at `dpi`.
inline double millimeters(int pixels, double dpi) { return dpi > 0 ? pixels / dpi * 25.4 : 0; }

} // namespace PdfExport
