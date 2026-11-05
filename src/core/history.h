#pragma once

#include <QObject>
#include <QImage>
#include <QString>
#include <QStringList>
#include "selection.h"
#include <functional>
#include <memory>
#include <vector>

class Document;

class HistoryCommand {
public:
    virtual ~HistoryCommand() = default;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual QString description() const = 0;
    // Calls `visit` for every image this step keeps, so the history can add up
    // its memory. Images shared with other steps or the document count once.
    virtual void forEachImage(const std::function<void(const QImage &)> &visit) const { Q_UNUSED(visit); }
};

struct ImageEditState { int index; QImage before, after; };
class MultiImageEditCommand : public HistoryCommand {
public:
    MultiImageEditCommand(Document *doc, std::vector<ImageEditState> edits, QString description);
    void undo() override;
    void redo() override;
    QString description() const override { return m_description; }
    void forEachImage(const std::function<void(const QImage &)> &visit) const override {
        for (const ImageEditState &e : m_edits) { visit(e.before); visit(e.after); }
    }
private:
    void apply(bool after);
    Document *m_doc;
    std::vector<ImageEditState> m_edits;
    QString m_description;
};

// One layer's pixels changed. Only the rectangle that actually differs is
// kept, so a brush stroke on a 5000 x 5000 px layer costs the size of the
// stroke, not two copies of the whole layer (200 MB).
class ImageEditCommand : public HistoryCommand {
public:
    ImageEditCommand(Document *doc, int layerIndex, const QImage &before, const QImage &after, const QString &desc);
    void undo() override;
    void redo() override;
    QString description() const override { return m_description; }
    // The changed rectangle in layer pixels; empty if nothing changed. Whole
    // images are kept (and this is the full rectangle) when the size changed.
    QRect changedRect() const { return m_rect; }
    void forEachImage(const std::function<void(const QImage &)> &visit) const override {
        visit(m_before);
        visit(m_after);
    }
    qint64 memoryBytes() const { return m_before.sizeInBytes() + m_after.sizeInBytes(); }
    // Smallest rectangle in which a and b differ (same size and format).
    static QRect differingRect(const QImage &a, const QImage &b);
private:
    void apply(const QImage &pixels);
    Document *m_doc;
    int m_layerIndex;
    bool m_partial = false;   // m_before/m_after hold just m_rect
    QRect m_rect;
    QSize m_layerSize;
    QImage m_before;
    QImage m_after;
    QString m_description;
};

// Nondestructive positioning for layers with retained off-canvas pixels.
class LayerOffsetCommand : public HistoryCommand {
public:
    LayerOffsetCommand(Document *doc, int index, QPoint before, QPoint after);
    void undo() override;
    void redo() override;
    QString description() const override { return "Move Layer"; }
private:
    void apply(QPoint offset);
    Document *m_doc;
    int m_index;
    QPoint m_before, m_after;
};

class LayerAddCommand : public HistoryCommand {
public:
    LayerAddCommand(Document *doc, int index, const QString &desc);
    void undo() override;
    void redo() override;
    QString description() const override { return m_description; }
    void forEachImage(const std::function<void(const QImage &)> &visit) const override;
private:
    Document *m_doc;
    int m_index;
    QString m_description;
    std::shared_ptr<class Layer> m_savedLayer;
};

class LayerRemoveCommand : public HistoryCommand {
public:
    LayerRemoveCommand(Document *doc, int index, const QString &desc);
    void undo() override;
    void redo() override;
    QString description() const override { return m_description; }
    void forEachImage(const std::function<void(const QImage &)> &visit) const override;
private:
    Document *m_doc;
    int m_index;
    QString m_description;
    std::shared_ptr<class Layer> m_savedLayer;
};

class LayerMoveCommand : public HistoryCommand {
public:
    LayerMoveCommand(Document *doc, int fromIndex, int toIndex, const QString &desc);
    void undo() override;
    void redo() override;
    QString description() const override { return m_description; }
private:
    Document *m_doc;
    int m_fromIndex;
    int m_toIndex;
    QString m_description;
};

// Generic full-document snapshot: captures the entire layer stack, size and
// active index. Used for structural operations that can't be represented by a
// single fine-grained command (merge, flatten, crop, resize, canvas size...).
class DocumentSnapshotCommand : public HistoryCommand {
public:
    DocumentSnapshotCommand(Document *doc, const QString &desc);
    // Call after the mutation has been performed to record the "after" state.
    void captureAfter();
    void undo() override;
    void redo() override;
    QString description() const override { return m_description; }
    void forEachImage(const std::function<void(const QImage &)> &visit) const override;
private:
    struct State {
        int width = 0;
        int height = 0;
        int activeLayer = 0;
        Selection selection;
        std::vector<std::shared_ptr<class Layer>> layers;
    };
    static State capture(Document *doc);
    static void restore(Document *doc, const State &state);
    Document *m_doc;
    QString m_description;
    State m_before;
    State m_after;
    bool m_hasAfter = false;
};

// Records a change to a single layer's properties (opacity, visibility,
// blend mode, name, lock) so those edits become undoable.
class LayerPropertyCommand : public HistoryCommand {
public:
    LayerPropertyCommand(Document *doc, int index,
                         float beforeOpacity, bool beforeVisible, int beforeBlend,
                         const QString &beforeName, bool beforeLocked,
                         const QString &desc);
    void captureAfter();
    void undo() override;
    void redo() override;
    QString description() const override { return m_description; }
private:
    struct Props { float opacity; bool visible; int blend; QString name; bool locked; };
    void apply(const Props &p);
    Document *m_doc;
    int m_index;
    Props m_before;
    Props m_after;
    QString m_description;
    bool m_hasAfter = false;
};

class HistoryManager : public QObject {
    Q_OBJECT
public:
    explicit HistoryManager(QObject *parent = nullptr);

    void push(std::unique_ptr<HistoryCommand> cmd);
    void undo();
    void redo();
    // Jump to a given point in history (0 = original, N = after N actions) by
    // undoing/redoing as needed. Used by the History panel's click-to-navigate.
    void goToIndex(int index);

    bool canUndo() const { return !m_undoStack.empty(); }
    bool canRedo() const { return !m_redoStack.empty(); }

    QString undoDescription() const;
    QString redoDescription() const;

    QStringList historyList() const;
    // Full timeline: done actions followed by the still-available redo actions.
    QStringList fullHistoryList() const;
    int currentIndex() const { return static_cast<int>(m_undoStack.size()); }
    // Undo and redo steps together.
    int stepCount() const { return static_cast<int>(m_undoStack.size() + m_redoStack.size()); }

    void clear();

    // Memory budget: undo and redo steps together may hold this many bytes
    // (0 = no limit). Beyond it the oldest steps are dropped; the latest step
    // is always kept, however large. At most kMaxSteps steps in any case.
    void setMemoryBudget(qint64 bytes);
    qint64 memoryBudget() const { return m_budget; }
    // Bytes the steps hold beyond what the document itself uses (pixels a
    // step shares with the open image or another step count once, or not).
    qint64 memoryUsed() const;
    // How the history sees the document's own images, which cost it nothing.
    using ImageVisitor = std::function<void(const QImage &)>;
    void setLiveImages(std::function<void(const ImageVisitor &)> liveImages);
    // Steps dropped to stay within the budget since the last clear().
    int droppedSteps() const { return m_dropped; }

    static constexpr int kMaxSteps = 1000;
    // 1/8 of the computer's memory, between 512 MB and 2 GB.
    static qint64 defaultMemoryBudget();
    // Settings "history/memoryLimitMB", or the default when not set.
    static qint64 configuredMemoryBudget();

signals:
    void historyChanged();

private:
    void trim();

    std::vector<std::unique_ptr<HistoryCommand>> m_undoStack;
    std::vector<std::unique_ptr<HistoryCommand>> m_redoStack;
    std::function<void(const ImageVisitor &)> m_liveImages;
    qint64 m_budget = 0;
    int m_dropped = 0;
};
