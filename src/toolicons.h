#pragma once

#include <QIcon>
#include "tools/tool.h"

namespace ToolIcons {
    QIcon forTool(ToolType type);
    QIcon newDoc();
    QIcon openDoc();
    QIcon saveDoc();
    QIcon undoAction();
    QIcon redoAction();
    QIcon cutAction();
    QIcon copyAction();
    QIcon pasteAction();
    QIcon printAction();
    QIcon cropAction();
    QIcon deselectAction();
    QIcon pixelGridAction();
    QIcon rulersAction();
    // Utility-window icons shown on the right of the menu bar
    QIcon toolsWindow();
    QIcon historyWindow();
    QIcon layersWindow();
    QIcon colorsWindow();
    QIcon settings();
    QIcon help();
    // Tools panel shortcuts: a pinned menu command (initials on a tile coloured
    // by its menu), a brush preset (tool icon plus its width) and the "+" slot.
    QString commandInitials(const QString &label);
    QIcon command(const QString &label, const QString &category);
    QIcon brushPreset(ToolType type, double size);
    QIcon addSlot();
}
