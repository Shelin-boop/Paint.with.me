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
               "<tr><td colspan='2'><i>Typing in a text frame</i></td></tr>"
               "<tr><td><b>Enter &nbsp; Shift+Enter</b></td><td>New paragraph, new line</td></tr>"
               "<tr><td><b>Ctrl+B &nbsp; Ctrl+I &nbsp; Ctrl+U</b></td><td>Bold, italic, underline</td></tr>"
               "<tr><td><b>Ctrl+A &nbsp; Ctrl+C &nbsp; Ctrl+X &nbsp; Ctrl+V</b></td>"
               "<td>Select all, copy, cut, paste text</td></tr>"
               "<tr><td><b>Ctrl+Z &nbsp; Ctrl+Y</b></td><td>Undo, redo typing</td></tr>"
               "<tr><td><b>Esc &nbsp; Ctrl+Enter</b></td><td>Finish the frame</td></tr>"
               "</table>"

               "<h3>Text frames</h3>"
               "<p>With the Text tool (T), drag to open a frame: the text wraps inside it. A click "
               "without dragging starts a frame that grows with the text. Each frame is a layer of "
               "its own, marked with a T in the Layers panel. Click its text to edit it again; drag "
               "its border to move it, its handles to resize it. While typing, the tool bar's font, "
               "size, style and alignment and the primary color change the selected text.</p>"
               "<p>Font sizes are points at the image's resolution (1 pt = 1/72 inch), so 12 pt "
               "prints 12 pt high. Painting tools and effects leave text layers alone until "
               "Layers &gt; Convert Text to Pixels. A .psd keeps the text editable for "
               "Paint.with.me; other programs show the same layer as pixels.</p>"

               "<h3>Print size and resolution</h3>"
               "<p>New, Resize and Canvas Size take sizes in pixels, millimeters, centimeters or "
               "inches, and a resolution in pixels per inch (dpi) or per centimeter. The resolution "
               "keeps whichever size you typed last, pixels or print size, so \u201c10 x 15 cm at "
               "300 dpi\u201d comes out the same in any order. The choice is shown below the "
               "resolution and can be changed.</p>"

               "<h3>Pictures from a PDF</h3>"
               "<p>File &gt; Extract Picture from PDF (or opening or dropping a PDF) shows the "
               "pictures inside it in the order the pages show them, the first one chosen. Open it "
               "as an image (with the resolution it has on the page), copy it to the clipboard, or "
               "add it as a layer. Pages are not drawn; pictures stored as JPEG 2000, JBIG2 or "
               "CCITT fax, and protected (encrypted) PDFs cannot be read.</p>")
        .arg(cycling, single);
}

}  // namespace ShortcutsHelp
