#include "shortcuts.h"

#include <QAction>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include "appinfo.h"
#include "panels/toolspanel.h"

namespace {
constexpr const char *kSettingsKey = "keyboard/custom";
}

Shortcuts::Shortcuts(QObject *parent) : QObject(parent) {
    load();
}

void Shortcuts::add(const Entry &newEntry) {
    // A platform's standard keys can name the same key twice (Redo: Ctrl+Y).
    Entry entry = newEntry;
    entry.defaults.clear();
    for (const QKeySequence &k : newEntry.defaults) {
        const QKeySequence one = firstCombination(k);
        if (!one.isEmpty() && !entry.defaults.contains(one)) entry.defaults << one;
    }
    bool replaced = false;
    for (Entry &e : m_entries) {
        if (e.id == entry.id) {
            e = entry;
            replaced = true;
            break;
        }
    }
    if (!replaced) m_entries.push_back(entry);
    apply(entry);
}

void Shortcuts::addMenuBar(QMenuBar *menuBar, const QList<QMenu *> &skip) {
    for (const ToolsPanel::Command &c : ToolsPanel::commandsFromMenuBar(menuBar, skip)) {
        if (!c.action) continue;
        Entry e;
        e.id = c.id;
        e.category = c.category();
        QStringList rest = c.path.mid(1);
        rest << c.label;
        e.label = rest.join(QStringLiteral(" > "));
        e.defaults = c.action->shortcuts();
        e.action = c.action;
        const QString tip = c.action->toolTip();
        if (!tip.isEmpty() && tip != c.label && !c.label.startsWith(tip)) e.hint = tip;
        add(e);
    }
}

const Shortcuts::Entry *Shortcuts::entry(const QString &id) const {
    for (const Entry &e : m_entries)
        if (e.id == id) return &e;
    return nullptr;
}

QStringList Shortcuts::categories() const {
    QStringList out;
    for (const Entry &e : m_entries)
        if (!out.contains(e.category)) out << e.category;
    return out;
}

QList<QKeySequence> Shortcuts::keys(const QString &id) const {
    auto it = m_custom.constFind(id);
    if (it != m_custom.constEnd()) return it.value();
    const Entry *e = entry(id);
    return e ? e->defaults : QList<QKeySequence>();
}

QKeySequence Shortcuts::key(const QString &id) const {
    const QList<QKeySequence> k = keys(id);
    return k.isEmpty() ? QKeySequence() : k.first();
}

bool Shortcuts::isChanged(const QString &id) const {
    const Entry *e = entry(id);
    return e && m_custom.contains(id) && m_custom.value(id) != e->defaults;
}

void Shortcuts::setKeys(const QString &id, const QList<QKeySequence> &keys) {
    const Entry *e = entry(id);
    if (!e) return;
    QList<QKeySequence> clean;
    for (const QKeySequence &k : keys) {
        const QKeySequence one = firstCombination(k);
        if (!one.isEmpty() && !clean.contains(one)) clean << one;
    }
    if (clean == e->defaults) m_custom.remove(id);
    else m_custom.insert(id, clean);
    apply(*e);
    save();
    emit changed();
}

void Shortcuts::reset(const QString &id) {
    if (!m_custom.remove(id)) return;
    if (const Entry *e = entry(id)) apply(*e);
    save();
    emit changed();
}

void Shortcuts::resetAll() {
    m_custom.clear();
    for (const Entry &e : m_entries) apply(e);
    save();
    emit changed();
}

QStringList Shortcuts::clashes(const QString &id, const QKeySequence &key) const {
    QStringList out;
    if (key.isEmpty()) return out;
    const Entry *self = entry(id);
    const QKeySequence one = firstCombination(key);
    for (const Entry &e : m_entries) {
        if (e.id == id) continue;
        if (self && self->kind == Kind::Tool && e.kind == Kind::Tool) continue;
        if (keys(e.id).contains(one)) out << e.id;
    }
    return out;
}

QString Shortcuts::title(const QString &id) const {
    const Entry *e = entry(id);
    if (!e) return id;
    return e->category + QStringLiteral(": ") + e->label;
}

QString Shortcuts::text(const QList<QKeySequence> &keys) {
    QStringList parts;
    for (const QKeySequence &k : keys) parts << k.toString(QKeySequence::NativeText);
    return parts.join(QStringLiteral(", "));
}

QKeySequence Shortcuts::firstCombination(const QKeySequence &key) {
    if (key.isEmpty()) return QKeySequence();
    return QKeySequence(key[0]);
}

void Shortcuts::apply(const Entry &e) const {
    if (e.action) e.action->setShortcuts(keys(e.id));
}

void Shortcuts::load() {
    m_custom.clear();
    const QString json = QSettings(AppInfo::kId, AppInfo::kId).value(kSettingsKey).toString();
    const QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        QList<QKeySequence> list;
        for (const QJsonValue &v : it.value().toArray()) {
            const QKeySequence k = QKeySequence::fromString(v.toString(), QKeySequence::PortableText);
            if (!k.isEmpty()) list << firstCombination(k);
        }
        m_custom.insert(it.key(), list);
    }
}

void Shortcuts::save() const {
    QJsonObject obj;
    for (auto it = m_custom.constBegin(); it != m_custom.constEnd(); ++it) {
        QJsonArray list;
        for (const QKeySequence &k : it.value()) list.append(k.toString(QKeySequence::PortableText));
        obj.insert(it.key(), list);
    }
    QSettings settings(AppInfo::kId, AppInfo::kId);
    if (obj.isEmpty()) settings.remove(kSettingsKey);
    else settings.setValue(kSettingsKey, QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}
