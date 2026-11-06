#include "dialogs/keyboardpage.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "shortcuts.h"

namespace {
constexpr int kIdRole = Qt::UserRole;

// Groups in this order; any others follow in the order they were registered.
const QStringList kCategoryOrder = {
    QStringLiteral("File"),  QStringLiteral("Edit"),        QStringLiteral("View"),
    QStringLiteral("Image"), QStringLiteral("Layers"),      QStringLiteral("Adjustments"),
    QStringLiteral("Effects"), QStringLiteral("Tools"),     QStringLiteral("Painting"),
    QStringLiteral("Canvas"), QStringLiteral("Panels"),
};
}  // namespace

KeyboardPage::KeyboardPage(Shortcuts *shortcuts, QWidget *parent)
    : QWidget(parent), m_shortcuts(shortcuts) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_filter = new QLineEdit;
    m_filter->setObjectName(QStringLiteral("keyboardFilter"));
    m_filter->setPlaceholderText(QStringLiteral("Search a command or a key, for example \"rotate\" or \"Ctrl+Z\""));
    m_filter->setClearButtonEnabled(true);
    layout->addWidget(m_filter);

    m_tree = new QTreeWidget;
    m_tree->setObjectName(QStringLiteral("keyboardList"));
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({QStringLiteral("Command"), QStringLiteral("Keys")});
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(m_tree, 1);

    m_title = new QLabel;
    m_title->setObjectName(QStringLiteral("keyboardTitle"));
    m_title->setTextFormat(Qt::RichText);
    m_title->setWordWrap(true);
    layout->addWidget(m_title);
    m_hint = new QLabel;
    m_hint->setObjectName(QStringLiteral("keyboardHint"));
    m_hint->setWordWrap(true);
    m_hint->setTextFormat(Qt::PlainText);
    layout->addWidget(m_hint);

    auto *form = new QFormLayout;
    for (int i = 0; i < 2; ++i) {
        auto *edit = new QKeySequenceEdit;
        edit->setObjectName(i == 0 ? QStringLiteral("keyboardKey") : QStringLiteral("keyboardSecondKey"));
        edit->setClearButtonEnabled(true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        edit->setMaximumSequenceLength(1);   // one key combination, finished at once
#endif
        edit->setToolTip(QStringLiteral("Click and press the new key; the x button removes it."));
        connect(edit, &QKeySequenceEdit::editingFinished, this, [this, i]() { commit(i); });
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this, [this, i](const QKeySequence &seq) {
            if (seq.isEmpty()) commit(i);   // removed with the clear button
        });
        m_edits[i] = edit;
        form->addRow(i == 0 ? QStringLiteral("Key:") : QStringLiteral("Second key:"), edit);
    }
    layout->addLayout(form);

    auto *buttons = new QHBoxLayout;
    m_default = new QPushButton(QStringLiteral("Default Keys"));
    m_default->setObjectName(QStringLiteral("keyboardDefault"));
    m_default->setToolTip(QStringLiteral("Gives the chosen command its original keys back."));
    buttons->addWidget(m_default);
    buttons->addStretch();
    m_resetAll = new QPushButton(QStringLiteral("Reset All Keys..."));
    m_resetAll->setObjectName(QStringLiteral("keyboardResetAll"));
    buttons->addWidget(m_resetAll);
    layout->addLayout(buttons);

    connect(m_filter, &QLineEdit::textChanged, this, &KeyboardPage::applyFilter);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this]() { showSelected(); });
    connect(m_default, &QPushButton::clicked, this, [this]() {
        const QString id = currentId();
        const Shortcuts::Entry *e = m_shortcuts->entry(id);
        if (!e) return;
        QList<QKeySequence> keys;
        for (const QKeySequence &key : e->defaults) {
            const QStringList others = m_shortcuts->clashes(id, key);
            if (!others.isEmpty()) {
                if (!askToTake(id, key, others)) continue;
                for (const QString &other : others) {
                    QList<QKeySequence> theirs = m_shortcuts->keys(other);
                    theirs.removeAll(key);
                    m_shortcuts->setKeys(other, theirs);
                }
            }
            keys << key;
        }
        m_shortcuts->setKeys(id, keys);
    });
    connect(m_resetAll, &QPushButton::clicked, this, [this]() {
        if (m_testAnswer < 0
            && QMessageBox::question(this, QStringLiteral("Reset All Keys"),
                                     QStringLiteral("Give every command its original keys back?"))
                   != QMessageBox::Yes)
            return;
        m_shortcuts->resetAll();
    });
    connect(m_shortcuts, &Shortcuts::changed, this, &KeyboardPage::refreshTexts);

    rebuild();
    showSelected();
}

void KeyboardPage::rebuild() {
    m_tree->clear();
    QStringList categories;
    for (const QString &c : kCategoryOrder)
        if (m_shortcuts->categories().contains(c)) categories << c;
    for (const QString &c : m_shortcuts->categories())
        if (!categories.contains(c)) categories << c;

    for (const QString &category : categories) {
        auto *group = new QTreeWidgetItem(m_tree, {category});
        group->setFlags(Qt::ItemIsEnabled);
        QFont bold = group->font(0);
        bold.setBold(true);
        group->setFont(0, bold);
        group->setFirstColumnSpanned(true);
        for (const Shortcuts::Entry &e : m_shortcuts->entries()) {
            if (e.category != category) continue;
            auto *item = new QTreeWidgetItem(group, {e.label, QString()});
            item->setData(0, kIdRole, e.id);
            if (!e.hint.isEmpty()) item->setToolTip(0, e.hint);
        }
        group->setExpanded(true);
    }
    refreshTexts();
}

void KeyboardPage::refreshTexts() {
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_tree->topLevelItem(g);
        for (int i = 0; i < group->childCount(); ++i) {
            QTreeWidgetItem *item = group->child(i);
            const QString id = item->data(0, kIdRole).toString();
            item->setText(1, Shortcuts::text(m_shortcuts->keys(id)));
            // Changed keys in bold.
            QFont f = item->font(0);
            f.setBold(m_shortcuts->isChanged(id));
            item->setFont(0, f);
            item->setFont(1, f);
        }
    }
    applyFilter(m_filter->text());
    showSelected();
}

void KeyboardPage::applyFilter(const QString &text) {
    const QString needle = text.trimmed();
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_tree->topLevelItem(g);
        int shown = 0;
        for (int i = 0; i < group->childCount(); ++i) {
            QTreeWidgetItem *item = group->child(i);
            const bool match = needle.isEmpty()
                || item->text(0).contains(needle, Qt::CaseInsensitive)
                || item->text(1).contains(needle, Qt::CaseInsensitive)
                || group->text(0).contains(needle, Qt::CaseInsensitive);
            item->setHidden(!match);
            if (match) ++shown;
        }
        group->setHidden(shown == 0);
    }
}

QString KeyboardPage::currentId() const {
    const QTreeWidgetItem *item = m_tree->currentItem();
    return item ? item->data(0, kIdRole).toString() : QString();
}

bool KeyboardPage::selectCommand(const QString &id) {
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_tree->topLevelItem(g);
        for (int i = 0; i < group->childCount(); ++i) {
            if (group->child(i)->data(0, kIdRole).toString() == id) {
                m_tree->setCurrentItem(group->child(i));
                return true;
            }
        }
    }
    return false;
}

void KeyboardPage::enterKey(int field, const QKeySequence &key) {
    if (field < 0 || field > 1) return;
    m_loading = true;
    m_edits[field]->setKeySequence(key);
    m_loading = false;
    commit(field);
}

void KeyboardPage::showSelected() {
    const QString id = currentId();
    const Shortcuts::Entry *e = m_shortcuts->entry(id);
    m_loading = true;
    for (QKeySequenceEdit *edit : m_edits) {
        edit->setEnabled(e != nullptr);
        edit->setKeySequence(QKeySequence());
    }
    m_default->setEnabled(e && m_shortcuts->isChanged(id));
    if (!e) {
        m_title->setText(QStringLiteral("Choose a command above, then click a key field and press the new key."));
        m_hint->clear();
        m_loading = false;
        return;
    }
    const QList<QKeySequence> keys = m_shortcuts->keys(id);
    m_edits[0]->setKeySequence(keys.value(0));
    m_edits[1]->setKeySequence(keys.value(1));
    m_loading = false;

    m_title->setText(QStringLiteral("<b>%1</b>").arg(m_shortcuts->title(id).toHtmlEscaped()));
    QString hint = e->hint;
    if (e->kind == Shortcuts::Kind::Tool) {
        // Name the tools that share a key with this one.
        QStringList sharing;
        for (const Shortcuts::Entry &other : m_shortcuts->entries()) {
            if (other.id == id || other.kind != Shortcuts::Kind::Tool) continue;
            for (const QKeySequence &k : keys)
                if (m_shortcuts->keys(other.id).contains(k) && !sharing.contains(other.label)) sharing << other.label;
        }
        hint = sharing.isEmpty()
            ? QString()
            : QStringLiteral("Shares its key with %1: press the key again for the next tool, "
                             "with Shift for the one before.").arg(sharing.join(QStringLiteral(", ")));
    } else if (e->kind == Shortcuts::Kind::Hold) {
        hint += QStringLiteral(" Works while the image has the keyboard focus (click it once).");
    }
    m_hint->setText(hint.trimmed());
}

bool KeyboardPage::askToTake(const QString &id, const QKeySequence &key, const QStringList &others) {
    if (m_testAnswer >= 0) return m_testAnswer == 1;
    QStringList names;
    for (const QString &other : others) names << QStringLiteral("    ") + m_shortcuts->title(other);
    QMessageBox box(QMessageBox::Question, QStringLiteral("Key Already Used"),
                    QStringLiteral("%1 is already used by:\n\n%2\n\nUse it for \"%3\" instead? The others lose it.")
                        .arg(key.toString(QKeySequence::NativeText), names.join(QLatin1Char('\n')),
                             m_shortcuts->entry(id) ? m_shortcuts->entry(id)->label : id),
                    QMessageBox::NoButton, this);
    QPushButton *take = box.addButton(QStringLiteral("Use It Here"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(take);
    box.exec();
    return box.clickedButton() == take;
}

void KeyboardPage::commit(int field) {
    if (m_loading) return;
    const QString id = currentId();
    if (!m_shortcuts->entry(id)) return;
    const QList<QKeySequence> old = m_shortcuts->keys(id);
    const QKeySequence k0 = Shortcuts::firstCombination(m_edits[0]->keySequence());
    const QKeySequence k1 = Shortcuts::firstCombination(m_edits[1]->keySequence());
    const QKeySequence entered = field == 0 ? k0 : k1;
    if (!entered.isEmpty() && !old.contains(entered)) {
        const QStringList others = m_shortcuts->clashes(id, entered);
        if (!others.isEmpty()) {
            if (!askToTake(id, entered, others)) {
                showSelected();   // keep the old keys
                return;
            }
            for (const QString &other : others) {
                QList<QKeySequence> theirs = m_shortcuts->keys(other);
                theirs.removeAll(entered);
                m_shortcuts->setKeys(other, theirs);
            }
        }
    }
    QList<QKeySequence> keys;
    if (!k0.isEmpty()) keys << k0;
    if (!k1.isEmpty()) keys << k1;
    keys << old.mid(2);   // a third key, if a command has one, stays
    if (keys != old) m_shortcuts->setKeys(id, keys);
    else showSelected();
}
