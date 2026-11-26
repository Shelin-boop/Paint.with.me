#include "instancelink.h"

#include "appinfo.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QSaveFile>
#include <QSettings>

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

constexpr const char *kSettingKey = "ui/singleWindow";

// Connects to a Unix socket; -1 on failure with the reason in `err`.
int connectTo(const QString &socketPath, int *err) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const QByteArray path = QFile::encodeName(socketPath);
    if (path.isEmpty() || path.size() >= int(sizeof(addr.sun_path))) {
        *err = ENAMETOOLONG;
        return -1;
    }
    std::memcpy(addr.sun_path, path.constData(), size_t(path.size()));
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        *err = errno;
        return -1;
    }
    int rc;
    do {
        rc = ::connect(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
    } while (rc != 0 && errno == EINTR);
    if (rc != 0) {
        *err = errno;
        ::close(fd);
        return -1;
    }
    return fd;
}

qint64 ownPid() { return qint64(::getpid()); }

}  // namespace

QString InstanceLink::folder() {
    QString dir = qEnvironmentVariable("PWM_INSTANCE_DIR");
    if (dir.isEmpty()) {
        const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
        if (!runtime.isEmpty() && QFileInfo(runtime).isDir())
            dir = runtime + QStringLiteral("/paint-with-me");
        else
            dir = QDir::tempPath() + QStringLiteral("/paint-with-me-%1").arg(::getuid());
    }
    // Private to the user: only their own programs may talk to the sockets.
    const QByteArray native = QFile::encodeName(dir);
    if (::mkdir(native.constData(), 0700) != 0 && errno != EEXIST) return dir;
    return dir;
}

QString InstanceLink::socketPathFor(qint64 pid) {
    return folder() + QStringLiteral("/%1.sock").arg(pid);
}

QJsonObject InstanceLink::call(const QString &socketPath, const QJsonObject &request, int timeoutMs,
                               QString *error) {
    auto fail = [error](const QString &why) {
        if (error) *error = why;
        return QJsonObject();
    };
    int err = 0;
    const int fd = connectTo(socketPath, &err);
    if (fd < 0) return fail(QString::fromLocal8Bit(std::strerror(err)));
    struct Closer {
        int fd;
        ~Closer() { ::close(fd); }
    } closer{fd};

    QElapsedTimer clock;
    clock.start();
    const QByteArray data = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    qsizetype sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.constData() + sent, size_t(data.size() - sent), MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return fail(QString::fromLocal8Bit(std::strerror(errno)));
        }
        sent += n;
    }
    QByteArray answer;
    while (!answer.contains('\n')) {
        const qint64 left = timeoutMs - clock.elapsed();
        if (left <= 0) return fail(QStringLiteral("The other window did not answer in time."));
        pollfd p{fd, POLLIN, 0};
        const int ready = ::poll(&p, 1, int(left));
        if (ready < 0) {
            if (errno == EINTR) continue;
            return fail(QString::fromLocal8Bit(std::strerror(errno)));
        }
        if (ready == 0) continue;   // the deadline check above ends it
        char buffer[4096];
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n < 0) {
            if (errno == EINTR) continue;
            return fail(QString::fromLocal8Bit(std::strerror(errno)));
        }
        if (n == 0) break;   // closed
        answer.append(buffer, n);
    }
    const qsizetype end = answer.indexOf('\n');
    if (end < 0) return fail(QStringLiteral("The other window closed the connection."));
    const QJsonDocument json = QJsonDocument::fromJson(answer.left(end));
    if (!json.isObject()) return fail(QStringLiteral("The other window gave an answer that could not be read."));
    if (error) error->clear();
    return json.object();
}

bool InstanceLink::handOverEnabled() {
    // Readable before the QApplication exists: the names are given here.
    return QSettings(AppInfo::kId, AppInfo::kId).value(kSettingKey, true).toBool();
}

void InstanceLink::setHandOverEnabled(bool on) {
    QSettings(AppInfo::kId, AppInfo::kId).setValue(kSettingKey, on);
}

bool InstanceLink::handOver(const QStringList &files) {
    const QString dir = folder();
    // The window used last first, then the others, newest first.
    QStringList candidates;
    QFile active(dir + QStringLiteral("/active"));
    if (active.open(QIODevice::ReadOnly)) {
        bool ok = false;
        const qint64 pid = active.readAll().trimmed().toLongLong(&ok);
        if (ok && pid != ownPid()) candidates << socketPathFor(pid);
    }
    const QFileInfoList sockets = QDir(dir).entryInfoList({QStringLiteral("*.sock")},
                                                          QDir::System | QDir::Files | QDir::Hidden, QDir::Time);
    for (const QFileInfo &info : sockets) {
        const QString path = info.absoluteFilePath();
        if (path != socketPathFor(ownPid()) && !candidates.contains(path)) candidates << path;
    }

    QJsonArray list;
    for (const QString &file : files) list.append(QFileInfo(file).absoluteFilePath());
    const QJsonObject request{
        {"cmd", "open"},
        {"files", list},
        {"token", qEnvironmentVariable("XDG_ACTIVATION_TOKEN")},
        {"startupId", qEnvironmentVariable("DESKTOP_STARTUP_ID")},
        {"sent", QDateTime::currentMSecsSinceEpoch()},
    };
    for (const QString &path : candidates) {
        int err = 0;
        const int fd = connectTo(path, &err);
        if (fd < 0) {
            // Left behind by a Paint.with.me that ended without cleaning up.
            if (err == ECONNREFUSED) QFile::remove(path);
            continue;
        }
        ::close(fd);
        const QJsonObject answer = call(path, request, kOpenTimeoutMs);
        if (answer.value("ok").toBool()) return true;
    }
    return false;
}

InstanceLink::InstanceLink(QObject *parent) : QObject(parent) {
    m_path = socketPathFor(ownPid());
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(m_path);   // left by an earlier process with this id
    if (!m_server->listen(m_path))
        qWarning("Paint.with.me: cannot listen on %s: %s", qPrintable(m_path),
                 qPrintable(m_server->errorString()));
    connect(m_server, &QLocalServer::newConnection, this, &InstanceLink::onNewConnection);
}

InstanceLink::~InstanceLink() { close(); }

bool InstanceLink::isListening() const { return m_server && m_server->isListening(); }

void InstanceLink::close() {
    if (m_server && m_server->isListening()) {
        m_server->close();
        QFile::remove(m_path);
    }
}

void InstanceLink::markActive() {
    if (!isListening()) return;
    QSaveFile file(folder() + QStringLiteral("/active"));
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QByteArray::number(ownPid()) + '\n');
    file.commit();
}

void InstanceLink::reply(QLocalSocket *socket, const QJsonObject &answer) {
    if (!socket || socket->state() != QLocalSocket::ConnectedState) return;
    socket->write(QJsonDocument(answer).toJson(QJsonDocument::Compact) + '\n');
    socket->flush();
}

void InstanceLink::onNewConnection() {
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            QByteArray buffer = socket->property("pwmBuffer").toByteArray() + socket->readAll();
            qsizetype end;
            while ((end = buffer.indexOf('\n')) >= 0) {
                const QJsonDocument json = QJsonDocument::fromJson(buffer.left(end));
                buffer.remove(0, end + 1);
                socket->setProperty("pwmBuffer", buffer);
                if (json.isObject()) emit requestReceived(json.object(), socket);
                else reply(socket, QJsonObject{{"ok", false}, {"error", "not JSON"}});
            }
            socket->setProperty("pwmBuffer", buffer);
        });
    }
}
