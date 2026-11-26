// Main window: other Paint.with.me windows (each its own process). Files
// opened from outside come to the window used last; images move between
// windows by dragging their thumbnails or with "Move to New Window".
// See InstanceLink for the messages.
#include "mainwindow.h"
#include "instancelink.h"
#include "displayserver.h"
#include "canvas/canvaswidget.h"
#include "core/document.h"
#include "core/layer.h"
#include "core/psdformat.h"
#include "core/thumbnails.h"
#include "panels/imagelistbar.h"
#include "tools/texttool.h"

#include <QApplication>
#include <QDateTime>
#include <QDrag>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QStatusBar>
#include <QTimer>
#include <QWindow>

namespace {
// Each image gets a number when it is first offered to another window.
quint64 g_nextImageId = 1;
}

void MainWindow::setInstanceLink(InstanceLink *link) {
    m_instanceLink = link;
    if (!link) return;
    connect(link, &InstanceLink::requestReceived, this, &MainWindow::handleInstanceRequest);
    if (m_imageListBar) m_imageListBar->setMoveEnabled(link->isListening());
    link->markActive();   // the newest window takes files until another is used
}

quint64 MainWindow::imageId(Document *doc) {
    if (!doc) return 0;
    quint64 id = doc->property("pwmImageId").toULongLong();
    if (id == 0) {
        id = g_nextImageId++;
        doc->setProperty("pwmImageId", id);
    }
    return id;
}

Document *MainWindow::documentWithId(quint64 id) const {
    for (Document *doc : m_documents)
        if (doc->property("pwmImageId").toULongLong() == id && id != 0) return doc;
    return nullptr;
}

void MainWindow::bringToFront(const QString &activationToken) {
    // On Wayland only a token from the desktop lets a window come to the
    // front; Qt uses XDG_ACTIVATION_TOKEN when the window asks to be activated.
    if (!activationToken.isEmpty() && QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        qputenv("XDG_ACTIVATION_TOKEN", activationToken.toUtf8());
    if (isMinimized()) setWindowState(windowState() & ~Qt::WindowMinimized);
    show();
    raise();
    activateWindow();
}

void MainWindow::handleInstanceRequest(const QJsonObject &request, QLocalSocket *socket) {
    const QPointer<QLocalSocket> guard(socket);
    const QString cmd = request.value("cmd").toString();
    if (cmd == QLatin1String("open")) {
        // The sender waits only so long, then opens its own window: too late
        // means it has done so, and the files must not open twice.
        const qint64 age = QDateTime::currentMSecsSinceEpoch() - qint64(request.value("sent").toDouble());
        if (age > InstanceLink::kOpenTimeoutMs - 1000) {
            InstanceLink::reply(guard, {{"ok", false}, {"error", "late"}});
            return;
        }
        InstanceLink::reply(guard, {{"ok", true}});
        QStringList files;
        for (const QJsonValue &v : request.value("files").toArray()) files << v.toString();
        const QString token = request.value("token").toString();
        // After answering: opening a big file takes a moment.
        QTimer::singleShot(0, this, [this, files, token]() {
            bringToFront(token);
            for (const QString &file : files)
                if (QFileInfo(file).isAbsolute() && QFileInfo::exists(file)) openFile(file);
        });
        return;
    }
    if (cmd == QLatin1String("list")) {
        QJsonArray images;
        for (Document *doc : m_documents)
            images.append(QJsonObject{{"doc", double(imageId(doc))},
                                      {"path", doc->filePath()},
                                      {"width", doc->width()},
                                      {"height", doc->height()},
                                      {"layers", doc->layerCount()},
                                      {"modified", doc->isModified()}});
        InstanceLink::reply(guard, {{"ok", true}, {"images", images}});
        return;
    }
    if (cmd == QLatin1String("take")) {
        Document *doc = documentWithId(quint64(request.value("doc").toDouble()));
        if (!doc) {
            InstanceLink::reply(guard, {{"ok", false}, {"error", "The image is no longer open there."}});
            return;
        }
        if (doc->width() > Psd::kMaxSide || doc->height() > Psd::kMaxSide) {
            InstanceLink::reply(guard, {{"ok", false},
                                        {"error", "The image is too large to be moved (more than 30000 pixels a side)."}});
            return;
        }
        // An open text frame is finished first, so its text goes along.
        if (TextTool *text = textTool(); text && text->isEditing() && text->editedDocument() == doc)
            text->deactivate(*m_canvas);
        // A layered .psd keeps the layers, their names, blend modes,
        // visibility, the resolution and editable text.
        std::vector<Layer> layers;
        for (int i = 0; i < doc->layerCount(); ++i) layers.push_back(*doc->layerAt(i));
        Psd::WriteOptions options;
        options.dpi = doc->dpi();
        options.activeLayer = doc->activeLayerIndex();
        const QString file = InstanceLink::folder()
                             + QStringLiteral("/move-%1-%2.psd").arg(QCoreApplication::applicationPid())
                                   .arg(imageId(doc));
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool written = Psd::write(file, layers, doc->size(), options);
        QApplication::restoreOverrideCursor();
        if (!written) {
            QFile::remove(file);
            InstanceLink::reply(guard, {{"ok", false}, {"error", "The image could not be written for moving."}});
            return;
        }
        InstanceLink::reply(guard, {{"ok", true},
                                    {"file", file},
                                    {"path", doc->filePath()},
                                    {"modified", doc->isModified()}});
        return;
    }
    if (cmd == QLatin1String("taken")) {
        Document *doc = documentWithId(quint64(request.value("doc").toDouble()));
        InstanceLink::reply(guard, {{"ok", doc != nullptr}});
        if (!doc) return;
        const QString name = doc->filePath().isEmpty() ? QStringLiteral("Untitled") : QFileInfo(doc->filePath()).fileName();
        // Its changes went along to the other window: nothing to ask here.
        QTimer::singleShot(0, this, [this, doc = QPointer<Document>(doc), name]() {
            if (!doc) return;
            removeDocumentQuietly(doc);
            statusBar()->showMessage(QStringLiteral("\u201c%1\u201d moved to another window.").arg(name), 8000);
        });
        return;
    }
    InstanceLink::reply(guard, {{"ok", false}, {"error", "unknown request"}});
}

void MainWindow::removeDocumentQuietly(Document *doc) {
    const int index = m_documents.indexOf(doc);
    if (index < 0) return;
    if (m_autosaveDocument == doc) {
        // Its snapshot belongs to the other window now.
        if (m_autosaveJob.isRunning()) m_discardAutosave = true;
        else clearAutosave();
    }
    const bool inView = m_document == doc;
    m_documents.removeAt(index);
    doc->deleteLater();
    if (!inView) {   // another image stays in view
        m_activeDocIndex = m_documents.indexOf(m_document);
        updateImageList();
        return;
    }
    disconnect(m_document, nullptr, this, nullptr);
    m_colorSource = doc;
    m_document = nullptr;
    if (m_documents.isEmpty()) {
        // paint.net always keeps at least one image open.
        auto *fresh = new Document(800, 600, this);
        fresh->activeLayer()->clear(Qt::white);
        fresh->setModified(false);
        addDocument(fresh);
        return;
    }
    setActiveDocument(qBound(0, index - 1, int(m_documents.size()) - 1));
}

QMimeData *MainWindow::imageDragData(int index) {
    if (!m_instanceLink || index < 0 || index >= m_documents.size()) return nullptr;
    Document *doc = m_documents[index];
    const QJsonObject payload{
        {"pid", double(QCoreApplication::applicationPid())},
        {"socket", m_instanceLink->socketPath()},
        {"doc", double(imageId(doc))},
        {"title", doc->filePath().isEmpty() ? QStringLiteral("Untitled") : QFileInfo(doc->filePath()).fileName()},
    };
    auto *mime = new QMimeData;
    mime->setData(InstanceLink::imageMimeType(), QJsonDocument(payload).toJson(QJsonDocument::Compact));
    return mime;
}

void MainWindow::startImageDrag(int index, const QPixmap &picture) {
    QMimeData *mime = imageDragData(index);
    if (!mime) return;
    auto *drag = new QDrag(m_imageListBar ? static_cast<QObject *>(m_imageListBar) : this);
    drag->setMimeData(mime);
    if (!picture.isNull()) {
        drag->setPixmap(picture);
        drag->setHotSpot(QPoint(picture.width(), picture.height()) / (2 * picture.devicePixelRatio()));
    }
    // The other window asks for the image and then tells this one to close
    // it ("taken"); nothing happens here when the drag ends.
    drag->exec(Qt::MoveAction);
}

bool MainWindow::receiveImage(const QJsonObject &from) {
    const QString socket = from.value("socket").toString();
    const double id = from.value("doc").toDouble();
    if (socket.isEmpty() || socket == (m_instanceLink ? m_instanceLink->socketPath() : QString())) return false;
    QString title = from.value("title").toString();
    statusBar()->showMessage(title.isEmpty() ? QStringLiteral("Moving an image here...")
                                             : QStringLiteral("Moving \u201c%1\u201d here...").arg(title));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    // Writing a big image there takes a few seconds.
    const QJsonObject answer = InstanceLink::call(socket, {{"cmd", "take"}, {"doc", id}}, 120000, &error);
    bool done = false;
    if (answer.value("ok").toBool()) {
        const QString file = answer.value("file").toString();
        auto *doc = new Document(1, 1, this);
        // Only a file in the shared private folder, written for this move.
        if (QFileInfo(file).absolutePath() == QFileInfo(InstanceLink::folder()).absoluteFilePath()
            && file.endsWith(QLatin1String(".psd")) && doc->load(file)) {
            doc->setFilePath(answer.value("path").toString());
            title = doc->filePath().isEmpty() ? QStringLiteral("Untitled") : QFileInfo(doc->filePath()).fileName();
            doc->setModified(answer.value("modified").toBool());
            addDocument(doc);
            dropPristineUntitled();
            done = true;
        } else {
            error = doc->lastError().isEmpty() ? QStringLiteral("It could not be opened here.") : doc->lastError();
            delete doc;
        }
        QFile::remove(file);
    }
    QApplication::restoreOverrideCursor();
    if (done) {
        // Now the other window closes its copy.
        InstanceLink::call(socket, {{"cmd", "taken"}, {"doc", id}}, 10000);
        statusBar()->showMessage(QStringLiteral("Moved \u201c%1\u201d here. Its undo history stayed behind.").arg(title),
                                 8000);
        bringToFront(QString());
    } else {
        statusBar()->clearMessage();
        QMessageBox box(QMessageBox::Warning, QStringLiteral("Move Image"),
                        title.isEmpty() ? QStringLiteral("The image could not be moved here.")
                                        : QStringLiteral("\u201c%1\u201d could not be moved here.").arg(title),
                        QMessageBox::Ok, this);
        box.setObjectName(QStringLiteral("moveImageError"));
        box.setInformativeText(error.isEmpty() ? answer.value("error").toString() : error);
        box.exec();
    }
    return done;
}

void MainWindow::moveImageToNewWindow(int index) {
    if (!m_instanceLink || index < 0 || index >= m_documents.size()) return;
    Document *doc = m_documents[index];
    // The new window takes the image like a drop would, then this one closes it.
    const QStringList arguments{QStringLiteral("--new-window"), QStringLiteral("--take"),
                                m_instanceLink->socketPath(), QString::number(imageId(doc))};
    if (DisplayServer::relaunch(arguments))
        statusBar()->showMessage(QStringLiteral("Opening a new window..."), 5000);
    else
        QMessageBox::warning(this, QStringLiteral("Move to New Window"),
                             QStringLiteral("A new Paint.with.me window could not be started."));
}

void MainWindow::reorderImage(int from, int to) {
    if (from < 0 || from >= m_documents.size() || to < 0 || to >= m_documents.size() || from == to) return;
    m_documents.move(from, to);
    m_activeDocIndex = m_documents.indexOf(m_document);
    updateImageList();
}

void MainWindow::dropPristineUntitled() {
    // If the only other image is a pristine untitled one, drop it — paint.net
    // doesn't leave an empty canvas hanging around when you open a file.
    if (m_documents.size() != 2) return;
    Document *first = m_documents[0];
    if (first->filePath().isEmpty() && !first->isModified() && first != m_document
        && first->history().canUndo() == false) {
        m_documents.removeAt(0);
        first->deleteLater();
        m_activeDocIndex = m_documents.indexOf(m_document);
        updateImageList();
    }
}
