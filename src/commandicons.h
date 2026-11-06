#pragma once

#include <QIcon>
#include <QList>
#include <QStringList>

class QMenu;

// Icons for the commands in the Image, Layers, Adjustments and Effects menus,
// drawn in code (no image files). They show in the menus and on commands
// pinned to the Tools panel. Own designs: the ideas are the usual ones for an
// image editor (a picture, layer cards, arrows, a histogram), the drawings
// are not copied from any program.
namespace CommandIcons {

// The icon for a menu command by its path, menu titles and label joined with
// " > ", without "&" and "..." ("Effects > Blurs > Gaussian Blur"). Null if
// there is none.
QIcon forPath(const QString &path);

// Gives every command in these menus (and their submenus) its icon. Call it
// again after a theme change: line colors follow the theme.
void decorate(const QList<QMenu *> &menus);

// Every path that has an icon (for tests).
QStringList paths();

// "Resize..." -> "Resize", "&Flip Horizontal" -> "Flip Horizontal".
QString plainLabel(const QString &menuText);

}  // namespace CommandIcons
