#pragma once

#include <QString>

class Shortcuts;

// The keyboard shortcuts as a small HTML table, for Help > Keyboard Shortcuts
// and the Help page in Settings: the keys as they are now (Settings >
// Keyboard), or the defaults without `shortcuts`.
namespace ShortcutsHelp {
QString html(const Shortcuts *shortcuts = nullptr);
}
