#pragma once

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>
#include <QVector>
#include <memory>

// The pictures inside a PDF, for File > Extract Picture from PDF.
//
// Finds the images in the order the pages draw them (so the first one is the
// first picture a reader sees), works out the resolution each is placed at,
// and decodes them. Own reader, no library: it reads the whole file, so
// broken cross-reference tables and incremental updates do not matter;
// compressed object streams are read too.
//
// Decoded: JPEG (DCT), Flate (with PNG and TIFF predictors), LZW, RunLength,
// ASCIIHex and ASCII85 data; gray, RGB, CMYK, ICC-based, indexed and
// separation colors; 1, 2, 4, 8 and 16 bits; soft masks (transparency),
// stencil and color-key masks. Not decoded (named in `problem`): JPEG 2000,
// JBIG2 and CCITT fax pictures. Encrypted PDFs are refused.
class PdfPictures {
public:
    struct Picture {
        int page = 0;                // 1-based; 0 when not found on any page
        int object = 0;              // PDF object number
        QSize size;                  // pixels
        QString compression;         // "JPEG", "Flate", "uncompressed" ...
        double dpiX = 0, dpiY = 0;   // as placed on the page; 0 when unknown
        QString problem;             // why it cannot be decoded; empty when it can
    };

    PdfPictures();
    ~PdfPictures();
    PdfPictures(const PdfPictures &) = delete;
    PdfPictures &operator=(const PdfPictures &) = delete;

    bool open(const QString &filePath);
    bool openData(const QByteArray &data);
    // Why open() failed, in plain words.
    QString error() const;

    // Pictures smaller than 4 x 4 pixels (spacers, dots) are left out.
    const QVector<Picture> &pictures() const;
    // Premultiplied ARGB32; null (with `problem`) when it cannot be decoded.
    QImage decode(int index, QString *problem = nullptr) const;
    // The resolution to give the picture as an image: the placed one, rounded
    // to whole dpi when within 0.05 dpi; 0 when unknown.
    static double suggestedDpi(const Picture &picture);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
