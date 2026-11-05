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
#include "appinfo.h"
#include <QMenu>

void MainWindow::wireDocumentSignals() {
    m_canvas->setDocument(m_document);
    m_layersPanel->setDocument(m_document);
    m_colorsPanel->setDocument(m_document);
    m_historyPanel->setDocument(m_document);

    connect(m_document, &Document::documentChanged, this, &MainWindow::updateTitle);
    connect(m_document, &Document::documentChanged, this, &MainWindow::updateImageList);
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

void MainWindow::addDocument(Document *doc) {
    m_documents.append(doc);
    setActiveDocument(m_documents.size() - 1);
}

void MainWindow::setActiveDocument(int index) {
    if (index < 0 || index >= m_documents.size()) return;
    // Drop the previous document's connections so stale signals don't fire.
    if (m_document) disconnect(m_document, nullptr, this, nullptr);

    m_activeDocIndex = index;
    m_document = m_documents[index];
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

void MainWindow::newDocument() {
    NewDocumentDialog dlg(this);
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
        QString(), "All supported formats (*.psd *.psb *.pdn *.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.webp);;"
                   "Photoshop (*.psd *.psb);;"
                   "Paint.NET (*.pdn);;"
                   "Images (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.webp);;All Files (*)");
    for (const QString &f : files)
        loadDocumentInto(f);
}

QString MainWindow::autosaveDir() const {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) base = QDir::homePath() + "/." + AppInfo::kId;
    return base + "/recovery";
}

void MainWindow::performAutosave() {
    if (!m_document || !m_document->isModified()) return;
    if (m_autosaveJob.isRunning()) return;   // the previous one is still writing
    QDir().mkpath(autosaveDir());
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
        if (!watcher->result() || recoveryFile().isEmpty()) return;
        QSettings s(AppInfo::kId, AppInfo::kId);
        s.setValue("recovery/pending", true);
        s.setValue("recovery/origPath", origPath);
        s.setValue("recovery/when", QDateTime::currentDateTime().toString(Qt::ISODate));
    });
    watcher->setFuture(m_autosaveJob);
}

QString MainWindow::recoveryFile() const {
    // autosave.psd keeps the layers; the .png forms are flattened (an image
    // too large for .psd, or an autosave from before layered autosave).
    for (const char *name : {"/autosave.psd", "/autosave.psd.png", "/autosave.png"}) {
        const QString path = autosaveDir() + QLatin1String(name);
        if (QFile::exists(path)) return path;
    }
    return QString();
}

void MainWindow::clearAutosave() {
    for (const char *name : {"/autosave.psd", "/autosave.psd.png", "/autosave.png"})
        QFile::remove(autosaveDir() + QLatin1String(name));
    QSettings s(AppInfo::kId, AppInfo::kId);
    s.remove("recovery/pending");
    s.remove("recovery/origPath");
    s.remove("recovery/when");
}

void MainWindow::checkForRecovery() {
    QSettings s(AppInfo::kId, AppInfo::kId);
    if (!s.value("recovery/pending", false).toBool()) return;
    const QString file = recoveryFile();
    if (file.isEmpty()) { clearAutosave(); return; }

    const QString orig = s.value("recovery/origPath").toString();
    const QString when = s.value("recovery/when").toString();
    const QString what = orig.isEmpty() ? QStringLiteral("an untitled document") : QFileInfo(orig).fileName();

    auto r = QMessageBox::question(this, QStringLiteral("Recovery"),
        QStringLiteral("An autosave was found (%1, %2).\nDo you want to recover it?").arg(what, when),
        QMessageBox::Yes | QMessageBox::No);
    if (r != QMessageBox::Yes) {
        clearAutosave();
        return;
    }
    auto *doc = new Document(1, 1, this);
    if (!doc->load(file)) {
        delete doc;
        QMessageBox::warning(this, QStringLiteral("Recovery"), QStringLiteral("The autosave could not be opened."));
        return;
    }
    // Saving goes to the original file again (or asks, for an untitled one).
    doc->setFilePath(orig);
    doc->setModified(true);   // force a re-save so it isn't lost again
    addDocument(doc);
    m_autosaveDocument = doc;   // saving it makes the snapshot unnecessary
    updateTitle();
}

void MainWindow::openFile(const QString &filePath) {
    loadDocumentInto(filePath);
    // If the only other image is a pristine untitled one, drop it — paint.net
    // doesn't leave an empty canvas hanging around when you open a file.
    if (m_documents.size() == 2) {
        Document *first = m_documents[0];
        if (first->filePath().isEmpty() && !first->isModified() && first != m_document) {
            m_documents.removeAt(0);
            first->deleteLater();
            m_activeDocIndex = 0;
            updateImageList();
        }
    }
}

void MainWindow::loadDocumentInto(const QString &filePath) {
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

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        for (const QUrl &url : urls) {
            QString filePath = url.toLocalFile();
            QString suffix = QFileInfo(filePath).suffix().toLower();
            if (suffix == "png" || suffix == "jpg" || suffix == "jpeg" || 
                suffix == "bmp" || suffix == "gif" || suffix == "tiff" || suffix == "webp" ||
                suffix == "pdn") {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    // A dropped .pdn keeps its layers, so open it as its own image rather
    // than flattening it into a layer of the current one.
    if (event->mimeData()->hasUrls() && !event->mimeData()->urls().isEmpty()) {
        const QString filePath = event->mimeData()->urls().first().toLocalFile();
        if (QFileInfo(filePath).suffix().compare("pdn", Qt::CaseInsensitive) == 0) {
            openFile(filePath);
            event->acceptProposedAction();
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
