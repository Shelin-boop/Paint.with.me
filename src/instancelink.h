#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class QLocalServer;
class QLocalSocket;

// Talking to the other running Paint.with.me windows (each its own process).
//
// Every running instance listens on a socket of its own in a private folder
// ($XDG_RUNTIME_DIR/paint-with-me, or PWM_INSTANCE_DIR for tests), named
// after its process id. The window used last writes its id to "active".
//
// - Starting Paint.with.me again (from the menu, a file manager, the command
//   line) hands the files to the window used last, which opens them as new
//   images and comes to the front; the new process ends at once.
//   Settings > User Interface can turn this off; "--new-window" skips it.
// - An image can be moved to another window: dragging its thumbnail there,
//   or "Move to New Window". The receiving window asks the giving one to
//   write the image as a layered .psd ("take"), opens it, and then tells the
//   giving one to close it ("taken").
//
// Requests and answers are one line of JSON each.
class InstanceLink : public QObject {
    Q_OBJECT
public:
    // Drag data of a thumbnail: JSON {"pid", "socket", "doc", "title"}.
    static QString imageMimeType() { return QStringLiteral("application/x-paint-with-me-image"); }

    static QString folder();
    static QString socketPathFor(qint64 pid);

    // Sends `request` and waits at most `timeoutMs` for the answer. An empty
    // object when there is none (`error` says why). Works without a
    // QApplication.
    static QJsonObject call(const QString &socketPath, const QJsonObject &request, int timeoutMs,
                            QString *error = nullptr);

    // Before any window exists: gives `files` to the window used last (any
    // running one if that is gone). True when one took them; then this
    // process can end. Passes the desktop's activation token along, so the
    // window may come to the front.
    static bool handOver(const QStringList &files);
    // "Open files in the window used last" (Settings > User Interface,
    // setting `ui/singleWindow`, on by default).
    static bool handOverEnabled();
    static void setHandOverEnabled(bool on);
    // Requests older than this are not answered (the sender has given up).
    static constexpr int kOpenTimeoutMs = 4000;

    explicit InstanceLink(QObject *parent = nullptr);   // starts listening
    ~InstanceLink() override;
    bool isListening() const;
    QString socketPath() const { return m_path; }
    // This window was used last: files opened from outside come here.
    void markActive();
    // Stops listening (before the app starts itself again).
    void close();

    static void reply(QLocalSocket *socket, const QJsonObject &answer);

signals:
    // Answer with reply(); the socket may be gone by then (it is checked).
    void requestReceived(const QJsonObject &request, QLocalSocket *socket);

private:
    void onNewConnection();

    QLocalServer *m_server = nullptr;
    QString m_path;
};
