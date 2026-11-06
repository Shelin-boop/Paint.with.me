#include "textframe.h"

#include <QAbstractTextDocumentLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <algorithm>
#include <vector>
#include <cmath>

QPaintDevice *TextFrame::layoutDevice() {
    // 72 dpi: a font's point size is then its size in pixels, whatever the
    // screen. Layout and drawing must use the same device, or glyph sizes and
    // line breaks would differ between the editor and the drawn pixels.
    static QImage device = [] {
        QImage img(1, 1, QImage::Format_ARGB32_Premultiplied);
        const int dpm = qRound(72.0 / 0.0254);
        img.setDotsPerMeterX(dpm);
        img.setDotsPerMeterY(dpm);
        return img;
    }();
    return &device;
}

void TextFrame::setUp(QTextDocument &doc) const {
    doc.documentLayout()->setPaintDevice(layoutDevice());
    doc.setDocumentMargin(0);
    doc.setDefaultFont(defaultFont);
    doc.setHtml(html);
    layOut(doc);
}

void TextFrame::layOut(QTextDocument &doc) const {
    if (autoWidth) {
        doc.setTextWidth(-1);
        const double ideal = doc.idealWidth();
        doc.setTextWidth(std::max(1.0, std::ceil(ideal)));
    } else {
        doc.setTextWidth(std::max(1.0, box.width()));
    }
}

QRectF TextFrame::frameRect(const QTextDocument &laidOut) const {
    const double width = autoWidth ? laidOut.textWidth() : box.width();
    const double height = std::max(box.height(), laidOut.size().height());
    return QRectF(box.topLeft(), QSizeF(std::max(1.0, width), std::max(1.0, height)));
}

QRectF TextFrame::frameRect() const {
    QTextDocument doc;
    setUp(doc);
    return frameRect(doc);
}

double TextFrame::largestPixelSize(const QTextDocument &doc) {
    double largest = doc.defaultFont().pointSizeF();
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        largest = std::max(largest, block.charFormat().fontPointSize());
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid()) largest = std::max(largest, fragment.charFormat().fontPointSize());
        }
    }
    return std::max(1.0, largest);
}

QImage TextFrame::render(QTextDocument &laidOut, const TextFrame &geometry, QPoint *origin) {
    QTextDocument *source = &laidOut;
    std::unique_ptr<QTextDocument> aliased;
    if (!geometry.antialias) {
        // Hard-edged glyphs: the font engines are chosen per format, so the
        // style strategy goes onto every character of a copy.
        aliased.reset(laidOut.clone());
        aliased->documentLayout()->setPaintDevice(layoutDevice());
        aliased->setDocumentMargin(0);
        QTextCursor all(aliased.get());
        all.select(QTextCursor::Document);
        QTextCharFormat hard;
        hard.setFontStyleStrategy(QFont::NoAntialias);
        all.mergeCharFormat(hard);
        aliased->setTextWidth(laidOut.textWidth());
        source = aliased.get();
    }

    const int margin = int(std::ceil(largestPixelSize(*source) * 0.4)) + 2;
    const QRectF frame = geometry.frameRect(*source);
    const QRect area = frame.adjusted(-margin, -margin, margin, margin).toAlignedRect();
    QImage image(area.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, geometry.antialias);
        painter.setRenderHint(QPainter::TextAntialiasing, geometry.antialias);
        painter.translate(geometry.box.topLeft() - QPointF(area.topLeft()));
        // Text without its own color is black, whatever the program's theme.
        QAbstractTextDocumentLayout::PaintContext context;
        context.palette.setColor(QPalette::Text, Qt::black);
        source->documentLayout()->draw(&painter, context);
    }
    if (origin) *origin = area.topLeft();
    return image;
}

QImage TextFrame::render(QPoint *origin) const {
    QTextDocument doc;
    setUp(doc);
    return render(doc, *this, origin);
}

QString TextFrame::plainText() const {
    QTextDocument doc;
    doc.setHtml(html);
    return doc.toPlainText();
}

QString TextFrame::suggestedLayerName() const {
    return layerNameFor(plainText());
}

QString TextFrame::layerNameFor(const QString &text) {
    for (QString line : text.split(QRegularExpression(QStringLiteral("[\\n\\x{2028}\\x{2029}]")))) {
        line = line.simplified();
        if (line.isEmpty()) continue;
        if (line.size() > 30) line = line.left(30).trimmed() + QStringLiteral("...");
        return line;
    }
    return QStringLiteral("Text");
}

std::shared_ptr<TextFrame> TextFrame::scaled(double factor) const {
    auto result = std::make_shared<TextFrame>(*this);
    QTextDocument doc;
    setUp(doc);
    // Every run of characters, and each paragraph's own format (used when
    // typing into an empty line), gets its size multiplied. Collected first:
    // changing formats splits and merges the runs being walked.
    struct Run { int position, length; double size; };
    std::vector<Run> runs;
    std::vector<std::pair<int, double>> blocks;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        if (block.charFormat().fontPointSize() > 0)
            blocks.emplace_back(block.position(), block.charFormat().fontPointSize());
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) continue;
            const double size = fragment.charFormat().fontPointSize() > 0
                                    ? fragment.charFormat().fontPointSize()
                                    : defaultFont.pointSizeF();
            runs.push_back({fragment.position(), fragment.length(), size});
        }
    }
    for (const auto &[position, size] : blocks) {
        QTextCursor cursor(&doc);
        cursor.setPosition(position);
        QTextCharFormat format = cursor.blockCharFormat();
        format.setFontPointSize(size * factor);
        cursor.setBlockCharFormat(format);
    }
    for (const Run &run : runs) {
        QTextCursor cursor(&doc);
        cursor.setPosition(run.position);
        cursor.setPosition(run.position + run.length, QTextCursor::KeepAnchor);
        QTextCharFormat sized;
        sized.setFontPointSize(run.size * factor);
        cursor.mergeCharFormat(sized);
    }
    QFont font = defaultFont;
    font.setPointSizeF(std::max(1.0, defaultFont.pointSizeF() * factor));
    result->defaultFont = font;
    doc.setDefaultFont(font);
    result->html = doc.toHtml();
    result->box = QRectF(box.x() * factor, box.y() * factor, box.width() * factor, box.height() * factor);
    return result;
}

QByteArray TextFrame::serialize() const {
    QJsonObject o;
    o.insert(QStringLiteral("format"), QStringLiteral("paint-with-me text frame"));
    o.insert(QStringLiteral("version"), 1);
    o.insert(QStringLiteral("html"), html);
    o.insert(QStringLiteral("box"), QJsonArray{box.x(), box.y(), box.width(), box.height()});
    o.insert(QStringLiteral("autoWidth"), autoWidth);
    o.insert(QStringLiteral("antialias"), antialias);
    o.insert(QStringLiteral("font"), defaultFont.toString());
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

std::shared_ptr<TextFrame> TextFrame::deserialize(const QByteArray &data) {
    QJsonParseError error;
    const QJsonDocument json = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject()) return nullptr;
    const QJsonObject o = json.object();
    if (o.value(QStringLiteral("version")).toInt() != 1) return nullptr;
    if (!o.value(QStringLiteral("html")).isString()) return nullptr;
    const QJsonArray b = o.value(QStringLiteral("box")).toArray();
    if (b.size() != 4) return nullptr;
    auto frame = std::make_shared<TextFrame>();
    frame->html = o.value(QStringLiteral("html")).toString();
    frame->box = QRectF(b.at(0).toDouble(), b.at(1).toDouble(), b.at(2).toDouble(), b.at(3).toDouble());
    if (!std::isfinite(frame->box.x()) || !std::isfinite(frame->box.y())
        || !(frame->box.width() >= 0) || !(frame->box.height() >= 0))
        return nullptr;
    frame->autoWidth = o.value(QStringLiteral("autoWidth")).toBool();
    frame->antialias = o.value(QStringLiteral("antialias")).toBool(true);
    QFont font;
    if (!font.fromString(o.value(QStringLiteral("font")).toString())) font = QFont();
    if (font.pointSizeF() <= 0) font.setPointSizeF(12);
    frame->defaultFont = font;
    return frame;
}
