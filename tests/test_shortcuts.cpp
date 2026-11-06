// Keyboard shortcuts (Settings > Keyboard): the list of commands and their
// default keys, changing and saving keys, tools sharing a key, the painting
// keys (last painting tool, Eraser toggle), the Color Picker while held,
// the Settings page with its clash question, and the Help text. Headless.
//
// PWM_SCREENSHOT_DIR=<folder> saves the Keyboard page.
#include <QApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <cstdio>

#include "appinfo.h"
#include "canvas/canvaswidget.h"
#include "dialogs/keyboardpage.h"
#include "dialogs/settingsdialog.h"
#include "dialogs/shortcutshelp.h"
#include "mainwindow.h"
#include "panels/toolspanel.h"
#include "shortcuts.h"
#include "tools/tool.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; \
    std::printf("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static QAction *actionWithKey(MainWindow &w, const QKeySequence &key) {
    for (QAction *a : w.findChildren<QAction *>())
        if (a->shortcuts().contains(key)) return a;
    return nullptr;
}

static int actionsWithKey(MainWindow &w, const QKeySequence &key) {
    int n = 0;
    for (QAction *a : w.findChildren<QAction *>())
        if (a->shortcuts().contains(key)) ++n;
    return n;
}

static void trigger(Shortcuts *s, const char *id) {
    const Shortcuts::Entry *e = s->entry(QString::fromUtf8(id));
    if (e && e->action) e->action->trigger();
    else std::printf("  (no action for %s)\n", id);
}

static ToolType currentTool(MainWindow &w) {
    auto *canvas = w.findChild<CanvasWidget *>();
    return canvas && canvas->currentTool() ? canvas->currentTool()->type() : ToolType::Brush;
}

static void defaults() {
    MainWindow window;
    Shortcuts *s = window.shortcuts();
    CHECK(s != nullptr);
    if (!s) return;
    CHECK(s->key("Edit > Undo") == QKeySequence(QKeySequence::Undo));
    CHECK(s->keys("Edit > Redo").contains(QKeySequence("Ctrl+Y")));
    CHECK(s->keys("Edit > Redo").count(QKeySequence("Ctrl+Y")) == 1);   // listed once
    CHECK(s->key("View > Zoom In") == QKeySequence(QKeySequence::ZoomIn));
    CHECK(s->key("View > Rotate View > Rotate Left") == QKeySequence("4"));
    CHECK(s->key("tool.paintbrush") == QKeySequence("B"));
    CHECK(s->key("tool.color-picker") == QKeySequence("K"));
    CHECK(s->key("tool.eraser") == QKeySequence("E"));
    CHECK(s->key("hold.pan") == QKeySequence(Qt::Key_Space));
    CHECK(s->key("hold.rotate") == QKeySequence(Qt::SHIFT | Qt::Key_Space));
    CHECK(s->key("hold.colorPicker") == QKeySequence("I"));
    CHECK(s->key("painting.lastTool") == QKeySequence("N"));
    CHECK(s->key("painting.eraserToggle") == QKeySequence("Shift+E"));
    CHECK(s->key("painting.sizeUp") == QKeySequence("]"));
    CHECK(s->key("panels.tools") == QKeySequence("F5"));
    CHECK(s->key("panels.colors.reset") == QKeySequence("Ctrl+Shift+F8"));
    CHECK(s->keys("View > Actual Size").size() == 2);   // Ctrl+0 and Ctrl+1

    // Every menu command is listed (the Recent files list is not).
    int effects = 0, adjustments = 0, recent = 0;
    for (const Shortcuts::Entry &e : s->entries()) {
        if (e.category == "Effects") ++effects;
        if (e.category == "Adjustments") ++adjustments;
        if (e.id.startsWith("File > Recent")) ++recent;
        CHECK(!e.id.isEmpty() && !e.label.isEmpty() && !e.category.isEmpty());
        CHECK(!e.label.contains(QChar(0x2026)) && !e.label.contains('&'));
    }
    CHECK(effects > 25 && adjustments > 10 && recent == 0);
    for (const char *c : {"File", "Edit", "View", "Image", "Layers", "Tools", "Painting", "Canvas", "Panels"})
        CHECK(s->categories().contains(QString::fromLatin1(c)));

    // No two commands share a default key (tools take turns and may).
    for (const Shortcuts::Entry &e : s->entries())
        for (const QKeySequence &k : s->keys(e.id)) {
            const QStringList clash = s->clashes(e.id, k);
            if (!clash.isEmpty())
                std::printf("  default clash: %s and %s on %s\n", qPrintable(e.id), qPrintable(clash.join(", ")),
                            qPrintable(k.toString()));
            CHECK(clash.isEmpty());
        }
    // ... and no key is bound twice to actions of the window (Qt would
    // then run neither).
    for (const char *k : {"4", "5", "6", "N", "I", "Shift+E", "B", "S", "Shift+S", "Ctrl+B", "Ctrl+Z", "F5"})
        CHECK(actionsWithKey(window, QKeySequence(k)) <= 1);
    CHECK(actionsWithKey(window, QKeySequence("4")) == 1);
    CHECK(actionsWithKey(window, QKeySequence("N")) == 1);
}

static void changeAndSave() {
    QSettings(AppInfo::kId, AppInfo::kId).remove("keyboard/custom");
    {
        MainWindow window;
        Shortcuts *s = window.shortcuts();
        const QString id = "View > Rotate View > Rotate Left";
        int changes = 0;
        QObject::connect(s, &Shortcuts::changed, [&changes]() { ++changes; });
        s->setKeys(id, {QKeySequence("Ctrl+Alt+L"), QKeySequence("Ctrl+Alt+L")});
        CHECK(changes == 1);
        CHECK(s->keys(id) == QList<QKeySequence>{QKeySequence("Ctrl+Alt+L")});   // no duplicates
        CHECK(s->isChanged(id));
        CHECK(s->entry(id)->action->shortcut() == QKeySequence("Ctrl+Alt+L"));
        // A recorded chord keeps its first key only.
        s->setKeys("Edit > Redo", {QKeySequence("Ctrl+K, Ctrl+L")});
        CHECK(s->keys("Edit > Redo") == QList<QKeySequence>{QKeySequence("Ctrl+K")});
        // No key at all is a choice too.
        s->setKeys("tool.text", {});
        CHECK(s->keys("tool.text").isEmpty() && s->isChanged("tool.text"));
        CHECK(actionWithKey(window, QKeySequence("T")) == nullptr);
        // Saved as JSON, portable key names.
        const QString json = QSettings(AppInfo::kId, AppInfo::kId).value("keyboard/custom").toString();
        const QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
        CHECK(obj.value(id).toArray().size() == 1 && obj.value(id).toArray().at(0).toString() == "Ctrl+Alt+L");
        CHECK(obj.contains("tool.text") && obj.value("tool.text").toArray().isEmpty());
        CHECK(!obj.contains("Edit > Undo"));   // only changes are saved
        // The Help text shows the keys as they are now.
        const QString help = ShortcutsHelp::html(s);
        CHECK(help.contains("Ctrl+Alt+L") && !help.contains(QChar(0x2026)));
        // The panel buttons' tooltips follow their keys.
        s->setKeys("panels.tools", {QKeySequence("F9")});
        bool tipFollows = false;
        for (QToolButton *b : window.menuBar()->findChildren<QToolButton *>())
            if (b->toolTip().startsWith("<b>Tools</b>") && b->toolTip().contains("F9")) tipFollows = true;
        CHECK(tipFollows);
    }
    {
        // A new start reads them back.
        MainWindow window;
        Shortcuts *s = window.shortcuts();
        CHECK(s->key("View > Rotate View > Rotate Left") == QKeySequence("Ctrl+Alt+L"));
        CHECK(s->entry("View > Rotate View > Rotate Left")->action->shortcut() == QKeySequence("Ctrl+Alt+L"));
        CHECK(s->keys("tool.text").isEmpty());
        CHECK(s->key("panels.tools") == QKeySequence("F9"));
        s->reset("View > Rotate View > Rotate Left");
        CHECK(s->key("View > Rotate View > Rotate Left") == QKeySequence("4"));
        CHECK(!s->isChanged("View > Rotate View > Rotate Left"));
        s->resetAll();
        CHECK(s->key("tool.text") == QKeySequence("T") && s->key("Edit > Redo") == QKeySequence("Ctrl+Y"));
        CHECK(!QSettings(AppInfo::kId, AppInfo::kId).contains("keyboard/custom"));
        CHECK(actionWithKey(window, QKeySequence("T")) != nullptr);
    }
}

static void toolsAndPainting() {
    MainWindow window;
    Shortcuts *s = window.shortcuts();

    // The Eraser on B as well: B takes turns between Paintbrush and Eraser.
    CHECK(s->clashes("tool.eraser", QKeySequence("B")).isEmpty());
    CHECK(s->clashes("Edit > Undo", QKeySequence("B")) == QStringList{"tool.paintbrush"});
    CHECK(s->clashes("hold.pan", QKeySequence("H")) == QStringList{"tool.pan"});
    s->setKeys("tool.eraser", {QKeySequence("B")});
    QAction *b = actionWithKey(window, QKeySequence("B"));
    CHECK(b && b->text() == "Tools B");
    CHECK(actionWithKey(window, QKeySequence("Shift+B")) != nullptr);   // backwards
    CHECK(actionWithKey(window, QKeySequence("E")) == nullptr);
    CHECK(currentTool(window) == ToolType::Brush);
    if (b) b->trigger();
    CHECK(currentTool(window) == ToolType::Eraser);
    if (b) b->trigger();
    CHECK(currentTool(window) == ToolType::Brush);
    // The Tools panel says so.
    auto *panel = window.findChild<ToolsPanel *>();
    QToolButton *eraserButton = panel ? panel->toolButton(ToolType::Eraser) : nullptr;
    CHECK(eraserButton && eraserButton->toolTip().contains("(B)"));
    // With Shift+B taken by a command, there is no backwards key.
    s->setKeys("View > Pixel Grid", {QKeySequence("Shift+B")});
    CHECK(actionsWithKey(window, QKeySequence("Shift+B")) == 1);
    s->resetAll();
    CHECK(actionWithKey(window, QKeySequence("E")) != nullptr);
    CHECK(eraserButton && eraserButton->toolTip().contains("(E)"));
    // A key with Ctrl for a tool.
    s->setKeys("tool.color-picker", {QKeySequence("Ctrl+Shift+K")});
    QAction *picker = actionWithKey(window, QKeySequence("Ctrl+Shift+K"));
    CHECK(picker && picker->text() == "Color Picker");
    if (picker) picker->trigger();
    CHECK(currentTool(window) == ToolType::ColorPicker);
    s->resetAll();

    // Last used painting tool and the Eraser toggle.
    QAction *u = actionWithKey(window, QKeySequence("U"));
    if (u) u->trigger();   // Smudge
    CHECK(currentTool(window) == ToolType::Smudge);
    QAction *e = actionWithKey(window, QKeySequence("E"));
    if (e) e->trigger();
    CHECK(currentTool(window) == ToolType::Eraser);
    trigger(s, "painting.lastTool");
    CHECK(currentTool(window) == ToolType::Smudge);
    trigger(s, "painting.eraserToggle");
    CHECK(currentTool(window) == ToolType::Eraser);
    trigger(s, "painting.eraserToggle");
    CHECK(currentTool(window) == ToolType::Smudge);
    // Non-painting tools in between do not count.
    QAction *k = actionWithKey(window, QKeySequence("K"));
    if (k) k->trigger();
    QAction *f = actionWithKey(window, QKeySequence("F"));
    if (f) f->trigger();
    CHECK(currentTool(window) == ToolType::Fill);
    trigger(s, "painting.lastTool");
    CHECK(currentTool(window) == ToolType::Smudge);

    // The Color Picker while I is held, then back.
    auto *canvas = window.findChild<CanvasWidget *>();
    if (!canvas) return;
    QKeyEvent down(QEvent::KeyPress, Qt::Key_I, Qt::NoModifier);
    QApplication::sendEvent(canvas, &down);
    CHECK(currentTool(window) == ToolType::ColorPicker);
    QKeyEvent up(QEvent::KeyRelease, Qt::Key_I, Qt::NoModifier);
    QApplication::sendEvent(canvas, &up);
    CHECK(currentTool(window) == ToolType::Smudge);
    // Let go during a pick: back once the button comes up.
    QApplication::sendEvent(canvas, &down);
    const QPointF p = canvas->canvasToWidget(QPointF(10, 10));
    QMouseEvent press(QEvent::MouseButtonPress, p, canvas->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(canvas, &press);
    QApplication::sendEvent(canvas, &up);
    CHECK(currentTool(window) == ToolType::ColorPicker);
    QMouseEvent release(QEvent::MouseButtonRelease, p, canvas->mapToGlobal(p), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(canvas, &release);
    CHECK(currentTool(window) == ToolType::Smudge);
    // Another key for it in Settings > Keyboard; I is an ordinary key then.
    s->setKeys("hold.colorPicker", {QKeySequence("Ctrl+I")});
    QApplication::sendEvent(canvas, &down);
    QApplication::sendEvent(canvas, &up);
    CHECK(currentTool(window) == ToolType::Smudge);
    s->resetAll();
}

static QStringList visibleLabels(QTreeWidget *tree) {
    QStringList out;
    for (int g = 0; g < tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = tree->topLevelItem(g);
        if (group->isHidden()) continue;
        for (int i = 0; i < group->childCount(); ++i)
            if (!group->child(i)->isHidden()) out << group->text(0) + ": " + group->child(i)->text(0);
    }
    return out;
}

static void settingsPage() {
    MainWindow window;
    Shortcuts *s = window.shortcuts();
    SettingsDialog dialog(&window, s);
    auto *page = dialog.findChild<KeyboardPage *>();
    auto *tree = dialog.findChild<QTreeWidget *>("keyboardList");
    auto *filter = dialog.findChild<QLineEdit *>("keyboardFilter");
    auto *hint = dialog.findChild<QLabel *>("keyboardHint");
    auto *defaultButton = dialog.findChild<QPushButton *>("keyboardDefault");
    auto *resetAll = dialog.findChild<QPushButton *>("keyboardResetAll");
    CHECK(page && tree && filter && hint && defaultButton && resetAll);
    if (!page || !tree || !filter || !hint || !defaultButton || !resetAll) return;

    // Groups in menu order, then tools, painting, canvas and panels.
    QStringList groups;
    for (int g = 0; g < tree->topLevelItemCount(); ++g) groups << tree->topLevelItem(g)->text(0);
    CHECK(groups.indexOf("File") == 0 && groups.indexOf("Tools") > groups.indexOf("Effects")
          && groups.indexOf("Panels") == groups.size() - 1);

    const QString left = "View > Rotate View > Rotate Left";
    CHECK(page->selectCommand(left));
    CHECK(!defaultButton->isEnabled());
    // Ctrl+B belongs to Best Fit: "no" keeps everything as it was.
    page->setClashAnswerForTests(0);
    page->enterKey(0, QKeySequence("Ctrl+B"));
    CHECK(s->key(left) == QKeySequence("4") && s->key("View > Best Fit") == QKeySequence("Ctrl+B"));
    // "Use it here" moves it.
    page->setClashAnswerForTests(1);
    page->enterKey(0, QKeySequence("Ctrl+B"));
    CHECK(s->key(left) == QKeySequence("Ctrl+B"));
    CHECK(s->keys("View > Best Fit").isEmpty());
    CHECK(defaultButton->isEnabled());
    // The changed commands are bold, with their new keys.
    bool bold = false;
    for (QTreeWidgetItem *item : tree->findItems("Rotate View > Rotate Left", Qt::MatchExactly | Qt::MatchRecursive))
        bold = item->font(0).bold() && item->text(1) == "Ctrl+B";
    CHECK(bold);
    // Default Keys gives 4 back.
    defaultButton->click();
    CHECK(s->key(left) == QKeySequence("4"));
    // A second key, then the first removed.
    CHECK(page->selectCommand("Edit > Undo"));
    page->enterKey(1, QKeySequence("Alt+Backspace"));
    CHECK(s->keys("Edit > Undo") == (QList<QKeySequence>{QKeySequence::Undo, QKeySequence("Alt+Backspace")}));
    page->enterKey(0, QKeySequence());
    CHECK(s->keys("Edit > Undo") == QList<QKeySequence>{QKeySequence("Alt+Backspace")});
    // Tools may share keys without a question; the hint names the others.
    page->setClashAnswerForTests(0);   // a question would refuse
    CHECK(page->selectCommand("tool.zoom"));
    page->enterKey(0, QKeySequence("M"));
    CHECK(s->key("tool.zoom") == QKeySequence("M"));
    CHECK(hint->text().contains("Move Selected Pixels") && hint->text().contains("Move Selection"));
    CHECK(page->selectCommand("hold.colorPicker"));
    CHECK(hint->text().contains("held") || hint->text().contains("Hold"));

    // Searching by name and by key.
    filter->setText("rotate");
    QStringList shown = visibleLabels(tree);
    CHECK(shown.size() >= 4);
    for (const QString &l : shown) CHECK(l.contains("rotate", Qt::CaseInsensitive));
    filter->setText("Alt+Backspace");
    shown = visibleLabels(tree);
    CHECK(shown == QStringList{"Edit: Undo"});
    filter->clear();

    const QByteArray shots = qgetenv("PWM_SCREENSHOT_DIR");
    if (!shots.isEmpty()) {
        page->selectCommand("tool.zoom");
        if (auto *list = dialog.findChild<QListWidget *>()) list->setCurrentRow(4);
        dialog.resize(720, 640);
        QDir().mkpath(QString::fromLocal8Bit(shots));
        dialog.grab().save(QString::fromLocal8Bit(shots) + "/settings-keyboard.png");
    }

    // Reset All (the question answered for the test).
    page->setClashAnswerForTests(1);
    resetAll->click();
    CHECK(s->key("View > Best Fit") == QKeySequence("Ctrl+B") && s->key("tool.zoom") == QKeySequence("Z")
          && s->key("Edit > Undo") == QKeySequence(QKeySequence::Undo));
    CHECK(!QSettings(AppInfo::kId, AppInfo::kId).contains("keyboard/custom"));
}

int main(int argc, char **argv) {
    QTemporaryDir home;
    for (const char *name : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"}) {
        const QString path = home.path() + '/' + QString::fromLatin1(name);
        QDir().mkpath(path);
        qputenv(name, path.toUtf8());
    }
    QApplication app(argc, argv);
    defaults();
    changeAndSave();
    toolsAndPainting();
    settingsPage();
    std::printf("shortcuts: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
