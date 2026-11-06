#include "texttool.h"
#include "canvas/canvaswidget.h"
#include "core/document.h"
#include "core/history.h"
#include "core/layer.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <algorithm>
#include <cmath>

namespace {

Qt::Alignment qtAlignment(TextTool::Align a) {
    switch (a) {
    case TextTool::Align::Center: return Qt::AlignHCenter;
    case TextTool::Align::Right: return Qt::AlignRight;
    case TextTool::Align::Justify: return Qt::AlignJustify;
    case TextTool::Align::Left: break;
    }
    return Qt::AlignLeft;
}

TextTool::Align toolAlignment(Qt::Alignment a) {
    if (a & Qt::AlignHCenter) return TextTool::Align::Center;
    if (a & Qt::AlignJustify) return TextTool::Align::Justify;
    if (a & Qt::AlignRight) return TextTool::Align::Right;
    return TextTool::Align::Left;
}

// Paragraph and line separators as plain new lines (clipboard).
QString plainFromSelection(QString text) {
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    text.replace(QChar::LineSeparator, QLatin1Char('\n'));
    return text;
}

QRectF toWidget(const QRectF &r, const CanvasWidget &canvas) {
    return QRectF(canvas.canvasToWidget(r.topLeft()), canvas.canvasToWidget(r.bottomRight())).normalized();
}

}  // namespace

TextTool::TextTool() {
    m_family = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
}

TextTool::~TextTool() {
    finishEditing();
}

// ---------------------------------------------------------------- state ----

double TextTool::dpi() const {
    return (m_doc && m_doc->dpi() > 0) ? m_doc->dpi() : 96.0;
}

Layer *TextTool::editedLayer() const {
    if (!m_doc || !m_layer) return nullptr;
    for (int i = 0; i < m_doc->layerCount(); ++i)
        if (m_doc->layerAt(i) == m_layer) return m_layer;
    return nullptr;
}

int TextTool::editedLayerIndex() const {
    if (!m_doc || !m_layer) return -1;
    for (int i = 0; i < m_doc->layerCount(); ++i)
        if (m_doc->layerAt(i) == m_layer) return i;
    return -1;
}

QTextCharFormat TextTool::newTextFormat(const QColor &color) const {
    QTextCharFormat f;
    f.setFontFamilies(QStringList{m_family});
    f.setFontPointSize(m_sizePt * dpi() / 72.0);   // pixels (see TextFrame)
    f.setFontWeight(m_bold ? QFont::Bold : QFont::Normal);
    f.setFontItalic(m_italic);
    f.setFontUnderline(m_underline);
    f.setFontStrikeOut(m_strikeout);
    f.setForeground(color);
    return f;
}

void TextTool::openDocument(const TextFrame &frame) {
    m_text = std::make_unique<QTextDocument>();
    frame.setUp(*m_text);
    m_text->setUndoRedoEnabled(true);
    m_text->clearUndoRedoStacks();
    m_cursor = QTextCursor(m_text.get());
    m_cursor.movePosition(QTextCursor::End);
}

void TextTool::startNewFrame(CanvasWidget &canvas, const QPointF &at) {
    Document *doc = canvas.document();
    if (!doc) return;
    m_doc = doc;
    m_canvas = &canvas;
    m_wasModified = doc->isModified();

    m_geometry = TextFrame();
    m_geometry.box = QRectF(at, QSizeF(0, 0));
    m_geometry.autoWidth = true;
    m_geometry.antialias = antialiased();
    QFont font(m_family);
    font.setPointSizeF(std::max(1.0, m_sizePt * dpi() / 72.0));
    font.setBold(m_bold);
    font.setItalic(m_italic);
    m_geometry.defaultFont = font;
    openDocument(m_geometry);

    const QTextCharFormat format = newTextFormat(doc->primaryColor());
    QTextBlockFormat block;
    block.setAlignment(qtAlignment(m_align));
    m_cursor.setBlockFormat(block);
    m_cursor.setBlockCharFormat(format);
    m_cursor.setCharFormat(format);
    m_text->clearUndoRedoStacks();

    auto layer = std::make_shared<Layer>(1, 1, QStringLiteral("Text"));
    m_layer = layer.get();
    m_previousActive = doc->activeLayerIndex();
    const int index = qBound(0, m_previousActive + 1, doc->layerCount());
    doc->insertLayerDirect(index, layer);
    m_newLayer = true;
    m_before.reset();
    m_changed = false;
    m_autoName = QStringLiteral("Text");
    doc->history().setBeforeChange([this] { finishEditing(); });
    refresh(false);
    notifyFormat();
}

bool TextTool::beginEditing(CanvasWidget &canvas, int layerIndex) {
    Document *doc = canvas.document();
    Layer *layer = doc ? doc->layerAt(layerIndex) : nullptr;
    if (!layer || layer->isLocked()) return false;
    auto text = layer->textFrame();
    if (!text) return false;
    if (isEditing()) finishEditing();

    m_doc = doc;
    m_canvas = &canvas;
    m_layer = layer;
    m_newLayer = false;
    m_changed = false;
    m_wasModified = doc->isModified();
    m_before = std::make_shared<const Layer>(*layer);
    m_geometry = *text;
    m_geometry.box.translate(layer->textShift());
    openDocument(m_geometry);
    m_geometry.html.clear();   // the live text is in m_text
    m_autoName = TextFrame::layerNameFor(m_text->toPlainText());
    doc->setActiveLayer(layerIndex);
    doc->history().setBeforeChange([this] { finishEditing(); });
    notifyFormat();
    canvas.update();
    return true;
}

void TextTool::refresh(bool markChanged) {
    Layer *layer = editedLayer();
    if (!layer || !m_text) return;
    m_geometry.antialias = antialiased();
    m_geometry.layOut(*m_text);
    TextFrame frame = m_geometry;
    frame.html = m_text->toHtml();
    const QRect oldArea(layer->offset(), layer->image().size());
    QPoint origin;
    const QImage drawn = TextFrame::render(*m_text, frame, &origin);
    layer->setText(std::make_shared<const TextFrame>(std::move(frame)), drawn, origin);

    // The layer is named after its first line until it is renamed by hand.
    const QString name = TextFrame::layerNameFor(m_text->toPlainText());
    if (layer->name() == m_autoName && name != m_autoName) {
        layer->setName(name);
        m_autoName = name;
    }
    if (markChanged) {
        m_changed = true;
        m_doc->setModified(true);
    }
    m_doc->notifyRegionChanged(oldArea.united(QRect(origin, drawn.size())));
}

void TextTool::finishEditing() {
    if (!m_text) return;
    QPointer<Document> doc = m_doc;
    std::unique_ptr<QTextDocument> text = std::move(m_text);   // isEditing() is false from here
    m_cursor = QTextCursor();
    m_drag = Drag::None;
    if (doc) {
        doc->history().setBeforeChange({});
        Layer *layer = nullptr;
        int index = -1;
        for (int i = 0; i < doc->layerCount(); ++i)
            if (doc->layerAt(i) == m_layer) { layer = m_layer; index = i; }
        const bool empty = text->toPlainText().isEmpty();
        if (m_newLayer) {
            if (layer && !empty) {
                doc->history().push(std::make_unique<LayerReplaceCommand>(
                    doc, index, nullptr, std::make_shared<const Layer>(*layer), QStringLiteral("Text")));
            } else if (layer) {
                // A click that typed nothing leaves no trace.
                doc->removeLayerDirect(index);
                doc->setActiveLayer(qBound(0, m_previousActive, doc->layerCount() - 1));
                doc->setModified(m_wasModified);
            }
        } else if (layer && m_changed) {
            doc->history().push(std::make_unique<LayerReplaceCommand>(
                doc, index, m_before, std::make_shared<const Layer>(*layer), QStringLiteral("Edit Text")));
        }
    }
    m_doc = nullptr;
    m_layer = nullptr;
    m_before.reset();
    m_newLayer = false;
    m_changed = false;
    notifyFormat();
    if (m_canvas) m_canvas->update();
}

void TextTool::deactivate(CanvasWidget &) { finishEditing(); }

void TextTool::commitText(CanvasWidget &) { finishEditing(); }

void TextTool::notifyFormat() {
    if (m_formatListener) m_formatListener();
}

QString TextTool::plainText() const {
    return m_text ? m_text->toPlainText() : QString();
}

void TextTool::selectAll() {
    if (!m_text) return;
    m_cursor.select(QTextCursor::Document);
    notifyFormat();
}

void TextTool::select(int from, int to) {
    if (!m_text) return;
    const int last = m_text->characterCount() - 1;
    m_cursor.setPosition(qBound(0, from, last));
    m_cursor.setPosition(qBound(0, to, last), QTextCursor::KeepAnchor);
    notifyFormat();
}

int TextTool::textLayerAt(const Document *doc, const QPointF &docPos) {
    if (!doc) return -1;
    for (int i = doc->layerCount() - 1; i >= 0; --i) {
        const Layer *layer = doc->layerAt(i);
        if (!layer || !layer->isVisible() || layer->isLocked()) continue;
        auto text = layer->textFrame();
        if (!text) continue;
        TextFrame placed = *text;
        placed.box.translate(layer->textShift());
        if (placed.frameRect().contains(docPos)) return i;
    }
    return -1;
}

// ------------------------------------------------------------- geometry ----

QRectF TextTool::frameRect() const {
    if (!m_text) return QRectF();
    return m_geometry.frameRect(*m_text);
}

QPointF TextTool::textPoint(const QPointF &docPos) const {
    return docPos - m_geometry.box.topLeft();
}

int TextTool::hitPosition(const QPointF &docPos) const {
    if (!m_text) return 0;
    m_text->size();   // make sure the layout is done
    const int pos = m_text->documentLayout()->hitTest(textPoint(docPos), Qt::FuzzyHit);
    return pos < 0 ? m_text->characterCount() - 1 : pos;
}

QRectF TextTool::caretRect() const {
    if (!m_text) return QRectF();
    m_text->size();
    const QTextBlock block = m_cursor.block();
    const QTextLayout *layout = block.layout();
    const int rel = m_cursor.position() - block.position();
    QPointF origin = m_text->documentLayout()->blockBoundingRect(block).topLeft();
    double x = 0, y = 0;
    double h = QFontMetricsF(fontAtCaret(), TextFrame::layoutDevice()).height();
    if (layout && layout->lineCount() > 0) {
        QTextLine line = layout->lineForTextPosition(rel);
        if (!line.isValid()) line = layout->lineAt(layout->lineCount() - 1);
        x = line.cursorToX(rel);
        y = line.y();
        h = line.height();
        origin = layout->position();
    }
    return QRectF(m_geometry.box.topLeft() + origin + QPointF(x, y), QSizeF(0, h));
}

std::vector<QRectF> TextTool::selectionRects() const {
    std::vector<QRectF> rects;
    if (!m_text || !m_cursor.hasSelection()) return rects;
    m_text->size();
    const int start = m_cursor.selectionStart();
    const int end = m_cursor.selectionEnd();
    for (QTextBlock block = m_text->findBlock(start); block.isValid() && block.position() <= end;
         block = block.next()) {
        const QTextLayout *layout = block.layout();
        if (!layout) continue;
        const QPointF blockPos = layout->position();
        for (int i = 0; i < layout->lineCount(); ++i) {
            const QTextLine line = layout->lineAt(i);
            const int lineStart = block.position() + line.textStart();
            const int lineEnd = lineStart + line.textLength();
            const int from = std::max(start, lineStart);
            const int to = std::min(end, lineEnd);
            if (from > to || (from == to && end <= lineEnd)) continue;
            double x1 = line.cursorToX(from - block.position());
            double x2 = line.cursorToX(to - block.position());
            if (end > lineEnd) x2 = std::max(x2, line.x() + line.naturalTextWidth() + line.height() * 0.25);
            if (x2 < x1) std::swap(x1, x2);
            rects.emplace_back(m_geometry.box.topLeft() + blockPos + QPointF(x1, line.y()),
                               QSizeF(std::max(1.0, x2 - x1), line.height()));
        }
    }
    return rects;
}

int TextTool::handleAt(const QPointF &docPos, double zoom) const {
    if (!m_text) return -1;
    const QRectF r = frameRect();
    const QPointF points[8] = {r.topLeft(), {r.center().x(), r.top()}, r.topRight(),
                               {r.right(), r.center().y()}, r.bottomRight(),
                               {r.center().x(), r.bottom()}, r.bottomLeft(), {r.left(), r.center().y()}};
    const double reach = 6.0 / std::max(zoom, 1e-6);
    for (int i = 0; i < 8; ++i)
        if (std::abs(docPos.x() - points[i].x()) <= reach && std::abs(docPos.y() - points[i].y()) <= reach)
            return i;
    return -1;
}

bool TextTool::onBorder(const QPointF &docPos, double zoom) const {
    if (!m_text) return false;
    const QRectF r = frameRect();
    const double outer = 6.0 / std::max(zoom, 1e-6);
    const double inner = 3.0 / std::max(zoom, 1e-6);
    const QRectF in = r.adjusted(inner, inner, -inner, -inner);
    return r.adjusted(-outer, -outer, outer, outer).contains(docPos) && !(in.isValid() && in.contains(docPos));
}

QCursor TextTool::cursorAt(const QPointF &canvasPos, const CanvasWidget &canvas) const {
    if (!isEditing() || canvas.document() != m_doc) return cursor();
    switch (handleAt(canvasPos, canvas.zoom())) {
    case 0: case 4: return Qt::SizeFDiagCursor;
    case 2: case 6: return Qt::SizeBDiagCursor;
    case 1: case 5: return Qt::SizeVerCursor;
    case 3: case 7: return Qt::SizeHorCursor;
    default: break;
    }
    if (onBorder(canvasPos, canvas.zoom())) return Qt::SizeAllCursor;
    return cursor();
}

// ---------------------------------------------------------------- mouse ----

void TextTool::mousePressEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) {
    if (event->button() != Qt::LeftButton) return;
    Document *doc = canvas.document();
    if (!doc) return;
    const double zoom = canvas.zoom();

    if (isEditing() && m_doc == doc && editedLayer()) {
        m_handle = handleAt(canvasPos, zoom);
        if (m_handle >= 0) {
            m_drag = Drag::Resize;
            m_dragStart = canvasPos;
            m_startBox = frameRect();
            return;
        }
        if (onBorder(canvasPos, zoom)) {
            m_drag = Drag::Move;
            m_dragStart = canvasPos;
            m_startBox = m_geometry.box;
            m_startOffset = m_layer->offset();
            return;
        }
        if (frameRect().contains(canvasPos)) {
            const int hit = hitPosition(canvasPos);
            if (event->type() == QEvent::MouseButtonDblClick) {
                m_cursor.setPosition(hit);
                m_cursor.select(QTextCursor::WordUnderCursor);
            } else {
                m_cursor.setPosition(hit, (event->modifiers() & Qt::ShiftModifier) ? QTextCursor::KeepAnchor
                                                                                   : QTextCursor::MoveAnchor);
            }
            m_drag = Drag::Select;
            notifyFormat();
            canvas.update();
            return;
        }
    }
    // A click outside the frame finishes it, then edits another text layer
    // or starts a new frame there (like Paint.NET's Text tool).
    finishEditing();

    const int index = textLayerAt(doc, canvasPos);
    if (index >= 0 && beginEditing(canvas, index)) {
        m_cursor.setPosition(hitPosition(canvasPos));
        m_drag = Drag::Select;
        notifyFormat();
        canvas.update();
        return;
    }
    startNewFrame(canvas, canvasPos);
    m_drag = Drag::Create;
    m_dragStart = canvasPos;
    canvas.update();
}

void TextTool::mouseMoveEvent(const QPointF &canvasPos, QMouseEvent *event, CanvasWidget &canvas) {
    if (!(event->buttons() & Qt::LeftButton) || !isEditing() || !editedLayer()) return;
    const double zoom = std::max(canvas.zoom(), 1e-6);
    switch (m_drag) {
    case Drag::Create: {
        // Dragging opens a frame of that size; text wraps inside it.
        const QRectF r = QRectF(m_dragStart, canvasPos).normalized();
        if (r.width() * zoom >= 4.0 || r.height() * zoom >= 4.0) {
            m_geometry.box = r;
            m_geometry.autoWidth = false;
        } else {
            m_geometry.box = QRectF(m_dragStart, QSizeF(0, 0));
            m_geometry.autoWidth = true;
        }
        refresh(false);
        break;
    }
    case Drag::Select:
        m_cursor.setPosition(hitPosition(canvasPos), QTextCursor::KeepAnchor);
        notifyFormat();
        break;
    case Drag::Move: {
        // The drawing moves with the layer's offset; it is drawn again on release.
        const QPoint delta = (canvasPos - m_dragStart).toPoint();
        const QRect oldArea(m_layer->offset(), m_layer->image().size());
        m_geometry.box = m_startBox.translated(delta);
        m_layer->setOffset(m_startOffset + delta);
        m_doc->setModified(true);
        m_changed = true;
        m_doc->notifyRegionChanged(oldArea.united(QRect(m_layer->offset(), m_layer->image().size())));
        break;
    }
    case Drag::Resize: {
        const QPointF d = canvasPos - m_dragStart;
        QRectF r = m_startBox;
        const double minSide = 4.0;
        switch (m_handle) {
        case 0: r.setTopLeft(r.topLeft() + d); break;
        case 1: r.setTop(r.top() + d.y()); break;
        case 2: r.setTopRight(r.topRight() + d); break;
        case 3: r.setRight(r.right() + d.x()); break;
        case 4: r.setBottomRight(r.bottomRight() + d); break;
        case 5: r.setBottom(r.bottom() + d.y()); break;
        case 6: r.setBottomLeft(r.bottomLeft() + d); break;
        case 7: r.setLeft(r.left() + d.x()); break;
        default: break;
        }
        if (r.width() < minSide) {
            if (m_handle == 0 || m_handle == 6 || m_handle == 7) r.setLeft(r.right() - minSide);
            else r.setRight(r.left() + minSide);
        }
        if (r.height() < minSide) {
            if (m_handle == 0 || m_handle == 1 || m_handle == 2) r.setTop(r.bottom() - minSide);
            else r.setBottom(r.top() + minSide);
        }
        m_geometry.box = r;
        if (m_handle != 1 && m_handle != 5) m_geometry.autoWidth = false;   // a set width wraps
        refresh();
        break;
    }
    case Drag::None:
        break;
    }
    canvas.update();
}

void TextTool::mouseReleaseEvent(const QPointF &, QMouseEvent *, CanvasWidget &canvas) {
    if (m_drag == Drag::Move && isEditing()) refresh();   // draw it at its new place
    m_drag = Drag::None;
    m_handle = -1;
    canvas.update();
}

// ------------------------------------------------------------- keyboard ----

bool TextTool::wantsShortcut(const QKeyEvent *event) const {
    if (!isEditing()) return false;
    const auto mods = event->modifiers();
    if (!(mods & Qt::ControlModifier) || (mods & (Qt::AltModifier | Qt::MetaModifier))) return false;
    switch (event->key()) {
    case Qt::Key_A: case Qt::Key_C: case Qt::Key_X: case Qt::Key_V:
    case Qt::Key_Z: case Qt::Key_Y: case Qt::Key_B: case Qt::Key_I: case Qt::Key_U:
    case Qt::Key_Left: case Qt::Key_Right: case Qt::Key_Up: case Qt::Key_Down:
    case Qt::Key_Home: case Qt::Key_End: case Qt::Key_Backspace: case Qt::Key_Delete:
    case Qt::Key_Return: case Qt::Key_Enter:
        return true;
    default:
        return false;
    }
}

void TextTool::moveCursor(QTextCursor::MoveOperation op, bool select) {
    if (!select && m_cursor.hasSelection()
        && (op == QTextCursor::Left || op == QTextCursor::Right)) {
        // Left or Right with a selection goes to its start or end.
        const int pos = op == QTextCursor::Left ? m_cursor.selectionStart() : m_cursor.selectionEnd();
        m_cursor.setPosition(pos);
        return;
    }
    m_cursor.movePosition(op, select ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
}

void TextTool::keyPressEvent(QKeyEvent *event, CanvasWidget &canvas) {
    if (!isEditing()) return;
    const auto mods = event->modifiers();
    const bool ctrl = mods & Qt::ControlModifier;
    const bool shift = mods & Qt::ShiftModifier;
    bool edited = false;

    switch (event->key()) {
    case Qt::Key_Escape:
        finishEditing();
        canvas.update();
        event->accept();
        return;
    case Qt::Key_Left: moveCursor(ctrl ? QTextCursor::PreviousWord : QTextCursor::Left, shift); break;
    case Qt::Key_Right: moveCursor(ctrl ? QTextCursor::NextWord : QTextCursor::Right, shift); break;
    case Qt::Key_Up: moveCursor(QTextCursor::Up, shift); break;
    case Qt::Key_Down: moveCursor(QTextCursor::Down, shift); break;
    case Qt::Key_Home: moveCursor(ctrl ? QTextCursor::Start : QTextCursor::StartOfLine, shift); break;
    case Qt::Key_End: moveCursor(ctrl ? QTextCursor::End : QTextCursor::EndOfLine, shift); break;
    case Qt::Key_Backspace:
        if (m_cursor.hasSelection()) m_cursor.removeSelectedText();
        else if (ctrl) {
            m_cursor.movePosition(QTextCursor::PreviousWord, QTextCursor::KeepAnchor);
            m_cursor.removeSelectedText();
        } else m_cursor.deletePreviousChar();
        edited = true;
        break;
    case Qt::Key_Delete:
        if (m_cursor.hasSelection()) m_cursor.removeSelectedText();
        else if (ctrl) {
            m_cursor.movePosition(QTextCursor::NextWord, QTextCursor::KeepAnchor);
            m_cursor.removeSelectedText();
        } else m_cursor.deleteChar();
        edited = true;
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (ctrl) {   // Ctrl+Enter finishes, like clicking outside
            finishEditing();
            canvas.update();
            event->accept();
            return;
        }
        if (shift) m_cursor.insertText(QString(QChar::LineSeparator));   // line break, same paragraph
        else m_cursor.insertBlock();
        edited = true;
        break;
    default:
        if (ctrl && !(mods & (Qt::AltModifier | Qt::MetaModifier))) {
            switch (event->key()) {
            case Qt::Key_A: m_cursor.select(QTextCursor::Document); break;
            case Qt::Key_C:
                if (m_cursor.hasSelection())
                    QApplication::clipboard()->setText(plainFromSelection(m_cursor.selectedText()));
                break;
            case Qt::Key_X:
                if (m_cursor.hasSelection()) {
                    QApplication::clipboard()->setText(plainFromSelection(m_cursor.selectedText()));
                    m_cursor.removeSelectedText();
                    edited = true;
                }
                break;
            case Qt::Key_V: insertText(QApplication::clipboard()->text(), canvas); break;
            case Qt::Key_Z:
                if (shift) m_text->redo(&m_cursor); else m_text->undo(&m_cursor);
                edited = true;
                break;
            case Qt::Key_Y: m_text->redo(&m_cursor); edited = true; break;
            case Qt::Key_B: setBold(!bold()); break;
            case Qt::Key_I: setItalic(!italic()); break;
            case Qt::Key_U: setUnderline(!underline()); break;
            default: return;
            }
            break;
        }
        {
            const QString typed = event->text();
            QString printable;
            for (QChar ch : typed)
                if (ch.isPrint() || ch.isSurrogate()) printable.append(ch);
            if (printable.isEmpty() || (mods & (Qt::AltModifier | Qt::MetaModifier))) return;
            m_cursor.insertText(printable);
            edited = true;
        }
        break;
    }
    if (edited) refresh();
    notifyFormat();
    canvas.update();
    event->accept();
}

void TextTool::insertText(const QString &text, CanvasWidget &canvas) {
    if (!isEditing() || text.isEmpty()) return;
    QString clean = text;
    clean.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    m_cursor.insertText(clean);   // '\n' starts a new paragraph
    refresh();
    notifyFormat();
    canvas.update();
}

void TextTool::inputMethodEvent(QInputMethodEvent *event, CanvasWidget &canvas) {
    if (!isEditing()) return;
    if (event->replacementLength() > 0) {
        const int start = m_cursor.position() + event->replacementStart();
        m_cursor.setPosition(start);
        m_cursor.setPosition(start + event->replacementLength(), QTextCursor::KeepAnchor);
        m_cursor.removeSelectedText();
    }
    if (!event->commitString().isEmpty() || event->replacementLength() > 0) {
        m_cursor.insertText(event->commitString());
        refresh();
        notifyFormat();
        canvas.update();
    }
    event->accept();
}

QVariant TextTool::inputMethodQuery(Qt::InputMethodQuery query, const CanvasWidget &canvas) const {
    switch (query) {
    case Qt::ImEnabled:
        return isEditing();
    case Qt::ImCursorRectangle:
        return isEditing() ? toWidget(caretRect(), canvas).toAlignedRect() : QRect();
    case Qt::ImFont:
        return isEditing() ? fontAtCaret() : QFont();
    case Qt::ImCursorPosition:
        return isEditing() ? m_cursor.position() - m_cursor.block().position() : 0;
    case Qt::ImAnchorPosition:
        return isEditing() ? qBound(0, m_cursor.anchor() - m_cursor.block().position(), m_cursor.block().length())
                           : 0;
    case Qt::ImSurroundingText:
        return isEditing() ? m_cursor.block().text() : QString();
    case Qt::ImCurrentSelection:
        return isEditing() ? plainFromSelection(m_cursor.selectedText()) : QString();
    case Qt::ImHints:
        return int(Qt::ImhMultiLine);
    default:
        return QVariant();
    }
}

// --------------------------------------------------------------- format ----

QTextCharFormat TextTool::formatAtCaret() const {
    return m_text ? m_cursor.charFormat() : QTextCharFormat();
}

QFont TextTool::fontAtCaret() const {
    if (!m_text) return font();
    const QTextCharFormat f = formatAtCaret();
    QFont result = m_text->defaultFont();
    const QStringList families = f.fontFamilies().toStringList();
    if (!families.isEmpty()) result.setFamilies(families);
    if (f.hasProperty(QTextFormat::FontPointSize)) result.setPointSizeF(f.fontPointSize());
    if (f.hasProperty(QTextFormat::FontWeight)) result.setWeight(QFont::Weight(f.fontWeight()));
    if (f.hasProperty(QTextFormat::FontItalic)) result.setItalic(f.fontItalic());
    result.setUnderline(f.fontUnderline());
    result.setStrikeOut(f.fontStrikeOut());
    return result;
}

void TextTool::applyCharFormat(const QTextCharFormat &format) {
    if (!isEditing()) return;
    m_cursor.mergeCharFormat(format);
    if (!m_cursor.hasSelection() && m_text->toPlainText().isEmpty())
        m_cursor.mergeBlockCharFormat(format);   // an empty frame takes it at once
    refresh();
    notifyFormat();
    if (m_canvas) m_canvas->update();
}

void TextTool::setFont(const QFont &font) {
    setFontFamily(font.family());
    if (font.pointSizeF() > 0) setFontSize(font.pointSizeF());
    setBold(font.bold());
    setItalic(font.italic());
    setUnderline(font.underline());
    setStrikeOut(font.strikeOut());
}

QFont TextTool::font() const {
    QFont f(fontFamily());
    f.setPointSizeF(fontSize());
    f.setBold(bold());
    f.setItalic(italic());
    f.setUnderline(underline());
    f.setStrikeOut(strikeOut());
    return f;
}

void TextTool::setFontFamily(const QString &family) {
    if (family.isEmpty()) return;
    m_family = family;
    if (!isEditing()) return;
    QTextCharFormat f;
    f.setFontFamilies(QStringList{family});
    applyCharFormat(f);
}

QString TextTool::fontFamily() const {
    return isEditing() ? fontAtCaret().family() : m_family;
}

void TextTool::setFontSize(double points) {
    m_sizePt = std::clamp(points, 1.0, 2000.0);
    if (!isEditing()) return;
    QTextCharFormat f;
    f.setFontPointSize(m_sizePt * dpi() / 72.0);
    applyCharFormat(f);
}

double TextTool::fontSize() const {
    if (!isEditing()) return m_sizePt;
    return fontAtCaret().pointSizeF() * 72.0 / dpi();
}

void TextTool::setBold(bool on) {
    m_bold = on;
    if (!isEditing()) return;
    QTextCharFormat f;
    f.setFontWeight(on ? QFont::Bold : QFont::Normal);
    applyCharFormat(f);
}

bool TextTool::bold() const { return isEditing() ? fontAtCaret().weight() >= QFont::DemiBold : m_bold; }

void TextTool::setItalic(bool on) {
    m_italic = on;
    if (!isEditing()) return;
    QTextCharFormat f;
    f.setFontItalic(on);
    applyCharFormat(f);
}

bool TextTool::italic() const { return isEditing() ? fontAtCaret().italic() : m_italic; }

void TextTool::setUnderline(bool on) {
    m_underline = on;
    if (!isEditing()) return;
    QTextCharFormat f;
    f.setFontUnderline(on);
    applyCharFormat(f);
}

bool TextTool::underline() const { return isEditing() ? fontAtCaret().underline() : m_underline; }

void TextTool::setStrikeOut(bool on) {
    m_strikeout = on;
    if (!isEditing()) return;
    QTextCharFormat f;
    f.setFontStrikeOut(on);
    applyCharFormat(f);
}

bool TextTool::strikeOut() const { return isEditing() ? fontAtCaret().strikeOut() : m_strikeout; }

void TextTool::setAlignment(Align a) {
    m_align = a;
    if (!isEditing()) return;
    QTextBlockFormat block;
    block.setAlignment(qtAlignment(a));
    m_cursor.mergeBlockFormat(block);   // every paragraph the selection touches
    refresh();
    notifyFormat();
    if (m_canvas) m_canvas->update();
}

TextTool::Align TextTool::alignment() const {
    return isEditing() ? toolAlignment(m_cursor.blockFormat().alignment()) : m_align;
}

void TextTool::setColor(const QColor &color) {
    if (!isEditing() || !color.isValid()) return;
    QTextCharFormat f;
    f.setForeground(color);
    applyCharFormat(f);
}

// -------------------------------------------------------------- drawing ----

void TextTool::drawOverlay(QPainter &painter, const CanvasWidget &canvas) {
    if (!isEditing() || canvas.document() != m_doc || !editedLayer()) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);

    const QRectF frame = toWidget(frameRect(), canvas);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::white, 1));
    painter.drawRect(frame);
    painter.setPen(QPen(QColor(40, 40, 40), 1, Qt::DashLine));
    painter.drawRect(frame);

    for (const QRectF &r : selectionRects())
        painter.fillRect(toWidget(r, canvas), QColor(0, 120, 215, 90));

    if (!m_cursor.hasSelection()) {
        const QRectF caret = toWidget(caretRect(), canvas);
        const QLineF line(caret.topLeft(), caret.bottomLeft());
        painter.setPen(QPen(QColor(255, 255, 255, 200), 3));
        painter.drawLine(line);
        painter.setPen(QPen(Qt::black, 1));
        painter.drawLine(line);
    }

    const QRectF r = frame;
    const QPointF points[8] = {r.topLeft(), {r.center().x(), r.top()}, r.topRight(),
                               {r.right(), r.center().y()}, r.bottomRight(),
                               {r.center().x(), r.bottom()}, r.bottomLeft(), {r.left(), r.center().y()}};
    painter.setPen(QPen(QColor(40, 40, 40), 1));
    painter.setBrush(Qt::white);
    for (const QPointF &p : points) painter.drawRect(QRectF(p - QPointF(3.5, 3.5), QSizeF(7, 7)));
    painter.restore();
}
