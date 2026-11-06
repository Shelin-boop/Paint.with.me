#include "panels/toolspanel.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFrame>
#include <QGridLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QSettings>
#include <QToolButton>
#include <functional>

#include "appinfo.h"
#include "toolicons.h"

namespace {

const char kPinMime[] = "application/x-paint-with-me-tools-pin";
const char kPathSeparator[] = " > ";

// Menu text without mnemonics: "Save &As..." -> "Save As...".
QString plainMenuText(QString text) {
    text.replace(QLatin1String("&&"), QString(QChar(0x1F)));
    text.remove(QLatin1Char('&'));
    text.replace(QChar(0x1F), QLatin1Char('&'));
    return text.trimmed();
}

// Text for a QMenu entry (the reverse: a literal "&" must be doubled).
QString menuEntryText(const QString &plain) {
    QString t = plain;
    return t.replace(QLatin1Char('&'), QLatin1String("&&"));
}

QString withoutDots(const QString &label) {
    QString t = label;
    if (t.endsWith(QLatin1String("..."))) t.chop(3);
    return t.trimmed();
}

QString shortcutText(const QAction *action) {
    if (!action || action->shortcut().isEmpty()) return QString();
    return action->shortcut().toString(QKeySequence::NativeText);
}

QString presetTooltip(const BrushPreset &p, const QString &toolName) {
    QString tip = QStringLiteral("<b>%1</b><br>Brush preset: %2").arg(p.name.toHtmlEscaped(), toolName);
    if (p.tool != ToolType::Pencil)
        tip += QStringLiteral(", width %1 px, hardness %2 %").arg(p.size).arg(p.hardness);
    tip += QStringLiteral(", opacity %1 %").arg(p.opacity);
    tip += QStringLiteral("<br><i>Right-click to rename, update or remove; drag to move.</i>");
    return tip;
}

} // namespace

ToolsPanel::ToolsPanel(const QVector<ToolEntry> &tools, QWidget *parent)
    : QWidget(parent), m_tools(tools) {
    setObjectName(QStringLiteral("ToolsPalette"));
    setAcceptDrops(true);
    m_grid = new QGridLayout(this);
    m_grid->setSpacing(0);
    m_grid->setContentsMargins(1, 2, 1, 2);

    for (const ToolEntry &t : m_tools) {
        m_toolRows = qMax(m_toolRows, t.row + 1);
        QToolButton *btn = makeButton();
        btn->setIcon(ToolIcons::forTool(t.type));
        btn->setCheckable(true);
        btn->setToolTip(toolTipFor(t));
        btn->setProperty("toolTypeInt", static_cast<int>(t.type));
        const ToolType type = t.type;
        connect(btn, &QToolButton::clicked, this, [this, type]() { emit toolClicked(type); });
        m_toolButtons.append(btn);
    }

    m_addButton = makeButton();
    m_addButton->setObjectName(QStringLiteral("ToolsPanelAddButton"));
    m_addButton->setIcon(ToolIcons::addSlot());
    m_addButton->setAutoRaise(true);
    m_addButton->setToolTip(QStringLiteral(
        "<b>Add to Tools panel</b><br>Pin a menu command (adjustment, effect, image or layer "
        "command) or save the current brush as a preset.<br><i>Make the panel wider for a "
        "third and fourth column.</i>"));
    connect(m_addButton, &QToolButton::clicked, this, &ToolsPanel::showAddMenu);

    m_separator = new QFrame(this);
    m_separator->setFrameShape(QFrame::VLine);
    m_separator->setFrameShadow(QFrame::Sunken);
    m_separator->hide();

    loadSettings();
}

QString ToolsPanel::toolTipFor(const ToolEntry &t) {
    // Rich tooltip: bold name + shortcut, then a one-line description.
    QString tip = QStringLiteral("<b>") + t.name + QStringLiteral("</b>");
    if (!t.shortcut.isEmpty()) tip += QStringLiteral(" (") + t.shortcut.toHtmlEscaped() + QLatin1Char(')');
    if (!t.description.isEmpty()) tip += QStringLiteral("<br>") + t.description;
    return tip;
}

void ToolsPanel::setToolKeys(const QMap<ToolType, QString> &keys) {
    for (ToolEntry &t : m_tools) {
        if (!keys.contains(t.type)) continue;
        t.shortcut = keys.value(t.type);
        if (QToolButton *btn = toolButton(t.type)) btn->setToolTip(toolTipFor(t));
    }
}

QToolButton *ToolsPanel::makeButton() {
    auto *btn = new QToolButton(this);
    btn->setIconSize(QSize(20, 20));
    // Fill the column width so the columns cover the whole panel.
    btn->setFixedHeight(kButtonHeight);
    btn->setMinimumWidth(kMinButtonWidth);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return btn;
}

void ToolsPanel::refreshIcons() {
    m_addButton->setIcon(ToolIcons::addSlot());
}

QToolButton *ToolsPanel::toolButton(ToolType type) const {
    for (QToolButton *btn : m_toolButtons)
        if (btn->property("toolTypeInt").toInt() == static_cast<int>(type)) return btn;
    return nullptr;
}

// ---- Columns and layout ---------------------------------------------------

int ToolsPanel::autoColumnCount(int width) {
    return qBound(kMinColumns, (width - 2) / kMinSlotWidth, kMaxColumns);
}

QVector<QPoint> ToolsPanel::shortcutCells(int columns, int count, int toolRows,
                                          const QVector<QPoint> &freeToolCells) {
    QVector<QPoint> cells;
    if (count <= 0) return cells;
    columns = qBound(kMinColumns, columns, kMaxColumns);
    for (int c = kMinColumns; c < columns && cells.size() < count; ++c)
        for (int r = 0; r < toolRows && cells.size() < count; ++r)
            cells.append(QPoint(c, r));
    for (const QPoint &p : freeToolCells)
        if (cells.size() < count) cells.append(p);
    for (int r = toolRows; cells.size() < count; ++r)
        for (int c = 0; c < columns && cells.size() < count; ++c)
            cells.append(QPoint(c, r));
    return cells;
}

void ToolsPanel::setHorizontal(bool horizontal) {
    m_horizontal = horizontal;
    relayout();
}

void ToolsPanel::setColumnMode(int mode) {
    if (mode != kAutoColumns) mode = qBound(kMinColumns, mode, kMaxColumns);
    if (mode == m_columnMode) return;
    m_columnMode = mode;
    saveSettings();
    m_columns = -1;   // force a relayout
    updateColumnCount();
}

void ToolsPanel::updateColumnCount() {
    const int columns = m_columnMode == kAutoColumns ? autoColumnCount(width()) : m_columnMode;
    if (columns == m_columns) return;
    m_columns = columns;
    relayout();
}

void ToolsPanel::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (!m_horizontal) updateColumnCount();
}

void ToolsPanel::relayout() {
    // Until the panel has its real width (first resize), assume two columns.
    if (m_columns < kMinColumns)
        m_columns = m_columnMode == kAutoColumns ? kMinColumns : m_columnMode;
    QList<QWidget *> widgets;
    for (QToolButton *b : m_toolButtons) widgets << b;
    for (QToolButton *b : m_pinButtons) widgets << b;
    widgets << m_addButton << m_separator;
    for (QWidget *w : widgets) m_grid->removeWidget(w);
    for (int i = 0; i < 64; ++i) {
        m_grid->setRowStretch(i, 0);
        m_grid->setColumnStretch(i, 0);
    }

    QList<QWidget *> shortcuts;
    for (QToolButton *b : m_pinButtons) shortcuts << b;
    shortcuts << m_addButton;

    if (m_horizontal) {
        // One row hugging the edge: tools, a divider, then the shortcuts.
        m_grid->setContentsMargins(1, 0, 1, 0);
        int col = 0;
        for (QToolButton *b : m_toolButtons) m_grid->addWidget(b, 0, col++);
        m_separator->show();
        m_grid->addWidget(m_separator, 0, col++);
        for (QWidget *w : shortcuts) m_grid->addWidget(w, 0, col++);
        m_grid->setColumnStretch(col, 1);   // trailing column takes the slack
        setMinimumWidth(0);
        setMaximumWidth(QWIDGETSIZE_MAX);
        setMinimumHeight(kButtonHeight);
        setMaximumHeight(kButtonHeight);
        return;
    }

    m_separator->hide();
    m_grid->setContentsMargins(1, 2, 1, 2);
    QVector<QPoint> freeCells;
    for (int r = 0; r < m_toolRows; ++r) {
        for (int c = 0; c < kMinColumns; ++c) {
            bool used = false;
            for (const ToolEntry &t : m_tools) used = used || (t.row == r && t.col == c);
            if (!used) freeCells.append(QPoint(c, r));
        }
    }
    for (int i = 0; i < m_toolButtons.size(); ++i)
        m_grid->addWidget(m_toolButtons[i], m_tools[i].row, m_tools[i].col);
    const QVector<QPoint> cells = shortcutCells(m_columns, shortcuts.size(), m_toolRows, freeCells);
    int lastRow = m_toolRows - 1;
    for (int i = 0; i < shortcuts.size(); ++i) {
        m_grid->addWidget(shortcuts[i], cells[i].y(), cells[i].x());
        lastRow = qMax(lastRow, cells[i].y());
    }
    for (int c = 0; c < m_columns; ++c) m_grid->setColumnStretch(c, 1);
    m_grid->setRowStretch(lastRow + 1, 1);   // slack below the last row
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    setMaximumWidth(QWIDGETSIZE_MAX);
    // Following the width, the panel may shrink to two columns; a fixed count
    // keeps room for all of them.
    const int fixedColumns = m_columnMode == kAutoColumns ? kMinColumns : m_columns;
    setMinimumWidth(fixedColumns * kMinButtonWidth + 2);
}

// ---- Commands and pins ----------------------------------------------------

QVector<ToolsPanel::Command> ToolsPanel::commandsFromMenuBar(QMenuBar *menuBar,
                                                             const QList<QMenu *> &skip) {
    QVector<Command> out;
    if (!menuBar) return out;
    std::function<void(QMenu *, const QStringList &)> walk =
        [&](QMenu *menu, const QStringList &path) {
            for (QAction *a : menu->actions()) {
                if (a->isSeparator()) continue;
                const QString text = plainMenuText(a->text());
                if (text.isEmpty()) continue;
                if (QMenu *sub = a->menu()) {
                    if (!skip.contains(sub)) walk(sub, path + QStringList{text});
                    continue;
                }
                if (!a->isEnabled() && !a->isCheckable() && path.contains(QStringLiteral("Plugins")))
                    continue;   // "No plugin loaded"
                Command c;
                c.path = path;
                c.label = text;
                c.id = (path + QStringList{text}).join(QLatin1String(kPathSeparator));
                c.action = a;
                out.push_back(c);
            }
        };
    for (QAction *top : menuBar->actions()) {
        if (QMenu *menu = top->menu())
            if (!skip.contains(menu)) walk(menu, {plainMenuText(top->text())});
    }
    return out;
}

void ToolsPanel::setCommands(const QVector<Command> &commands) {
    m_commands = commands;
    pinsChanged();
}

const ToolsPanel::Command *ToolsPanel::command(const QString &id) const {
    for (const Command &c : m_commands)
        if (c.id == id) return &c;
    return nullptr;
}

const BrushPreset *ToolsPanel::preset(const QString &id) const {
    for (const BrushPreset &p : m_presets)
        if (p.id == id) return &p;
    return nullptr;
}

bool ToolsPanel::isCommandPinned(const QString &commandId) const {
    return m_pins.contains(commandPin(commandId));
}

void ToolsPanel::pinCommand(const QString &commandId) {
    const QString pin = commandPin(commandId);
    if (m_pins.contains(pin)) return;
    m_pins.append(pin);
    saveSettings();
    pinsChanged();
}

void ToolsPanel::unpin(const QString &pin) {
    if (!m_pins.contains(pin)) return;
    m_pins.removeAll(pin);
    if (pin.startsWith(QLatin1String("preset:"))) {
        const QString id = pin.mid(7);
        for (int i = m_presets.size() - 1; i >= 0; --i)
            if (m_presets[i].id == id) m_presets.remove(i);
    }
    saveSettings();
    pinsChanged();
}

void ToolsPanel::movePin(const QString &pin, int newIndex) {
    const int from = m_pins.indexOf(pin);
    if (from < 0) return;
    newIndex = qBound(0, newIndex, int(m_pins.size()) - 1);
    if (newIndex == from) return;
    m_pins.move(from, newIndex);
    saveSettings();
    pinsChanged();
}

void ToolsPanel::stepPin(const QString &pin, int direction) {
    const QStringList shown = shownPins();
    const int i = shown.indexOf(pin);
    const int j = i + (direction < 0 ? -1 : 1);
    if (i < 0 || j < 0 || j >= shown.size()) return;
    movePin(pin, m_pins.indexOf(shown[j]));
}

QStringList ToolsPanel::shownPins() const {
    QStringList shown;
    for (QToolButton *b : m_pinButtons) shown << b->property("pin").toString();
    return shown;
}

void ToolsPanel::addPreset(const BrushPreset &preset) {
    if (preset.id.isEmpty() || this->preset(preset.id)) return;
    m_presets.append(preset);
    m_pins.append(presetPin(preset.id));
    saveSettings();
    pinsChanged();
}

void ToolsPanel::updatePreset(const BrushPreset &preset) {
    for (BrushPreset &p : m_presets) {
        if (p.id != preset.id) continue;
        p = preset;
        saveSettings();
        pinsChanged();
        return;
    }
}

void ToolsPanel::renamePreset(const QString &id, const QString &name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return;
    for (BrushPreset &p : m_presets) {
        if (p.id != id) continue;
        p.name = trimmed;
        saveSettings();
        pinsChanged();
        return;
    }
}

void ToolsPanel::pinsChanged() {
    rebuildPinnedButtons();
    relayout();
}

void ToolsPanel::rebuildPinnedButtons() {
    // deleteLater: this can run while a pinned button is still on the call
    // stack (its own drag, for one).
    for (QToolButton *b : m_pinButtons) {
        m_grid->removeWidget(b);
        b->hide();
        b->deleteLater();
    }
    m_pinButtons.clear();

    for (const QString &pin : m_pins) {
        QToolButton *btn = nullptr;
        if (pin.startsWith(QLatin1String("cmd:"))) {
            const Command *c = command(pin.mid(4));
            if (!c || !c->action) continue;
            btn = makeButton();
            QAction *action = c->action;
            btn->setIcon(action->icon().isNull() ? ToolIcons::command(c->label, c->category())
                                                 : action->icon());
            QString tip = QStringLiteral("<b>%1</b>").arg(withoutDots(c->label).toHtmlEscaped());
            const QString keys = shortcutText(action);
            if (!keys.isEmpty()) tip += QStringLiteral(" (%1)").arg(keys);
            tip += QStringLiteral("<br>%1").arg(c->path.join(QLatin1String(kPathSeparator)).toHtmlEscaped());
            tip += QStringLiteral("<br><i>Right-click to remove; drag to move.</i>");
            btn->setToolTip(tip);
            btn->setCheckable(action->isCheckable());
            QPointer<QAction> guard(action);
            auto sync = [btn, guard]() {
                if (!guard) return;
                btn->setEnabled(guard->isEnabled());
                if (guard->isCheckable()) btn->setChecked(guard->isChecked());
            };
            sync();
            connect(action, &QAction::changed, btn, sync);
            connect(btn, &QToolButton::clicked, this, [guard, sync]() {
                if (guard) guard->trigger();
                sync();
            });
        } else if (pin.startsWith(QLatin1String("preset:"))) {
            const BrushPreset *p = preset(pin.mid(7));
            if (!p) continue;
            btn = makeButton();
            btn->setIcon(ToolIcons::brushPreset(p->tool, p->size));
            QString toolName;
            for (const ToolEntry &t : m_tools)
                if (t.type == p->tool) toolName = t.name;
            btn->setToolTip(presetTooltip(*p, toolName));
            const BrushPreset copy = *p;
            connect(btn, &QToolButton::clicked, this, [this, copy]() { emit presetActivated(copy); });
        } else {
            continue;
        }
        btn->setProperty("pin", pin);
        btn->installEventFilter(this);
        btn->show();
        m_pinButtons.append(btn);
    }
}

// ---- Menus ----------------------------------------------------------------

void ToolsPanel::fillAddMenu(QMenu *menu) {
    QMap<QString, QMenu *> submenus;
    for (const Command &c : m_commands) {
        QMenu *parent = menu;
        QString key;
        for (const QString &part : c.path) {
            key += part + QLatin1String(kPathSeparator);
            QMenu *&sub = submenus[key];
            if (!sub) sub = parent->addMenu(menuEntryText(part));
            parent = sub;
        }
        QAction *a = parent->addAction(menuEntryText(c.label));
        a->setCheckable(true);
        a->setChecked(isCommandPinned(c.id));
        const QString id = c.id;
        connect(a, &QAction::toggled, this, [this, id](bool on) {
            if (on) pinCommand(id);
            else unpin(commandPin(id));
        });
    }
    menu->addSeparator();
    QAction *save = menu->addAction(QStringLiteral("Save Current Brush as Preset..."));
    save->setEnabled(BrushPreset::supports(m_currentTool));
    if (!save->isEnabled())
        save->setToolTip(QStringLiteral("Choose the paintbrush, eraser, pencil, clone stamp or recolor tool first."));
    menu->setToolTipsVisible(true);
    connect(save, &QAction::triggered, this, &ToolsPanel::savePresetRequested);
}

void ToolsPanel::showAddMenu() {
    QMenu menu(this);
    fillAddMenu(&menu);
    menu.exec(m_addButton->mapToGlobal(QPoint(0, m_addButton->height())));
}

void ToolsPanel::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    const QString pin = pinAt(event->pos());
    if (!pin.isEmpty()) {
        const bool isPreset = pin.startsWith(QLatin1String("preset:"));
        if (isPreset) {
            const QString id = pin.mid(7);
            menu.addAction(QStringLiteral("Rename Preset..."), this, [this, id]() {
                const BrushPreset *p = preset(id);
                if (!p) return;
                bool ok = false;
                const QString name = QInputDialog::getText(this, QStringLiteral("Rename Brush Preset"),
                                                           QStringLiteral("Name:"), QLineEdit::Normal,
                                                           p->name, &ok);
                if (ok) renamePreset(id, name);
            });
            QAction *update = menu.addAction(QStringLiteral("Update Preset from Current Brush"), this,
                                             [this, id]() { emit updatePresetRequested(id); });
            update->setEnabled(BrushPreset::supports(m_currentTool));
        }
        const QStringList shown = shownPins();
        const int index = shown.indexOf(pin);
        QAction *earlier = menu.addAction(QStringLiteral("Move Earlier"), this,
                                          [this, pin]() { stepPin(pin, -1); });
        earlier->setEnabled(index > 0);
        QAction *later = menu.addAction(QStringLiteral("Move Later"), this,
                                        [this, pin]() { stepPin(pin, +1); });
        later->setEnabled(index >= 0 && index < shown.size() - 1);
        menu.addAction(isPreset ? QStringLiteral("Delete Preset") : QStringLiteral("Remove from Tools Panel"),
                       this, [this, pin]() { unpin(pin); });
        menu.addSeparator();
    }
    if (!m_horizontal) {
        QMenu *columns = menu.addMenu(QStringLiteral("Columns"));
        const QList<QPair<QString, int>> choices = {
            {QStringLiteral("Automatic (follows the panel width)"), kAutoColumns},
            {QStringLiteral("2"), 2}, {QStringLiteral("3"), 3}, {QStringLiteral("4"), 4}};
        for (const auto &choice : choices) {
            QAction *a = columns->addAction(choice.first);
            a->setCheckable(true);
            a->setChecked(m_columnMode == choice.second);
            const int mode = choice.second;
            connect(a, &QAction::triggered, this, [this, mode]() { setColumnMode(mode); });
        }
    }
    QMenu *add = menu.addMenu(QStringLiteral("Add to Tools Panel"));
    fillAddMenu(add);
    menu.exec(event->globalPos());
    event->accept();
}

// ---- Drag to move ---------------------------------------------------------

QString ToolsPanel::pinAt(const QPoint &pos) const {
    for (QToolButton *b : m_pinButtons)
        if (b->isVisible() && b->geometry().contains(pos)) return b->property("pin").toString();
    return QString();
}

bool ToolsPanel::eventFilter(QObject *watched, QEvent *event) {
    auto *btn = qobject_cast<QToolButton *>(watched);
    if (!btn || !m_pinButtons.contains(btn)) return QWidget::eventFilter(watched, event);
    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) m_pressPos = me->position().toPoint();
    } else if (event->type() == QEvent::MouseMove) {
        auto *me = static_cast<QMouseEvent *>(event);
        if ((me->buttons() & Qt::LeftButton)
            && (me->position().toPoint() - m_pressPos).manhattanLength()
                   >= QApplication::startDragDistance()) {
            auto *mime = new QMimeData;
            mime->setData(QLatin1String(kPinMime), btn->property("pin").toString().toUtf8());
            auto *drag = new QDrag(btn);
            drag->setMimeData(mime);
            drag->setPixmap(btn->icon().pixmap(QSize(20, 20), btn->devicePixelRatioF()));
            btn->setDown(false);
            drag->exec(Qt::MoveAction);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ToolsPanel::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat(QLatin1String(kPinMime))) event->acceptProposedAction();
}

void ToolsPanel::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasFormat(QLatin1String(kPinMime))) event->acceptProposedAction();
}

void ToolsPanel::dropEvent(QDropEvent *event) {
    if (!event->mimeData()->hasFormat(QLatin1String(kPinMime))) return;
    const QString pin = QString::fromUtf8(event->mimeData()->data(QLatin1String(kPinMime)));
    const QPoint pos = event->position().toPoint();
    const QString target = pinAt(pos);
    if (!target.isEmpty()) movePin(pin, m_pins.indexOf(target));
    else if (m_addButton->geometry().contains(pos)) movePin(pin, int(m_pins.size()) - 1);
    event->acceptProposedAction();
}

// ---- Settings -------------------------------------------------------------

void ToolsPanel::loadSettings() {
    QSettings s(AppInfo::kId, AppInfo::kId);
    int mode = s.value(QStringLiteral("toolsPanel/columns"), kAutoColumns).toInt();
    if (mode != kAutoColumns && (mode < kMinColumns || mode > kMaxColumns)) mode = kAutoColumns;
    m_columnMode = mode;
    m_columns = -1;

    m_presets.clear();
    const QJsonArray presets =
        QJsonDocument::fromJson(s.value(QStringLiteral("toolsPanel/presets")).toString().toUtf8()).array();
    for (const QJsonValue &v : presets) {
        BrushPreset p;
        if (BrushPreset::fromJson(v.toObject(), &p) && !preset(p.id)) m_presets.append(p);
    }

    m_pins.clear();
    for (const QString &pin : s.value(QStringLiteral("toolsPanel/pins")).toStringList()) {
        if (m_pins.contains(pin)) continue;
        if (pin.startsWith(QLatin1String("preset:")) && !preset(pin.mid(7))) continue;
        if (pin.startsWith(QLatin1String("cmd:")) || pin.startsWith(QLatin1String("preset:")))
            m_pins.append(pin);
    }
    // A preset always has a place on the panel.
    for (const BrushPreset &p : m_presets)
        if (!m_pins.contains(presetPin(p.id))) m_pins.append(presetPin(p.id));
    pinsChanged();
}

void ToolsPanel::saveSettings() const {
    QSettings s(AppInfo::kId, AppInfo::kId);
    s.setValue(QStringLiteral("toolsPanel/columns"), m_columnMode);
    s.setValue(QStringLiteral("toolsPanel/pins"), m_pins);
    QJsonArray presets;
    for (const BrushPreset &p : m_presets) presets.append(p.toJson());
    s.setValue(QStringLiteral("toolsPanel/presets"),
               QString::fromUtf8(QJsonDocument(presets).toJson(QJsonDocument::Compact)));
}
