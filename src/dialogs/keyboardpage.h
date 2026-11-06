#pragma once

// Settings > Keyboard: every command with its keys; pick one and press the
// new key. A key another command has is taken from it after asking.

#include <QWidget>

class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class Shortcuts;

class KeyboardPage : public QWidget {
    Q_OBJECT
public:
    explicit KeyboardPage(Shortcuts *shortcuts, QWidget *parent = nullptr);

    // For tests: picks a command as if clicked, and gives it keys as if
    // typed into the first and second key field (asks about clashes).
    bool selectCommand(const QString &id);
    void enterKey(int field, const QKeySequence &key);

    // Answer to "already used by ..." for tests: true = take it, false =
    // keep the old keys. Unset: the question is shown.
    void setClashAnswerForTests(int answer) { m_testAnswer = answer; }

private:
    void rebuild();
    void refreshTexts();
    void applyFilter(const QString &text);
    void showSelected();
    void commit(int field);
    QString currentId() const;
    bool askToTake(const QString &id, const QKeySequence &key, const QStringList &others);

    Shortcuts *m_shortcuts;
    QLineEdit *m_filter = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_hint = nullptr;
    QKeySequenceEdit *m_edits[2] = {nullptr, nullptr};
    QPushButton *m_default = nullptr;
    QPushButton *m_resetAll = nullptr;
    bool m_loading = false;
    int m_testAnswer = -1;
};
