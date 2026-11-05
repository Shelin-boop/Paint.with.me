// Tools panel: default panel layout, extra columns, pinned menu commands and
// brush presets. Headless (QT_QPA_PLATFORM=offscreen).
//
// Set PWM_SCREENSHOT_DIR to a folder to also save pictures of the main window
// (default layout, 4 columns with shortcuts, narrow panel) for a visual check.
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QGridLayout>
#include <QMenuBar>
#include <QJsonObject>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <cstdio>

#include "appinfo.h"
#include "canvas/canvaswidget.h"
#include "core/document.h"
#include "mainwindow.h"
#include "panels/toolspanel.h"
#include "toolicons.h"
#include "tools/brushpreset.h"
#include "tools/brushtool.h"
#include "tools/toolregistry.h"
#include <QSet>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; \
    std::printf("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static void settle() {
    for (int i = 0; i < 5; ++i) QApplication::processEvents();
}

static void screenshot(QWidget &w, const char *name) {
    const QByteArray dir = qgetenv("PWM_SCREENSHOT_DIR");
    if (dir.isEmpty()) return;
    QDir().mkpath(QString::fromLocal8Bit(dir));
    w.grab().save(QString::fromLocal8Bit(dir) + '/' + name + ".png");
}

static void clearPanelSettings() {
    QSettings s(AppInfo::kId, AppInfo::kId);
    s.remove("toolsPanel");
    s.remove("ui/mainWindowStateV8");
    s.remove("ui/mainWindowGeometry");
}

static QVector<ToolsPanel::ToolEntry> paintNetTools() {
    return {
        {"Rectangle Select", "S", ToolType::RectSelection, 0, 0, {}},
        {"Lasso Select", "S", ToolType::LassoSelection, 1, 0, {}},
        {"Ellipse Select", "S", ToolType::EllipseSelection, 2, 0, {}},
        {"Magic Wand", "S", ToolType::MagicWand, 3, 0, {}},
        {"Move Selected Pixels", "M", ToolType::Move, 0, 1, {}},
        {"Move Selection", "M", ToolType::MoveSelection, 1, 1, {}},
        {"Zoom", "Z", ToolType::Zoom, 2, 1, {}},
        {"Pan", "H", ToolType::Pan, 3, 1, {}},
        {"Paint Bucket", "F", ToolType::Fill, 4, 0, {}},
        {"Gradient", "G", ToolType::Gradient, 4, 1, {}},
        {"Paintbrush", "B", ToolType::Brush, 5, 0, {}},
        {"Eraser", "E", ToolType::Eraser, 5, 1, {}},
        {"Pencil", "P", ToolType::Pencil, 6, 0, {}},
        {"Color Picker", "K", ToolType::ColorPicker, 6, 1, {}},
        {"Clone Stamp", "L", ToolType::CloneStamp, 7, 0, {}},
        {"Recolor", "R", ToolType::Recolor, 7, 1, {}},
        {"Text", "T", ToolType::Text, 8, 0, {}},
        {"Line / Curve", "O", ToolType::Line, 8, 1, {}},
        {"Shapes", "O", ToolType::Shape, 9, 0, {}},
    };
}

// Grid column of a widget inside the panel's layout.
static int columnOf(QWidget *w) {
    auto *grid = qobject_cast<QGridLayout *>(w->parentWidget()->layout());
    if (!grid) return -1;
    const int i = grid->indexOf(w);
    if (i < 0) return -1;
    int row, col, rs, cs;
    grid->getItemPosition(i, &row, &col, &rs, &cs);
    return col;
}
static int rowOf(QWidget *w) {
    auto *grid = qobject_cast<QGridLayout *>(w->parentWidget()->layout());
    if (!grid) return -1;
    const int i = grid->indexOf(w);
    if (i < 0) return -1;
    int row, col, rs, cs;
    grid->getItemPosition(i, &row, &col, &rs, &cs);
    return row;
}

static void toolKeys() {
    std::printf("== Tool type keys ==\n");
    QStringList seen;
    for (int i = 0; i <= static_cast<int>(ToolType::Perspective); ++i) {
        const auto type = static_cast<ToolType>(i);
        const QString key = toolTypeKey(type);
        CHECK(!key.isEmpty());
        CHECK(!seen.contains(key));
        seen << key;
        ToolType back = ToolType::Brush;
        CHECK(toolTypeFromKey(key, &back) && back == type);
    }
    CHECK(!toolTypeFromKey("smudge-with-a-typo", nullptr));

    // The registry lists every tool once, in a slot of its own, and makes it.
    std::printf("== Tool registry ==\n");
    QSet<int> types;
    QSet<QPair<int, int>> cells;
    for (const ToolInfo &info : toolInfos()) {
        CHECK(!types.contains(int(info.type)));
        types.insert(int(info.type));
        CHECK(!cells.contains({info.row, info.col}));
        cells.insert({info.row, info.col});
        CHECK(qstrlen(info.name) > 0 && qstrlen(info.shortcut) == 1 && qstrlen(info.hint) > 0);
        auto tool = createTool(info.type);
        CHECK(tool && tool->type() == info.type);
        CHECK(&toolInfo(info.type) == &info);
    }
    CHECK(types.size() == int(ToolType::Perspective) + 1);
}

static void presets() {
    std::printf("== Brush presets ==\n");
    BrushTool brush;
    brush.setBrushSize(1400);
    brush.setHardness(12);
    brush.setSpacing(15);
    brush.setOpacity(80);
    brush.setBlendMode(3);
    brush.setAntialiased(false);
    const BrushPreset p = BrushPreset::capture(brush, "Big soft");
    CHECK(!p.id.isEmpty() && p.tool == ToolType::Brush && p.size == 1400 && p.hardness == 12);
    CHECK(BrushPreset::defaultName(brush) == "Paintbrush 1400 px");

    BrushPreset back;
    CHECK(BrushPreset::fromJson(p.toJson(), &back));
    CHECK(back.id == p.id && back.name == "Big soft" && back.sameSettings(p));

    BrushTool other;
    back.applyTo(other);
    CHECK(other.brushSize() == 1400 && other.hardness() == 12 && other.opacity() == 80
          && other.blendMode() == 3 && !other.antialiased());

    QJsonObject bad = p.toJson();
    bad["tool"] = "no-such-tool";
    CHECK(!BrushPreset::fromJson(bad, nullptr));
    QJsonObject wild = p.toJson();
    wild["size"] = 99999;
    wild["blendMode"] = 999;
    wild["hardness"] = -5;
    CHECK(BrushPreset::fromJson(wild, &back));
    CHECK(back.size == 2000 && back.hardness == 0 && back.blendMode >= 0 && back.blendMode < 20);

    CHECK(BrushPreset::supports(ToolType::Brush) && BrushPreset::supports(ToolType::Eraser));
    CHECK(!BrushPreset::supports(ToolType::Zoom) && !BrushPreset::supports(ToolType::Text));

    CHECK(ToolIcons::commandInitials("Brightness / Contrast...") == "BC");
    CHECK(ToolIcons::commandInitials("Move Layer Down") == "MD");
    CHECK(ToolIcons::commandInitials("Levels...") == "Le");
    CHECK(!ToolIcons::brushPreset(ToolType::Brush, 1400).isNull());
}

static void cellOrder() {
    std::printf("== Shortcut cells ==\n");
    const QVector<QPoint> free = {QPoint(1, 9)};
    CHECK(ToolsPanel::autoColumnCount(60) == 2);
    CHECK(ToolsPanel::autoColumnCount(121) == 2);
    CHECK(ToolsPanel::autoColumnCount(122) == 3);
    CHECK(ToolsPanel::autoColumnCount(161) == 3);
    CHECK(ToolsPanel::autoColumnCount(162) == 4);
    CHECK(ToolsPanel::autoColumnCount(2000) == 4);

    // Two columns: next to Shapes, then rows below the tools.
    QVector<QPoint> c = ToolsPanel::shortcutCells(2, 3, 10, free);
    CHECK(c == (QVector<QPoint>{QPoint(1, 9), QPoint(0, 10), QPoint(1, 10)}));
    // Four columns: the third column top to bottom, then the fourth.
    c = ToolsPanel::shortcutCells(4, 12, 10, free);
    CHECK(c.size() == 12 && c[0] == QPoint(2, 0) && c[9] == QPoint(2, 9)
          && c[10] == QPoint(3, 0) && c[11] == QPoint(3, 1));
    // More than fit: the free tool cell, then new rows across all columns.
    c = ToolsPanel::shortcutCells(3, 13, 10, free);
    CHECK(c[10] == QPoint(1, 9) && c[11] == QPoint(0, 10) && c[12] == QPoint(1, 10));
    CHECK(ToolsPanel::shortcutCells(4, 0, 10, free).isEmpty());
}

static void standalonePanel() {
    std::printf("== Panel columns ==\n");
    clearPanelSettings();
    QMenuBar bar;
    QMenu *adj = bar.addMenu("&Adjustments");
    int curves = 0;
    QAction *curvesAction = adj->addAction("&Curves...");
    QObject::connect(curvesAction, &QAction::triggered, [&curves]() { ++curves; });
    QMenu *fx = bar.addMenu("Effe&cts");
    QMenu *blurs = fx->addMenu("Blurs");
    blurs->addAction("Gaussian Blur...");
    QMenu *plugins = fx->addMenu("Plugins");
    plugins->addAction("No plugin loaded")->setEnabled(false);
    QMenu *recent = bar.addMenu("Recent");
    recent->addAction("/tmp/a.png");

    const auto commands = ToolsPanel::commandsFromMenuBar(&bar, {recent});
    CHECK(commands.size() == 2);
    CHECK(commands.value(0).id == "Adjustments > Curves..." && commands.value(0).category() == "Adjustments");
    CHECK(commands.value(1).id == "Effects > Blurs > Gaussian Blur...");

    ToolsPanel panel(paintNetTools());
    panel.setCommands(commands);
    panel.resize(204, 400);
    panel.show();
    settle();
    CHECK(panel.columnCount() == 4);
    CHECK(panel.pinnedButtons().isEmpty());
    CHECK(columnOf(panel.addButton()) == 2 && rowOf(panel.addButton()) == 0);

    panel.pinCommand("Adjustments > Curves...");
    panel.pinCommand("Effects > Blurs > Gaussian Blur...");
    panel.pinCommand("Effects > Plugins > Gone");   // kept, not shown
    settle();
    CHECK(panel.pinnedButtons().size() == 2);
    QToolButton *first = panel.pinnedButtons().value(0);
    CHECK(first && columnOf(first) == 2 && rowOf(first) == 0);
    CHECK(first && first->geometry().left() >= panel.toolButton(ToolType::Move)->geometry().right());
    if (first) first->click();
    CHECK(curves == 1);

    // Order: move the blur first, then back.
    panel.stepPin(ToolsPanel::commandPin("Effects > Blurs > Gaussian Blur..."), -1);
    CHECK(panel.pins().value(0) == ToolsPanel::commandPin("Effects > Blurs > Gaussian Blur..."));
    panel.stepPin(ToolsPanel::commandPin("Effects > Blurs > Gaussian Blur..."), +1);
    CHECK(panel.pins().value(0) == ToolsPanel::commandPin("Adjustments > Curves..."));

    // The "+" menu lists the commands, checked when pinned.
    QMenu menu;
    panel.fillAddMenu(&menu);
    bool sawCurvesChecked = false;
    for (QAction *a : menu.actions())
        if (a->menu())
            for (QAction *b : a->menu()->actions())
                if (b->text() == "Curves..." && b->isChecked()) sawCurvesChecked = true;
    CHECK(sawCurvesChecked);

    // Narrow: two columns, shortcuts beside Shapes and below the tools.
    panel.resize(100, 400);
    settle();
    CHECK(panel.columnCount() == 2);
    first = panel.pinnedButtons().value(0);
    CHECK(first && columnOf(first) == 1 && rowOf(first) == 9);
    CHECK(rowOf(panel.addButton()) == 10);

    // A fixed column count wins over the width.
    panel.setColumnMode(3);
    settle();
    CHECK(panel.columnCount() == 3 && panel.minimumWidth() >= 3 * ToolsPanel::kMinButtonWidth);
    panel.setColumnMode(ToolsPanel::kAutoColumns);
    settle();
    CHECK(panel.columnCount() == 2);

    // Docked at the top: one row, shortcuts after the tools.
    panel.setHorizontal(true);
    settle();
    CHECK(rowOf(panel.pinnedButtons().value(0)) == 0 && columnOf(panel.pinnedButtons().value(0)) > 19);
    CHECK(panel.maximumHeight() == ToolsPanel::kButtonHeight);
    panel.setHorizontal(false);

    // Saved: a new panel shows the same pins, including the hidden one.
    ToolsPanel again(paintNetTools());
    again.setCommands(commands);
    CHECK(again.pins() == panel.pins() && again.pins().size() == 3);
    CHECK(again.pinnedButtons().size() == 2);
    panel.unpin(ToolsPanel::commandPin("Adjustments > Curves..."));
    CHECK(panel.pinnedButtons().size() == 1 && !panel.isCommandPinned("Adjustments > Curves..."));
    clearPanelSettings();
}

static QDockWidget *dock(MainWindow &w, const char *name) {
    return w.findChild<QDockWidget *>(name);
}
static QRect inWindow(QWidget *w, QWidget *top) {
    return QRect(w->mapTo(top, QPoint()), w->size());
}

static void mainWindowLayout() {
    std::printf("== Default panel layout ==\n");
    clearPanelSettings();
    MainWindow window;
    window.resize(1944, 1040);
    window.show();
    settle();
    QDockWidget *tools = dock(window, "ToolsDock"), *colors = dock(window, "ColorsDock");
    QDockWidget *history = dock(window, "HistoryDock"), *layers = dock(window, "LayersDock");
    CHECK(tools && colors && history && layers);
    if (!tools || !colors || !history || !layers) return;
    CHECK(window.dockWidgetArea(tools) == Qt::LeftDockWidgetArea);
    CHECK(window.dockWidgetArea(colors) == Qt::LeftDockWidgetArea);
    CHECK(window.dockWidgetArea(history) == Qt::RightDockWidgetArea);
    CHECK(window.dockWidgetArea(layers) == Qt::RightDockWidgetArea);
    CHECK(inWindow(tools, &window).bottom() < inWindow(colors, &window).top());
    CHECK(inWindow(history, &window).bottom() < inWindow(layers, &window).top());
    // The left column is wide enough for four columns, like the owner's layout.
    auto *panel = window.findChild<ToolsPanel *>();
    CHECK(panel && panel->columnCount() == 4);
    CHECK(tools->width() >= 190 && tools->width() <= 260);
    screenshot(window, "1-default-layout");

    // Key S cycles the selection tools in Paint.NET's order, Shift+S back.
    auto *canvasForKeys = window.findChild<CanvasWidget *>();
    auto trigger = [&window](const QString &text) {
        for (QAction *a : window.findChildren<QAction *>())
            if (a->text() == text) { a->trigger(); return true; }
        return false;
    };
    CHECK(trigger("Tools S"));
    CHECK(canvasForKeys->currentTool()->type() == ToolType::RectSelection);
    trigger("Tools S");
    CHECK(canvasForKeys->currentTool()->type() == ToolType::LassoSelection);
    trigger("Tools S reverse");
    trigger("Tools S reverse");
    CHECK(canvasForKeys->currentTool()->type() == ToolType::MagicWand);
    QAction *keyV = nullptr;
    for (QAction *a : window.findChildren<QAction *>())
        if (a->shortcut() == QKeySequence("V")) keyV = a;
    CHECK(keyV != nullptr);
    if (keyV) keyV->trigger();
    CHECK(canvasForKeys->currentTool()->type() == ToolType::Perspective);

    // Pin commands from the real menus and a brush preset.
    CHECK(panel != nullptr);
    if (!panel) return;
    bool hasCurves = false, hasBlur = false, hasRecent = false;
    for (const auto &c : panel->commands()) {
        hasCurves |= c.id == "Adjustments > Curves...";
        hasBlur |= c.id == "Effects > Blurs > Gaussian Blur...";
        hasRecent |= c.id.startsWith("File > Recent files");
    }
    CHECK(hasCurves && hasBlur && !hasRecent);

    auto *canvas = window.findChild<CanvasWidget *>();
    Document *doc = canvas->document();
    const int layersBefore = doc->layerCount();
    panel->pinCommand("Layers > Add New Layer");
    panel->pinCommand("Adjustments > Hue / Saturation...");
    panel->pinCommand("Effects > Blurs > Gaussian Blur...");
    panel->pinCommand("Edit > Undo");
    settle();
    CHECK(panel->pinnedButtons().size() == 4);
    QToolButton *undoButton = panel->pinnedButtons().value(3);
    CHECK(undoButton && !undoButton->isEnabled());   // nothing to undo yet
    panel->pinnedButtons().value(0)->click();
    CHECK(doc->layerCount() == layersBefore + 1);
    settle();
    CHECK(undoButton && undoButton->isEnabled());    // follows the menu command

    // Brush preset: 1400 px, 12 % hardness; switch away, click it, back.
    panel->toolButton(ToolType::Brush)->click();
    Tool *brush = canvas->currentTool();
    CHECK(brush && brush->type() == ToolType::Brush);
    brush->setBrushSize(1400);
    brush->setHardness(12);
    panel->addPreset(BrushPreset::capture(*brush, "Cover glow"));
    brush->setBrushSize(10);
    brush->setHardness(100);
    panel->toolButton(ToolType::Zoom)->click();
    settle();
    QToolButton *presetButton = panel->pinnedButtons().value(4);
    CHECK(presetButton != nullptr);
    if (presetButton) presetButton->click();
    Tool *now = canvas->currentTool();
    CHECK(now && now->type() == ToolType::Brush && now->brushSize() == 1400 && now->hardness() == 12);
    screenshot(window, "2-four-columns-with-shortcuts");

    // "Update preset" keeps the name and id, takes the current settings.
    const QString presetId = panel->presets().value(0).id;
    now->setBrushSize(300);
    emit panel->updatePresetRequested(presetId);
    CHECK(panel->preset(presetId) && panel->preset(presetId)->size == 300
          && panel->preset(presetId)->name == "Cover glow");

    // Narrow left column: the panel falls back to two columns.
    window.resizeDocks({colors}, {120}, Qt::Horizontal);
    colors->hide();
    window.resizeDocks({tools}, {100}, Qt::Horizontal);
    settle();
    CHECK(panel->columnCount() == 2);
    screenshot(window, "3-narrow-two-columns");
    colors->show();

    // Reset Colors puts it back below Tools.
    colors->setFloating(true);
    settle();
    for (QAction *a : window.findChildren<QAction *>())
        if (a->text() == QStringLiteral("Reset Colors")) a->trigger();
    settle();
    CHECK(!colors->isFloating() && window.dockWidgetArea(colors) == Qt::LeftDockWidgetArea);
    CHECK(inWindow(tools, &window).bottom() < inWindow(colors, &window).top());
    clearPanelSettings();
}

int main(int argc, char **argv) {
    QTemporaryDir settings;
    for (const char *name : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"}) {
        const QString path = settings.path() + '/' + QString::fromLatin1(name);
        QDir().mkpath(path);
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv(name, path.toUtf8());
    }
    QApplication app(argc, argv);
    toolKeys();
    presets();
    cellOrder();
    standalonePanel();
    mainWindowLayout();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
