#include "dialogs/shortcutshelp.h"

#include <QMap>
#include <QStringList>

#include "tools/toolregistry.h"

namespace ShortcutsHelp {

QString html() {
    // Tools sharing a key cycle in registry order; the others have a key each.
    QMap<QString, QStringList> byKey;
    QStringList keyOrder;
    for (const ToolInfo &info : toolInfos()) {
        const QString key = QString::fromLatin1(info.shortcut);
        if (!byKey.contains(key)) keyOrder << key;
        byKey[key] << QString::fromLatin1(info.name);
    }
    QString cycling, single;
    for (const QString &key : keyOrder) {
        const QStringList &names = byKey[key];
        if (names.size() > 1)
            cycling += QStringLiteral("<tr><td><b>%1</b></td><td>%2 (press again for the next)</td></tr>")
                           .arg(key, names.join(QStringLiteral(", ")));
        else
            single += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>").arg(key, names.first());
    }

    return QStringLiteral(
               "<h3>Keyboard shortcuts</h3>"
               "<table cellspacing='4'>"
               "<tr><td colspan='2'><i>Tools</i></td></tr>%1%2"
               "<tr><td colspan='2'><i>Drawing</i></td></tr>"
               "<tr><td><b>[ &nbsp; ]</b></td><td>Brush width smaller, bigger "
               "(with Ctrl: in bigger steps)</td></tr>"
               "<tr><td><b>X</b></td><td>Swap primary and secondary color</td></tr>"
               "<tr><td><b>C</b></td><td>Switch which color the palette edits</td></tr>"
               "<tr><td colspan='2'><i>Panels</i></td></tr>"
               "<tr><td><b>F5 &nbsp; F6 &nbsp; F7 &nbsp; F8</b></td>"
               "<td>Show or hide Tools, History, Layers, Colors</td></tr>"
               "<tr><td><b>Ctrl+Shift+F5 ... F8</b></td><td>Put that panel back in its place</td></tr>"
               "<tr><td colspan='2'><i>Editing</i></td></tr>"
               "<tr><td><b>Ctrl+Z &nbsp; Ctrl+Y</b></td><td>Undo, redo</td></tr>"
               "<tr><td><b>Ctrl+F</b></td><td>Repeat the last effect</td></tr>"
               "<tr><td><b>Alt+PgUp &nbsp; Alt+PgDown</b></td><td>Layer above, below</td></tr>"
               "</table>")
        .arg(cycling, single);
}

}  // namespace ShortcutsHelp
