#pragma once

// Keyboard shortcuts that can be changed in Settings > Keyboard: every menu
// command, every tool, the painting and panel keys and the keys that work
// while held (pan, turn the view, color picker).
//
// Each command has a stable id (the settings key): menu commands use their
// menu path ("View > Zoom In"), the others a fixed name ("tool.paintbrush",
// "hold.pan"). Only keys that differ from the defaults are saved, as JSON in
// the setting `keyboard/custom` ({"id": ["Ctrl+Y", ...]}, an empty list
// meaning "no key").

#include <QKeySequence>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>

class QAction;
class QMenu;
class QMenuBar;

class Shortcuts : public QObject {
    Q_OBJECT
public:
    enum class Kind {
        Command,   // a key press runs it
        Tool,      // selects a tool; tools sharing a key take turns
        Hold,      // works while the key is held down
    };
    struct Entry {
        QString id;
        QString category;   // group in Settings: "File", ..., "Tools", "Painting", "Canvas"
        QString label;      // "Zoom In", "Rotate View > Rotate Left"
        QList<QKeySequence> defaults;
        Kind kind = Kind::Command;
        QPointer<QAction> action;   // receives the keys; null for tools and held keys
        QString hint;               // shown in Settings below the list
    };

    explicit Shortcuts(QObject *parent = nullptr);

    // Registers a command; its action gets the current keys at once. An id
    // registered again replaces the old entry.
    void add(const Entry &newEntry);
    // Every command of the menus, with the keys they have now as defaults.
    void addMenuBar(QMenuBar *menuBar, const QList<QMenu *> &skip = {});

    const QVector<Entry> &entries() const { return m_entries; }
    const Entry *entry(const QString &id) const;
    // Categories in the order they were first registered.
    QStringList categories() const;

    QList<QKeySequence> keys(const QString &id) const;
    QKeySequence key(const QString &id) const;   // the first key, or none
    bool isChanged(const QString &id) const;      // differs from the default

    // Changes, saves and applies the keys; emits changed().
    void setKeys(const QString &id, const QList<QKeySequence> &keys);
    void reset(const QString &id);
    void resetAll();

    // Commands other than `id` that use `key` and would clash with it. Tools
    // do not clash with each other: they take turns.
    QStringList clashes(const QString &id, const QKeySequence &key) const;
    // "Category: Label" for messages.
    QString title(const QString &id) const;

    // "Ctrl+Y, Ctrl+Shift+Z" as the platform writes them ("" = none).
    static QString text(const QList<QKeySequence> &keys);
    // A key sequence of one key combination only (a recorded "Ctrl+K, Ctrl+L"
    // keeps Ctrl+K).
    static QKeySequence firstCombination(const QKeySequence &key);

signals:
    void changed();

private:
    void apply(const Entry &e) const;
    void load();
    void save() const;

    QVector<Entry> m_entries;
    QMap<QString, QList<QKeySequence>> m_custom;   // id -> keys chosen in Settings
};
