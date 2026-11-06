#include "dialogs/shortcutshelp.h"

#include <QKeySequence>
#include <QMap>
#include <QStringList>

#include "shortcuts.h"
#include "tools/toolregistry.h"

namespace ShortcutsHelp {

QString html(const Shortcuts *shortcuts) {
    // The keys of a command now, or `fallback` (the default) without the list.
    auto keys = [shortcuts](const char *id, const char *fallback) {
        if (!shortcuts) return QString::fromUtf8(fallback);
        const QString text = Shortcuts::text(shortcuts->keys(QString::fromUtf8(id)));
        return text.isEmpty() ? QStringLiteral("(none)") : text.toHtmlEscaped();
    };
    auto row = [](const QString &k, const QString &what) {
        return QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>").arg(k, what);
    };
    auto both = [](const QString &a, const QString &b) { return a + QStringLiteral(" &nbsp; ") + b; };

    // Tools sharing a key cycle in registry order; the others have a key each.
    QMap<QString, QStringList> byKey;
    QStringList keyOrder;
    for (const ToolInfo &info : toolInfos()) {
        QStringList toolKeys;
        if (shortcuts) {
            for (const QKeySequence &k : shortcuts->keys(QStringLiteral("tool.") + QString::fromLatin1(info.key)))
                toolKeys << k.toString(QKeySequence::NativeText);
        } else if (info.shortcut && *info.shortcut) {
            toolKeys << QString::fromLatin1(info.shortcut);
        }
        for (const QString &key : toolKeys) {
            if (!byKey.contains(key)) keyOrder << key;
            byKey[key] << QString::fromLatin1(info.name);
        }
    }
    QString cycling, single;
    for (const QString &key : keyOrder) {
        const QStringList &names = byKey[key];
        if (names.size() > 1)
            cycling += row(key.toHtmlEscaped(), names.join(QStringLiteral(", ")) + QStringLiteral(" (press again for the next)"));
        else
            single += row(key.toHtmlEscaped(), names.first());
    }

    QString table = QStringLiteral("<tr><td colspan='2'><i>Tools</i></td></tr>") + cycling + single;
    table += QStringLiteral("<tr><td colspan='2'><i>Painting</i></td></tr>");
    table += row(both(keys("painting.sizeDown", "["), keys("painting.sizeUp", "]")),
                 QStringLiteral("Brush width smaller, bigger (%1, %2: in bigger steps)")
                     .arg(keys("painting.sizeDownFast", "Ctrl+["), keys("painting.sizeUpFast", "Ctrl+]")));
    table += row(keys("painting.lastTool", "N"), QStringLiteral("Back to the painting tool used last"));
    table += row(keys("painting.eraserToggle", "Shift+E"), QStringLiteral("Eraser; again: back to the painting tool"));
    table += row(keys("colors.swap", "X"), QStringLiteral("Swap primary and secondary color"));
    table += row(keys("colors.toggle", "C"), QStringLiteral("Switch which color the palette edits"));
    table += QStringLiteral("<tr><td colspan='2'><i>View</i></td></tr>");
    table += row(both(keys("View > Zoom In", "Ctrl++"), keys("View > Zoom Out", "Ctrl+-")),
                 QStringLiteral("Zoom in, out (or the mouse wheel)"));
    table += row(keys("View > Best Fit", "Ctrl+B"), QStringLiteral("Whole image in the window"));
    table += row(both(keys("View > Rotate View > Rotate Left", "4"), keys("View > Rotate View > Rotate Right", "6")),
                 QStringLiteral("Rotate the view left, right by the step (View &gt; Rotate View)"));
    table += row(keys("View > Rotate View > Reset Rotation", "5"), QStringLiteral("Rotation back to 0\u00B0"));
    table += row(keys("hold.pan", "Space") + QStringLiteral(" + drag"), QStringLiteral("Move the view (or the middle mouse button)"));
    table += row(keys("hold.rotate", "Shift+Space") + QStringLiteral(" + drag"),
                 QStringLiteral("Turn the view freely around its middle"));
    table += row(keys("hold.colorPicker", "I") + QStringLiteral(" held"),
                 QStringLiteral("Color Picker until the key is let go"));
    table += QStringLiteral("<tr><td colspan='2'><i>Panels</i></td></tr>");
    table += row(QStringList{keys("panels.tools", "F5"), keys("panels.history", "F6"), keys("panels.layers", "F7"),
                             keys("panels.colors", "F8")}.join(QStringLiteral(" &nbsp; ")),
                 QStringLiteral("Show or hide Tools, History, Layers, Colors"));
    table += row(keys("panels.tools.reset", "Ctrl+Shift+F5") + QStringLiteral(" ..."),
                 QStringLiteral("Put that panel back in its place"));
    table += QStringLiteral("<tr><td colspan='2'><i>Editing</i></td></tr>");
    table += row(both(keys("Edit > Undo", "Ctrl+Z"), keys("Edit > Redo", "Ctrl+Y")), QStringLiteral("Undo, redo"));
    table += row(keys("Effects > Repeat last effect", "Ctrl+F"), QStringLiteral("Repeat the last effect"));
    table += row(both(keys("layers.above", "Alt+PgUp"), keys("layers.below", "Alt+PgDown")),
                 QStringLiteral("Layer above, below"));

    return QStringLiteral(
               "<h3>Keyboard shortcuts</h3>"
               "<p>Every key below can be changed in Settings &gt; Keyboard, where all menu commands "
               "are listed too. The keys while typing in a text frame stay as they are.</p>"
               "<table cellspacing='4'>%1"
               "<tr><td colspan='2'><i>Typing in a text frame</i></td></tr>"
               "<tr><td><b>Enter &nbsp; Shift+Enter</b></td><td>New paragraph, new line</td></tr>"
               "<tr><td><b>Ctrl+B &nbsp; Ctrl+I &nbsp; Ctrl+U</b></td><td>Bold, italic, underline</td></tr>"
               "<tr><td><b>Ctrl+A &nbsp; Ctrl+C &nbsp; Ctrl+X &nbsp; Ctrl+V</b></td>"
               "<td>Select all, copy, cut, paste text</td></tr>"
               "<tr><td><b>Ctrl+Z &nbsp; Ctrl+Y</b></td><td>Undo, redo typing</td></tr>"
               "<tr><td><b>Esc &nbsp; Ctrl+Enter</b></td><td>Finish the frame</td></tr>"
               "</table>"

               "<h3>Rotating the view</h3>"
               "<p>View &gt; Rotate View turns the view like a sheet of paper on a desk: the image "
               "itself does not change, and saving or printing it is not affected. Choose how far "
               "one step turns it in the same menu or in Settings &gt; Canvas. While the view is "
               "turned, the status bar shows the angle; click it to turn back. The rulers show no "
               "scale while the view is turned.</p>"

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
        .arg(table);
}

}  // namespace ShortcutsHelp
