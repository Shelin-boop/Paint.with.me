#pragma once

#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "tools/brushpreset.h"
#include "tools/tool.h"

class QAction;
class QFrame;
class QGridLayout;
class QMenu;
class QMenuBar;
class QToolButton;

// The Tools panel. Paint.NET's 19 tools sit in their usual two columns. When
// the panel is wide enough it grows a third and a fourth column for shortcuts
// the user pins there: any menu command (adjustments, effects, image and layer
// commands ...) and saved brush presets. A "+" button and the right-click menu
// add them; dragging a shortcut moves it. Narrow, the shortcuts continue below
// the tools; docked at the top or bottom, after them in the same row.
class ToolsPanel : public QWidget {
    Q_OBJECT
public:
    struct ToolEntry {
        QString name;
        QString shortcut;
        ToolType type;
        int row;   // Paint.NET's slot in the two-column layout
        int col;
        QString description;
    };

    // A menu command that can be pinned.
    struct Command {
        QString id;          // "Adjustments > Curves...": menu path and label
        QStringList path;    // menu titles above it: {"Effects", "Blurs"}
        QString label;       // "Curves..."
        QAction *action = nullptr;
        QString category() const { return path.value(0); }
    };

    static constexpr int kButtonHeight = 28;
    static constexpr int kMinButtonWidth = 28;
    // A shortcut column is added for every 40 px of width (at most 4 columns).
    static constexpr int kMinSlotWidth = 40;
    static constexpr int kMinColumns = 2;
    static constexpr int kMaxColumns = 4;
    static constexpr int kAutoColumns = 0;   // column mode: follow the width

    explicit ToolsPanel(const QVector<ToolEntry> &tools, QWidget *parent = nullptr);

    QToolButton *toolButton(ToolType type) const;
    QToolButton *addButton() const { return m_addButton; }
    // The pinned shortcuts that are shown, in order.
    QVector<QToolButton *> pinnedButtons() const { return m_pinButtons; }

    void setCurrentTool(ToolType type) { m_currentTool = type; }
    // Redraws the icons that depend on the light or dark color scheme.
    void refreshIcons();
    void setHorizontal(bool horizontal);
    bool isHorizontal() const { return m_horizontal; }

    // kAutoColumns (default) or a fixed 2, 3 or 4.
    void setColumnMode(int mode);
    int columnMode() const { return m_columnMode; }
    int columnCount() const { return m_columns; }
    static int autoColumnCount(int width);
    // Grid cells (x = column, y = row) for `count` shortcut buttons: the extra
    // columns first, top to bottom; then cells of the tool area that are
    // still free; then new rows below the tools.
    static QVector<QPoint> shortcutCells(int columns, int count, int toolRows,
                                         const QVector<QPoint> &freeToolCells);

    // Every command of a menu bar, except those in `skip` (dynamic lists such
    // as Recent files) and disabled placeholders.
    static QVector<Command> commandsFromMenuBar(QMenuBar *menuBar,
                                                const QList<QMenu *> &skip = {});
    void setCommands(const QVector<Command> &commands);
    const QVector<Command> &commands() const { return m_commands; }

    // Pins are "cmd:<command id>" or "preset:<preset id>", in display order.
    // Pins whose command is missing (an uninstalled plugin, say) are kept but
    // not shown.
    QStringList pins() const { return m_pins; }
    static QString commandPin(const QString &commandId) { return QStringLiteral("cmd:") + commandId; }
    static QString presetPin(const QString &presetId) { return QStringLiteral("preset:") + presetId; }
    bool isCommandPinned(const QString &commandId) const;
    void pinCommand(const QString &commandId);
    void unpin(const QString &pin);   // a preset's pin deletes the preset
    void movePin(const QString &pin, int newIndex);
    // Moves a shown pin one place earlier (-1) or later (+1) among the shown ones.
    void stepPin(const QString &pin, int direction);

    QVector<BrushPreset> presets() const { return m_presets; }
    const BrushPreset *preset(const QString &id) const;
    void addPreset(const BrushPreset &preset);      // pins it at the end
    void updatePreset(const BrushPreset &preset);   // same id, new settings
    void renamePreset(const QString &id, const QString &name);

    // Fills `menu` with every pinnable command (checked when pinned) and
    // "Save Current Brush as Preset...". Used by "+" and the right-click menu.
    void fillAddMenu(QMenu *menu);

    void loadSettings();
    void saveSettings() const;

signals:
    void toolClicked(ToolType type);
    void presetActivated(const BrushPreset &preset);
    void savePresetRequested();
    void updatePresetRequested(const QString &presetId);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QToolButton *makeButton();
    const Command *command(const QString &id) const;
    void rebuildPinnedButtons();
    void relayout();
    void updateColumnCount();
    QString pinAt(const QPoint &pos) const;
    QStringList shownPins() const;
    void pinsChanged();
    void showAddMenu();

    QVector<ToolEntry> m_tools;
    QVector<QToolButton *> m_toolButtons;
    QVector<Command> m_commands;
    QStringList m_pins;
    QVector<BrushPreset> m_presets;
    QVector<QToolButton *> m_pinButtons;
    QToolButton *m_addButton = nullptr;
    QFrame *m_separator = nullptr;
    QGridLayout *m_grid = nullptr;
    int m_toolRows = 0;
    bool m_horizontal = false;
    int m_columnMode = kAutoColumns;
    int m_columns = kMinColumns;
    ToolType m_currentTool = ToolType::Brush;
    QPoint m_pressPos;
};
