#include <QFile>
#include <QSet>
#include <QSettings>
#include "appinfo.h"
#include "history.h"
#include "document.h"
#include "layer.h"
#include <algorithm>
#include <cstring>
#include <QPainter>

MultiImageEditCommand::MultiImageEditCommand(Document *doc, std::vector<ImageEditState> edits, QString description)
    : m_doc(doc), m_edits(std::move(edits)), m_description(std::move(description)) {}
void MultiImageEditCommand::apply(bool after) {
    for (const auto &edit : m_edits)
        if (auto *layer = m_doc->layerAt(edit.index)) layer->setImage(after ? edit.after : edit.before);
    m_doc->notifyContentChanged();
}
void MultiImageEditCommand::undo() { apply(false); }
void MultiImageEditCommand::redo() { apply(true); }

// ImageEditCommand
ImageEditCommand::ImageEditCommand(Document *doc, int layerIndex, const QImage &before, const QImage &after, const QString &desc)
    : m_doc(doc), m_layerIndex(layerIndex), m_description(desc) {
    if (!before.isNull() && before.size() == after.size() && before.format() == after.format()
        && before.depth() == 32) {
        m_partial = true;
        m_layerSize = after.size();
        m_rect = differingRect(before, after);
        if (!m_rect.isEmpty()) {
            m_before = before.copy(m_rect);
            m_after = after.copy(m_rect);
        }
    } else {
        m_before = before;
        m_after = after;
        m_rect = after.rect();
    }
}

QRect ImageEditCommand::differingRect(const QImage &a, const QImage &b) {
    if (a.size() != b.size() || a.format() != b.format() || a.depth() != 32) return a.rect();
    const int w = a.width(), h = a.height();
    const size_t rowBytes = size_t(w) * 4;
    auto same = [&](int y) { return std::memcmp(a.constScanLine(y), b.constScanLine(y), rowBytes) == 0; };
    int top = 0;
    while (top < h && same(top)) ++top;
    if (top == h) return QRect();
    int bottom = h - 1;
    while (bottom > top && same(bottom)) --bottom;
    int left = w, right = -1;
    for (int y = top; y <= bottom; ++y) {
        const quint32 *la = reinterpret_cast<const quint32 *>(a.constScanLine(y));
        const quint32 *lb = reinterpret_cast<const quint32 *>(b.constScanLine(y));
        int x = 0;
        while (x < left && la[x] == lb[x]) ++x;
        left = std::min(left, x);
        int r = w - 1;
        while (r > right && la[r] == lb[r]) --r;
        right = std::max(right, r);
    }
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

void ImageEditCommand::apply(const QImage &pixels) {
    if (m_layerIndex < 0 || m_layerIndex >= m_doc->layerCount()) return;
    Layer *layer = m_doc->layerAt(m_layerIndex);
    if (!m_partial) {
        layer->setImage(pixels);
        m_doc->notifyContentChanged();
        return;
    }
    if (m_rect.isEmpty()) return;
    if (layer->image().size() != m_layerSize) return;   // history out of step; leave it
    QPainter painter(&layer->image());
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(m_rect.topLeft(), pixels);
    painter.end();
    m_doc->notifyRegionChanged(m_rect.translated(layer->offset()));
}

void ImageEditCommand::undo() { apply(m_before); }
void ImageEditCommand::redo() { apply(m_after); }

LayerOffsetCommand::LayerOffsetCommand(Document *doc, int index, QPoint before, QPoint after)
    : m_doc(doc), m_index(index), m_before(before), m_after(after) {}
void LayerOffsetCommand::apply(QPoint offset) {
    if (auto *layer = m_doc->layerAt(m_index)) {
        layer->setOffset(offset);
        m_doc->setModified(true);
        m_doc->notifyContentChanged();
    }
}
void LayerOffsetCommand::undo() { apply(m_before); }
void LayerOffsetCommand::redo() { apply(m_after); }

// LayerAddCommand
LayerAddCommand::LayerAddCommand(Document *doc, int index, const QString &desc)
    : m_doc(doc), m_index(index), m_description(desc) {}

void LayerAddCommand::undo() {
    m_savedLayer = std::make_shared<Layer>(*m_doc->layerAt(m_index));
    m_doc->removeLayerDirect(m_index);
}

void LayerAddCommand::forEachImage(const std::function<void(const QImage &)> &visit) const {
    if (m_savedLayer) visit(m_savedLayer->image());
}

void LayerAddCommand::redo() {
    if (m_savedLayer)
        m_doc->insertLayerDirect(m_index, std::make_shared<Layer>(*m_savedLayer));
}

// LayerReplaceCommand
LayerReplaceCommand::LayerReplaceCommand(Document *doc, int index, std::shared_ptr<const Layer> before,
                                         std::shared_ptr<const Layer> after, const QString &desc)
    : m_doc(doc), m_index(index), m_before(std::move(before)), m_after(std::move(after)),
      m_description(desc) {}

void LayerReplaceCommand::apply(const std::shared_ptr<const Layer> &from, const std::shared_ptr<const Layer> &to) {
    if (from && !to) {
        m_doc->removeLayerDirect(m_index);
        m_doc->setActiveLayer(qBound(0, m_index - 1, m_doc->layerCount() - 1));
    } else if (!from && to) {
        m_doc->insertLayerDirect(m_index, std::make_shared<Layer>(*to));
    } else if (from && to) {
        m_doc->replaceLayerDirect(m_index, std::make_shared<Layer>(*to));
    }
    m_doc->setModified(true);
}

void LayerReplaceCommand::undo() { apply(m_after, m_before); }
void LayerReplaceCommand::redo() { apply(m_before, m_after); }

void LayerReplaceCommand::forEachImage(const std::function<void(const QImage &)> &visit) const {
    if (m_before) visit(m_before->image());
    if (m_after) visit(m_after->image());
}

// LayerRemoveCommand
LayerRemoveCommand::LayerRemoveCommand(Document *doc, int index, const QString &desc)
    : m_doc(doc), m_index(index), m_description(desc) {
    m_savedLayer = std::make_shared<Layer>(*m_doc->layerAt(index));
}

void LayerRemoveCommand::forEachImage(const std::function<void(const QImage &)> &visit) const {
    if (m_savedLayer) visit(m_savedLayer->image());
}

void LayerRemoveCommand::undo() {
    m_doc->insertLayerDirect(m_index, std::make_shared<Layer>(*m_savedLayer));
}

void LayerRemoveCommand::redo() {
    m_doc->removeLayerDirect(m_index);
}

// LayerMoveCommand
LayerMoveCommand::LayerMoveCommand(Document *doc, int fromIndex, int toIndex, const QString &desc)
    : m_doc(doc), m_fromIndex(fromIndex), m_toIndex(toIndex), m_description(desc) {}

void LayerMoveCommand::undo() {
    m_doc->moveLayerDirect(m_toIndex, m_fromIndex);
}

void LayerMoveCommand::redo() {
    m_doc->moveLayerDirect(m_fromIndex, m_toIndex);
}

// DocumentSnapshotCommand
DocumentSnapshotCommand::DocumentSnapshotCommand(Document *doc, const QString &desc)
    : m_doc(doc), m_description(desc) {
    m_before = capture(doc);
}

void DocumentSnapshotCommand::captureAfter() {
    m_after = capture(m_doc);
    m_hasAfter = true;
}

DocumentSnapshotCommand::State DocumentSnapshotCommand::capture(Document *doc) {
    State s;
    s.width = doc->width();
    s.height = doc->height();
    s.activeLayer = doc->activeLayerIndex();
    s.dpi = doc->dpi();
    s.layers = doc->snapshotLayers();
    s.selection = doc->selection();
    return s;
}

void DocumentSnapshotCommand::restore(Document *doc, const State &state) {
    std::vector<std::shared_ptr<Layer>> copy;
    copy.reserve(state.layers.size());
    for (const auto &l : state.layers)
        copy.push_back(std::make_shared<Layer>(*l));
    doc->setDpi(state.dpi);
    doc->restoreFromSnapshot(std::move(copy), state.width, state.height, state.activeLayer);
    doc->selection() = state.selection;
    emit doc->selectionChanged();
}

void DocumentSnapshotCommand::forEachImage(const std::function<void(const QImage &)> &visit) const {
    for (const State *state : {&m_before, &m_after}) {
        for (const auto &l : state->layers)
            if (l) visit(l->image());
        visit(state->selection.mask());
    }
}

void DocumentSnapshotCommand::undo() {
    restore(m_doc, m_before);
}

void DocumentSnapshotCommand::redo() {
    if (m_hasAfter)
        restore(m_doc, m_after);
}

// LayerPropertyCommand
LayerPropertyCommand::LayerPropertyCommand(Document *doc, int index,
                                           float beforeOpacity, bool beforeVisible, int beforeBlend,
                                           const QString &beforeName, bool beforeLocked,
                                           const QString &desc)
    : m_doc(doc), m_index(index), m_description(desc) {
    m_before = {beforeOpacity, beforeVisible, beforeBlend, beforeName, beforeLocked};
}

void LayerPropertyCommand::captureAfter() {
    if (auto *l = m_doc->layerAt(m_index)) {
        m_after = {l->opacity(), l->isVisible(), static_cast<int>(l->blendMode()),
                   l->name(), l->isLocked()};
        m_hasAfter = true;
    }
}

void LayerPropertyCommand::apply(const Props &p) {
    if (auto *l = m_doc->layerAt(m_index)) {
        l->setOpacity(p.opacity);
        l->setVisible(p.visible);
        l->setBlendMode(static_cast<BlendMode>(p.blend));
        l->setName(p.name);
        l->setLocked(p.locked);
        m_doc->notifyContentChanged();
    }
}

void LayerPropertyCommand::undo() { apply(m_before); }
void LayerPropertyCommand::redo() { if (m_hasAfter) apply(m_after); }

// HistoryManager
HistoryManager::HistoryManager(QObject *parent) : QObject(parent) {}

void HistoryManager::push(std::unique_ptr<HistoryCommand> cmd) {
    finishPendingEdit();   // an unfinished text edit comes first
    m_redoStack.clear();
    m_undoStack.push_back(std::move(cmd));
    trim();
    emit historyChanged();
}

void HistoryManager::setLiveImages(std::function<void(const ImageVisitor &)> liveImages) {
    m_liveImages = std::move(liveImages);
}

void HistoryManager::setMemoryBudget(qint64 bytes) {
    m_budget = qMax<qint64>(0, bytes);
    const size_t before = m_undoStack.size() + m_redoStack.size();
    trim();
    if (m_undoStack.size() + m_redoStack.size() != before) emit historyChanged();
}

qint64 HistoryManager::memoryUsed() const {
    QSet<qint64> live;
    if (m_liveImages)
        m_liveImages([&live](const QImage &img) { if (!img.isNull()) live.insert(img.cacheKey()); });
    QSet<qint64> seen;
    qint64 total = 0;
    // Copies of an image share its pixels until one is changed, and keep the
    // same cacheKey(); count each block of pixels once.
    const ImageVisitor add = [&](const QImage &img) {
        if (img.isNull()) return;
        const qint64 key = img.cacheKey();
        if (live.contains(key) || seen.contains(key)) return;
        seen.insert(key);
        total += img.sizeInBytes();
    };
    for (const auto &c : m_undoStack) c->forEachImage(add);
    for (const auto &c : m_redoStack) c->forEachImage(add);
    return total;
}

void HistoryManager::trim() {
    auto steps = [this]() { return m_undoStack.size() + m_redoStack.size(); };
    auto over = [this]() { return m_budget > 0 && memoryUsed() > m_budget; };
    // Oldest done steps first; the latest done step always stays.
    while (m_undoStack.size() > 1 && (steps() > size_t(kMaxSteps) || over())) {
        m_undoStack.erase(m_undoStack.begin());
        ++m_dropped;
    }
    // Still too much (the budget was lowered): the furthest redo steps go.
    while (!m_redoStack.empty() && (steps() > size_t(kMaxSteps) || over())) {
        m_redoStack.erase(m_redoStack.begin());
        ++m_dropped;
    }
}

qint64 HistoryManager::defaultMemoryBudget() {
    constexpr qint64 MB = 1024 * 1024;
    qint64 total = 0;
    QFile meminfo(QStringLiteral("/proc/meminfo"));
    if (meminfo.open(QIODevice::ReadOnly)) {
        for (const QByteArray &line : meminfo.readAll().split('\n')) {
            if (line.startsWith("MemTotal:")) {
                total = line.mid(9).trimmed().split(' ').value(0).toLongLong() * 1024;
                break;
            }
        }
    }
    if (total <= 0) return 1024 * MB;
    return qBound<qint64>(512 * MB, total / 8, 2048 * MB);
}

qint64 HistoryManager::configuredMemoryBudget() {
    const int mb = QSettings(AppInfo::kId, AppInfo::kId).value(QStringLiteral("history/memoryLimitMB"), 0).toInt();
    return mb > 0 ? qint64(mb) * 1024 * 1024 : defaultMemoryBudget();
}

void HistoryManager::setBeforeChange(std::function<void()> finishPendingEdit) {
    m_beforeChange = std::move(finishPendingEdit);
}

void HistoryManager::finishPendingEdit() {
    if (!m_beforeChange || m_finishing) return;
    m_finishing = true;
    auto finish = m_beforeChange;   // the call usually clears m_beforeChange
    finish();
    m_finishing = false;
}

void HistoryManager::undo() {
    finishPendingEdit();
    if (m_undoStack.empty()) return;
    auto cmd = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    cmd->undo();
    m_redoStack.push_back(std::move(cmd));
    emit historyChanged();
}

void HistoryManager::redo() {
    finishPendingEdit();
    if (m_redoStack.empty()) return;
    auto cmd = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    cmd->redo();
    m_undoStack.push_back(std::move(cmd));
    emit historyChanged();
}

void HistoryManager::goToIndex(int index) {
    finishPendingEdit();
    const int total = static_cast<int>(m_undoStack.size() + m_redoStack.size());
    index = std::max(0, std::min(index, total));
    // currentIndex() == m_undoStack.size(); undo to shrink it, redo to grow it.
    while (static_cast<int>(m_undoStack.size()) > index) undo();
    while (static_cast<int>(m_undoStack.size()) < index) redo();
}

QString HistoryManager::undoDescription() const {
    if (m_undoStack.empty()) return QString();
    return m_undoStack.back()->description();
}

QString HistoryManager::redoDescription() const {
    if (m_redoStack.empty()) return QString();
    return m_redoStack.back()->description();
}

QStringList HistoryManager::historyList() const {
    QStringList list;
    for (const auto &cmd : m_undoStack)
        list.append(cmd->description());
    return list;
}

QStringList HistoryManager::fullHistoryList() const {
    QStringList list;
    for (const auto &cmd : m_undoStack)
        list.append(cmd->description());
    // Redo stack is in reverse chronological order (back() = next to redo).
    for (auto it = m_redoStack.rbegin(); it != m_redoStack.rend(); ++it)
        list.append((*it)->description());
    return list;
}

void HistoryManager::clear() {
    m_dropped = 0;
    m_undoStack.clear();
    m_redoStack.clear();
    emit historyChanged();
}
