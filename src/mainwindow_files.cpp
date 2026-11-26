// Main window: images open at once, opening, saving, exporting, autosave and
// crash recovery, recent files, printing, drag and drop.
// Split from mainwindow.cpp; one class, several files.
#include "mainwindow.h"
#include "dialogs/pastedialog.h"
#include "toolicons.h"
#include "core/document.h"
#include "core/thumbnails.h"
#include "canvas/canvaswidget.h"
#include "panels/layerspanel.h"
#include "panels/colorspanel.h"
#include "panels/historypanel.h"
#include "panels/imagelistbar.h"
#include "panels/toolspanel.h"
#include "core/psdformat.h"
#include "core/pdfexport.h"
#include "core/perspective.h"
#include "dialogs/pdfexportdialog.h"
#include "core/history.h"
#include "dialogs/newdocumentdialog.h"
#include "dialogs/saveconfigdialog.h"
#include "tools/tool.h"
#include "tools/clonestamptool.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QPainter>
#include <QDialog>
#include <QFileInfo>
#include <QSettings>
#include <QPrinter>
#include <QPrintDialog>
#include <QFile>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include "core/pdfpictures.h"
#include "dialogs/pdfpicturedialog.h"
#include <QClipboard>
#include <QTimer>
#include "appinfo.h"
#include "instancelink.h"
#include <QJsonDocument>
#include <QMenu>

void MainWindow::wireDocumentSignals() {
    m_canvas->setDocument(m_document);
    m_layersPanel->setDocument(m_document);
    m_colorsPanel->setDocument(m_document);
    m_historyPanel->setDocument(m_document);

    connect(m_document, &Document::documentChanged, this, &MainWindow::updateTitle);
    connect(m_document, &Document::documentChanged, this, &MainWindow::updateImageList);
    connect(m_document, &Document::primaryColorChanged, this, &MainWindow::onPrimaryColorChanged,
            Qt::UniqueConnection);
    connect(m_document, &Document::secondaryColorChanged, this, &MainWindow::onSecondaryColorChanged,
            Qt::UniqueConnection);
    connect(m_document, &Document::sizeChanged, this, [this](int w, int h) {
        m_sizeLabel->setText(QString("%1 × %2").arg(w).arg(h));
    });
    connect(&m_document->history(), &HistoryManager::historyChanged, this, [this]() {
        const bool canUndo = m_document->history().canUndo();
        const bool canRedo = m_document->history().canRedo();
        m_undoAction->setEnabled(canUndo);
        m_redoAction->setEnabled(canRedo);
        if (m_undoToolbarAction) m_undoToolbarAction->setEnabled(canUndo);
        if (m_redoToolbarAction) m_redoToolbarAction->setEnabled(canRedo);
    });

    m_canvas->resetToDefaultView();
    m_sizeLabel->setText(QString("%1 × %2").arg(m_document->width()).arg(m_document->height()));
    updateTitle();
}

// ---- Multi-document ----

// The two colors and which one is chosen belong to the window, as in
// Paint.NET: the image switched to (or a new one) takes them over.
static void takeColors(Document *to, const Document *from) {
    if (!to || !from || to == from) return;
    to->setPrimaryColor(from->primaryColor());
    to->setSecondaryColor(from->secondaryColor());
    to->setSecondaryActive(from->secondaryActive());
}

void MainWindow::addDocument(Document *doc) {
    m_documents.append(doc);
    setActiveDocument(m_documents.size() - 1);
}

void MainWindow::setActiveDocument(int index) {
    if (index < 0 || index >= m_documents.size()) return;
    // Drop the previous document's connections so stale signals don't fire.
    if (m_document) disconnect(m_document, nullptr, this, nullptr);

    m_activeDocIndex = index;
    takeColors(m_documents[index], m_document ? m_document : m_colorSource.data());
    m_document = m_documents[index];
    m_colorSource = nullptr;
    wireDocumentSignals();

    // Refresh undo/redo enablement for the newly active document.
    const bool canUndo = m_document->history().canUndo();
    const bool canRedo = m_document->history().canRedo();
    m_undoAction->setEnabled(canUndo);
    m_redoAction->setEnabled(canRedo);
    if (m_undoToolbarAction) m_undoToolbarAction->setEnabled(canUndo);
    if (m_redoToolbarAction) m_redoToolbarAction->setEnabled(canRedo);

    updateImageList();
}

void MainWindow::closeDocument(int index) {
    if (index < 0 || index >= m_documents.size()) return;
    Document *doc = m_documents[index];
    if (!maybeSaveDocument(doc)) return;

    if (m_document == doc) disconnect(m_document, nullptr, this, nullptr);
    // The image shown next takes over the colors of the one in view.
    m_colorSource = m_document ? m_document : doc;
    m_documents.removeAt(index);
    doc->deleteLater();

    if (m_documents.isEmpty()) {
        // paint.net always keeps at least one image open.
        auto *fresh = new Document(800, 600, this);
        fresh->activeLayer()->clear(Qt::white);
        m_document = nullptr;
        addDocument(fresh);
        return;
    }
    m_document = nullptr;
    setActiveDocument(qBound(0, index - 1, m_documents.size() - 1));
}

void MainWindow::closeCurrentDocument() {
    closeDocument(m_activeDocIndex);
}

void MainWindow::updateImageList() {
    if (m_imageListBar)
        m_imageListBar->setDocuments(m_documents, m_activeDocIndex);
    updateTitle();
}

bool MainWindow::maybeSaveDocument(Document *doc) {
    if (!doc || !doc->isModified()) return true;
    // Make sure the user is looking at the document they're being asked about.
    int idx = m_documents.indexOf(doc);
    if (idx >= 0 && idx != m_activeDocIndex) setActiveDocument(idx);

    auto result = QMessageBox::question(this, QStringLiteral("Save changes"),
        QStringLiteral("The document has been modified.\nDo you want to save your changes?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (result == QMessageBox::Save) return saveDocument();
    if (result == QMessageBox::Cancel) return false;
    return true;  // Discard
}

bool MainWindow::maybeSave() {
    return maybeSaveDocument(m_document);
}

bool MainWindow::maybeSaveAll() {
    // Every open image, not just the one in view: quitting used to ask only
    // about the active image and dropped unsaved changes in the others.
    const QVector<Document *> docs = m_documents;
    for (Document *doc : docs)
        if (!maybeSaveDocument(doc)) return false;
    return true;
}

void MainWindow::newDocument() {
    NewDocumentDialog dlg(this);
    dlg.setObjectName(QStringLiteral("newDocumentDialog"));
    // A picture on the clipboard: start at its size (and resolution, when it
    // was copied in Paint.with.me), like Paint.NET.
    double clipDpi = 0;
    const QSize clip = clipboardPictureSize(&clipDpi);
    if (!clip.isEmpty()) dlg.setClipboardPicture(clip, clipDpi);
    if (dlg.exec() == QDialog::Accepted) {
        auto *newDoc = new Document(dlg.imageWidth(), dlg.imageHeight(), this);
        newDoc->setDpi(dlg.resolution());
        newDoc->activeLayer()->clear(dlg.backgroundColor());
        newDoc->setModified(false);
        addDocument(newDoc);   // opens alongside the existing images
    }
}

void MainWindow::openDocument() {
    QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Open an image"),
        QString(), "All supported formats (*.psd *.psb *.pdn *.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.webp *.pdf);;"
                   "Photoshop (*.psd *.psb);;"
                   "Paint.NET (*.pdn);;"
                   "PDF, pictures in it (*.pdf);;"
                   "Images (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.webp);;All Files (*)");
    for (const QString &f : files)
        loadDocumentInto(f);
}

// Crash recovery: each window keeps its snapshot in a folder of its own,
// recovery/<process id>-<start time>, locked (QLockFile) while the window is
// open. Before 2026-11-26 all windows shared recovery/autosave.psd, so one
// window quitting deleted the snapshot of another.
QString MainWindow::recoveryRoot() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) base = QDir::homePath() + "/." + AppInfo::kId;
    return base + "/recovery";
}

QString MainWindow::autosaveDir() const {
    return recoveryRoot() + '/' + m_recoveryId;
}

bool MainWindow::prepareAutosaveDir() {
    if (m_recoveryLock && m_recoveryLock->isLocked()) return true;
    if (!QDir().mkpath(autosaveDir())) return false;
    m_recoveryLock = std::make_unique<QLockFile>(autosaveDir() + "/lock");
    m_recoveryLock->setStaleLockTime(0);
    return m_recoveryLock->tryLock(0);
}

void MainWindow::releaseAutosaveDir() {
    if (!m_recoveryLock) return;
    m_recoveryLock->unlock();
    m_recoveryLock.reset();
    QDir(autosaveDir()).removeRecursively();
}

void MainWindow::extractPictureFromPdf(const QString &givenPath) {
    QString path = givenPath;
    if (path.isEmpty()) {
        path = QFileDialog::getOpenFileName(this, QStringLiteral("Extract Picture from PDF"), QString(),
                                            QStringLiteral("PDF documents (*.pdf);;All Files (*)"));
        if (path.isEmpty()) return;
    }
    const QString name = QFileInfo(path).fileName();
    PdfPictures pdf;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool opened = pdf.open(path);
    QApplication::restoreOverrideCursor();
    if (!opened) {
        QMessageBox box(QMessageBox::Warning, QStringLiteral("Extract Picture from PDF"),
                        QStringLiteral("\u201c%1\u201d could not be read.").arg(name), QMessageBox::Ok, this);
        box.setObjectName(QStringLiteral("pdfPictureError"));
        box.setInformativeText(pdf.error());
        box.exec();
        return;
    }
    if (pdf.pictures().isEmpty()) {
        QMessageBox box(QMessageBox::Information, QStringLiteral("Extract Picture from PDF"),
                        QStringLiteral("No pictures were found in \u201c%1\u201d.").arg(name), QMessageBox::Ok, this);
        box.setObjectName(QStringLiteral("pdfPictureError"));
        box.setInformativeText(QStringLiteral(
            "Its pages may hold only text and drawings. Paint.with.me takes the pictures out of a PDF; "
            "it does not draw whole pages."));
        box.exec();
        return;
    }
    PdfPictureDialog dialog(pdf, name, this);
    dialog.setObjectName(QStringLiteral("pdfPictureDialog"));
    if (dialog.exec() != QDialog::Accepted) return;
    const QImage image = dialog.currentImage();
    if (image.isNull()) return;
    const double dpi = dialog.currentDpi();
    const QString label = QStringLiteral("%1, picture %2").arg(QFileInfo(path).completeBaseName())
                              .arg(dialog.currentIndex() + 1);
    switch (dialog.action()) {
    case PdfPictureDialog::Action::OpenAsImage: {
        auto *doc = new Document(image.width(), image.height(), this);
        doc->activeLayer()->setImage(image);
        doc->activeLayer()->setName(label);
        if (dpi > 0) doc->setDpi(dpi);
        doc->setModified(false);
        addDocument(doc);
        statusBar()->showMessage(QStringLiteral("Opened %1: %2 x %3 pixels%4.").arg(label).arg(image.width())
                                     .arg(image.height())
                                     .arg(dpi > 0 ? QStringLiteral(" at %1 dpi").arg(dpi) : QString()), 8000);
        break;
    }
    case PdfPictureDialog::Action::CopyToClipboard:
        QApplication::clipboard()->setImage(image.convertToFormat(QImage::Format_ARGB32));
        statusBar()->showMessage(QStringLiteral("Copied %1 to the clipboard (%2 x %3 pixels).").arg(label)
                                     .arg(image.width()).arg(image.height()), 8000);
        break;
    case PdfPictureDialog::Action::AddAsLayer:
        if (!m_document) return;
        m_document->addLayer(image, label);
        m_canvas->updateCanvas();
        break;
    case PdfPictureDialog::Action::None:
        break;
    }
}

void MainWindow::performAutosave() {
    if (!m_document || !m_document->isModified()) return;
    if (m_autosaveJob.isRunning()) return;   // the previous one is still writing
    if (!prepareAutosaveDir()) return;
    // A layered .psd recovery snapshot, plus settings recording the original
    // path so it can be labelled and saved there again on recovery. The layer
    // copies share their pixels with the document (nothing is copied here);
    // writing the file runs in the background, because for a big image it
    // takes seconds and used to freeze the window every minute.
    std::vector<Layer> layers;
    layers.reserve(m_document->layerCount());
    for (int i = 0; i < m_document->layerCount(); ++i)
        layers.push_back(*m_document->layerAt(i));
    const QSize size = m_document->size();
    const QString psd = autosaveDir() + "/autosave.psd";
    const QString origPath = m_document->filePath();
    Psd::WriteOptions options;
    options.dpi = m_document->dpi();
    options.activeLayer = m_document->activeLayerIndex();
    m_discardAutosave = false;
    m_autosaveDocument = m_document;
    m_autosaveJob = QtConcurrent::run([layers = std::move(layers), size, psd, options]() {
        // Larger than a .psd allows: keep the flattened picture instead.
        if (size.width() > Psd::kMaxSide || size.height() > Psd::kMaxSide) {
            const QImage flat = Document::compositeLayers(layers, size);
            return flat.save(psd + ".png", "PNG", 80);
        }
        return Psd::write(psd, layers, size, options);
    });
    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, origPath]() {
        watcher->deleteLater();
        // The image was saved while this was being written: not needed.
        if (m_discardAutosave) {
            m_discardAutosave = false;
            clearAutosave();
            return;
        }
        // Not if the snapshot was discarded meanwhile (clean exit, recovery).
        if (!watcher->result() || recoveryFile(autosaveDir()).isEmpty()) return;
        QSettings s(autosaveDir() + "/info.ini", QSettings::IniFormat);
        s.setValue("recovery/pending", true);
        s.setValue("recovery/origPath", origPath);
        s.setValue("recovery/when", QDateTime::currentDateTime().toString(Qt::ISODate));
    });
    watcher->setFuture(m_autosaveJob);
}

QString MainWindow::recoveryFile(const QString &dir) {
    // autosave.psd keeps the layers; the .png forms are flattened (an image
    // too large for .psd, or an autosave from before layered autosave).
    for (const char *name : {"/autosave.psd", "/autosave.psd.png", "/autosave.png"}) {
        const QString path = dir + QLatin1String(name);
        if (QFile::exists(path)) return path;
    }
    return QString();
}

void MainWindow::clearAutosave() {
    for (const char *name : {"/autosave.psd", "/autosave.psd.png", "/autosave.png", "/info.ini"})
        QFile::remove(autosaveDir() + QLatin1String(name));
}

namespace {
// A snapshot left behind by a window that did not close normally.
struct Snapshot {
    QString file;
    QString origPath;
    QString when;
    QString dir;                       // its folder, removed once dealt with
    std::shared_ptr<QLockFile> lock;   // held while it is dealt with
    bool legacy = false;               // the shared layout from before 2026-11-26
};
}

void MainWindow::checkForRecovery() {
    QList<Snapshot> found;
    const QString root = recoveryRoot();
    {
        // Left by a version before 2026-11-26 (one snapshot for all windows).
        QSettings s(AppInfo::kId, AppInfo::kId);
        const QString file = recoveryFile(root);
        if (s.value("recovery/pending", false).toBool() && !file.isEmpty())
            found.append({file, s.value("recovery/origPath").toString(), s.value("recovery/when").toString(),
                          root, nullptr, true});
        else if (!file.isEmpty() || s.contains("recovery/pending")) {
            for (const char *name : {"/autosave.psd", "/autosave.psd.png", "/autosave.png"})
                QFile::remove(root + QLatin1String(name));
            s.remove("recovery");
        }
    }
    const QFileInfoList dirs = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const QFileInfo &info : dirs) {
        if (info.fileName() == m_recoveryId) continue;
        // Locked: that window is still open (another Paint.with.me).
        auto lock = std::make_shared<QLockFile>(info.absoluteFilePath() + "/lock");
        lock->setStaleLockTime(0);
        if (!lock->tryLock(0)) continue;
        const QString file = recoveryFile(info.absoluteFilePath());
        QSettings s(info.absoluteFilePath() + "/info.ini", QSettings::IniFormat);
        if (file.isEmpty() || !s.value("recovery/pending", false).toBool()) {
            lock->unlock();
            QDir(info.absoluteFilePath()).removeRecursively();
            continue;
        }
        found.append({file, s.value("recovery/origPath").toString(), s.value("recovery/when").toString(),
                      info.absoluteFilePath(), lock, false});
    }

    for (const Snapshot &snapshot : found) {
        auto forget = [&snapshot]() {
            if (snapshot.legacy) {
                for (const char *name : {"/autosave.psd", "/autosave.psd.png", "/autosave.png"})
                    QFile::remove(snapshot.dir + QLatin1String(name));
                QSettings(AppInfo::kId, AppInfo::kId).remove("recovery");
                return;
            }
            snapshot.lock->unlock();
            QDir(snapshot.dir).removeRecursively();
        };
        const QString what = snapshot.origPath.isEmpty() ? QStringLiteral("an untitled image")
                                                         : QFileInfo(snapshot.origPath).fileName();
        const auto r = QMessageBox::question(this, QStringLiteral("Recovery"),
            QStringLiteral("An autosave was found (%1, %2).\nDo you want to recover it?").arg(what, snapshot.when),
            QMessageBox::Yes | QMessageBox::No);
        if (r != QMessageBox::Yes) {
            forget();
            continue;
        }
        auto *doc = new Document(1, 1, this);
        if (!doc->load(snapshot.file)) {
            delete doc;
            QMessageBox::warning(this, QStringLiteral("Recovery"), QStringLiteral("The autosave could not be opened."));
            continue;   // kept, so it can be tried again next time
        }
        // Saving goes to the original file again (or asks, for an untitled one).
        doc->setFilePath(snapshot.origPath);
        doc->setModified(true);   // force a re-save so it isn't lost again
        addDocument(doc);
        updateTitle();
        // This window's own snapshot takes over at once, then the old one goes.
        performAutosave();
        m_autosaveJob.waitForFinished();
        forget();
    }
}

void MainWindow::openFile(const QString &filePath) {
    loadDocumentInto(filePath);
    dropPristineUntitled();
}

void MainWindow::loadDocumentInto(const QString &filePath) {
    // A PDF is not drawn as a page: its pictures can be taken out.
    if (QFileInfo(filePath).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0) {
        extractPictureFromPdf(filePath);
        return;
    }
    // If it's already open, just switch to it.
    for (int i = 0; i < m_documents.size(); ++i) {
        if (m_documents[i]->filePath() == filePath) {
            setActiveDocument(i);
            return;
        }
    }
    auto *newDoc = new Document(1, 1, this);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool loaded = newDoc->load(filePath);
    QApplication::restoreOverrideCursor();
    if (loaded) {
        addDocument(newDoc);
        addRecentFile(filePath);
        const QStringList notes = newDoc->loadNotes();
        if (!notes.isEmpty()) {
            // Not blocking: the image is open and usable behind it.
            auto *box = new QMessageBox(QMessageBox::Information, QFileInfo(filePath).fileName(),
                QStringLiteral("The file is open. Some parts could not be kept as they were:"),
                QMessageBox::Ok, this);
            box->setInformativeText(QStringLiteral("\u2022 ") + notes.join(QStringLiteral("\n\u2022 ")));
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->open();
        }
    } else {
        const QString why = newDoc->lastError();
        delete newDoc;
        QMessageBox::warning(this, QStringLiteral("Error"),
            QStringLiteral("Could not open the image file.") + (why.isEmpty() ? QString() : QStringLiteral("\n\n") + why));
    }
}

void MainWindow::printDocument() {
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dlg(&printer, this);
    dlg.setWindowTitle(QStringLiteral("Print image"));
    if (dlg.exec() != QDialog::Accepted) return;

    QPainter painter(&printer);
    QImage image = m_document->flatten();
    QRect page = painter.viewport();
    QSize size = image.size();
    size.scale(page.size(), Qt::KeepAspectRatio);
    painter.setViewport(page.x(), page.y(), size.width(), size.height());
    painter.setWindow(image.rect());
    painter.drawImage(0, 0, image);
}

void MainWindow::openRecentFile() {
    auto *action = qobject_cast<QAction*>(sender());
    if (!action) return;
    if (!maybeSave()) return;
    loadDocumentInto(action->data().toString());
}

void MainWindow::addRecentFile(const QString &filePath) {
    m_recentFiles.removeAll(filePath);
    m_recentFiles.prepend(filePath);
    while (m_recentFiles.size() > 10) m_recentFiles.removeLast();
    QSettings(AppInfo::kId, AppInfo::kId).setValue("recentFiles", m_recentFiles);
    updateRecentFilesMenu();
}

void MainWindow::updateRecentFilesMenu() {
    if (!m_recentFilesMenu) return;
    m_recentFilesMenu->clear();
    if (m_recentFiles.isEmpty()) {
        m_recentFilesMenu->addAction(QStringLiteral("(none)"))->setEnabled(false);
        return;
    }
    for (const QString &path : m_recentFiles) {
        QAction *a = m_recentFilesMenu->addAction(QFileInfo(path).fileName(),
                                                  this, &MainWindow::openRecentFile);
        a->setData(path);
        a->setToolTip(path);
    }
    m_recentFilesMenu->addSeparator();
    m_recentFilesMenu->addAction(QStringLiteral("Clear list"), this, [this]() {
        m_recentFiles.clear();
        QSettings(AppInfo::kId, AppInfo::kId).setValue("recentFiles", m_recentFiles);
        updateRecentFilesMenu();
    });
}

bool MainWindow::saveDocument() {
    // .pdn and .psb are opened but not written back: ask where to save instead.
    if (m_document->filePath().isEmpty() || Document::isOpenOnlyFormat(m_document->filePath()))
        return saveDocumentAs();
    return writeDocument(m_document->filePath(), -1);
}

bool MainWindow::saveDocumentAs() {
    // Layers are kept in Photoshop's .psd, so it comes first. Coming from a
    // .pdn or .psb, suggest the same name as .psd.
    QString selectedFilter;
    QString suggested;
    const QString current = m_document->filePath();
    if (Document::isOpenOnlyFormat(current)) {
        const QFileInfo info(current);
        suggested = info.dir().filePath(info.completeBaseName() + ".psd");
    } else if (!current.isEmpty()) {
        suggested = current;
    }
    QString filePath = QFileDialog::getSaveFileName(this, QStringLiteral("Save image"),
        suggested, "Photoshop, keeps layers (*.psd);;PNG (*.png);;JPEG (*.jpg *.jpeg);;"
                   "BMP (*.bmp);;TIFF (*.tif *.tiff);;WebP (*.webp);;All Files (*)",
        &selectedFilter);
    if (filePath.isEmpty()) return false;

    // Ensure the file has an extension; infer it from the chosen filter.
    if (QFileInfo(filePath).suffix().isEmpty()) {
        QString ext = "psd";
        if (selectedFilter.startsWith("PNG")) ext = "png";
        else if (selectedFilter.startsWith("JPEG")) ext = "jpg";
        else if (selectedFilter.startsWith("BMP")) ext = "bmp";
        else if (selectedFilter.startsWith("TIFF")) ext = "tiff";
        else if (selectedFilter.startsWith("WebP")) ext = "webp";
        filePath += "." + ext;
    }
    if (Document::isOpenOnlyFormat(filePath)) {
        QMessageBox::warning(this, QStringLiteral("Save image"),
            QStringLiteral("Paint.with.me can open this format but not save it. Choose Photoshop (.psd) to keep the layers."));
        return false;
    }

    // Warn when a multi-layer document is saved to a flat image format.
    if (!Document::isLayeredFormat(filePath) && m_document->layerCount() > 1) {
        auto res = QMessageBox::question(this, QStringLiteral("Flatten layers?"),
            QStringLiteral("This format does not keep layers: the image will be flattened.\nUse Photoshop (.psd) to keep your layers.\n\nContinue?"),
            QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Save);
        if (res == QMessageBox::Cancel) return false;
    }

    // For lossy formats (JPEG, WebP), let the user tune quality with a live
    // preview and size estimate — Paint.NET's "Save Configuration" step.
    int quality = -1;
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (SaveConfigDialog::supportsQuality(suffix)) {
        SaveConfigDialog dlg(m_document->flatten(), suffix, this);
        if (dlg.exec() != QDialog::Accepted) return false;
        quality = dlg.quality();
    }
    return writeDocument(filePath, quality);
}

bool MainWindow::writeDocument(const QString &filePath, int quality) {
    if (Document::isLayeredFormat(filePath)) {
        // Photoshop has no Glow, Negation, Reflect, Xor or Overwrite mode.
        // Asked once per image, not at every Ctrl+S.
        std::vector<Layer> layers;
        for (int i = 0; i < m_document->layerCount(); ++i) layers.push_back(*m_document->layerAt(i));
        const QStringList lost = Psd::layersWithoutPsdBlendMode(layers);
        if (!lost.isEmpty() && !m_document->property("psdBlendModesConfirmed").toBool()) {
            const auto res = QMessageBox::question(this, QStringLiteral("Blend modes"),
                QStringLiteral("Photoshop files have no equivalent for these blend modes, so these layers "
                               "will be saved as Normal:\n\n%1\n\nThey stay as they are in the open image.")
                    .arg(lost.join('\n')),
                QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Save);
            if (res != QMessageBox::Save) return false;
            m_document->setProperty("psdBlendModesConfirmed", true);
        }
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool saved = m_document->save(filePath, quality);
    QApplication::restoreOverrideCursor();
    if (!saved) {
        const QString why = m_document->lastError();
        QMessageBox::warning(this, QStringLiteral("Error"),
            QStringLiteral("Could not save the file.") + (why.isEmpty() ? QString() : QStringLiteral("\n\n") + why));
        return false;
    }
    addRecentFile(filePath);
    updateTitle();
    // Saved, so the crash-recovery snapshot of this image is not needed.
    if (m_autosaveDocument == m_document) {
        if (m_autosaveJob.isRunning()) m_discardAutosave = true;
        else clearAutosave();
    }
    return true;
}

void MainWindow::exportPdf() {
    if (!m_document) return;
    // Suggest the image's name with .pdf, next to it.
    QString suggested;
    const QString current = m_document->filePath();
    if (!current.isEmpty()) {
        const QFileInfo info(current);
        suggested = info.dir().filePath(info.completeBaseName() + ".pdf");
    }
    QString filePath = QFileDialog::getSaveFileName(this, QStringLiteral("Export as PDF"), suggested,
                                                    QStringLiteral("PDF (*.pdf)"));
    if (filePath.isEmpty()) return;
    if (QFileInfo(filePath).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) != 0)
        filePath += QStringLiteral(".pdf");
    PdfExportDialog dlg(m_document->size(), m_document->dpi(), m_document->layerCount(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    exportPdfTo(filePath, dlg.options());
}

bool MainWindow::exportPdfTo(const QString &filePath, PdfExport::Options options) {
    if (options.title.isEmpty()) {
        const QString current = m_document->filePath();
        options.title = current.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(current).completeBaseName();
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool ok = PdfExport::write(filePath, PdfExport::pagesFor(*m_document, options),
                                     m_document->dpi(), options, &error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("Export as PDF"),
            QStringLiteral("Could not export the PDF.") + (error.isEmpty() ? QString() : QStringLiteral("\n\n") + error));
        return false;
    }
    statusBar()->showMessage(QStringLiteral("Exported %1").arg(QFileInfo(filePath).fileName()), 6000);
    return true;
}

// The thumbnail of an image in another Paint.with.me window: its drag data.
static QJsonObject foreignImage(const QMimeData *mime) {
    if (!mime->hasFormat(InstanceLink::imageMimeType())) return {};
    const QJsonObject from = QJsonDocument::fromJson(mime->data(InstanceLink::imageMimeType())).object();
    if (qint64(from.value("pid").toDouble()) == QCoreApplication::applicationPid()) return {};
    return from;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!foreignImage(event->mimeData()).isEmpty()) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        for (const QUrl &url : urls) {
            QString filePath = url.toLocalFile();
            QString suffix = QFileInfo(filePath).suffix().toLower();
            if (suffix == "png" || suffix == "jpg" || suffix == "jpeg" || 
                suffix == "bmp" || suffix == "gif" || suffix == "tiff" || suffix == "webp" ||
                suffix == "pdn" || suffix == "pdf") {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (!foreignImage(event->mimeData()).isEmpty()) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }
    if (event->mimeData()->hasFormat(InstanceLink::imageMimeType())) {
        event->ignore();   // this window's own image: only the strip takes it
        return;
    }
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    // An image from another Paint.with.me window moves here.
    if (const QJsonObject from = foreignImage(event->mimeData()); !from.isEmpty()) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
        // After the drop has finished, so the other window is not kept waiting.
        QTimer::singleShot(0, this, [this, from]() { receiveImage(from); });
        return;
    }
    // A dropped .pdn keeps its layers, so open it as its own image rather
    // than flattening it into a layer of the current one.
    if (event->mimeData()->hasUrls() && !event->mimeData()->urls().isEmpty()) {
        const QString filePath = event->mimeData()->urls().first().toLocalFile();
        if (QFileInfo(filePath).suffix().compare("pdn", Qt::CaseInsensitive) == 0) {
            openFile(filePath);
            event->acceptProposedAction();
            return;
        }
        if (QFileInfo(filePath).suffix().compare("pdf", Qt::CaseInsensitive) == 0) {
            event->acceptProposedAction();
            // After the drop has finished, so the window manager is not kept waiting.
            QTimer::singleShot(0, this, [this, filePath]() { extractPictureFromPdf(filePath); });
            return;
        }
    }

    if (!m_document) {
        QMessageBox::warning(this, QStringLiteral("Error"), QStringLiteral("Please create a document before adding a layer."));
        return;
    }

    const QMimeData *mimeData = event->mimeData();
    if (mimeData->hasUrls()) {
        const QList<QUrl> urls = mimeData->urls();
        if (!urls.isEmpty()) {
            QString filePath = urls.first().toLocalFile();
            QString suffix = QFileInfo(filePath).suffix().toLower();
            if (suffix == "png" || suffix == "jpg" || suffix == "jpeg" || 
                suffix == "bmp" || suffix == "gif" || suffix == "tiff" || suffix == "webp") {
                
                QImage image(filePath);
                if (!image.isNull()) {
                    QString layerName = QFileInfo(filePath).baseName();
                    
                    // Calculer la nouvelle taille requise (jamais réduire)
                    int newWidth = qMax(m_document->width(), image.width());
                    int newHeight = qMax(m_document->height(), image.height());
                    
                    // Si le document doit être agrandi, redimensionner le canvas
                    if (newWidth > m_document->width() || newHeight > m_document->height()) {
                        m_document->resizeCanvas(newWidth, newHeight, 0, 0);
                    }
                    
                    // Ensuite redimensionner l'image si nécessaire
                    QImage layerImage = image;
                    if (image.width() < newWidth || image.height() < newHeight) {
                        QImage resizedImage(newWidth, newHeight, QImage::Format_ARGB32_Premultiplied);
                        resizedImage.fill(Qt::transparent);
                        QPainter p(&resizedImage);
                        p.drawImage(0, 0, image);
                        layerImage = resizedImage;
                    }
                    
                    // Add the image as a new layer
                    m_document->addLayer(layerImage, layerName);
                    m_canvas->update();
                    m_layersPanel->updateLayerList();
                    m_sizeLabel->setText(QString("%1 × %2").arg(m_document->width()).arg(m_document->height()));
                    updateTitle();
                    event->acceptProposedAction();
                } else {
                    QMessageBox::warning(this, QStringLiteral("Error"), QStringLiteral("Could not load the image file."));
                }
            }
        }
    }
}
